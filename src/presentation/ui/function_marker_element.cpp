#include "presentation/ui/function_marker_element.h"

#include "presentation/list_metrics.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <numbers>
#include <utility>

namespace gitman::ui {
    rect_f function_marker_slot(const rect_f& button, const float scale, const function_marker_shape shape) noexcept
    {
        const bool failed { shape == function_marker_shape::failed };
        const float size { (failed ? layout_function_failure_marker_size : layout_function_marker_size) * scale };
        const float inset { (failed ? layout_function_failure_marker_inset : layout_function_marker_inset) * scale };
        return { button.x + button.width - inset - size, button.y + inset, size, size };
    }

    function_marker_element::function_marker_element(ui_element_id id, const function_marker_shape shape) noexcept
        : ui_element { std::move(id) }
        , shape_ { shape }
    {
    }

    void function_marker_element::set_shape(const function_marker_shape shape) noexcept
    {
        shape_ = shape;
    }

    function_marker_shape function_marker_element::shape() const noexcept
    {
        return shape_;
    }

    void function_marker_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void function_marker_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        const rect_f box { bounds() };
        const float center_x { box.x + box.width / 2.0f };
        const float center_y { box.y + box.height / 2.0f };
        if (shape_ == function_marker_shape::running)
        {
            SkPaint paint { solid_paint(context.palette.accent) };
            paint.setAntiAlias(true);
            context.canvas.drawCircle(center_x, center_y, box.width / 2.0f, paint);
            return;
        }

        // 마름모는 자리의 폭을 대각선으로 하는 정사각형을 45도 돌린 것이다. 꼭짓점이 자리의 네 변 가운데에 닿는다.
        SkPaint paint { solid_paint(context.palette.error_accent) };
        paint.setAntiAlias(true);
        const float half_side { box.width / 2.0f / std::numbers::sqrt2_v<float> };
        context.canvas.save();
        context.canvas.rotate(45.0f, center_x, center_y);
        context.canvas.drawRect(SkRect::MakeLTRB(center_x - half_side, center_y - half_side, center_x + half_side, center_y + half_side), paint);
        context.canvas.restore();
    }
} // namespace gitman::ui
