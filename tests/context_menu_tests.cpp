#include "helpers/ui_test_support.h"

#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/context_menu_popup.h"
#include "presentation/ui/element_kinds.h"

#include "luil/ui/menu_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 컨텍스트 메뉴는 주 tree의 overlay가 아니라 popup 창이다 (설계 D3-A). 여는 것과
// 닫는 것은 logic 상태이고, 항목의 키보드 탐색(↑/↓ 건너뛰기·Enter 실행·Esc 닫기)은
// luil controller가 popup tree 위에서 한다 — gitman은 `gitman_policy::menu()`로 kind
// 짝을 알려 줄 뿐이다 (설계 L7). 여기서는 그 배선이 실제로 도는지와 항목이 내는
// intent·명령을 본다.
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
    gitman::context_menu_view make_document_menu()
    {
        gitman::context_menu_view menu {};
        menu.anchor_x = 100.0f;
        menu.anchor_y = 60.0f;
        menu.items.push_back({ gitman::context_menu_entry::open_document_folder, u8"경로를 탐색기로 열기", true, u8"C:\\work\\p.version-list" });
        menu.items.push_back({ gitman::context_menu_entry::open_document_in_vscode, u8"VSCode로 열기", true, u8"C:\\work" });
        return menu;
    }

    // 문서가 열린 상태의 기본 view다. 배너 우클릭 경로를 확인한다.
    gitman::view_snapshot make_document_view()
    {
        gitman::view_snapshot view { make_view(0) };
        view.document_path = u8"C:\\work\\p.version-list";
        view.document_display_path = u8"C:/work/p.version-list";
        return view;
    }

    // 카드 메뉴의 여섯 항목을 담은 표시 모델이다. popup 빌드 검증용이다.
    gitman::context_menu_view make_card_menu(const float anchor_x, const float anchor_y, const std::vector<bool>& enabled_flags)
    {
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
        return menu;
    }

    // popup tree 안 항목의 id다. owner는 entry 키 그대로다 (계약 2장).
    luil::ui_element_id menu_item_id(const gitman::context_menu_entry entry)
    {
        return { luil::ui_element_kind::menu_item, std::u8string { ui::context_menu_entry_key(entry) } };
    }

    // 항목 수 × 줄 높이 + 위아래 여백이다. `menu_element::height_for`가 내는 값과
    // 같아야 한다 (gitman 메뉴에는 구분선이 없다).
    float expected_menu_height(const std::size_t item_count)
    {
        return static_cast<float>(item_count) * luil::menu_item_height + luil::menu_padding * 2.0f;
    }

    // popup을 controller의 표면 목록에 얹는다. 표식이 있어야 포인터·키가 이 tree로 간다.
    void install_popup(interaction_harness& harness, const luil::win32::ui_popup& popup)
    {
        harness.controller.set_surface_trees({ { popup.id, popup.tree } });
    }

    // popup client 좌표(항목 bounds 가운데)를 누르고 뗀다.
    std::vector<luil::input_action> click_popup_item(interaction_harness& harness, const luil::win32::ui_popup& popup, const gitman::context_menu_entry entry)
    {
        const luil::ui_element* const item { popup.tree->find(menu_item_id(entry)) };
        REQUIRE(item != nullptr);
        const luil::rect_f box { item->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        const auto pressed { harness.controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(0), false, popup.id }) };
        REQUIRE(pressed.empty());
        return harness.controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, at(0), popup.id });
    }

    // 닫힘 계기 하나가 내는 액션이다.
    std::vector<luil::input_action> dismiss_actions(const luil::win32::ui_popup& popup, const luil::win32::popup_dismiss_reason reason)
    {
        REQUIRE(popup.dismiss != nullptr);
        std::vector<luil::input_action> actions {};
        actions.push_back(popup.dismiss(reason));
        return actions;
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

TEST_CASE("Selecting a menu item closes the menu and runs the entry", "[logic][context-menu]")
{
    // popup 항목은 닫기와 본 동작을 두 액션으로 내지 않는다 — logic이 하나의
    // intent를 받아 닫고 이어서 처리한다 (설계 L6).
    menu_fixture fixture {};
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 120.0f, 240.0f });

    SECTION("상태 갱신은 조회를 제출한다")
    {
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::refresh });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.size() == 1u);
        REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::refresh);
        REQUIRE(fixture.submitter.requests.front().project.id.value == u8"alpha");
    }

    SECTION("업데이트는 변경 작업을 시작한다")
    {
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::update });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.size() == 1u);
        REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::update);
        REQUIRE(fixture.submitter.requests.front().project.id.value == u8"alpha");
    }

    SECTION("전환은 switch dialog를 연다")
    {
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::switch_to });
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->context_menu.has_value() == false);
        REQUIRE(view->switch_dialog.has_value());
        REQUIRE(fixture.submitter.requests.size() == 1u);
        REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::query_switch_candidates);
    }

    SECTION("로컬 변경 확인은 F4 dialog를 연다")
    {
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::show_local_changes });
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->context_menu.has_value() == false);
        REQUIRE(view->local_changes_dialog.has_value());
        REQUIRE(fixture.submitter.requests.size() == 1u);
        REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::query_local_changes);
    }
}

