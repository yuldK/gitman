#include "presentation/ui/card_list_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/card_element.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/scrollbar_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <memory>
#include <string>

namespace gitman::ui {
    card_list_element::card_list_element(const view_snapshot& view)
        : ui_element { ui_element_id { ui_element_kind::card_list } }
        , total_card_count_ { view.cards.size() }
    {
        card_ids_.reserve(view.cards.size());
        for (const card_view_model& card : view.cards)
            card_ids_.push_back(card.id);

        // 목록이 hit 대상이 되므로 빈 영역 클릭의 선택 해제를 root처럼 등록한다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { select_card_intent {} } } }; });

        // 카드 순서 변경의 drop 대상이다. 놓는 위치는 카드가 아니라 목록 좌표가
        // 정하므로 카드 사이 여백에 놓아도 동작한다. 삽입 위치 계산은 여백을 벌려
        // 그리는 쪽과 같은 함수를 쓴다 (field-feedback-design 4.1).
        drop_target target {};
        target.accepts = [](const drag_payload& payload) { return payload.source.kind == ui_element_kind::card_body; };
        target.on_drop = [this](const drag_payload& payload, const ui_action_context& context) -> std::vector<input_action> {
            std::size_t dragged { card_ids_.size() };
            for (std::size_t index = 0; index < card_ids_.size(); ++index)
                if (card_ids_[index] == payload.dragged_project)
                    dragged = index;
            if (dragged == card_ids_.size() || card_ids_.size() < 2)
                return {};

            const std::size_t slot { card_drag_insertion_slot(context.y, bounds().y, scroll_, dragged, card_ids_.size(), scale_) };
            // 제자리에 놓으면 아무 일도 없다.
            if (slot == dragged)
                return {};

            // slot은 dragged를 뺀 목록 기준이다. 그 목록의 index를 원래 목록으로
            // 되돌려 앞/뒤 카드를 찾는다.
            reorder_card_intent intent {};
            intent.id = payload.dragged_project;
            if (slot == 0)
            {
                intent.target = card_ids_[dragged == 0 ? 1 : 0];
                intent.place_after = false;
            }
            else
            {
                const std::size_t remaining_before { slot - 1 };
                intent.target = card_ids_[remaining_before + (remaining_before >= dragged ? 1u : 0u)];
                intent.place_after = true;
            }
            return { input_action { logic_message { intent } } };
        };
        set_drop_target(std::move(target));

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
        scale_ = scale;

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
        const std::optional<drag_view> drag { derive_drag_view(interaction) };

        // 스크롤로 목록 위아래를 넘어가는 카드가 toolbar나 caption 위에 그려지지
        // 않도록 자기 영역으로 자른다. 스크롤 막대도 이 안에 있다.
        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
        if (drag.has_value() == false)
            draw_children(context, interaction);
        else
        {
            // drag 중에는 카드가 원래 slot에서 빠지고 삽입 위치의 여백이 벌어진다.
            // 배치와 hit test는 그대로 두고 그리기 offset만 옮긴다 (ADR-004의 불변
            // tree 유지, field-feedback-design 4.1).
            const std::span<const std::unique_ptr<ui_element>> elements { children() };
            for (std::size_t child = 0; child < visible_indices_.size() && child < elements.size(); ++child)
            {
                const ui_element& card { *elements[child] };
                const std::size_t index { visible_indices_[child] };
                if (card.visible() == false || index == drag->dragged_index)
                    continue;

                const float offset { card_drag_offset(index, drag->dragged_index, drag->insertion_slot, scale_) };
                if (offset == 0.0f)
                {
                    card.draw(context, interaction);
                    continue;
                }
                context.canvas.save();
                context.canvas.translate(0.0f, offset);
                card.draw(context, interaction);
                context.canvas.restore();
            }
            if (scrollbar_->visible())
                scrollbar_->draw(context, interaction);
        }

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

        // 떠 있는 카드는 목록 clip 밖(로그 pane·toolbar 위)으로도 따라간다.
        if (drag.has_value())
            draw_lifted_card(context, interaction, *drag);
    }

    std::optional<card_list_element::drag_view> card_list_element::derive_drag_view(const interaction_snapshot& interaction) const
    {
        if (interaction.drag.has_value() == false || interaction.drag->payload.source.kind != ui_element_kind::card_body)
            return std::nullopt;

        std::size_t dragged { card_ids_.size() };
        for (std::size_t index = 0; index < card_ids_.size(); ++index)
            if (card_ids_[index] == interaction.drag->payload.dragged_project)
                dragged = index;
        if (dragged == card_ids_.size())
            return std::nullopt;

        drag_view view {};
        view.dragged_index = dragged;
        // 포인터가 목록 밖이면 여백은 원래 자리에 남는다. dragged를 뺀 목록에서
        // 원래 위치의 slot 값은 원래 index와 같다.
        view.insertion_slot = bounds().contains(interaction.drag->x, interaction.drag->y)
            ? card_drag_insertion_slot(interaction.drag->y, bounds().y, scroll_, dragged, card_ids_.size(), scale_)
            : dragged;

        const std::span<const std::unique_ptr<ui_element>> elements { children() };
        for (std::size_t child = 0; child < visible_indices_.size() && child < elements.size(); ++child)
            if (visible_indices_[child] == dragged)
                view.dragged_child = elements[child].get();
        return view;
    }

    void card_list_element::draw_lifted_card(draw_context& context, const interaction_snapshot& interaction, const drag_view& drag) const
    {
        const drag_visual& visual { *interaction.drag };
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };

        // drag 중 스크롤로 카드 element가 tree에서 빠진 경우만 소형 ghost로
        // 대체한다. 평소에는 카드 전체가 잡은 지점 그대로 따라온다.
        if (drag.dragged_child == nullptr)
        {
            const SkRect ghost { SkRect::MakeXYWH(visual.x + 10.0f * scale, visual.y + 10.0f * scale, 112.0f * scale, 24.0f * scale) };
            SkPaint fill { solid_paint(context.palette.surface_background) };
            fill.setAlphaf(0.85f);
            context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, 3.0f * scale, 3.0f * scale), fill);
            SkPaint outline { solid_paint(context.palette.positive_accent) };
            outline.setStyle(SkPaint::kStroke_Style);
            outline.setStrokeWidth(1.0f * scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(ghost, 3.0f * scale, 3.0f * scale), outline);
            const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
            const std::u8string& text { visual.payload.label.empty() ? visual.payload.dragged_project.value : visual.payload.label };
            draw_text(context.canvas, text, ghost.left() + 7.0f * scale, ghost.top() + centered_text_baseline(font, ghost.height()), font, solid_paint(context.palette.primary_foreground));
            return;
        }

        const rect_f card { drag.dragged_child->bounds() };
        const float dx { visual.x - visual.payload.grab_offset_x - card.x };
        const float dy { visual.y - visual.payload.grab_offset_y - card.y };
        context.canvas.save();
        context.canvas.translate(dx, dy);
        drag.dragged_child->draw(context, interaction);
        SkPaint border { solid_paint(context.palette.positive_accent) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(card.x, card.y, card.width, card.height), 3.0f * scale, 3.0f * scale), border);
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
