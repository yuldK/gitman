#include "application/logic_controller.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/discovery_dialog_element.h"
#include "presentation/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
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

    gitman::discovery_candidate make_candidate(const std::u8string_view name, const gitman::repository_kind kind, const gitman::discovery_exclusion exclusion = gitman::discovery_exclusion::none)
    {
        gitman::discovery_candidate candidate {};
        candidate.directory_name = name;
        candidate.absolute_path = std::u8string { u8"C:\\work\\scan\\" } + std::u8string { name };
        candidate.normalized_path = candidate.absolute_path;
        candidate.kind = kind;
        candidate.exclusion = exclusion;
        return candidate;
    }

    // 문서(활성 카드 alpha)와 열린 탐색 dialog까지 갖춘 controller다. 열자마자
    // 제출되는 query_local과 탐색 요청은 기록에 남는다.
    struct discovery_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };
        std::uint64_t scan_operation_id { 0 };

        discovery_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";

            gitman::project_definition alpha {};
            alpha.id.value = u8"alpha";
            alpha.display_name = u8"alpha";
            alpha.path.original = u8"C:\\work\\alpha";
            alpha.path.normalized = alpha.path.original;
            document.projects.push_back(std::move(alpha));

            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));

            gitman::query_completed_event local {};
            local.id.value = u8"alpha";
            local.generation = 1;
            local.final_event = true;
            local.result.snapshot.project.value = u8"alpha";
            local.result.snapshot.kind = gitman::repository_kind::git;
            local.result.snapshot.availability = gitman::repository_availability::ready;
            controller.handle(std::move(local));
            submitter.requests.clear();

            controller.handle(gitman::begin_discovery_intent { u8"C:\\work\\scan" });
            REQUIRE(submitter.requests.size() == 1u);
            scan_operation_id = submitter.requests.front().operation_id;
        }

        void deliver_candidates(std::vector<gitman::discovery_candidate> candidates, const bool completed = true)
        {
            gitman::discovery_completed_event event {};
            event.operation_id = scan_operation_id;
            event.result.completed = completed;
            event.result.candidates = std::move(candidates);
            controller.handle(std::move(event));
        }
    };

    std::vector<gitman::ui::input_action> click(const gitman::ui::ui_tree& tree, const gitman::ui::ui_element_id& id)
    {
        const gitman::ui::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const gitman::ui::ui_action* const action { element->action(gitman::ui::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }
} // namespace

TEST_CASE("Opening the discovery dialog submits a scan with the document copy", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    const gitman::operation_request& scan { fixture.submitter.requests.front() };
    REQUIRE(scan.kind == gitman::operation_kind::discover_projects);
    REQUIRE(scan.scan_root == u8"C:\\work\\scan");
    REQUIRE(scan.document.has_value());
    REQUIRE(scan.document->projects.size() == 1u);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->discovery_dialog.has_value());
    REQUIRE(view->discovery_dialog->loading);
    REQUIRE(view->discovery_dialog->can_confirm == false);
}

TEST_CASE("The discovery dialog does not open without a document", "[logic][discovery-ui]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::begin_discovery_intent { u8"C:\\work\\scan" });

    REQUIRE(submitter.requests.empty());
    REQUIRE(controller.make_view_snapshot()->discovery_dialog.has_value() == false);
}

TEST_CASE("Delivered candidates check selectable rows by default and late results are discarded", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};

    gitman::discovery_completed_event late {};
    late.operation_id = fixture.scan_operation_id + 100u;
    fixture.controller.handle(std::move(late));
    REQUIRE(fixture.controller.make_view_snapshot()->discovery_dialog->loading);

    fixture.deliver_candidates({
        make_candidate(u8"repo-a", gitman::repository_kind::git),
        make_candidate(u8"plain", gitman::repository_kind::unknown, gitman::discovery_exclusion::not_a_repository),
        make_candidate(u8"repo-b", gitman::repository_kind::subversion),
    });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->discovery_dialog->loading == false);
    REQUIRE(view->discovery_dialog->rows.size() == 3u);
    REQUIRE(view->discovery_dialog->rows[0].checked);
    REQUIRE(view->discovery_dialog->rows[1].checked == false);
    REQUIRE(view->discovery_dialog->rows[2].checked);
    REQUIRE(view->discovery_dialog->can_confirm);
}

TEST_CASE("An incomplete scan keeps the dialog open with a Korean message", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({}, false);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->discovery_dialog.has_value());
    REQUIRE(view->discovery_dialog->message.empty() == false);
    REQUIRE(view->discovery_dialog->can_confirm == false);
}

