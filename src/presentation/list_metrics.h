#pragma once

#include <cstddef>

namespace gitman {
    // 논리 픽셀 단위의 layout 상수다. 실제 좌표는 DPI 배율을 곱한다.
    // custom caption이 client 영역 위쪽을 차지하므로 목록은 그 아래에서 시작한다.
    // 값은 `ui::default_caption_ui_metrics.height`와 같아야 한다.
    inline constexpr float layout_caption_height { 36.0f };
    inline constexpr float layout_toolbar_height { 40.0f };
    inline constexpr float layout_card_height { 64.0f };
    inline constexpr float layout_card_gap { 6.0f };
    inline constexpr float layout_margin { 10.0f };
    inline constexpr float layout_button_size { 28.0f };

    [[nodiscard]] float card_list_content_height(std::size_t card_count, float scale) noexcept;

    // 스크롤을 [0, 최대]로 고정한다. 내용이 화면보다 작으면 0이다.
    [[nodiscard]] float clamp_scroll_offset(float offset, float content_height, float viewport_height) noexcept;
} // namespace gitman
