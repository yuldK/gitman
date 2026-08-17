#include "presentation/ui/draw_primitives.h"

#include "gitman/generated/codicons.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"

#include <array>

namespace gitman::ui {
    SkPaint solid_paint(const ui_color color)
    {
        SkPaint paint {};
        paint.setAntiAlias(true);
        paint.setColor(color);
        return paint;
    }

    void draw_text(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
    {
        if (text.empty())
            return;
        canvas.drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y, font, paint);
    }

    void draw_centered_glyph(SkCanvas& canvas, const char32_t codepoint, const rect_f& target, const SkFont& font, const SkPaint& paint)
    {
        const SkGlyphID glyph { font.unicharToGlyph(static_cast<SkUnichar>(codepoint)) };
        if (glyph == 0)
            return;

        const SkRect glyph_bounds { font.getBounds(glyph, &paint) };
        if (glyph_bounds.isEmpty())
            return;

        const SkRect bounds { SkRect::MakeXYWH(target.x, target.y, target.width, target.height) };
        const std::array glyphs { glyph };
        const std::array positions {
            SkPoint {
                bounds.centerX() - glyph_bounds.centerX(),
                bounds.centerY() - glyph_bounds.centerY(),
            },
        };
        canvas.drawGlyphs(glyphs, positions, SkPoint {}, font, paint);
    }

    float centered_text_baseline(const SkFont& font, const float target_height)
    {
        SkFontMetrics font_metrics {};
        font.getMetrics(&font_metrics);
        return target_height * 0.5f - (font_metrics.fAscent + font_metrics.fDescent) * 0.5f;
    }

    char32_t codicon_for_name(const std::u8string_view name) noexcept
    {
        if (name == u8"pass")
            return codicons::icon_pass;
        if (name == u8"arrow-down")
            return codicons::icon_arrow_down;
        if (name == u8"arrow-up")
            return codicons::icon_arrow_up;
        if (name == u8"git-compare")
            return codicons::icon_git_compare;
        if (name == u8"key")
            return codicons::icon_key;
        if (name == u8"home")
            return codicons::icon_home;
        if (name == u8"warning")
            return codicons::icon_warning;
        if (name == u8"debug-disconnect")
            return codicons::icon_debug_disconnect;
        if (name == u8"error")
            return codicons::icon_error;
        if (name == u8"circle-slash")
            return codicons::icon_circle_slash;
        return codicons::icon_question;
    }
} // namespace gitman::ui
