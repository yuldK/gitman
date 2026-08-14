#include "presentation/skia_smoke_view.h"

#include "gitman/generated/codicons.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <array>
#include <string_view>

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

        void draw_caption_button(
            SkCanvas& canvas, const char32_t codepoint, const SkRect& target, const SkFont& font, const SkPaint& text_paint, const bool hovered, const bool close_button, const bool high_contrast)
        {
            SkPaint icon_paint { text_paint };
            if (hovered)
            {
                SkPaint hover_paint {};
                hover_paint.setColor(high_contrast ? SK_ColorWHITE : close_button ? SkColorSetRGB(196, 43, 28) : SkColorSetRGB(63, 63, 64));
                canvas.drawRect(target, hover_paint);
                if (high_contrast)
                    icon_paint.setColor(SK_ColorBLACK);
            }
            draw_centered_codicon(canvas, codepoint, target, font, icon_paint);
        }

        void draw_utf8(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
        {
            canvas.drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y, font, paint);
        }
    } // namespace

    void draw_skia_smoke_view(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const smoke_view_state& state)
    {
        const float scale { state.dpi_scale };
        const SkColor background {
            state.high_contrast ? SK_ColorBLACK : SkColorSetRGB(30, 30, 30),
        };
        const SkColor caption { state.high_contrast ? SK_ColorBLACK : SkColorSetRGB(37, 37, 38) };
        const SkColor foreground { SK_ColorWHITE };
        const SkColor accent {
            state.used_fallback ? SkColorSetRGB(220, 170, 45) : SkColorSetRGB(78, 201, 176),
        };
        const float caption_height { 48.0F * scale };
        const float button_width { 46.0F * scale };

        canvas.clear(background);
        SkPaint fill_paint {};
        fill_paint.setAntiAlias(true);
        fill_paint.setColor(caption);
        canvas.drawRect(SkRect::MakeXYWH(0.0F, 0.0F, static_cast<float>(state.width), caption_height), fill_paint);

        SkPaint text_paint {};
        text_paint.setAntiAlias(true);
        text_paint.setColor(foreground);
        SkFont ui_font { sk_ref_sp(ui_typeface), 15.0F * scale };

        float title_left { 16.0F * scale };
        if (codicon_typeface != nullptr)
        {
            const SkFont icon_font { sk_ref_sp(codicon_typeface), 20.0F * scale };
            const SkRect icon_slot { SkRect::MakeXYWH(0.0F, 0.0F, 46.0F * scale, caption_height) };
            if (draw_centered_codicon(canvas, codicons::icon_source_control, icon_slot, icon_font, text_paint))
                title_left = 56.0F * scale;
        }
        draw_utf8(canvas, u8"Gitman", title_left, 30.0F * scale, ui_font, text_paint);

        const float close_left { static_cast<float>(state.width) - button_width };
        const float maximize_left { close_left - button_width };
        const float minimize_left { maximize_left - button_width };
        if (codicon_typeface != nullptr)
        {
            const SkFont chrome_font { sk_ref_sp(codicon_typeface), 16.0F * scale };
            const SkRect minimize_slot { SkRect::MakeXYWH(minimize_left, 0.0F, button_width, caption_height) };
            const SkRect maximize_slot { SkRect::MakeXYWH(maximize_left, 0.0F, button_width, caption_height) };
            const SkRect close_slot { SkRect::MakeXYWH(close_left, 0.0F, button_width, caption_height) };
            const char32_t maximize_icon { state.maximized ? codicons::icon_chrome_restore : codicons::icon_chrome_maximize };

            draw_caption_button(canvas
                , codicons::icon_chrome_minimize
                , minimize_slot
                , chrome_font
                , text_paint
                , state.hovered_caption_button == caption_button_hover::minimize
                , false
                , state.high_contrast
            );

            draw_caption_button(canvas
                , maximize_icon
                , maximize_slot
                , chrome_font
                , text_paint
                , state.hovered_caption_button == caption_button_hover::maximize
                , false
                , state.high_contrast
            );

            draw_caption_button(canvas
                , codicons::icon_chrome_close
                , close_slot
                , chrome_font
                , text_paint
                , state.hovered_caption_button == caption_button_hover::close
                , true
                , state.high_contrast
            );
        }

        const float card_x { 24.0F * scale };
        const float card_y { caption_height + 28.0F * scale };
        fill_paint.setColor(state.high_contrast ? SK_ColorBLACK : SkColorSetRGB(45, 45, 48));
        canvas.drawRoundRect(
            SkRect::MakeXYWH(card_x, card_y, static_cast<float>(state.width) - 48.0F * scale, 112.0F * scale)
            , 8.0F * scale, 8.0F * scale
            , fill_paint
        );

        text_paint.setColor(accent);
        const std::u8string_view backend_text {
            state.backend == renderer_backend::direct3d ? u8"Direct3D renderer 활성" : u8"CPU renderer 활성",
        };
        draw_utf8(canvas, backend_text, card_x + 20.0F * scale, card_y + 43.0F * scale, ui_font, text_paint);

        text_paint.setColor(foreground);
        const std::u8string_view detail_text {
            state.used_fallback ? u8"Direct3D 초기화 실패를 감지해 CPU로 전환했습니다." : u8"ADR-001 단계 1 smoke view",
        };
        draw_utf8(canvas, detail_text, card_x + 20.0F * scale, card_y + 76.0F * scale, ui_font, text_paint);
    }
} // namespace gitman
