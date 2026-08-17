#pragma once

#include <cstdint>

namespace gitman {
    using ui_color = std::uint32_t;

    enum class color_theme
    {
        dark,
        high_contrast,
    };

    struct caption_color_palette
    {
        ui_color background { 0 };
        ui_color foreground { 0 };
        ui_color button_hover_background { 0 };
        ui_color button_hover_foreground { 0 };
        ui_color close_button_hover_background { 0 };
        ui_color close_button_hover_foreground { 0 };
    };

    struct ui_color_palette
    {
        ui_color window_background { 0 };
        ui_color surface_background { 0 };
        ui_color primary_foreground { 0 };
        ui_color positive_accent { 0 };
        ui_color warning_accent { 0 };
        ui_color error_accent { 0 };
        // caption 밖의 일반 버튼(도구 막대·카드)의 hover와 눌림 표시다.
        ui_color button_hover_background { 0 };
        ui_color button_hover_foreground { 0 };
        ui_color button_pressed_background { 0 };
        ui_color tooltip_background { 0 };
        ui_color tooltip_border { 0 };
        caption_color_palette caption {};
    };

    [[nodiscard]] constexpr ui_color make_ui_color(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue, const std::uint8_t alpha = 255) noexcept
    {
        return static_cast<ui_color>(alpha) << 24U | static_cast<ui_color>(red) << 16U | static_cast<ui_color>(green) << 8U | static_cast<ui_color>(blue);
    }

    [[nodiscard]] const ui_color_palette& color_palette_for(color_theme theme) noexcept;
} // namespace gitman
