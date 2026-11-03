#include "application/cancellation_registry.h"
#include "application/function_execution_pool.h"
#include "application/logic_controller.h"
#include "application/process_request.h"
#include "domain/function_trust.h"
#include "infrastructure/function_execution_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 기능 버튼의 logic 경로다 (function-buttons-design 6.1 T6). UI는 아직 없으므로 intent와
// worker event를 직접 넣고 제출된 요청, 카드 로그, 기능 상태(`function_state`)를 본다.
namespace {
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

    class recording_function_submitter final : public gitman::function_run_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::function_run_request request) override
        {
            if (reject_submissions)
                return false;
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::function_run_request> requests {};
        bool reject_submissions { false };
    };

    gitman::project_definition make_project(const std::u8string_view id)
    {
        gitman::project_definition project {};
        project.id.value = id;
        project.display_name = id;
        project.path.original = std::u8string { u8"C:\\work\\" } + std::u8string { id };
        project.path.normalized = project.path.original;
        // 문서를 읽을 때 resolver가 채우는 값이다. 기본값(`unchecked`)이면 `${projectRoot}`를
        // 쓰는 기능이 모두 비활성이 된다 (4.8의 5).
        project.path.state = gitman::configured_path_state::available;
        return project;
    }

    gitman::function_button make_button(const std::u8string_view id, const std::u8string_view label, const std::u8string_view program = u8"C:\\Tools\\build.exe")
    {
        gitman::function_button button {};
        button.id = id;
        button.label = label;
        button.program = program;
        return button;
    }

    gitman::query_completed_event make_local_result(const std::u8string_view id, const std::uint64_t generation, const std::u8string_view branch = u8"main")
    {
        gitman::query_completed_event event {};
        event.id.value = id;
        event.generation = generation;
        event.final_event = true;
        event.result.snapshot.project.value = id;
        event.result.snapshot.kind = gitman::repository_kind::git;
        event.result.snapshot.availability = gitman::repository_availability::ready;
        event.result.snapshot.current_reference = branch;
        event.result.snapshot.repository_root = std::u8string { u8"C:\\work\\" } + std::u8string { id };
        event.result.snapshot.local_revision = u8"a1b2c3d";
        event.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
        return event;
    }

    gitman::function_buttons_definition make_definition(std::vector<gitman::function_button> buttons)
    {
        gitman::function_buttons_definition definition {};
        for (gitman::function_button& button : buttons)
            definition.buttons.emplace_back(std::move(button));
        return definition;
    }

    gitman::function_variable make_variable(const std::u8string_view name, const std::u8string_view value)
    {
        return gitman::function_variable { std::u8string { name }, std::u8string { value } };
    }

    gitman::diagnostic make_diagnostic(const gitman::diagnostic_code code, const gitman::diagnostic_severity severity, const std::u8string_view message)
    {
        gitman::diagnostic value {};
        value.code = code;
        value.severity = severity;
        value.message = message;
        return value;
    }

    // 문서를 열고 기능 실행 경계를 묶은 controller다. 로컬 조회 결과는 test가 필요할 때 넣는다
    // (조회 전 판정을 보기 위해서다).
    struct function_fixture
    {
        recording_submitter submitter {};
        recording_function_submitter functions {};
        // 등록과 되돌림을 보려고 보관소를 밖에서 준다.
        gitman::cancellation_registry registry {};
        gitman::logic_controller controller { submitter, registry };
        std::map<std::u8string, std::u8string> environment {};

        function_fixture(std::vector<gitman::project_definition> projects, std::vector<gitman::function_button> buttons, const std::u8string_view document_path = u8"C:\\work\\projects.version-list")
            : function_fixture { std::move(projects), make_definition(std::move(buttons)), document_path }
        {
        }

        function_fixture(std::vector<gitman::project_definition> projects, gitman::function_buttons_definition definition, const std::u8string_view document_path = u8"C:\\work\\projects.version-list")
        {
            controller.bind_function_execution(functions
                , gitman::make_function_run_services(
                    [this](const std::u8string_view name) -> std::optional<std::u8string>
                    {
                        const auto found { environment.find(std::u8string { name }) };
                        if (found == environment.end())
                            return std::nullopt;
                        return found->second;
                    }
                )
            );

            // 이 파일은 신뢰를 기록한 정의의 실행 경로를 본다. 신뢰 확인 dialog(T7)는
            // `logic_function_trust_tests.cpp`가 본다. 앱 설정을 읽을 때 이 문서의 해시를 싣는다.
            controller.start();
            gitman::app_settings_loaded_event settings {};
            settings.operation_id = submitter.requests.back().operation_id;
            settings.settings.trusted_function_definitions.push_back({ std::u8string { document_path }, gitman::function_trust_digest(definition, projects) });
            controller.handle(std::move(settings));

            controller.handle(gitman::open_document_intent { std::u8string { document_path } });

            gitman::document_loaded_event event {};
            gitman::workspace_document document {};
            document.document_path = document_path;
            document.projects = std::move(projects);
            document.function_buttons = std::move(definition);
            event.document = { std::move(document) };
            controller.handle(std::move(event));
            submitter.requests.clear();
        }

        void query(const std::u8string_view id, const std::u8string_view branch = u8"main")
        {
            controller.handle(make_local_result(id, 1u, branch));
            submitter.requests.clear();
        }

        void run(const std::u8string_view id, const std::u8string_view button)
        {
            controller.handle(gitman::run_function_intent { gitman::project_id { std::u8string { id } }, std::u8string { button } });
            // cmd가 부를 배치 파일은 worker가 읽는다 (S3). 파일이 없다는 답이라 S3 전의 판정 그대로다.
            for (auto request = submitter.requests.rbegin(); request != submitter.requests.rend(); ++request)
            {
                if (request->kind != gitman::operation_kind::inspect_cmd_target || request->operation_id <= answered_cmd_inspection || request->cmd_target.has_value() == false)
                    continue;
                answered_cmd_inspection = request->operation_id;
                gitman::cmd_target_inspected_event event {};
                event.operation_id = request->operation_id;
                event.facts.query = *request->cmd_target;
                controller.handle(std::move(event));
                break;
            }
        }

        std::uint64_t answered_cmd_inspection { 0 };

        // 떠 있는 확인 dialog다. cmd 보정의 최종 승인(D30)을 볼 때 쓴다.
        [[nodiscard]] std::optional<gitman::function_confirm_dialog_view> dialog() const
        {
            return controller.make_view_snapshot()->function_confirm_dialog;
        }

        void confirm()
        {
            controller.handle(gitman::confirm_function_run_intent {});
        }

        [[nodiscard]] gitman::logic_controller::function_button_state state(const std::u8string_view id, const std::u8string_view button) const
        {
            const std::optional<gitman::logic_controller::function_button_state> value { controller.function_state(gitman::project_id { std::u8string { id } }, button) };
            REQUIRE(value.has_value());
            return *value;
        }

        [[nodiscard]] const gitman::operation_log_buffer& log(const std::u8string_view id) const
        {
            const gitman::operation_log_buffer* const buffer { controller.card_log(gitman::project_id { std::u8string { id } }) };
            REQUIRE(buffer != nullptr);
            return *buffer;
        }

        [[nodiscard]] const gitman::operation_log_entry& last_log(const std::u8string_view id) const
        {
            REQUIRE(log(id).records().empty() == false);
            return log(id).records().back().entry;
        }

        [[nodiscard]] const gitman::card_view_model& card(const std::u8string_view id)
        {
            view = controller.make_view_snapshot();
            const gitman::card_view_model* found { nullptr };
            for (const gitman::card_view_model& model : view->cards)
            {
                if (model.id.value == id)
                    found = &model;
            }
            REQUIRE(found != nullptr);
            return *found;
        }

        void started(const gitman::function_run_request& request, const std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now())
        {
            controller.handle(gitman::function_started_event { request.id, request.run_id, at });
        }

        void completed(const gitman::function_run_request& request, const gitman::function_run_outcome outcome, const std::optional<std::int32_t> exit_code = std::nullopt, const std::chrono::milliseconds duration = std::chrono::milliseconds { 0 }, std::vector<gitman::diagnostic> diagnostics = {})
        {
            gitman::function_completed_event event {};
            event.id = request.id;
            event.run_id = request.run_id;
            event.outcome = outcome;
            event.exit_code = exit_code;
            event.duration = duration;
            event.diagnostics = std::move(diagnostics);
            controller.handle(std::move(event));
        }

        std::shared_ptr<const gitman::view_snapshot> view {};
    };

    bool contains(const std::u8string_view text, const std::u8string_view part)
    {
        return text.find(part) != std::u8string_view::npos;
    }
} // namespace

