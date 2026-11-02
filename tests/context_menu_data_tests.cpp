#include "helpers/ui_test_support.h"

#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/context_menu_popup.h"

#include "luil/generated/codicons.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_tree.h"
#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 컨텍스트 메뉴를 항목 데이터로 짓는 것을 본다 (function-buttons-design T8). 앞 절은 logic이
// 짓는 카드 메뉴·배너 메뉴가 데이터로 바뀐 뒤에도 예전과 같은 라벨·글리프·활성·크기·동작을
// 내는지, 뒤 절은 다음 체크포인트의 기능 메뉴·카드 메뉴 기능 절(T9)과 배치 메뉴(T10)가 쓸
// 칸(가변 항목 수와 긴 메뉴 높이, 구분선, 단축키 열, radio, 비활성, 기본 항목, 접근 이름,
// 같은 동작을 나눠 쓰는 항목)을 popup이 그대로 옮기는지 본다. 기존 동작 자체는
// `context_menu_tests.cpp`가 고친 데 없이 그대로 본다.
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

    // 문서와 카드 1장(alpha, 준비 완료)을 갖춘 controller다.
    struct data_menu_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        data_menu_fixture()
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
        }
    };

    // popup tree의 root 메뉴다. 항목 설정(글리프·구분선·radio·단축키·기본 항목)을 여기서 읽는다.
    const luil::menu_element& popup_menu(const luil::win32::ui_popup& popup)
    {
        REQUIRE(popup.tree != nullptr);
        const auto* const menu { dynamic_cast<const luil::menu_element*>(popup.tree->root()) };
        REQUIRE(menu != nullptr);
        return *menu;
    }

    // 예전 popup의 entry별 아이콘 표다 (T8 전 `entry_glyph`). 데이터로 옮긴 뒤에도 같은 글리프여야 한다.
    char32_t previous_glyph(const gitman::context_menu_entry entry)
    {
        switch (entry)
        {
        case gitman::context_menu_entry::open_repository:
        case gitman::context_menu_entry::open_document_folder:
            return luil::codicons::icon_folder_opened;
        case gitman::context_menu_entry::open_in_vscode:
        case gitman::context_menu_entry::open_document_in_vscode:
            return luil::codicons::icon_vscode;
        case gitman::context_menu_entry::show_local_changes:
            return luil::codicons::icon_diff;
        case gitman::context_menu_entry::refresh:
            return luil::codicons::icon_refresh;
        case gitman::context_menu_entry::update:
            return luil::codicons::icon_repo_pull;
        case gitman::context_menu_entry::switch_to:
            return luil::codicons::icon_source_control;
        }
        return luil::codicons::icon_question;
    }

    // 예전 메뉴는 구분선·단축키·radio·기본 항목 없이 아이콘·라벨·활성만 있었다.
    void require_plain_rows(const gitman::context_menu_view& view, const luil::win32::ui_popup& popup)
    {
        REQUIRE(view.default_item.has_value() == false);
        const luil::menu_config& config { popup_menu(popup).config() };
        REQUIRE(config.items.size() == view.items.size());
        REQUIRE(config.default_key.empty());
        for (std::size_t index = 0; index < view.items.size(); ++index)
        {
            const gitman::context_menu_item_view& item { view.items[index] };
            REQUIRE(item.argument.empty());
            REQUIRE(item.shortcut_text.empty());
            REQUIRE(item.separator_above == false);
            REQUIRE(item.kind == gitman::context_menu_item_kind::action);
            REQUIRE(item.checked == false);

            const luil::menu_item_config& row { config.items[index] };
            REQUIRE(row.key == ui::context_menu_entry_key(item.entry));
            REQUIRE(row.label == item.label);
            REQUIRE(row.icon == previous_glyph(item.entry));
            REQUIRE(row.enabled == item.enabled);
            REQUIRE(row.separator_above == false);
            REQUIRE(row.kind == luil::menu_item_kind::action);
            REQUIRE(row.checked == false);
            REQUIRE(row.shortcut_text.empty());
            REQUIRE(row.children.empty());
        }
    }

    void install_popup(interaction_harness& harness, const luil::win32::ui_popup& popup)
    {
        harness.controller.set_surface_trees({ { popup.id, popup.tree } });
    }

    // popup client 좌표(항목 bounds 가운데)를 누르고 뗀다. 항목은 키로 찾는다.
    std::vector<luil::input_action> click_popup_key(interaction_harness& harness, const luil::win32::ui_popup& popup, const std::u8string& key)
    {
        const luil::ui_element* const item { popup.tree->find({ luil::ui_element_kind::menu_item, key }) };
        REQUIRE(item != nullptr);
        const luil::rect_f box { item->bounds() };
        const float x { box.x + box.width / 2.0f };
        const float y { box.y + box.height / 2.0f };
        const auto pressed { harness.controller.process(luil::pointer_pressed_event { x, y, luil::pointer_button::left, at(0), false, popup.id }) };
        REQUIRE(pressed.empty());
        return harness.controller.process(luil::pointer_released_event { x, y, luil::pointer_button::left, at(0), popup.id });
    }

    // 같은 동작을 나눠 쓰는 기능 항목을 흉내 낸다. 기능 메뉴의 실행 항목은 T9에서 생기므로
    // 여기서는 logic으로 가는 기존 동작(`refresh`)에 argument를 실어 쓴다.
    gitman::context_menu_item_view function_like_item(std::u8string argument, std::u8string label)
    {
        gitman::context_menu_item_view item {};
        item.entry = gitman::context_menu_entry::refresh;
        item.label = std::move(label);
        item.argument = std::move(argument);
        item.icon = u8"refresh";
        return item;
    }

    // 기능 `function_count`개 + 구분선 + 끝 항목 `tail_count`개인 카드 소유 메뉴다 (2.3.2·2.3.3).
    gitman::context_menu_view long_menu(const std::size_t head_count, const std::size_t function_count, const std::size_t tail_count)
    {
        gitman::context_menu_view menu {};
        menu.owner.value = u8"alpha";
        for (std::size_t index = 0; index < head_count; ++index)
        {
            gitman::context_menu_item_view head { function_like_item(u8"head-" + std::u8string(1, static_cast<char8_t>(u8'a' + index)), u8"기존 항목") };
            menu.items.push_back(std::move(head));
        }
        for (std::size_t index = 0; index < function_count; ++index)
        {
            gitman::context_menu_item_view item { function_like_item(u8"fn-" + std::u8string(1, static_cast<char8_t>(u8'a' + index)), u8"기능") };
            // 카드 메뉴의 기능 절은 기존 항목 아래 구분선으로 시작한다 (2.3.3).
            item.separator_above = head_count > 0 && index == 0;
            menu.items.push_back(std::move(item));
        }
        for (std::size_t index = 0; index < tail_count; ++index)
        {
            gitman::context_menu_item_view tail { function_like_item(u8"tail-" + std::u8string(1, static_cast<char8_t>(u8'a' + index)), u8"끝 항목") };
            // 기능 메뉴는 기능 목록 아래 구분선 뒤에 끝 항목을 둔다 (2.3.2).
            tail.separator_above = head_count == 0 && index == 0;
            menu.items.push_back(std::move(tail));
        }
        return menu;
    }
} // namespace

