#include "helpers/function_card_fixture.h"
#include "helpers/ui_test_support.h"

#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/context_menu_popup.h"
#include "presentation/ui/element_kinds.h"

#include "luil/ui/menu_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 결정안의 기능 줄과 배치 메뉴(function-buttons-design 2.2.5, 2.4, 4.11)의 조작을 본다 (6.1 T10 `ui_interaction_tests`,
// 배치 메뉴, logic test 항목). 입력은 gitman policy를 꽂은 luil controller로 넣고, 나온 intent를 logic에 되먹인다.
using namespace gitman::testing;

namespace {
    namespace ui = gitman::ui;

    // `a`(card), `b`·`c`·`d`(bar), `e`(menu)다. 모두 출력 수집이라 같은 카드에서 하나가 돌면 나머지는 비활성이다.
    std::vector<gitman::function_button> sample_buttons()
    {
        return {
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"bar"),
            make_function_card_button(u8"c", u8"다", u8"bar"),
            make_function_card_button(u8"d", u8"라", u8"bar"),
            make_function_card_button(u8"e", u8"마"),
        };
    }

    void select(function_card_fixture& fixture, const std::u8string_view id)
    {
        fixture.controller.handle(gitman::select_card_intent { gitman::project_id { std::u8string { id } } });
    }

    std::u8string bar_owner(const std::u8string_view button)
    {
        return ui::card_function_owner(u8"p00", button);
    }

    luil::key_pressed_event shift_key(const luil::key_code key)
    {
        luil::key_pressed_event event { key };
        event.shift = true;
        return event;
    }

    bool forward(function_card_fixture& fixture, const std::vector<luil::input_action>& actions)
    {
        bool forwarded { false };
        for (std::size_t index = 0; index < actions.size(); ++index)
        {
            const gitman::logic_message* const message { message_at(actions, index) };
            if (message == nullptr)
                continue;
            fixture.controller.handle(*message);
            forwarded = true;
        }
        return forwarded;
    }

    void tab_to(interaction_harness& harness, const luil::ui_element_kind kind, const std::u8string_view owner)
    {
        for (int step = 0; step < 200; ++step)
        {
            static_cast<void>(harness.process(luil::key_pressed_event { luil::key_code::tab }));
            if (harness.snapshot().focused == luil::ui_element_id { kind, std::u8string { owner } })
                return;
        }
        FAIL("Tab으로 닿지 못했습니다");
    }

    std::vector<luil::input_action> right_click(interaction_harness& harness, const luil::rect_f& bounds)
    {
        return click(harness, bounds, 0, luil::pointer_button::right);
    }

    // 배치 메뉴의 radio 하나를 고른다. 메뉴 항목 선택 intent를 logic에 넣는다 (popup이 내는 것과 같다).
    void choose(function_card_fixture& fixture, const gitman::context_menu_entry entry, const std::u8string_view button)
    {
        fixture.controller.handle(gitman::select_context_menu_item_intent { entry, std::u8string { button } });
    }

    const gitman::context_menu_item_view& item_of(const gitman::context_menu_view& menu, const gitman::context_menu_entry entry)
    {
        const gitman::context_menu_item_view* found { nullptr };
        for (const gitman::context_menu_item_view& item : menu.items)
        {
            if (item.entry == entry)
                found = &item;
        }
        REQUIRE(found != nullptr);
        return *found;
    }

    std::optional<gitman::function_placement> placement_of(const gitman::card_view_model& card, const std::u8string_view button)
    {
        for (const gitman::card_function_view& function : card.functions)
        {
            if (function.button_id == button)
                return function.placement;
        }
        return std::nullopt;
    }

    std::size_t shortcut_of(const gitman::card_view_model& card, const std::u8string_view button)
    {
        for (const gitman::card_function_view& function : card.functions)
        {
            if (function.button_id == button)
                return function.shortcut;
        }
        return 0;
    }

    // 저장 요청 수와 마지막 저장 요청의 문서다.
    std::size_t save_count(const function_card_fixture& fixture)
    {
        std::size_t count { 0 };
        for (const gitman::operation_request& request : fixture.submitter.requests)
        {
            if (request.kind == gitman::operation_kind::save_document)
                ++count;
        }
        return count;
    }

    // 마지막 저장 요청이 끝났다고 알린다. 저장은 하나씩 나가므로 다음 변경의 저장은 이 뒤에 나간다.
    void acknowledge_save(function_card_fixture& fixture)
    {
        for (auto request = fixture.submitter.requests.rbegin(); request != fixture.submitter.requests.rend(); ++request)
        {
            if (request->kind != gitman::operation_kind::save_document)
                continue;
            fixture.controller.handle(gitman::document_saved_event { request->operation_id });
            return;
        }
        FAIL("저장 요청이 없습니다");
    }

    std::optional<std::u8string> saved_placement(const function_card_fixture& fixture, const std::u8string_view button)
    {
        for (auto request = fixture.submitter.requests.rbegin(); request != fixture.submitter.requests.rend(); ++request)
        {
            if (request->kind != gitman::operation_kind::save_document || request->document.has_value() == false)
                continue;
            for (const gitman::function_button_entry& entry : request->document->function_buttons->buttons)
            {
                const gitman::function_button* const value { std::get_if<gitman::function_button>(&entry) };
                if (value != nullptr && value->id == button)
                    return value->placement;
            }
        }
        return std::nullopt;
    }
}

