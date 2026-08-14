#include "presentation/skia_smoke_view.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <string_view>

namespace gitman {
    namespace {
        void draw_utf8(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
        {
            canvas.drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y, font, paint);
        }
    } // namespace

    void draw_skia_smoke_view(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const smoke_view_state& state)
    {
        const float scale { state.dpi_scale };
        const ui_color_palette& colors { color_palette_for(state.theme) };
        const caption_ui caption { colors.caption };
        const float caption_height { caption.height(scale) };

        canvas.clear(colors.window_background);
        caption.draw(canvas, codicon_typeface, ui_typeface,
            caption_ui_state {
                .width = state.width,
                .dpi_scale = state.dpi_scale,
                .maximized = state.maximized,
                .hovered_button = state.hovered_caption_button,
                .title = u8"Gitman",
            });

        SkPaint fill_paint {};
        fill_paint.setAntiAlias(true);

        SkPaint text_paint {};
        text_paint.setAntiAlias(true);
        text_paint.setColor(colors.primary_foreground);
        SkFont ui_font { sk_ref_sp(ui_typeface), 15.0F * scale };

        const float card_x { 24.0F * scale };
        const float card_y { caption_height + 28.0F * scale };
        fill_paint.setColor(colors.surface_background);
        canvas.drawRoundRect(SkRect::MakeXYWH(card_x, card_y, static_cast<float>(state.width) - 48.0F * scale, 112.0F * scale), 8.0F * scale, 8.0F * scale, fill_paint);

        text_paint.setColor(state.used_fallback ? colors.warning_accent : colors.positive_accent);
        const std::u8string_view backend_text {
            state.backend == renderer_backend::direct3d ? u8"Direct3D renderer 활성" : u8"CPU renderer 활성",
        };
        draw_utf8(canvas, backend_text, card_x + 20.0F * scale, card_y + 43.0F * scale, ui_font, text_paint);

        text_paint.setColor(colors.primary_foreground);
        const std::u8string_view detail_text {
            state.used_fallback ? u8"Direct3D 초기화 실패를 감지해 CPU로 전환했습니다." : u8"ADR-001 단계 1 smoke view",
        };
        draw_utf8(canvas, detail_text, card_x + 20.0F * scale, card_y + 76.0F * scale, ui_font, text_paint);
    }
} // namespace gitman