TEST_CASE("The card menu built from item data matches the previous menu", "[function_buttons][t8][ui][context-menu]")
{
    data_menu_fixture fixture {};
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 120.0f, 240.0f });
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->context_menu.has_value());
    const gitman::context_menu_view& menu { *view->context_menu };

    const std::vector<std::pair<gitman::context_menu_entry, std::u8string_view>> expected {
        { gitman::context_menu_entry::open_repository, u8"저장소 열기" },
        { gitman::context_menu_entry::open_in_vscode, u8"VSCode로 열기" },
        { gitman::context_menu_entry::show_local_changes, u8"로컬 변경 확인" },
        { gitman::context_menu_entry::refresh, u8"상태 갱신" },
        { gitman::context_menu_entry::update, u8"업데이트" },
        { gitman::context_menu_entry::switch_to, u8"전환…" },
    };
    REQUIRE(menu.items.size() == expected.size());
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        REQUIRE(menu.items[index].entry == expected[index].first);
        REQUIRE(menu.items[index].label == expected[index].second);
        REQUIRE(menu.items[index].enabled);
    }
    REQUIRE(menu.items[0].target_path == u8"C:\\work\\alpha");
    REQUIRE(menu.items[1].target_path == u8"C:\\work\\alpha");
    for (std::size_t index = 2; index < menu.items.size(); ++index)
        REQUIRE(menu.items[index].target_path.empty());

    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
    REQUIRE(popup.id == ui::card_menu_popup_id);
    REQUIRE(popup.x == 120.0f);
    REQUIRE(popup.y == 240.0f);
    // 폭은 예전처럼 고정 190이고 높이는 6행 `8 + 26 × 6`이다.
    REQUIRE(popup.width == gitman::layout_context_menu_width);
    REQUIRE(popup.width == 190.0f);
    REQUIRE(popup.height == 164.0f);
    require_clean_tree(*popup.tree);
    require_plain_rows(menu, popup);
}