TEST_CASE("Clicking a bar button runs it and clicking it again stops it", "[function_buttons][t10][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };

    const auto actions { click(harness, bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"c"))) };
    const auto* const intent { as_intent<gitman::run_function_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->id.value == u8"p00");
    REQUIRE(intent->button_id == u8"c");
    REQUIRE(forward(fixture, actions));
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.functions.requests[0].id.value == u8"p00");

    // 실행 중이면 같은 자리가 중지 버튼이다. 같은 카드의 다른 출력 수집 버튼은 비활성이다.
    const auto running { fixture.tree() };
    harness.controller.set_tree(running);
    REQUIRE(find(*running, ui::kind_function_bar_button, bar_owner(u8"b"))->enabled() == false);
    const auto stop { click(harness, bounds_of(*running, ui::kind_function_bar_button, bar_owner(u8"c"))) };
    REQUIRE(as_intent<gitman::stop_function_intent>(stop) != nullptr);
    REQUIRE(forward(fixture, stop));
    REQUIRE(fixture.functions.requests[0].token.cancelled());
}

TEST_CASE("Right click on an active bar or card button and Shift+F10 on a bar button open the placement menu", "[function_buttons][t10][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    const auto tree { fixture.tree() };

    SECTION("줄 버튼 우클릭")
    {
        interaction_harness harness { tree };
        const auto actions { right_click(harness, bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"c"))) };
        const auto* const intent { as_intent<gitman::open_function_placement_menu_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"p00");
        REQUIRE(intent->button_id == u8"c");
        REQUIRE(intent->from_bar);
        REQUIRE(forward(fixture, actions));
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.owner.value == u8"p00");
        REQUIRE(item_of(menu, gitman::context_menu_entry::place_function_on_bar).checked);
        REQUIRE(item_of(menu, gitman::context_menu_entry::place_function_on_card).checked == false);
        // 우클릭의 누름은 Tab 자리인 줄 버튼에 초점을 준다 (초점 옮기기의 전제).
        REQUIRE(harness.snapshot().focused == luil::ui_element_id { ui::kind_function_bar_button, bar_owner(u8"c") });
        // 선택은 그대로다.
        REQUIRE(fixture.snapshot()->selected == std::optional<gitman::project_id> { gitman::project_id { u8"p00" } });
    }

    SECTION("카드 버튼 우클릭")
    {
        interaction_harness harness { tree };
        const auto actions { right_click(harness, bounds_of(*tree, ui::kind_card_function, u8"p01:a")) };
        const auto* const intent { as_intent<gitman::open_function_placement_menu_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"p01");
        REQUIRE(intent->button_id == u8"a");
        REQUIRE(intent->from_bar == false);
        REQUIRE(forward(fixture, actions));
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.owner.value == u8"p01");
        REQUIRE(item_of(menu, gitman::context_menu_entry::place_function_on_card).checked);
        REQUIRE(item_of(menu, gitman::context_menu_entry::place_function_on_card).shortcut_text == u8"Alt+1");
        // 카드 버튼은 카드 안 버튼이라 선택을 바꾸지 않는다.
        REQUIRE(fixture.snapshot()->selected == std::optional<gitman::project_id> { gitman::project_id { u8"p00" } });
    }

    SECTION("초점을 가진 줄 버튼의 Shift+F10")
    {
        interaction_harness harness { tree };
        tab_to(harness, ui::kind_function_bar_button, bar_owner(u8"b"));
        const auto actions { harness.process(shift_key(luil::key_code::f10)) };
        const auto* const intent { as_intent<gitman::open_function_placement_menu_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->button_id == u8"b");
        REQUIRE(intent->from_bar);
        // 앵커는 버튼 가운데다.
        const luil::rect_f box { bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"b")) };
        REQUIRE(intent->anchor_x == box.x + box.width / 2.0f);
        REQUIRE(intent->anchor_y == box.y + box.height / 2.0f);
        REQUIRE(forward(fixture, actions));
        REQUIRE(item_of(fixture.menu(), gitman::context_menu_entry::place_function_on_bar).argument == u8"b");
    }
}

