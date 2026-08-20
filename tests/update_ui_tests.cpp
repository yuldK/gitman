#include "application/logic_controller.h"
#include "helpers/git_repository_fixture.h"
#include "infrastructure/vcs_operation_executor.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

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

    // update 경로는 store를 쓰지 않지만 executor 생성에는 계약 구현이 필요하다.
    class null_project_store final : public gitman::project_store
    {
    public:
        [[nodiscard]] gitman::project_store_load_result load(std::u8string_view) noexcept override
        {
            return {};
        }

        [[nodiscard]] gitman::project_store_load_result load_backup(std::u8string_view) noexcept override
        {
            return {};
        }

        [[nodiscard]] gitman::project_store_save_result save(std::u8string_view, const gitman::workspace_document&, const gitman::workspace_revision_token&) noexcept override
        {
            return {};
        }
    };

    // 문서와 준비된 카드 하나를 갖춘 controller다.
    struct overlay_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        overlay_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            gitman::project_definition project {};
            project.id.value = u8"alpha";
            project.display_name = u8"alpha";
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
            local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
            controller.handle(std::move(local));
            submitter.requests.clear();
        }
    };

    gitman::view_snapshot make_card_view(const gitman::card_view_model& card)
    {
        gitman::view_snapshot view {};
        view.document_path = u8"C:\\work\\p.version-list";
        view.window_width = 800.0f;
        view.window_height = 600.0f;
        view.scale = 1.0f;
        view.cards.push_back(card);
        return view;
    }

    gitman::card_view_model make_ready_card(const gitman::repository_kind kind)
    {
        gitman::card_view_model card {};
        card.id.value = u8"alpha";
        card.display_name = u8"alpha";
        card.kind = kind;
        card.state = gitman::card_view_state::ready;
        card.can_change = true;
        return card;
    }

    gitman::logic_message first_logic_message(const std::vector<gitman::ui::input_action>& actions)
    {
        for (const gitman::ui::input_action& action : actions)
            if (const auto* const message { std::get_if<gitman::logic_message>(&action) }; message != nullptr)
                return *message;
        return gitman::logic_message { gitman::shutdown_message {} };
    }

    std::vector<gitman::ui::input_action> click(const gitman::ui::ui_tree& tree, const gitman::ui::ui_element_id& id)
    {
        const gitman::ui::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const gitman::ui::ui_action* const action { element->action(gitman::ui::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }
} // namespace

TEST_CASE("Update requests take the submodule choice from the document settings", "[logic][update-ui]")
{
    // 확인 overlay 없이 곧바로 실행되고, submodule 여부는 문서 settings가 정한다
    // (2026-08-20 검수). 기본은 ADR-003의 off다.
    overlay_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::update);
    REQUIRE(fixture.submitter.requests.front().options.update_submodules == false);
}

TEST_CASE("Update requests honor an enabled submodule setting", "[logic][update-ui]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    gitman::document_loaded_event loaded {};
    gitman::workspace_document document {};
    document.document_path = u8"C:\\work\\p.version-list";
    document.settings.update_submodules = true;
    gitman::project_definition project {};
    project.id.value = u8"alpha";
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
    local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
    controller.handle(std::move(local));
    submitter.requests.clear();

    controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(submitter.requests.size() == 1u);
    REQUIRE(submitter.requests.front().options.update_submodules);
}

