#include "presentation/ui/dialog_elements.h"

#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <utility>

namespace gitman::ui {
    text_button_element::text_button_element(const ui_element_id id, std::u8string text, const bool accent)
        : ui_element { id }
        , text_ { std::move(text) }
        , accent_ { accent }
    {}

    void text_button_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void text_button_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { 3.0f * scale };

        ui_color background { accent_ ? with_alpha(context.palette.accent_soft, 0.25f) : with_alpha(context.palette.primary_foreground, 0.08f) };
        if (enabled() && interaction.pressed == id())
            background = context.palette.button_pressed_background;
        else if (enabled() && interaction.hovered == id())
            background = accent_ ? with_alpha(context.palette.accent_hover, 0.35f) : context.palette.button_hover_background;
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(background));

        const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
        SkPaint foreground { solid_paint(accent_ ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
        if (enabled() == false)
            foreground.setAlphaf(0.4f);
        const float text_width { measure_text(text_, font) };
        draw_text(context.canvas, text_, box.x + (box.width - text_width) / 2.0f, box.y + centered_text_baseline(font, box.height), font, foreground);
    }
} // namespace gitman::ui
