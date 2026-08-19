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
    // 카드 텍스트가 시작하는 왼쪽 여백(상태 아이콘 자리)과, 버튼을 숨기기 전에
    // 지켜 주는 최소 텍스트 폭이다.
    inline constexpr float layout_card_text_left { 28.0f };
    inline constexpr float layout_card_minimum_text { 72.0f };
    // 카드 세 번째 줄(상태 조각)의 높이다.
    inline constexpr float layout_card_status_height { 16.0f };
    // 상단 막대(도구 막대·배너) 아래에 드리우는 그림자의 높이와 진하기다. 스크롤된
    // 카드가 막대 아래로 지나갈 때만 나타난다.
    inline constexpr float layout_content_shadow_height { 8.0f };
    inline constexpr float layout_content_shadow_strength { 0.45f };
    // 스크롤 막대의 치수다. 보이는 막대는 `width`이고, 잡기 쉽도록 클릭·끌기를 받는
    // hit 영역은 `hit_width`로 더 넓다. `right_inset`은 창 오른쪽 가장자리의 크기
    // 조절 테두리와 겹치지 않도록 안쪽으로 들여놓는 거리다.
    inline constexpr float layout_scrollbar_width { 8.0f };
    inline constexpr float layout_scrollbar_hit_width { 16.0f };
    inline constexpr float layout_scrollbar_right_inset { 6.0f };
    // 막대가 목록 위아래 끝에 붙지 않도록 두는 여백이다.
    inline constexpr float layout_scrollbar_vertical_inset { 6.0f };
    inline constexpr float layout_scrollbar_minimum_thumb { 24.0f };
    // 선택 카드 전용 하단 로그 pane의 치수다 (stage-7-plan 4.3).
    inline constexpr float layout_log_pane_height { 160.0f };
    inline constexpr float layout_log_header_height { 26.0f };
    inline constexpr float layout_log_line_height { 15.0f };
    inline constexpr float layout_log_text_inset { 8.0f };
    // switch dialog의 치수다. 후보 목록의 스크롤 한계 계산을 logic과 element가
    // 같은 값으로 하도록 여기에 둔다 (stage-7-plan 4.5).
    inline constexpr float layout_switch_dialog_width { 400.0f };
    inline constexpr float layout_switch_dialog_height { 320.0f };
    inline constexpr float layout_switch_dialog_row_height { 22.0f };
    inline constexpr float layout_switch_dialog_list_height { 190.0f };
    // 탐색 후보 선택 등록 dialog의 치수다 (stage-8-plan 5.2). 목록 스크롤 한계를
    // logic과 element가 같은 값으로 계산하도록 여기에 둔다.
    inline constexpr float layout_discovery_dialog_width { 480.0f };
    inline constexpr float layout_discovery_dialog_height { 360.0f };
    inline constexpr float layout_discovery_dialog_row_height { 22.0f };
    inline constexpr float layout_discovery_dialog_list_height { 224.0f };

    // 카드 목록이 차지하는 영역이다. tree 빌드, 카드 목록 element와 logic의 스크롤
    // 한계 계산이 모두 이 함수를 거쳐 같은 값을 쓴다.
    struct list_layout
    {
        // 목록 slot의 위쪽 좌표다 (caption + toolbar + 보이는 notice 배너).
        float content_top { 0.0f };
        // 목록 slot의 높이다. 창이 작으면 0이다.
        float viewport_height { 0.0f };
        // 하단 로그 pane의 영역이다. pane이 없으면 높이가 0이다. 창이 작으면 로그가
        // 먼저 줄어들어 목록이 완전히 사라지지 않는다.
        float log_top { 0.0f };
        float log_height { 0.0f };
    };

    // `window_height`와 결과는 같은 단위다. 논리 픽셀로 물으면 논리 픽셀로 답한다.
    [[nodiscard]] list_layout compute_list_layout(float window_height, float scale, bool has_notice, bool has_log_pane = false) noexcept;

    [[nodiscard]] float card_list_content_height(std::size_t card_count, float scale) noexcept;

    // 목록 content 좌표에서 `card_index` 카드가 차지하는 위쪽 좌표다.
    [[nodiscard]] float card_content_top(std::size_t card_index, float scale) noexcept;

    // 카드 하나가 화면 안에 완전히 들어오도록 조정한 스크롤 값이다. 이미 보이면
    // 현재 값을 그대로 돌려준다.
    [[nodiscard]] float scroll_offset_showing_card(float offset, std::size_t card_index, std::size_t card_count, float viewport_height, float scale) noexcept;

    // 스크롤을 [0, 최대]로 고정한다. 내용이 화면보다 작으면 0이다.
    [[nodiscard]] float clamp_scroll_offset(float offset, float content_height, float viewport_height) noexcept;
} // namespace gitman
