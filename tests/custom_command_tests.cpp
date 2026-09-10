#include "application/logic_controller.h"
#include "helpers/ui_test_support.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/script_command_builder.h"
#include "infrastructure/vcs_operation_executor.h"
#include "presentation/ui/card_element.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/status_glyphs.h"

#include "generated/codicon_names.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace gitman::testing;

namespace {
    namespace ui = gitman::ui;

    class recording_submitter final : public gitman::operation_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::operation_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::operation_request> requests {};
    };

    // executor 조립에만 필요한 최소 store다. 사용자 명령 경로는 문서를 건드리지 않는다.
    class idle_project_store final : public gitman::project_store
    {
    public:
        [[nodiscard]] gitman::project_store_load_result load(std::u8string_view) noexcept override
        {
            return {};
        }

        [[nodiscard]] gitman::project_store_save_result save(std::u8string_view, const gitman::workspace_document&, const gitman::workspace_revision_token&) noexcept override
        {
            return {};
        }
    };

    gitman::custom_command make_command(const std::u8string_view id, const std::u8string_view script)
    {
        gitman::custom_command command {};
        command.id = id;
        command.label = u8"빌드 테스트";
        command.script = script;
        return command;
    }

    // 문서 하나와 카드 하나(alpha, git·준비 완료)를 갖춘 controller다. 명령 목록은
    // 전역 앱 설정 대신 문서가 정의한 것으로 넣는다 — 문서 override 경로까지 함께 본다.
    struct command_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        explicit command_fixture(std::vector<gitman::custom_command> commands)
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });

            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            document.commands.custom = { std::move(commands) };
            gitman::project_definition project {};
            project.id.value = u8"alpha";
            project.display_name = u8"알파";
            project.path.original = u8"C:\\work\\alpha";
            project.path.normalized = project.path.original;
            document.projects.push_back(std::move(project));
            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));

            gitman::query_completed_event local {};
            local.id.value = u8"alpha";
            local.generation = 1;
            local.final_event = true;
            local.result.snapshot.project.value = u8"alpha";
            local.result.snapshot.kind = gitman::repository_kind::git;
            local.result.snapshot.availability = gitman::repository_availability::ready;
            local.result.snapshot.current_reference = u8"main";
            local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
            controller.handle(std::move(local));
            submitter.requests.clear();
        }
    };

    // snapshot을 값으로 들고 카드를 찾는다. `make_view_snapshot`이 매번 새 객체를
    // 내므로 참조를 넘기면 수명이 끊긴다.
    std::vector<gitman::card_command_view> card_commands(const gitman::logic_controller& controller, const std::u8string_view id)
    {
        const auto view { controller.make_view_snapshot() };
        for (const gitman::card_view_model& card : view->cards)
            if (card.id.value == id)
                return card.commands;
        return {};
    }
} // namespace

TEST_CASE("The generated Codicon table is sorted and finds names and aliases", "[ui][commands][codicon]")
{
    // 이분 탐색이 성립하려면 표의 순서가 `std::u8string_view`의 사전식 비교와 같아야
    // 한다. 생성 script가 그것을 지키는지 여기서 확인한다.
    REQUIRE(gitman::ui::generated::codicon_names.size() > 500u);
    REQUIRE(std::ranges::is_sorted(gitman::ui::generated::codicon_names, {}, &gitman::ui::generated::codicon_name_entry::name));

    const char32_t fallback { U'\uFFFD' };
    REQUIRE(ui::codicon_for_configured_name(u8"run-all", fallback) != fallback);
    REQUIRE(ui::codicon_for_configured_name(u8"terminal", fallback) != fallback);
    // 별칭도 찾아야 한다. `sync`와 `warning`은 mapping.json에서 별칭이다.
    REQUIRE(ui::codicon_for_configured_name(u8"sync", fallback) != fallback);
    REQUIRE(ui::codicon_for_configured_name(u8"warning", fallback) != fallback);

    REQUIRE(ui::codicon_for_configured_name(u8"없는-아이콘", fallback) == fallback);
    REQUIRE(ui::codicon_for_configured_name(u8"", fallback) == fallback);
    // 접두사만 같은 이름을 잘못 집지 않는다.
    REQUIRE(ui::codicon_for_configured_name(u8"add-", fallback) == fallback);
}

