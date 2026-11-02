#include "helpers/function_card_fixture.h"
#include "helpers/ui_test_support.h"

#include "application/app_commands.h"
#include "presentation/function_presentation.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/context_menu_popup.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_icon_catalog.h"

#include "luil/generated/codicons.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

// 결정안의 카드 부분(function-buttons-design 2.3)의 키보드·메뉴를 본다 (6.1 T9 `ui_interaction_tests` 항목). 키는
// gitman policy를 꽂은 luil controller로 넣고, 나온 intent를 logic에 되먹여 실제로 무엇이 실행되는지 본다.
using namespace gitman::testing;

namespace {
    namespace ui = gitman::ui;

    luil::key_pressed_event alt_key(const luil::key_code key)
    {
        luil::key_pressed_event event { key };
        event.alt = true;
        return event;
    }

    luil::key_pressed_event shift_key(const luil::key_code key)
    {
        luil::key_pressed_event event { key };
        event.shift = true;
        return event;
    }

    // 키 하나를 넣고 나온 logic 메시지를 controller에 되먹인다. 메시지가 없으면 거짓이다.
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

    // Tab을 눌러 `kind`·`owner` element에 초점을 세운다. 그리기 순서가 Tab 순서다.
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

    // `Alt` 키 하나가 낸 단축키 intent(`Alt+N`의 실행, `Alt+↓`의 기능 메뉴)의 카드다. 그런 intent가 아니면 비어 있다.
    // 액션 목록이 살아 있는 동안 값을 꺼내 돌려준다.
    std::optional<std::u8string> shortcut_target(interaction_harness& harness, const luil::key_code key)
    {
        const auto actions { harness.process(alt_key(key)) };
        if (const auto* const intent { as_intent<gitman::run_function_shortcut_intent>(actions) }; intent != nullptr)
            return intent->id.value;
        if (const auto* const menu { as_intent<gitman::open_function_menu_intent>(actions) }; menu != nullptr)
            return menu->id.value;
        return std::nullopt;
    }

    // 예시 문서다: `a`(card), `b`(bar), `c`(card), `d`(card), `e`(menu).
    std::vector<gitman::function_button> shortcut_buttons()
    {
        return {
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"bar"),
            make_function_card_button(u8"c", u8"다", u8"card"),
            make_function_card_button(u8"d", u8"라", u8"card"),
            make_function_card_button(u8"e", u8"마"),
        };
    }

    std::u8string submitted_card(const function_card_fixture& fixture, const std::size_t index)
    {
        REQUIRE(index < fixture.functions.requests.size());
        return fixture.functions.requests[index].id.value;
    }

    // 실행이 어느 버튼의 것인지는 시작 로그 줄의 `[라벨]` 접두로 본다.
    std::u8string last_log_text(const function_card_fixture& fixture, const std::u8string_view card)
    {
        const gitman::operation_log_buffer* const log { fixture.controller.card_log(gitman::project_id { std::u8string { card } }) };
        REQUIRE(log != nullptr);
        REQUIRE(log->records().empty() == false);
        return log->records().back().entry.text;
    }

    std::vector<gitman::function_button> numbered_buttons(const std::size_t count)
    {
        std::vector<gitman::function_button> buttons {};
        for (std::size_t index = 0; index < count; ++index)
        {
            std::u8string id { u8"f" };
            id += static_cast<char8_t>(u8'0' + index / 10);
            id += static_cast<char8_t>(u8'0' + index % 10);
            buttons.push_back(make_function_card_button(id, u8"기능 " + id, index < 3 ? std::optional<std::u8string_view> { u8"card" } : std::nullopt));
        }
        return buttons;
    }
}

