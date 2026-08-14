#include "presentation/renderer_policy.h"

namespace gitman {
    std::optional<renderer_mode> parse_renderer_mode(const std::u8string_view value) noexcept
    {
        if (value == u8"auto")
            return renderer_mode::automatic;
        if (value == u8"direct3d")
            return renderer_mode::direct3d;
        if (value == u8"cpu")
            return renderer_mode::cpu;
        return std::nullopt;
    }

    std::u8string_view renderer_mode_name(const renderer_mode mode) noexcept
    {
        switch (mode)
        {
        case renderer_mode::automatic:
            return u8"auto";
        case renderer_mode::direct3d:
            return u8"direct3d";
        case renderer_mode::cpu:
            return u8"cpu";
        }
        return u8"unknown";
    }

    std::u8string_view renderer_backend_name(const renderer_backend backend) noexcept
    {
        switch (backend)
        {
        case renderer_backend::direct3d:
            return u8"direct3d";
        case renderer_backend::cpu:
            return u8"cpu";
        }
        return u8"unknown";
    }

    renderer_selection select_renderer_backend(const renderer_mode mode, const bool direct3d_available) noexcept
    {
        if (mode == renderer_mode::cpu)
            return { renderer_selection_status::selected, renderer_backend::cpu, false };
        if (direct3d_available)
            return { renderer_selection_status::selected, renderer_backend::direct3d, false };
        if (mode == renderer_mode::automatic)
            return { renderer_selection_status::selected, renderer_backend::cpu, true };
        return { renderer_selection_status::unavailable, renderer_backend::direct3d, false };
    }
} // namespace gitman
