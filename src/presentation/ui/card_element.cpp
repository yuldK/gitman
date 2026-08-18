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
#include <vector>

namespace gitman::ui {
    namespace {
        // 상태 줄의 조각 하나다. 배경이 0이면 배경 없이 글자만 그린다.
        struct card_chip
        {
            char32_t glyph { 0 };
            std::u8string text {};
            ui_color background { 0 };
            ui_color foreground { 0 };
        };

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

        // 카드 body는 순서 변경의 drag 출발지이자 도착지다. 제외된 카드도 문서
        // 순서는 옮길 수 있다. lambda는 element가 소유하고 tree는 게시 후 불변이라
        // this 캡처가 안전하다.
        drag_source source {};
        source.make_payload = [this](const ui_action_context& context) {
            drag_payload payload {};
            payload.source = context.element;
            payload.dragged_project = card_.id;
            payload.label = card_.display_name;
            return payload;
        };
        set_drag_source(std::move(source));

        drop_target target {};
        target.accepts = [this](const drag_payload& payload) { return payload.source.kind == ui_element_kind::card_body && (payload.dragged_project == card_.id) == false; };
        target.on_drop = [this](const drag_payload& payload, const ui_action_context& context) -> std::vector<input_action> {
            // 대상의 위쪽 절반에 놓으면 앞으로, 아래쪽 절반이면 뒤로 삽입한다.
            reorder_card_intent intent {};
            intent.id = payload.dragged_project;
            intent.target = card_.id;
            intent.place_after = context.y >= bounds().y + bounds().height * 0.5f;
            return { input_action { logic_message { intent } } };
        };
        set_drop_target(std::move(target));