TEST_CASE("Alt+1..3 start the card placements in document order", "[function_buttons][t9][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), shortcut_buttons() };
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    // 카드 body를 누르면 그 카드가 초점 카드다 (policy `focused_card_`).
    REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));

    struct shortcut_case
    {
        luil::key_code key { luil::key_code::none };
        std::size_t number { 0 };
        std::u8string_view label {};
    };
    // `bar`(나)와 `menu`(마)는 번호가 없다. N은 문서의 `card` 순서다: 가, 다, 라.
    const std::vector<shortcut_case> cases {
        { luil::key_code::key_2, 2, u8"[다]" },
        { luil::key_code::key_1, 1, u8"[가]" },
        { luil::key_code::key_3, 3, u8"[라]" },
    };
    std::size_t expected_runs { 0 };
    for (const shortcut_case& sample : cases)
    {
        const auto actions { harness.process(alt_key(sample.key)) };
        const auto* const intent { as_intent<gitman::run_function_shortcut_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"p00");
        REQUIRE(intent->number == sample.number);
        REQUIRE(forward(fixture, actions));
        ++expected_runs;
        REQUIRE(fixture.functions.requests.size() == expected_runs);
        REQUIRE(submitted_card(fixture, expected_runs - 1) == u8"p00");
        REQUIRE(last_log_text(fixture, u8"p00").starts_with(sample.label));
        // 다음 키가 같은 카드의 다른 출력 수집과 부딪히지 않게 끝낸다.
        fixture.completed(fixture.functions.requests.back(), gitman::function_run_outcome::exited, 0);
    }

    // 자동 반복과 다른 수정자 조합은 단축키가 아니다.
    luil::key_pressed_event repeated { alt_key(luil::key_code::key_1) };
    repeated.repeat = true;
    REQUIRE(harness.process(repeated).empty());
    luil::key_pressed_event control { alt_key(luil::key_code::key_1) };
    control.control = true;
    REQUIRE(as_intent<gitman::run_function_shortcut_intent>(harness.process(control)) == nullptr);
    REQUIRE(as_intent<gitman::run_function_shortcut_intent>(harness.process(alt_key(luil::key_code::key_4))) == nullptr);
}

TEST_CASE("Alt+N does nothing on a running function and never stops it", "[function_buttons][t9][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), shortcut_buttons() };
    fixture.run(u8"p00", u8"a");
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.started(fixture.functions.requests[0], std::chrono::steady_clock::now());
    const std::size_t log_size { fixture.controller.card_log(gitman::project_id { u8"p00" })->records().size() };

    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));
    REQUIRE(forward(fixture, harness.process(alt_key(luil::key_code::key_1))));

    // 새 제출도, 중지 요청도, 로그 줄도 없다 (2.3.4).
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.functions.requests[0].token.cancelled() == false);
    REQUIRE(fixture.controller.card_log(gitman::project_id { u8"p00" })->records().size() == log_size);
    REQUIRE(fixture.card(u8"p00").function_running);
}

TEST_CASE("Alt+N does nothing when the Nth card function is outside the card's scope", "[function_buttons][t9][ui][interaction]")
{
    // `가`는 p01에만 적용된다. 번호는 문서 전체의 `card` 순서라 p00에서도 `Alt+1`은 `가`이고, p00의 열 첫 자리(다)가
    // 아니다 (2.3.1 규칙 1).
    std::vector<gitman::function_button> buttons { shortcut_buttons() };
    buttons[0].project_ids = std::vector<std::u8string> { u8"p01" };
    function_card_fixture fixture { make_function_card_projects(2), std::move(buttons) };
    const auto tree { fixture.tree() };
    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:a") == nullptr);
    interaction_harness harness { tree };
    REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));

    REQUIRE(forward(fixture, harness.process(alt_key(luil::key_code::key_1))));
    REQUIRE(fixture.functions.requests.empty());

    REQUIRE(forward(fixture, harness.process(alt_key(luil::key_code::key_2))));
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(last_log_text(fixture, u8"p00").starts_with(u8"[다]"));
}

TEST_CASE("Without a focus card Alt+N and Alt+Down do nothing", "[function_buttons][t9][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), shortcut_buttons() };
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    // Tab 초점도, 누른 카드도, 선택도 없다.
    REQUIRE(harness.process(alt_key(luil::key_code::key_1)).empty());
    REQUIRE(harness.process(alt_key(luil::key_code::arrow_down)).empty());

    // 카드를 누른 뒤 Esc로 선택과 초점 카드를 지우면 다시 같다. 같은 policy로 본다 — Esc가 `focused_card_`를 지웠는지가
    // 요점이다. tree는 선택이 풀린 snapshot으로 다시 짓는다.
    REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));
    REQUIRE(harness.policy.focused_card().has_value());
    REQUIRE(forward(fixture, harness.process(luil::key_pressed_event { luil::key_code::escape })));
    REQUIRE(harness.policy.focused_card().has_value() == false);
    REQUIRE(fixture.snapshot()->selected.has_value() == false);
    harness.controller.set_tree(fixture.tree());
    REQUIRE(harness.process(alt_key(luil::key_code::key_1)).empty());
    REQUIRE(harness.process(alt_key(luil::key_code::arrow_down)).empty());
    REQUIRE(fixture.functions.requests.empty());
}

