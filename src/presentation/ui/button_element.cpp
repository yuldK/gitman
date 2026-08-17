#include "presentation/ui/button_element.h"

#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <utility>

namespace gitman::ui {
    namespace {
        struct button_colors
        {
            ui_color hover_background { 0 };
            ui_color hover_foreground { 0 };
            ui_color pressed_background { 0 };
            ui_color foreground { 0 };
        };

        button_colors colors_for(const ui_color_palette& palette, const button_visual_role role) noexcept
        {
            switch (role)
            {
            case button_visual_role::caption:
                return { palette.caption.button_hover_background, palette.caption.button_hover_foreground, palette.caption.button_hover_background, palette.caption.foreground };
            case button_visual_role::caption_close:
                return { palette.caption.close_button_hover_background, palette.caption.close_button_hover_foreground, palette.caption.close_button_hover_background, palette.caption.foreground };
            case button_visual_role::toolbar:
                break;
            }
            return { palette.button_hover_background, palette.button_hover_foreground, palette.button_pressed_background, palette.primary_foreground };
        }
    } // namespace

    button_element::button_element(ui_element_id id, const button_config config) noexcept
        : ui_element { std::move(id) }
        , config_ { config }
    {}

    void button_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void button_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const button_colors colors { colors_for(context.palette, config_.role) };
        const bool hovered { enabled() && interaction.hovered == id() };
        const bool pressed { enabled() && interaction.pressed == id() };

        const rect_f box { bounds() };
        if (hovered || pressed)
        {
            const SkRect background { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
            const float radius { config_.corner_radius * scale };
            const SkPaint fill { solid_paint(pressed ? colors.pressed_background : colors.hover_background) };
            context.canvas.drawRRect(SkRRect::MakeRectXY(background, radius, radius), fill);
        }

        if (context.codicon_typeface == nullptr)
            return;

        SkPaint icon_paint { solid_paint(hovered || pressed ? colors.hover_foreground : colors.foreground) };
        if (enabled() == false)
            icon_paint.setAlphaf(0.3f);
        const char32_t glyph { context.maximized && config_.maximized_glyph != 0 ? config_.maximized_glyph : config_.glyph };
        const SkFont icon_font { sk_ref_sp(context.codicon_typeface), config_.icon_size * scale };
        draw_centered_glyph(context.canvas, glyph, box, icon_font, icon_paint);
    }
} // namespace gitman::ui
