#include "helpers/ui_test_support.h"

#include "application/app_commands.h"
#include "presentation/ui/element_kinds.h"

#include "luil/ui/ui_events.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <variant>
#include <vector>

namespace {
    namespace ui = gitman::ui;
} // namespace

// hover/press 상태 기계, 더블 클릭 판정, drag 임계, 키 조합 전수는 luil test가
// 검증한다 (ADR-007). 여기 남는 것은 gitman의 의미다 — 어느 클릭·키가 어떤
// intent나 앱 명령이 되는가.
using namespace gitman::testing;

TEST_CASE("Clicks resolve to intents through the tree", "[ui][interaction]")
{
    const auto tree { make_tree(2) };
    interaction_harness harness { tree };

    SECTION("refresh 버튼")
    {
        const auto actions { click(harness, bounds_of(*tree, ui::kind_card_refresh, u8"card-1")) };
        const auto* const intent { as_intent<gitman::refresh_card_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-1");
    }

    SECTION("카드 body는 선택이다")
    {
        const auto actions { click(harness, bounds_of(*tree, ui::kind_card_body, u8"card-0")) };
        const auto* const intent { as_intent<gitman::select_card_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.has_value());
        REQUIRE(intent->id->value == u8"card-0");
        // 마우스 선택은 키보드 탐색의 초점도 잇는다 (policy::on_click).
        REQUIRE(harness.policy.focused_card().has_value());
        REQUIRE(harness.policy.focused_card()->value == u8"card-0");
    }

    SECTION("빈 영역 클릭은 선택 해제다")
    {
        const auto actions { click(harness, luil::rect_f { 700.0f, 580.0f, 4.0f, 4.0f }) };
        const auto* const intent { as_intent<gitman::select_card_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.has_value() == false);
    }

    SECTION("전체 refresh")
    {
        const auto actions { click(harness, bounds_of(*tree, ui::kind_toolbar_refresh_all)) };
        const gitman::logic_message* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(*message));
    }
}

TEST_CASE("Disabled stage seven buttons produce nothing but still show a tooltip target", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };

    const luil::rect_f update { bounds_of(*tree, ui::kind_card_update, u8"card-0") };
    REQUIRE(click(harness, update).empty());
    REQUIRE(click(harness, bounds_of(*tree, ui::kind_card_switch, u8"card-0")).empty());

    // 비활성 버튼도 hover 대상이 되어 tooltip을 예고한다. 강조는 렌더러가
    // enabled로 거른다.
    REQUIRE(harness.process(luil::pointer_moved_event { update.x + 1.0f, update.y + 1.0f, at(50) }).empty());
    REQUIRE(harness.snapshot().hovered.kind == ui::kind_card_update);
}

TEST_CASE("The open document button asks the UI thread for the file dialog", "[ui][interaction]")
{
    const auto tree { make_tree(0, gitman::view_empty_state::no_document) };
    interaction_harness harness { tree };

    const auto actions { click(harness, bounds_of(*tree, ui::kind_toolbar_open_document)) };
    const luil::app_ui_command* const command { as_app_command(actions) };
    REQUIRE(command != nullptr);
    REQUIRE(gitman::parse_app_command(command->command) == gitman::app_command::show_open_document_dialog);
    // 문서 열기는 인자가 없다 (시작 폴더를 지정하지 않는다).
    REQUIRE(command->argument.empty());
}

TEST_CASE("The close document button asks logic to close the document", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };

    const auto actions { click(harness, bounds_of(*tree, ui::kind_toolbar_close_document)) };
    const gitman::logic_message* const message { as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::close_document_intent>(*message));
}

TEST_CASE("Caption buttons return window commands", "[ui][interaction]")
{
    // caption 버튼은 luil이 만들고 luil 명령을 낸다. gitman tree에 그대로 실려
    // 나오는지만 확인한다.
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };

    const auto actions { click(harness, bounds_of(*tree, luil::ui_element_kind::caption_close)) };
    REQUIRE(actions.size() == 1u);
    const auto* const command { std::get_if<luil::ui_command>(&actions.front()) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == luil::ui_command::window_close);
}

TEST_CASE("Wheel rotation becomes a scroll intent", "[ui][interaction]")
{
    interaction_harness harness { make_tree(3) };

    const auto actions { harness.process(luil::mouse_wheel_event { 100.0f, 100.0f, -120.0f }) };
    const auto* const intent { as_intent<gitman::scroll_intent>(actions) };
    REQUIRE(intent != nullptr);
    // 아래로 굴리면 목록이 아래로 이동한다. 논리 픽셀·부호 변환은 luil이 한다.
    REQUIRE(intent->delta == luil::input_wheel_scroll_step);
}

