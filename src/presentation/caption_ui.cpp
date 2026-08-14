#include "presentation/caption_ui.h"

#include "gitman/generated/codicons.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <array>

namespace gitman {
    namespace {
        bool draw_centered_codicon(SkCanvas& canvas, const char32_t codepoint, const SkRect& target, const SkFont& font, const SkPaint& paint)
        {
            const SkGlyphID glyph { font.unicharToGlyph(static_cast<SkUnichar>(codepoint)) };
            if (glyph == 0)
                return false;

            const SkRect glyph_bounds { font.getBounds(glyph, &paint) };
            if (glyph_bounds.isEmpty())
                return false;

            const std::array glyphs { glyph };
            const std::array positions {
                SkPoint {
                    target.centerX() - glyph_bounds.centerX(),
                    target.centerY() - glyph_bounds.centerY(),
                },
            };
            canvas.drawGlyphs(glyphs, positions, SkPoint {}, font, paint);
            return true;
        }

        float centered_text_baseline(const SkFont& font, const float target_height)
        {
            SkFontMetrics font_metrics {};
            font.getMetrics(&font_metrics);
            return target_height * 0.5F - (font_metrics.fAscent + font_metrics.fDescent) * 0.5F;
        }

        void draw_utf8(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
        {
            canvas.drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y, font, paint);
        }

        void draw_caption_button(SkCanvas& canvas, const char32_t codepoint, const SkRect& target, const SkFont& font, const SkPaint& foreground_paint, const bool hovered,
            const ui_color hover_background, const ui_color hover_foreground)
        {
            SkPaint icon_paint { foreground_paint };
            if (hovered)
            {
                SkPaint hover_paint {};
                hover_paint.setColor(hover_background);
                canvas.drawRect(target, hover_paint);
                icon_paint.setColor(hover_foreground);
            }
            draw_centered_codicon(canvas, codepoint, target, font, icon_paint);
        }
    } // namespace

    caption_ui::caption_ui(const caption_color_palette colors, const caption_ui_metrics metrics) noexcept
        : colors_ { colors }
        , metrics_ { metrics }
    {}

    const caption_ui_metrics& caption_ui::metrics() const noexcept
    {
        return metrics_;
    }

    float caption_ui::height(const float dpi_scale) const noexcept
    {
        return static_cast<float>(metrics_.height) * std::max(0.0F, dpi_scale);
    }

    void caption_ui::draw(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const caption_ui_state& state) const
    {
        const float scale { std::max(0.0F, state.dpi_scale) };
        const float caption_height { height(scale) };
        const float button_width { static_cast<float>(metrics_.button_width) * scale };

        SkPaint fill_paint {};
        fill_paint.setAntiAlias(true);
        fill_paint.setColor(colors_.background);
        canvas.drawRect(SkRect::MakeXYWH(0.0F, 0.0F, static_cast<float>(state.width), caption_height), fill_paint);

        SkPaint foreground_paint {};
        foreground_paint.setAntiAlias(true);
        foreground_paint.setColor(colors_.foreground);
        const SkFont title_font { sk_ref_sp(ui_typeface), static_cast<float>(metrics_.title_font_size) * scale };

        float title_left { static_cast<float>(metrics_.title_left_padding) * scale };
        if (codicon_typeface != nullptr)
        {
            const SkFont application_icon_font { sk_ref_sp(codicon_typeface), static_cast<float>(metrics_.application_icon_size) * scale };
            const SkRect icon_slot {
                SkRect::MakeXYWH(0.0F, 0.0F, static_cast<float>(metrics_.application_icon_slot_width) * scale, caption_height),
            };
            if (draw_centered_codicon(canvas, codicons::icon_source_control, icon_slot, application_icon_font, foreground_paint))
                title_left = static_cast<float>(metrics_.application_icon_slot_width + metrics_.title_icon_gap) * scale;
        }
        draw_utf8(canvas, state.title, title_left, centered_text_baseline(title_font, caption_height), title_font, foreground_paint);

        if (codicon_typeface == nullptr)
            return;

        const float close_left { static_cast<float>(state.width) - button_width };
        const float maximize_left { close_left - button_width };
        const float minimize_left { maximize_left - button_width };
        const SkFont button_font { sk_ref_sp(codicon_typeface), static_cast<float>(metrics_.button_icon_size) * scale };
        const SkRect minimize_slot { SkRect::MakeXYWH(minimize_left, 0.0F, button_width, caption_height) };
        const SkRect maximize_slot { SkRect::MakeXYWH(maximize_left, 0.0F, button_width, caption_height) };
        const SkRect close_slot { SkRect::MakeXYWH(close_left, 0.0F, button_width, caption_height) };
        const char32_t maximize_icon { state.maximized ? codicons::icon_chrome_restore : codicons::icon_chrome_maximize };

        draw_caption_button(canvas, codicons::icon_chrome_minimize, minimize_slot, button_font, foreground_paint, state.hovered_button == caption_button_hover::minimize,
            colors_.button_hover_background, colors_.button_hover_foreground);
        draw_caption_button(canvas, maximize_icon, maximize_slot, button_font, foreground_paint, state.hovered_button == caption_button_hover::maximize, colors_.button_hover_background,
            colors_.button_hover_foreground);
        draw_caption_button(canvas, codicons::icon_chrome_close, close_slot, button_font, foreground_paint, state.hovered_button == caption_button_hover::close, colors_.close_button_hover_background,
            colors_.close_button_hover_foreground);
    }
} // namespace gitman
