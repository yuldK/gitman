#include "presentation/list_metrics.h"

#include <cmath>
#include <cstddef>

namespace gitman {
    list_layout compute_list_layout(const float window_height, const float scale, const bool has_notice, const bool has_log_pane) noexcept
    {
        const float factor { scale > 0.0f ? scale : 1.0f };
        list_layout layout {};
        layout.content_top = (layout_caption_height + layout_toolbar_height + (has_notice ? layout_notice_height : 0.0f)) * factor;
        float available { window_height - layout.content_top };
        if (available < 0.0f)
            available = 0.0f;

        // 로그 pane은 남은 공간 안에서만 자리를 차지한다. 창이 아주 작으면 로그가
        // 먼저 줄어든다.
        float log_height { has_log_pane ? layout_log_pane_height * factor : 0.0f };
        if (log_height > available)
            log_height = available;
        layout.viewport_height = available - log_height;
        layout.log_top = layout.content_top + layout.viewport_height;
        layout.log_height = log_height;
        return layout;
    }

    float card_list_content_height(const std::size_t card_count, const float scale) noexcept
    {
        if (card_count == 0)
            return layout_margin * 2.0f * scale;
        const float cards { static_cast<float>(card_count) * layout_card_height + static_cast<float>(card_count - 1) * layout_card_gap };
        return (cards + layout_margin * 2.0f) * scale;
    }

    float card_content_top(const std::size_t card_index, const float scale) noexcept
    {
        return (layout_margin + static_cast<float>(card_index) * (layout_card_height + layout_card_gap)) * scale;
    }

    float scroll_offset_showing_card(const float offset, const std::size_t card_index, const std::size_t card_count, const float viewport_height, const float scale) noexcept
    {
        if (card_index >= card_count)
            return offset;

        const float top { card_content_top(card_index, scale) };
        const float bottom { top + layout_card_height * scale };
        const float margin { layout_margin * scale };

        float adjusted { offset };
        if (bottom > offset + viewport_height)
            adjusted = bottom - viewport_height + margin;
        // 위로 벗어난 경우가 뒤에 온다. 카드가 화면보다 클 때 위쪽을 우선한다.
        if (top < adjusted)
            adjusted = top - margin;
        return clamp_scroll_offset(adjusted, card_list_content_height(card_count, scale), viewport_height);
    }

    float clamp_scroll_offset(const float offset, const float content_height, const float viewport_height) noexcept
    {
        const float maximum { content_height - viewport_height };
        if (maximum <= 0.0f)
            return 0.0f;
        if (offset < 0.0f)
            return 0.0f;
        return offset > maximum ? maximum : offset;
    }

    std::size_t card_drag_insertion_slot(
        const float pointer_y, const float list_top, const float scroll, const std::size_t dragged_index, const std::size_t card_count, const float scale) noexcept
    {
        if (card_count <= 1)
            return 0;

        const float pitch { (layout_card_height + layout_card_gap) * scale };
        const float content_y { pointer_y - list_top + scroll };
        // 원래 배치 기준 "몇 번째 카드 앞인가" [0, card_count]다. 카드 중앙보다
        // 위면 그 카드 앞이 된다.
        const float relative { content_y - card_content_top(0, scale) - layout_card_height * scale * 0.5f };
        std::ptrdiff_t before { static_cast<std::ptrdiff_t>(std::floor(relative / pitch)) + 1 };
        if (before < 0)
            before = 0;
        if (before > static_cast<std::ptrdiff_t>(card_count))
            before = static_cast<std::ptrdiff_t>(card_count);

        // dragged를 뺀 목록의 위치로 바꾼다.
        if (before > static_cast<std::ptrdiff_t>(dragged_index))
            --before;
        return static_cast<std::size_t>(before);
    }

    float card_drag_offset(const std::size_t card_index, const std::size_t dragged_index, const std::size_t insertion_slot, const float scale) noexcept
    {
        if (card_index == dragged_index)
            return 0.0f;

        const float pitch { (layout_card_height + layout_card_gap) * scale };
        const std::size_t remaining { card_index > dragged_index ? card_index - 1 : card_index };
        const std::size_t slot { remaining + (remaining >= insertion_slot ? 1u : 0u) };
        return (static_cast<float>(slot) - static_cast<float>(card_index)) * pitch;
    }
} // namespace gitman
