#include "presentation/ui/card_list_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/card_element.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <memory>

namespace gitman::ui {
    card_list_element::card_list_element(const view_snapshot& view)
        : ui_element { ui_element_id { ui_element_kind::card_list } }
        , total_card_count_ { view.cards.size() }
    {
        // 화면에 걸치는 카드와 위아래로 한 장씩만 자식으로 만든다. 가시 범위 계산은
        // arrange와 같은 수식을 쓰므로 배치 결과와 어긋나지 않는다. 여분의 한 장은
        // 키보드 순회가 화면 밖 카드로 넘어갈 수 있게 하는 용도이며, 보이지 않으므로
        // 그리기·hit test·drop 대상에서는 빠진다.
        const float scale { view.scale > 0.0f ? view.scale : 1.0f };
        const list_layout layout { compute_list_layout(view.window_height, scale, view.notices.empty() == false) };
        const float content { card_list_content_height(view.cards.size(), scale) };
        const float scroll { clamp_scroll_offset(view.scroll_offset * scale, content, layout.viewport_height) };
        const float card_height { layout_card_height * scale };
        const float card_gap { layout_card_gap * scale };
        const float list_bottom { layout.content_top + layout.viewport_height };

        // 화면 범위를 카드 한 장만큼 넓힌 구간이다.
        const float overscan { card_height + card_gap };
        for (std::size_t index = 0; index < view.cards.size(); ++index)
        {
            const float top { layout.content_top + card_content_top(index, scale) - scroll };
            const bool inside { top + card_height >= layout.content_top && top <= list_bottom };
            const bool near_viewport { top + card_height + overscan >= layout.content_top && top - overscan <= list_bottom };
            if (near_viewport == false)
                continue;

            visible_indices_.push_back(index);
            auto card { std::make_unique<card_element>(view.cards[index]) };
            card->set_visible(inside);
            add_child(std::move(card));
        }
    }

    void card_list_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        content_height_ = card_list_content_height(total_card_count_, scale);
        scroll_ = clamp_scroll_offset(context.scroll_offset * scale, content_height_, context.slot.height);
        const float margin { layout_margin * scale };
        const float card_height { layout_card_height * scale };
        const float card_width { context.slot.width - margin * 2.0f };

        const std::span<const std::unique_ptr<ui_element>> cards { children() };
        for (std::size_t child = 0; child < cards.size(); ++child)
        {
            const float top { context.slot.y + card_content_top(visible_indices_[child], scale) - scroll_ };
            cards[child]->arrange({ { context.slot.x + margin, top, card_width, card_height }, scale });
        }
    }

    void card_list_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };

        // 스크롤로 목록 위아래를 넘어가는 카드가 toolbar나 caption 위에 그려지지
        // 않도록 자기 영역으로 자른다.
        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
        draw_children(context, interaction);
        context.canvas.restore();

        draw_scroll_indicator(context);
    }

    void card_list_element::draw_scroll_indicator(draw_context& context) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const float scrollable { content_height_ - box.height };
        if (scrollable <= 0.0f || box.height <= 0.0f)
            return;

        const float width { layout_scrollbar_width * scale };
        const float inset { (layout_margin * scale - width) / 2.0f };
        const float track_top { box.y + inset };
        const float track_height { box.height - inset * 2.0f };
        if (track_height <= 0.0f)
            return;

        float thumb_height { track_height * (box.height / content_height_) };
        const float minimum { layout_scrollbar_minimum_thumb * scale };
        if (thumb_height < minimum)
            thumb_height = minimum > track_height ? track_height : minimum;
        const float thumb_top { track_top + (track_height - thumb_height) * (scroll_ / scrollable) };

        SkPaint thumb { solid_paint(context.palette.primary_foreground) };
        thumb.setAlphaf(0.28f);
        const SkRect shape { SkRect::MakeXYWH(box.x + box.width - inset - width, thumb_top, width, thumb_height) };
        context.canvas.drawRRect(SkRRect::MakeRectXY(shape, width * 0.5f, width * 0.5f), thumb);
    }

    const ui_element* card_list_element::hit_test(const float x, const float y) const
    {
        // 목록 밖으로 걸친 카드는 그려지지 않으므로 hit도 되지 않아야 한다.
        if (visible() == false || bounds().contains(x, y) == false)
            return nullptr;
        return ui_element::hit_test(x, y);
    }

    float card_list_element::content_height() const noexcept
    {
        return content_height_;
    }
} // namespace gitman::ui
