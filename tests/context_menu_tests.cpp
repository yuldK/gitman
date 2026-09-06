#include "helpers/ui_test_support.h"

#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "presentation/ui/context_menu_element.h"
#include "presentation/ui/element_kinds.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 메뉴의 키보드 탐색(↑/↓ 건너뛰기·Enter 실행·Esc 닫기)은 luil controller가 하고
// gitman은 `gitman_policy::menu()`로 kind 짝을 알려 줄 뿐이다 (설계 L7). 여기서는
// 그 배선이 실제로 gitman 메뉴 위에서 도는지와 항목이 내는 intent·명령을 본다.
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

    // 문서와 카드 2장(alpha는 준비 완료, beta는 로컬 조회 대기)을 갖춘 controller다.
    struct menu_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        menu_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            for (const std::u8string_view id : { std::u8string_view { u8"alpha" }, std::u8string_view { u8"beta" } })
            {
                gitman::project_definition project {};
                project.id.value = id;
                project.display_name = id;
                project.path.original = std::u8string { u8"C:\\work\\" } + std::u8string { id };
                project.path.normalized = project.path.original;
                document.projects.push_back(std::move(project));
            }
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

    const gitman::context_menu_item_view* find_item(const gitman::context_menu_view& menu, const gitman::context_menu_entry entry)
    {
        for (const gitman::context_menu_item_view& item : menu.items)
            if (item.entry == entry)
                return &item;
        return nullptr;
    }

    // 도우미의 `click`은 bounds 안쪽 1px를 누른다. 배너처럼 element가 아니라 자리를
    // 짚어야 하는 사례만 이 형태를 쓴다.
    std::vector<luil::input_action> click_point(interaction_harness& harness, const float x, const float y, const luil::pointer_button button)
    {
        return click(harness, luil::rect_f { x - 1.0f, y - 1.0f, 2.0f, 2.0f }, 0, button);
    }

    // 배너(도구 막대) 우클릭이 여는 문서 메뉴를 담은 view다 (T1).
    gitman::view_snapshot make_document_menu_view()
    {
        gitman::view_snapshot view { make_view(0) };
        view.document_path = u8"C:\\work\\p.version-list";
        view.document_display_path = u8"C:/work/p.version-list";

        gitman::context_menu_view menu {};
        menu.anchor_x = 100.0f;
        menu.anchor_y = 60.0f;
        menu.items.push_back({ gitman::context_menu_entry::open_document_folder, u8"경로를 탐색기로 열기", true, u8"C:\\work\\p.version-list" });
        menu.items.push_back({ gitman::context_menu_entry::open_document_in_vscode, u8"VSCode로 열기", true, u8"C:\\work" });
        view.context_menu = { std::move(menu) };
        return view;
    }

    // 문서가 열린 상태의 기본 view다. 배너 우클릭 경로를 확인한다.
    gitman::view_snapshot make_document_view()
    {
        gitman::view_snapshot view { make_view(0) };
        view.document_path = u8"C:\\work\\p.version-list";
        view.document_display_path = u8"C:/work/p.version-list";
        return view;
    }

    // 카드 목록과 임의 항목의 메뉴를 담은 view다. UI element 검증용이다.
    gitman::view_snapshot make_menu_view(const float anchor_x, const float anchor_y, const std::vector<bool>& enabled_flags)
    {
        gitman::view_snapshot view { make_view(1) };

        gitman::context_menu_view menu {};
        menu.owner.value = u8"card-0";
        menu.anchor_x = anchor_x;
        menu.anchor_y = anchor_y;
        const std::vector<gitman::context_menu_entry> entries {
            gitman::context_menu_entry::open_repository,
            gitman::context_menu_entry::open_in_vscode,
            gitman::context_menu_entry::show_local_changes,
            gitman::context_menu_entry::refresh,
            gitman::context_menu_entry::update,
            gitman::context_menu_entry::switch_to,
        };
        for (std::size_t index = 0; index < enabled_flags.size() && index < entries.size(); ++index)
            menu.items.push_back({ entries[index], u8"항목", enabled_flags[index], u8"C:\\work\\card-0" });
        view.context_menu = { std::move(menu) };
        return view;
    }
} // namespace

