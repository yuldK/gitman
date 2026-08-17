#pragma once

#include "application/app_messages.h"
#include "domain/project.h"
#include "messaging/channel.h"
#include "messaging/latest_slot.h"
#include "presentation/layout_model.h"

#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace gitman {
    enum class pointer_button
    {
        none,
        left,
        right,
    };

    // Win32 메시지의 최소 복사다. `HWND`와 lparam 원문은 담지 않는다 (ADR-004).
    struct pointer_moved_event
    {
        float x { 0.0f };
        float y { 0.0f };
    };

    struct pointer_pressed_event
    {
        float x { 0.0f };
        float y { 0.0f };
        pointer_button button { pointer_button::left };
    };

    struct pointer_released_event
    {
        float x { 0.0f };
        float y { 0.0f };
        pointer_button button { pointer_button::left };
    };

    struct mouse_wheel_event
    {
        float x { 0.0f };
        float y { 0.0f };
        // WHEEL_DELTA(120) 단위다. 양수가 위로 굴림이다.
        float delta { 0.0f };
    };

    enum class key_code
    {
        none,
        arrow_up,
        arrow_down,
        enter,
        f5,
        escape,
    };

    struct key_pressed_event
    {
        key_code key { key_code::none };
        bool control { false };
    };

    using raw_input_event = std::variant<pointer_moved_event, pointer_pressed_event, pointer_released_event, mouse_wheel_event, key_pressed_event>;

    // 파일 dialog는 UI thread에서만 열 수 있어 logic이 아니라 UI로 보내는 요청이다.
    struct show_open_document_dialog
    {};

    using input_action = std::variant<std::monostate, logic_message, show_open_document_dialog>;

    // 휠 한 눈금이 움직이는 논리 픽셀이다.
    inline constexpr float input_wheel_scroll_step { 48.0f };

    // raw input을 layout snapshot으로 hit test해 의미 있는 intent로 바꾼다 (ADR-004).
    // 상태는 입력 정규화에 필요한 것(누른 대상, 키보드 초점)뿐이며 업무 상태는 없다.
    class input_controller
    {
    public:
        void set_layout(std::shared_ptr<const layout_snapshot> layout) noexcept;

        [[nodiscard]] std::vector<input_action> process(const raw_input_event& event);

    private:
        [[nodiscard]] std::vector<input_action> process_release(const pointer_released_event& event);
        [[nodiscard]] std::vector<input_action> process_key(const key_pressed_event& event);
        [[nodiscard]] std::vector<project_id> visible_cards() const;

        std::shared_ptr<const layout_snapshot> layout_ {};
        hit_target_kind pressed_kind_ { hit_target_kind::none };
        project_id pressed_id_ {};
        // 키보드 탐색의 초점이다. 화살표 이동의 기준일 뿐이고 선택의 진실은 logic이
        // 소유한다. 마우스 선택도 이 값을 갱신해 두 입력이 이어진다.
        std::optional<project_id> focused_ {};
    };

    // input thread의 소비 루프다. raw input 채널이 닫히면 반환한다. layout은 처리
    // 직전에 최신 것으로 갱신하므로 오래된 배치로 hit test하지 않는다.
    void run_input_pump(messaging::channel<raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const layout_snapshot>>& layout_slot, messaging::channel<logic_message>& logic_inbox,
        const std::function<void()>& show_open_dialog);
} // namespace gitman
