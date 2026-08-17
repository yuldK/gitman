#pragma once

#include "messaging/channel.h"
#include "messaging/latest_slot.h"
#include "presentation/ui/ui_element.h"
#include "presentation/ui/ui_tree.h"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace gitman::ui {
    struct interaction_config
    {
        // Win32에서는 GetDoubleClickTime()을 주입한다. test는 고정값을 쓴다.
        std::chrono::milliseconds double_click_time { 500 };
        float double_click_distance { 4.0f };
        // 이 거리(논리 픽셀 × scale 없이 창 좌표)만큼 끌면 클릭 대신 drag다.
        float drag_start_distance { 6.0f };
    };

    // raw input을 tree로 hit test해 액션과 interaction snapshot으로 바꾸는 상태
    // 기계다. input thread가 소유한다 (ADR-004의 입력 정규화 상태). 시각은 이벤트에
    // 담긴 timestamp만 사용하므로 test가 결정적이다.
    class interaction_controller
    {
    public:
        explicit interaction_controller(interaction_config config = {}) noexcept;

        void set_tree(std::shared_ptr<const ui_tree> tree) noexcept;
        [[nodiscard]] std::vector<input_action> process(const raw_input_event& event);
        [[nodiscard]] const interaction_snapshot& snapshot() const noexcept;

    private:
        [[nodiscard]] std::vector<input_action> process_move(const pointer_moved_event& event);
        [[nodiscard]] std::vector<input_action> process_press(const pointer_pressed_event& event);
        [[nodiscard]] std::vector<input_action> process_release(const pointer_released_event& event);
        [[nodiscard]] std::vector<input_action> process_key(const key_pressed_event& event);
        [[nodiscard]] std::vector<input_action> run_trigger(const ui_element& element, ui_trigger trigger, float x, float y, bool control);
        void update_hover(float x, float y, std::chrono::steady_clock::time_point time);
        void clear_press() noexcept;

        interaction_config config_ {};
        std::shared_ptr<const ui_tree> tree_ {};
        interaction_snapshot snapshot_ {};

        // 클릭은 같은 대상 위의 누름과 뗌이다. 비활성 element는 누름부터 무시한다.
        ui_element_id pressed_id_ {};
        pointer_button pressed_button_ { pointer_button::none };
        float pressed_x_ { 0.0f };
        float pressed_y_ { 0.0f };
        bool drag_candidate_ { false };

        // 더블 클릭 판정: 직전 클릭의 대상·시각·위치다.
        ui_element_id last_click_id_ {};
        std::chrono::steady_clock::time_point last_click_time_ {};
        float last_click_x_ { 0.0f };
        float last_click_y_ { 0.0f };

        // 키보드 탐색의 초점이다. 선택의 진실은 logic이 소유한다 (ADR-004).
        std::optional<project_id> focused_ {};
    };

    // input thread의 소비 루프다. raw input 채널이 닫히면 반환한다. tree는 처리
    // 직전에 최신 것으로 갱신하고, interaction snapshot은 바뀔 때만 게시한다.
    // `ui_command`는 UI thread 전용이라 callback으로 넘긴다.
    void run_ui_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui_tree>>& tree_slot, messaging::channel<logic_message>& logic_inbox,
        messaging::latest_slot<interaction_snapshot>& interaction_slot, const std::function<void(ui_command)>& execute_ui_command, interaction_config config = {});
} // namespace gitman::ui
