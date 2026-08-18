#include "application/logic_controller.h"
#include "helpers/git_repository_fixture.h"
#include "infrastructure/vcs_operation_executor.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/switch_dialog_element.h"
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

    gitman::switch_candidate make_candidate(const std::u8string_view display, const gitman::switch_candidate_kind kind, const bool requires_tracking = false)
    {
        gitman::switch_candidate candidate {};
        candidate.kind = kind;
        candidate.display_name = display;
        candidate.target = display;
        candidate.requires_tracking_branch = requires_tracking;
        return candidate;
    }

    // 문서와 준비된 카드(현재 branch main), 열린 switch dialog까지 갖춘 controller다.
    struct dialog_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };
        std::uint64_t candidates_operation_id { 0 };

        dialog_fixture()
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
            local.result.snapshot.current_reference = u8"main";
            local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
            controller.handle(std::move(local));
            submitter.requests.clear();

            controller.handle(gitman::begin_switch_intent { gitman::project_id { u8"alpha" } });
            REQUIRE(submitter.requests.size() == 1u);
            candidates_operation_id = submitter.requests.front().operation_id;
        }

        void deliver_candidates(std::vector<gitman::switch_candidate> candidates, const bool stale = false)
        {
            gitman::switch_candidates_event event {};
            event.operation_id = candidates_operation_id;
            event.id.value = u8"alpha";
            event.result.candidates = std::move(candidates);
            event.result.stale = stale;
            controller.handle(std::move(event));
        }
    };

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

TEST_CASE("Opening the switch dialog queries candidates and shows the loading state", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::query_switch_candidates);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog.has_value());
    REQUIRE(view->switch_dialog->loading);
    REQUIRE(view->switch_dialog->can_confirm == false);
}

TEST_CASE("Delivered candidates fill the dialog and late results are discarded", "[logic][switch-ui]")
{
    dialog_fixture fixture {};

    // 다른 조회의 늦은 결과는 무시된다.
    gitman::switch_candidates_event stale_event {};
    stale_event.operation_id = fixture.candidates_operation_id + 7;
    stale_event.id.value = u8"alpha";
    stale_event.result.candidates.push_back(make_candidate(u8"ghost", gitman::switch_candidate_kind::git_local_branch));
    fixture.controller.handle(std::move(stale_event));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->loading);

    fixture.deliver_candidates(
        { make_candidate(u8"origin/feature", gitman::switch_candidate_kind::git_remote_branch, true), make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) }, true);
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->loading == false);
    REQUIRE(view->switch_dialog->stale);
    REQUIRE(view->switch_dialog->candidates.size() == 2u);
    // remote group이 먼저다 (provider가 만든 순서를 그대로 쓴다).
    REQUIRE(view->switch_dialog->candidates.front().kind == gitman::switch_candidate_kind::git_remote_branch);
}

TEST_CASE("Selecting the current reference blocks the confirm with a message", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"main", gitman::switch_candidate_kind::git_local_branch), make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) });

    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->can_confirm == false);
        REQUIRE(view->switch_dialog->message.empty() == false);
    }

    fixture.controller.handle(gitman::select_switch_candidate_intent { 1 });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->can_confirm);
        REQUIRE(view->switch_dialog->confirm_label == u8"전환 실행");
    }

    // 검증에 걸린 후보로는 확인해도 명령이 만들어지지 않는다 (REQ-007).
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("A tracking branch candidate needs the two step confirmation", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"origin/feature", gitman::switch_candidate_kind::git_remote_branch, true) });
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->confirm_label == u8"브랜치 만들고 전환");

    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    // 첫 확인은 실행하지 않고 안내만 띄운다.
    REQUIRE(fixture.submitter.requests.empty());
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->confirm_label == u8"생성 확인");
        REQUIRE(view->switch_dialog->message.empty() == false);
    }

    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& request { fixture.submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::switch_to);
    REQUIRE(request.switch_target.has_value());
    REQUIRE(request.switch_target->tracking_branch_confirmed);
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->executing);
}

TEST_CASE("A rejected switch keeps the dialog open with the reason and an executed one closes it", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) });
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request request { fixture.submitter.requests.front() };

    SECTION("재검증 거부는 dialog에 사유를 남긴다 (REQ-007)")
    {
        gitman::change_completed_event rejected {};
        rejected.operation_id = request.operation_id;
        rejected.id = request.project.id;
        rejected.kind = gitman::operation_kind::switch_to;
        rejected.result.executed = false;
        rejected.result.rejected_by = gitman::switch_rejection::working_tree_unsafe;
        fixture.controller.handle(std::move(rejected));

        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog.has_value());
        REQUIRE(view->switch_dialog->executing == false);
        REQUIRE(view->switch_dialog->message.empty() == false);
    }

    SECTION("실행된 전환은 성패와 관계없이 dialog를 닫는다")
    {
        gitman::change_completed_event executed {};
        executed.operation_id = request.operation_id;
        executed.id = request.project.id;
        executed.kind = gitman::operation_kind::switch_to;
        executed.result.executed = true;
        executed.result.succeeded = true;
        executed.result.snapshot.availability = gitman::repository_availability::ready;
        executed.result.snapshot.current_reference = u8"develop";
        fixture.controller.handle(std::move(executed));
        REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog.has_value() == false);
    }

    SECTION("취소 intent는 dialog를 닫는다")
    {
        fixture.controller.handle(gitman::cancel_switch_dialog_intent {});
        REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog.has_value() == false);
    }
}