TEST_CASE("A capture run submits a request, logs the display command line and leaves busy alone", "[function_buttons][t6]")
{
    gitman::function_button button { make_button(u8"build", u8"빌드") };
    button.args = { u8"--key", u8"${env:API_KEY}", u8"${projectRoot}" };
    function_fixture fixture { { make_project(u8"alpha") }, { button } };
    fixture.environment[u8"API_KEY"] = u8"secret-value";
    fixture.query(u8"alpha");

    fixture.run(u8"alpha", u8"build");

    REQUIRE(fixture.functions.requests.size() == 1u);
    const gitman::function_run_request& request { fixture.functions.requests.front() };
    REQUIRE(request.id.value == u8"alpha");
    REQUIRE(request.token.cancellable());
    const gitman::process_request& command { std::get<gitman::process_request>(request.command) };
    REQUIRE(command.executable == u8"C:\\Tools\\build.exe");
    REQUIRE(command.arguments == std::vector<std::u8string> { u8"--key", u8"secret-value", u8"C:\\work\\alpha" });
    REQUIRE(command.working_directory == u8"C:\\work\\alpha");
    REQUIRE(command.timeout == std::optional<std::chrono::milliseconds> { std::chrono::seconds { 3600 } });

    // 시작 줄은 표시용 명령줄이다. env 값은 이름으로만 남는다 (3.6).
    const gitman::operation_log_entry& start { fixture.last_log(u8"alpha") };
    REQUIRE(start.text == u8"[빌드] 실행: \"C:\\Tools\\build.exe\" --key ${env:API_KEY} C:\\work\\alpha");
    REQUIRE(contains(start.text, u8"secret-value") == false);

    // 기능 실행은 `busy`를 세우지 않는다. VCS 요청도 나가지 않는다 (B8).
    REQUIRE(fixture.card(u8"alpha").busy == false);
    REQUIRE(fixture.submitter.requests.empty());
    const gitman::logic_controller::function_button_state state { fixture.state(u8"alpha", u8"build") };
    REQUIRE(state.running);
    REQUIRE(state.enabled);
}

TEST_CASE("A completed capture logs the end line from the started-based duration and chains no refresh", "[function_buttons][t6]")
{
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드") } };
    fixture.query(u8"alpha");
    fixture.controller.handle(gitman::select_card_intent { { gitman::project_id { u8"alpha" } } });
    fixture.run(u8"alpha", u8"build");
    const gitman::function_run_request request { fixture.functions.requests.front() };

    // 헤더 경과 시간은 started가 준 시각이 기준이다. 대기 시간이 섞이지 않는다 (4.5).
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE_FALSE(view->log->change_started_at.has_value());
    }
    const std::chrono::steady_clock::time_point started_at { std::chrono::steady_clock::now() - std::chrono::seconds { 30 } };
    fixture.started(request, started_at);
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->log->change_started_at == std::optional<std::chrono::steady_clock::time_point> { started_at });
        REQUIRE_FALSE(view->log->change_elapsed.has_value());
    }

    // 출력 줄에도 `[라벨]` 접두가 붙는다 (2.3.6).
    gitman::function_log_event output {};
    output.id = request.id;
    output.run_id = request.run_id;
    gitman::operation_log_entry entry {};
    entry.kind = gitman::log_entry_kind::standard_output;
    entry.text = u8"compiling";
    output.entries.push_back(entry);
    fixture.controller.handle(std::move(output));
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] compiling");
    REQUIRE(fixture.last_log(u8"alpha").kind == gitman::log_entry_kind::standard_output);

    fixture.completed(request, gitman::function_run_outcome::exited, 0, std::chrono::milliseconds { 12345 });

    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 종료 코드 0 · 12.3초");
    REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::information);
    // 완료 뒤 refresh가 없다 (B8).
    REQUIRE(fixture.submitter.requests.empty());
    REQUIRE(fixture.card(u8"alpha").busy == false);
    // 헤더는 걸린 시간(started 기준, event의 값)을 세워 둔다.
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE_FALSE(view->log->change_started_at.has_value());
    REQUIRE(view->log->change_elapsed == std::optional<std::chrono::steady_clock::duration> { std::chrono::milliseconds { 12345 } });
    const gitman::logic_controller::function_button_state state { fixture.state(u8"alpha", u8"build") };
    REQUIRE_FALSE(state.running);
    REQUIRE_FALSE(state.failed);
}

TEST_CASE("Only one capture runs per card and other capture buttons are disabled", "[function_buttons][t6]")
{
    gitman::function_button open { make_button(u8"open", u8"열기") };
    open.mode = gitman::function_run_mode::detached;
    function_fixture fixture { { make_project(u8"alpha"), make_project(u8"beta") }, { make_button(u8"build", u8"빌드"), make_button(u8"test", u8"테스트"), open } };
    fixture.query(u8"alpha");
    fixture.query(u8"beta");
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);

    const gitman::logic_controller::function_button_state other { fixture.state(u8"alpha", u8"test") };
    REQUIRE_FALSE(other.enabled);
    REQUIRE(other.disabled_reason == u8"이 카드에서 다른 기능이 실행 중입니다");
    fixture.run(u8"alpha", u8"test");
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[테스트] 이 카드에서 다른 기능이 실행 중입니다");

    // 실행 중인 기능을 다시 눌러도 (`Alt+N`) 아무것도 하지 않는다. 중지는 따로다.
    const std::size_t before { fixture.log(u8"alpha").records().size() };
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.log(u8"alpha").records().size() == before);
    REQUIRE_FALSE(fixture.functions.requests.front().token.cancelled());

    // 다른 카드와 분리 실행은 막지 않는다.
    fixture.run(u8"beta", u8"test");
    fixture.run(u8"alpha", u8"open");
    REQUIRE(fixture.functions.requests.size() == 3u);
}