TEST_CASE("A late selection on a disabled or missing menu only closes", "[logic][context-menu]")
{
    menu_fixture fixture {};

    SECTION("변경 실행 중의 업데이트는 닫기만 한다")
    {
        // 메뉴가 떠 있는 사이에 변경이 시작됐다. view가 이미 걸렀지만 늦은 클릭이
        // 도착할 수 있어 logic이 같은 판정을 다시 본다.
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
        fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
        REQUIRE(fixture.submitter.requests.size() == 1u);

        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::update });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.size() == 1u);
    }

    SECTION("메뉴가 없으면 무시한다")
    {
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::refresh });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.empty());
    }

    SECTION("로컬 조회가 끝나지 않은 카드의 전환은 닫기만 한다")
    {
        // beta는 아직 로컬 결과가 없어 view가 전환 항목을 비활성으로 준다. 늦은 클릭이
        // 와도 logic이 같은 판정(`can_change`)을 다시 보고 dialog를 열지 않는다.
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"beta" }, 10.0f, 10.0f });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value());

        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::switch_to });
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->context_menu.has_value() == false);
        REQUIRE(view->switch_dialog.has_value() == false);
        REQUIRE(fixture.submitter.requests.empty());
    }

    SECTION("문서 메뉴 항목은 닫기만 한다")
    {
        // 배너 메뉴의 두 항목은 외부 열기뿐이라 UI thread가 곧바로 처리한다.
        fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value());
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::open_document_folder });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.empty());
    }
}

TEST_CASE("The card menu popup carries the anchor, the size and one item per entry", "[ui][context-menu][popup]")
{
    // 항목 4(update)가 비활성이다.
    const gitman::context_menu_view menu { make_card_menu(120.0f, 240.0f, { true, true, true, true, false, true }) };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };

    REQUIRE(popup.id == ui::card_menu_popup_id);
    // 앵커 표면이 비면 주 창이다.
    REQUIRE(popup.anchor.empty());
    REQUIRE(popup.x == 120.0f);
    REQUIRE(popup.y == 240.0f);
    REQUIRE(popup.width == gitman::layout_context_menu_width);
    REQUIRE(popup.height == expected_menu_height(menu.items.size()));

    REQUIRE(popup.tree != nullptr);
    require_clean_tree(*popup.tree);
    const std::vector<luil::ui_element_id> items { popup.tree->ids_of_kind(luil::ui_element_kind::menu_item) };
    REQUIRE(items.size() == menu.items.size());
    for (std::size_t index = 0; index < items.size(); ++index)
    {
        // 항목의 owner는 entry 키다 — select factory가 이 키로 항목을 되찾는다.
        REQUIRE(items[index].owner == ui::context_menu_entry_key(menu.items[index].entry));
        const luil::ui_element* const item { popup.tree->find(items[index]) };
        REQUIRE(item != nullptr);
        REQUIRE(item->enabled() == menu.items[index].enabled);
    }
}

