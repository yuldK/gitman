#pragma once

#include <optional>
#include <string_view>

namespace gitman {
    enum class renderer_mode
    {
        automatic,
        direct3d,
        cpu,
    };

    enum class renderer_backend
    {
        direct3d,
        cpu,
    };

    enum class renderer_selection_status
    {
        selected,
        unavailable,
    };

    struct renderer_selection
    {
        renderer_selection_status status { renderer_selection_status::unavailable };
        renderer_backend backend { renderer_backend::cpu };
        bool used_fallback { false };
    };

    [[nodiscard]] std::optional<renderer_mode> parse_renderer_mode(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view renderer_mode_name(renderer_mode mode) noexcept;
    [[nodiscard]] std::u8string_view renderer_backend_name(renderer_backend backend) noexcept;
    [[nodiscard]] renderer_selection select_renderer_backend(renderer_mode mode, bool direct3d_available) noexcept;
} // namespace gitman
