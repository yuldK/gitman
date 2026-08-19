#include "presentation/ui/scrollbar_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <utility>

namespace gitman::ui {
    namespace {
        // 카드 목록용 기본 factory다. 함수 포인터로 두면 위임 생성자가 한 줄에 든다.
        logic_message make_card_scroll_message(const float delta)
        {
            return logic_message { scroll_intent { delta } };
        }
    } // namespace

    scrollbar_element::scrollbar_element(
        const ui_element_id id, scroll_message_factory make_message, const float content_height, const float viewport_height, const float scroll_offset, const float scale)
        : ui_element { id }
        , make_message_ { std::move(make_message) }
        , content_height_ { content_height }
        , viewport_height_ { viewport_height }
        , scroll_ { scroll_offset }
        , scale_ { scale > 0.0f ? scale : 1.0f }
    {
        // tooltip은 두지 않는다. 끄는 동안 hover가 유지되어 tooltip이 떠 버린다.
        pointer_drag_target target {};
        // 누른 지점이 thumb 밖이면 그 자리로 한 번 이동하고 이어서 끌린다. thumb
        // 안이면 잡기만 하고 움직이지 않는다.
        target.on_press = [this](const ui_action_context& context) -> std::vector<input_action> {
            if (draggable() == false || (context.y >= thumb_top_ && context.y <= thumb_top_ + thumb_height_))
                return {};
            const float delta { scroll_delta_for(context.y - (thumb_top_ + thumb_height_ * 0.5f)) };
            return { input_action { make_message_(delta) } };
        };
        target.on_move = [this](const ui_action_context& previous, const ui_action_context& current) -> std::vector<input_action> {
            if (draggable() == false)
                return {};
            const float delta { scroll_delta_for(current.y - previous.y) };
            if (delta == 0.0f)
                return {};
            return { input_action { make_message_(delta) } };
        };
        set_pointer_drag_target(std::move(target));
    }

    scrollbar_element::scrollbar_element(const float content_height, const float viewport_height, const float scroll_offset, const float scale)
        : scrollbar_element { ui_element_id { ui_element_kind::card_scrollbar }, &make_card_scroll_message, content_height, viewport_height, scroll_offset, scale }
    {}

    void scrollbar_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;

        track_top_ = context.slot.y;
        track_height_ = context.slot.height;
        const float scrollable { content_height_ - viewport_height_ };
        if (scrollable <= 0.0f || track_height_ <= 0.0f)
        {
            thumb_top_ = track_top_;
            thumb_height_ = track_height_;
            return;
        }

        thumb_height_ = track_height_ * (viewport_height_ / content_height_);
        const float minimum { layout_scrollbar_minimum_thumb * scale_ };
        if (thumb_height_ < minimum)
            thumb_height_ = minimum > track_height_ ? track_height_ : minimum;
        const float ratio { scroll_ / scrollable };
        thumb_top_ = track_top_ + (track_height_ - thumb_height_) * (ratio < 0.0f ? 0.0f : (ratio > 1.0f ? 1.0f : ratio));
    }

    bool scrollbar_element::draggable() const noexcept
    {
        return content_height_ - viewport_height_ > 0.0f && track_height_ - thumb_height_ > 0.0f;
    }

    float scrollbar_element::scroll_delta_for(const float pixels) const noexcept
    {
        if (draggable() == false)
            return 0.0f;
        const float scrollable { content_height_ - viewport_height_ };
        // thumb가 움직일 수 있는 거리와 내용이 움직일 수 있는 거리의 비율이다.
        return pixels * (scrollable / (track_height_ - thumb_height_)) / scale_;
    }

    void scrollbar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        if (thumb_height_ <= 0.0f || track_height_ <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const bool hovered { interaction.hovered == id() };
        const bool pressed { interaction.pressed == id() };

        // hit 영역은 잡기 쉽도록 넓고, 보이는 막대는 그 안에서 오른쪽에 붙인다.
        const float width { layout_scrollbar_width * scale };
        const float left { box.x + box.width - width };
        const SkRect shape { SkRect::MakeXYWH(left, thumb_top_, width, thumb_height_) };
        const float radius { width * 0.5f };

        float alpha { 0.28f };
        if (pressed)
            alpha = 0.62f;
        else if (hovered)
            alpha = 0.45f;
        const SkPaint thumb { solid_paint(with_alpha(context.palette.primary_foreground, alpha)) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(shape, radius, radius), thumb);
    }
} // namespace gitman::ui