TEST_CASE("The popup divides the physical anchor by the scale", "[ui][context-menu][popup]")
{
    // 앵커는 창 좌표의 물리 픽셀이고 popup 자리는 논리 픽셀이다. tree는 배율이 곱해진
    // popup client 좌표에 배치된다.
    constexpr float scale { 2.0f };
    const gitman::context_menu_view menu { make_card_menu(240.0f, 300.0f, { true, true, true, true, true, true }) };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, scale) };

    REQUIRE(popup.x == 120.0f);
    REQUIRE(popup.y == 150.0f);
    // 크기도 논리 픽셀이다. 배율은 tree 배치에만 곱해진다.
    REQUIRE(popup.width == gitman::layout_context_menu_width);
    REQUIRE(popup.height == expected_menu_height(menu.items.size()));

    const luil::ui_element* const first { popup.tree->find(menu_item_id(gitman::context_menu_entry::open_repository)) };
    REQUIRE(first != nullptr);
    REQUIRE(first->bounds().x == luil::menu_padding * scale);
    REQUIRE(first->bounds().y == luil::menu_padding * scale);
    REQUIRE(first->bounds().height == luil::menu_item_height * scale);
}

TEST_CASE("A scale of zero or less counts as one", "[ui][context-menu][popup]")
{
    // 첫 `window_metrics_intent` 전의 view는 배율이 0이다. 나누기가 터지지 않고 물리
    // 앵커를 그대로 쓴다.
    const gitman::context_menu_view menu { make_card_menu(120.0f, 240.0f, { true, true, true, true, true, true }) };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 0.0f) };
    REQUIRE(popup.x == 120.0f);
    REQUIRE(popup.y == 240.0f);
    REQUIRE(popup.tree != nullptr);
    require_clean_tree(*popup.tree);
    REQUIRE(popup.tree->ids_of_kind(luil::ui_element_kind::menu_item).size() == menu.items.size());
}

TEST_CASE("The document menu popup gets its own id and entries", "[ui][context-menu][popup][banner]")
{
    const gitman::context_menu_view menu { make_document_menu() };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };

    // owner가 빈 메뉴는 배너(문서) 메뉴다 (T1).
    REQUIRE(popup.id == ui::document_menu_popup_id);
    REQUIRE(popup.x == 100.0f);
    REQUIRE(popup.y == 60.0f);
    REQUIRE(popup.height == expected_menu_height(2u));
    require_clean_tree(*popup.tree);

    const std::vector<luil::ui_element_id> items { popup.tree->ids_of_kind(luil::ui_element_kind::menu_item) };
    REQUIRE(items.size() == 2u);
    REQUIRE(items[0].owner == ui::context_menu_entry_key(gitman::context_menu_entry::open_document_folder));
    REQUIRE(items[1].owner == ui::context_menu_entry_key(gitman::context_menu_entry::open_document_in_vscode));
}

TEST_CASE("Every entry key round trips", "[ui][context-menu][popup]")
{
    const std::vector<gitman::context_menu_entry> entries {
        gitman::context_menu_entry::open_repository,
        gitman::context_menu_entry::open_in_vscode,
        gitman::context_menu_entry::show_local_changes,
        gitman::context_menu_entry::refresh,
        gitman::context_menu_entry::update,
        gitman::context_menu_entry::switch_to,
        gitman::context_menu_entry::open_document_folder,
        gitman::context_menu_entry::open_document_in_vscode,
    };
    for (const gitman::context_menu_entry entry : entries)
    {
        const std::u8string_view key { ui::context_menu_entry_key(entry) };
        REQUIRE(key.empty() == false);
        REQUIRE(ui::parse_context_menu_entry(key).has_value());
        REQUIRE(*ui::parse_context_menu_entry(key) == entry);
    }
    REQUIRE(ui::parse_context_menu_entry(u8"없는-키").has_value() == false);
}

