#include "presentation/ui/switch_dialog_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/dialog_elements.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/scrollbar_element.h"

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

namespace gitman::ui {
    namespace {
        constexpr float dialog_padding { 14.0f };
        constexpr float action_button_width { 120.0f };
        constexpr float action_button_height { 28.0f };
        constexpr float list_top_offset { 34.0f };
        constexpr float tree_indent { 16.0f };
        constexpr float tree_glyph_width { 16.0f };

        float row_tree_indent(const std::size_t depth, const float row_width, const float scale) noexcept
        {
            const float logical_width { scale > 0.0f ? row_width / scale : row_width };
            float maximum { logical_width - 126.0f };
            if (maximum < 0.0f)
                maximum = 0.0f;
            const float requested { static_cast<float>(depth) * tree_indent };
            return requested < maximum ? requested : maximum;
        }

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

        ui_element_id svn_status_id(const svn_repository_browser_row& row)
        {
            ui_element_id id { ui_element_kind::switch_dialog_svn_status };
            id.owner.value = row.url;
            id.owner.value.append(row.kind == svn_browser_row_kind::loading ? u8"#loading" : u8"#error");
            return id;
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

        // Git 후보 한 행이다. F6에서도 Git dialog의 표시와 동작은 그대로 유지한다.
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
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.accent_soft, 0.18f)));
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
                SkPaint foreground { solid_paint(selected_ ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
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

        class svn_expand_element final : public ui_element
        {
        public:
            svn_expand_element(std::u8string url, const bool expanded)
                : ui_element { switch_dialog_svn_expand_id(url) }
                , expanded_ { expanded }
            {
                set_action(ui_trigger::left_click,
                    [url = std::move(url)](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { toggle_svn_browser_node_intent { url } } } }; });
                set_tooltip(expanded_ ? u8"접기" : u8"펼치기");
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot&) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont font { sk_ref_sp(context.codicon_typeface), 11.0f * scale };
                    const char32_t glyph { expanded_ ? codicons::icon_chevron_down : codicons::icon_chevron_right };
                    draw_centered_glyph(context.canvas, glyph, box, font, solid_paint(context.palette.primary_foreground));
                    return;
                }

                const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                draw_text(context.canvas, expanded_ ? std::u8string_view { u8"▾" } : std::u8string_view { u8"▸" }, box.x, box.y + centered_text_baseline(font, box.height), font,
                    solid_paint(context.palette.primary_foreground));
            }

        private:
            bool expanded_ { false };
        };

        class svn_directory_row_element final : public ui_element
        {
        public:
            explicit svn_directory_row_element(svn_repository_browser_row row)
                : ui_element { switch_dialog_svn_item_id(row.url) }
                , row_ { std::move(row) }
            {
                set_action(ui_trigger::left_click,
                    [url = row_.url](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { select_svn_browser_node_intent { url } } } }; });
                if (row_.can_expand)
                {
                    set_action(ui_trigger::double_click,
                        [url = row_.url](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { toggle_svn_browser_node_intent { url } } } }; });
                    add_child(std::make_unique<svn_expand_element>(row_.url, row_.expanded));
                }
                set_tooltip(row_.url);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
                if (children().empty() == false)
                {
                    const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                    const float indent { row_tree_indent(row_.depth, context.slot.width, scale) };
                    const float left { context.slot.x + (6.0f + indent) * scale };
                    children().front()->arrange({ { left, context.slot.y, tree_glyph_width * scale, context.slot.height }, scale });
                }
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
                else if (row_.current)
                    context.canvas.drawRect(body, solid_paint(with_alpha(context.palette.accent_soft, 0.08f)));

                if (row_.current)
                    context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + 2.0f * scale, 2.0f * scale, box.height - 4.0f * scale), solid_paint(context.palette.accent));

                const float inset { 6.0f * scale };
                const float indent { row_tree_indent(row_.depth, box.width, scale) };
                float text_left { box.x + inset + (indent + tree_glyph_width) * scale };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 11.5f * scale };
                    SkPaint icon_paint { solid_paint(row_.current ? context.palette.accent : context.palette.primary_foreground) };
                    icon_paint.setAlphaf(row_.current ? 1.0f : 0.72f);
                    const char32_t glyph { row_.depth == 0 ? codicons::icon_repo : (row_.expanded ? codicons::icon_folder_opened : codicons::icon_folder) };
                    draw_centered_glyph(context.canvas, glyph, { text_left, box.y, 14.0f * scale, box.height }, icon_font, icon_paint);
                    text_left += 18.0f * scale;
                }

                std::u8string text { row_.text };
                if (row_.current)
                    text.append(u8" (현재 위치)");
                const SkFont font { sk_ref_sp(context.ui_typeface), 11.5f * scale };
                const SkPaint foreground { solid_paint(row_.current ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
                static_cast<void>(draw_text_within(context.canvas, text, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - inset - text_left, font, foreground));
                draw_children(context, interaction);
            }

        private:
            svn_repository_browser_row row_ {};
        };

        class svn_status_row_element final : public ui_element
        {
        public:
            explicit svn_status_row_element(svn_repository_browser_row row)
                : ui_element { svn_status_id(row) }
                , row_ { std::move(row) }
            {}

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot&) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const float indent { row_tree_indent(row_.depth, box.width, scale) };
                float text_left { box.x + (6.0f + indent + tree_glyph_width) * scale };
                const bool failed { row_.kind == svn_browser_row_kind::error };
                const std::uint32_t color { failed ? context.palette.warning_accent : context.palette.primary_foreground };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 11.0f * scale };
                    SkPaint icon_paint { solid_paint(color) };
                    if (failed == false)
                        icon_paint.setAlphaf(0.6f);
                    draw_centered_glyph(context.canvas, failed ? codicons::icon_warning : codicons::icon_loading, { text_left, box.y, 14.0f * scale, box.height }, icon_font, icon_paint);
                    text_left += 18.0f * scale;
                }
                const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                SkPaint foreground { solid_paint(color) };
                if (failed == false)
                    foreground.setAlphaf(0.6f);
                static_cast<void>(
                    draw_text_within(context.canvas, row_.text, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - 6.0f * scale - text_left, font, foreground));
            }

        private:
            svn_repository_browser_row row_ {};
        };

        class switch_panel_element final : public ui_element
        {
        public:
            explicit switch_panel_element(const switch_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::switch_dialog_panel } }
                , title_ { dialog.title }
                , loading_ { dialog.loading }
                , stale_ { dialog.stale }
                , svn_browser_ { dialog.svn_browser }
                , empty_ { dialog.svn_browser ? dialog.svn_rows.empty() : dialog.candidates.empty() }
                , message_ { dialog.message }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
            }

            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

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
                std::u8string heading { svn_browser_ ? std::u8string { u8"SVN 저장소 전환 - " } : std::u8string { u8"전환 - " } };
                heading += title_;
                if (stale_ && svn_browser_ == false)
                    heading += u8" (일부 후보 미갱신)";
                static_cast<void>(draw_text_within(
                    context.canvas, heading, box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font, solid_paint(context.palette.primary_foreground)));

                const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                if (loading_ || empty_)
                {
                    SkPaint dim { solid_paint(context.palette.primary_foreground) };
                    dim.setAlphaf(0.6f);
                    std::u8string_view note {};
                    if (svn_browser_)
                        note = loading_ ? std::u8string_view { u8"SVN 저장소 정보를 조회하는 중입니다..." } : std::u8string_view { u8"SVN 저장소를 탐색할 수 없습니다." };
                    else
                        note = loading_ ? std::u8string_view { u8"전환 후보를 조회하는 중입니다..." } : std::u8string_view { u8"전환할 수 있는 후보가 없습니다." };
                    draw_text(context.canvas, note, list_area.x, list_area.y + 16.0f * scale, body_font, dim);
                }

                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height));
                draw_children(context, interaction);
                context.canvas.restore();

                // 목록이 주변(제목·버튼 영역)과 같은 색이라 경계가 보이지 않는다.
                // 하단 로그 콘솔과 같은 실선 경계를 둘러 스크롤 영역을 구분한다
                // (2026-08-22 사용자 지시: 그림자 대신 solid border).
                SkPaint list_border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                list_border.setStyle(SkPaint::kStroke_Style);
                list_border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height), list_border);

                if (message_.empty() == false)
                {
                    const float message_top { list_area.y + list_area.height + 6.0f * scale };
                    static_cast<void>(
                        draw_text_within(context.canvas, message_, box.x + padding, message_top + 10.0f * scale, box.width - padding * 2.0f, body_font, solid_paint(context.palette.warning_accent)));
                }
            }

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
                if (interactive() && bounds().contains(x, y))
                    return this;
                return nullptr;
            }

        private:
            std::u8string title_ {};
            bool loading_ { false };
            bool stale_ { false };
            bool svn_browser_ { false };
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

    ui_element_id switch_dialog_svn_item_id(const std::u8string_view url)
    {
        return { ui_element_kind::switch_dialog_item, project_id { std::u8string { url } } };
    }

    ui_element_id switch_dialog_svn_expand_id(const std::u8string_view url)
    {
        return { ui_element_kind::switch_dialog_svn_expand, project_id { std::u8string { url } } };
    }

    switch_dialog_element::switch_dialog_element(switch_dialog_view dialog, const float scale, const float window_height)
        : ui_element { ui_element_id { ui_element_kind::switch_dialog } }
        , dialog_ { std::move(dialog) }
    {
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_switch_dialog_intent {} } } }; });

        auto panel { std::make_unique<switch_panel_element>(dialog_) };
        if (dialog_.svn_browser)
        {
            for (const svn_repository_browser_row& row : dialog_.svn_rows)
                if (row.kind == svn_browser_row_kind::directory)
                    panel->adopt(std::make_unique<svn_directory_row_element>(row));
                else
                    panel->adopt(std::make_unique<svn_status_row_element>(row));
        }
        else
        {
            for (std::size_t index = 0; index < dialog_.candidates.size(); ++index)
            {
                const bool selected { dialog_.selected.has_value() && *dialog_.selected == index };
                panel->adopt(std::make_unique<candidate_row_element>(index, dialog_.candidates[index], selected));
            }
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

        // 목록의 스크롤 막대다. 내용이 목록 영역보다 길 때만 보인다. content·
        // viewport는 logic의 clamp 계산과 같은 값이라 thumb와 실제 스크롤이
        // 어긋나지 않는다.
        const float effective { scale > 0.0f ? scale : 1.0f };
        const std::size_t bar_rows { dialog_.svn_browser ? dialog_.svn_rows.size() : dialog_.candidates.size() };
        const float bar_content { static_cast<float>(bar_rows) * layout_switch_dialog_row_height * effective };
        const float bar_viewport { switch_dialog_list_height(dialog_.svn_browser, window_height / effective) * effective };
        auto bar {
            std::make_unique<scrollbar_element>(
                ui_element_id { ui_element_kind::switch_dialog_scrollbar }, [](const float delta) { return logic_message { switch_dialog_scroll_intent { delta } }; }, bar_content, bar_viewport,
                dialog_.scroll_offset * effective, effective),
        };
        bar->set_visible(bar_content > bar_viewport);
        scrollbar_ = bar.get();
        add_child(std::move(bar));
    }

    void switch_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { (dialog_.svn_browser ? layout_svn_browser_dialog_width : layout_switch_dialog_width) * scale };
        float height { (dialog_.svn_browser ? layout_svn_browser_dialog_height : layout_switch_dialog_height) * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { dialog_padding * scale };
        auto* const panel { static_cast<switch_panel_element*>(panel_) };
        const float list_height { switch_dialog_list_height(dialog_.svn_browser, context.slot.height / scale) * scale };
        panel->list_area = { left + padding, top + list_top_offset * scale, width - padding * 2.0f, list_height };

        const float row_height { layout_switch_dialog_row_height * scale };
        // 스크롤 막대가 보이면 행을 그만큼 좁혀 막대와 겹치지 않게 한다.
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

        // 스크롤 막대는 목록 영역의 오른쪽 안쪽 세로 띠다. 목록 경계와
        // 겹치지 않도록 top/right/bottom에 1px 여백을 둔다.
        const float bar_width { layout_scrollbar_hit_width * scale };
        const float bar_left { panel->list_area.x + panel->list_area.width - scrollbar_margin - bar_width };
        const float bar_height { panel->list_area.height - scrollbar_margin * 2.0f };
        scrollbar_->arrange({ { bar_left, panel->list_area.y + scrollbar_margin, bar_width, bar_height }, scale });

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