TEST_CASE("A right click intent selects the card and opens the menu with button availability", "[logic][context-menu]")
{
    menu_fixture fixture {};
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 120.0f, 240.0f });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->selected.has_value());
    REQUIRE(view->selected->value == u8"alpha");
    REQUIRE(view->context_menu.has_value());
    REQUIRE(view->context_menu->owner.value == u8"alpha");
    REQUIRE(view->context_menu->anchor_x == 120.0f);
    REQUIRE(view->context_menu->anchor_y == 240.0f);
    // 외부 열기 항목이 셸에 넘길 경로는 항목이 들고 있다 (T1).
    REQUIRE(view->context_menu->items[0].target_path == u8"C:\\work\\alpha");
    REQUIRE(view->context_menu->items.size() == 6u);
    // VSCode 열기는 저장소 열기 바로 아래다 (2026-08-22 지시).
    REQUIRE(view->context_menu->items[0].entry == gitman::context_menu_entry::open_repository);
    REQUIRE(view->context_menu->items[1].entry == gitman::context_menu_entry::open_in_vscode);

    // 준비된 카드는 모든 항목이 활성이다.
    for (const gitman::context_menu_item_view& item : view->context_menu->items)
        REQUIRE(item.enabled);
}

TEST_CASE("A loading card disables the update and switch items only", "[logic][context-menu]")
{
    menu_fixture fixture {};
    // beta는 로컬 조회가 아직 끝나지 않아 변경 작업을 시작할 수 없다.
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"beta" }, 10.0f, 10.0f });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->context_menu.has_value());
    REQUIRE(find_item(*view->context_menu, gitman::context_menu_entry::open_repository)->enabled);
    REQUIRE(find_item(*view->context_menu, gitman::context_menu_entry::show_local_changes)->enabled);
    REQUIRE(find_item(*view->context_menu, gitman::context_menu_entry::refresh)->enabled);
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::update)->enabled);
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::switch_to)->enabled);
}

TEST_CASE("A running change disables the update item while the menu stays open", "[logic][context-menu]")
{
    menu_fixture fixture {};
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->context_menu.has_value());
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::update)->enabled);
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::switch_to)->enabled);
}

TEST_CASE("Unknown cards, the close intent, and a document swap dismiss the menu", "[logic][context-menu]")
{
    menu_fixture fixture {};

    SECTION("없는 카드는 열지 않는다")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"ghost" }, 10.0f, 10.0f });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
    }

    SECTION("닫기 intent")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
        fixture.controller.handle(gitman::close_context_menu_intent {});
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
    }

    SECTION("문서 교체")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
        gitman::document_loaded_event loaded {};
        gitman::workspace_document document {};
        document.document_path = u8"C:\\work\\other.version-list";
        loaded.document = { std::move(document) };
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
        fixture.controller.handle(std::move(loaded));
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
    }
}

TEST_CASE("The built tree anchors the menu panel and pushes it back inside the window", "[ui][context-menu]")
{
    SECTION("앵커에 붙는다")
    {
        const auto tree { make_tree(make_menu_view(120.0f, 240.0f, { true, true, true, true, true, true })) };
        const luil::ui_element* const panel { find(*tree, ui::kind_context_menu_panel) };
        REQUIRE(panel != nullptr);
        REQUIRE(panel->bounds().x == 120.0f);
        REQUIRE(panel->bounds().y == 240.0f);
        REQUIRE(tree->find(ui::context_menu_item_id(5)) != nullptr);
    }

    SECTION("창 밖으로 나가면 안쪽으로 민다")
    {
        const auto tree { make_tree(make_menu_view(790.0f, 595.0f, { true, true, true, true, true, true })) };
        const luil::ui_element* const panel { find(*tree, ui::kind_context_menu_panel) };
        REQUIRE(panel != nullptr);
        REQUIRE(panel->bounds().x + panel->bounds().width <= 800.0f);
        REQUIRE(panel->bounds().y + panel->bounds().height <= 600.0f);
    }
}