TEST_CASE("Right click on a disabled bar or card button opens nothing", "[function_buttons][t10][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");

    SECTION("줄 버튼")
    {
        // `a`가 돌면 같은 카드의 다른 출력 수집(`b`)은 비활성이다.
        fixture.run(u8"p00", u8"a");
        const auto tree { fixture.tree() };
        REQUIRE(find(*tree, ui::kind_function_bar_button, bar_owner(u8"b"))->enabled() == false);
        interaction_harness harness { tree };
        REQUIRE(right_click(harness, bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"b"))).empty());
        // 늦게 온 intent도 logic이 같은 판정으로 버린다.
        fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"b", true, 0.0f, 0.0f });
        REQUIRE(fixture.snapshot()->context_menu.has_value() == false);
    }

    SECTION("카드 버튼")
    {
        fixture.run(u8"p00", u8"b");
        const auto tree { fixture.tree() };
        REQUIRE(find(*tree, ui::kind_card_function, u8"p00:a")->enabled() == false);
        interaction_harness harness { tree };
        REQUIRE(right_click(harness, bounds_of(*tree, ui::kind_card_function, u8"p00:a")).empty());
        fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"a", false, 0.0f, 0.0f });
        REQUIRE(fixture.snapshot()->context_menu.has_value() == false);
    }
}

TEST_CASE("Right click on the card body still opens the card menu", "[function_buttons][t10][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    const auto actions { right_click(harness, bounds_of(*tree, ui::kind_card_body, u8"p01")) };
    const auto* const intent { as_intent<gitman::open_context_menu_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->id.value == u8"p01");
    REQUIRE(forward(fixture, actions));
    const gitman::context_menu_view menu { fixture.menu() };
    REQUIRE(menu.items.front().entry == gitman::context_menu_entry::open_repository);
    REQUIRE(function_item_count(menu) == 5u);
}

