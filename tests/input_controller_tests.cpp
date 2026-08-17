#include "presentation/input_controller.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {
    std::shared_ptr<const gitman::layout_snapshot> make_layout(const std::size_t card_count, const gitman::view_empty_state empty_state = gitman::view_empty_state::none)
    {
        gitman::view_snapshot view {};
        view.window_width = 800.0f;
        view.window_height = 600.0f;
        view.scale = 1.0f;
        view.empty_state = empty_state;
        for (std::size_t index = 0; index < card_count; ++index)
        {
            gitman::card_view_model card {};
            const std::string digits { std::to_string(index) };
            card.id.value = u8"card-";
            card.id.value.append(digits.begin(), digits.end());
            view.cards.push_back(std::move(card));
        }
        return std::make_shared<const gitman::layout_snapshot>(gitman::compute_layout(view));
    }

    const gitman::hit_area* find_area(const gitman::layout_snapshot& layout, const gitman::hit_target_kind kind, const std::u8string_view id = u8"")
    {
        for (const gitman::hit_area& area : layout.areas)
            if (area.kind == kind && (id.empty() || area.id.value == id))
                return &area;
        return nullptr;
    }

    std::vector<gitman::input_action> click(gitman::input_controller& controller, const gitman::rect_f& bounds)
    {
        const float x { bounds.x + 1.0f };
        const float y { bounds.y + 1.0f };
        auto pressed { controller.process(gitman::pointer_pressed_event { x, y, gitman::pointer_button::left }) };
        REQUIRE(pressed.empty());
        return controller.process(gitman::pointer_released_event { x, y, gitman::pointer_button::left });
    }

    const gitman::logic_message* as_message(const std::vector<gitman::input_action>& actions)
    {
        if (actions.size() != 1u)
            return nullptr;
        return std::get_if<gitman::logic_message>(&actions.front());
    }

    void pump_thread_main(messaging::channel<gitman::raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const gitman::layout_snapshot>>& layout_slot,
        messaging::channel<gitman::logic_message>& logic_inbox, std::atomic<int>& dialog_requests)
    {
        gitman::run_input_pump(input_inbox, layout_slot, logic_inbox, [&dialog_requests] { dialog_requests.fetch_add(1); });
    }
} // namespace