TEST_CASE("Toggling flips only selectable candidates", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({
        make_candidate(u8"repo-a", gitman::repository_kind::git),
        make_candidate(u8"taken", gitman::repository_kind::git, gitman::discovery_exclusion::already_registered),
    });

    fixture.controller.handle(gitman::toggle_discovery_candidate_intent { 0 });
    fixture.controller.handle(gitman::toggle_discovery_candidate_intent { 1 });
    fixture.controller.handle(gitman::toggle_discovery_candidate_intent { 9 });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->discovery_dialog->rows[0].checked == false);
    REQUIRE(view->discovery_dialog->rows[1].checked == false);
    // 전부 해제되면 등록 버튼이 비활성이고 확인 intent도 등록을 만들지 않는다.
    REQUIRE(view->discovery_dialog->can_confirm == false);
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_discovery_intent {});
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("Confirming submits the selected candidates with the document and revision", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({
        make_candidate(u8"repo-a", gitman::repository_kind::git),
        make_candidate(u8"repo-b", gitman::repository_kind::git),
    });
    fixture.controller.handle(gitman::toggle_discovery_candidate_intent { 1 });
    fixture.submitter.requests.clear();

    fixture.controller.handle(gitman::confirm_discovery_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& request { fixture.submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::register_projects);
    REQUIRE(request.document.has_value());
    REQUIRE(request.discovery_selection.size() == 1u);
    REQUIRE(request.discovery_selection.front().directory_name == u8"repo-a");

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->discovery_dialog->executing);
    REQUIRE(view->discovery_dialog->can_confirm == false);
}

TEST_CASE("Confirming while a document save is pending shows guidance instead of a conflict", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"repo-a", gitman::repository_kind::git) });

    // 경로 표시 토글이 문서 저장을 만든다. 그 저장이 끝나기 전의 등록 시도다.
    fixture.controller.handle(gitman::toggle_path_display_intent {});
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_discovery_intent {});

    REQUIRE(fixture.submitter.requests.empty());
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->discovery_dialog->executing == false);
    REQUIRE(view->discovery_dialog->message.empty() == false);
}

TEST_CASE("A successful registration adopts the document and adds only the new cards", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"repo-a", gitman::repository_kind::git) });
    fixture.controller.handle(gitman::confirm_discovery_intent {});
    const std::uint64_t register_id { fixture.submitter.requests.back().operation_id };
    fixture.submitter.requests.clear();

    gitman::projects_registered_event event {};
    event.operation_id = register_id;
    gitman::workspace_document registered {};
    registered.document_path = u8"C:\\work\\p.version-list";
    gitman::project_definition alpha {};
    alpha.id.value = u8"alpha";
    alpha.path.original = u8"C:\\work\\alpha";
    alpha.path.normalized = alpha.path.original;
    registered.projects.push_back(std::move(alpha));
    gitman::project_definition added {};
    added.id.value = u8"repo-a";
    added.display_name = u8"repo-a";
    added.path.original = u8"C:\\work\\scan\\repo-a";
    added.path.normalized = added.path.original;
    registered.projects.push_back(std::move(added));
    event.document = { std::move(registered) };
    event.revision = { gitman::workspace_revision_token {} };
    fixture.controller.handle(std::move(event));

    // dialog는 닫히고 새 카드의 로컬 조회 하나만 제출된다. 기존 카드는 그대로다.
    REQUIRE(fixture.controller.make_view_snapshot()->discovery_dialog.has_value() == false);
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::query_local);
    REQUIRE(fixture.submitter.requests.front().project.id.value == u8"repo-a");

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->cards.size() == 2u);
    // 기존 카드의 조회 결과가 보존된다 (install_document를 거치지 않는다).
    for (const gitman::card_view_model& card : view->cards)
        if (card.id.value == u8"alpha")
            REQUIRE(card.state != gitman::card_view_state::loading);
}

TEST_CASE("A failed registration keeps the dialog with the diagnostic message", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"repo-a", gitman::repository_kind::git) });
    fixture.controller.handle(gitman::confirm_discovery_intent {});
    const std::uint64_t register_id { fixture.submitter.requests.back().operation_id };

    gitman::projects_registered_event event {};
    event.operation_id = register_id;
    gitman::diagnostic value {};
    value.severity = gitman::diagnostic_severity::error;
    value.message = u8"다른 프로세스가 문서를 수정했습니다.";
    event.diagnostics.push_back(std::move(value));
    fixture.controller.handle(std::move(event));

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->discovery_dialog.has_value());
    REQUIRE(view->discovery_dialog->executing == false);
    REQUIRE(view->discovery_dialog->message == u8"다른 프로세스가 문서를 수정했습니다.");
    // 실패 후 그대로 다시 시도할 수 있다.
    REQUIRE(view->discovery_dialog->can_confirm);
}