TEST_CASE("A running update refuses a capture run on the same card and the reverse", "[function_buttons][t6]")
{
    gitman::function_button open { make_button(u8"open", u8"열기") };
    open.mode = gitman::function_run_mode::detached;
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드"), open } };
    fixture.query(u8"alpha");

    SECTION("update first")
    {
        fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
        REQUIRE(fixture.submitter.requests.size() == 1u);

        const gitman::logic_controller::function_button_state state { fixture.state(u8"alpha", u8"build") };
        REQUIRE_FALSE(state.enabled);
        REQUIRE(state.disabled_reason == u8"업데이트 중입니다");
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.functions.requests.empty());
        REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::warning);

        // 분리 실행은 배타 대상이 아니다.
        fixture.run(u8"alpha", u8"open");
        REQUIRE(fixture.functions.requests.size() == 1u);
    }

    SECTION("capture first")
    {
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.functions.requests.size() == 1u);

        // 카드 view와 메뉴가 같은 판정을 쓴다. 마지막 거부는 begin_change가 한다.
        REQUIRE_FALSE(fixture.card(u8"alpha").can_change);
        fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
        REQUIRE(fixture.submitter.requests.empty());
        REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::warning);
        REQUIRE(contains(fixture.last_log(u8"alpha").text, u8"빌드"));

        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 0.0f, 0.0f });
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->context_menu.has_value());
        for (const gitman::context_menu_item_view& item : view->context_menu->items)
        {
            if (item.entry == gitman::context_menu_entry::update || item.entry == gitman::context_menu_entry::switch_to)
                REQUIRE_FALSE(item.enabled);
        }

        // 끝나면 다시 update를 받는다.
        fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::exited, 0);
        REQUIRE(fixture.card(u8"alpha").can_change);
    }
}

TEST_CASE("Confirming a switch while a capture runs keeps the dialog open with the reason", "[function_buttons][t6]")
{
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드") } };
    fixture.query(u8"alpha");
    fixture.controller.handle(gitman::begin_switch_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    gitman::switch_candidates_event candidates {};
    candidates.operation_id = fixture.submitter.requests.front().operation_id;
    candidates.id.value = u8"alpha";
    gitman::switch_candidate feature {};
    feature.kind = gitman::switch_candidate_kind::git_local_branch;
    feature.display_name = u8"feature";
    feature.target = u8"refs/heads/feature";
    candidates.result.candidates = { feature };
    fixture.controller.handle(std::move(candidates));
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.submitter.requests.clear();

    // dialog가 열린 뒤 기능을 시작했다. 확인은 전환을 내지 않고 사유를 dialog에 보인다 (4.7).
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.controller.handle(gitman::confirm_switch_intent {});

    REQUIRE(fixture.submitter.requests.empty());
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog.has_value());
    REQUIRE(view->switch_dialog->message == u8"이 카드에서 기능이 실행 중입니다. 끝난 뒤 다시 시도하세요.");

    // 기능이 끝나면 같은 확인으로 전환한다.
    fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::exited, 0);
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::switch_to);
}

TEST_CASE("A refresh in progress does not block a capture run", "[function_buttons][t6]")
{
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드") } };
    fixture.query(u8"alpha");
    fixture.controller.handle(gitman::refresh_card_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(fixture.card(u8"alpha").busy);

    REQUIRE(fixture.state(u8"alpha", u8"build").enabled);
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("A capture run registers its cancellation and closing the document cancels it", "[function_buttons][t6]")
{
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드") } };
    fixture.query(u8"alpha");
    const std::size_t baseline { fixture.registry.size() };

    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.registry.size() == baseline + 1u);
    const gitman::function_run_request request { fixture.functions.requests.front() };

    SECTION("registry cancels without the logic thread")
    {
        fixture.registry.cancel_all();
        REQUIRE(request.token.cancelled());
    }

    // 실행 중이면 닫기·다른 문서 열기는 먼저 묻는다 (T11, 4.10). 확인하면 취소한다.
    SECTION("closing the document")
    {
        fixture.controller.handle(gitman::close_document_intent {});
        REQUIRE(request.token.cancelled() == false);
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(request.token.cancelled());
    }

    SECTION("opening another document")
    {
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
        REQUIRE(request.token.cancelled() == false);
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(request.token.cancelled());
    }

    SECTION("shutdown")
    {
        fixture.controller.handle(gitman::close_intent {});
        REQUIRE(request.token.cancelled());
    }

    SECTION("the stop intent")
    {
        fixture.controller.handle(gitman::stop_function_intent { gitman::project_id { u8"alpha" }, u8"build" });
        REQUIRE(request.token.cancelled());
        fixture.completed(request, gitman::function_run_outcome::cancelled);
        REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 취소됨");
        REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::warning);
        // 사용자 중지는 실패 표식을 남기지 않는다 (2.3.5).
        REQUIRE_FALSE(fixture.state(u8"alpha", u8"build").failed);
    }
}

TEST_CASE("Late function events are discarded by run id", "[function_buttons][t6]")
{
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드") } };
    fixture.query(u8"alpha");
    fixture.run(u8"alpha", u8"build");
    const gitman::function_run_request request { fixture.functions.requests.front() };

    gitman::function_log_event late {};
    late.id = request.id;
    late.run_id = request.run_id;
    late.entries.push_back({});

    SECTION("after completion")
    {
        fixture.completed(request, gitman::function_run_outcome::exited, 0);
        const std::size_t before { fixture.log(u8"alpha").records().size() };
        fixture.controller.handle(gitman::function_log_event { late });
        // executor의 예외 경로가 completed를 한 번 더 보낼 수 있다 (T5).
        fixture.completed(request, gitman::function_run_outcome::failed);
        fixture.started(request);
        REQUIRE(fixture.log(u8"alpha").records().size() == before);
        REQUIRE_FALSE(fixture.state(u8"alpha", u8"build").failed);
    }

    SECTION("after the document is closed and opened again")
    {
        // 실행 중이라 닫기는 확인을 거친다 (T11, 4.10).
        fixture.controller.handle(gitman::close_document_intent {});
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        // 같은 controller에 같은 문서를 다시 연다. 카드 id는 같지만 run id가 다르다.
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\projects.version-list" });
        gitman::document_loaded_event event {};
        gitman::workspace_document document {};
        document.document_path = u8"C:\\work\\projects.version-list";
        document.projects = { make_project(u8"alpha") };
        gitman::function_buttons_definition definition {};
        definition.buttons.emplace_back(make_button(u8"build", u8"빌드"));
        document.function_buttons = std::move(definition);
        event.document = { std::move(document) };
        fixture.controller.handle(std::move(event));
        fixture.query(u8"alpha");

        const std::size_t before { fixture.log(u8"alpha").records().size() };
        fixture.controller.handle(gitman::function_log_event { late });
        fixture.completed(request, gitman::function_run_outcome::exited, 1);
        REQUIRE(fixture.log(u8"alpha").records().size() == before);
        REQUIRE_FALSE(fixture.state(u8"alpha", u8"build").failed);
        // 늦은 completed가 새 문서의 실행 자리를 비우지 않는다.
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.functions.requests.size() == 2u);
        REQUIRE(fixture.state(u8"alpha", u8"build").running);
    }
}