TEST_CASE("Alt+N and Alt+Down do nothing while a dialog is open", "[function_buttons][t9][ui][interaction]")
{
    SECTION("환경설정 dialog가 떠 있으면 키는 그 dialog의 것이다")
    {
        function_card_fixture fixture { make_function_card_projects(2), shortcut_buttons() };
        fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"p00" } });
        fixture.controller.handle(gitman::open_settings_intent {});
        const auto tree { fixture.tree() };
        REQUIRE(tree->ids_of_kind(luil::ui_element_kind::modal_host).empty() == false);
        interaction_harness harness { tree };
        REQUIRE(harness.process(alt_key(luil::key_code::key_1)).empty());
        REQUIRE(harness.process(alt_key(luil::key_code::arrow_down)).empty());
        REQUIRE(fixture.functions.requests.empty());
    }

    SECTION("기능 확인 dialog가 떠 있으면 키도, 늦게 온 기능 메뉴 intent도 아무것도 하지 않는다")
    {
        // `confirm` 기능은 신뢰한 정의여도 미리보기 dialog를 연다 (T7).
        gitman::function_button guarded { make_function_card_button(u8"a", u8"가", u8"card") };
        guarded.confirm = true;
        function_card_fixture fixture { make_function_card_projects(2), { guarded } };
        fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"p00" } });
        fixture.run(u8"p00", u8"a");
        REQUIRE(fixture.functions.requests.empty());
        const auto tree { fixture.tree() };
        REQUIRE(find(*tree, ui::kind_function_confirm_dialog) != nullptr);
        interaction_harness harness { tree };
        REQUIRE(harness.process(alt_key(luil::key_code::key_1)).empty());
        REQUIRE(harness.process(alt_key(luil::key_code::arrow_down)).empty());
        fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
        REQUIRE(fixture.snapshot()->context_menu.has_value() == false);
        REQUIRE(fixture.functions.requests.empty());
    }
}

TEST_CASE("Alt+N on a disabled function leaves one warning line and on a filtered card does nothing", "[function_buttons][t9][ui][interaction]")
{
    // 비활성 기능의 `Alt+N`은 버튼의 늦은 클릭과 같은 길이라 사유 한 줄(warning)을 남기고 실행하지 않는다 (T6 규칙).
    gitman::function_button runtime { make_function_card_button(u8"deploy", u8"배포", u8"card") };
    runtime.args = { u8"${branch}" };
    function_card_fixture fixture { make_function_card_projects(2), { runtime } };
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));
    REQUIRE(forward(fixture, harness.process(alt_key(luil::key_code::key_1))));
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(last_log_text(fixture, u8"p00") == u8"[배포] 저장소를 조회한 뒤에 쓸 수 있습니다");

    // 필터로 목록에서 빠진 카드는 policy의 초점 카드로 남아 있어도 기능을 시작하지 않는다.
    fixture.query(u8"p00");
    fixture.controller.handle(gitman::set_filter_intent { u8"p01" });
    fixture.controller.handle(gitman::run_function_shortcut_intent { gitman::project_id { u8"p00" }, 1 });
    REQUIRE(fixture.functions.requests.empty());
    fixture.controller.handle(gitman::set_filter_intent { u8"" });
    fixture.controller.handle(gitman::run_function_shortcut_intent { gitman::project_id { u8"p00" }, 1 });
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("Alt+Down opens the focus card's function menu at its more button", "[function_buttons][t9][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(3), shortcut_buttons() };
    fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"p02" } });
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    tab_to(harness, ui::kind_card_function_more, u8"p01");

    const auto actions { harness.process(alt_key(luil::key_code::arrow_down)) };
    const auto* const intent { as_intent<gitman::open_function_menu_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->id.value == u8"p01");
    const luil::rect_f more { bounds_of(*tree, ui::kind_card_function_more, u8"p01") };
    REQUIRE(intent->anchor_x == more.x);
    REQUIRE(intent->anchor_y == more.y + more.height);
    REQUIRE(forward(fixture, actions));

    // 기능 메뉴가 그 카드 것으로 열리고 선택은 바뀌지 않는다 (2.2.6).
    const gitman::context_menu_view menu { fixture.menu() };
    REQUIRE(menu.owner.value == u8"p01");
    REQUIRE(function_item_count(menu) == 5u);
    REQUIRE(fixture.snapshot()->selected == std::optional<gitman::project_id> { gitman::project_id { u8"p02" } });

    // `⋯` 클릭도 같은 intent다.
    const auto clicked { click(harness, more) };
    const auto* const clicked_intent { as_intent<gitman::open_function_menu_intent>(clicked) };
    REQUIRE(clicked_intent != nullptr);
    REQUIRE(clicked_intent->id.value == u8"p01");
    REQUIRE(clicked_intent->anchor_y == more.y + more.height);

    // Alt+↓은 카드 선택을 옮기지 않는다. 설계가 정하지 않은 Alt+↑은 예전처럼 카드 이동이다.
    REQUIRE(as_intent<gitman::select_card_intent>(harness.process(alt_key(luil::key_code::arrow_down))) == nullptr);
    REQUIRE(as_intent<gitman::select_card_intent>(harness.process(alt_key(luil::key_code::arrow_up))) != nullptr);
}