TEST_CASE("Clicking a popup item fires the entry action", "[ui][context-menu][popup]")
{
    // 주 tree에는 메뉴가 없다. popup은 표면 목록에 따로 얹는다.
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(make_card_menu(100.0f, 100.0f, { true, true, true, true, false, true }), 1.0f) };
    install_popup(harness, popup);

    SECTION("저장소 열기는 폴더 열기 요청이다")
    {
        // 외부 열기는 UI thread가 실행하는 앱 명령이라 logic을 거치지 않는다 (설계 L7).
        const auto actions { click_popup_item(harness, popup, gitman::context_menu_entry::open_repository) };
        const luil::app_ui_command* const open { as_app_command(actions) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_explorer_folder);
        REQUIRE(open->argument == u8"C:\\work\\card-0");
    }

    SECTION("VSCode로 열기는 작업 복사본을 VSCode로 여는 요청이다")
    {
        const auto actions { click_popup_item(harness, popup, gitman::context_menu_entry::open_in_vscode) };
        const luil::app_ui_command* const open { as_app_command(actions) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_vscode);
        REQUIRE(open->argument == u8"C:\\work\\card-0");
    }

    SECTION("상태 갱신")
    {
        const auto actions { click_popup_item(harness, popup, gitman::context_menu_entry::refresh) };
        const auto* const intent { as_intent<gitman::select_context_menu_item_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->entry == gitman::context_menu_entry::refresh);
    }

    SECTION("로컬 변경 확인")
    {
        const auto actions { click_popup_item(harness, popup, gitman::context_menu_entry::show_local_changes) };
        const auto* const intent { as_intent<gitman::select_context_menu_item_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->entry == gitman::context_menu_entry::show_local_changes);
    }

    SECTION("전환")
    {
        const auto actions { click_popup_item(harness, popup, gitman::context_menu_entry::switch_to) };
        const auto* const intent { as_intent<gitman::select_context_menu_item_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->entry == gitman::context_menu_entry::switch_to);
    }

    SECTION("비활성 항목은 아무 일도 하지 않는다")
    {
        REQUIRE(click_popup_item(harness, popup, gitman::context_menu_entry::update).empty());
    }
}

TEST_CASE("Document popup items open the explorer selection and the folder in VSCode", "[ui][context-menu][popup][banner]")
{
    const auto tree { make_tree(make_document_view()) };
    interaction_harness harness { tree };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(make_document_menu(), 1.0f) };
    install_popup(harness, popup);

    SECTION("경로를 탐색기로 열기는 문서를 선택 상태로 연다")
    {
        const auto actions { click_popup_item(harness, popup, gitman::context_menu_entry::open_document_folder) };
        const luil::app_ui_command* const open { as_app_command(actions) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_explorer_select);
        REQUIRE(open->argument == u8"C:\\work\\p.version-list");
    }

    SECTION("VSCode로 열기는 문서가 있는 폴더를 연다")
    {
        const auto actions { click_popup_item(harness, popup, gitman::context_menu_entry::open_document_in_vscode) };
        const luil::app_ui_command* const open { as_app_command(actions) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_vscode);
        REQUIRE(open->argument == u8"C:\\work");
    }
}