TEST_CASE("The banner menu built from item data matches the previous menu", "[function_buttons][t8][ui][context-menu][banner]")
{
    data_menu_fixture fixture {};
    fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->context_menu.has_value());
    const gitman::context_menu_view& menu { *view->context_menu };

    REQUIRE(menu.owner.value.empty());
    REQUIRE(menu.items.size() == 2u);
    REQUIRE(menu.items[0].entry == gitman::context_menu_entry::open_document_folder);
    REQUIRE(menu.items[0].label == u8"경로를 탐색기로 열기");
    REQUIRE(menu.items[0].enabled);
    REQUIRE(menu.items[0].target_path == u8"C:\\work\\p.version-list");
    REQUIRE(menu.items[1].entry == gitman::context_menu_entry::open_document_in_vscode);
    REQUIRE(menu.items[1].label == u8"VSCode로 열기");
    REQUIRE(menu.items[1].enabled);
    REQUIRE(menu.items[1].target_path == u8"C:\\work");

    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
    REQUIRE(popup.id == ui::document_menu_popup_id);
    REQUIRE(popup.x == 40.0f);
    REQUIRE(popup.y == 50.0f);
    REQUIRE(popup.width == 190.0f);
    REQUIRE(popup.height == 60.0f);
    require_clean_tree(*popup.tree);
    require_plain_rows(menu, popup);

    // logic이 지은 항목을 눌러도 예전처럼 UI thread의 앱 명령이 곧바로 나간다.
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };
    install_popup(harness, popup);

    SECTION("경로를 탐색기로 열기")
    {
        const auto actions { click_popup_key(harness, popup, u8"open_document_folder") };
        const luil::app_ui_command* const open { as_app_command(actions) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_explorer_select);
        REQUIRE(open->argument == u8"C:\\work\\p.version-list");
    }

    SECTION("VSCode로 열기")
    {
        const auto actions { click_popup_key(harness, popup, u8"open_document_in_vscode") };
        const luil::app_ui_command* const open { as_app_command(actions) };
        REQUIRE(open != nullptr);
        REQUIRE(gitman::parse_app_command(open->command) == gitman::app_command::open_in_vscode);
        REQUIRE(open->argument == u8"C:\\work");
    }
}

TEST_CASE("A selection not on the open menu only closes it", "[function_buttons][t8][context-menu][logic]")
{
    // logic은 고른 항목을 지금 메뉴의 항목 목록에서 하위 동작과 argument로 다시 찾는다.
    data_menu_fixture fixture {};

    SECTION("argument가 다른 항목")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::refresh, u8"build" });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.empty());
    }

    SECTION("배너 메뉴에 카드 동작")
    {
        fixture.controller.handle(gitman::open_document_context_menu_intent { 10.0f, 10.0f });
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::refresh });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.empty());
    }

    SECTION("같은 항목은 그대로 실행한다")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::refresh, {} });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.submitter.requests.size() == 1u);
        REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::refresh);
    }
}

TEST_CASE("Icon names resolve to codicon glyphs", "[function_buttons][t8][ui][context-menu][popup]")
{
    REQUIRE(ui::context_menu_icon_glyph(u8"folder-opened") == luil::codicons::icon_folder_opened);
    REQUIRE(ui::context_menu_icon_glyph(u8"vscode") == luil::codicons::icon_vscode);
    REQUIRE(ui::context_menu_icon_glyph(u8"diff") == luil::codicons::icon_diff);
    REQUIRE(ui::context_menu_icon_glyph(u8"refresh") == luil::codicons::icon_refresh);
    REQUIRE(ui::context_menu_icon_glyph(u8"repo-pull") == luil::codicons::icon_repo_pull);
    REQUIRE(ui::context_menu_icon_glyph(u8"source-control") == luil::codicons::icon_source_control);
    // 비었거나 모르는 이름은 아이콘이 없다.
    REQUIRE(ui::context_menu_icon_glyph(u8"") == 0);
    REQUIRE(ui::context_menu_icon_glyph(u8"없는-아이콘") == 0);
}

