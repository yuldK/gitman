#include "presentation/ui/switch_dialog_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/dialog_elements.h"
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
        constexpr float dialog_padding { 14.0f };
        constexpr float action_button_width { 120.0f };
        constexpr float action_button_height { 28.0f };
        // panel 안 배치: 제목 아래 목록, 목록 아래 메시지 한 줄, 아래 버튼이다.
        constexpr float list_top_offset { 34.0f };

        std::u8string item_owner_value(const std::size_t index)
        {
            std::u8string value { u8"switch-item-" };
            std::size_t remaining { index };
            std::u8string digits {};
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                remaining /= 10;
            } while (remaining > 0);
            value += digits;
            return value;
        }

        char32_t candidate_glyph(const switch_candidate_kind kind) noexcept
        {
            switch (kind)
            {
            case switch_candidate_kind::git_remote_branch:
                return codicons::icon_cloud;
            case switch_candidate_kind::git_local_branch:
                return codicons::icon_git_branch;
            case switch_candidate_kind::subversion_url:
                return codicons::icon_link;
            }
            return codicons::icon_git_branch;
        }

        // 후보 한 행이다. hover·선택 강조와 종류 아이콘, stale 표시를 그린다.
        class candidate_row_element final : public ui_element
        {
        public:
            candidate_row_element(const std::size_t index, const switch_candidate& candidate, const bool selected)
                : ui_element { switch_dialog_item_id(index) }
                , candidate_ { candidate }
                , selected_ { selected }
            {
                set_action(
                    ui_trigger::left_click, [index](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { select_switch_candidate_intent { index } } } }; });
                if (candidate_.requires_tracking_branch)
                    set_tooltip(u8"local tracking branch를 만들고 전환합니다 (확인 필요)");
                else if (candidate_.stale)
                    set_tooltip(u8"이번 조회에서 갱신되지 않은 후보입니다");
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

                if (selected_)
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.positive_accent, 0.18f)));
                else if (interaction.hovered == id())
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.06f)));

                const float inset { 6.0f * scale };
                float text_left { box.x + inset };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 11.0f * scale };
                    SkPaint icon_paint { solid_paint(context.palette.primary_foreground) };
                    icon_paint.setAlphaf(0.75f);
                    draw_centered_glyph(context.canvas, candidate_glyph(candidate_.kind), { text_left, box.y, 14.0f * scale, box.height }, icon_font, icon_paint);
                    text_left += 18.0f * scale;
                }

                const SkFont font { sk_ref_sp(context.ui_typeface), 11.5f * scale };
                SkPaint foreground { solid_paint(selected_ ? context.palette.positive_accent : context.palette.primary_foreground) };
                if (candidate_.stale)
                    foreground.setAlphaf(0.6f);
                std::u8string text { candidate_.display_name };
                if (candidate_.stale)
                    text += u8" (미갱신)";
                static_cast<void>(draw_text_within(context.canvas, text, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - inset - text_left, font, foreground));
            }

        private:
            switch_candidate candidate_ {};
            bool selected_ { false };
        };

        // panel이다. 클릭을 흡수하고 제목·안내·메시지를 그린다. 목록 행과 버튼은
        // dialog가 직접 배치한다.
        class switch_panel_element final : public ui_element
        {
        public:
            explicit switch_panel_element(const switch_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::switch_dialog_panel } }
                , title_ { dialog.title }
                , loading_ { dialog.loading }
                , stale_ { dialog.stale }
                , empty_ { dialog.candidates.empty() }
                , message_ { dialog.message }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
            }

            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            // 목록 영역이다. dialog가 배치 시점에 채우고 draw가 clip에 쓴다.
            rect_f list_area {};

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 5.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.surface_background));
                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const float padding { dialog_padding * scale };
                const SkFont title_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                std::u8string heading { std::u8string { u8"전환 - " } + title_ };
                if (stale_)
                    heading += u8" (일부 후보 미갱신)";
                static_cast<void>(draw_text_within(
                    context.canvas, heading, box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font, solid_paint(context.palette.primary_foreground)));

                const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                if (loading_ || empty_)
                {
                    SkPaint dim { solid_paint(context.palette.primary_foreground) };
                    dim.setAlphaf(0.6f);
                    const std::u8string_view note { loading_ ? std::u8string_view { u8"전환 후보를 조회하는 중입니다..." } : std::u8string_view { u8"전환할 수 있는 후보가 없습니다." } };
                    draw_text(context.canvas, note, list_area.x, list_area.y + 16.0f * scale, body_font, dim);
                }

                // 목록 행을 자기 영역으로 잘라 그린다. 행은 자식이라 함께 그려진다.
                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height));
                draw_children(context, interaction);
                context.canvas.restore();

                if (message_.empty() == false)
                {
                    const float message_top { list_area.y + list_area.height + 6.0f * scale };
                    static_cast<void>(
                        draw_text_within(context.canvas, message_, box.x + padding, message_top + 10.0f * scale, box.width - padding * 2.0f, body_font, solid_paint(context.palette.warning_accent)));
                }
            }

            // 버튼은 목록 clip 밖에 그려져야 한다. 자식 순서 대신 clip 영역으로
            // 구분하면 버튼이 잘리므로, 버튼은 panel 자식이 아니라 dialog 자식이다.

        private:
            std::u8string title_ {};
            bool loading_ { false };
            bool stale_ { false };
            bool empty_ { false };
            std::u8string message_ {};
        };
    } // namespace

    ui_element_id switch_dialog_item_id(const std::size_t index)
    {
        ui_element_id id { ui_element_kind::switch_dialog_item };
        id.owner.value = item_owner_value(index);
        return id;
    }

    switch_dialog_element::switch_dialog_element(switch_dialog_view dialog)
        : ui_element { ui_element_id { ui_element_kind::switch_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 배경 클릭은 취소다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_switch_dialog_intent {} } } }; });

        auto panel { std::make_unique<switch_panel_element>(dialog_) };
        for (std::size_t index = 0; index < dialog_.candidates.size(); ++index)
        {
            // 배치에서 화면 밖 행은 숨긴다. 생성은 전부 하되 그리기·hit는 배치가
            // 거른다 (목록이 수백 행이면 카드 목록처럼 가시 범위 생성으로 바꾼다).
            const bool selected { dialog_.selected.has_value() && *dialog_.selected == index };
            panel->adopt(std::make_unique<candidate_row_element>(index, dialog_.candidates[index], selected));
        }
        panel_ = panel.get();
        add_child(std::move(panel));

        auto confirm { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::switch_dialog_confirm, dialog_.card }, dialog_.confirm_label, true) };
        confirm->set_enabled(dialog_.can_confirm);
        confirm->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { confirm_switch_intent {} } } }; });
        add_child(std::move(confirm));

        auto cancel { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::switch_dialog_cancel, dialog_.card }, std::u8string { u8"취소" }, false) };
        cancel->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_switch_dialog_intent {} } } }; });
        add_child(std::move(cancel));
    }

    void switch_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { layout_switch_dialog_width * scale };
        float height { layout_switch_dialog_height * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { dialog_padding * scale };
        auto* const panel { static_cast<switch_panel_element*>(panel_) };
        panel->list_area = { left + padding, top + list_top_offset * scale, width - padding * 2.0f, layout_switch_dialog_list_height * scale };

        // 후보 행 배치: 스크롤을 반영하고 목록 영역을 벗어난 행은 숨긴다.
        const float row_height { layout_switch_dialog_row_height * scale };
        const std::span<const std::unique_ptr<ui_element>> rows { panel_->children() };
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const float row_top { panel->list_area.y + static_cast<float>(index) * row_height - dialog_.scroll_offset * scale };
            const bool inside { row_top + row_height > panel->list_area.y && row_top < panel->list_area.y + panel->list_area.height };
            rows[index]->set_visible(inside);
            rows[index]->arrange({ { panel->list_area.x, row_top, panel->list_area.width, row_height }, scale });
        }

        // 버튼은 panel 오른쪽 아래다. dialog의 자식이라 목록 clip의 영향을 받지 않는다.
        const std::span<const std::unique_ptr<ui_element>> children { this->children() };
        if (children.size() >= 3)
        {
            const float button_width { action_button_width * scale };
            const float button_height { action_button_height * scale };
            const float button_top { top + height - padding - button_height };
            children[1]->arrange({ { left + width - padding - button_width * 2.0f - 8.0f * scale, button_top, button_width, button_height }, scale });
            children[2]->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
        }
    }

    void switch_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
