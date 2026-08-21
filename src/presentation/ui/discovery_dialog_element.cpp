#include "presentation/ui/discovery_dialog_element.h"

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
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        constexpr float dialog_padding { 14.0f };
        constexpr float action_button_width { 96.0f };
        constexpr float action_button_height { 28.0f };
        // panel 안 배치: 제목과 스캔 루트 아래 목록, 목록 아래 메시지 한 줄, 아래
        // 버튼이다.
        constexpr float list_top_offset { 52.0f };

        std::u8string item_owner_value(const std::size_t index)
        {
            std::u8string value { u8"discovery-item-" };
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

        // 후보 종류 아이콘이다. 제외 후보도 종류가 판정됐으면 같은 아이콘을 흐리게
        // 그린다.
        char32_t candidate_glyph(const repository_kind kind) noexcept
        {
            switch (kind)
            {
            case repository_kind::git:
                return codicons::icon_git_branch;
            case repository_kind::subversion:
                return codicons::icon_link;
            case repository_kind::unknown:
                break;
            }
            return codicons::icon_folder;
        }

        // 제외 사유의 한국어 표시다. 표시 전용이라 element 파일에 둔다.
        std::u8string_view exclusion_text(const discovery_exclusion exclusion) noexcept
        {
            switch (exclusion)
            {
            case discovery_exclusion::not_a_repository:
                return u8"저장소 아님";
            case discovery_exclusion::bare_repository:
                return u8"bare 저장소";
            case discovery_exclusion::conflicting_metadata:
                return u8"Git·SVN 표식 충돌";
            case discovery_exclusion::reparse_point:
                return u8"링크라 제외";
            case discovery_exclusion::already_registered:
                return u8"이미 등록됨";
            case discovery_exclusion::inaccessible:
                return u8"접근 실패";
            case discovery_exclusion::none:
                break;
            }
            return u8"";
        }

        // 후보 한 행이다. 체크박스, 종류 아이콘, 디렉터리 이름과 제외 사유를 그린다.
        // 제외 후보는 비활성이라 클릭이 무시되고 흐리게 표시된다.
        class discovery_row_element final : public ui_element
        {
        public:
            discovery_row_element(const std::size_t index, const discovery_row_view& row)
                : ui_element { discovery_dialog_item_id(index) }
                , row_ { row }
            {
                if (row_.candidate.selectable())
                {
                    set_action(ui_trigger::left_click,
                        [index](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { toggle_discovery_candidate_intent { index } } } }; });
                    set_tooltip(row_.candidate.absolute_path);
                }
                else
                {
                    set_enabled(false);
                    set_tooltip(row_.candidate.absolute_path + u8" - " + std::u8string { exclusion_text(row_.candidate.exclusion) });
                }
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

                if (enabled() && interaction.hovered == id())
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.06f)));

                const float inset { 6.0f * scale };
                float text_left { box.x + inset };

                // 체크박스: 제외 후보는 상자 대신 빈 자리를 남겨 정렬을 유지한다.
                const float check_size { 13.0f * scale };
                if (row_.candidate.selectable())
                {
                    const float check_top { box.y + (box.height - check_size) / 2.0f };
                    const SkRect check_box { SkRect::MakeXYWH(text_left, check_top, check_size, check_size) };
                    SkPaint border { solid_paint(row_.checked ? context.palette.positive_accent : context.palette.primary_foreground) };
                    border.setStyle(SkPaint::kStroke_Style);
                    border.setStrokeWidth(1.0f * scale);
                    context.canvas.drawRRect(SkRRect::MakeRectXY(check_box, 2.0f * scale, 2.0f * scale), border);
                    if (row_.checked && context.codicon_typeface != nullptr)
                    {
                        const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 11.0f * scale };
                        draw_centered_glyph(context.canvas, codicons::icon_check, { text_left, check_top, check_size, check_size }, icon_font, solid_paint(context.palette.positive_accent));
                    }
                }
                text_left += check_size + 8.0f * scale;

                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 11.0f * scale };
                    SkPaint icon_paint { solid_paint(context.palette.primary_foreground) };
                    icon_paint.setAlphaf(row_.candidate.selectable() ? 0.75f : 0.4f);
                    draw_centered_glyph(context.canvas, candidate_glyph(row_.candidate.kind), { text_left, box.y, 14.0f * scale, box.height }, icon_font, icon_paint);
                    text_left += 18.0f * scale;
                }

                const SkFont font { sk_ref_sp(context.ui_typeface), 11.5f * scale };
                SkPaint foreground { solid_paint(row_.checked ? context.palette.positive_accent : context.palette.primary_foreground) };
                if (row_.candidate.selectable() == false)
                    foreground.setAlphaf(0.45f);
                std::u8string text { row_.candidate.directory_name };
                if (row_.candidate.exclusion != discovery_exclusion::none)
                    text += std::u8string { u8" (" } + std::u8string { exclusion_text(row_.candidate.exclusion) } + u8")";
                static_cast<void>(draw_text_within(context.canvas, text, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - inset - text_left, font, foreground));
            }

        private:
            discovery_row_view row_ {};
        };

        // panel이다. 클릭을 흡수하고 제목·스캔 루트·안내·메시지를 그린다. 목록 행은
        // 자식이고 버튼은 clip을 피해 dialog의 자식이다 (switch dialog와 같은 구조).
        class discovery_panel_element final : public ui_element
        {
        public:
            explicit discovery_panel_element(const discovery_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::discovery_dialog_panel } }
                , scan_root_ { dialog.scan_root }
                , loading_ { dialog.loading }
                , executing_ { dialog.executing }
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
                static_cast<void>(draw_text_within(context.canvas, u8"하위 저장소 탐색·등록", box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font,
                    solid_paint(context.palette.primary_foreground)));

                const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                SkPaint dim { solid_paint(context.palette.primary_foreground) };
                dim.setAlphaf(0.65f);
                static_cast<void>(draw_text_within(context.canvas, scan_root_, box.x + padding, box.y + padding + 30.0f * scale, box.width - padding * 2.0f, body_font, dim));

                if (loading_ || empty_)
                {
                    const std::u8string_view note { loading_ ? std::u8string_view { u8"하위 폴더를 탐색하는 중입니다..." } : std::u8string_view { u8"깊이 1에서 발견된 후보가 없습니다." } };
                    draw_text(context.canvas, note, list_area.x, list_area.y + 16.0f * scale, body_font, dim);
                }

                // 목록 행을 자기 영역으로 잘라 그린다. 행은 자식이라 함께 그려진다.
                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height));
                draw_children(context, interaction);
                context.canvas.restore();

                // 하단 로그 콘솔과 같은 실선 경계로 스크롤 영역을 구분한다 (switch
                // dialog와 같은 규칙, 2026-08-22 사용자 지시).
                SkPaint list_border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                list_border.setStyle(SkPaint::kStroke_Style);
                list_border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height), list_border);

                std::u8string_view footer {};
                if (executing_)
                    footer = u8"선택 항목을 등록하는 중입니다...";
                else if (message_.empty() == false)
                    footer = message_;
                if (footer.empty() == false)
                {
                    const float footer_top { list_area.y + list_area.height + 6.0f * scale };
                    static_cast<void>(
                        draw_text_within(context.canvas, footer, box.x + padding, footer_top + 10.0f * scale, box.width - padding * 2.0f, body_font, solid_paint(context.palette.warning_accent)));
                }
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

        private:
            std::u8string scan_root_ {};
            bool loading_ { false };
            bool executing_ { false };
            bool empty_ { false };
            std::u8string message_ {};
        };
    } // namespace

    ui_element_id discovery_dialog_item_id(const std::size_t index)
    {
        ui_element_id id { ui_element_kind::discovery_dialog_item };
        id.owner.value = item_owner_value(index);
        return id;
    }

    discovery_dialog_element::discovery_dialog_element(discovery_dialog_view dialog, const float scale)
        : ui_element { ui_element_id { ui_element_kind::discovery_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 배경 클릭은 취소다 (등록 실행 중에는 logic이 무시한다).
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_discovery_dialog_intent {} } } }; });

        auto panel { std::make_unique<discovery_panel_element>(dialog_) };
        for (std::size_t index = 0; index < dialog_.rows.size(); ++index)
            // 배치에서 화면 밖 행은 숨긴다 (switch dialog와 같은 정책).
            panel->adopt(std::make_unique<discovery_row_element>(index, dialog_.rows[index]));
        panel_ = panel.get();
        add_child(std::move(panel));

        auto confirm { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::discovery_dialog_confirm }, std::u8string { u8"선택 등록" }, true) };
        confirm->set_enabled(dialog_.can_confirm);
        confirm->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { confirm_discovery_intent {} } } }; });
        add_child(std::move(confirm));

        auto cancel { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::discovery_dialog_cancel }, std::u8string { u8"닫기" }, false) };
        cancel->set_enabled(dialog_.executing == false);
        cancel->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_discovery_dialog_intent {} } } }; });
        add_child(std::move(cancel));

        // 목록의 스크롤 막대다 (switch dialog와 같은 정책). logic의 clamp 계산과
        // 같은 content·viewport를 쓴다.
        const float effective { scale > 0.0f ? scale : 1.0f };
        const float bar_content { static_cast<float>(dialog_.rows.size()) * layout_discovery_dialog_row_height * effective };
        const float bar_viewport { layout_discovery_dialog_list_height * effective };
        auto bar {
            std::make_unique<scrollbar_element>(ui_element_id { ui_element_kind::discovery_dialog_scrollbar },
                [](const float delta) { return logic_message { discovery_dialog_scroll_intent { delta } }; }, bar_content, bar_viewport, dialog_.scroll_offset * effective, effective),
        };
        bar->set_visible(bar_content > bar_viewport);
        scrollbar_ = bar.get();
        add_child(std::move(bar));
    }

    void discovery_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { layout_discovery_dialog_width * scale };
        float height { layout_discovery_dialog_height * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { dialog_padding * scale };
        auto* const panel { static_cast<discovery_panel_element*>(panel_) };
        panel->list_area = { left + padding, top + list_top_offset * scale, width - padding * 2.0f, layout_discovery_dialog_list_height * scale };

        // 후보 행 배치: 스크롤을 반영하고 목록 영역을 벗어난 행은 숨긴다. 스크롤
        // 막대가 보이면 행을 그만큼 좁혀 막대와 겹치지 않게 한다.
        const float row_height { layout_discovery_dialog_row_height * scale };
        const float scrollbar_reserved { scrollbar_->visible() ? layout_scrollbar_hit_width * scale : 0.0f };
        const std::span<const std::unique_ptr<ui_element>> rows { panel_->children() };
        for (std::size_t index = 0; index < rows.size(); ++index)
        {
            const float row_top { panel->list_area.y + static_cast<float>(index) * row_height - dialog_.scroll_offset * scale };
            const bool inside { row_top + row_height > panel->list_area.y && row_top < panel->list_area.y + panel->list_area.height };
            rows[index]->set_visible(inside);
            rows[index]->arrange({ { panel->list_area.x, row_top, panel->list_area.width - scrollbar_reserved, row_height }, scale });
        }

        // 스크롤 막대는 목록 영역의 오른쪽 안쪽 세로 띠다.
        const float bar_width { layout_scrollbar_hit_width * scale };
        scrollbar_->arrange({ { panel->list_area.x + panel->list_area.width - bar_width, panel->list_area.y, bar_width, panel->list_area.height }, scale });

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

    void discovery_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