TEST_CASE("Long menus size by rows and separators", "[function_buttons][t8][ui][context-menu][popup]")
{
    struct size_case
    {
        std::size_t head_count { 0 };
        std::size_t function_count { 0 };
        std::size_t tail_count { 0 };
        float height { 0.0f };
    };
    // 기능 메뉴: 기능 20 + 구분선 + 기능 편집·문서 다시 읽기 = 22행 → 589 (2.3.2).
    // 카드 메뉴: 기존 6 + 구분선 + 기능 8 + 기능 더 보기 + 기능 편집 = 16행 → 433,
    // 기능이 정확히 8개면 기능 더 보기가 없어 15행 → 407 (2.3.3).
    // 배치 메뉴: radio 3 + 구분선 + 기능 편집 = 4행 → 121 (2.2.5).
    const std::vector<size_case> cases {
        { 0, 20, 2, 589.0f },
        { 6, 10, 0, 433.0f },
        { 6, 9, 0, 407.0f },
        { 0, 3, 1, 121.0f },
    };
    for (const size_case& sample : cases)
    {
        const gitman::context_menu_view menu { long_menu(sample.head_count, sample.function_count, sample.tail_count) };
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup.height == sample.height);
        REQUIRE(popup.width == gitman::layout_context_menu_width);
        require_clean_tree(*popup.tree);

        // 항목마다 키가 달라 popup tree에 모두 있다.
        const std::vector<luil::ui_element_id> ids { popup.tree->ids_of_kind(luil::ui_element_kind::menu_item) };
        REQUIRE(ids.size() == menu.items.size());
        std::set<std::u8string> keys {};
        for (const luil::ui_element_id& id : ids)
            keys.insert(id.owner);
        REQUIRE(keys.size() == menu.items.size());

        // 구분선은 그 항목 위에 9를 띄운다.
        const luil::menu_config& config { popup_menu(popup).config() };
        std::size_t separators { 0 };
        for (const luil::menu_item_config& row : config.items)
            if (row.separator_above)
                ++separators;
        REQUIRE(separators == 1u);
    }

    // 배율 2에서도 높이는 논리 픽셀이고 tree만 배율이 곱해진다.
    const luil::win32::ui_popup scaled { ui::build_context_menu_popup(long_menu(0, 20, 2), 2.0f) };
    REQUIRE(scaled.height == 589.0f);
    const luil::ui_element* const last { scaled.tree->find({ luil::ui_element_kind::menu_item, u8"refresh:tail-b" }) };
    REQUIRE(last != nullptr);
    REQUIRE(last->bounds().y + last->bounds().height == (589.0f - luil::menu_padding) * 2.0f);
}

TEST_CASE("Items sharing an action are told apart by their argument", "[function_buttons][t8][ui][context-menu][popup]")
{
    gitman::context_menu_view menu {};
    menu.owner.value = u8"alpha";
    menu.items.push_back(function_like_item(u8"build", u8"빌드"));
    menu.items.push_back(function_like_item(u8"test", u8"테스트"));
    REQUIRE(ui::context_menu_item_key(menu.items[0]) == u8"refresh:build");
    REQUIRE(ui::context_menu_item_key(menu.items[1]) == u8"refresh:test");
    // argument가 없으면 키는 동작 키 그대로다 (기존 메뉴).
    REQUIRE(ui::context_menu_item_key({ gitman::context_menu_entry::update, u8"업데이트" }) == u8"update");

    const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };
    install_popup(harness, popup);

    const auto actions { click_popup_key(harness, popup, u8"refresh:test") };
    const auto* const intent { as_intent<gitman::select_context_menu_item_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->entry == gitman::context_menu_entry::refresh);
    REQUIRE(intent->argument == u8"test");
}