TEST_CASE("Hover and pressed states follow the pointer", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    interaction_harness harness { tree };

    const luil::rect_f refresh { bounds_of(*tree, ui::kind_card_refresh, u8"card-0") };
    REQUIRE(harness.process(luil::pointer_moved_event { refresh.x + 1.0f, refresh.y + 1.0f, at(100) }).empty());
    REQUIRE(harness.snapshot().hovered.kind == ui::kind_card_refresh);
    REQUIRE(harness.snapshot().hover_started_at == at(100));

    // 누르면 pressed가 켜지고 떼면 꺼진다 (뗌이 intent를 낸다).
    REQUIRE(harness.process(luil::pointer_pressed_event { refresh.x + 1.0f, refresh.y + 1.0f, luil::pointer_button::left, at(300) }).empty());
    REQUIRE(harness.snapshot().pressed.kind == ui::kind_card_refresh);
    REQUIRE(harness.process(luil::pointer_released_event { refresh.x + 1.0f, refresh.y + 1.0f, luil::pointer_button::left, at(350) }).empty() == false);
    REQUIRE(harness.snapshot().pressed == luil::ui_element_id {});

    // 창을 벗어나면 hover가 사라진다.
    REQUIRE(harness.process(luil::pointer_left_event {}).empty());
    REQUIRE(harness.snapshot().hovered == luil::ui_element_id {});
    REQUIRE(harness.snapshot().hover_started_at.has_value() == false);
}

TEST_CASE("Keyboard focus walks the visible cards", "[ui][interaction]")
{
    interaction_harness harness { make_tree(3) };

    const auto first { harness.process(luil::key_pressed_event { luil::key_code::arrow_down }) };
    REQUIRE(as_intent<gitman::select_card_intent>(first)->id->value == u8"card-0");

    const auto second { harness.process(luil::key_pressed_event { luil::key_code::arrow_down }) };
    REQUIRE(as_intent<gitman::select_card_intent>(second)->id->value == u8"card-1");

    const auto back { harness.process(luil::key_pressed_event { luil::key_code::arrow_up }) };
    REQUIRE(as_intent<gitman::select_card_intent>(back)->id->value == u8"card-0");
    REQUIRE(harness.policy.focused_card()->value == u8"card-0");

    // Enter는 초점 카드를 refresh한다.
    const auto refresh { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
    REQUIRE(as_intent<gitman::refresh_card_intent>(refresh)->id.value == u8"card-0");

    // F5는 전체 refresh, Escape는 선택 해제다.
    const auto refresh_all { harness.process(luil::key_pressed_event { luil::key_code::f5 }) };
    REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(*as_message(refresh_all)));
    const auto cleared { harness.process(luil::key_pressed_event { luil::key_code::escape }) };
    REQUIRE(as_intent<gitman::select_card_intent>(cleared)->id.has_value() == false);
    REQUIRE(harness.policy.focused_card().has_value() == false);

    // 초점이 풀린 Enter는 아무것도 하지 않는다.
    REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::enter }).empty());
}