TEST_CASE("The placement menu is 121 tall with radios on the visible placement", "[function_buttons][t10][ui][context-menu]")
{
    SECTION("세 radio, 구분선, 기능 편집 자리")
    {
        function_card_fixture fixture { make_function_card_projects(1), sample_buttons() };
        select(fixture, u8"p00");
        fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"c", true, 10.0f, 20.0f });
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.items.size() == 4u);
        REQUIRE(menu.items[0].label == u8"카드에 두기");
        REQUIRE(menu.items[1].label == u8"기능 줄에 두기");
        REQUIRE(menu.items[2].label == u8"메뉴에만 두기");
        for (std::size_t index = 0; index < 3; ++index)
        {
            REQUIRE(menu.items[index].kind == gitman::context_menu_item_kind::radio);
            REQUIRE(menu.items[index].argument == u8"c");
            REQUIRE(menu.items[index].enabled);
        }
        REQUIRE(menu.items[1].checked);
        REQUIRE(menu.items[0].checked == false);
        REQUIRE(menu.items[2].checked == false);
        // `c`를 카드에 두면 문서 순서로 두 번째 `card`라 `Alt+2`다.
        REQUIRE(menu.items[0].shortcut_text == u8"Alt+2");
        // `기능 편집…`은 T12부터 그 기능을 고른 상태로 편집 dialog를 연다 (argument가 버튼 id).
        REQUIRE(menu.items[3].label == u8"기능 편집…");
        REQUIRE(menu.items[3].entry == gitman::context_menu_entry::edit_functions);
        REQUIRE(menu.items[3].argument == u8"c");
        REQUIRE(menu.items[3].separator_above);
        REQUIRE(menu.default_item.has_value() == false);

        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup.height == 121.0f);
        const auto* const root { dynamic_cast<const luil::menu_element*>(popup.tree->root()) };
        REQUIRE(root != nullptr);
        REQUIRE(root->config().items[1].kind == luil::menu_item_kind::radio);
        REQUIRE(root->config().items[1].checked);
        REQUIRE(root->config().items[0].shortcut_text == u8"Alt+2");
    }

    SECTION("4번째 card는 기능 줄 radio에 checked가 선다")
    {
        std::vector<gitman::function_button> buttons {
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"card"),
            make_function_card_button(u8"c", u8"다", u8"card"),
            make_function_card_button(u8"d", u8"라", u8"card"),
        };
        function_card_fixture fixture { make_function_card_projects(1), std::move(buttons) };
        select(fixture, u8"p00");
        const auto tree { fixture.tree() };
        // `d`는 모델 값이 `card`지만 보이는 배치는 `bar`라 기능 줄에 버튼이 있다.
        REQUIRE(find(*tree, ui::kind_function_bar_button, bar_owner(u8"d")) != nullptr);
        REQUIRE(find(*tree, ui::kind_card_function, u8"p00:d") == nullptr);
        interaction_harness harness { tree };
        REQUIRE(forward(fixture, right_click(harness, bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"d")))));
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(item_of(menu, gitman::context_menu_entry::place_function_on_bar).checked);
        REQUIRE(item_of(menu, gitman::context_menu_entry::place_function_on_card).checked == false);

        // 보이는 `card`가 3개라 `카드에 두기`는 사유를 붙여 비활성이고 받을 번호가 없다.
        const gitman::context_menu_item_view& card { item_of(menu, gitman::context_menu_entry::place_function_on_card) };
        REQUIRE(card.label == u8"카드에 두기 — 3개까지");
        REQUIRE(card.enabled == false);
        REQUIRE(card.shortcut_text.empty());
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup.height == 121.0f);

        // 그중 하나인 `card` 기능에서 열면 `카드에 두기`는 자기 자리라 활성이고 checked다.
        fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"b", false, 0.0f, 0.0f });
        const gitman::context_menu_view own_menu { fixture.menu() };
        const gitman::context_menu_item_view& own { item_of(own_menu, gitman::context_menu_entry::place_function_on_card) };
        REQUIRE(own.label == u8"카드에 두기");
        REQUIRE(own.enabled);
        REQUIRE(own.checked);
        REQUIRE(own.shortcut_text == u8"Alt+2");
    }
}