TEST_CASE("A full pool refuses a fifth run with one log line", "[function_buttons][t6]")
{
    std::vector<gitman::project_definition> projects {};
    const std::vector<std::u8string_view> ids { u8"a", u8"b", u8"c", u8"d", u8"e" };
    for (const std::u8string_view id : ids)
        projects.push_back(make_project(id));
    function_fixture fixture { projects, { make_button(u8"build", u8"빌드") } };
    for (const std::u8string_view id : ids)
        fixture.query(id);

    for (std::size_t index = 0; index < gitman::function_execution_capacity; ++index)
        fixture.run(ids[index], u8"build");
    REQUIRE(fixture.functions.requests.size() == gitman::function_execution_capacity);
    const std::size_t registered { fixture.registry.size() };

    fixture.run(u8"e", u8"build");
    REQUIRE(fixture.functions.requests.size() == gitman::function_execution_capacity);
    REQUIRE(fixture.last_log(u8"e").text == u8"[빌드] 동시에 실행할 수 있는 기능은 4개까지입니다");
    REQUIRE(fixture.registry.size() == registered);
    REQUIRE_FALSE(fixture.state(u8"e", u8"build").running);

    // 하나가 끝나면 자리가 난다.
    fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::exited, 0);
    fixture.run(u8"e", u8"build");
    REQUIRE(fixture.functions.requests.size() == gitman::function_execution_capacity + 1u);
}

TEST_CASE("A refused submission rolls back the run and its registration", "[function_buttons][t6]")
{
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드") } };
    fixture.query(u8"alpha");
    const std::size_t baseline { fixture.registry.size() };
    fixture.functions.reject_submissions = true;

    fixture.run(u8"alpha", u8"build");

    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 동시에 실행할 수 있는 기능은 4개까지입니다");
    REQUIRE(fixture.registry.size() == baseline);
    const gitman::logic_controller::function_button_state state { fixture.state(u8"alpha", u8"build") };
    REQUIRE_FALSE(state.running);
    REQUIRE(state.enabled);
    // 남은 run이 update를 막지 않는다.
    REQUIRE(fixture.card(u8"alpha").can_change);

    fixture.functions.reject_submissions = false;
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("A button without runtime variables runs before the repository is queried", "[function_buttons][t6]")
{
    gitman::function_button branch { make_button(u8"branch", u8"브랜치") };
    branch.args = { u8"${branch}" };
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드"), branch } };

    REQUIRE(fixture.state(u8"alpha", u8"build").enabled);
    const gitman::logic_controller::function_button_state runtime { fixture.state(u8"alpha", u8"branch") };
    REQUIRE_FALSE(runtime.enabled);
    REQUIRE(runtime.disabled_reason == u8"저장소를 조회한 뒤에 쓸 수 있습니다");

    fixture.run(u8"alpha", u8"build");
    fixture.run(u8"alpha", u8"branch");
    REQUIRE(fixture.functions.requests.size() == 1u);

    // 조회가 끝나면 런타임 변수를 쓰는 기능도 활성이다.
    fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::exited, 0);
    fixture.query(u8"alpha");
    REQUIRE(fixture.state(u8"alpha", u8"branch").enabled);
}

TEST_CASE("A vcs-scoped button on an auto card waits for the query", "[function_buttons][t6]")
{
    gitman::function_button git { make_button(u8"git", u8"Git 전용") };
    git.vcs = gitman::function_vcs_scope::git;
    gitman::function_button svn { make_button(u8"svn", u8"SVN 전용") };
    svn.vcs = gitman::function_vcs_scope::subversion;
    gitman::project_definition hinted { make_project(u8"hinted") };
    hinted.hint = gitman::vcs_hint::git;
    function_fixture fixture { { make_project(u8"alpha"), hinted }, { git, svn } };

    const gitman::logic_controller::function_button_state before { fixture.state(u8"alpha", u8"git") };
    REQUIRE_FALSE(before.enabled);
    REQUIRE(before.disabled_reason == u8"저장소 종류를 아직 모릅니다");
    fixture.run(u8"alpha", u8"git");
    REQUIRE(fixture.functions.requests.empty());

    // `vcs_hint`가 정해진 카드는 조회 전에도 판정한다.
    REQUIRE(fixture.state(u8"hinted", u8"git").enabled);
    REQUIRE_FALSE(fixture.controller.function_state(gitman::project_id { u8"hinted" }, u8"svn").has_value());

    fixture.query(u8"alpha");
    REQUIRE(fixture.state(u8"alpha", u8"git").enabled);
    REQUIRE_FALSE(fixture.controller.function_state(gitman::project_id { u8"alpha" }, u8"svn").has_value());
}

TEST_CASE("A button that uses the project root is disabled when the path is unavailable", "[function_buttons][t6]")
{
    gitman::project_definition missing { make_project(u8"missing") };
    missing.path.state = gitman::configured_path_state::missing;
    gitman::function_button elsewhere { make_button(u8"elsewhere", u8"다른 폴더") };
    elsewhere.cwd = u8"C:\\Tools";
    function_fixture fixture { { missing }, { make_button(u8"build", u8"빌드"), elsewhere } };

    // 기본 `cwd`가 `${projectRoot}`라 대부분의 기능이 여기에 걸린다.
    const gitman::logic_controller::function_button_state state { fixture.state(u8"missing", u8"build") };
    REQUIRE_FALSE(state.enabled);
    REQUIRE(state.disabled_reason == u8"저장소 경로를 쓸 수 없습니다");
    fixture.run(u8"missing", u8"build");
    REQUIRE(fixture.functions.requests.empty());

    REQUIRE(fixture.state(u8"missing", u8"elsewhere").enabled);
}