        auto refresh { std::make_unique<button_element>(ui_element_id { ui_element_kind::card_refresh, card_.id }, button_config { .glyph = codicons::icon_refresh }) };
        refresh->set_tooltip(u8"이 카드 새로 고침");
        refresh->set_action(
            ui_trigger::left_click, [id = card_.id](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { refresh_card_intent { id } } } }; });
        refresh_ = refresh.get();
        add_child(std::move(refresh));

        // update 버튼: 실행 중에는 그 작업의 중지 버튼이 된다 (stage-7-plan 4.4).
        button_config update_config {};
        update_config.glyph = card_.change_running ? codicons::icon_stop_circle : codicons::icon_repo_pull;
        auto update { std::make_unique<button_element>(ui_element_id { ui_element_kind::card_update, card_.id }, update_config) };
        if (card_.change_running)
        {
            update->set_tooltip(u8"실행 중인 작업 취소");
            update->set_action(
                ui_trigger::left_click, [id = card_.id](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_operation_intent { id } } } }; });
        }
        else if (card_.can_change)
        {
            if (card_.kind == repository_kind::subversion)
            {
                // SVN은 option이 없어 확인 없이 곧바로 실행한다.
                update->set_tooltip(u8"업데이트 (svn update)");
                update->set_action(
                    ui_trigger::left_click, [id = card_.id](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { request_update_intent { id, {} } } } }; });
            }
            else
            {
                update->set_tooltip(u8"업데이트 (git pull --ff-only)");
                update->set_action(ui_trigger::left_click,
                    [id = card_.id](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { show_update_options_intent { id } } } }; });
            }
        }
        else
        {
            update->set_tooltip(u8"카드가 준비된 뒤 업데이트할 수 있습니다");
            update->set_enabled(false);
        }
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

        // 카드 오른쪽 끝에 버튼 3개를 세로 중앙 정렬로 둔다. 창이 좁아 글자가 들어갈
        // 최소 폭도 남지 않으면 오른쪽 버튼부터 숨긴다.
        const float margin { layout_margin * scale };
        const float gap { layout_card_gap * scale };
        const float button { layout_button_size * scale };
        const float buttons_y { context.slot.y + (context.slot.height - button) / 2.0f };
        const float minimum_right { context.slot.x + (layout_card_text_left + layout_card_minimum_text) * scale };

        float next_x { context.slot.x + context.slot.width - margin - button };
        const auto place = [&](ui_element* const element) {
            if (next_x < minimum_right)
            {
                element->set_visible(false);
                return;
            }
            element->set_visible(true);
            element->arrange({ { next_x, buttons_y, button, button }, scale });
            next_x -= button + gap;
        };

        place(switch_);
        place(update_);
        place(refresh_);
        text_limit_ = next_x + button;
    }

    void card_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

        SkPaint card_paint { solid_paint(context.palette.surface_background) };
        if (card_.enabled == false)
            card_paint.setAlphaf(0.5f);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), card_paint);
        if (card_.selected)
        {
            SkPaint border { solid_paint(context.palette.positive_accent) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(1.0f * scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), border);
        }

        const SkFont title_font { sk_ref_sp(context.ui_typeface), 14.0f * scale };
        const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        SkPaint dim_foreground { solid_paint(context.palette.primary_foreground) };
        dim_foreground.setAlphaf(0.6f);

        const float padding { 8.0f * scale };
        const float text_x { box.x + layout_card_text_left * scale };
        const float line_1 { box.y + padding + 11.0f * scale };
        const float line_2 { line_1 + 16.0f * scale };
        const float line_3_top { line_2 + 5.0f * scale };

        // 상태 아이콘: 진행 중이면 sync, 아니면 상태 글리프다.
        if (context.codicon_typeface != nullptr)
        {
            const rect_f icon_slot { box.x + padding, box.y + padding, 16.0f * scale, 16.0f * scale };
            const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 14.0f * scale };
            const SkPaint icon_paint { solid_paint(state_accent(context.palette, card_.state)) };
            const char32_t glyph { card_.busy ? codicons::icon_sync : codicon_for_name(card_.status.codicon) };
            draw_centered_glyph(context.canvas, glyph, icon_slot, icon_font, icon_paint);
        }

        // 글자가 버튼을 침범하지 않도록 arrange가 정한 한계 안에서만 그린다.
        const float text_width { text_limit_ - text_x };
        static_cast<void>(draw_text_within(context.canvas, card_.display_name, text_x, line_1, text_width, title_font, foreground));
        static_cast<void>(draw_text_within(context.canvas, card_.path, text_x, line_2, text_width, body_font, dim_foreground));

        // 세 번째 줄은 조각(chip)으로 그린다. 브랜치·리비전·상태·작업 트리를 같은
        // 폰트의 한 줄로 이어 붙이면 구분이 되지 않는다는 지적을 반영한 것이다.
        draw_status_row(context, { text_x, line_3_top, text_width, layout_card_status_height * scale });

        draw_children(context, interaction);
    }

    void card_element::draw_status_row(draw_context& context, const rect_f& row) const
    {
        if (row.width <= 0.0f || row.height <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const ui_color accent { state_accent(context.palette, card_.state) };
        const ui_color neutral { context.palette.primary_foreground };

        std::vector<card_chip> chips {};
        if (card_.reference.empty() == false)
        {
            // SVN의 참조는 URL이라 branch 아이콘 대신 link 아이콘을 쓴다.
            const char32_t glyph { card_.kind == repository_kind::subversion ? codicons::icon_link : codicons::icon_git_branch };
            chips.push_back({ glyph, card_.reference, with_alpha(neutral, 0.12f), neutral });
        }
        if (card_.revision.empty() == false)
            chips.push_back({ codicons::icon_git_commit, std::u8string { revision_display_text(card_.kind, card_.revision) }, with_alpha(neutral, 0.12f), with_alpha(neutral, 0.75f) });
        if (card_.status.tooltip.empty() == false)
            chips.push_back({ codicon_for_name(card_.status.codicon), card_.status.tooltip, with_alpha(accent, 0.18f), accent });
        if (card_.working_tree_text.empty() == false)
        {
            const ui_color tree_color { context.palette.warning_accent };
            chips.push_back({ codicons::icon_edit, card_.working_tree_text, with_alpha(tree_color, 0.18f), tree_color });
        }

        const SkFont chip_font { sk_ref_sp(context.ui_typeface), 10.5f * scale };
        const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 10.0f * scale };
        const float padding { 5.0f * scale };
        const float icon_width { context.codicon_typeface != nullptr ? 11.0f * scale : 0.0f };
        const float gap { 4.0f * scale };
        const float radius { 3.0f * scale };

        float left { row.x };
        const float limit { row.x + row.width };
        for (const card_chip& chip : chips)
        {
            const float fixed { padding * 2.0f + (icon_width > 0.0f && chip.glyph != 0 ? icon_width + gap * 0.5f : 0.0f) };
            const float available { limit - left - fixed };
            // 글자가 한 자도 들어가지 않는 조각은 그리지 않는다. 뒤 조각도 마찬가지다.
            const std::u8string text { elide_text(chip.text, available, chip_font) };
            if (text.empty())
                break;

            const float text_width { measure_text(text, chip_font) };
            const float width { text_width + fixed };
            const SkRect box { SkRect::MakeXYWH(left, row.y, width, row.height) };
            if (chip.background != 0)
                context.canvas.drawRRect(SkRRect::MakeRectXY(box, radius, radius), solid_paint(chip.background));

            const SkPaint foreground { solid_paint(chip.foreground) };
            float text_left { left + padding };
            if (icon_width > 0.0f && chip.glyph != 0)
            {
                const rect_f icon_slot { left + padding, row.y, icon_width, row.height };
                draw_centered_glyph(context.canvas, chip.glyph, icon_slot, icon_font, foreground);
                text_left += icon_width + gap * 0.5f;
            }
            draw_text(context.canvas, text, text_left, row.y + centered_text_baseline(chip_font, row.height), chip_font, foreground);

            left += width + gap;
            if (left >= limit)
                break;
        }
    }
} // namespace gitman::ui
