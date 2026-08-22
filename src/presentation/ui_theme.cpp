#include "presentation/ui_theme.h"

#include "gitman/generated/accents.h"

#include <array>
#include <cstddef>

namespace gitman {
    namespace {
        using accent_table = std::array<accent_definition, std::size(generated::accents)>;

        accent_table build_catalog() noexcept
        {
            accent_table values {};
            for (std::size_t index = 0; index < values.size(); ++index)
            {
                const generated::accent_entry& entry { generated::accents[index] };
                values[index] = accent_definition {
                    .id = entry.id,
                    .label = entry.label,
                    .swatch = entry.swatch,
                    .dark = { entry.dark[0], entry.dark[1], entry.dark[2], entry.dark[3] },
                    .light = { entry.light[0], entry.light[1], entry.light[2], entry.light[3] },
                };
            }
            return values;
        }

        // 생성 표(gitman/generated/accents.h)를 한 번만 접어 놓는다. 문자열과 색은
        // 모두 정적 수명이라 복사가 없다.
        const accent_table& catalog() noexcept
        {
            static const accent_table built { build_catalog() };
            return built;
        }

        // 키 컬러와 무관한 중립 색이다. 팔레트 합성이 여기에 accent 4역할을 얹는다.
        constexpr ui_color_palette dark_neutral_palette {
            .window_background = make_ui_color(30, 30, 30),
            .surface_background = make_ui_color(45, 45, 48),
            .primary_foreground = make_ui_color(255, 255, 255),
            .warning_accent = make_ui_color(220, 170, 45),
            .error_accent = make_ui_color(224, 108, 117),
            .button_hover_background = make_ui_color(255, 255, 255, 26),
            .button_hover_foreground = make_ui_color(255, 255, 255),
            .button_pressed_background = make_ui_color(255, 255, 255, 45),
            .tooltip_background = make_ui_color(37, 37, 38),
            .tooltip_border = make_ui_color(90, 90, 92),
            .content_shadow = make_ui_color(0, 0, 0),
            .notice_background = make_ui_color(66, 36, 39),
            .caption = {
                .background = make_ui_color(37, 37, 38),
                .foreground = make_ui_color(255, 255, 255),
                .button_hover_background = make_ui_color(63, 63, 64),
                .button_hover_foreground = make_ui_color(255, 255, 255),
                .close_button_hover_background = make_ui_color(196, 43, 28),
                .close_button_hover_foreground = make_ui_color(255, 255, 255),
            },
        };

        // 밝은 바탕이다 (T3.2). 중립 색은 VSCode Light Modern에 맞췄다. 낮은
        // 알파로 primary_foreground를 겹쳐 쓰는 그리기 코드는 전경색이 뒤집히면서
        // 그대로 성립한다.
        constexpr ui_color_palette light_neutral_palette {
            .window_background = make_ui_color(248, 248, 248),
            .surface_background = make_ui_color(255, 255, 255),
            .primary_foreground = make_ui_color(31, 31, 31),
            .warning_accent = make_ui_color(154, 103, 0),
            .error_accent = make_ui_color(192, 48, 58),
            .button_hover_background = make_ui_color(0, 0, 0, 20),
            .button_hover_foreground = make_ui_color(31, 31, 31),
            .button_pressed_background = make_ui_color(0, 0, 0, 36),
            .tooltip_background = make_ui_color(255, 255, 255),
            .tooltip_border = make_ui_color(200, 200, 200),
            .content_shadow = make_ui_color(0, 0, 0),
            .notice_background = make_ui_color(253, 231, 233),
            .caption = {
                .background = make_ui_color(240, 240, 240),
                .foreground = make_ui_color(31, 31, 31),
                .button_hover_background = make_ui_color(218, 218, 218),
                .button_hover_foreground = make_ui_color(31, 31, 31),
                .close_button_hover_background = make_ui_color(196, 43, 28),
                .close_button_hover_foreground = make_ui_color(255, 255, 255),
            },
        };

        constexpr ui_color_palette high_contrast_palette {
            .window_background = make_ui_color(0, 0, 0),
            .surface_background = make_ui_color(0, 0, 0),
            .primary_foreground = make_ui_color(255, 255, 255),
            // 고대비는 키 컬러를 쓰지 않는다. 네 역할이 모두 흰색이다.
            .accent = make_ui_color(255, 255, 255),
            .accent_hover = make_ui_color(255, 255, 255),
            .accent_soft = make_ui_color(255, 255, 255),
            .accent_emphasis_foreground = make_ui_color(255, 255, 255),
            .warning_accent = make_ui_color(255, 255, 255),
            .error_accent = make_ui_color(255, 255, 255),
            .button_hover_background = make_ui_color(255, 255, 255),
            .button_hover_foreground = make_ui_color(0, 0, 0),
            .button_pressed_background = make_ui_color(255, 255, 255),
            .tooltip_background = make_ui_color(0, 0, 0),
            .tooltip_border = make_ui_color(255, 255, 255),
            .content_shadow = make_ui_color(255, 255, 255),
            .notice_background = make_ui_color(0, 0, 0),
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

    const accent_color_set& accent_definition::for_theme(const color_theme theme) const noexcept
    {
        switch (theme)
        {
        case color_theme::light:
            return light;
        case color_theme::high_contrast:
        case color_theme::dark:
        default:
            return dark;
        }
    }

    std::span<const accent_definition> accent_catalog() noexcept
    {
        return { catalog().data(), catalog().size() };
    }

    const accent_definition& accent_for(const std::u8string_view id) noexcept
    {
        for (const accent_definition& value : catalog())
            if (value.id == id)
                return value;
        // 목록에 없는 id는 기본 색으로 물러선다 (T4.3). 생성 script가 mint의
        // 존재를 보장하므로 첫 항목 fallback은 실제로 도달하지 않는다.
        for (const accent_definition& value : catalog())
            if (value.id == default_accent_id)
                return value;
        return catalog().front();
    }

    bool accent_exists(const std::u8string_view id) noexcept
    {
        for (const accent_definition& value : catalog())
            if (value.id == id)
                return true;
        return false;
    }

    ui_color_palette color_palette_for(const color_theme theme, const accent_definition& accent) noexcept
    {
        if (theme == color_theme::high_contrast)
            return high_contrast_palette;

        const accent_color_set& colors { accent.for_theme(theme) };
        ui_color_palette palette { theme == color_theme::light ? light_neutral_palette : dark_neutral_palette };
        palette.accent = colors.accent;
        palette.accent_hover = colors.hover;
        palette.accent_soft = colors.soft;
        palette.accent_emphasis_foreground = colors.emphasis_foreground;
        return palette;
    }

    ui_color_palette color_palette_for(const color_theme theme) noexcept
    {
        return color_palette_for(theme, accent_for(default_accent_id));
    }

    color_theme resolve_color_theme(const theme_preference preference, const bool high_contrast, const bool system_prefers_light) noexcept
    {
        // 고대비는 접근성 설정이라 어떤 선호보다 세다.
        if (high_contrast)
            return color_theme::high_contrast;
        switch (preference)
        {
        case theme_preference::light:
            return color_theme::light;
        case theme_preference::dark:
            return color_theme::dark;
        case theme_preference::system:
        default:
            return system_prefers_light ? color_theme::light : color_theme::dark;
        }
    }
} // namespace gitman
