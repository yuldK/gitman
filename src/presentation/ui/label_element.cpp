#include "presentation/ui/label_element.h"

#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <utility>

namespace gitman::ui {
    namespace {
        SkPaint label_paint(const ui_color_palette& palette, const label_color_role role)
        {
            switch (role)
            {
            case label_color_role::error:
                return solid_paint(palette.error_accent);
            case label_color_role::dim: {
                SkPaint paint { solid_paint(palette.primary_foreground) };
                paint.setAlphaf(0.6f);
                return paint;
            }
            case label_color_role::primary:
                break;
            }
            return solid_paint(palette.primary_foreground);
        }
    } // namespace

    label_element::label_element(ui_element_id id, label_config config)
        : ui_element { std::move(id) }
        , config_ { std::move(config) }
    {}

    void label_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void label_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        if (config_.text.empty())
            return;
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkFont font { sk_ref_sp(context.ui_typeface), config_.font_size * scale };
        const SkPaint paint { label_paint(context.palette, config_.color) };
        const rect_f box { bounds() };

        if (config_.background == label_background_role::notice)
        {
            const SkPaint fill { solid_paint(context.palette.notice_background) };
            context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);
        }

        // 창이 좁아 slot이 줄면 글자를 잘라 옆 UI를 침범하지 않게 한다.
        const float padding { config_.padding * scale };
        const float text_width { box.width - padding * 2.0f };
        static_cast<void>(draw_text_within(context.canvas, config_.text, box.x + padding, box.y + centered_text_baseline(font, box.height), text_width, font, paint));
    }
} // namespace gitman::ui
