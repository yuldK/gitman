#include "presentation/ui/local_changes_dialog_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/diff_presentation.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/button_element.h"
#include "presentation/ui/dialog_elements.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/scrollbar_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gitman::ui {
    namespace {
        constexpr float dialog_padding { 14.0f };
        constexpr float action_button_width { 88.0f };
        constexpr float action_button_height { 28.0f };
        // panel 안 배치: 제목 아래 목록, 목록 아래 diff pane, 아래 닫기 버튼이다.
        constexpr float list_top_offset { 34.0f };
        constexpr float diff_gap { 10.0f };

        std::u8string item_owner_value(const std::size_t index)
        {
            std::u8string value { u8"local-change-" };
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

        // 변경 항목 한 행이다. 종류 배지, (미추적) 파일·폴더 아이콘, 말줄임 경로와
        // 오른쪽 끝의 외부 열기 아이콘 2개를 담는다.
        class change_row_element final : public ui_element
        {
        public:
            change_row_element(const std::size_t index, const local_change_row_view& row)
                : ui_element { local_changes_item_id(index) }
                , row_ { row }
            {
                set_action(
                    ui_trigger::left_click, [index](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { select_local_change_intent { index } } } }; });

                ui_element_id vscode_id { ui_element_kind::local_changes_open_vscode };
                vscode_id.owner.value = local_changes_item_id(index).owner.value;
                auto vscode { std::make_unique<button_element>(vscode_id, button_config { .glyph = codicons::icon_file }) };
                vscode->set_tooltip(u8"VSCode에서 열기");
                vscode->set_action(ui_trigger::left_click, [path = row.absolute_path](const ui_action_context&) -> std::vector<input_action> {
                    return { input_action { open_external_request { external_open_target::vscode, path } } };
                });
                open_vscode_ = vscode.get();
                add_child(std::move(vscode));

                ui_element_id explorer_id { ui_element_kind::local_changes_open_explorer };
                explorer_id.owner.value = local_changes_item_id(index).owner.value;
                auto explorer { std::make_unique<button_element>(explorer_id, button_config { .glyph = codicons::icon_folder_opened }) };
                explorer->set_tooltip(u8"탐색기에서 열기");
                explorer->set_action(ui_trigger::left_click, [path = row.absolute_path](const ui_action_context&) -> std::vector<input_action> {
                    return { input_action { open_external_request { external_open_target::explorer, path } } };
                });
                open_explorer_ = explorer.get();
                add_child(std::move(explorer));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);

                // 열기 아이콘은 오른쪽 끝에서부터 [VSCode][탐색기] 순서로 나란하다.
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const float button { context.slot.height - 4.0f * scale };
                const float gap { 2.0f * scale };
                float right { context.slot.x + context.slot.width - 4.0f * scale };
                open_explorer_->arrange({ { right - button, context.slot.y + 2.0f * scale, button, button }, scale });
                right -= button + gap;
                open_vscode_->arrange({ { right - button, context.slot.y + 2.0f * scale, button, button }, scale });
                text_limit_ = right - button - 6.0f * scale;
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

                if (row_.selected)
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.accent_soft, 0.18f)));
                else if (interaction.hovered == id())
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.06f)));

                // 미추적 항목은 아직 저장소 밖의 파일이라 흐리게(비활성 느낌) 그린다.
                const float dim { row_.untracked ? 0.55f : 1.0f };
                const float inset { 6.0f * scale };
                const SkFont badge_font { sk_ref_sp(context.ui_typeface), 10.0f * scale };
                const SkFont path_font { sk_ref_sp(context.ui_typeface), 11.5f * scale };

                // 배지는 고정 폭 상자로 그려 경로 시작 위치를 맞춘다. 미추적은 강조색
                // 대신 중립색이고, 파일/폴더 codicon을 배지 라벨 안(글자 앞)에 그린다.
                const ui_color badge_color { row_.untracked ? context.palette.primary_foreground : context.palette.warning_accent };
                const float badge_width { 74.0f * scale };
                const SkRect badge_box { SkRect::MakeXYWH(box.x + inset, box.y + 3.0f * scale, badge_width, box.height - 6.0f * scale) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(badge_box, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(badge_color, 0.14f)));
                SkPaint badge_paint { solid_paint(badge_color) };
                badge_paint.setAlphaf(dim);
                float badge_text_left { badge_box.left() + 5.0f * scale };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 10.0f * scale };
                    draw_centered_glyph(
                        context.canvas, row_.directory ? codicons::icon_folder : codicons::icon_file, { badge_text_left, box.y, 12.0f * scale, box.height }, icon_font, badge_paint);
                    badge_text_left += 15.0f * scale;
                }
                static_cast<void>(draw_text_within(context.canvas, row_.badge, badge_text_left, box.y + centered_text_baseline(badge_font, box.height),
                    badge_box.right() - 5.0f * scale - badge_text_left, badge_font, badge_paint));

                const float path_left { box.x + inset + badge_width + 8.0f * scale };

                SkPaint foreground { solid_paint(row_.selected ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
                foreground.setAlphaf(dim);
                // 경로가 열기 아이콘과 겹치면 `...`으로 줄이고 아이콘은 유지한다.
                const float available { text_limit_ - path_left };
                const std::u8string shown { elide_text(row_.path, available, path_font) };
                draw_text(context.canvas, shown, path_left, box.y + centered_text_baseline(path_font, box.height), path_font, foreground);

                draw_children(context, interaction);
            }

        private:
            local_change_row_view row_ {};
            ui_element* open_vscode_ { nullptr };
            ui_element* open_explorer_ { nullptr };
            float text_limit_ { 0.0f };
        };

        // 하단 diff viewer다. 줄 첫 문자로 색을 정하고 보이는 범위만 그린다. 휠
        // 라우팅의 기준 영역이라 hit 대상이어야 한다.
        class diff_pane_element final : public ui_element
        {
        public:
            explicit diff_pane_element(const local_changes_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::local_changes_diff } }
                , dialog_ { &dialog }
            {
                // 클릭을 흡수해 배경 닫기로 흐르지 않게 한다.
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot&) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.05f)));

                const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                const float inset { 7.0f * scale };
                const float line_height { layout_local_changes_diff_line_height * scale };
                const float scroll { dialog_->diff_scroll * scale };

                context.canvas.save();
                context.canvas.clipRect(body);

                if (dialog_->diff_loading)
                {
                    SkPaint dim { solid_paint(context.palette.primary_foreground) };
                    dim.setAlphaf(0.6f);
                    draw_text(context.canvas, u8"diff를 조회하는 중입니다...", box.x + inset, box.y + 16.0f * scale, font, dim);
                }
                else
                {
                    // 2-way: 가운데 구분선을 두고 왼쪽이 이전, 오른쪽이 이후다. 화면에
                    // 걸치는 행만 그린다.
                    const float divider { box.x + box.width / 2.0f };
                    const float left_text { box.x + inset };
                    const float left_width { divider - inset - left_text };
                    const float right_text { divider + inset };
                    const float right_width { box.x + box.width - inset - right_text };

                    const std::size_t first { static_cast<std::size_t>(std::max(0.0f, std::floor(scroll / line_height))) };
                    const std::size_t visible { static_cast<std::size_t>(box.height / line_height) + 2 };
                    for (std::size_t index = first; index < dialog_->diff_rows.size() && index < first + visible; ++index)
                    {
                        const two_way_diff_row& row { dialog_->diff_rows[index] };
                        const float top { box.y + 4.0f * scale + static_cast<float>(index) * line_height - scroll };
                        const float baseline { top + centered_text_baseline(font, line_height) };

                        if (row.heading)
                        {
                            SkPaint heading_paint { solid_paint(context.palette.primary_foreground) };
                            heading_paint.setAlphaf(0.55f);
                            static_cast<void>(draw_text_within(context.canvas, row.left, left_text, baseline, box.width - inset * 2.0f, font, heading_paint));
                            continue;
                        }

                        if (row.changed)
                        {
                            // 변경 행은 좌우 반 칸에 옅은 배경을 깔아 쌍을 보여 준다.
                            if (row.has_left)
                                context.canvas.drawRect(SkRect::MakeXYWH(box.x, top, divider - box.x, line_height), solid_paint(with_alpha(context.palette.error_accent, 0.12f)));
                            if (row.has_right)
                                context.canvas.drawRect(SkRect::MakeXYWH(divider, top, box.x + box.width - divider, line_height), solid_paint(with_alpha(context.palette.accent_soft, 0.12f)));
                        }

                        if (row.has_left)
                        {
                            SkPaint left_paint { row.changed ? solid_paint(context.palette.error_accent) : solid_paint(context.palette.primary_foreground) };
                            if (row.changed == false)
                                left_paint.setAlphaf(0.85f);
                            static_cast<void>(draw_text_within(context.canvas, row.left, left_text, baseline, left_width, font, left_paint));
                        }
                        if (row.has_right)
                        {
                            SkPaint right_paint { row.changed ? solid_paint(context.palette.accent_emphasis_foreground) : solid_paint(context.palette.primary_foreground) };
                            if (row.changed == false)
                                right_paint.setAlphaf(0.85f);
                            static_cast<void>(draw_text_within(context.canvas, row.right, right_text, baseline, right_width, font, right_paint));
                        }
                    }

                    // 가운데 구분선이다. 행 배경 위에 그린다.
                    SkPaint divider_paint { solid_paint(with_alpha(context.palette.primary_foreground, 0.2f)) };
                    context.canvas.drawRect(SkRect::MakeXYWH(divider, box.y, 1.0f * scale, box.height), divider_paint);

                    if (dialog_->diff_notice.empty() == false)
                    {
                        // 안내는 pane 아래쪽에 고정해 스크롤과 무관하게 보인다.
                        const SkRect notice_box { SkRect::MakeXYWH(box.x, box.y + box.height - 20.0f * scale, box.width, 20.0f * scale) };
                        context.canvas.drawRect(notice_box, solid_paint(with_alpha(context.palette.warning_accent, 0.15f)));
                        static_cast<void>(draw_text_within(context.canvas, dialog_->diff_notice, box.x + inset, notice_box.top() + centered_text_baseline(font, 20.0f * scale),
                            box.width - inset * 2.0f, font, solid_paint(context.palette.warning_accent)));
                    }
                }
                context.canvas.restore();

                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.2f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), border);
            }

        private:
            const local_changes_dialog_view* dialog_ { nullptr };
        };

        // panel이다. 클릭을 흡수하고 제목·목록 안내·메시지를 그린다. 목록 행과 diff
        // pane, 버튼은 dialog가 직접 배치한다.
        class local_changes_panel_element final : public ui_element
        {
        public:
            explicit local_changes_panel_element(const local_changes_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::local_changes_dialog_panel } }
                , title_ { dialog.title }
                , loading_ { dialog.loading }
                , empty_ { dialog.rows.empty() }
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
                const std::u8string heading { std::u8string { u8"로컬 변경 - " } + title_ };
                static_cast<void>(draw_text_within(
                    context.canvas, heading, box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font, solid_paint(context.palette.primary_foreground)));

                const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                if (loading_ || empty_)
                {
                    SkPaint dim { solid_paint(context.palette.primary_foreground) };
                    dim.setAlphaf(0.6f);
                    const std::u8string_view note { loading_ ? std::u8string_view { u8"로컬 변경을 조회하는 중입니다..." } : std::u8string_view { u8"표시할 로컬 변경이 없습니다." } };
                    draw_text(context.canvas, note, list_area.x, list_area.y + 16.0f * scale, body_font, dim);
                }

                // 목록 행을 자기 영역으로 잘라 그린다. 행은 자식이라 함께 그려진다.
                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height));
                draw_children(context, interaction);
                context.canvas.restore();

                if (message_.empty() == false && loading_ == false)
                {
                    // 목록 바로 아래 한 줄이다. 빈 목록 안내와 겹치지 않는 위치다.
                    const float message_top { list_area.y + list_area.height + 2.0f * scale };
                    static_cast<void>(
                        draw_text_within(context.canvas, message_, box.x + padding, message_top - 14.0f * scale, box.width - padding * 2.0f, body_font, solid_paint(context.palette.warning_accent)));
                }
            }

        private:
            std::u8string title_ {};
            bool loading_ { false };
            bool empty_ { false };
            std::u8string message_ {};
        };
    } // namespace

    ui_element_id local_changes_item_id(const std::size_t index)
    {
        ui_element_id id { ui_element_kind::local_changes_item };
        id.owner.value = item_owner_value(index);
        return id;
    }

    local_changes_dialog_element::local_changes_dialog_element(local_changes_dialog_view dialog, const float scale)
        : ui_element { ui_element_id { ui_element_kind::local_changes_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 배경 클릭은 닫기다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_local_changes_dialog_intent {} } } }; });

        auto panel { std::make_unique<local_changes_panel_element>(dialog_) };
        for (std::size_t index = 0; index < dialog_.rows.size(); ++index)
            panel->adopt(std::make_unique<change_row_element>(index, dialog_.rows[index]));
        panel_ = panel.get();
        add_child(std::move(panel));

        auto diff { std::make_unique<diff_pane_element>(dialog_) };
        diff_ = diff.get();
        add_child(std::move(diff));

        auto close { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::local_changes_dialog_close, dialog_.card }, std::u8string { u8"닫기" }, true) };
        close->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_local_changes_dialog_intent {} } } }; });
        close_ = close.get();
        add_child(std::move(close));

        // 목록·diff 각각의 시각적 스크롤 막대다. 내용이 넘칠 때만 보인다.
        const float effective { scale > 0.0f ? scale : 1.0f };
        const float list_content { static_cast<float>(dialog_.rows.size()) * layout_local_changes_row_height * effective };
        auto list_bar {
            std::make_unique<scrollbar_element>(ui_element_id { ui_element_kind::local_changes_list_scrollbar },
                [](const float delta) { return logic_message { local_changes_scroll_intent { delta } }; }, list_content, layout_local_changes_list_height * effective,
                dialog_.list_scroll * effective, effective),
        };
        list_bar->set_visible(list_content > layout_local_changes_list_height * effective);
        list_scrollbar_ = list_bar.get();
        add_child(std::move(list_bar));

        const float diff_content { static_cast<float>(dialog_.diff_rows.size()) * layout_local_changes_diff_line_height * effective };
        auto diff_bar {
            std::make_unique<scrollbar_element>(ui_element_id { ui_element_kind::local_changes_diff_scrollbar },
                [](const float delta) { return logic_message { local_changes_diff_scroll_intent { delta } }; }, diff_content, layout_local_changes_diff_height * effective,
                dialog_.diff_scroll * effective, effective),
        };
        diff_bar->set_visible(diff_content > layout_local_changes_diff_height * effective);
        diff_scrollbar_ = diff_bar.get();
        add_child(std::move(diff_bar));
    }

    void local_changes_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { layout_local_changes_dialog_width * scale };
        float height { layout_local_changes_dialog_height * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { dialog_padding * scale };
        auto* const panel { static_cast<local_changes_panel_element*>(panel_) };
        panel->list_area = { left + padding, top + list_top_offset * scale, width - padding * 2.0f, layout_local_changes_list_height * scale };

        const float row_height { layout_local_changes_row_height * scale };
        // 스크롤 막대가 보이면 행을 그만큼 좁혀 열기 아이콘과 겹치지 않게 한다.
        const float scrollbar_reserved { list_scrollbar_->visible() ? layout_scrollbar_hit_width * scale : 0.0f };
        const std::span<const std::unique_ptr<ui_element>> rows { panel_->children() };
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const float row_top { panel->list_area.y + static_cast<float>(index) * row_height - dialog_.list_scroll * scale };
            const bool inside { row_top + row_height > panel->list_area.y && row_top < panel->list_area.y + panel->list_area.height };
            rows[index]->set_visible(inside);
            rows[index]->arrange({ { panel->list_area.x, row_top, panel->list_area.width - scrollbar_reserved, row_height }, scale });
        }

        // diff pane은 목록 아래 남는 공간을 채운다.
        const float diff_top { panel->list_area.y + panel->list_area.height + diff_gap * scale };
        const rect_f diff_area { left + padding, diff_top, width - padding * 2.0f, layout_local_changes_diff_height * scale };
        diff_->arrange({ diff_area, scale });

        const float button_width { action_button_width * scale };
        const float button_height { action_button_height * scale };
        close_->arrange({ { left + width - padding - button_width, top + height - padding - button_height, button_width, button_height }, scale });

        // 스크롤 막대는 각 영역의 오른쪽 안쪽 세로 띠다.
        const float bar_width { layout_scrollbar_hit_width * scale };
        list_scrollbar_->arrange({ { panel->list_area.x + panel->list_area.width - bar_width, panel->list_area.y, bar_width, panel->list_area.height }, scale });
        diff_scrollbar_->arrange({ { diff_area.x + diff_area.width - bar_width, diff_area.y, bar_width, diff_area.height }, scale });
    }

    void local_changes_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        // 화면 전체를 어둡게 덮어 뒤 내용이 비활성임을 보인다.
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