TEST_CASE("A card carries the commands that pass its filters", "[logic][commands]")
{
    gitman::custom_command everywhere { make_command(u8"build", u8"scripts/build.py") };
    gitman::custom_command git_only { make_command(u8"git-only", u8"scripts/git.py") };
    git_only.vcs = gitman::command_target_vcs::git;
    gitman::custom_command svn_only { make_command(u8"svn-only", u8"scripts/svn.py") };
    svn_only.vcs = gitman::command_target_vcs::subversion;
    gitman::custom_command other_card { make_command(u8"other", u8"scripts/other.py") };
    other_card.projects = { std::u8string { u8"beta" } };

    const command_fixture fixture { { everywhere, git_only, svn_only, other_card } };
    const std::vector<gitman::card_command_view> commands { card_commands(fixture.controller, u8"alpha") };

    REQUIRE(commands.size() == 2u);
    REQUIRE(commands[0].id == u8"build");
    REQUIRE(commands[1].id == u8"git-only");
    REQUIRE(commands[0].label == u8"빌드 테스트");
    REQUIRE(commands[0].enabled);
}

TEST_CASE("Running a card command submits the definition to the worker", "[logic][commands]")
{
    gitman::custom_command command { make_command(u8"build", u8"scripts/build.py") };
    command.arguments = { std::u8string { u8"--release" } };
    command_fixture fixture { { command } };

    fixture.controller.handle(gitman::run_custom_command_intent { gitman::project_id { u8"alpha" }, u8"build" });

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& request { fixture.submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::run_custom_command);
    REQUIRE(request.project.id.value == u8"alpha");
    REQUIRE(request.command.has_value());
    REQUIRE(request.command->id == u8"build");
    REQUIRE(request.command->arguments.size() == 1u);
    // 카드 단위 취소가 되어야 하므로 전역 token이 아니라 작업별 token이다.
    REQUIRE_FALSE(request.token.cancelled());

    // 실행 중에는 카드가 바쁘고 경과 시간이 흐른다 (REQ-019와 같은 배선).
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->cards.front().busy);
    REQUIRE(view->cards.front().operation_started_at.has_value());
    REQUIRE(view->cards.front().commands.front().enabled == false);
}

TEST_CASE("A command that no longer matches the card is refused", "[logic][commands]")
{
    // 메뉴를 띄운 뒤 문서가 바뀌었거나 카드 종류가 확정되어 필터에서 빠질 수 있다.
    gitman::custom_command svn_only { make_command(u8"svn-only", u8"scripts/svn.py") };
    svn_only.vcs = gitman::command_target_vcs::subversion;
    command_fixture fixture { { svn_only } };

    fixture.controller.handle(gitman::run_custom_command_intent { gitman::project_id { u8"alpha" }, u8"svn-only" });
    REQUIRE(fixture.submitter.requests.empty());

    fixture.controller.handle(gitman::run_custom_command_intent { gitman::project_id { u8"alpha" }, u8"모르는-명령" });
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("A finished command reports its exit code and follows refresh_after", "[logic][commands]")
{
    gitman::custom_command command { make_command(u8"build", u8"scripts/build.py") };
    command.refresh_after = false;
    command_fixture fixture { { command } };

    fixture.controller.handle(gitman::run_custom_command_intent { gitman::project_id { u8"alpha" }, u8"build" });
    const gitman::operation_request request { fixture.submitter.requests.front() };
    fixture.submitter.requests.clear();

    gitman::custom_command_completed_event completed {};
    completed.operation_id = request.operation_id;
    completed.id = request.project.id;
    completed.command_id = u8"build";
    completed.command_label = u8"빌드 테스트";
    completed.executed = true;
    completed.succeeded = false;
    completed.exit_code = { 3 };
    completed.refresh_after = false;
    fixture.controller.handle(std::move(completed));

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE_FALSE(view->cards.front().busy);
    REQUIRE(view->cards.front().last_finished_at.has_value());
    // `refresh_after`가 꺼져 있으면 조회를 잇지 않는다 — 스크립트가 저장소를
    // 건드렸는지 앱이 모르므로 매번 원격까지 다시 보지 않는다.
    REQUIRE(fixture.submitter.requests.empty());

    const gitman::operation_log_buffer* const log { fixture.controller.card_log(gitman::project_id { u8"alpha" }) };
    REQUIRE(log != nullptr);
    REQUIRE(log->records().back().entry.severity == gitman::diagnostic_severity::error);
    REQUIRE(log->records().back().entry.text.find(u8"3") != std::u8string::npos);
}

TEST_CASE("A successful command with refresh_after queries the card again", "[logic][commands]")
{
    command_fixture fixture { { make_command(u8"build", u8"scripts/build.py") } };
    fixture.controller.handle(gitman::run_custom_command_intent { gitman::project_id { u8"alpha" }, u8"build" });
    const gitman::operation_request request { fixture.submitter.requests.front() };
    fixture.submitter.requests.clear();

    gitman::custom_command_completed_event completed {};
    completed.operation_id = request.operation_id;
    completed.id = request.project.id;
    completed.command_id = u8"build";
    completed.executed = true;
    completed.succeeded = true;
    completed.refresh_after = true;
    fixture.controller.handle(std::move(completed));

    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::refresh);
}