TEST_CASE("An excluded card enables only document-sourced buttons", "[function_buttons][t6]")
{
    gitman::project_definition excluded { make_project(u8"excluded") };
    excluded.enabled = false;
    gitman::function_button branch { make_button(u8"branch", u8"브랜치") };
    branch.args = { u8"${branch}" };
    gitman::function_button git { make_button(u8"git", u8"Git 전용") };
    git.vcs = gitman::function_vcs_scope::git;
    function_fixture fixture { { excluded }, { make_button(u8"build", u8"빌드"), branch, git } };

    // 제외된 카드는 조회하지 않으므로 런타임 변수를 쓰는 기능과 `vcs_hint: auto` 카드의
    // vcs 한정 기능은 늘 비활성이다. 사유는 기다려도 풀리지 않는다는 것을 말한다 (D17).
    REQUIRE(fixture.state(u8"excluded", u8"build").enabled);
    const gitman::logic_controller::function_button_state runtime { fixture.state(u8"excluded", u8"branch") };
    REQUIRE_FALSE(runtime.enabled);
    REQUIRE(runtime.disabled_reason == u8"제외된 카드는 저장소를 조회하지 않습니다");
    const gitman::logic_controller::function_button_state scoped { fixture.state(u8"excluded", u8"git") };
    REQUIRE_FALSE(scoped.enabled);
    REQUIRE(scoped.disabled_reason == u8"제외된 카드는 저장소를 조회하지 않습니다");
    fixture.run(u8"excluded", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("A button outside its project scope is absent from the card", "[function_buttons][t6]")
{
    gitman::function_button scoped { make_button(u8"lint", u8"린트") };
    scoped.project_ids = std::vector<std::u8string> { u8"frontend" };
    function_fixture fixture { { make_project(u8"frontend"), make_project(u8"backend") }, { scoped } };

    REQUIRE(fixture.state(u8"frontend", u8"lint").enabled);
    REQUIRE_FALSE(fixture.controller.function_state(gitman::project_id { u8"backend" }, u8"lint").has_value());
    fixture.run(u8"backend", u8"lint");
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.log(u8"backend").records().empty());
}

TEST_CASE("An undefined environment variable refuses the run with one log line and a failure mark", "[function_buttons][t6]")
{
    gitman::function_button button { make_button(u8"lint", u8"린트") };
    button.args = { u8"--key", u8"${env:LINT_KEY}" };
    function_fixture fixture { { make_project(u8"alpha") }, { button } };
    fixture.query(u8"alpha");

    // 판정은 env를 보지 않으므로 버튼은 활성이다. 클릭 시점에 거부한다 (3.7).
    REQUIRE(fixture.state(u8"alpha", u8"lint").enabled);
    const std::size_t before { fixture.log(u8"alpha").records().size() };
    fixture.run(u8"alpha", u8"lint");

    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.log(u8"alpha").records().size() == before + 1u);
    REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::error);
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[린트] 실행하지 않습니다: 정의되지 않은 환경 변수입니다: ${env:LINT_KEY}");
    REQUIRE(fixture.state(u8"alpha", u8"lint").failed);

    // 정의하면 같은 버튼이 실행되고 실패 점이 지워진다.
    fixture.environment[u8"LINT_KEY"] = u8"value";
    fixture.run(u8"alpha", u8"lint");
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE_FALSE(fixture.state(u8"alpha", u8"lint").failed);
}

TEST_CASE("The cmd defence asks before running a corrected runtime value and leaves no failure mark", "[function_buttons][t6][r4]")
{
    gitman::function_button button { make_button(u8"echo", u8"브랜치 출력", u8"C:\\Windows\\System32\\cmd.exe") };
    button.args = { u8"/d", u8"/c", u8"echo", u8"${branch}" };
    function_fixture fixture { { make_project(u8"alpha"), make_project(u8"beta") }, { button } };
    fixture.query(u8"alpha", u8"feature&calc");
    fixture.query(u8"beta", u8"feature");

    // 인자의 `&`는 따옴표로 보정하고(D27) 같은 값이 `GITMAN_BRANCH`로도 나가므로 경고한다. R1은 거부했고 R4(D30)부터는
    // 보정한 명령을 보이고 사용자의 최종 승인을 받는다.
    fixture.run(u8"alpha", u8"echo");
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->mode == gitman::function_confirm_mode::correction);
    REQUIRE_FALSE(fixture.state(u8"alpha", u8"echo").failed);
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    const gitman::process_request& command { std::get<gitman::process_request>(fixture.functions.requests.front().command) };
    REQUIRE(command.command_line == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"echo \"feature&calc\"\"");

    // 메타 문자가 없는 값은 묻지 않고 실행한다.
    fixture.run(u8"beta", u8"echo");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 2u);
}

TEST_CASE("A cmd batch that can read GITMAN_BRANCH with cmd metacharacters runs only after the warning is approved", "[function_buttons][r1][r4]")
{
    // 배치 안의 `echo %GITMAN_BRANCH%`는 gitman이 고칠 수 없다. 인자에 `${branch}`를 쓰지 않아도 경고한다 (D26을 D30이
    // 거부에서 경고로 바꿈).
    gitman::function_button button { make_button(u8"env", u8"배치", u8"C:\\Windows\\System32\\cmd.exe") };
    button.args = { u8"/d", u8"/c", u8"${projectRoot}\\tools\\echo-branch.bat" };
    function_fixture fixture { { make_project(u8"alpha"), make_project(u8"beta") }, { button } };
    fixture.query(u8"alpha", u8"main&whoami");
    fixture.query(u8"beta", u8"main");

    fixture.run(u8"alpha", u8"env");
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.dialog().has_value());
    bool warned { false };
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    for (const gitman::function_confirm_line& line : dialog->items.front().lines)
        warned = warned || (line.warning && contains(line.text, u8"GITMAN_BRANCH"));
    REQUIRE(warned);
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);

    fixture.run(u8"beta", u8"env");
    REQUIRE(fixture.functions.requests.size() == 2u);
}

TEST_CASE("A cmd button in an R&D document folder runs with the cmd command line it logs", "[function_buttons][r1][r4]")
{
    gitman::function_button button { make_button(u8"build", u8"빌드", u8"C:\\Windows\\System32\\cmd.exe") };
    button.args = { u8"/d", u8"/c", u8"${workspaceFolder}\\tools\\build.bat", u8"${projectName}" };
    function_fixture fixture { { make_project(u8"alpha") }, { button }, u8"C:\\R&D\\projects.version-list" };
    fixture.query(u8"alpha");

    // 문서 경로의 `&`를 따옴표 안에 넣은 것도 보정이라 한 번 승인을 받는다.
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.dialog().has_value());
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    const gitman::process_request& command { std::get<gitman::process_request>(fixture.functions.requests.front().command) };
    REQUIRE(command.syntax == gitman::command_line_syntax::cmd);
    REQUIRE(command.arguments == std::vector<std::u8string> { u8"/d", u8"/c", u8"C:\\R&D\\tools\\build.bat", u8"alpha" });
    REQUIRE(command.command_line == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"\"C:\\R&D\\tools\\build.bat\" alpha\"");
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 실행: \"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"\"C:\\R&D\\tools\\build.bat\" alpha\"");
}

