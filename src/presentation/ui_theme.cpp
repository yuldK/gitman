#include "presentation/ui_theme.h"

namespace gitman {
    namespace {
        constexpr ui_color_palette dark_palette {
            .window_background = make_ui_color(30, 30, 30),
            .surface_background = make_ui_color(45, 45, 48),
            .primary_foreground = make_ui_color(255, 255, 255),
            .positive_accent = make_ui_color(78, 201, 176),
            .warning_accent = make_ui_color(220, 170, 45),
            .caption = {
                .background = make_ui_color(37, 37, 38),
                .foreground = make_ui_color(255, 255, 255),
                .button_hover_background = make_ui_color(63, 63, 64),
                .button_hover_foreground = make_ui_color(255, 255, 255),
                .close_button_hover_background = make_ui_color(196, 43, 28),
                .close_button_hover_foreground = make_ui_color(255, 255, 255),
            },
        };

        constexpr ui_color_palette high_contrast_palette {
            .window_background = make_ui_color(0, 0, 0),
            .surface_background = make_ui_color(0, 0, 0),
            .primary_foreground = make_ui_color(255, 255, 255),
            .positive_accent = make_ui_color(255, 255, 255),
            .warning_accent = make_ui_color(255, 255, 255),
            .caption = {
                .background = make_ui_color(0, 0, 0),
                .foreground = make_ui_color(255, 255, 255),
                .button_hover_background = make_ui_color(255, 255, 255),
                .button_hover_foreground = make_ui_color(0, 0, 0),
                .close_button_hover_background = make_ui_color(255, 255, 255),
                .close_button_hover_foreground = make_ui_color(0, 0, 0),
            },
        };
    } // namespace

    const ui_color_palette& color_palette_for(const color_theme theme) noexcept
    {
        switch (theme)
        {
        case color_theme::high_contrast:
            return high_contrast_palette;
        case color_theme::dark:
        default:
            return dark_palette;
        }
    }
} // namespace gitman