TEST_CASE("The card face shows only the commands marked for it", "[ui][commands]")
{
    gitman::view_snapshot view { make_view(1) };
    gitman::card_command_view surfaced {};
    surfaced.id = u8"build";
    surfaced.label = u8"빌드";
    surfaced.icon = u8"run-all";
    surfaced.show_on_card = true;
    gitman::card_command_view menu_only {};
    menu_only.id = u8"clean";
    menu_only.label = u8"정리";
    view.cards.front().commands = { surfaced, menu_only };

    const auto tree { make_tree(view) };
    const std::u8string owner { ui::card_command_owner(u8"card-0", u8"build") };
    const luil::ui_element* const button { find(*tree, ui::kind_card_custom_command, owner) };
    REQUIRE(button != nullptr);
    REQUIRE(button->tooltip() == std::u8string { u8"빌드" });
    // 메뉴에만 나오는 명령은 카드 표면에 버튼을 만들지 않는다.
    REQUIRE(find(*tree, ui::kind_card_custom_command, ui::card_command_owner(u8"card-0", u8"clean")) == nullptr);

    // 사용자 명령은 기본 버튼 왼쪽이다 — 좁은 창에서 새로 고침이 먼저 살아남는다.
    REQUIRE(button->bounds().x < bounds_of(*tree, ui::kind_card_refresh, u8"card-0").x);
}

TEST_CASE("Clicking a card command button runs that command", "[ui][commands][interaction]")
{
    gitman::view_snapshot view { make_view(2) };
    gitman::card_command_view command {};
    command.id = u8"build";
    command.label = u8"빌드";
    command.show_on_card = true;
    view.cards.front().commands = { command };
    view.cards.back().commands = { command };

    const auto tree { make_tree(view) };
    interaction_harness harness { tree };

    const auto actions { click(harness, bounds_of(*tree, ui::kind_card_custom_command, ui::card_command_owner(u8"card-1", u8"build"))) };
    const auto* const intent { as_intent<gitman::run_custom_command_intent>(actions) };
    REQUIRE(intent != nullptr);
    // 같은 명령이 여러 카드에 있어도 누른 카드가 실행된다 — element id의 owner가
    // 카드 id와 명령 id를 함께 담기 때문이다.
    REQUIRE(intent->id.value == u8"card-1");
    REQUIRE(intent->command_id == u8"build");
}

TEST_CASE("A disabled card command produces nothing", "[ui][commands][interaction]")
{
    gitman::view_snapshot view { make_view(1) };
    gitman::card_command_view command {};
    command.id = u8"build";
    command.label = u8"빌드";
    command.show_on_card = true;
    command.enabled = false;
    view.cards.front().commands = { command };

    const auto tree { make_tree(view) };
    interaction_harness harness { tree };

    const luil::ui_element* const button { find(*tree, ui::kind_card_custom_command, ui::card_command_owner(u8"card-0", u8"build")) };
    REQUIRE(button != nullptr);
    REQUIRE(button->enabled() == false);
    REQUIRE(click(harness, button->bounds()).empty());
}

