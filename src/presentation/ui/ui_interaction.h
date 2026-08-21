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
        // 컨텍스트 메뉴가 열린 동안의 키 처리다. ↑/↓는 활성 항목 사이를 오가고
        // Enter는 강조 항목의 클릭 액션을 실행하며 Esc는 닫는다 (3장).
        [[nodiscard]] std::vector<input_action> process_menu_key(const key_pressed_event& event);
        [[nodiscard]] std::vector<input_action> run_trigger(const ui_element& element, ui_trigger trigger, float x, float y, bool control);
        void update_hover(float x, float y, std::chrono::steady_clock::time_point time);
        void clear_press() noexcept;
        void update_input_focus();

        interaction_config config_ {};
        std::shared_ptr<const ui_tree> tree_ {};
        interaction_snapshot snapshot_ {};

        // 스크롤 막대처럼 누른 채 끄는 대상이다. 값이 있으면 포인터 이동이 클릭·
        // 카드 drag 대신 이 element로 간다. 좌표는 마지막으로 보낸 위치다.
        ui_element_id pointer_drag_id_ {};
        float pointer_drag_x_ { 0.0f };
        float pointer_drag_y_ { 0.0f };

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

        // 환경설정 dialog의 열림 상태다. 열리는 순간 텍스트 박스에 초점을 주고
        // 닫히면 거두기 위한 edge 감지용이다.
        bool settings_dialog_open_ { false };

        // 컨텍스트 메뉴의 열림 상태다. 여닫는 edge에서 키보드 강조를 지운다.
        bool context_menu_open_ { false };

        // 마지막으로 본 포인터 위치·시각이다. 포인터가 머문 채 내용만 스크롤되면
        // (휠) 새 tree를 받을 때 이 자리로 hover를 다시 판정한다. 창을 벗어나면
        // 무효가 된다.
        bool pointer_inside_ { false };
        float last_pointer_x_ { 0.0f };
        float last_pointer_y_ { 0.0f };
        std::chrono::steady_clock::time_point last_pointer_time_ {};
    };

    // input thread의 소비 루프다. raw input 채널이 닫히면 반환한다. tree는 처리
    // 직전에 최신 것으로 갱신하고, interaction snapshot은 바뀔 때만 게시한다.
    // `ui_command`와 외부 열기 요청은 UI thread 전용이라 callback으로 넘긴다.
    void run_ui_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const ui_tree>>& tree_slot, messaging::channel<logic_message>& logic_inbox,
        messaging::latest_slot<interaction_snapshot>& interaction_slot, const std::function<void(ui_command)>& execute_ui_command, interaction_config config = {},
        const std::function<void(open_external_request)>& execute_open_external = {});
} // namespace gitman::ui