TEST_CASE("A placement change saves at once and does not ask for trust again", "[function_buttons][t10][logic]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"c", true, 0.0f, 0.0f });
    choose(fixture, gitman::context_menu_entry::place_function_on_card, u8"c");

    // 메뉴가 닫히고 저장 요청 하나가 새 배치를 싣는다.
    REQUIRE(fixture.snapshot()->context_menu.has_value() == false);
    REQUIRE(save_count(fixture) == 1u);
    REQUIRE(saved_placement(fixture, u8"c") == std::optional<std::u8string> { u8"card" });
    REQUIRE(placement_of(fixture.card(u8"p00"), u8"c") == gitman::function_placement::card);
    acknowledge_save(fixture);

    // 신뢰를 기록한 정의였다. 배치는 해시 입력이 아니라 누르면 확인 없이 곧바로 실행된다 (3.10).
    //  - 같은 세션 안의 보장이다. logic은 읽을 때 만든 해시 입력을 그대로 쓰고 배치 변경은 그 값을 건드리지 않는다.
    //    저장한 문서를 다음에 열어도 해시가 같은지는 문서 test("A placement change saved and read again keeps the
    //    trust digest")가 쓴 뒤 읽기로 본다.
    fixture.run(u8"p00", u8"c");
    REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value() == false);
    REQUIRE(fixture.functions.requests.size() == 1u);

    // 같은 값을 다시 고르면 쓸 것이 없다.
    fixture.controller.handle(gitman::set_function_placement_intent { u8"c", gitman::function_placement::card });
    REQUIRE(save_count(fixture) == 1u);

    // 버튼이 없는 기능(`menu`)도 intent로는 바꿀 수 있다 (T12의 편집 dialog가 같은 쓰기 길을 쓴다).
    fixture.controller.handle(gitman::set_function_placement_intent { u8"e", gitman::function_placement::bar });
    REQUIRE(save_count(fixture) == 2u);
    REQUIRE(saved_placement(fixture, u8"e") == std::optional<std::u8string> { u8"bar" });
}

TEST_CASE("Moving the last bar function removes the bar and moving one back restores it", "[function_buttons][t10][logic]")
{
    std::vector<gitman::function_button> buttons { make_function_card_button(u8"a", u8"가", u8"card"), make_function_card_button(u8"b", u8"나", u8"bar") };
    function_card_fixture fixture { make_function_card_projects(2), std::move(buttons) };
    select(fixture, u8"p00");
    REQUIRE(fixture.snapshot()->function_bar.has_value());
    REQUIRE(bounds_of(*fixture.tree(), ui::kind_log_pane).height == 124.0f);

    fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"b", true, 0.0f, 0.0f });
    choose(fixture, gitman::context_menu_entry::place_function_in_menu, u8"b");
    REQUIRE(fixture.snapshot()->function_bar.has_value() == false);
    REQUIRE(bounds_of(*fixture.tree(), ui::kind_log_pane).height == 160.0f);
    acknowledge_save(fixture);

    // 카드 버튼의 배치 메뉴로 `a`를 줄에 두면 줄이 다시 선다.
    fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"a", false, 0.0f, 0.0f });
    choose(fixture, gitman::context_menu_entry::place_function_on_bar, u8"a");
    REQUIRE(fixture.snapshot()->function_bar.has_value());
    REQUIRE(fixture.snapshot()->function_bar->functions.front().button_id == u8"a");
    REQUIRE(bounds_of(*fixture.tree(), ui::kind_log_pane).height == 124.0f);
    REQUIRE(save_count(fixture) == 2u);
}

