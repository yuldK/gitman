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
        // Shift+클릭은 텍스트 박스에서 선택을 그 자리까지 늘린다 (R2.4).
        bool shift { false };
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
        // 스크롤로 내용이 흐른 뒤 hover를 다시 판정할 때의 기준 시각이다.
        std::chrono::steady_clock::time_point time {};
    };

    enum class key_code
    {
        none,
        arrow_up,
        arrow_down,
        enter,
        f5,
        escape,
        // 텍스트 박스 편집용이다 (switch-dialog-query-bar-design R2.4). 초점을 가진
        // 박스가 없으면 무시된다.
        arrow_left,
        arrow_right,
        home,
        end,
        delete_forward,
        // 글자 키는 **Ctrl이 눌린 동안에만** 만든다. 그러지 않으면 WM_CHAR가 막혀
        // 그 글자를 칠 수 없다.
        key_a,
        key_c,
        key_v,
        key_x,
        key_y,
        key_z,
        // Ctrl+Insert·Shift+Insert·Shift+Delete의 예전 클립보드 배치다. Ctrl이나
        // Shift가 눌린 동안에만 만든다.
        insert,
    };

    struct key_pressed_event
    {
        key_code key { key_code::none };
        bool control { false };
        bool shift { false };
    };

    // WM_CHAR가 만드는 문자 입력이다 (field-feedback-design 1.3). 텍스트 박스가
    // 있는 dialog가 열려 있을 때만 소비되고 그 밖에는 무시된다. backspace는
    // U+0008로 온다.
    struct character_typed_event
    {
        char32_t character { 0 };
    };

    using raw_input_event = std::variant<pointer_moved_event, pointer_pressed_event, pointer_released_event, pointer_left_event, mouse_wheel_event, key_pressed_event, character_typed_event>;

    // UI thread에서만 실행할 수 있는 명령이다. dialog와 창 조작은 업무 상태가
    // 아니므로 logic을 거치지 않는다 (docs/ui-element-design.md 6.4).
    enum class ui_command
    {
        show_open_document_dialog,
        // 새 문서 dialog의 두 폴더 선택이다 (fonts-and-in-app-dialogs-design F3).
        // 고른 경로는 intent로 logic에 돌아온다. 취소하면 아무 일도 없다.
        show_generate_scan_folder_picker,
        show_generate_location_picker,
        // 하단 로그 뷰의 표시 중 내용을 클립보드에 넣는다. 클립보드는 Win32 자원이라
        // UI thread 전용이다.
        copy_selected_log,
        // 환경설정 dialog의 찾아보기다 (REQ-017). Win32 파일 선택 dialog로 실행
        // 파일을 고른 뒤 `set_settings_executable_intent`를 logic에 보낸다.
        show_git_executable_picker,
        show_svn_executable_picker,
        // 탐색 등록의 스캔 폴더 선택이다 (REQ-004). 폴더를 고르면
        // `begin_discovery_intent`를 logic에 보낸다.
        show_discovery_folder_picker,
        // `.version-list` file association 등록·제거다 (REQ-016). registry는 Win32
        // 자원이고 앱 상태와 무관해 UI thread가 곧바로 수행하고 결과를 알린다.
        register_file_association,
        unregister_file_association,
        window_minimize,
        window_toggle_maximize,
        window_close,
    };

    // 인자가 필요한 외부 열기 명령이다 (field-feedback-design 2.3). `ui_command`는
    // 인자를 담지 못하므로 별도 variant 항목으로 다닌다. UI thread가 shell로
    // 실행한다.
    enum class external_open_target
    {
        vscode,
        // 탐색기로 부모 폴더를 열고 대상을 선택한다 (`explorer /select,`).
        explorer,
        // 탐색기로 폴더 자체를 연다. 컨텍스트 메뉴의 "저장소 열기"다 (3장).
        explorer_folder,
    };

    struct open_external_request
    {
        external_open_target target { external_open_target::explorer };
        std::u8string absolute_path {};

        [[nodiscard]] bool operator==(const open_external_request&) const noexcept = default;
    };

    // 초점을 가진 텍스트 박스의 선택 글을 클립보드에 넣는다 (R2.5). 글은 입력
    // thread가 tree에서 읽어 담고, 실제 클립보드 접근은 UI thread의 몫이다.
    struct clipboard_copy_request
    {
        std::u8string text {};

        [[nodiscard]] bool operator==(const clipboard_copy_request&) const noexcept = default;
    };

    // 클립보드의 글을 그 텍스트 박스에 넣으라는 요청이다. UI thread가 클립보드를
    // 읽어 대상 target의 insert intent를 logic에 보낸다.
    struct clipboard_paste_request
    {
        text_input_target target { text_input_target::settings_timeout };

        [[nodiscard]] bool operator==(const clipboard_paste_request&) const noexcept = default;
    };

    // UI thread만 다룰 수 있는 클립보드 요청이다 (R2.5).
    using clipboard_request = std::variant<clipboard_copy_request, clipboard_paste_request>;

    // element 액션과 interaction controller가 돌려주는 후속 조치다. 액션은 상태를
    // 직접 바꾸지 않고 이 메시지를 반환만 한다 (ADR-004).
    using input_action = std::variant<std::monostate, logic_message, ui_command, open_external_request, clipboard_copy_request, clipboard_paste_request>;

    // 휠 한 눈금이 움직이는 논리 픽셀이다.
    inline constexpr float input_wheel_scroll_step { 48.0f };
} // namespace gitman::ui