TEST_CASE("The focus card is the Tab focus owner, then the clicked card, then the selection", "[function_buttons][t9][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(3), shortcut_buttons() };

    SECTION("Tab 초점이 카드 버튼이면 그 owner 카드다")
    {
        const auto tree { fixture.tree() };
        interaction_harness harness { tree };
        // 누른 카드(p00)와 Tab 초점 카드(p01)가 다르면 Tab 초점이 이긴다.
        REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));
        tab_to(harness, ui::kind_card_function_more, u8"p01");
        REQUIRE(shortcut_target(harness, luil::key_code::key_1) == std::optional<std::u8string> { u8"p01" });

        // 내장 버튼도 카드 버튼이다.
        tab_to(harness, ui::kind_card_refresh, u8"p02");
        REQUIRE(shortcut_target(harness, luil::key_code::key_1) == std::optional<std::u8string> { u8"p02" });
    }

    SECTION("Tab 초점이 카드 밖이면 누른 카드다")
    {
        const auto tree { fixture.tree() };
        interaction_harness harness { tree };
        REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p01"))));
        REQUIRE(shortcut_target(harness, luil::key_code::key_1) == std::optional<std::u8string> { u8"p01" });
    }

    SECTION("`card` 기능 버튼을 누른 것은 초점 카드를 바꾸지 않는다")
    {
        // 2.2.6대로 카드 안 버튼 클릭은 `focused_card_`를 세우지 않는다. `card` 버튼은 Tab 자리가 아니라 누르면 초점도
        // 걷히므로, 뒤따르는 `Alt+N`은 앞서 누른 카드 body(p00)의 것이다.
        fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"p02" } });
        const auto tree { fixture.tree() };
        interaction_harness harness { tree };
        REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));
        REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_function, u8"p01:a"))));
        REQUIRE(harness.snapshot().focused == luil::ui_element_id {});
        REQUIRE(harness.policy.focused_card() == std::optional<gitman::project_id> { gitman::project_id { u8"p00" } });
        REQUIRE(shortcut_target(harness, luil::key_code::key_2) == std::optional<std::u8string> { u8"p00" });
    }

    SECTION("둘 다 없으면 선택 카드다")
    {
        fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"p02" } });
        const auto tree { fixture.tree() };
        interaction_harness harness { tree };
        REQUIRE(harness.policy.focused_card().has_value() == false);
        REQUIRE(shortcut_target(harness, luil::key_code::key_1) == std::optional<std::u8string> { u8"p02" });
        REQUIRE(shortcut_target(harness, luil::key_code::arrow_down) == std::optional<std::u8string> { u8"p02" });
    }
}

TEST_CASE("Shift+F10 on a focused more button opens that card's menu", "[function_buttons][t9][ui][interaction]")
{
    // luil이 policy보다 먼저 초점의 조상에서 우클릭 액션을 찾는다 (2.3.4). `⋯`의 조상은 카드 body라 그 카드의
    // 카드 메뉴가 기능 절과 함께 열린다. policy는 바꾸지 않았다.
    function_card_fixture fixture { make_function_card_projects(3), shortcut_buttons() };
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    REQUIRE(forward(fixture, click(harness, bounds_of(*tree, ui::kind_card_body, u8"p00"))));
    tab_to(harness, ui::kind_card_function_more, u8"p01");

    const auto actions { harness.process(shift_key(luil::key_code::f10)) };
    const auto* const intent { as_intent<gitman::open_context_menu_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->id.value == u8"p01");
    REQUIRE(forward(fixture, actions));
    const gitman::context_menu_view menu { fixture.menu() };
    REQUIRE(menu.owner.value == u8"p01");
    REQUIRE(function_item_count(menu) == 5u);
}

