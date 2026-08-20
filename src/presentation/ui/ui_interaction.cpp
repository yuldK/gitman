#include "presentation/ui/ui_interaction.h"

#include "messaging/envelope.h"

#include <cmath>
#include <cstdint>
#include <thread>
#include <utility>
#include <variant>

namespace gitman::ui {
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

        [[nodiscard]] float distance_between(const float from_x, const float from_y, const float to_x, const float to_y) noexcept
        {
            const float delta_x { to_x - from_x };
            const float delta_y { to_y - from_y };
            return std::sqrt(delta_x * delta_x + delta_y * delta_y);
        }
    } // namespace

    interaction_controller::interaction_controller(const interaction_config config) noexcept
        : config_ { config }
    {}

    void interaction_controller::set_tree(std::shared_ptr<const ui_tree> tree) noexcept
    {
        tree_ = std::move(tree);
    }

    std::vector<input_action> interaction_controller::process(const raw_input_event& event)
    {
        update_input_focus();
        return std::visit(
            [this](const auto& value) -> std::vector<input_action> {
                using value_type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<value_type, pointer_moved_event>)
                    return process_move(value);
                else if constexpr (std::is_same_v<value_type, pointer_pressed_event>)
                    return process_press(value);
                else if constexpr (std::is_same_v<value_type, pointer_released_event>)
                    return process_release(value);
                else if constexpr (std::is_same_v<value_type, pointer_left_event>)
                {
                    snapshot_.hovered = {};
                    snapshot_.hover_started_at.reset();
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, mouse_wheel_event>)
                {
                    // 위로 굴리면 내용이 위로 간다.
                    const float delta { -(value.delta / 120.0f) * input_wheel_scroll_step };

                    // 로컬 변경 dialog가 열려 있으면 휠은 diff pane 위에서는 diff를,
                    // 그 밖에서는 목록을 스크롤한다.
                    if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::local_changes_dialog }) != nullptr)
                    {
                        const ui_element* const diff { tree_->find(ui_element_id { ui_element_kind::local_changes_diff }) };
                        if (diff != nullptr && diff->bounds().contains(value.x, value.y))
                            return { input_action { logic_message { local_changes_diff_scroll_intent { delta } } } };
                        return { input_action { logic_message { local_changes_scroll_intent { delta } } } };
                    }

                    // switch·탐색 dialog가 열려 있으면 휠은 그 후보 목록을 스크롤한다.
                    if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::switch_dialog }) != nullptr)
                        return { input_action { logic_message { switch_dialog_scroll_intent { delta } } } };
                    if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::discovery_dialog }) != nullptr)
                        return { input_action { logic_message { discovery_dialog_scroll_intent { delta } } } };

                    // 하단 로그 pane 위의 휠은 목록이 아니라 로그를 스크롤한다.
                    if (tree_ != nullptr)
                    {
                        const ui_element* const pane { tree_->find(ui_element_id { ui_element_kind::log_pane }) };
                        if (pane != nullptr && pane->visible() && pane->bounds().contains(value.x, value.y))
                            return { input_action { logic_message { log_scroll_intent { delta } } } };
                    }

                    return { input_action { logic_message { scroll_intent { delta } } } };
                }
                else if constexpr (std::is_same_v<value_type, key_pressed_event>)
                    return process_key(value);
                else if constexpr (std::is_same_v<value_type, character_typed_event>)
                {
                    // 문자 입력은 초점을 가진 텍스트 박스로만 간다. 현재 유일한
                    // 대상은 환경설정 dialog의 제한 시간 칸이다. 숫자 여부는 logic이
                    // 거른다.
                    if (snapshot_.focused_input.kind == ui_element_kind::settings_timeout_input)
                        return { input_action { logic_message { edit_settings_timeout_intent { value.character } } } };
                    return {};
                }
                else
                    return {};
            },
            event);
    }

    const interaction_snapshot& interaction_controller::snapshot() const noexcept
    {
        return snapshot_;
    }

    std::vector<input_action> interaction_controller::process_move(const pointer_moved_event& event)
    {
        if (tree_ == nullptr)
            return {};

        // 스크롤 막대를 잡고 있으면 이동은 전부 그 element의 몫이다. tree가 다시
        // 빌드되어도 같은 id로 찾아 이어서 끈다.
        if (pointer_drag_id_ != ui_element_id {})
        {
            const ui_element* const target { tree_->find(pointer_drag_id_) };
            const pointer_drag_target* const handler { target != nullptr ? target->pointer_drag() : nullptr };
            if (handler == nullptr || handler->on_move == nullptr)
                return {};

            const ui_action_context previous { pointer_drag_id_, pointer_drag_x_, pointer_drag_y_, false };
            const ui_action_context current { pointer_drag_id_, event.x, event.y, false };
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            return handler->on_move(previous, current);
        }

        // drag 중이면 ghost 위치와 수락 중인 drop 대상만 갱신한다.
        if (snapshot_.drag.has_value())
        {
            snapshot_.drag->x = event.x;
            snapshot_.drag->y = event.y;
            snapshot_.drag->hovered_drop_target = {};
            const ui_element* const over { tree_->find_drop_target(event.x, event.y, snapshot_.drag->payload) };
            if (over != nullptr)
                snapshot_.drag->hovered_drop_target = over->id();
            update_hover(event.x, event.y, event.time);
            return {};
        }

        // 눌린 채 임계 거리를 넘으면 클릭 대신 drag가 시작된다.
        if (drag_candidate_ && pressed_button_ == pointer_button::left && distance_between(pressed_x_, pressed_y_, event.x, event.y) >= config_.drag_start_distance)
        {
            const ui_element* const source { tree_->find(pressed_id_) };
            if (source != nullptr && source->enabled() && source->drag() != nullptr && source->drag()->make_payload)
            {
                const ui_action_context context { pressed_id_, pressed_x_, pressed_y_, false };
                snapshot_.drag = drag_visual { source->drag()->make_payload(context), event.x, event.y, {} };
                clear_press();
            }
            drag_candidate_ = false;
        }

        update_hover(event.x, event.y, event.time);
        return {};
    }

    std::vector<input_action> interaction_controller::process_press(const pointer_pressed_event& event)
    {
        if (tree_ == nullptr)
            return {};

        const ui_element* const hit { tree_->hit_test(event.x, event.y) };
        if (hit == nullptr || hit->enabled() == false)
        {
            clear_press();
            return {};
        }

        // 텍스트 박스를 누르면 초점을 주고, 다른 곳을 누르면 거둔다. 초점 시각은
        // caret 깜빡임의 위상 기준이다.
        if (hit->id().kind == ui_element_kind::settings_timeout_input)
        {
            snapshot_.focused_input = hit->id();
            snapshot_.focus_started_at = event.time;
        }
        else
        {
            snapshot_.focused_input = {};
            snapshot_.focus_started_at.reset();
        }

        pressed_id_ = hit->id();
        pressed_button_ = event.button;
        pressed_x_ = event.x;
        pressed_y_ = event.y;
        drag_candidate_ = event.button == pointer_button::left && hit->drag() != nullptr;
        if (event.button == pointer_button::left)
            snapshot_.pressed = hit->id();

        // 스크롤 막대는 누른 순간부터 끌기가 시작된다. 임계 거리를 두지 않는다.
        const pointer_drag_target* const handler { hit->pointer_drag() };
        if (event.button == pointer_button::left && handler != nullptr)
        {
            pointer_drag_id_ = hit->id();
            pointer_drag_x_ = event.x;
            pointer_drag_y_ = event.y;
            if (handler->on_press)
                return handler->on_press(ui_action_context { pointer_drag_id_, event.x, event.y, false });
        }
        return {};
    }

    std::vector<input_action> interaction_controller::process_release(const pointer_released_event& event)
    {
        if (tree_ == nullptr)
            return {};

        // 스크롤 막대를 놓는 것은 클릭이 아니다. 끌기만 끝낸다.
        if (pointer_drag_id_ != ui_element_id {} && event.button == pointer_button::left)
        {
            pointer_drag_id_ = {};
            clear_press();
            return {};
        }

        // drag를 끝낸다. 수락하는 대상 위에서만 drop 액션이 실행된다.
        if (snapshot_.drag.has_value())
        {
            std::vector<input_action> actions {};
            if (event.button == pointer_button::left)
            {
                const drag_visual drag { *snapshot_.drag };
                const ui_element* const over { tree_->find_drop_target(event.x, event.y, drag.payload) };
                if (over != nullptr && over->drop()->on_drop)
                    actions = over->drop()->on_drop(drag.payload, ui_action_context { over->id(), event.x, event.y, false });
                snapshot_.drag.reset();
                clear_press();
            }
            return actions;
        }

        const ui_element* const hit { tree_->hit_test(event.x, event.y) };
        const ui_element_id pressed { pressed_id_ };
        const pointer_button pressed_button { pressed_button_ };
        clear_press();

        // 클릭은 같은 대상 위에서의 누름과 뗌이다. 누른 뒤 벗어나면 아무 일도 없다.
        if (hit == nullptr || hit->enabled() == false || (hit->id() == pressed) == false || event.button != pressed_button)
            return {};

        // 키보드 탐색의 초점을 마우스 선택에 잇는다.
        if (hit->id().kind == ui_element_kind::card_body)
            focused_ = hit->id().owner;
        else if (hit->id().kind == ui_element_kind::root)
            focused_.reset();

        // 더블 클릭: 같은 대상을 임계 시간·거리 안에 다시 왼쪽 클릭했고 등록된
        // 액션이 있을 때만이다. 등록이 없으면 왼쪽 클릭 두 번으로 처리한다.
        ui_trigger trigger { event.button == pointer_button::right ? ui_trigger::right_click : ui_trigger::left_click };
        if (trigger == ui_trigger::left_click && hit->id() == last_click_id_ && event.time - last_click_time_ <= config_.double_click_time
            && distance_between(last_click_x_, last_click_y_, event.x, event.y) <= config_.double_click_distance && hit->action(ui_trigger::double_click) != nullptr)
        {
            trigger = ui_trigger::double_click;
            last_click_id_ = {};
        }
        else if (trigger == ui_trigger::left_click)
        {
            last_click_id_ = hit->id();
            last_click_time_ = event.time;
            last_click_x_ = event.x;
            last_click_y_ = event.y;
        }
        return run_trigger(*hit, trigger, event.x, event.y, false);
    }

    std::vector<input_action> interaction_controller::process_key(const key_pressed_event& event)
    {
        switch (event.key)
        {
        case key_code::arrow_down:
        case key_code::arrow_up: {
            if (tree_ == nullptr)
                return {};
            const std::vector<ui_element_id> cards { tree_->ids_of_kind(ui_element_kind::card_body) };
            if (cards.empty())
                return {};

            std::size_t next { 0 };
            if (focused_.has_value())
            {
                std::size_t current { cards.size() };
                for (std::size_t index = 0; index < cards.size(); ++index)
                    if (cards[index].owner == *focused_)
                        current = index;

                if (current == cards.size())
                    next = 0;
                else if (event.key == key_code::arrow_down)
                    next = current + 1 < cards.size() ? current + 1 : current;
                else
                    next = current > 0 ? current - 1 : 0;
            }
            focused_ = cards[next].owner;
            return { input_action { logic_message { select_card_intent { { cards[next].owner } } } } };
        }
        case key_code::enter:
            if (focused_.has_value())
                return { input_action { logic_message { refresh_card_intent { *focused_ } } } };
            return {};
        case key_code::f5:
            return { input_action { logic_message { refresh_all_intent {} } } };
        case key_code::escape:
            // drag 중이면 취소가 먼저다. 선택 해제는 다음 escape가 한다.
            if (snapshot_.drag.has_value())
            {
                snapshot_.drag.reset();
                return {};
            }
            // 열린 dialog·overlay가 있으면 닫기가 먼저다.
            if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::local_changes_dialog }) != nullptr)
                return { input_action { logic_message { cancel_local_changes_dialog_intent {} } } };
            if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::discovery_dialog }) != nullptr)
                return { input_action { logic_message { cancel_discovery_dialog_intent {} } } };
            if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::settings_dialog }) != nullptr)
                return { input_action { logic_message { cancel_settings_dialog_intent {} } } };
            if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::switch_dialog }) != nullptr)
                return { input_action { logic_message { cancel_switch_dialog_intent {} } } };
            focused_.reset();
            return { input_action { logic_message { select_card_intent {} } } };
        case key_code::none:
            break;
        }
        return {};
    }

    std::vector<input_action> interaction_controller::run_trigger(const ui_element& element, const ui_trigger trigger, const float x, const float y, const bool control)
    {
        const ui_action* const action { element.action(trigger) };
        if (action == nullptr)
            return {};
        return (*action)(ui_action_context { element.id(), x, y, control });
    }

    void interaction_controller::update_hover(const float x, const float y, const std::chrono::steady_clock::time_point time)
    {
        const ui_element* const hit { tree_ != nullptr ? tree_->hit_test(x, y) : nullptr };
        const ui_element_id hovered { hit != nullptr ? hit->id() : ui_element_id {} };
        if (hovered == snapshot_.hovered)
            return;
        snapshot_.hovered = hovered;
        if (hovered == ui_element_id {})
            snapshot_.hover_started_at.reset();
        else
            snapshot_.hover_started_at = time;
    }

    void interaction_controller::update_input_focus()
    {
        // 초점은 텍스트 박스를 눌러야 생긴다 (검수 지시: 열릴 때 자동 초점 없음).
        // dialog가 닫히면 남아 있던 초점을 거둔다.
        const bool open { tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::settings_dialog }) != nullptr };
        if (open == settings_dialog_open_)
            return;
        settings_dialog_open_ = open;
        if (open == false)
        {
            snapshot_.focused_input = {};
            snapshot_.focus_started_at.reset();
        }
    }

    void interaction_controller::clear_press() noexcept
    {
        pointer_drag_id_ = {};
        pressed_id_ = {};
        pressed_button_ = pointer_button::none;
        drag_candidate_ = false;
        snapshot_.pressed = {};
    }

    void run_ui_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui_tree>>& tree_slot, messaging::channel<logic_message>& logic_inbox,
        messaging::latest_slot<interaction_snapshot>& interaction_slot, const std::function<void(ui_command)>& execute_ui_command, const interaction_config config,
        const std::function<void(open_external_request)>& execute_open_external)
    {
        interaction_controller controller { config };
        std::uint64_t tree_version { 0 };
        interaction_snapshot published {};
        messaging::envelope<raw_input_event> received {};
        while (true)
        {
            const messaging::receive_status status { input_inbox.receive_wait(received, std::chrono::milliseconds { 250 }) };
            if (status == messaging::receive_status::closed)
                return;
            if (status != messaging::receive_status::received)
                continue;

            if (const auto tree { tree_slot.take_newer(tree_version) }; tree.has_value())
            {
                tree_version = tree->version;
                controller.set_tree(tree->value);
            }

            for (input_action& action : controller.process(received.payload))
                if (const auto* const message { std::get_if<logic_message>(&action) }; message != nullptr)
                    post_with_retry(logic_inbox, *message);
                else if (const auto* const command { std::get_if<ui_command>(&action) }; command != nullptr && execute_ui_command)
                    execute_ui_command(*command);
                else if (auto* const open { std::get_if<open_external_request>(&action) }; open != nullptr && execute_open_external)
                    execute_open_external(std::move(*open));

            if ((controller.snapshot() == published) == false)
            {
                published = controller.snapshot();
                static_cast<void>(interaction_slot.publish(published));
            }
        }
    }
} // namespace gitman::ui
