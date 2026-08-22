#include "presentation/skia_smoke_view.h"

#include "presentation/ui/caption_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <chrono>
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
        const ui_color_palette colors { color_palette_for(state.theme, accent_for(state.accent_id)) };
        ui::draw_context context {
            .canvas = canvas,
            .codicon_typeface = codicon_typeface,
            .ui_typeface = ui_typeface,
            .palette = colors,
            .scale = scale,
            .now = std::chrono::steady_clock::now(),
            .maximized = state.maximized,
        };

        // 앱 모드에서는 tree가 caption을 포함한 화면 전체를 그린다.
        if (state.application_tree != nullptr)
        {
            state.application_tree->draw(context, state.interaction);
            return;
        }

        // smoke 모드는 view snapshot이 없으므로 caption만 단독 tree로 그린다.
        canvas.clear(colors.window_background);
        const ui::ui_tree caption { ui::make_caption_tree(static_cast<float>(state.width), scale, u8"Gitman") };
        caption.draw(context, state.interaction);
        const float caption_height { static_cast<float>(ui::default_caption_ui_metrics.height) * scale };

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

        text_paint.setColor(state.used_fallback ? colors.warning_accent : colors.accent_emphasis_foreground);
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