TEST_CASE("The card update button routes by kind and running state", "[ui][update-ui]")
{
    SECTION("준비된 카드는 종류와 무관하게 곧바로 update를 요청한다")
    {
        for (const gitman::repository_kind kind : { gitman::repository_kind::git, gitman::repository_kind::subversion })
        {
            const auto tree { gitman::ui::build_ui_tree(make_card_view(make_ready_card(kind))) };
            const gitman::logic_message message { first_logic_message(click(*tree, { gitman::ui::ui_element_kind::card_update, gitman::project_id { u8"alpha" } })) };
            const auto* const intent { std::get_if<gitman::request_update_intent>(&message) };
            REQUIRE(intent != nullptr);
            REQUIRE(intent->id.value == u8"alpha");
        }
    }

    SECTION("실행 중에는 중지 버튼이 되어 취소를 보낸다")
    {
        gitman::card_view_model card { make_ready_card(gitman::repository_kind::git) };
        card.can_change = false;
        card.busy = true;
        card.change_running = true;
        const auto tree { gitman::ui::build_ui_tree(make_card_view(card)) };
        const gitman::logic_message message { first_logic_message(click(*tree, { gitman::ui::ui_element_kind::card_update, gitman::project_id { u8"alpha" } })) };
        REQUIRE(std::get_if<gitman::cancel_operation_intent>(&message) != nullptr);
    }

    SECTION("준비되지 않은 카드의 update 버튼은 비활성이다")
    {
        gitman::card_view_model card { make_ready_card(gitman::repository_kind::git) };
        card.can_change = false;
        const auto tree { gitman::ui::build_ui_tree(make_card_view(card)) };
        REQUIRE(tree->find({ gitman::ui::ui_element_kind::card_update, gitman::project_id { u8"alpha" } })->enabled() == false);
    }
}

#define REQUIRE_GIT_AVAILABLE(fixture)                                                                                                                                                                 \
    if ((fixture).available() == false)                                                                                                                                                                \
    SKIP("호스트에 사용할 수 있는 Git이 없어 통합 test를 건너뜁니다")

TEST_CASE("An executor update fast forwards a real repository and streams its log", "[executor][update-ui][integration]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    // writer가 remote에 새 커밋을 밀어 reader가 behind가 되게 한다.
    const std::u8string remote { fixture.make_bare_repository(u8"remote") };
    const std::u8string writer { fixture.make_repository(u8"writer") };
    fixture.write_file(writer, u8"a.txt", "first\n");
    fixture.git(writer, { u8"add", u8"." });
    fixture.git(writer, { u8"commit", u8"-m", u8"first" });
    fixture.git(writer, { u8"remote", u8"add", u8"origin", remote });
    fixture.git(writer, { u8"push", u8"-u", u8"origin", u8"main" });
    fixture.git(fixture.path_of(u8""), { u8"clone", remote, u8"reader" });
    const std::u8string reader { fixture.path_of(u8"reader") };
    fixture.write_file(writer, u8"b.txt", "second\n");
    fixture.git(writer, { u8"add", u8"." });
    fixture.git(writer, { u8"commit", u8"-m", u8"second" });
    fixture.git(writer, { u8"push", u8"origin", u8"main" });
    REQUIRE(fixture.failures().empty());

    null_project_store store {};
    gitman::vcs_operation_executor executor { store, fixture.runner(), fixture.probe(), {} };

    gitman::operation_request request {};
    request.operation_id = 42;
    request.generation = 1;
    request.kind = gitman::operation_kind::update;
    request.project.id.value = u8"reader";
    request.project.path.original = reader;
    request.project.path.normalized = reader;
    request.project.hint = gitman::vcs_hint::git;
    request.settings.git_executable = fixture.tool().executable;

    std::vector<gitman::logic_message> emitted {};
    executor.execute(request, [&emitted](gitman::logic_message message) { emitted.push_back(std::move(message)); });

    REQUIRE(emitted.empty() == false);
    const auto* const final_event { std::get_if<gitman::change_completed_event>(&emitted.back()) };
    REQUIRE(final_event != nullptr);
    REQUIRE(final_event->operation_id == 42u);
    REQUIRE(final_event->result.executed);
    REQUIRE(final_event->result.succeeded);
    // 실행 직후 재조회한 상태가 함께 온다 (plan 5.2).
    REQUIRE(final_event->result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(final_event->result.snapshot.working_tree.state == gitman::working_tree_state::clean);

    // pull까지의 프로세스 출력이 로그 event로 흘렀다.
    bool found_log { false };
    for (std::size_t index = 0; index + 1 < emitted.size(); ++index)
    {
        const auto* const log { std::get_if<gitman::operation_log_event>(&emitted[index]) };
        REQUIRE(log != nullptr);
        if (log->entries.empty() == false)
            found_log = true;
    }
    REQUIRE(found_log);
}