TEST_CASE("A program that is not allowed is refused at click time", "[function_buttons][t6]")
{
    gitman::function_button button { make_button(u8"script", u8"스크립트", u8"${projectRoot}\\run.bat") };
    function_fixture fixture { { make_project(u8"alpha") }, { button } };
    fixture.query(u8"alpha");

    fixture.run(u8"alpha", u8"script");
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(contains(fixture.last_log(u8"alpha").text, u8"[스크립트] 실행하지 않습니다: program"));
    REQUIRE(fixture.state(u8"alpha", u8"script").failed);
}

TEST_CASE("A detached button submits a launch request without registering a cancellation", "[function_buttons][t6]")
{
    // 모델에 남은 `timeout_seconds`(T3)는 분리 실행 요청에 자리가 없어 형식으로 쓰이지 않는다.
    // 여기서는 요청 종류, 취소 미등록, 늦게 남는 `시작:` 줄을 본다.
    gitman::function_button open { make_button(u8"open", u8"Unity로 열기", u8"C:\\Program Files\\Unity\\Unity.exe") };
    open.mode = gitman::function_run_mode::detached;
    open.timeout_seconds = 5;
    open.args = { u8"-projectPath", u8"${projectRoot}" };
    gitman::function_button unlimited { make_button(u8"watch", u8"감시") };
    unlimited.timeout_seconds = 0;
    function_fixture fixture { { make_project(u8"alpha") }, { open, unlimited } };
    fixture.query(u8"alpha");
    const std::size_t baseline { fixture.registry.size() };

    fixture.run(u8"alpha", u8"open");
    REQUIRE(fixture.functions.requests.size() == 1u);
    const gitman::function_run_request request { fixture.functions.requests.front() };
    // 분리 실행은 취소 등록을 하지 않는다 (4.1).
    REQUIRE(std::holds_alternative<gitman::process_launch_request>(request.command));
    REQUIRE(fixture.registry.size() == baseline);
    // `시작:` 줄은 결과를 받은 뒤에 남긴다.
    const std::size_t before { fixture.log(u8"alpha").records().size() };
    REQUIRE_FALSE(fixture.state(u8"alpha", u8"open").running);

    fixture.completed(request, gitman::function_run_outcome::launched);
    REQUIRE(fixture.log(u8"alpha").records().size() == before + 1u);
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[Unity로 열기] 시작: \"C:\\Program Files\\Unity\\Unity.exe\" -projectPath C:\\work\\alpha");

    // 출력 수집의 0은 무제한이다 (D18).
    fixture.run(u8"alpha", u8"watch");
    REQUIRE(fixture.functions.requests.size() == 2u);
    REQUIRE_FALSE(std::get<gitman::process_request>(fixture.functions.requests.back().command).timeout.has_value());
}

TEST_CASE("End lines report failures with a mark that selection and a rerun clear", "[function_buttons][t6]")
{
    gitman::function_button detached { make_button(u8"open", u8"열기") };
    detached.mode = gitman::function_run_mode::detached;
    function_fixture fixture { { make_project(u8"alpha"), make_project(u8"beta") }, { make_button(u8"build", u8"빌드"), detached } };
    fixture.query(u8"alpha");
    fixture.query(u8"beta");

    fixture.run(u8"alpha", u8"build");
    fixture.started(fixture.functions.requests.back());
    fixture.completed(fixture.functions.requests.back(), gitman::function_run_outcome::exited, 1, std::chrono::milliseconds { 5000 });
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 종료 코드 1 · 5.0초");
    REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::error);
    REQUIRE(fixture.state(u8"alpha", u8"build").failed);

    // 다시 실행하면 지운다.
    fixture.run(u8"alpha", u8"build");
    REQUIRE_FALSE(fixture.state(u8"alpha", u8"build").failed);
    fixture.completed(fixture.functions.requests.back(), gitman::function_run_outcome::timed_out);
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 시간 초과(3600초)");
    REQUIRE(fixture.state(u8"alpha", u8"build").failed);

    // 카드를 선택하면 지운다.
    fixture.controller.handle(gitman::select_card_intent { { gitman::project_id { u8"alpha" } } });
    REQUIRE_FALSE(fixture.state(u8"alpha", u8"build").failed);

    // 시작 실패는 진단을 사유로 단다. 분리 실행도 같다.
    fixture.run(u8"beta", u8"open");
    gitman::function_completed_event failed {};
    failed.id = fixture.functions.requests.back().id;
    failed.run_id = fixture.functions.requests.back().run_id;
    failed.outcome = gitman::function_run_outcome::start_failed;
    gitman::diagnostic reason {};
    reason.severity = gitman::diagnostic_severity::error;
    reason.message = u8"프로세스를 시작하지 못했습니다.";
    failed.diagnostics.push_back(reason);
    fixture.controller.handle(std::move(failed));
    REQUIRE(fixture.last_log(u8"beta").text == u8"[열기] 시작하지 못했습니다: 프로세스를 시작하지 못했습니다.");
    REQUIRE(fixture.state(u8"beta", u8"open").failed);
}

TEST_CASE("A drive-root document joins workspaceFolder paths that the checks accept", "[function_buttons][t6]")
{
    // T2에서 넘어온 위험이다. 문서가 드라이브 루트에 있으면 `${workspaceFolder}`가 `E:\`라서
    // `${workspaceFolder}\tools`는 `E:\\tools`처럼 구분자가 겹친다. Windows 경로로는 같은 곳이다.
    gitman::project_definition project { make_project(u8"alpha") };
    project.path.original = u8"E:\\alpha";
    project.path.normalized = project.path.original;
    gitman::function_button button { make_button(u8"tool", u8"도구", u8"${workspaceFolder}\\tools\\run.exe") };
    button.cwd = u8"${workspaceFolder}\\work";
    function_fixture fixture { { project }, { button }, u8"E:\\projects.version-list" };
    fixture.query(u8"alpha");

    REQUIRE(fixture.state(u8"alpha", u8"tool").enabled);
    fixture.run(u8"alpha", u8"tool");

    REQUIRE(fixture.functions.requests.size() == 1u);
    const gitman::process_request& command { std::get<gitman::process_request>(fixture.functions.requests.front().command) };
    REQUIRE(command.executable == u8"E:\\\\tools\\run.exe");
    REQUIRE(command.working_directory == u8"E:\\\\work");
    REQUIRE(gitman::validate_process_request(command).empty());
    REQUIRE_FALSE(fixture.state(u8"alpha", u8"tool").failed);
}