TEST_CASE("Menu item clicks close the menu first and then fire the action", "[ui][context-menu]")
{
    const auto tree { make_tree(make_menu_view(100.0f, 100.0f, { true, true, true, true, false, true })) };
    interaction_harness harness { tree };

    SECTION("메뉴 항목과 backdrop은 Tab 자리가 아니다")
    {
        // D1-B: 메뉴 안 이동은 ↑/↓ 강조가 맡고 Tab은 메뉴로 들어오지 않는다.
        REQUIRE(tree->find(ui::context_menu_item_id(0))->tab_stop() == false);
        REQUIRE(find(*tree, ui::kind_context_menu)->tab_stop() == false);
    }

    SECTION("저장소 열기는 폴더 열기 요청이다")
    {
        const auto actions { click(harness, tree->find(ui::context_menu_item_id(0))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const gitman::logic_message* const close { message_at(actions, 0) };
        REQUIRE(close != nullptr);
        REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*close));
        // 외부 열기는 UI thread가 실행하는 앱 명령이다 (설계 L7).
        const auto* const open { std::get_if<luil::app_ui_command>(&actions[1]) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_explorer_folder);
        REQUIRE(open->argument == u8"C:\\work\\card-0");
    }

    SECTION("VSCode로 열기는 작업 복사본을 VSCode로 여는 요청이다")
    {
        const auto actions { click(harness, tree->find(ui::context_menu_item_id(1))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const auto* const open { std::get_if<luil::app_ui_command>(&actions[1]) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_vscode);
        REQUIRE(open->argument == u8"C:\\work\\card-0");
    }

    SECTION("상태 갱신")
    {
        const auto actions { click(harness, tree->find(ui::context_menu_item_id(3))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const gitman::logic_message* const refresh { message_at(actions, 1) };
        REQUIRE(refresh != nullptr);
        const auto* const intent { std::get_if<gitman::refresh_card_intent>(refresh) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
    }

    SECTION("로컬 변경 확인은 F4 dialog를 연다")
    {
        const auto actions { click(harness, tree->find(ui::context_menu_item_id(2))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const gitman::logic_message* const message { message_at(actions, 1) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::open_local_changes_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
    }

    SECTION("비활성 항목은 아무 일도 하지 않는다")
    {
        REQUIRE(click(harness, tree->find(ui::context_menu_item_id(4))->bounds()).empty());
    }

    SECTION("바깥 클릭은 좌·우 모두 닫기다")
    {
        for (const luil::pointer_button button : { luil::pointer_button::left, luil::pointer_button::right })
        {
            const auto actions { click(harness, luil::rect_f { 700.0f, 500.0f, 4.0f, 4.0f }, 0, button) };
            const gitman::logic_message* const message { as_message(actions) };
            REQUIRE(message != nullptr);
            REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*message));
        }
    }

    SECTION("메뉴가 떠 있는 동안 휠은 무시된다")
    {
        REQUIRE(harness.process(luil::mouse_wheel_event { 400.0f, 300.0f, 120.0f }).empty());
    }
}

TEST_CASE("A card body right click asks for the menu at the pointer", "[ui][context-menu]")
{
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };

    const luil::rect_f body { bounds_of(*tree, ui::kind_card_body, u8"card-0") };
    const auto actions { click(harness, body, 0, luil::pointer_button::right) };
    const auto* const intent { as_intent<gitman::open_context_menu_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->id.value == u8"card-0");
    REQUIRE(intent->anchor_x == body.x + 1.0f);
    REQUIRE(intent->anchor_y == body.y + 1.0f);
}

TEST_CASE("The keyboard walks enabled items, runs the highlight, and escape closes", "[ui][context-menu]")
{
    // 항목 1이 비활성이라 ↑/↓가 건너뛰어야 한다. 탐색은 luil이 하고 gitman은
    // `menu()`가 돌려준 kind 짝({ 메뉴, 항목 })으로 그 길에 올라탄다.
    const auto tree { make_tree(make_menu_view(100.0f, 100.0f, { true, false, true, true, true, true })) };
    interaction_harness harness { tree };

    SECTION("↓는 활성 항목 사이를 오가고 Enter가 실행한다")
    {
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_down }).empty());
        REQUIRE(harness.snapshot().menu_highlight == ui::context_menu_item_id(0));
        // 비활성 항목 1을 건너뛴다.
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_down }).empty());
        REQUIRE(harness.snapshot().menu_highlight == ui::context_menu_item_id(2));
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_up }).empty());
        REQUIRE(harness.snapshot().menu_highlight == ui::context_menu_item_id(0));

        const auto actions { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
        REQUIRE(actions.size() == 2u);
        REQUIRE(std::get_if<luil::app_ui_command>(&actions[1]) != nullptr);
    }

    SECTION("강조가 없으면 Enter는 아무 일도 하지 않는다")
    {
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::enter }).empty());
    }

    SECTION("Esc는 닫기 intent다")
    {
        const auto actions { harness.process(luil::key_pressed_event { luil::key_code::escape }) };
        const gitman::logic_message* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*message));
    }

    SECTION("메뉴가 닫히면 강조가 지워진다")
    {
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_down }).empty());
        REQUIRE((harness.snapshot().menu_highlight == luil::ui_element_id {}) == false);
        // 메뉴 없는 tree를 받는 순간 luil이 강조를 거둔다.
        harness.controller.set_tree(make_tree(1));
        REQUIRE(harness.snapshot().menu_highlight == luil::ui_element_id {});
    }
}

