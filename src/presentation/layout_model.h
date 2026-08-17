#pragma once

#include "domain/project.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <vector>

namespace gitman {
    struct rect_f
    {
        float x { 0.0f };
        float y { 0.0f };
        float width { 0.0f };
        float height { 0.0f };

        [[nodiscard]] bool contains(float point_x, float point_y) const noexcept;
    };

    enum class hit_target_kind
    {
        none,
        toolbar_refresh_all,
        toolbar_open_document,
        card_body,
        card_refresh,
        // 단계 7에서 활성화될 버튼이다. 지금은 비활성 표시용 영역만 둔다.
        card_update_disabled,
        card_switch_disabled,
    };

    struct hit_area
    {
        hit_target_kind kind { hit_target_kind::none };
        project_id id {};
        rect_f bounds {};
    };

    // input thread의 hit test와 UI thread의 그리기가 공유하는 layout이다. 두 스레드가
    // 같은 view snapshot에서 같은 순수 함수로 계산하므로 결과가 항상 일치한다.
    struct layout_snapshot
    {
        float window_width { 0.0f };
        float window_height { 0.0f };
        float scale { 1.0f };
        float scroll_offset { 0.0f };
        float content_height { 0.0f };
        std::vector<hit_area> areas {};
    };

    // 논리 픽셀 단위의 layout 상수다. 실제 좌표는 DPI 배율을 곱한다.
    // custom caption이 client 영역 위쪽을 차지하므로 layout은 그 아래에서 시작한다.
    // 값은 `default_caption_ui_metrics.height`와 같아야 한다.
    inline constexpr float layout_caption_height { 40.0f };
    inline constexpr float layout_toolbar_height { 48.0f };
    inline constexpr float layout_card_height { 72.0f };
    inline constexpr float layout_card_gap { 8.0f };
    inline constexpr float layout_margin { 12.0f };
    inline constexpr float layout_button_size { 32.0f };

    [[nodiscard]] float card_list_content_height(std::size_t card_count, float scale) noexcept;

    // 스크롤을 [0, 최대]로 고정한다. 내용이 화면보다 작으면 0이다.
    [[nodiscard]] float clamp_scroll_offset(float offset, float content_height, float viewport_height) noexcept;

    // 화면에 걸치는 카드의 hit 영역만 만든다. 카드가 수백 개여도 목록 크기가 화면에
    // 비례한다.
    [[nodiscard]] layout_snapshot compute_layout(const view_snapshot& view);

    [[nodiscard]] hit_area hit_test(const layout_snapshot& layout, float x, float y) noexcept;
} // namespace gitman