TEST_CASE("Alt+N numbers follow a placement change", "[function_buttons][t10][logic]")
{
    function_card_fixture fixture { make_function_card_projects(1), sample_buttons() };
    select(fixture, u8"p00");
    REQUIRE(shortcut_of(fixture.card(u8"p00"), u8"a") == 1u);

    // `c`를 카드에 두면 문서 순서로 `a`, `c`다. 앞에 있는 `b`를 더 올리면 `c`의 번호가 3으로 밀린다.
    fixture.controller.handle(gitman::set_function_placement_intent { u8"c", gitman::function_placement::card });
    REQUIRE(shortcut_of(fixture.card(u8"p00"), u8"c") == 2u);
    acknowledge_save(fixture);
    fixture.controller.handle(gitman::set_function_placement_intent { u8"b", gitman::function_placement::card });
    const gitman::card_view_model card { fixture.card(u8"p00") };
    REQUIRE(shortcut_of(card, u8"a") == 1u);
    REQUIRE(shortcut_of(card, u8"b") == 2u);
    REQUIRE(shortcut_of(card, u8"c") == 3u);
    REQUIRE(placement_of(card, u8"d") == gitman::function_placement::bar);

    // 보이는 `card`가 3개면 더 올리지 않는다 (메뉴에서 이미 비활성이다).
    fixture.controller.handle(gitman::set_function_placement_intent { u8"d", gitman::function_placement::card });
    REQUIRE(placement_of(fixture.card(u8"p00"), u8"d") == gitman::function_placement::bar);
    REQUIRE(save_count(fixture) == 2u);
}

TEST_CASE("Taking one of the first three cards down lifts the fourth card", "[function_buttons][t10][logic]")
{
    std::vector<gitman::function_button> buttons {
        make_function_card_button(u8"a", u8"가", u8"card"),
        make_function_card_button(u8"b", u8"나", u8"card"),
        make_function_card_button(u8"c", u8"다", u8"card"),
        make_function_card_button(u8"d", u8"라", u8"card"),
    };
    function_card_fixture fixture { make_function_card_projects(1), std::move(buttons) };
    select(fixture, u8"p00");
    REQUIRE(placement_of(fixture.card(u8"p00"), u8"d") == gitman::function_placement::bar);

    fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"b", false, 0.0f, 0.0f });
    choose(fixture, gitman::context_menu_entry::place_function_in_menu, u8"b");

    // 모델 값은 바꾸지 않았는데 넘쳐 있던 `d`가 카드로 올라온다 (2.2.2 규칙 4). 번호는 `a`, `c`, `d` 순서다.
    const gitman::card_view_model card { fixture.card(u8"p00") };
    REQUIRE(placement_of(card, u8"d") == gitman::function_placement::card);
    REQUIRE(shortcut_of(card, u8"d") == 3u);
    REQUIRE(shortcut_of(card, u8"c") == 2u);
    REQUIRE(saved_placement(fixture, u8"d") == std::optional<std::u8string> { u8"card" });
    // `bar` 기능이 없어져 줄도 없다.
    REQUIRE(fixture.snapshot()->function_bar.has_value() == false);
    REQUIRE(find(*fixture.tree(), ui::kind_card_function, u8"p00:d") != nullptr);
}