TEST_CASE("Function and card menus are 589, 433 and 407 tall", "[function_buttons][t9][ui][context-menu]")
{
    SECTION("기능 메뉴: 기능 20 + 구분선 + 기능 편집 자리 + 문서 다시 읽기 = 589")
    {
        function_card_fixture fixture { make_function_card_projects(1), numbered_buttons(20) };
        fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.items.size() == 22u);
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup.height == 589.0f);
        REQUIRE(popup.id == ui::card_menu_popup_id);
        require_clean_tree(*popup.tree);

        // 아래 구역은 구분선 뒤에 있다. `기능 편집…`은 T12부터 편집 dialog를 열고(버튼 id 없이), `문서 다시 읽기`는
        // T11부터 활성이다.
        REQUIRE(menu.items[20].separator_above);
        REQUIRE(menu.items[20].entry == gitman::context_menu_entry::edit_functions);
        REQUIRE(menu.items[20].label == u8"기능 편집…");
        REQUIRE(menu.items[20].argument.empty());
        REQUIRE(menu.items[21].entry == gitman::context_menu_entry::reload_document);
        REQUIRE(menu.items[21].label == u8"문서 다시 읽기");
        REQUIRE(menu.items[21].enabled);
    }

    SECTION("카드 메뉴: 기존 6 + 구분선 + 기능 8 + 기능 더 보기 + 기능 편집 = 433")
    {
        function_card_fixture fixture { make_function_card_projects(1), numbered_buttons(9) };
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.items.size() == 16u);
        REQUIRE(function_item_count(menu) == 8u);
        REQUIRE(menu.items[6].separator_above);
        REQUIRE(menu.items[14].entry == gitman::context_menu_entry::more_functions);
        REQUIRE(menu.items[14].label == u8"기능 더 보기…");
        REQUIRE(menu.items[15].entry == gitman::context_menu_entry::edit_functions);
        REQUIRE(ui::build_context_menu_popup(menu, 1.0f).height == 433.0f);
    }

    SECTION("기능이 정확히 8개면 기능 더 보기가 없어 407")
    {
        function_card_fixture fixture { make_function_card_projects(1), numbered_buttons(8) };
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.items.size() == 15u);
        REQUIRE(ui::build_context_menu_popup(menu, 1.0f).height == 407.0f);
    }

    SECTION("적용 기능이 없는 카드의 메뉴는 그대로 164")
    {
        gitman::function_button elsewhere { make_function_card_button(u8"build", u8"빌드", u8"card") };
        elsewhere.project_ids = std::vector<std::u8string> { u8"other" };
        function_card_fixture fixture { make_function_card_projects(1), { elsewhere } };
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.items.size() == 6u);
        const luil::win32::ui_popup popup { ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup.height == 164.0f);
        REQUIRE(popup.width == 190.0f);
    }
}

TEST_CASE("The function menu default key is the first enabled function only while idle", "[function_buttons][t9][ui][context-menu]")
{
    // 첫 기능이 비활성(조회 전 `${branch}`)이면 Enter가 고를 수 없으므로 첫 활성 기능이 기본이다 (T8에서 넘어온 일).
    gitman::function_button runtime { make_function_card_button(u8"deploy", u8"배포", u8"card") };
    runtime.args = { u8"${branch}" };
    function_card_fixture fixture { make_function_card_projects(1), { runtime, make_function_card_button(u8"build", u8"빌드", u8"card"), make_function_card_button(u8"test", u8"테스트") } };

    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
    const gitman::context_menu_view idle { fixture.menu() };
    REQUIRE(idle.items[0].enabled == false);
    REQUIRE(idle.default_item == std::optional<std::size_t> { 1u });
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(idle, 1.0f) };
    const auto* const root { dynamic_cast<const luil::menu_element*>(popup.tree->root()) };
    REQUIRE(root != nullptr);
    REQUIRE(root->config().default_key == u8"run_function:build");

    // 메뉴를 열고 Enter 한 번이면 그 기능이 실행된다.
    {
        interaction_harness harness { fixture.tree() };
        harness.controller.set_surface_trees({ { popup.id, popup.tree } });
        const auto actions { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
        const auto* const select { as_intent<gitman::select_context_menu_item_intent>(actions) };
        REQUIRE(select != nullptr);
        REQUIRE(select->entry == gitman::context_menu_entry::run_function);
        REQUIRE(select->argument == u8"build");
        REQUIRE(forward(fixture, actions));
        REQUIRE(fixture.functions.requests.size() == 1u);
    }

    // 그 카드에서 출력 수집이 돌고 있으면 기본 항목이 없다. 습관적인 Enter가 작업을 끊지 않는다.
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
    const gitman::context_menu_view running { fixture.menu() };
    REQUIRE(running.default_item.has_value() == false);
    REQUIRE(ui::build_context_menu_popup(running, 1.0f).tree != nullptr);
    REQUIRE(dynamic_cast<const luil::menu_element*>(ui::build_context_menu_popup(running, 1.0f).tree->root())->config().default_key.empty());

    // 카드 메뉴에는 기본 항목이 없다 (기존과 같다).
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
    REQUIRE(fixture.menu().default_item.has_value() == false);
}

