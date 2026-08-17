#include "presentation/ui/card_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/button_element.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        ui_color state_accent(const ui_color_palette& palette, const card_view_state state) noexcept
        {
            switch (state)
            {
            case card_view_state::failed:
                return palette.error_accent;
            case card_view_state::warning:
                return palette.warning_accent;
            case card_view_state::ready:
                return palette.positive_accent;
            case card_view_state::loading:
            case card_view_state::running:
            case card_view_state::disabled:
                break;
            }
            return palette.primary_foreground;
        }
    } // namespace

    card_element::card_element(card_view_model card)
        : ui_element { ui_element_id { ui_element_kind::card_body, card.id } }
        , card_ { std::move(card) }
    {
        // 제외된 카드도 선택은 가능해야 상태를 확인할 수 있으므로 body는 항상
        // 활성이다. 흐림 표시는 draw가 card_.enabled로 판정한다.
        set_action(ui_trigger::left_click, [id = card_.id](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { select_card_intent { { id } } } } }; });

        auto refresh { std::make_unique<button_element>(ui_element_id { ui_element_kind::card_refresh, card_.id }, button_config { .glyph = codicons::icon_refresh }) };
        refresh->set_tooltip(u8"이 카드 새로 고침");
        refresh->set_action(
            ui_trigger::left_click, [id = card_.id](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { refresh_card_intent { id } } } }; });
        refresh_ = refresh.get();
        add_child(std::move(refresh));

        auto update { std::make_unique<button_element>(ui_element_id { ui_element_kind::card_update, card_.id }, button_config { .glyph = codicons::icon_repo_pull }) };
        update->set_tooltip(u8"업데이트는 단계 7에서 활성화됩니다");
        update->set_enabled(false);
        update_ = update.get();
        add_child(std::move(update));

        auto switch_button { std::make_unique<button_element>(ui_element_id { ui_element_kind::card_switch, card_.id }, button_config { .glyph = codicons::icon_source_control }) };
        switch_button->set_tooltip(u8"전환은 단계 7에서 활성화됩니다");
        switch_button->set_enabled(false);
        switch_ = switch_button.get();
        add_child(std::move(switch_button));
    }

    void card_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        // 카드 오른쪽 끝에 버튼 3개를 세로 중앙 정렬로 둔다.
        const float margin { layout_margin * scale };
        const float gap { layout_card_gap * scale };
        const float button { layout_button_size * scale };
        const float buttons_y { context.slot.y + (context.slot.height - button) / 2.0f };
        float button_x { context.slot.x + context.slot.width - margin - button };
        switch_->arrange({ { button_x, buttons_y, button, button }, scale });
        button_x -= button + gap;
        update_->arrange({ { button_x, buttons_y, button, button }, scale });
        button_x -= button + gap;
        refresh_->arrange({ { button_x, buttons_y, button, button }, scale });
    }

    void card_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

        SkPaint card_paint { solid_paint(context.palette.surface_background) };
        if (card_.enabled == false)
            card_paint.setAlphaf(0.5f);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, 6.0f * scale, 6.0f * scale), card_paint);
        if (card_.selected)
        {
            SkPaint border { solid_paint(context.palette.positive_accent) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(2.0f * scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, 6.0f * scale, 6.0f * scale), border);
        }

        const SkFont title_font { sk_ref_sp(context.ui_typeface), 15.0f * scale };
        const SkFont body_font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        SkPaint dim_foreground { solid_paint(context.palette.primary_foreground) };
        dim_foreground.setAlphaf(0.6f);

        const float padding { 10.0f * scale };
        const float text_x { box.x + 32.0f * scale };
        const float line_1 { box.y + padding + 12.0f * scale };
        const float line_2 { line_1 + 18.0f * scale };
        const float line_3 { line_2 + 16.0f * scale };

        // 상태 아이콘: 진행 중이면 sync, 아니면 상태 글리프다.
        if (context.codicon_typeface != nullptr)
        {
            const rect_f icon_slot { box.x + padding, box.y + padding, 18.0f * scale, 18.0f * scale };
            const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 16.0f * scale };
            const SkPaint icon_paint { solid_paint(state_accent(context.palette, card_.state)) };
            const char32_t glyph { card_.busy ? codicons::icon_sync : codicon_for_name(card_.status.codicon) };
            draw_centered_glyph(context.canvas, glyph, icon_slot, icon_font, icon_paint);
        }

        draw_text(context.canvas, card_.display_name, text_x, line_1, title_font, foreground);
        draw_text(context.canvas, card_.path, text_x, line_2, body_font, dim_foreground);

        // 세 번째 줄: 참조(브랜치/URL), 리비전, 상태 툴팁과 작업 트리 요약.
        std::u8string detail {};
        if (card_.reference.empty() == false)
            detail.append(card_.reference);
        if (card_.revision.empty() == false)
        {
            if (detail.empty() == false)
                detail.append(u8" @ ");
            detail.append(card_.revision);
        }
        if (card_.status.tooltip.empty() == false)
        {
            if (detail.empty() == false)
                detail.append(u8" · ");
            detail.append(card_.status.tooltip);
        }
        if (card_.working_tree_text.empty() == false)
        {
            if (detail.empty() == false)
                detail.append(u8" · ");
            detail.append(card_.working_tree_text);
        }
        draw_text(context.canvas, detail, text_x, line_3, body_font, dim_foreground);

        draw_children(context, interaction);
    }
} // namespace gitman::ui
