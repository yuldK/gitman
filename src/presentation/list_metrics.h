#pragma once

#include <cstddef>

namespace gitman {
    // 논리 픽셀 단위의 layout 상수다. 실제 좌표는 DPI 배율을 곱한다.
    // custom caption이 client 영역 위쪽을 차지하므로 목록은 그 아래에서 시작한다.
    // 값은 `ui::default_caption_ui_metrics.height`와 같아야 한다.
    inline constexpr float layout_caption_height { 36.0f };
    inline constexpr float layout_toolbar_height { 40.0f };
    // notice 배너의 높이다. 배너가 보이면 목록이 그만큼 아래에서 시작한다.
    inline constexpr float layout_notice_height { 22.0f };
    inline constexpr float layout_card_height { 64.0f };
    inline constexpr float layout_card_gap { 6.0f };
    inline constexpr float layout_margin { 10.0f };
    inline constexpr float layout_button_size { 28.0f };
    // 스크롤 표시 thumb의 너비와 최소 길이다. 카드 오른쪽 여백 안에 그린다.
    inline constexpr float layout_scrollbar_width { 4.0f };
    inline constexpr float layout_scrollbar_minimum_thumb { 24.0f };

    // 카드 목록이 차지하는 영역이다. tree 빌드, 카드 목록 element와 logic의 스크롤
    // 한계 계산이 모두 이 함수를 거쳐 같은 값을 쓴다.
    struct list_layout
    {
        // 목록 slot의 위쪽 좌표다 (caption + toolbar + 보이는 notice 배너).
        float content_top { 0.0f };
        // 목록 slot의 높이다. 창이 작으면 0이다.
        float viewport_height { 0.0f };
    };

    // `window_height`와 결과는 같은 단위다. 논리 픽셀로 물으면 논리 픽셀로 답한다.
    [[nodiscard]] list_layout compute_list_layout(float window_height, float scale, bool has_notice) noexcept;

    [[nodiscard]] float card_list_content_height(std::size_t card_count, float scale) noexcept;

    // 목록 content 좌표에서 `card_index` 카드가 차지하는 위쪽 좌표다.
    [[nodiscard]] float card_content_top(std::size_t card_index, float scale) noexcept;

    // 카드 하나가 화면 안에 완전히 들어오도록 조정한 스크롤 값이다. 이미 보이면
    // 현재 값을 그대로 돌려준다.
    [[nodiscard]] float scroll_offset_showing_card(float offset, std::size_t card_index, std::size_t card_count, float viewport_height, float scale) noexcept;

    // 스크롤을 [0, 최대]로 고정한다. 내용이 화면보다 작으면 0이다.
    [[nodiscard]] float clamp_scroll_offset(float offset, float content_height, float viewport_height) noexcept;
} // namespace gitman
