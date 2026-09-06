#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "domain/local_changes.h"
#include "helpers/ui_test_support.h"
#include "presentation/ui/local_changes_dialog_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// 로컬 변경 dialog의 logic과 UI다. 파서·명령 조립·provider·diff 표시 모델은
// `local_changes_domain_tests.cpp`에 있다 (luil 이식 C2-4).
namespace {
    using gitman::testing::as_app_command;
    using gitman::testing::as_intent;
    using gitman::testing::bounds_of;
    using gitman::testing::click;
    using gitman::testing::find;
    using gitman::testing::id_of;
    using gitman::testing::interaction_harness;
    using gitman::testing::make_tree;

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

    // 카드 alpha 하나를 갖춘 controller다. 초기 query_local 요청은 기록에서 지운다.
    struct dialog_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        dialog_fixture()
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
            submitter.requests.clear();
        }
    };

    gitman::local_changes_event make_list_event(const std::uint64_t operation_id, std::vector<gitman::local_change_entry> entries)
    {
        gitman::local_changes_event event {};
        event.operation_id = operation_id;
        event.id.value = u8"alpha";
        event.result.entries = std::move(entries);
        return event;
    }

    // element에 걸린 왼쪽 클릭 액션을 그 자리에서 실행한다 (조립 확인용).
    std::vector<luil::input_action> click_action(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const action { element->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }
} // namespace

TEST_CASE("Opening the dialog queries the list and auto selects the first entry", "[logic][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& list_request { fixture.submitter.requests[0] };
    REQUIRE(list_request.kind == gitman::operation_kind::query_local_changes);
    REQUIRE(list_request.project.id.value == u8"alpha");

    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->local_changes_dialog.has_value());
        REQUIRE(view->local_changes_dialog->loading);
        REQUIRE(view->local_changes_dialog->title == u8"alpha");
    }

    // 목록이 오면 첫 항목이 선택되고 diff 조회가 이어진다.
    std::vector<gitman::local_change_entry> entries {
        { gitman::local_change_kind::modified, u8"a.txt" },
        { gitman::local_change_kind::untracked, u8"new-dir/" },
    };
    fixture.controller.handle(make_list_event(list_request.operation_id, std::move(entries)));

    REQUIRE(fixture.submitter.requests.size() == 2u);
    const gitman::operation_request& diff_request { fixture.submitter.requests[1] };
    REQUIRE(diff_request.kind == gitman::operation_kind::query_file_diff);
    REQUIRE(diff_request.diff_target.has_value());
    REQUIRE(diff_request.diff_target->path == u8"a.txt");

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->local_changes_dialog->loading == false);
    REQUIRE(view->local_changes_dialog->rows.size() == 2u);
    REQUIRE(view->local_changes_dialog->rows[0].badge == u8"수정");
    REQUIRE(view->local_changes_dialog->rows[0].selected);
    // 미추적 디렉터리는 배지 안 folder 아이콘으로 표시한다 (3차 검수: 텍스트 표기
    // 대신 directory 플래그가 아이콘을 정한다).
    REQUIRE(view->local_changes_dialog->rows[1].badge == u8"미추적");
    REQUIRE(view->local_changes_dialog->rows[1].directory);
    REQUIRE(view->local_changes_dialog->diff_loading);
}

TEST_CASE("Diff events fill the pane and selection switches entries", "[logic][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, { { gitman::local_change_kind::modified, u8"a.txt" }, { gitman::local_change_kind::untracked, u8"b.txt" } }));

    gitman::file_diff_event diff {};
    diff.operation_id = fixture.submitter.requests[1].operation_id;
    diff.id.value = u8"alpha";
    diff.result.lines = { u8"+new" };
    diff.result.truncated = true;
    fixture.controller.handle(std::move(diff));

    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->local_changes_dialog->diff_loading == false);
        REQUIRE(view->local_changes_dialog->diff_rows.size() == 1u);
        REQUIRE(view->local_changes_dialog->diff_rows[0].right == u8"new");
        REQUIRE(view->local_changes_dialog->diff_rows[0].has_left == false);
        REQUIRE(view->local_changes_dialog->diff_notice.empty() == false);
    }

    // 다른 행을 고르면 그 항목의 diff 조회가 새로 제출된다. 같은 행은 무시한다.
    fixture.controller.handle(gitman::select_local_change_intent { 1 });
    REQUIRE(fixture.submitter.requests.size() == 3u);
    REQUIRE(fixture.submitter.requests[2].diff_target->path == u8"b.txt");
    fixture.controller.handle(gitman::select_local_change_intent { 1 });
    REQUIRE(fixture.submitter.requests.size() == 3u);

    // 늦게 도착한 이전 diff 결과는 버린다.
    gitman::file_diff_event stale {};
    stale.operation_id = fixture.submitter.requests[1].operation_id;
    stale.result.lines = { u8"-old" };
    fixture.controller.handle(std::move(stale));
    REQUIRE(fixture.controller.make_view_snapshot()->local_changes_dialog->diff_loading);

    // 닫으면 이후 event가 무시된다.
    fixture.controller.handle(gitman::cancel_local_changes_dialog_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->local_changes_dialog.has_value() == false);
}