TEST_CASE("A focused bar button that leaves the bar hands focus to the next button, the bar more, then the card more", "[function_buttons][t10][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    interaction_harness harness { fixture.tree() };

    // 초점을 쥔 버튼을 줄에서 내린 뒤 새 tree를 받고, UI thread가 하듯 고른 자리에 초점을 세운다.
    const auto move_away = [&](const std::u8string_view button, const gitman::context_menu_entry entry) -> std::optional<luil::ui_element_id> {
        tab_to(harness, ui::kind_function_bar_button, bar_owner(button));
        REQUIRE(forward(fixture, harness.process(shift_key(luil::key_code::f10))));
        const std::optional<std::uint64_t> before { fixture.snapshot()->function_focus.has_value() ? std::optional<std::uint64_t> { fixture.snapshot()->function_focus->id } : std::nullopt };
        choose(fixture, entry, button);
        const auto view { fixture.snapshot() };
        REQUIRE(view->function_focus.has_value());
        REQUIRE(std::optional<std::uint64_t> { view->function_focus->id } != before);
        const std::shared_ptr<const luil::ui_tree> tree { make_tree(*view) };
        harness.controller.set_tree(tree);
        // 초점을 쥔 버튼이 사라졌으므로 luil이 먼저 초점을 거둔다.
        REQUIRE(harness.snapshot().focused == luil::ui_element_id {});
        const std::optional<luil::ui_element_id> target { ui::function_focus_target(*tree, *view->function_focus) };
        if (target.has_value())
            static_cast<void>(harness.process(luil::access_focus_event { *target, u8"", at(0) }));
        return target;
    };

    // `b`를 메뉴로 내리면 줄의 다음 버튼 `c`다.
    REQUIRE(move_away(u8"b", gitman::context_menu_entry::place_function_in_menu) == luil::ui_element_id { ui::kind_function_bar_button, bar_owner(u8"c") });
    REQUIRE(harness.snapshot().focused == luil::ui_element_id { ui::kind_function_bar_button, bar_owner(u8"c") });

    // 줄의 마지막 버튼 `d`를 카드로 올리면 다음 버튼이 없어 줄의 `⋯`다.
    REQUIRE(move_away(u8"d", gitman::context_menu_entry::place_function_on_card) == luil::ui_element_id { ui::kind_function_bar_more });
    REQUIRE(harness.snapshot().focused == luil::ui_element_id { ui::kind_function_bar_more });

    // 남은 하나 `c`를 내리면 줄이 사라지므로 선택 카드의 `⋯`다.
    REQUIRE(move_away(u8"c", gitman::context_menu_entry::place_function_in_menu) == luil::ui_element_id { ui::kind_card_function_more, u8"p00" });
    REQUIRE(harness.snapshot().focused == luil::ui_element_id { ui::kind_card_function_more, u8"p00" });
    REQUIRE(fixture.snapshot()->function_bar.has_value() == false);
}

TEST_CASE("Focus moves skip folded or disabled candidates and give up when nothing is left", "[function_buttons][t10][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    // `c`가 돌면 `b`·`d`는 비활성이다. `c`를 메뉴로 내려도 비활성인 `d`는 초점을 받지 못해 줄의 `⋯`로 간다.
    fixture.run(u8"p00", u8"c");
    fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"c", true, 0.0f, 0.0f });
    choose(fixture, gitman::context_menu_entry::place_function_in_menu, u8"c");
    const auto view { fixture.snapshot() };
    REQUIRE(view->function_focus.has_value());
    REQUIRE(view->function_focus->next_buttons == std::vector<std::u8string> { u8"d" });
    const auto tree { make_tree(*view) };
    REQUIRE(ui::function_focus_target(*tree, *view->function_focus) == luil::ui_element_id { ui::kind_function_bar_more });

    // 갈 곳이 하나도 없으면(선택이 없어 줄에 버튼이 없고 줄의 `⋯`가 비활성이며, 그 카드가 목록에 없다) 값이 없다.
    function_card_fixture idle { make_function_card_projects(2), sample_buttons() };
    const auto unselected { make_tree(*idle.snapshot()) };
    REQUIRE(find(*unselected, ui::kind_function_bar_more)->enabled() == false);
    const gitman::function_focus_request nowhere { 9, gitman::project_id { u8"ghost" }, { u8"x" } };
    REQUIRE(ui::function_focus_target(*unselected, nowhere).has_value() == false);

    // 카드 버튼의 배치 메뉴는 Tab 자리가 아닌 버튼이라 초점을 옮기지 않는다. 실행을 끝내 `a`를 활성으로 둔다.
    fixture.completed(fixture.functions.requests.back(), gitman::function_run_outcome::exited, 0);
    const std::uint64_t id { view->function_focus->id };
    fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"a", false, 0.0f, 0.0f });
    choose(fixture, gitman::context_menu_entry::place_function_in_menu, u8"a");
    REQUIRE(placement_of(fixture.card(u8"p00"), u8"a") == gitman::function_placement::menu);
    REQUIRE(fixture.snapshot()->function_focus->id == id);
}