TEST_CASE("A run intent without bound function execution is refused with a log line", "[function_buttons][t6]")
{
    recording_submitter submitter {};
    recording_function_submitter functions {};
    gitman::logic_controller controller { submitter };

    SECTION("nothing bound")
    {
    }

    SECTION("a bundle with an empty function is not bound")
    {
        // 빈 함수를 부르면 logic thread에서 `bad_function_call`이 나므로 묶지 않는다.
        gitman::function_run_services services {
            gitman::make_function_run_services(
                [](const std::u8string_view) -> std::optional<std::u8string>
                {
                    return std::nullopt;
                }
            )
        };
        services.mask = nullptr;
        controller.bind_function_execution(functions, std::move(services));
    }

    controller.handle(gitman::open_document_intent { u8"C:\\work\\projects.version-list" });
    gitman::document_loaded_event event {};
    gitman::workspace_document document {};
    document.document_path = u8"C:\\work\\projects.version-list";
    document.projects = { make_project(u8"alpha") };
    gitman::function_buttons_definition definition {};
    definition.buttons.emplace_back(make_button(u8"build", u8"빌드"));
    document.function_buttons = std::move(definition);
    event.document = { std::move(document) };
    controller.handle(std::move(event));

    controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"build" });

    const gitman::operation_log_buffer* const log { controller.card_log(gitman::project_id { u8"alpha" }) };
    REQUIRE(log != nullptr);
    REQUIRE(log->records().size() == 1u);
    REQUIRE(log->records().back().entry.severity == gitman::diagnostic_severity::error);
    REQUIRE(functions.requests.empty());
}

TEST_CASE("User variables resolve per card and a project root inside a value needs the path", "[function_buttons][t6]")
{
    gitman::function_button deploy { make_button(u8"deploy", u8"배포") };
    deploy.args = { u8"${target}" };
    deploy.cwd = u8"C:\\Tools";
    gitman::function_button regional { make_button(u8"regional", u8"지역") };
    regional.args = { u8"${region}" };
    regional.cwd = u8"C:\\Tools";
    gitman::function_button nested { make_button(u8"nested", u8"출력 폴더") };
    nested.args = { u8"${output}" };
    nested.cwd = u8"C:\\Tools";
    gitman::function_buttons_definition definition { make_definition({ deploy, regional, nested }) };
    definition.variables = { make_variable(u8"target", u8"default-server"), make_variable(u8"output", u8"${projectRoot}\\out") };
    gitman::function_project_variables overrides {};
    overrides.project = u8"alpha";
    overrides.variables = std::vector<gitman::function_variable> { make_variable(u8"target", u8"alpha-server"), make_variable(u8"region", u8"kr") };
    definition.project_variables = { overrides };
    gitman::project_definition missing { make_project(u8"gamma") };
    missing.path.state = gitman::configured_path_state::missing;
    function_fixture fixture { { make_project(u8"alpha"), make_project(u8"beta"), missing }, std::move(definition) };

    // 이 카드에서 정의되지 않은 사용자 변수다 (4.8의 3).
    REQUIRE(fixture.state(u8"alpha", u8"regional").enabled);
    const gitman::logic_controller::function_button_state undefined { fixture.state(u8"beta", u8"regional") };
    REQUIRE_FALSE(undefined.enabled);
    REQUIRE(undefined.disabled_reason == u8"이 저장소에 정의되지 않은 변수입니다: ${region}");

    // 사용자 변수 값 안의 `${projectRoot}`도 경로 검사를 부른다 (4.8의 5). 쓰지 않는 기능은 막지 않는다.
    REQUIRE(fixture.state(u8"alpha", u8"nested").enabled);
    const gitman::logic_controller::function_button_state path { fixture.state(u8"gamma", u8"nested") };
    REQUIRE_FALSE(path.enabled);
    REQUIRE(path.disabled_reason == u8"저장소 경로를 쓸 수 없습니다");
    REQUIRE(fixture.state(u8"gamma", u8"deploy").enabled);

    // 카드의 덮어쓰기가 문서 변수보다 앞선다 (3.4).
    fixture.run(u8"alpha", u8"deploy");
    fixture.run(u8"beta", u8"deploy");
    REQUIRE(fixture.functions.requests.size() == 2u);
    REQUIRE(std::get<gitman::process_request>(fixture.functions.requests[0].command).arguments == std::vector<std::u8string> { u8"alpha-server" });
    REQUIRE(std::get<gitman::process_request>(fixture.functions.requests[1].command).arguments == std::vector<std::u8string> { u8"default-server" });
}

TEST_CASE("A running switch disables capture runs with its own reason", "[function_buttons][t6]")
{
    gitman::function_button open { make_button(u8"open", u8"열기") };
    open.mode = gitman::function_run_mode::detached;
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드"), open } };
    fixture.query(u8"alpha");
    fixture.controller.handle(gitman::begin_switch_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    gitman::switch_candidates_event candidates {};
    candidates.operation_id = fixture.submitter.requests.front().operation_id;
    candidates.id.value = u8"alpha";
    gitman::switch_candidate feature {};
    feature.kind = gitman::switch_candidate_kind::git_local_branch;
    feature.display_name = u8"feature";
    feature.target = u8"refs/heads/feature";
    candidates.result.candidates = { feature };
    fixture.controller.handle(std::move(candidates));
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::switch_to);

    const gitman::logic_controller::function_button_state state { fixture.state(u8"alpha", u8"build") };
    REQUIRE_FALSE(state.enabled);
    REQUIRE(state.disabled_reason == u8"전환 중입니다");
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 전환 중입니다");
    // 분리 실행은 배타 대상이 아니다.
    REQUIRE(fixture.state(u8"alpha", u8"open").enabled);
}

TEST_CASE("A relative working directory is refused at click time", "[function_buttons][t6]")
{
    gitman::function_button button { make_button(u8"build", u8"빌드") };
    button.cwd = u8"build";
    function_fixture fixture { { make_project(u8"alpha") }, { button } };
    fixture.query(u8"alpha");

    REQUIRE(fixture.state(u8"alpha", u8"build").enabled);
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 실행하지 않습니다: 작업 폴더(cwd)가 절대 경로가 아닙니다");
    REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::error);
    REQUIRE(fixture.state(u8"alpha", u8"build").failed);
}