TEST_CASE("A banner right click opens the document menu without changing the selected card", "[logic][context-menu][banner]")
{
    menu_fixture fixture {};
    fixture.controller.handle(gitman::select_card_intent { { gitman::project_id { u8"alpha" } } });
    fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->context_menu.has_value());
    // 문서 메뉴는 카드와 무관하다 — owner가 비어 있고 선택 카드도 그대로다.
    REQUIRE(view->context_menu->owner.value.empty());
    REQUIRE(view->selected.has_value());
    REQUIRE(view->selected->value == u8"alpha");
    REQUIRE(view->context_menu->anchor_x == 40.0f);
    REQUIRE(view->context_menu->anchor_y == 50.0f);

    REQUIRE(view->context_menu->items.size() == 2u);
    REQUIRE(view->context_menu->items[0].entry == gitman::context_menu_entry::open_document_folder);
    REQUIRE(view->context_menu->items[0].enabled);
    // 탐색기는 문서 파일을, VSCode는 그 폴더를 받는다. 둘 다 Windows 원형이다.
    REQUIRE(view->context_menu->items[0].target_path == u8"C:\\work\\p.version-list");
    REQUIRE(view->context_menu->items[1].entry == gitman::context_menu_entry::open_document_in_vscode);
    REQUIRE(view->context_menu->items[1].target_path == u8"C:\\work");
}

TEST_CASE("The document menu does not open without an open document", "[logic][context-menu][banner]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });

    REQUIRE(controller.make_view_snapshot()->context_menu.has_value() == false);
}

TEST_CASE("The banner opens the document menu on right click only while a document is open", "[ui][context-menu][banner]")
{
    SECTION("문서가 열려 있으면 우클릭이 메뉴 intent를 낸다")
    {
        const auto tree { make_tree(make_document_view()) };
        interaction_harness harness { tree };

        const luil::rect_f toolbar { bounds_of(*tree, ui::kind_toolbar) };
        // 가운데는 문서 경로 label 자리다. 버튼은 오른쪽 끝에 모여 있다.
        const float x { toolbar.x + toolbar.width / 2.0f };
        const float y { toolbar.y + toolbar.height / 2.0f };

        const auto actions { click_point(harness, x, y, luil::pointer_button::right) };
        const auto* const intent { as_intent<gitman::open_document_context_menu_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->anchor_x == x);
        REQUIRE(intent->anchor_y == y);

        // 좌클릭은 지금까지처럼 선택 해제다.
        const auto left { click_point(harness, x, y, luil::pointer_button::left) };
        REQUIRE(as_intent<gitman::select_card_intent>(left) != nullptr);
    }

    SECTION("문서가 없으면 배너 우클릭은 아무 일도 하지 않는다")
    {
        const auto tree { make_tree(0, gitman::view_empty_state::no_document) };
        interaction_harness harness { tree };

        const luil::rect_f toolbar { bounds_of(*tree, ui::kind_toolbar) };
        const float x { toolbar.x + toolbar.width / 2.0f };
        const float y { toolbar.y + toolbar.height / 2.0f };
        REQUIRE(click_point(harness, x, y, luil::pointer_button::right).empty());
    }
}

TEST_CASE("Document menu items open the explorer selection and the folder in VSCode", "[ui][context-menu][banner]")
{
    const auto tree { make_tree(make_document_menu_view()) };
    interaction_harness harness { tree };

    SECTION("경로를 탐색기로 열기는 문서를 선택 상태로 연다")
    {
        const auto actions { click(harness, tree->find(ui::context_menu_item_id(0))->bounds()) };
        REQUIRE(actions.size() == 2u);
        REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*message_at(actions, 0)));
        const auto* const open { std::get_if<luil::app_ui_command>(&actions[1]) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_explorer_select);
        REQUIRE(open->argument == u8"C:\\work\\p.version-list");
    }

    SECTION("VSCode로 열기는 문서가 있는 폴더를 연다")
    {
        const auto actions { click(harness, tree->find(ui::context_menu_item_id(1))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const auto* const open { std::get_if<luil::app_ui_command>(&actions[1]) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_vscode);
        REQUIRE(open->argument == u8"C:\\work");
    }
}
