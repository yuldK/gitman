#include "platform/win32/caption_layout.h"

#include "presentation/ui/caption_metrics.h"

#include <algorithm>

namespace gitman::win32 {
    namespace {
        int scale_for_dpi(const int value, const std::uint32_t dpi) noexcept
        {
            return static_cast<int>((static_cast<std::uint64_t>(value) * dpi + 48U) / 96U);
        }
    } // namespace

    caption_layout make_caption_layout(const int client_width, const std::uint32_t dpi) noexcept
    {
        caption_layout layout {};
        layout.height = scale_for_dpi(ui::default_caption_ui_metrics.height, dpi);
        layout.button_width = scale_for_dpi(ui::default_caption_ui_metrics.button_width, dpi);
        layout.right = std::max(0, client_width);
        layout.close_left = std::max(0, layout.right - layout.button_width);
        layout.maximize_left = std::max(0, layout.close_left - layout.button_width);
        layout.minimize_left = std::max(0, layout.maximize_left - layout.button_width);
        return layout;
    }

    caption_hit hit_test_caption(const caption_layout& layout, const int x, const int y) noexcept
    {
        if (x < 0 || x >= layout.right || y < 0 || y >= layout.height)
            return caption_hit::client;
        if (x >= layout.close_left)
            return caption_hit::close;
        if (x >= layout.maximize_left)
            return caption_hit::maximize;
        if (x >= layout.minimize_left)
            return caption_hit::minimize;
        return caption_hit::drag;
    }
} // namespace gitman::win32