TEST_CASE("End lines cover unknown and crash exit codes and runner diagnostics follow them", "[function_buttons][t6]")
{
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드") } };
    fixture.query(u8"alpha");
    const auto run_once = [&fixture]()
    {
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.state(u8"alpha", u8"build").running);
        const std::size_t before { fixture.log(u8"alpha").records().size() };
        return std::pair<gitman::function_run_request, std::size_t> { fixture.functions.requests.back(), before };
    };
    // 끝 줄부터 남은 줄의 글이다.
    const auto lines_from = [&fixture](const std::size_t first)
    {
        std::vector<std::u8string> lines {};
        const auto& records { fixture.log(u8"alpha").records() };
        for (std::size_t index = first; index < records.size(); ++index)
            lines.push_back(records[index].entry.text);
        return lines;
    };

    SECTION("an exit without a code")
    {
        const auto [request, before] { run_once() };
        fixture.completed(request, gitman::function_run_outcome::exited, std::nullopt, std::chrono::milliseconds { 1000 });
        REQUIRE(lines_from(before) == std::vector<std::u8string> { u8"[빌드] 종료 코드를 알 수 없습니다 · 1.0초" });
        REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::error);
        REQUIRE(fixture.state(u8"alpha", u8"build").failed);
    }

    SECTION("a crash code is written in hexadecimal")
    {
        const auto [request, before] { run_once() };
        fixture.completed(request, gitman::function_run_outcome::exited, static_cast<std::int32_t>(0xC0000005u), std::chrono::milliseconds { 200 });
        REQUIRE(lines_from(before) == std::vector<std::u8string> { u8"[빌드] 종료 코드 0xC0000005 · 0.2초" });
        REQUIRE(fixture.state(u8"alpha", u8"build").failed);
    }

    SECTION("warnings on a normal exit follow the end line")
    {
        // 출력 잘림, 손자 정리, sink 예외는 정상 종료에도 붙는다 (4.1, 6.3). information은 남기지 않는다.
        const auto [request, before] { run_once() };
        std::vector<gitman::diagnostic> diagnostics {
            make_diagnostic(gitman::diagnostic_code::process_output_truncated, gitman::diagnostic_severity::warning, u8"출력이 스트림별 캡처 상한을 넘어 이후 내용을 버렸습니다."),
            make_diagnostic(gitman::diagnostic_code::operation_failed, gitman::diagnostic_severity::information, u8"참고"),
            make_diagnostic(gitman::diagnostic_code::operation_failed, gitman::diagnostic_severity::error, u8"출력 sink가 예외를 던져 일부 레코드를 전달하지 못했습니다.")
        };
        fixture.completed(request, gitman::function_run_outcome::exited, 0, std::chrono::milliseconds { 0 }, std::move(diagnostics));

        REQUIRE(lines_from(before) == std::vector<std::u8string> {
            u8"[빌드] 종료 코드 0 · 0.0초",
            u8"[빌드] 출력이 스트림별 캡처 상한을 넘어 이후 내용을 버렸습니다.",
            u8"[빌드] 출력 sink가 예외를 던져 일부 레코드를 전달하지 못했습니다."
        });
        const auto& records { fixture.log(u8"alpha").records() };
        REQUIRE(records[before + 1u].entry.severity == gitman::diagnostic_severity::warning);
        REQUIRE(records[before + 2u].entry.severity == gitman::diagnostic_severity::error);
        // 종료 코드 0이면 진단이 있어도 실패 점은 없다.
        REQUIRE_FALSE(fixture.state(u8"alpha", u8"build").failed);
    }

    SECTION("a failed run uses its first diagnostic once")
    {
        const auto [request, before] { run_once() };
        std::vector<gitman::diagnostic> diagnostics {
            make_diagnostic(gitman::diagnostic_code::operation_failed, gitman::diagnostic_severity::error, u8"첫 사유"),
            make_diagnostic(gitman::diagnostic_code::operation_failed, gitman::diagnostic_severity::warning, u8"둘째 진단")
        };
        fixture.completed(request, gitman::function_run_outcome::failed, std::nullopt, std::chrono::milliseconds { 0 }, std::move(diagnostics));
        REQUIRE(lines_from(before) == std::vector<std::u8string> { u8"[빌드] 실행하지 못했습니다: 첫 사유", u8"[빌드] 둘째 진단" });
        REQUIRE(fixture.state(u8"alpha", u8"build").failed);
    }

    SECTION("timeout and cancellation diagnostics are not repeated")
    {
        const auto [request, before] { run_once() };
        std::vector<gitman::diagnostic> diagnostics {
            make_diagnostic(gitman::diagnostic_code::process_timed_out, gitman::diagnostic_severity::error, u8"제한 시간을 초과해 자식 프로세스를 종료했습니다.")
        };
        fixture.completed(request, gitman::function_run_outcome::timed_out, std::nullopt, std::chrono::milliseconds { 0 }, std::move(diagnostics));
        REQUIRE(lines_from(before) == std::vector<std::u8string> { u8"[빌드] 시간 초과(3600초)" });

        const auto [stopped, stopped_before] { run_once() };
        std::vector<gitman::diagnostic> cancelled {
            make_diagnostic(gitman::diagnostic_code::process_cancelled, gitman::diagnostic_severity::error, u8"취소 요청으로 자식 프로세스를 종료했습니다.")
        };
        fixture.completed(stopped, gitman::function_run_outcome::cancelled, std::nullopt, std::chrono::milliseconds { 0 }, std::move(cancelled));
        REQUIRE(lines_from(stopped_before) == std::vector<std::u8string> { u8"[빌드] 취소됨" });
    }
}

TEST_CASE("Detached runs waiting to launch count toward the pool limit", "[function_buttons][t6]")
{
    gitman::function_button open { make_button(u8"open", u8"열기") };
    open.mode = gitman::function_run_mode::detached;
    function_fixture fixture { { make_project(u8"alpha") }, { make_button(u8"build", u8"빌드"), open } };
    fixture.query(u8"alpha");

    for (std::size_t index = 0; index < gitman::function_execution_capacity; ++index)
        fixture.run(u8"alpha", u8"open");
    REQUIRE(fixture.functions.requests.size() == gitman::function_execution_capacity);

    // 띄우기를 기다리는 분리 실행도 풀의 자리를 쓴다 (4.4).
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.size() == gitman::function_execution_capacity);
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[빌드] 동시에 실행할 수 있는 기능은 4개까지입니다");

    fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::launched);
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.functions.requests.size() == gitman::function_execution_capacity + 1u);
}

TEST_CASE("A running capture stays stoppable when the repository kind changes", "[function_buttons][t6]")
{
    gitman::function_button git { make_button(u8"git", u8"Git 전용") };
    git.vcs = gitman::function_vcs_scope::git;
    function_fixture fixture { { make_project(u8"alpha") }, { git } };
    fixture.query(u8"alpha");
    fixture.run(u8"alpha", u8"git");
    REQUIRE(fixture.functions.requests.size() == 1u);

    // capture 중에 refresh 결과가 와서 `vcs_hint: auto` 카드의 종류가 바뀌었다.
    gitman::query_completed_event changed { make_local_result(u8"alpha", 1u) };
    changed.result.snapshot.kind = gitman::repository_kind::unknown;
    fixture.controller.handle(std::move(changed));

    const gitman::logic_controller::function_button_state running { fixture.state(u8"alpha", u8"git") };
    REQUIRE(running.running);
    REQUIRE(running.enabled);
    fixture.controller.handle(gitman::stop_function_intent { gitman::project_id { u8"alpha" }, u8"git" });
    REQUIRE(fixture.functions.requests.front().token.cancelled());

    fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::cancelled);
    const gitman::logic_controller::function_button_state after { fixture.state(u8"alpha", u8"git") };
    REQUIRE_FALSE(after.enabled);
    REQUIRE(after.disabled_reason == u8"저장소 종류를 알 수 없습니다");
}
