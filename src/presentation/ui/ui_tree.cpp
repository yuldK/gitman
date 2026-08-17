#include "presentation/ui/ui_tree.h"

#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <utility>

namespace gitman::ui {
    ui_tree::ui_tree(std::unique_ptr<ui_element> root)
        : root_ { std::move(root) }
    {
        if (root_ != nullptr)
            index_element(*root_);
    }

    const ui_element& ui_tree::root() const noexcept
    {
        return *root_;
    }

    const ui_element* ui_tree::hit_test(const float x, const float y) const
    {
        return root_ != nullptr ? root_->hit_test(x, y) : nullptr;
    }

    const ui_element* ui_tree::find(const ui_element_id& id) const noexcept
    {
        for (const ui_element* const element : index_)
            if (element->id() == id)
                return element;
        return nullptr;
    }

    std::vector<ui_element_id> ui_tree::ids_of_kind(const ui_element_kind kind) const
    {
        std::vector<ui_element_id> collected {};
        for (const ui_element* const element : index_)
            if (element->id().kind == kind)
                collected.push_back(element->id());
        return collected;
    }

    void ui_tree::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        if (root_ == nullptr)
            return;
        root_->draw(context, interaction);
        if (interaction.drag.has_value())
            draw_drag_visual(context, *interaction.drag);
        draw_tooltip(context, interaction);
    }

    void ui_tree::index_element(const ui_element& element)
    {
        index_.push_back(&element);
        for (const std::unique_ptr<ui_element>& child : element.children())
            index_element(*child);
    }

    void ui_tree::draw_tooltip(draw_context& context, const interaction_snapshot& interaction) const
    {
        if (interaction.hover_started_at.has_value() == false || context.now - *interaction.hover_started_at < tooltip_delay)
            return;
        const ui_element* const hovered { find(interaction.hovered) };
        if (hovered == nullptr || hovered->tooltip().empty())
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
        const std::u8string& text { hovered->tooltip() };
        const float text_width { font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8) };
        const float padding { 6.0f * scale };
        const float box_width { text_width + padding * 2.0f };
        const float box_height { 24.0f * scale };
        const rect_f window { root_->bounds() };
        const rect_f anchor { hovered->bounds() };

        // 기본은 대상 아래이고, 창을 벗어나면 위로 뒤집고 좌우는 창 안으로 민다.
        float box_x { anchor.x };
        float box_y { anchor.y + anchor.height + 4.0f * scale };
        if (box_y + box_height > window.y + window.height)
            box_y = anchor.y - box_height - 4.0f * scale;
        box_x = std::clamp(box_x, window.x, std::max(window.x, window.x + window.width - box_width));

        const SkRect box { SkRect::MakeXYWH(box_x, box_y, box_width, box_height) };
        const SkPaint background { solid_paint(context.palette.tooltip_background) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(box, 3.0f * scale, 3.0f * scale), background);
        SkPaint border { solid_paint(context.palette.tooltip_border) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(box, 3.0f * scale, 3.0f * scale), border);
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        draw_text(context.canvas, text, box_x + padding, box_y + centered_text_baseline(font, box_height), font, foreground);
    }

    void ui_tree::draw_drag_visual(draw_context& context, const drag_visual& drag) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };

        // 수락 중인 drop 대상을 먼저 강조한다.
        if (const ui_element* const target { find(drag.hovered_drop_target) }; target != nullptr)
        {
            const rect_f target_bounds { target->bounds() };
            SkPaint highlight { solid_paint(context.palette.positive_accent) };
            highlight.setStyle(SkPaint::kStroke_Style);
            highlight.setStrokeWidth(2.0f * scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(target_bounds.x, target_bounds.y, target_bounds.width, target_bounds.height), 4.0f * scale, 4.0f * scale), highlight);
        }

        // 포인터를 따라다니는 ghost다. 원본 element의 축소 표시로 충분하다.
        const SkRect ghost { SkRect::MakeXYWH(drag.x + 12.0f * scale, drag.y + 12.0f * scale, 120.0f * scale, 28.0f * scale) };
        SkPaint fill { solid_paint(context.palette.surface_background) };
        fill.setAlphaf(0.85f);
        context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, 4.0f * scale, 4.0f * scale), fill);
        SkPaint border { solid_paint(context.palette.positive_accent) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.5f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, 4.0f * scale, 4.0f * scale), border);

        const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        draw_text(context.canvas, drag.payload.dragged_project.value, ghost.left() + 8.0f * scale, ghost.top() + centered_text_baseline(font, ghost.height()), font, foreground);
    }
} // namespace gitman::ui
