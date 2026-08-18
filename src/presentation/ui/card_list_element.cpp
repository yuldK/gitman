#include "presentation/ui/card_list_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/card_element.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/scrollbar_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkRect.h"

#include <algorithm>
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
        const list_layout layout { compute_list_layout(view.window_height, scale, view.notices.empty() == false, view.log.has_value()) };
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

        // 막대는 카드보다 뒤에 둔다. 위에 그려지고 hit test에서도 먼저 걸린다.
        auto scrollbar { std::make_unique<scrollbar_element>(content, layout.viewport_height, scroll, scale) };
        scrollbar->set_visible(content > layout.viewport_height);
        scrollbar_ = scrollbar.get();
        add_child(std::move(scrollbar));
    }

    void card_list_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        content_height_ = card_list_content_height(total_card_count_, scale);
        scroll_ = clamp_scroll_offset(context.scroll_offset * scale, content_height_, context.slot.height);
        const float margin { layout_margin * scale };
        const float card_height { layout_card_height * scale };
        const float hit_width { layout_scrollbar_hit_width * scale };
        const float inset { layout_scrollbar_right_inset * scale };
        // 막대가 보이면 그만큼 카드를 좁혀 클릭 영역이 겹치지 않게 한다.
        const float reserved { scrollbar_->visible() ? std::max(0.0f, hit_width + inset - margin) : 0.0f };
        const float card_width { context.slot.width - margin * 2.0f - reserved };

        const std::span<const std::unique_ptr<ui_element>> cards { children() };
        for (std::size_t child = 0; child < visible_indices_.size() && child < cards.size(); ++child)
        {
            const float top { context.slot.y + card_content_top(visible_indices_[child], scale) - scroll_ };
            cards[child]->arrange({ { context.slot.x + margin, top, card_width, card_height }, scale });
        }

        // 막대는 목록 오른쪽 안쪽에 세로로 가득 찬다. 창 가장자리의 크기 조절
        // 테두리와 겹치지 않도록 들여놓는다.
        const float track_left { context.slot.x + context.slot.width - inset - hit_width };
        const float vertical_inset { layout_scrollbar_vertical_inset * scale };
        const float track_height { std::max(0.0f, context.slot.height - vertical_inset * 2.0f) };
        scrollbar_->arrange({ { track_left, context.slot.y + vertical_inset, hit_width, track_height }, scale });
    }

    void card_list_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };

        // 스크롤로 목록 위아래를 넘어가는 카드가 toolbar나 caption 위에 그려지지
        // 않도록 자기 영역으로 자른다. 스크롤 막대도 이 안에 있다.
        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
        draw_children(context, interaction);

        // 카드가 상단 막대와 같은 색이라 스크롤되어 들어가면 경계가 사라진다.
        // 내용이 위로 밀려 있을 때만 그림자를 드리워 아래로 지나간다는 것을 보인다.
        if (scroll_ > 0.0f)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            const float height { layout_content_shadow_height * scale };
            // 막 스크롤을 시작한 구간에서는 그림자도 옅게 시작한다.
            const float ratio { scroll_ < height ? scroll_ / height : 1.0f };
            draw_downward_shadow(context.canvas, { box.x, box.y, box.width, height }, context.palette.content_shadow, layout_content_shadow_strength * ratio);
        }
        context.canvas.restore();
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