TEST_CASE("Clicks resolve to intents through the layout", "[input][app]")
{
    const auto layout { make_layout(2) };
    gitman::input_controller controller {};
    controller.set_layout(layout);

    SECTION("refresh 버튼")
    {
        const auto actions { click(controller, find_area(*layout, gitman::hit_target_kind::card_refresh, u8"card-1")->bounds) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::refresh_card_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-1");
    }

    SECTION("카드 body는 선택이다")
    {
        const auto actions { click(controller, find_area(*layout, gitman::hit_target_kind::card_body, u8"card-0")->bounds) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::select_card_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.has_value());
        REQUIRE(intent->id->value == u8"card-0");
    }

    SECTION("빈 영역 클릭은 선택 해제다")
    {
        const auto actions { click(controller, gitman::rect_f { 700.0f, 580.0f, 4.0f, 4.0f }) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::select_card_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.has_value() == false);
    }

    SECTION("전체 refresh")
    {
        const auto actions { click(controller, find_area(*layout, gitman::hit_target_kind::toolbar_refresh_all)->bounds) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(*message));
    }
}

TEST_CASE("A press that leaves its target produces nothing", "[input][app]")
{
    const auto layout { make_layout(1) };
    gitman::input_controller controller {};
    controller.set_layout(layout);

    const gitman::rect_f refresh { find_area(*layout, gitman::hit_target_kind::card_refresh)->bounds };
    const gitman::rect_f body { find_area(*layout, gitman::hit_target_kind::card_body)->bounds };
    REQUIRE(controller.process(gitman::pointer_pressed_event { refresh.x + 1.0f, refresh.y + 1.0f, gitman::pointer_button::left }).empty());
    REQUIRE(controller.process(gitman::pointer_released_event { body.x + 1.0f, body.y + 1.0f, gitman::pointer_button::left }).empty());
}

TEST_CASE("Disabled stage seven buttons produce nothing", "[input][app]")
{
    const auto layout { make_layout(1) };
    gitman::input_controller controller {};
    controller.set_layout(layout);

    REQUIRE(click(controller, find_area(*layout, gitman::hit_target_kind::card_update_disabled)->bounds).empty());
    REQUIRE(click(controller, find_area(*layout, gitman::hit_target_kind::card_switch_disabled)->bounds).empty());
}

TEST_CASE("The open document button asks the UI for a dialog", "[input][app]")
{
    const auto layout { make_layout(0, gitman::view_empty_state::no_document) };
    gitman::input_controller controller {};
    controller.set_layout(layout);

    const auto actions { click(controller, find_area(*layout, gitman::hit_target_kind::toolbar_open_document)->bounds) };
    REQUIRE(actions.size() == 1u);
    REQUIRE(std::holds_alternative<gitman::show_open_document_dialog>(actions.front()));
}

TEST_CASE("Wheel rotation becomes a scroll intent", "[input][app]")
{
    gitman::input_controller controller {};
    controller.set_layout(make_layout(3));

    const auto actions { controller.process(gitman::mouse_wheel_event { 100.0f, 100.0f, -120.0f }) };
    const auto* const message { as_message(actions) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::scroll_intent>(message) };
    REQUIRE(intent != nullptr);
    // 아래로 굴리면 목록이 아래로 이동한다.
    REQUIRE(intent->delta == gitman::input_wheel_scroll_step);
}

TEST_CASE("Keyboard focus walks the visible cards", "[input][app]")
{
    const auto layout { make_layout(3) };
    gitman::input_controller controller {};
    controller.set_layout(layout);

    const auto first { controller.process(gitman::key_pressed_event { gitman::key_code::arrow_down }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(first))->id->value == u8"card-0");

    const auto second { controller.process(gitman::key_pressed_event { gitman::key_code::arrow_down }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(second))->id->value == u8"card-1");

    const auto back { controller.process(gitman::key_pressed_event { gitman::key_code::arrow_up }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(back))->id->value == u8"card-0");

    // Enter는 초점 카드를 refresh한다.
    const auto refresh { controller.process(gitman::key_pressed_event { gitman::key_code::enter }) };
    REQUIRE(std::get_if<gitman::refresh_card_intent>(as_message(refresh))->id.value == u8"card-0");

    // F5는 전체 refresh, Escape는 선택 해제다.
    REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(*as_message(controller.process(gitman::key_pressed_event { gitman::key_code::f5 }))));
    const auto cleared { controller.process(gitman::key_pressed_event { gitman::key_code::escape }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(cleared))->id.has_value() == false);

    // 초점이 풀린 Enter는 아무것도 하지 않는다.
    REQUIRE(controller.process(gitman::key_pressed_event { gitman::key_code::enter }).empty());
}

TEST_CASE("The input pump forwards intents and dialog requests until closed", "[input][app]")
{
    messaging::channel<gitman::raw_input_event> input_inbox { messaging::channel_options { 64, messaging::overflow_policy::drop_oldest, {} } };
    messaging::latest_slot<std::shared_ptr<const gitman::layout_snapshot>> layout_slot {};
    messaging::channel<gitman::logic_message> logic_inbox { messaging::channel_options { 64, messaging::overflow_policy::reject_newest, {} } };
    std::atomic<int> dialog_requests { 0 };

    const auto layout { make_layout(1, gitman::view_empty_state::no_document) };
    REQUIRE(layout_slot.publish(layout) == 1u);

    std::thread pump { &pump_thread_main, std::ref(input_inbox), std::ref(layout_slot), std::ref(logic_inbox), std::ref(dialog_requests) };

    const gitman::rect_f refresh_all { find_area(*layout, gitman::hit_target_kind::toolbar_refresh_all)->bounds };
    REQUIRE(input_inbox.post(gitman::pointer_pressed_event { refresh_all.x + 1.0f, refresh_all.y + 1.0f, gitman::pointer_button::left }) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(gitman::pointer_released_event { refresh_all.x + 1.0f, refresh_all.y + 1.0f, gitman::pointer_button::left }) == messaging::post_result::posted);

    const gitman::rect_f open_button { find_area(*layout, gitman::hit_target_kind::toolbar_open_document)->bounds };
    REQUIRE(input_inbox.post(gitman::pointer_pressed_event { open_button.x + 1.0f, open_button.y + 1.0f, gitman::pointer_button::left }) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(gitman::pointer_released_event { open_button.x + 1.0f, open_button.y + 1.0f, gitman::pointer_button::left }) == messaging::post_result::posted);

    messaging::envelope<gitman::logic_message> received {};
    REQUIRE(logic_inbox.receive_wait(received, std::chrono::milliseconds { 5000 }) == messaging::receive_status::received);
    REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(received.payload));

    input_inbox.close();
    pump.join();
    REQUIRE(dialog_requests.load() == 1);
}