TEST_CASE("Function status labels append running time, failure or disabled reason","[function_buttons][t9][ui][context-menu]")
{
    gitman::card_function_view function {};
    function.label = u8"빌드";
    function.enabled = true;
    REQUIRE(gitman::function_menu_label(function, std::chrono::seconds { 0 }) == u8"빌드");

    function.running = true;
    REQUIRE(gitman::function_menu_label(function, std::chrono::seconds { 12 }) == u8"빌드 — 실행 중 00:12");
    REQUIRE(gitman::function_menu_label(function, std::chrono::seconds { 75 }) == u8"빌드 — 실행 중 01:15");

    gitman::card_function_view failed {};
    failed.label = u8"테스트";
    failed.enabled = true;
    failed.failed = true;
    failed.exit_code = 1;
    REQUIRE(gitman::function_menu_label(failed, {}) == u8"테스트 — 실패 (종료 코드 1)");
    failed.exit_code = static_cast<std::int32_t>(0xC0000005u);
    REQUIRE(gitman::function_menu_label(failed, {}) == u8"테스트 — 실패 (종료 코드 0xC0000005)");
    failed.exit_code.reset();
    REQUIRE(gitman::function_menu_label(failed, {}) == u8"테스트 — 실패");

    gitman::card_function_view disabled {};
    disabled.label = u8"린트";
    disabled.disabled_reason = u8"저장소를 조회한 뒤에 쓸 수 있습니다";
    REQUIRE(gitman::function_menu_label(disabled, {}) == u8"린트 — 저장소를 조회한 뒤에 쓸 수 있습니다");

    // logic이 짓는 메뉴 라벨도 같은 형식이다. 경과 시간은 메뉴를 연 시각의 값이다.
    function_card_fixture fixture { make_function_card_projects(1), { make_function_card_button(u8"build", u8"빌드", u8"card"), make_function_card_button(u8"test", u8"테스트") } };
    fixture.run(u8"p00", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.started(fixture.functions.requests[0], std::chrono::steady_clock::now() - std::chrono::seconds { 12 });
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
    const gitman::context_menu_view menu { fixture.menu() };
    REQUIRE(menu.items[0].entry == gitman::context_menu_entry::stop_function);
    REQUIRE(menu.items[0].label.starts_with(u8"빌드 — 실행 중 00:1"));
    REQUIRE(menu.items[0].icon == u8"stop-circle");
    REQUIRE(menu.items[0].shortcut_text == u8"Alt+1");
    REQUIRE(menu.items[1].label == u8"테스트 — 이 카드에서 다른 기능이 실행 중입니다");
    REQUIRE(menu.items[1].enabled == false);

    // 메뉴가 떠 있는 동안 snapshot이 다시 지어져도(기능 출력·조회 event마다) 라벨은 연 시각에서 멈춘다. 초마다
    // 바뀌면 초점 항목의 접근 이름이 바뀌어 낭독이 되풀이된다 (2.3.4).
    std::this_thread::sleep_for(std::chrono::milliseconds { 1100 });
    REQUIRE(fixture.menu().items[0].label == menu.items[0].label);
}

TEST_CASE("Function menu items carry their state in the accessible name", "[function_buttons][t9][ui][context-menu]")
{
    // 메뉴 항목의 접근 이름은 라벨뿐이다. 상태는 라벨에 들고 단축키 열은 읽히지 않는다 (2.3.2).
    function_card_fixture fixture { make_function_card_projects(1), { make_function_card_button(u8"build", u8"빌드", u8"card"), make_function_card_button(u8"test", u8"테스트", u8"bar") } };
    fixture.run(u8"p00", u8"test");
    fixture.completed(fixture.functions.requests[0], gitman::function_run_outcome::exited, 2);
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(fixture.menu(), 1.0f) };

    const luil::ui_element* const build { popup.tree->find({ luil::ui_element_kind::menu_item, u8"run_function:build" }) };
    const luil::ui_element* const test { popup.tree->find({ luil::ui_element_kind::menu_item, u8"run_function:test" }) };
    REQUIRE(build != nullptr);
    REQUIRE(test != nullptr);
    REQUIRE(build->accessibility().name == u8"빌드");
    REQUIRE(test->accessibility().name == u8"테스트 — 실패 (종료 코드 2)");

    // 카드 버튼과 같은 이름 규칙이다.
    gitman::card_function_view view {};
    view.label = u8"빌드";
    view.enabled = true;
    REQUIRE(gitman::function_button_access_name(view, u8"frontend") == u8"빌드, frontend");
    view.running = true;
    REQUIRE(gitman::function_button_access_name(view, u8"frontend") == u8"빌드, frontend, 실행 중");
    view.running = false;
    view.failed = true;
    REQUIRE(gitman::function_button_access_name(view, u8"frontend") == u8"빌드, frontend, 마지막 실패");
    view.exit_code = 3;
    REQUIRE(gitman::function_button_access_name(view, u8"frontend") == u8"빌드, frontend, 마지막 실패: 종료 코드 3");
    REQUIRE(gitman::function_more_access_name(u8"frontend", 4) == u8"기능 더 보기, frontend, 4개");
}