TEST_CASE("The keyboard walks the popup items, runs the highlight, and escape closes", "[ui][context-menu][popup]")
{
    // 항목 1(VSCode로 열기)이 비활성이라 ↑/↓가 건너뛰어야 한다. 주 tree에 메뉴가
    // 없어도 `menu()`가 돌려준 kind 짝을 luil이 popup tree에서 찾아 탐색을 잇는다.
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(make_card_menu(100.0f, 100.0f, { true, false, true, true, true, true }), 1.0f) };
    install_popup(harness, popup);

    SECTION("↓는 활성 항목 사이를 오가고 Enter가 실행한다")
    {
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_down }).empty());
        REQUIRE(harness.snapshot().menu_highlight == menu_item_id(gitman::context_menu_entry::open_repository));
        // 강조는 그 메뉴가 사는 표면을 함께 남긴다.
        REQUIRE(harness.snapshot().menu_surface == popup.id);
        // 비활성 항목(VSCode로 열기)을 건너뛴다.
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_down }).empty());
        REQUIRE(harness.snapshot().menu_highlight == menu_item_id(gitman::context_menu_entry::show_local_changes));
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_up }).empty());
        REQUIRE(harness.snapshot().menu_highlight == menu_item_id(gitman::context_menu_entry::open_repository));

        const auto actions { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
        const luil::app_ui_command* const open { as_app_command(actions) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_explorer_folder);
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

    SECTION("popup이 사라지면 강조가 지워진다")
    {
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_down }).empty());
        REQUIRE((harness.snapshot().menu_highlight == luil::ui_element_id {}) == false);
        // 메뉴가 닫히면 frame에서 popup이 빠지고 표면 목록도 비워진다. 그 뒤의 ↓는
        // 메뉴가 아니라 앱 정책(카드 선택 이동)으로 간다.
        harness.controller.set_surface_trees({});
        const std::vector<luil::input_action> after { harness.process(luil::key_pressed_event { luil::key_code::arrow_down }) };
        REQUIRE(as_intent<gitman::select_card_intent>(after) != nullptr);
        REQUIRE(harness.snapshot().menu_highlight == luil::ui_element_id {});
    }
}

TEST_CASE("A wheel over the popup scrolls nothing behind it", "[ui][context-menu][popup]")
{
    // popup 표면의 휠은 luil이 닫힘 계기로 보지 않고 popup tree를 정책에 넘긴다.
    // 뒤의 카드 목록이 스크롤되면 메뉴가 앵커에서 떠 버리므로 정책은 아무것도
    // 내지 않는다 (주 창 위의 휠은 UI thread의 dismiss가 닫는다).
    const gitman::context_menu_view menu { make_card_menu(120.0f, 240.0f, { true, true, true, true, true, true }) };
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };
    install_popup(harness, popup);

    REQUIRE(harness.process(luil::mouse_wheel_event { 20.0f, 20.0f, -120.0f, at(0), popup.id }).empty());
    // 주 창 위의 휠은 여전히 카드 목록을 스크롤한다.
    const std::vector<luil::input_action> main { harness.process(luil::mouse_wheel_event { 400.0f, 300.0f, -120.0f, at(0) }) };
    REQUIRE(as_intent<gitman::scroll_intent>(main) != nullptr);
}

TEST_CASE("Every dismiss reason closes the menu", "[ui][context-menu][popup]")
{
    // 메뉴는 앵커에 매인 짧은 선택이라 계기를 가리지 않는다. 바깥 클릭도 휠도
    // 예전에는 element가 직접 다뤘지만 이제 popup의 닫힘 계기다 (계약 5장).
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(make_card_menu(100.0f, 100.0f, { true, true, true, true, true, true }), 1.0f) };

    const std::vector<luil::win32::popup_dismiss_reason> reasons {
        luil::win32::popup_dismiss_reason::pointer_press_outside,
        luil::win32::popup_dismiss_reason::wheel_scrolled,
        luil::win32::popup_dismiss_reason::escape_key,
        luil::win32::popup_dismiss_reason::surface_moved,
        luil::win32::popup_dismiss_reason::surface_resized,
        luil::win32::popup_dismiss_reason::activation_changed,
    };
    for (const luil::win32::popup_dismiss_reason reason : reasons)
    {
        const auto actions { dismiss_actions(popup, reason) };
        const gitman::logic_message* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*message));
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