TEST_CASE("The executor runs the script through its interpreter", "[infrastructure][commands][executor]")
{
    idle_project_store store {};
    fake_process_runner runner {};
    runner.set_default_response({ gitman::process_completion::exited, 0, u8"빌드 완료\n", {} });
    fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Python312\\python.exe");

    gitman::vcs_tool_environment environment {};
    environment.path_environment = u8"C:\\Python312";
    environment.system_directory = u8"C:\\Windows\\System32";
    gitman::vcs_operation_executor executor { store, runner, probe, environment };

    gitman::operation_request request {};
    gitman::custom_command command {};
    command.id = u8"build";
    command.label = u8"빌드 테스트";
    // 문서 폴더 기준 상대 경로도 풀어야 한다.
    command.script = u8"scripts\\build.py";
    command.arguments = { std::u8string { u8"--release" } };
    request.operation_id = 7;
    request.kind = gitman::operation_kind::run_custom_command;
    request.document_path = u8"C:\\work\\p.version-list";
    request.project.id.value = u8"alpha";
    request.project.display_name = u8"알파";
    request.project.path.original = u8"C:\\work\\alpha";
    request.project.path.normalized = request.project.path.original;
    request.project.hint = gitman::vcs_hint::git;
    request.command = { std::move(command) };

    std::vector<gitman::logic_message> messages {};
    executor.execute(request, [&messages](gitman::logic_message message) { messages.push_back(std::move(message)); });

    REQUIRE(runner.request_count() == 1u);
    const gitman::process_request& process { runner.request(0) };
    REQUIRE(process.executable == u8"C:\\Python312\\python.exe");
    REQUIRE(process.arguments.size() == 2u);
    REQUIRE(process.arguments[0] == u8"C:\\work\\scripts\\build.py");
    REQUIRE(process.arguments[1] == u8"--release");
    // 기본 작업 폴더는 저장소다. C++ 프로젝트를 빌드하는 명령이 이것을 쓴다.
    REQUIRE(process.working_directory == u8"C:\\work\\alpha");

    bool has_repository_path { false };
    for (const gitman::process_environment_override& value : process.environment_overrides)
        if (value.name == gitman::script_environment_repository_path && value.value == std::optional<std::u8string> { u8"C:\\work\\alpha" })
            has_repository_path = true;
    REQUIRE(has_repository_path);

    // 마지막 event는 완료다. 그 앞에 실행 명령줄과 출력이 로그로 나간다.
    REQUIRE(messages.size() >= 2u);
    const auto* const completed { std::get_if<gitman::custom_command_completed_event>(&messages.back()) };
    REQUIRE(completed != nullptr);
    REQUIRE(completed->operation_id == 7u);
    REQUIRE(completed->command_id == u8"build");
    REQUIRE(completed->executed);
    REQUIRE(completed->succeeded);

    const auto* const log { std::get_if<gitman::operation_log_event>(&messages.front()) };
    REQUIRE(log != nullptr);
    REQUIRE(log->entries.front().kind == gitman::log_entry_kind::lifecycle);
    // 문서 신뢰 게이트를 두지 않기로 한 대신 무엇이 실행되는지 로그로 남긴다.
    REQUIRE(log->entries.front().text.find(u8"python.exe") != std::u8string::npos);
    REQUIRE(log->entries.front().text.find(u8"build.py") != std::u8string::npos);
}

TEST_CASE("A missing interpreter fails the command without running anything", "[infrastructure][commands][executor]")
{
    idle_project_store store {};
    fake_process_runner runner {};
    const fake_vcs_file_probe probe {};

    gitman::vcs_tool_environment environment {};
    environment.path_environment = u8"C:\\empty";
    gitman::vcs_operation_executor executor { store, runner, probe, environment };

    gitman::operation_request request {};
    gitman::custom_command command {};
    command.id = u8"build";
    command.label = u8"빌드";
    command.script = u8"C:\\work\\build.py";
    request.operation_id = 9;
    request.kind = gitman::operation_kind::run_custom_command;
    request.project.id.value = u8"alpha";
    request.project.path.normalized = u8"C:\\work\\alpha";
    request.command = { std::move(command) };

    std::vector<gitman::logic_message> messages {};
    executor.execute(request, [&messages](gitman::logic_message message) { messages.push_back(std::move(message)); });

    REQUIRE(runner.request_count() == 0u);
    REQUIRE(messages.size() == 1u);
    const auto* const completed { std::get_if<gitman::custom_command_completed_event>(&messages.back()) };
    REQUIRE(completed != nullptr);
    REQUIRE_FALSE(completed->executed);
    REQUIRE_FALSE(completed->succeeded);
    REQUIRE(completed->diagnostics.front().code == gitman::diagnostic_code::script_interpreter_unavailable);
}
