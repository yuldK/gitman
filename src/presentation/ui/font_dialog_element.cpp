#include "presentation/ui/font_dialog_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/button_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/scrollbar_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace gitman::ui {
    namespace {
        constexpr float dialog_padding { 14.0f };
        constexpr float action_button_width { 96.0f };
        constexpr float action_button_height { 28.0f };
        constexpr float query_top { 32.0f };
        constexpr float query_height { 26.0f };
        constexpr float query_gap { 6.0f };
        constexpr float list_top { 66.0f };
        constexpr float preview_gap { 8.0f };

        std::u8string item_owner_value(const std::size_t index)
        {
            std::u8string value { u8"font-item-" };
            std::u8string digits {};
            std::size_t remaining { index };
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                remaining /= 10;
            } while (remaining > 0);
            value += digits;
            return value;
        }

        // 그 가족의 typeface다. 해석기가 없거나 찾지 못하면 UI 글꼴로 그린다 — 이름은
        // 언제나 읽을 수 있어야 한다.
        SkTypeface* family_or_ui(const draw_context& context, const std::u8string_view family)
        {
            if (context.fonts == nullptr)
                return context.ui_typeface;
            SkTypeface* const resolved { context.fonts->family(family) };
            return resolved != nullptr ? resolved : context.ui_typeface;
        }

        // 글꼴 한 줄이다. 이름을 그 글꼴로 그려 목록 자체가 미리 보기가 된다.
        class font_row_element final : public ui_element
        {
        public:
            font_row_element(const std::size_t index, std::u8string name, std::u8string family, const bool selected)
                : ui_element { font_dialog_item_id(index) }
                , name_ { std::move(name) }
                , family_ { std::move(family) }
                , selected_ { selected }
            {
                set_action(ui_trigger::left_click, make_logic_click(select_font_family_intent { index }));
                set_action(ui_trigger::double_click, [index](const ui_action_context&) -> std::vector<input_action> {
                    return { make_logic_action(select_font_family_intent { index }), make_logic_action(confirm_font_dialog_intent {}) };
                });
                // 목록 행은 Tab 자리가 아니다 (설계 D1-B).
                set_tab_stop(false);
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
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.accent_soft, 0.18f)));
                else if (interaction.hovered == id())
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.06f)));

                const float inset { 8.0f * scale };
                const SkFont font { sk_ref_sp(family_or_ui(context, family_)), 12.5f * scale };
                const SkPaint foreground { solid_paint(selected_ ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
                static_cast<void>(draw_text_within(context.canvas, name_, box.x + inset, box.y + centered_text_baseline(font, box.height), box.width - inset * 2.0f, font, foreground));
            }

        private:
            std::u8string name_ {};
            std::u8string family_ {};
            bool selected_ { false };
        };

        class font_panel_element final : public ui_element
        {
        public:
            explicit font_panel_element(const font_dialog_view& dialog)
                : ui_element { ui_element_id { kind_font_dialog_panel } }
                , title_ { dialog.title }
                , list_note_ { dialog.list_note }
                , preview_ { dialog.preview }
                , preview_family_ { dialog.preview_family }
            {
                // 배경 클릭이 뒤의 scrim(취소)으로 새지 않게 흡수만 한다.
                set_hit_opaque(true);
            }

            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            rect_f list_area {};
            rect_f preview_area {};

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
                static_cast<void>(draw_text_within(
                    context.canvas, title_, box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font, solid_paint(context.palette.primary_foreground)));

                if (list_note_.empty() == false)
                {
                    const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                    SkPaint dim { solid_paint(context.palette.primary_foreground) };
                    dim.setAlphaf(0.6f);
                    static_cast<void>(draw_text_within(context.canvas, list_note_, list_area.x + 4.0f * scale, list_area.y + 16.0f * scale, list_area.width - 8.0f * scale, body_font, dim));
                }

                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height));
                draw_children(context, interaction);
                context.canvas.restore();

                SkPaint outline { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                outline.setStyle(SkPaint::kStroke_Style);
                outline.setStrokeWidth(1.0f * scale);
                context.canvas.drawRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height), outline);

                // 미리 보기는 고른 글꼴로 예문 한 줄이다. 목록 행보다 크게 그려
                // 자획을 확인할 수 있다.
                context.canvas.drawRect(SkRect::MakeXYWH(preview_area.x, preview_area.y, preview_area.width, preview_area.height), outline);
                const SkFont preview_font { sk_ref_sp(family_or_ui(context, preview_family_)), 14.0f * scale };
                static_cast<void>(draw_text_within(context.canvas, preview_, preview_area.x + 8.0f * scale, preview_area.y + centered_text_baseline(preview_font, preview_area.height),
                    preview_area.width - 16.0f * scale, preview_font, solid_paint(context.palette.primary_foreground)));
            }

            // 목록 영역 밖에 걸친 행은 눌리지 않는다. 자르는 경계가 panel bounds가
            // 아니라 `list_area`라 `set_clip_children`으로는 표현할 수 없어 재정의로
            // 남긴다 (판정 나머지는 기본 구현과 같다).
            const ui_element* hit_test(const float x, const float y) const override
            {
                if (visible() == false)
                    return nullptr;
                if (list_area.contains(x, y))
                {
                    const std::span<const std::unique_ptr<ui_element>> rows { children() };
                    for (std::size_t index = rows.size(); index > 0; --index)
                        if (const ui_element* const hit { rows[index - 1]->hit_test(x, y) }; hit != nullptr)
                            return hit;
                }
                if ((interactive() || hit_opaque()) && bounds().contains(x, y))
                    return this;
                return nullptr;
            }

        private:
            std::u8string title_ {};
            std::u8string list_note_ {};
            std::u8string preview_ {};
            std::u8string preview_family_ {};
        };
    } // namespace

    ui_element_id font_dialog_item_id(const std::size_t index)
    {
        return { kind_font_dialog_item, item_owner_value(index) };
    }

    font_dialog_element::font_dialog_element(const font_dialog_view& dialog, const float)
        : ui_element { ui_element_id { kind_font_dialog } }
        , dialog_ { dialog }
    {
        set_action(ui_trigger::left_click, make_logic_click(cancel_font_dialog_intent {}));
        // scrim은 마우스만의 자리다. 키보드 취소는 Esc다 (설계 D1-B).
        set_tab_stop(false);

        auto panel { std::make_unique<font_panel_element>(dialog_) };
        for (std::size_t index = 0; index < dialog_.names.size(); ++index)
        {
            const bool selected { dialog_.selected.has_value() && *dialog_.selected == index };
            panel->adopt(std::make_unique<font_row_element>(index, dialog_.names[index], index < dialog_.families.size() ? dialog_.families[index] : std::u8string {}, selected));
        }
        panel_ = panel.get();
        add_child(std::move(panel));

        // 질의 막대는 예전처럼 텍스트 칸과 버튼이 형제다. luil 텍스트 칸의 지우기
        // 버튼은 글리프가 고정이라 search/close 두 모양을 낼 수 없다 (설계 L5).
        auto input {
            std::make_unique<luil::text_input_element>(
                ui_element_id { kind_font_dialog_query_input }, dialog_.query, luil::text_input_config { .placeholder = dialog_.query_placeholder }),
        };
        input->set_tooltip(u8"이름의 일부를 입력해 글꼴을 거릅니다");
        query_input_ = input.get();
        add_child(std::move(input));

        auto clear {
            std::make_unique<luil::button_element>(ui_element_id { kind_font_dialog_query_clear },
                luil::button_config { .glyph = dialog_.query_action_enabled ? codicons::icon_close : codicons::icon_search, .icon_size = 13.0f }),
        };
        clear->set_enabled(dialog_.query_action_enabled);
        clear->set_tooltip(u8"필터를 지웁니다");
        clear->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> {
            edit_text_input_intent intent {};
            intent.target = text_input_target::font_filter;
            intent.command = text_edit_command::clear;
            return { make_logic_action(std::move(intent)) };
        });
        query_clear_ = clear.get();
        add_child(std::move(clear));

        auto confirm {
            std::make_unique<luil::text_button_element>(
                ui_element_id { kind_font_dialog_confirm }, luil::text_button_config { .text = std::u8string { u8"선택" }, .visual = luil::text_button_visual::accent }),
        };
        confirm->set_enabled(dialog_.can_confirm);
        confirm->set_action(ui_trigger::left_click, make_logic_click(confirm_font_dialog_intent {}));
        confirm_ = confirm.get();
        add_child(std::move(confirm));

        auto cancel {
            std::make_unique<luil::text_button_element>(
                ui_element_id { kind_font_dialog_cancel }, luil::text_button_config { .text = std::u8string { u8"취소" }, .visual = luil::text_button_visual::normal }),
        };
        cancel->set_action(ui_trigger::left_click, make_logic_click(cancel_font_dialog_intent {}));
        cancel_ = cancel.get();
        add_child(std::move(cancel));

        // 막대에 넘기는 치수와 스크롤 값은 전부 논리 픽셀이다 (설계 L4).
        const float bar_content { static_cast<float>(dialog_.names.size()) * layout_font_dialog_row_height };
        const float bar_viewport { layout_font_dialog_list_height };
        auto bar {
            std::make_unique<luil::scrollbar_element>(ui_element_id { kind_scrollbar, std::u8string { scrollbar_owner_font_dialog } },
                luil::scrollbar_config {
                    .scroll = [](const float delta) -> input_action { return make_logic_action(font_dialog_scroll_intent { delta }); },
                    .content_height = bar_content,
                    .viewport_height = bar_viewport,
                    .scroll_offset = dialog_.scroll_offset,
                }),
        };
        // 목록에 딸린 막대는 Tab 자리가 아니다 (luil list_element와 같은 관례, D1-B).
        bar->set_tab_stop(false);
        bar->set_visible(bar_content > bar_viewport);
        scrollbar_ = bar.get();
        add_child(std::move(bar));
    }

    void font_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { layout_font_dialog_width * scale };
        float height { layout_font_dialog_height * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { dialog_padding * scale };
        auto* const panel { static_cast<font_panel_element*>(panel_) };
        panel->list_area = { left + padding, top + list_top * scale, width - padding * 2.0f, layout_font_dialog_list_height * scale };
        panel->preview_area = { panel->list_area.x, panel->list_area.y + panel->list_area.height + preview_gap * scale, panel->list_area.width, layout_font_dialog_preview_height * scale };

        const float bar_top { top + query_top * scale };
        const float bar_height { query_height * scale };
        const float bar_left { left + padding };
        const float bar_width { width - padding * 2.0f };
        query_clear_->arrange({ { bar_left + bar_width - bar_height, bar_top, bar_height, bar_height }, scale });
        float input_width { bar_width - bar_height - query_gap * scale };
        if (input_width < 0.0f)
            input_width = 0.0f;
        query_input_->arrange({ { bar_left, bar_top, input_width, bar_height }, scale });

        const float row_height { layout_font_dialog_row_height * scale };
        const float scrollbar_margin { 1.0f * scale };
        const float scrollbar_reserved { scrollbar_->visible() ? layout_scrollbar_hit_width * scale + scrollbar_margin : 0.0f };
        const std::span<const std::unique_ptr<ui_element>> rows { panel_->children() };
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const float row_top { panel->list_area.y + static_cast<float>(index) * row_height - dialog_.scroll_offset * scale };
            const bool inside { row_top + row_height > panel->list_area.y && row_top < panel->list_area.y + panel->list_area.height };
            rows[index]->set_visible(inside);
            rows[index]->arrange({ { panel->list_area.x, row_top, panel->list_area.width - scrollbar_reserved, row_height }, scale });
        }

        const float bar_track_width { layout_scrollbar_hit_width * scale };
        const float bar_track_left { panel->list_area.x + panel->list_area.width - scrollbar_margin - bar_track_width };
        const float bar_track_height { panel->list_area.height - scrollbar_margin * 2.0f };
        scrollbar_->arrange({ { bar_track_left, panel->list_area.y + scrollbar_margin, bar_track_width, bar_track_height }, scale });

        const float button_width { action_button_width * scale };
        const float button_height { action_button_height * scale };
        const float button_top { top + height - padding - button_height };
        confirm_->arrange({ { left + width - padding - button_width * 2.0f - 8.0f * scale, button_top, button_width, button_height }, scale });
        cancel_->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
    }

    void font_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
