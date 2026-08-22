#include "presentation/ui/ui_interaction.h"

#include "domain/text_edit.h"
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

        // 초점을 받아 문자 입력을 소비하는 element다. 새 텍스트 박스는 이 목록에만
        // 더하면 초점·caret·문자·클립보드 배선이 함께 따라온다.
        [[nodiscard]] bool is_text_input_kind(const ui_element_kind kind) noexcept
        {
            return kind == ui_element_kind::settings_timeout_input || kind == ui_element_kind::switch_dialog_query_input;
        }

        [[nodiscard]] text_input_target target_for_kind(const ui_element_kind kind) noexcept
        {
            return kind == ui_element_kind::switch_dialog_query_input ? text_input_target::switch_query : text_input_target::settings_timeout;
        }

        [[nodiscard]] input_action text_edit_action(const ui_element_kind kind, const text_edit_command command, const bool extend = false)
        {
            edit_text_input_intent intent {};
            intent.target = target_for_kind(kind);
            intent.command = command;
            intent.extend = extend;
            return input_action { logic_message { std::move(intent) } };
        }
    } // namespace

    interaction_controller::interaction_controller(const interaction_config config) noexcept
        : config_ { config }
    {}

    void interaction_controller::set_tree(std::shared_ptr<const ui_tree> tree) noexcept
    {
        tree_ = std::move(tree);

        // 사라진 element를 가리키는 초점·강조는 새 tree를 받는 즉시 거둔다. snapshot을
        // 곧바로 읽는 렌더러가 없어진 텍스트 박스의 caret을 기다리지 않게 한다.
        update_input_focus();

        // 포인터가 머문 자리에서 내용이 스크롤로 흐르면 커서 아래 element가
        // 바뀐다. 새 tree를 받을 때 마지막 위치로 hover를 다시 판정해 tooltip이
        // 이전 대상을 따라 쓸려 다니지 않게 한다. 끌기 중에는 hover가 잡은
        // 대상에 남아야 하므로 건드리지 않는다.
        if (pointer_inside_ && pointer_drag_id_ == ui_element_id {} && snapshot_.drag.has_value() == false)
            update_hover(last_pointer_x_, last_pointer_y_, last_pointer_time_);
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
                    pointer_inside_ = false;
                    return {};
                }
                else if constexpr (std::is_same_v<value_type, mouse_wheel_event>)
                {
                    // 스크롤 뒤 tree가 다시 오면 이 자리로 hover를 재판정한다.
                    pointer_inside_ = true;
                    last_pointer_x_ = value.x;
                    last_pointer_y_ = value.y;
                    last_pointer_time_ = value.time;

                    // 위로 굴리면 내용이 위로 간다.
                    const float delta { -(value.delta / 120.0f) * input_wheel_scroll_step };

                    // 컨텍스트 메뉴가 떠 있는 동안 휠은 무시한다. 앵커에 붙은 메뉴
                    // 아래로 목록이 흘러가면 대상 카드와 어긋난다.
                    if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::context_menu }) != nullptr)
                        return {};

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
                    // 문자 입력은 초점을 가진 텍스트 박스로만 간다. 받을 값인지는
                    // logic이 거른다 (제한 시간은 숫자만 남긴다).
                    const ui_element_kind kind { snapshot_.focused_input.kind };
                    if (is_text_input_kind(kind) == false)
                        return {};
                    if (value.character == U'\b')
                        return { text_edit_action(kind, text_edit_command::backspace) };
                    // Ctrl+Backspace는 WM_CHAR가 U+007F로 준다.
                    if (value.character == 0x7Fu)
                        return { text_edit_action(kind, text_edit_command::delete_word_left) };
                    // 나머지 제어 문자는 키 경로(Ctrl+C 등)가 이미 다뤘다.
                    if (value.character < U' ')
                        return {};

                    edit_text_input_intent intent {};
                    intent.target = target_for_kind(kind);
                    intent.command = text_edit_command::insert;
                    intent.text = text_edit_encode_utf8(value.character);
                    return { input_action { logic_message { std::move(intent) } } };
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
        pointer_inside_ = true;
        last_pointer_x_ = event.x;
        last_pointer_y_ = event.y;
        last_pointer_time_ = event.time;

        if (tree_ == nullptr)
            return {};

        // 텍스트 박스를 잡고 있으면 이동이 선택 범위를 늘린다. 포인터가 칸을
        // 벗어나도 이어진다 — 같은 id로 다시 찾는다.
        if (text_drag_id_ != ui_element_id {})
        {
            const ui_element* const target { tree_->find(text_drag_id_) };
            if (target == nullptr)
                return {};
            return place_text_caret(*target, event.x, true, false);
        }

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
        if (is_text_input_kind(hit->id().kind))
        {
            snapshot_.focused_input = hit->id();
            snapshot_.focus_started_at = event.time;
        }
        else
        {
            snapshot_.focused_input = {};
            snapshot_.focus_started_at.reset();
        }

        // 텍스트 박스는 누른 자리로 caret이 가고, 누른 채 끌면 범위가 잡힌다.
        // 임계 시간·거리 안의 두 번째 누름은 그 자리의 낱말을 고른다 (R2.4).
        if (event.button == pointer_button::left && is_text_input_kind(hit->id().kind))
        {
            const bool repeat { hit->id() == last_click_id_ && event.time - last_click_time_ <= config_.double_click_time };
            const bool same_spot { repeat && distance_between(last_click_x_, last_click_y_, event.x, event.y) <= config_.double_click_distance };
            click_streak_ = same_spot ? click_streak_ + 1 : 1;
            last_click_id_ = hit->id();
            last_click_time_ = event.time;
            last_click_x_ = event.x;
            last_click_y_ = event.y;

            pressed_id_ = hit->id();
            pressed_button_ = event.button;
            pressed_x_ = event.x;
            pressed_y_ = event.y;
            drag_candidate_ = false;
            snapshot_.pressed = hit->id();
            // 두 번째 누름은 낱말, 세 번째부터는 전부다. Shift+클릭은 선택을 늘린다.
            if (click_streak_ >= 3)
            {
                text_drag_id_ = {};
                return { text_edit_action(hit->id().kind, text_edit_command::select_all) };
            }
            const bool word { click_streak_ == 2 };
            text_drag_id_ = word ? ui_element_id {} : hit->id();
            return place_text_caret(*hit, event.x, word == false && event.shift, word);
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

        // 텍스트 범위 선택을 놓는 것은 클릭이 아니다. 선택은 이미 잡혀 있다.
        if (text_drag_id_ != ui_element_id {} && event.button == pointer_button::left)
        {
            text_drag_id_ = {};
            clear_press();
            return {};
        }

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

    std::vector<input_action> interaction_controller::place_text_caret(const ui_element& element, const float x, const bool extend, const bool select_word) const
    {
        const std::optional<std::size_t> offset { element.offset_at(x, config_.measure_text) };
        if (offset.has_value() == false)
            return {};

        edit_text_input_intent intent {};
        intent.target = target_for_kind(element.id().kind);
        intent.command = select_word ? text_edit_command::select_word : text_edit_command::place_caret;
        intent.offset = *offset;
        intent.extend = extend;
        return { input_action { logic_message { std::move(intent) } } };
    }

    std::vector<input_action> interaction_controller::copy_focused_selection(const ui_element_kind kind, const bool erase) const
    {
        // 복사할 글은 tree가 들고 있다. 선택이 없으면 아무 일도 하지 않는다.
        const ui_element* const element { tree_ != nullptr ? tree_->find(snapshot_.focused_input) : nullptr };
        const std::optional<text_input_snapshot> value { element != nullptr ? element->text_input() : std::nullopt };
        if (value.has_value() == false || value->caret == value->anchor)
            return {};

        const std::size_t begin { value->caret < value->anchor ? value->caret : value->anchor };
        const std::size_t end { value->caret < value->anchor ? value->anchor : value->caret };
        std::vector<input_action> actions {};
        actions.push_back(input_action { clipboard_copy_request { std::u8string { value->text.substr(begin, end - begin) } } });
        if (erase)
            actions.push_back(text_edit_action(kind, text_edit_command::cut));
        return actions;
    }

    std::optional<std::vector<input_action>> interaction_controller::process_text_input_key(const key_pressed_event& event)
    {
        const ui_element_kind kind { snapshot_.focused_input.kind };
        if (is_text_input_kind(kind) == false)
            return std::nullopt;

        switch (event.key)
        {
        case key_code::arrow_left:
            return std::vector<input_action> { text_edit_action(kind, event.control ? text_edit_command::move_word_left : text_edit_command::move_left, event.shift) };
        case key_code::arrow_right:
            return std::vector<input_action> { text_edit_action(kind, event.control ? text_edit_command::move_word_right : text_edit_command::move_right, event.shift) };
        case key_code::home:
            return std::vector<input_action> { text_edit_action(kind, text_edit_command::move_line_start, event.shift) };
        case key_code::end:
            return std::vector<input_action> { text_edit_action(kind, text_edit_command::move_line_end, event.shift) };
        case key_code::delete_forward:
            // Shift+Delete는 예전 배치의 잘라내기다. Ctrl+Delete는 낱말 지우기다.
            if (event.shift && event.control == false)
                return copy_focused_selection(kind, true);
            return std::vector<input_action> { text_edit_action(kind, event.control ? text_edit_command::delete_word_right : text_edit_command::delete_forward) };
        case key_code::insert:
            // Ctrl+Insert는 복사, Shift+Insert는 붙여넣기다.
            if (event.control)
                return copy_focused_selection(kind, false);
            if (event.shift)
                return std::vector<input_action> { input_action { clipboard_paste_request { target_for_kind(kind) } } };
            return std::nullopt;
        case key_code::key_z:
            if (event.control)
                return std::vector<input_action> { text_edit_action(kind, event.shift ? text_edit_command::redo : text_edit_command::undo) };
            return std::nullopt;
        case key_code::key_y:
            if (event.control)
                return std::vector<input_action> { text_edit_action(kind, text_edit_command::redo) };
            return std::nullopt;
        case key_code::key_a:
            if (event.control)
                return std::vector<input_action> { text_edit_action(kind, text_edit_command::select_all) };
            return std::nullopt;
        case key_code::key_c:
        case key_code::key_x:
            if (event.control == false)
                return std::nullopt;
            return copy_focused_selection(kind, event.key == key_code::key_x);
        case key_code::key_v:
            if (event.control)
                return std::vector<input_action> { input_action { clipboard_paste_request { target_for_kind(kind) } } };
            return std::nullopt;
        default:
            break;
        }
        return std::nullopt;
    }

    std::vector<input_action> interaction_controller::process_key(const key_pressed_event& event)
    {
        // 컨텍스트 메뉴가 열려 있으면 ↑/↓/Enter/Esc는 메뉴의 몫이다
        // (field-feedback-design 3장). 카드 목록 탐색으로 흘러가지 않는다.
        if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::context_menu }) != nullptr)
            return process_menu_key(event);

        // 초점을 가진 텍스트 박스가 먼저 가져간다 (R2.4). 소비하지 않은 키만
        // 아래의 목록 탐색·dialog 경로로 흐른다.
        if (std::optional<std::vector<input_action>> handled { process_text_input_key(event) }; handled.has_value())
            return std::move(*handled);

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
            // 초점을 가진 질의 박스가 있으면 Enter는 그 박스의 몫이다. 카드 갱신으로
            // 흘러가지 않는다 (switch-dialog-query-bar-design Q1).
            if (snapshot_.focused_input.kind == ui_element_kind::switch_dialog_query_input)
                return { input_action { logic_message { submit_switch_query_intent {} } } };
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
            // 열린 dialog·overlay가 있으면 닫기가 먼저다. 알림은 다른 dialog 위에
            // 뜨므로 가장 먼저 닫힌다 (app-shell-design A3.2).
            if (tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::notice_dialog }) != nullptr)
                return { input_action { logic_message { dismiss_notice_intent {} } } };
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
        default:
            break;
        }
        return {};
    }

    std::vector<input_action> interaction_controller::process_menu_key(const key_pressed_event& event)
    {
        switch (event.key)
        {
        case key_code::arrow_down:
        case key_code::arrow_up: {
            const std::vector<ui_element_id> items { tree_->ids_of_kind(ui_element_kind::context_menu_item) };
            if (items.empty())
                return {};

            // 현재 강조 위치다. 없으면 아래는 처음부터, 위는 끝부터 시작한다.
            std::size_t current { items.size() };
            for (std::size_t index = 0; index < items.size(); ++index)
                if (items[index] == snapshot_.menu_highlight)
                    current = index;

            // 비활성 항목은 건너뛴다. 끝에 닿으면 제자리에 머문다.
            const auto item_enabled = [this](const ui_element_id& id) {
                const ui_element* const element { tree_->find(id) };
                return element != nullptr && element->enabled();
            };
            if (event.key == key_code::arrow_down)
            {
                for (std::size_t index = current == items.size() ? 0 : current + 1; index < items.size(); ++index)
                    if (item_enabled(items[index]))
                    {
                        snapshot_.menu_highlight = items[index];
                        break;
                    }
            }
            else
            {
                for (std::size_t index = current == items.size() ? items.size() : current; index > 0; --index)
                    if (item_enabled(items[index - 1]))
                    {
                        snapshot_.menu_highlight = items[index - 1];
                        break;
                    }
            }
            return {};
        }
        case key_code::enter: {
            if (snapshot_.menu_highlight == ui_element_id {})
                return {};
            const ui_element* const item { tree_->find(snapshot_.menu_highlight) };
            if (item == nullptr || item->enabled() == false)
                return {};
            const rect_f box { item->bounds() };
            return run_trigger(*item, ui_trigger::left_click, box.x + box.width / 2.0f, box.y + box.height / 2.0f, false);
        }
        case key_code::escape:
            return { input_action { logic_message { close_context_menu_intent {} } } };
        case key_code::f5:
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
        // 초점을 가진 박스가 tree에서 사라지면(그 dialog가 닫혔다) 초점을 거둔다.
        // dialog 종류마다 edge를 따로 세지 않고 element의 존재만 본다.
        if (is_text_input_kind(snapshot_.focused_input.kind) && (tree_ == nullptr || tree_->find(snapshot_.focused_input) == nullptr))
        {
            snapshot_.focused_input = {};
            snapshot_.focus_started_at.reset();
        }

        // 컨텍스트 메뉴가 열리고 닫힐 때마다 키보드 강조를 지운다. 새 메뉴는 강조
        // 없이 시작한다 (첫 ↓가 첫 활성 항목을 고른다).
        const bool menu_open { tree_ != nullptr && tree_->find(ui_element_id { ui_element_kind::context_menu }) != nullptr };
        if (menu_open != context_menu_open_)
        {
            context_menu_open_ = menu_open;
            snapshot_.menu_highlight = {};
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
        const std::function<void(open_external_request)>& execute_open_external, const std::function<void(clipboard_request)>& execute_clipboard)
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

            // 이벤트가 없는 턴(타임아웃)에도 최신 tree를 받는다. set_tree가 마지막
            // 포인터 위치로 hover를 재판정하므로, 휠을 멈춘 뒤 도착한 tree의 반영이
            // 다음 입력을 기다리지 않는다.
            if (const auto tree { tree_slot.take_newer(tree_version) }; tree.has_value())
            {
                tree_version = tree->version;
                controller.set_tree(tree->value);
            }

            if (status == messaging::receive_status::received)
                for (input_action& action : controller.process(received.payload))
                    if (const auto* const message { std::get_if<logic_message>(&action) }; message != nullptr)
                        post_with_retry(logic_inbox, *message);
                    else if (const auto* const command { std::get_if<ui_command>(&action) }; command != nullptr && execute_ui_command)
                        execute_ui_command(*command);
                    else if (auto* const open { std::get_if<open_external_request>(&action) }; open != nullptr && execute_open_external)
                        execute_open_external(std::move(*open));
                    else if (auto* const copy { std::get_if<clipboard_copy_request>(&action) }; copy != nullptr && execute_clipboard)
                        execute_clipboard(clipboard_request { std::move(*copy) });
                    else if (const auto* const paste { std::get_if<clipboard_paste_request>(&action) }; paste != nullptr && execute_clipboard)
                        execute_clipboard(clipboard_request { *paste });

            if ((controller.snapshot() == published) == false)
            {
                published = controller.snapshot();
                static_cast<void>(interaction_slot.publish(published));
            }
        }
    }
} // namespace gitman::ui