TEST_CASE("Dragging a card lets the list insert it at the nearest slot", "[ui][interaction][drag]")
{
    const auto tree { make_tree(3) };
    interaction_harness harness { tree };

    const luil::rect_f source { bounds_of(*tree, ui::kind_card_body, u8"card-0") };
    const luil::rect_f target { bounds_of(*tree, ui::kind_card_body, u8"card-2") };

    // 카드 body 왼쪽(버튼이 없는 곳)에서 눌러 임계 거리 밖으로 끌면 drag가 시작된다.
    REQUIRE(harness.process(luil::pointer_pressed_event { source.x + 5.0f, source.y + 5.0f, luil::pointer_button::left, at(0) }).empty());
    REQUIRE(harness.process(luil::pointer_moved_event { source.x + 5.0f, source.y + 25.0f, at(20) }).empty());
    REQUIRE(harness.snapshot().drag.has_value());
    REQUIRE(harness.snapshot().drag->payload.dragged_owner == u8"card-0");
    // 떠 있는 카드가 잡은 지점 그대로 따라오도록 offset이 실린다. 목록이 삽입 틈과
    // 들린 카드를 직접 그리므로 luil의 기본 ghost·강조는 끈다.
    REQUIRE(harness.snapshot().drag->payload.grab_offset_x == 5.0f);
    REQUIRE(harness.snapshot().drag->payload.grab_offset_y == 5.0f);
    REQUIRE(harness.snapshot().drag->payload.custom_visual);
    REQUIRE(harness.snapshot().drag->payload.suppress_drop_highlight);

    SECTION("대상 카드의 아래쪽 절반은 그 카드 뒤로 삽입한다")
    {
        // 카드가 아니라 목록이 drop 대상이다. 버튼 영역 위라도 잡힌다.
        const float drop_x { target.x + target.width - 20.0f };
        const float drop_y { target.y + target.height - 5.0f };
        REQUIRE(harness.process(luil::pointer_moved_event { drop_x, drop_y, at(40) }).empty());
        REQUIRE(harness.snapshot().drag->hovered_drop_target.kind == ui::kind_card_list);

        const auto actions { harness.process(luil::pointer_released_event { drop_x, drop_y, luil::pointer_button::left, at(60) }) };
        const auto* const intent { as_intent<gitman::reorder_card_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
        REQUIRE(intent->target.value == u8"card-2");
        REQUIRE(intent->place_after);
    }

    SECTION("대상 카드의 위쪽 절반은 그 카드 앞(= 이전 카드 뒤)으로 삽입한다")
    {
        const auto actions { harness.process(luil::pointer_released_event { target.x + 5.0f, target.y + 5.0f, luil::pointer_button::left, at(60) }) };
        const auto* const intent { as_intent<gitman::reorder_card_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
        REQUIRE(intent->target.value == u8"card-1");
        REQUIRE(intent->place_after);
    }

    SECTION("카드 사이 여백에 놓아도 그 자리로 삽입한다")
    {
        const luil::rect_f middle { bounds_of(*tree, ui::kind_card_body, u8"card-1") };
        const float gap_y { middle.y + middle.height + 3.0f };
        const auto actions { harness.process(luil::pointer_released_event { middle.x + 5.0f, gap_y, luil::pointer_button::left, at(60) }) };
        const auto* const intent { as_intent<gitman::reorder_card_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->target.value == u8"card-1");
        REQUIRE(intent->place_after);
    }

    SECTION("제자리에 놓으면 intent가 없다")
    {
        REQUIRE(harness.process(luil::pointer_moved_event { source.x + 5.0f, source.y + 30.0f, at(40) }).empty());
        // 목록이 drop을 받지만 위치가 그대로라 아무것도 만들지 않는다.
        REQUIRE(harness.snapshot().drag->hovered_drop_target.kind == ui::kind_card_list);
        REQUIRE(harness.process(luil::pointer_released_event { source.x + 5.0f, source.y + 30.0f, luil::pointer_button::left, at(60) }).empty());
    }
}

// D1-B (설계 4장): Tab 자리는 버튼·dialog 컨트롤·텍스트 칸뿐이다. 카드 body·root·
// 목록·메뉴 항목은 `set_tab_stop(false)`로 빠져 있어 마우스로만 누른다.
TEST_CASE("Tab visits controls only and skips card bodies", "[ui][interaction][focus]")
{
    const auto tree { make_tree(2) };
    interaction_harness harness { tree };

    // 순회 목록 자체에 배경·목록·카드 body가 없다.
    const std::vector<luil::ui_element_id> order { tree->focus_order() };
    REQUIRE(order.empty() == false);
    for (const luil::ui_element_id& id : order)
    {
        REQUIRE(id.kind != luil::ui_element_kind::root);
        REQUIRE(id.kind != ui::kind_toolbar);
        REQUIRE(id.kind != ui::kind_card_list);
        REQUIRE(id.kind != ui::kind_card_body);
    }
    REQUIRE(order.front() == id_of(ui::kind_toolbar_refresh_all));

    // 목록에 딸린 스크롤 막대도 자리가 아니다 (luil list_element와 같은 관례).
    const auto long_tree { make_tree(make_view(300)) };
    REQUIRE(find(*long_tree, ui::kind_scrollbar, ui::scrollbar_owner_cards) != nullptr);
    for (const luil::ui_element_id& id : long_tree->focus_order())
        REQUIRE(id.kind != ui::kind_scrollbar);

    // 첫 Tab은 도구 막대의 첫 버튼에 선다. 키보드로 옮긴 초점이라 테가 그려진다.
    REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::tab }).empty());
    REQUIRE(harness.snapshot().focused == id_of(ui::kind_toolbar_refresh_all));
    REQUIRE(harness.snapshot().focus_visible);

    // Tab을 이어 누르면 카드 body에는 서지 않고 카드의 refresh 버튼에 닿는다.
    bool reached_card_refresh { false };
    for (std::size_t step = 0; step < order.size(); ++step)
    {
        REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::tab }).empty());
        REQUIRE(harness.snapshot().focused.kind != ui::kind_card_body);
        if (harness.snapshot().focused == id_of(ui::kind_card_refresh, u8"card-0"))
        {
            reached_card_refresh = true;
            break;
        }
    }
    REQUIRE(reached_card_refresh);
}

TEST_CASE("Enter after clicking a card refreshes it instead of reselecting", "[ui][interaction][focus]")
{
    const auto tree { make_tree(2) };
    interaction_harness harness { tree };

    const auto selected { click(harness, bounds_of(*tree, ui::kind_card_body, u8"card-1")) };
    REQUIRE(as_intent<gitman::select_card_intent>(selected)->id->value == u8"card-1");
    // 카드 body가 자리가 아니라 클릭이 초점을 세우지 않는다. policy의 focused_card_만 선다.
    REQUIRE(harness.snapshot().focused == luil::ui_element_id {});
    REQUIRE(harness.policy.focused_card()->value == u8"card-1");

    // 그래서 Enter는 luil의 활성 키(= body의 좌클릭)가 아니라 policy로 흘러
    // 초점 카드 새로 고침이 된다.
    const auto refreshed { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
    const auto* const intent { as_intent<gitman::refresh_card_intent>(refreshed) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->id.value == u8"card-1");
}

TEST_CASE("Enter runs the action of the focused control", "[ui][interaction][focus]")
{
    interaction_harness harness { make_tree(2) };

    REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::tab }).empty());
    REQUIRE(harness.snapshot().focused == id_of(ui::kind_toolbar_refresh_all));

    const auto actions { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
    const gitman::logic_message* const message { as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(*message));
}
