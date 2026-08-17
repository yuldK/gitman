#include "presentation/input_controller.h"

#include <chrono>
#include <thread>
#include <utility>

namespace gitman {
    namespace {
        void post_with_retry(messaging::channel<logic_message>& inbox, const logic_message& message)
        {
            // intent는 버리지 않는다. 가득 찬 inbox에는 짧게 물러났다 다시 시도하고
            // 닫힌 inbox(종료 중)만 조용히 포기한다.
            while (true)
            {
                logic_message attempt { message };
                const messaging::post_result result { inbox.post(std::move(attempt)) };
                if (result != messaging::post_result::channel_full)
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
            }
        }
    } // namespace

    void input_controller::set_layout(std::shared_ptr<const layout_snapshot> layout) noexcept
    {
        layout_ = std::move(layout);
    }

    std::vector<input_action> input_controller::process(const raw_input_event& event)
    {
        return std::visit(
            [this](const auto& value) -> std::vector<input_action> {
                using value_type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<value_type, pointer_pressed_event>)
                {
                    if (value.button == pointer_button::left && layout_ != nullptr)
                    {
                        const hit_area area { hit_test(*layout_, value.x, value.y) };
                        pressed_kind_ = area.kind;
                        pressed_id_ = area.id;
                    }
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, pointer_released_event>)
                    return process_release(value);
                else if constexpr (std::is_same_v<value_type, mouse_wheel_event>)
                {
                    // 위로 굴리면 목록이 위로 간다.
                    scroll_intent intent {};
                    intent.delta = -(value.delta / 120.0f) * input_wheel_scroll_step;
                    return { input_action { logic_message { intent } } };
                }
                else if constexpr (std::is_same_v<value_type, key_pressed_event>)
                    return process_key(value);
                else
                    return {};
            },
            event);
    }

    std::vector<input_action> input_controller::process_release(const pointer_released_event& event)
    {
        if (event.button != pointer_button::left || layout_ == nullptr)
            return {};

        const hit_area area { hit_test(*layout_, event.x, event.y) };
        const hit_target_kind pressed_kind { pressed_kind_ };
        const project_id pressed_id { pressed_id_ };
        pressed_kind_ = hit_target_kind::none;
        pressed_id_ = {};

        // 클릭은 같은 대상 위에서의 누름과 뗌이다. 누른 뒤 벗어나면 아무 일도 없다.
        if (area.kind != pressed_kind || area.id.value != pressed_id.value)
            return {};

        switch (area.kind)
        {
        case hit_target_kind::card_body:
            focused_ = area.id;
            return { input_action { logic_message { select_card_intent { { area.id } } } } };
        case hit_target_kind::card_refresh:
            return { input_action { logic_message { refresh_card_intent { area.id } } } };
        case hit_target_kind::toolbar_refresh_all:
            return { input_action { logic_message { refresh_all_intent {} } } };
        case hit_target_kind::toolbar_open_document:
            return { input_action { show_open_document_dialog {} } };
        case hit_target_kind::card_update_disabled:
        case hit_target_kind::card_switch_disabled:
            // 단계 7 전까지 비활성이다.
            return {};
        case hit_target_kind::none:
            focused_.reset();
            return { input_action { logic_message { select_card_intent {} } } };
        }
        return {};
    }

    std::vector<input_action> input_controller::process_key(const key_pressed_event& event)
    {
        switch (event.key)
        {
        case key_code::arrow_down:
        case key_code::arrow_up: {
            const std::vector<project_id> cards { visible_cards() };
            if (cards.empty())
                return {};

            std::size_t next { 0 };
            if (focused_.has_value())
            {
                std::size_t current { cards.size() };
                for (std::size_t index = 0; index < cards.size(); ++index)
                    if (cards[index] == *focused_)
                        current = index;

                if (current == cards.size())
                    next = 0;
                else if (event.key == key_code::arrow_down)
                    next = current + 1 < cards.size() ? current + 1 : current;
                else
                    next = current > 0 ? current - 1 : 0;
            }
            focused_ = cards[next];
            return { input_action { logic_message { select_card_intent { { cards[next] } } } } };
        }
        case key_code::enter:
            if (focused_.has_value())
                return { input_action { logic_message { refresh_card_intent { *focused_ } } } };
            return {};
        case key_code::f5:
            return { input_action { logic_message { refresh_all_intent {} } } };
        case key_code::escape:
            focused_.reset();
            return { input_action { logic_message { select_card_intent {} } } };
        case key_code::none:
            break;
        }
        return {};
    }

    std::vector<project_id> input_controller::visible_cards() const
    {
        std::vector<project_id> cards {};
        if (layout_ == nullptr)
            return cards;
        for (const hit_area& area : layout_->areas)
            if (area.kind == hit_target_kind::card_body)
                cards.push_back(area.id);
        return cards;
    }

    void run_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const layout_snapshot>>& layout_slot, messaging::channel<logic_message>& logic_inbox,
        const std::function<void()>& show_open_dialog)
    {
        input_controller controller {};
        std::uint64_t layout_version { 0 };
        messaging::envelope<raw_input_event> received {};
        while (true)
        {
            const messaging::receive_status status { input_inbox.receive_wait(received, std::chrono::milliseconds { 250 }) };
            if (status == messaging::receive_status::closed)
                return;
            if (status != messaging::receive_status::received)
                continue;

            if (const auto layout { layout_slot.take_newer(layout_version) }; layout.has_value())
            {
                layout_version = layout->version;
                controller.set_layout(layout->value);
            }

            for (input_action& action : controller.process(received.payload))
                if (const auto* const message { std::get_if<logic_message>(&action) }; message != nullptr)
                    post_with_retry(logic_inbox, *message);
                else if (std::holds_alternative<show_open_document_dialog>(action) && show_open_dialog)
                    show_open_dialog();
        }
    }
} // namespace gitman
