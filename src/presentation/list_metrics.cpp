#include "presentation/list_metrics.h"

namespace gitman {
    list_layout compute_list_layout(const float window_height, const float scale, const bool has_notice) noexcept
    {
        const float factor { scale > 0.0f ? scale : 1.0f };
        list_layout layout {};
        layout.content_top = (layout_caption_height + layout_toolbar_height + (has_notice ? layout_notice_height : 0.0f)) * factor;
        layout.viewport_height = window_height - layout.content_top;
        if (layout.viewport_height < 0.0f)
            layout.viewport_height = 0.0f;
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
} // namespace gitman