TEST_CASE("Radio, shortcut, disabled and default rows pass through to the popup", "[function_buttons][t8][ui][context-menu][popup]")
{
    // 배치 메뉴 모양이다 (2.2.5): radio 셋 중 보이는 배치에 checked, `카드에 두기`는 `card`가
    // 이미 3개라 상한 사유 라벨로 비활성이고 받을 번호가 없어 단축키 열이 빈다(`Alt+1..3`뿐),
    // 구분선 뒤 끝 항목은 단축키 열과 상태 라벨을 든다.
    gitman::context_menu_view menu {};
    menu.owner.value = u8"alpha";
    gitman::context_menu_item_view card { function_like_item(u8"card", u8"카드에 두기 — 3개까지") };
    card.kind = gitman::context_menu_item_kind::radio;
    card.enabled = false;
    gitman::context_menu_item_view bar { function_like_item(u8"bar", u8"기능 줄에 두기") };
    bar.kind = gitman::context_menu_item_kind::radio;
    bar.checked = true;
    gitman::context_menu_item_view hidden { function_like_item(u8"menu", u8"메뉴에만 두기") };
    hidden.kind = gitman::context_menu_item_kind::radio;
    gitman::context_menu_item_view edit { function_like_item(u8"edit", u8"빌드 — 실행 중 00:12") };
    edit.separator_above = true;
    edit.shortcut_text = u8"Alt+1";
    menu.items = { card, bar, hidden, edit };

    SECTION("항목 설정")
    {
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup.height == 121.0f);
        const luil::menu_config& config { popup_menu(popup).config() };
        REQUIRE(config.items.size() == 4u);
        REQUIRE(config.items[0].kind == luil::menu_item_kind::radio);
        REQUIRE(config.items[0].checked == false);
        REQUIRE(config.items[0].enabled == false);
        REQUIRE(config.items[0].shortcut_text.empty());
        REQUIRE(config.items[1].kind == luil::menu_item_kind::radio);
        REQUIRE(config.items[1].checked);
        REQUIRE(config.items[2].kind == luil::menu_item_kind::radio);
        REQUIRE(config.items[2].checked == false);
        REQUIRE(config.items[3].kind == luil::menu_item_kind::action);
        REQUIRE(config.items[3].separator_above);
        REQUIRE(config.items[3].icon == luil::codicons::icon_refresh);
        REQUIRE(config.items[3].shortcut_text == u8"Alt+1");
        // 기본 항목을 정하지 않으면 강조 없는 Enter는 아무것도 하지 않는다.
        REQUIRE(config.default_key.empty());

        // 접근 이름은 라벨이다 — 상태는 라벨에 들고 단축키 열은 읽히지 않는다 (2.3.2).
        // radio는 checked를 선택 상태로 알린다.
        const luil::ui_element* const card_row { popup.tree->find({ luil::ui_element_kind::menu_item, u8"refresh:card" }) };
        const luil::ui_element* const bar_row { popup.tree->find({ luil::ui_element_kind::menu_item, u8"refresh:bar" }) };
        const luil::ui_element* const edit_row { popup.tree->find({ luil::ui_element_kind::menu_item, u8"refresh:edit" }) };
        REQUIRE(card_row != nullptr);
        REQUIRE(bar_row != nullptr);
        REQUIRE(edit_row != nullptr);
        REQUIRE(card_row->accessibility().name == u8"카드에 두기 — 3개까지");
        REQUIRE(card_row->enabled() == false);
        REQUIRE(bar_row->accessibility().selected == std::optional<bool> { true });
        REQUIRE(card_row->accessibility().selected == std::optional<bool> { false });
        REQUIRE(edit_row->accessibility().name == u8"빌드 — 실행 중 00:12");
    }

    SECTION("비활성 항목은 키보드가 건너뛰고 눌러도 아무 일이 없다")
    {
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        const auto tree { make_tree(1) };
        interaction_harness harness { tree };
        install_popup(harness, popup);
        REQUIRE(click_popup_key(harness, popup, u8"refresh:card").empty());
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_down }).empty());
        REQUIRE(harness.snapshot().menu_highlight == luil::ui_element_id { luil::ui_element_kind::menu_item, u8"refresh:bar" });
    }

    SECTION("기본 항목은 강조 없는 Enter가 고른다")
    {
        // popup이 기본 항목 순번을 그 항목의 키로 바꿔 강조 없는 Enter가 고르는지만 본다.
        // 어느 항목을 기본으로 둘지(유휴일 때 첫 기능, 실행 중이면 없음, 2.3.2)는 T9 builder가 정한다.
        menu.default_item = 3u;
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup_menu(popup).config().default_key == u8"refresh:edit");

        const auto tree { make_tree(1) };
        interaction_harness harness { tree };
        install_popup(harness, popup);
        const auto actions { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
        const auto* const intent { as_intent<gitman::select_context_menu_item_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->argument == u8"edit");
    }

    SECTION("범위 밖 기본 항목은 없는 것과 같다")
    {
        menu.default_item = 4u;
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup_menu(popup).config().default_key.empty());
    }
}