TEST_CASE("Opening the document again drops a pending focus request", "[function_buttons][t10][logic]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"b", true, 0.0f, 0.0f });
    choose(fixture, gitman::context_menu_entry::place_function_in_menu, u8"b");
    REQUIRE(fixture.snapshot()->function_focus.has_value());

    // 요청은 이전 문서의 카드 id를 가리킨다. 새 문서에 같은 id가 있어도 그 element로 초점이 가지 않게 버린다.
    fixture.controller.handle(gitman::open_document_intent { std::u8string { function_card_fixture::document_path } });
    gitman::document_loaded_event event {};
    gitman::workspace_document document {};
    document.document_path = function_card_fixture::document_path;
    document.projects = make_function_card_projects(2);
    gitman::function_buttons_definition definition {};
    for (gitman::function_button& button : sample_buttons())
        definition.buttons.emplace_back(std::move(button));
    document.function_buttons = std::move(definition);
    event.document = { std::move(document) };
    fixture.controller.handle(std::move(event));

    const auto view { fixture.snapshot() };
    REQUIRE(view->function_bar.has_value());
    REQUIRE(view->function_focus.has_value() == false);
}

TEST_CASE("The wheel over the function bar scrolls the log", "[function_buttons][t10][ui][interaction][log]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };

    // 줄은 로그 pane 160 안 맨 위 36이다. pane이 줄 아래로 내려갔어도 줄 위의 휠은 줄이 없을 때처럼 로그를 움직인다.
    const luil::rect_f bar { bounds_of(*tree, ui::kind_function_bar) };
    REQUIRE(bar.height > 0.0f);
    const std::vector<luil::input_action> over_bar { harness.process(luil::mouse_wheel_event { bar.x + 2.0f, bar.y + bar.height / 2.0f, -120.0f, at(0) }) };
    REQUIRE(as_intent<gitman::log_scroll_intent>(over_bar) != nullptr);

    // 줄 버튼 위도 같다. 글자 버튼은 휠을 받지 않는다.
    const luil::rect_f button { bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"b")) };
    const std::vector<luil::input_action> over_button { harness.process(luil::mouse_wheel_event { button.x + button.width / 2.0f, button.y + button.height / 2.0f, -120.0f, at(10) }) };
    REQUIRE(as_intent<gitman::log_scroll_intent>(over_button) != nullptr);

    // 줄 바로 위 카드 목록은 기존 목록 스크롤이다.
    const std::vector<luil::input_action> above { harness.process(luil::mouse_wheel_event { bar.x + 2.0f, bar.y - 4.0f, -120.0f, at(20) }) };
    REQUIRE(as_intent<gitman::scroll_intent>(above) != nullptr);
}

TEST_CASE("Esc that clears the selection drops the focus of a bar button", "[function_buttons][t10][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    select(fixture, u8"p00");
    interaction_harness harness { fixture.tree() };
    tab_to(harness, ui::kind_function_bar_button, bar_owner(u8"c"));

    REQUIRE(forward(fixture, harness.process(luil::key_pressed_event { luil::key_code::escape })));
    const auto view { fixture.snapshot() };
    REQUIRE(view->selected.has_value() == false);
    // 줄은 남지만 버튼이 없고 `⋯`는 비활성이며 선택 카드도 없어 갈 곳이 없다. 초점은 지워진다.
    REQUIRE(view->function_bar.has_value());
    REQUIRE(view->function_focus.has_value() == false);
    harness.controller.set_tree(make_tree(*view));
    REQUIRE(harness.snapshot().focused == luil::ui_element_id {});
}
