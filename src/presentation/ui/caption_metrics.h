#pragma once

namespace gitman::ui {
    // caption 그리기(caption_element)와 Win32 비클라이언트 hit test(caption_layout)가
    // 공유하는 논리 픽셀 치수다. 의존성이 없는 독립 헤더라 어느 계층에서도 포함할
    // 수 있다.
    struct caption_ui_metrics
    {
        int height { 36 };
        int button_width { 42 };
        int application_icon_slot_width { 40 };
        int title_left_padding { 14 };
        int title_icon_gap { 6 };
        int title_font_size { 14 };
        int application_icon_size { 16 };
        int button_icon_size { 14 };
    };

    inline constexpr caption_ui_metrics default_caption_ui_metrics {};

    // Win32 비클라이언트 hit(WM_NCMOUSEMOVE)의 UI thread 추적 상태다. UI thread가
    // element id로 변환해 interaction snapshot에 합친다.
    enum class caption_button_hover
    {
        none,
        minimize,
        maximize,
        close,
    };
} // namespace gitman::ui