TEST_CASE("An empty change list shows a notice instead of a diff query", "[logic][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, {}));

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->local_changes_dialog->rows.empty());
    REQUIRE(view->local_changes_dialog->message == u8"표시할 로컬 변경이 없습니다.");
}

TEST_CASE("The dialog tree exposes rows, the diff pane, and close paths", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, { { gitman::local_change_kind::modified, u8"a.txt" }, { gitman::local_change_kind::untracked, u8"b.txt" } }));

    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    REQUIRE(find(*tree, gitman::ui::kind_local_changes_dialog) != nullptr);
    REQUIRE(find(*tree, gitman::ui::kind_local_changes_diff) != nullptr);

    // D2-A: 본체는 luil modal host의 content이고 초점은 그 안에 갇힌다.
    REQUIRE(find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_local_changes) != nullptr);
    REQUIRE(tree->focus_trap() != nullptr);

    // 행 클릭은 선택 intent다.
    const auto selected_actions { click_action(*tree, gitman::ui::local_changes_item_id(1)) };
    const auto* const selected { as_intent<gitman::select_local_change_intent>(selected_actions) };
    REQUIRE(selected != nullptr);
    REQUIRE(selected->index == 1u);

    // 닫기 버튼과 배경(scrim) 클릭은 둘 다 dialog 닫기다.
    REQUIRE(as_intent<gitman::cancel_local_changes_dialog_intent>(click_action(*tree, id_of(gitman::ui::kind_local_changes_dialog_close, u8"alpha"))) != nullptr);
    REQUIRE(
        as_intent<gitman::cancel_local_changes_dialog_intent>(click_action(*tree, id_of(luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_local_changes))) != nullptr);

    // D1-B: 변경 행과 그 안의 열기 아이콘, 본체와 scrim은 Tab 자리가 아니다.
    REQUIRE(tree->find(gitman::ui::local_changes_item_id(1))->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_local_changes_dialog)->tab_stop() == false);
    REQUIRE(find(*tree, luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_local_changes)->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_local_changes_open_vscode, gitman::ui::local_changes_item_id(1).owner)->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_local_changes_open_explorer, gitman::ui::local_changes_item_id(1).owner)->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_local_changes_dialog_close, u8"alpha")->tab_stop());

    interaction_harness harness { tree };

    // 휠은 diff pane 위에서 diff를, 그 밖에서 목록을 스크롤한다.
    const luil::rect_f diff { bounds_of(*tree, gitman::ui::kind_local_changes_diff) };
    REQUIRE(as_intent<gitman::local_changes_diff_scroll_intent>(harness.process(luil::mouse_wheel_event { diff.x + 5.0f, diff.y + 5.0f, -120.0f })) != nullptr);
    REQUIRE(as_intent<gitman::local_changes_scroll_intent>(harness.process(luil::mouse_wheel_event { diff.x + 5.0f, diff.y - 60.0f, -120.0f })) != nullptr);

    // Esc는 dialog 닫기다 (host의 dismiss 액션, D2-A).
    REQUIRE(as_intent<gitman::cancel_local_changes_dialog_intent>(harness.process(luil::key_pressed_event { luil::key_code::escape })) != nullptr);
}

