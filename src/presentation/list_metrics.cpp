#include "presentation/list_metrics.h"

namespace gitman {
    float card_list_content_height(const std::size_t card_count, const float scale) noexcept
    {
        if (card_count == 0)
            return layout_margin * 2.0f * scale;
        const float cards { static_cast<float>(card_count) * layout_card_height + static_cast<float>(card_count - 1) * layout_card_gap };
        return (cards + layout_margin * 2.0f) * scale;
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
