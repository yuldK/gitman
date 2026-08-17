#pragma once

#include "application/app_messages.h"

#include <chrono>
#include <variant>

namespace gitman::ui {
    enum class pointer_button
    {
        none,
        left,
        right,
    };

    // Win32 메시지의 최소 복사다. `HWND`와 lparam 원문은 담지 않는다 (ADR-004).
    // time은 UI thread가 게시 시점에 기록하며 더블 클릭·tooltip 판정의 기준이다.
    // interaction controller는 이 값만 읽고 시계를 직접 조회하지 않아 test가
    // 결정적이다.
    struct pointer_moved_event
    {
        float x { 0.0f };
        float y { 0.0f };
        std::chrono::steady_clock::time_point time {};
    };

    struct pointer_pressed_event
    {
        float x { 0.0f };
        float y { 0.0f };
        pointer_button button { pointer_button::left };
        std::chrono::steady_clock::time_point time {};
    };

    struct pointer_released_event
    {
        float x { 0.0f };
        float y { 0.0f };
        pointer_button button { pointer_button::left };
        std::chrono::steady_clock::time_point time {};
    };

    // 포인터가 창을 벗어났다. hover 강조가 창 밖에서 남지 않게 한다.
    struct pointer_left_event
    {};

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

    using raw_input_event = std::variant<pointer_moved_event, pointer_pressed_event, pointer_released_event, pointer_left_event, mouse_wheel_event, key_pressed_event>;

    // UI thread에서만 실행할 수 있는 명령이다. dialog와 창 조작은 업무 상태가
    // 아니므로 logic을 거치지 않는다 (docs/ui-element-design.md 6.4).
    enum class ui_command
    {
        show_open_document_dialog,
        show_generate_document_dialog,
        window_minimize,
        window_toggle_maximize,
        window_close,
    };

    // element 액션과 interaction controller가 돌려주는 후속 조치다. 액션은 상태를
    // 직접 바꾸지 않고 이 메시지를 반환만 한다 (ADR-004).
    using input_action = std::variant<std::monostate, logic_message, ui_command>;

    // 휠 한 눈금이 움직이는 논리 픽셀이다.
    inline constexpr float input_wheel_scroll_step { 48.0f };
} // namespace gitman::ui
