#include "presentation/ui/card_list_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/card_element.h"

#include <memory>

namespace gitman::ui {
    card_list_element::card_list_element(const view_snapshot& view)
        : ui_element { ui_element_id { ui_element_kind::card_list } }
        , total_card_count_ { view.cards.size() }
    {
        // 화면에 걸치는 카드만 자식으로 만든다. 가시 범위 계산은 arrange와 같은
        // 수식을 쓰므로 배치 결과와 어긋나지 않는다.
        const float scale { view.scale > 0.0f ? view.scale : 1.0f };
        const float list_top { (layout_caption_height + layout_toolbar_height) * scale };
        const float viewport_height { view.window_height - list_top };
        const float content { card_list_content_height(view.cards.size(), scale) };
        const float scroll { clamp_scroll_offset(view.scroll_offset * scale, content, viewport_height) };
        const float card_height { layout_card_height * scale };
        const float card_gap { layout_card_gap * scale };
        const float margin { layout_margin * scale };

        for (std::size_t index = 0; index < view.cards.size(); ++index)
        {
            const float top { list_top + margin + static_cast<float>(index) * (card_height + card_gap) - scroll };
            if (top + card_height < list_top || top > view.window_height)
                continue;
            visible_indices_.push_back(index);
            add_child(std::make_unique<card_element>(view.cards[index]));
        }
    }

    void card_list_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        content_height_ = card_list_content_height(total_card_count_, scale);
        const float scroll { clamp_scroll_offset(context.scroll_offset * scale, content_height_, context.slot.height) };
        const float margin { layout_margin * scale };
        const float card_height { layout_card_height * scale };
        const float card_gap { layout_card_gap * scale };
        const float card_width { context.slot.width - margin * 2.0f };

        const std::span<const std::unique_ptr<ui_element>> cards { children() };
        for (std::size_t child = 0; child < cards.size(); ++child)
        {
            const float top { context.slot.y + margin + static_cast<float>(visible_indices_[child]) * (card_height + card_gap) - scroll };
            cards[child]->arrange({ { context.slot.x + margin, top, card_width, card_height }, scale });
        }
    }

    void card_list_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    float card_list_element::content_height() const noexcept
    {
        return content_height_;
    }
} // namespace gitman::ui