TEST_CASE("Cancelling the dialog cancels a running scan", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    const gitman::process_cancellation_token token { fixture.submitter.requests.front().token };
    REQUIRE(token.cancelled() == false);

    fixture.controller.handle(gitman::cancel_discovery_dialog_intent {});
    REQUIRE(token.cancelled());
    REQUIRE(fixture.controller.make_view_snapshot()->discovery_dialog.has_value() == false);
}

TEST_CASE("The dialog refuses to close while a registration is executing", "[logic][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"repo-a", gitman::repository_kind::git) });
    fixture.controller.handle(gitman::confirm_discovery_intent {});

    fixture.controller.handle(gitman::cancel_discovery_dialog_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->discovery_dialog.has_value());
}

TEST_CASE("The discovery dialog elements register toggles and actions", "[ui][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({
        make_candidate(u8"repo-a", gitman::repository_kind::git),
        make_candidate(u8"taken", gitman::repository_kind::git, gitman::discovery_exclusion::already_registered),
    });
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::discovery_dialog }) != nullptr);

    // 후보 행 클릭은 index를 담은 toggle intent다.
    const std::vector<gitman::ui::input_action> toggle { click(*tree, gitman::ui::discovery_dialog_item_id(0)) };
    REQUIRE(toggle.size() == 1u);
    const auto* const toggle_message { std::get_if<gitman::logic_message>(&toggle.front()) };
    REQUIRE(toggle_message != nullptr);
    const auto* const toggle_intent { std::get_if<gitman::toggle_discovery_candidate_intent>(toggle_message) };
    REQUIRE(toggle_intent != nullptr);
    REQUIRE(toggle_intent->index == 0u);

    // 제외 후보 행은 비활성이다.
    const gitman::ui::ui_element* const excluded { tree->find(gitman::ui::discovery_dialog_item_id(1)) };
    REQUIRE(excluded != nullptr);
    REQUIRE(excluded->enabled() == false);

    const std::vector<gitman::ui::input_action> confirm { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::discovery_dialog_confirm }) };
    REQUIRE(confirm.size() == 1u);
    const auto* const confirm_message { std::get_if<gitman::logic_message>(&confirm.front()) };
    REQUIRE(confirm_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::confirm_discovery_intent>(*confirm_message));
}

TEST_CASE("The toolbar discover button requests the folder picker only with a document", "[ui][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.controller.handle(gitman::cancel_discovery_dialog_intent {});
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    const std::vector<gitman::ui::input_action> actions { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::toolbar_discover }) };
    REQUIRE(actions.size() == 1u);
    const auto* const command { std::get_if<gitman::ui::ui_command>(&actions.front()) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == gitman::ui::ui_command::show_discovery_folder_picker);

    gitman::view_snapshot empty {};
    empty.window_width = 800.0f;
    empty.window_height = 600.0f;
    empty.scale = 1.0f;
    const auto empty_tree { gitman::ui::build_ui_tree(empty) };
    const gitman::ui::ui_element* const button { empty_tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::toolbar_discover }) };
    REQUIRE(button != nullptr);
    REQUIRE(button->enabled() == false);
}

TEST_CASE("Escape closes the discovery dialog and the wheel scrolls its list", "[ui][discovery-ui]")
{
    discovery_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"repo-a", gitman::repository_kind::git) });
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    gitman::ui::interaction_controller interaction {};
    interaction.set_tree(tree);

    const std::vector<gitman::ui::input_action> escape { interaction.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape }) };
    REQUIRE(escape.size() == 1u);
    const auto* const escape_message { std::get_if<gitman::logic_message>(&escape.front()) };
    REQUIRE(escape_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_discovery_dialog_intent>(*escape_message));

    const std::vector<gitman::ui::input_action> wheel { interaction.process(gitman::ui::mouse_wheel_event { 400.0f, 300.0f, -120.0f }) };
    REQUIRE(wheel.size() == 1u);
    const auto* const wheel_message { std::get_if<gitman::logic_message>(&wheel.front()) };
    REQUIRE(wheel_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::discovery_dialog_scroll_intent>(*wheel_message));
}