TEST_CASE("Choosing function menu items runs, stops and opens more", "[function_buttons][t9][ui][context-menu]")
{
    function_card_fixture fixture { make_function_card_projects(2), numbered_buttons(10) };
    fixture.query(u8"p00");

    // 카드 메뉴의 기능 항목은 버튼 클릭과 같은 실행 경로다. 카드 메뉴는 그 카드를 선택한다(기존 동작).
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 30.0f, 40.0f });
    fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::run_function, u8"f04" });
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(last_log_text(fixture, u8"p00").starts_with(u8"[기능 f04]"));
    REQUIRE(fixture.snapshot()->context_menu.has_value() == false);

    // 실행 중인 항목은 중지다. 메뉴가 떠 있는 사이 시작된 기능의 늦은 "실행" 클릭은 닫기만 한다.
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 30.0f, 40.0f });
    fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::run_function, u8"f04" });
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.functions.requests[0].token.cancelled() == false);
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 30.0f, 40.0f });
    fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::stop_function, u8"f04" });
    REQUIRE(fixture.functions.requests[0].token.cancelled());

    // `기능 더 보기…`는 카드 메뉴 자리에 같은 카드의 기능 메뉴를 연다.
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 30.0f, 40.0f });
    fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::more_functions, {} });
    const gitman::context_menu_view more { fixture.menu() };
    REQUIRE(more.owner.value == u8"p00");
    REQUIRE(more.anchor_x == 30.0f);
    REQUIRE(more.anchor_y == 40.0f);
    REQUIRE(function_item_count(more) == 10u);

    // `문서 다시 읽기`는 안전한 다시 읽기다 (T11, 4.9). 중지한 f04의 completed가 아직 오지 않아 출력 수집 실행이 남아
    // 있으므로 먼저 묻는다. 취소하면 지금 문서 그대로이고 읽기를 내지 않는다.
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::reload_document, {} });
    REQUIRE(fixture.snapshot()->context_menu.has_value() == false);
    REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
    REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value());
    REQUIRE(fixture.snapshot()->function_confirm_dialog->mode == gitman::function_confirm_mode::running);
    REQUIRE(fixture.submitter.requests.empty());
    fixture.controller.handle(gitman::cancel_function_run_intent {});

    // `기능 편집…`은 T12부터 logic intent로 편집 dialog를 연다 (2.7). 앱 명령이 아니다.
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 30.0f, 40.0f });
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(fixture.menu(), 1.0f) };
    interaction_harness harness { fixture.tree() };
    harness.controller.set_surface_trees({ { popup.id, popup.tree } });
    const luil::ui_element* const edit { popup.tree->find({ luil::ui_element_kind::menu_item, u8"edit_functions" }) };
    REQUIRE(edit != nullptr);
    const luil::rect_f box { edit->bounds() };
    REQUIRE(harness.controller.process(luil::pointer_pressed_event { box.x + 2.0f, box.y + 2.0f, luil::pointer_button::left, at(0), false, popup.id }).empty());
    const auto actions { harness.controller.process(luil::pointer_released_event { box.x + 2.0f, box.y + 2.0f, luil::pointer_button::left, at(0), popup.id }) };
    REQUIRE(as_app_command(actions) == nullptr);
    const auto* const intent { as_intent<gitman::select_context_menu_item_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->entry == gitman::context_menu_entry::edit_functions);
    fixture.controller.handle(*intent);
    REQUIRE(fixture.snapshot()->function_editor.has_value());
    REQUIRE(fixture.snapshot()->context_menu.has_value() == false);
}

TEST_CASE("Opening the function menu keeps the selection and the failure dots", "[function_buttons][t9][ui][context-menu]")
{
    // `⋯`는 카드 안 버튼이라 선택을 바꾸지 않는다 (2.2.6). 선택이 바뀌면 지워지는 실패 점도 그대로다.
    function_card_fixture fixture { make_function_card_projects(2), { make_function_card_button(u8"build", u8"빌드", u8"bar") } };
    fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"p00" } });
    fixture.run(u8"p01", u8"build");
    fixture.completed(fixture.functions.requests[0], gitman::function_run_outcome::exited, 1);
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p01" }, 0.0f, 0.0f });

    const auto view { fixture.snapshot() };
    REQUIRE(view->selected == std::optional<gitman::project_id> { gitman::project_id { u8"p00" } });
    REQUIRE(fixture.card(u8"p01").functions[0].failed);
    REQUIRE(fixture.menu().items[0].label == u8"빌드 — 실패 (종료 코드 1)");
}

