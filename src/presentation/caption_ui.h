#pragma once

#include "presentation/ui_theme.h"

#include <string_view>

class SkCanvas;
class SkTypeface;

namespace gitman {
    enum class caption_button_hover
    {
        none,
        minimize,
        maximize,
        close,
    };

    struct caption_ui_metrics
    {
        int height { 40 };
        int button_width { 46 };
        int application_icon_slot_width { 46 };
        int title_left_padding { 16 };
        int title_icon_gap { 10 };
        int title_font_size { 15 };
        int application_icon_size { 18 };
        int button_icon_size { 15 };
    };

    inline constexpr caption_ui_metrics default_caption_ui_metrics {};

    struct caption_ui_state
    {
        int width { 0 };
        float dpi_scale { 1.0F };
        bool maximized { false };
        caption_button_hover hovered_button { caption_button_hover::none };
        std::u8string_view title { u8"Gitman" };
    };

    class caption_ui final
    {
    public:
        explicit caption_ui(caption_color_palette colors, caption_ui_metrics metrics = default_caption_ui_metrics) noexcept;

        [[nodiscard]] const caption_ui_metrics& metrics() const noexcept;
        [[nodiscard]] float height(float dpi_scale) const noexcept;
        void draw(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const caption_ui_state& state) const;

    private:
        caption_color_palette colors_ {};
        caption_ui_metrics metrics_ {};
    };
} // namespace gitman
