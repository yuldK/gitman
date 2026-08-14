#pragma once

#include <cstdint>

namespace gitman::win32 {
    struct caption_layout
    {
        int height { 0 };
        int button_width { 0 };
        int minimize_left { 0 };
        int maximize_left { 0 };
        int close_left { 0 };
        int right { 0 };
    };

    enum class caption_hit
    {
        client,
        drag,
        minimize,
        maximize,
        close,
    };

    [[nodiscard]] caption_layout make_caption_layout(int client_width, std::uint32_t dpi) noexcept;
    [[nodiscard]] caption_hit hit_test_caption(const caption_layout& layout, int x, int y) noexcept;
} // namespace gitman::win32