TEST_CASE("Function icon names resolve through one table", "[function_buttons][t9][ui][context-menu]")
{
    // D16의 이름 표다. 모르는 이름은 기본 `play`이고, 메뉴의 기존 항목은 모르는 이름이면 아이콘이 없다.
    REQUIRE(ui::function_icon_glyph(u8"tools") == luil::codicons::icon_tools);
    REQUIRE(ui::function_icon_glyph(u8"beaker") == luil::codicons::icon_beaker);
    REQUIRE(ui::function_icon_glyph(u8"run-all") == luil::codicons::icon_run_all);
    REQUIRE(ui::function_icon_glyph(u8"window") == luil::codicons::icon_window);
    REQUIRE(ui::function_icon_glyph(u8"없는-아이콘") == luil::codicons::icon_play);
    REQUIRE(ui::function_icon_glyph(u8"") == luil::codicons::icon_play);
    // 메뉴 전용 이름은 기능 아이콘이 아니다. 쉬는 기능이 중지 버튼이나 `⋯`처럼 보이지 않는다.
    REQUIRE(ui::function_icon_glyph(u8"stop-circle") == luil::codicons::icon_play);
    REQUIRE(ui::function_icon_glyph(u8"ellipsis") == luil::codicons::icon_play);
    REQUIRE(ui::context_menu_icon_glyph(u8"없는-아이콘") == 0);
    REQUIRE(ui::context_menu_icon_glyph(u8"stop-circle") == luil::codicons::icon_stop_circle);
    REQUIRE(ui::context_menu_icon_glyph(u8"beaker") == luil::codicons::icon_beaker);

    // 메뉴의 기능 항목은 카드 버튼과 같은 글리프다. 모르는 이름도 `play`다.
    gitman::function_button odd { make_function_card_button(u8"odd", u8"이상한 아이콘", u8"card") };
    odd.icon = u8"없는-아이콘";
    gitman::function_button lab { make_function_card_button(u8"lab", u8"실험") };
    lab.icon = u8"beaker";
    function_card_fixture fixture { make_function_card_projects(1), { odd, lab } };
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 0.0f, 0.0f });
    const luil::win32::ui_popup popup { ui::build_context_menu_popup(fixture.menu(), 1.0f) };
    const auto* const root { dynamic_cast<const luil::menu_element*>(popup.tree->root()) };
    REQUIRE(root != nullptr);
    REQUIRE(root->config().items[0].icon == luil::codicons::icon_play);
    REQUIRE(root->config().items[1].icon == luil::codicons::icon_beaker);
    REQUIRE(root->config().items[2].icon == luil::codicons::icon_gear);
    REQUIRE(root->config().items[3].icon == luil::codicons::icon_sync);
}

TEST_CASE("Context menu width grows with its labels between 190 and 320", "[function_buttons][t9][ui][context-menu]")
{
    // 폭은 `clamp(width_for, 190, 320)`이다 (2.3.2). 짧은 메뉴는 190이고 아주 긴 라벨도 320에서 멈춘다.
    gitman::context_menu_view menu {};
    menu.owner.value = u8"p00";
    gitman::context_menu_item_view item {};
    item.entry = gitman::context_menu_entry::run_function;
    item.argument = u8"a";
    item.label = u8"짧음";
    menu.items = { item };
    REQUIRE(ui::build_context_menu_popup(menu, 1.0f).width == 190.0f);

    menu.items[0].label = std::u8string(400, u8'W');
    menu.items[0].shortcut_text = u8"Alt+1";
    REQUIRE(ui::build_context_menu_popup(menu, 1.0f).width == 320.0f);

    // 그 사이는 `width_for`가 잰 값 그대로다.
    luil::menu_config config {};
    luil::menu_item_config row {};
    row.label = u8"아주 긴 기능 이름 — 실행 중 00:12";
    row.shortcut_text = u8"Alt+1";
    config.items.push_back(row);
    const float measured { luil::menu_element::width_for(config) };
    menu.items[0].label = row.label;
    REQUIRE(ui::build_context_menu_popup(menu, 1.0f).width == std::clamp(measured, 190.0f, 320.0f));
}