TEST_CASE("The switch dialog tree wires rows and buttons and routes escape and the wheel", "[ui][switch-ui]")
{
    gitman::view_snapshot view {};
    view.document_path = u8"C:\\work\\p.version-list";
    view.window_width = 800.0f;
    view.window_height = 600.0f;
    view.scale = 1.0f;
    gitman::card_view_model card {};
    card.id.value = u8"alpha";
    card.display_name = u8"alpha";
    view.cards.push_back(std::move(card));

    gitman::switch_dialog_view dialog {};
    dialog.card.value = u8"alpha";
    dialog.title = u8"alpha";
    dialog.loading = false;
    dialog.candidates.push_back(make_candidate(u8"origin/feature", gitman::switch_candidate_kind::git_remote_branch, true));
    dialog.candidates.push_back(make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch));
    dialog.selected = { 1u };
    dialog.can_confirm = true;
    dialog.confirm_label = u8"전환 실행";
    view.switch_dialog = { std::move(dialog) };

    const auto tree { gitman::ui::build_ui_tree(view) };
    REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::switch_dialog }) != nullptr);

    {
        const gitman::logic_message message { first_logic_message(click(*tree, gitman::ui::switch_dialog_item_id(0))) };
        const auto* const intent { std::get_if<gitman::select_switch_candidate_intent>(&message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->index == 0u);
    }
    {
        const gitman::logic_message message { first_logic_message(click(*tree, { gitman::ui::ui_element_kind::switch_dialog_confirm, gitman::project_id { u8"alpha" } })) };
        REQUIRE(std::get_if<gitman::confirm_switch_intent>(&message) != nullptr);
    }
    {
        const gitman::logic_message message { first_logic_message(click(*tree, { gitman::ui::ui_element_kind::switch_dialog_cancel, gitman::project_id { u8"alpha" } })) };
        REQUIRE(std::get_if<gitman::cancel_switch_dialog_intent>(&message) != nullptr);
    }
    {
        const gitman::logic_message message { first_logic_message(click(*tree, { gitman::ui::ui_element_kind::switch_dialog })) };
        REQUIRE(std::get_if<gitman::cancel_switch_dialog_intent>(&message) != nullptr);
    }

    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);
    const gitman::logic_message escape_message { first_logic_message(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape })) };
    REQUIRE(std::get_if<gitman::cancel_switch_dialog_intent>(&escape_message) != nullptr);

    const gitman::logic_message wheel_message { first_logic_message(controller.process(gitman::ui::mouse_wheel_event { 400.0f, 300.0f, -120.0f })) };
    REQUIRE(std::get_if<gitman::switch_dialog_scroll_intent>(&wheel_message) != nullptr);
}

#define REQUIRE_GIT_AVAILABLE(fixture)                                                                                                                                                                 \
    if ((fixture).available() == false)                                                                                                                                                                \
    SKIP("호스트에 사용할 수 있는 Git이 없어 통합 test를 건너뜁니다")

TEST_CASE("An executor switch creates the confirmed tracking branch on a real repository", "[executor][switch-ui][integration]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { fixture.make_bare_repository(u8"remote") };
    const std::u8string writer { fixture.make_repository(u8"writer") };
    fixture.write_file(writer, u8"a.txt", "first\n");
    fixture.git(writer, { u8"add", u8"." });
    fixture.git(writer, { u8"commit", u8"-m", u8"first" });
    fixture.git(writer, { u8"remote", u8"add", u8"origin", remote });
    fixture.git(writer, { u8"push", u8"-u", u8"origin", u8"main" });
    fixture.git(writer, { u8"switch", u8"-c", u8"feature" });
    fixture.git(writer, { u8"commit", u8"--allow-empty", u8"-m", u8"feature work" });
    fixture.git(writer, { u8"push", u8"-u", u8"origin", u8"feature" });
    fixture.git(writer, { u8"switch", u8"main" });
    fixture.git(fixture.path_of(u8""), { u8"clone", remote, u8"reader" });
    const std::u8string reader { fixture.path_of(u8"reader") };
    REQUIRE(fixture.failures().empty());

    null_project_store store {};
    gitman::vcs_operation_executor executor { store, fixture.runner(), fixture.probe(), {} };
    std::vector<gitman::logic_message> emitted {};
    const auto emit { [&emitted](gitman::logic_message message) { emitted.push_back(std::move(message)); } };

    gitman::operation_request request {};
    request.operation_id = 60;
    request.generation = 1;
    request.kind = gitman::operation_kind::query_switch_candidates;
    request.project.id.value = u8"reader";
    request.project.path.original = reader;
    request.project.path.normalized = reader;
    request.project.hint = gitman::vcs_hint::git;
    request.settings.git_executable = fixture.tool().executable;
    executor.execute(request, emit);

    REQUIRE(emitted.size() == 1u);
    const auto* const candidates { std::get_if<gitman::switch_candidates_event>(&emitted.front()) };
    REQUIRE(candidates != nullptr);
    const gitman::switch_candidate* feature { nullptr };
    for (const gitman::switch_candidate& candidate : candidates->result.candidates)
        if (candidate.display_name == u8"origin/feature")
            feature = &candidate;
    REQUIRE(feature != nullptr);
    REQUIRE(feature->requires_tracking_branch);

    // dialog의 두 단계 확인을 거친 것과 같은 상태로 실행한다.
    gitman::switch_candidate confirmed { *feature };
    confirmed.tracking_branch_confirmed = true;
    emitted.clear();
    request.operation_id = 61;
    request.kind = gitman::operation_kind::switch_to;
    request.switch_target = { std::move(confirmed) };
    executor.execute(request, emit);

    REQUIRE(emitted.empty() == false);
    const auto* const final_event { std::get_if<gitman::change_completed_event>(&emitted.back()) };
    REQUIRE(final_event != nullptr);
    REQUIRE(final_event->result.executed);
    REQUIRE(final_event->result.succeeded);
    REQUIRE(final_event->result.snapshot.current_reference == u8"feature");
}