TEST_CASE("A card double click opens the local changes dialog", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    interaction_harness harness { tree };

    const luil::rect_f card { bounds_of(*tree, gitman::ui::kind_card_body, u8"alpha") };

    // 첫 클릭은 선택이고, 임계 시간 안의 두 번째 클릭이 dialog를 연다.
    REQUIRE(as_intent<gitman::select_card_intent>(click(harness, card, 0)) != nullptr);
    const auto open_actions { click(harness, card, 100) };
    const auto* const open { as_intent<gitman::open_local_changes_intent>(open_actions) };
    REQUIRE(open != nullptr);
    REQUIRE(open->id.value == u8"alpha");
}

TEST_CASE("Rows carry untracked flags, absolute paths, and open buttons", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id,
        { { gitman::local_change_kind::modified, u8"src/a.txt" }, { gitman::local_change_kind::untracked, u8"new-dir/" } }));

    const auto view { fixture.controller.make_view_snapshot() };
    // 미추적·디렉터리 표시와 외부 열기용 절대 경로가 행에 실린다.
    REQUIRE(view->local_changes_dialog->rows[0].untracked == false);
    // 상대 경로의 `/` 구분자는 Windows 구분자로 통일된다 (explorer /select 요구).
    REQUIRE(view->local_changes_dialog->rows[0].absolute_path == u8"C:\\work\\alpha\\src\\a.txt");
    REQUIRE(view->local_changes_dialog->rows[1].untracked);
    REQUIRE(view->local_changes_dialog->rows[1].directory);
    REQUIRE(view->local_changes_dialog->rows[1].absolute_path == u8"C:\\work\\alpha\\new-dir");

    const auto tree { make_tree(*view) };

    // 행 오른쪽의 아이콘이 UI thread 앱 명령을 만든다. 인자는 그 행의 절대 경로다.
    const luil::ui_element_id vscode_id { gitman::ui::kind_local_changes_open_vscode, gitman::ui::local_changes_item_id(0).owner };
    const auto vscode_actions { click_action(*tree, vscode_id) };
    const luil::app_ui_command* const vscode { as_app_command(vscode_actions) };
    REQUIRE(vscode != nullptr);
    REQUIRE(gitman::parse_app_command(vscode->command) == gitman::app_command::open_in_vscode);
    REQUIRE(vscode->argument == u8"C:\\work\\alpha\\src\\a.txt");

    const luil::ui_element_id explorer_id { gitman::ui::kind_local_changes_open_explorer, gitman::ui::local_changes_item_id(1).owner };
    const auto explorer_actions { click_action(*tree, explorer_id) };
    const luil::app_ui_command* const explorer { as_app_command(explorer_actions) };
    REQUIRE(explorer != nullptr);
    REQUIRE(gitman::parse_app_command(explorer->command) == gitman::app_command::open_in_explorer_select);
    REQUIRE(explorer->argument == u8"C:\\work\\alpha\\new-dir");
}

TEST_CASE("Scrollbars appear only when the list or diff overflows", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });

    // 6행(132px)까지는 목록 막대가 없고, 그보다 많으면 나타난다. 막대 8종이 owner로
    // 갈리므로 목록·diff는 같은 kind에 다른 owner다 (계약 2장).
    std::vector<gitman::local_change_entry> few {};
    for (int index = 0; index < 3; ++index)
        few.push_back({ gitman::local_change_kind::modified, u8"a.txt" });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, few));
    {
        const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
        REQUIRE(find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_local_changes_list)->visible() == false);
        REQUIRE(find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_local_changes_diff)->visible() == false);
    }

    // 목록·diff가 넘치면 각각의 막대가 보인다.
    gitman::file_diff_event diff {};
    diff.operation_id = fixture.submitter.requests[1].operation_id;
    diff.id.value = u8"alpha";
    for (int index = 0; index < 40; ++index)
        diff.result.lines.push_back(u8"+line");
    fixture.controller.handle(std::move(diff));

    fixture.controller.handle(gitman::cancel_local_changes_dialog_intent {});
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    std::vector<gitman::local_change_entry> many {};
    for (int index = 0; index < 20; ++index)
        many.push_back({ gitman::local_change_kind::modified, u8"a.txt" });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[2].operation_id, many));
    gitman::file_diff_event long_diff {};
    long_diff.operation_id = fixture.submitter.requests[3].operation_id;
    long_diff.id.value = u8"alpha";
    for (int index = 0; index < 40; ++index)
        long_diff.result.lines.push_back(u8"+line");
    fixture.controller.handle(std::move(long_diff));

    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    REQUIRE(find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_local_changes_list)->visible());
    REQUIRE(find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_local_changes_diff)->visible());
}
