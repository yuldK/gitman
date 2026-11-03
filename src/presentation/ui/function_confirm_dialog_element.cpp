#include "presentation/ui/function_confirm_dialog_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/dialog_host.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"

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
        constexpr float action_button_width { 104.0f };
        constexpr float action_button_gap { 8.0f };
        // 실행 중인 것은 없고 저장하지 못한 변경만 묻는 확인이다 (D28). 같은 틀이지만 제목이 실행 중인 기능을 말하면 틀린다.
        [[nodiscard]] bool unsaved_only(const function_confirm_dialog_view& dialog) noexcept
        {
            return dialog.mode == function_confirm_mode::running && dialog.unsaved_changes && dialog.items.empty();
        }

        // 제목이다. 실행 중인 기능 확인(4.10)은 실행하려는 것이 아니라 실행 중인 것을 멈출지 묻는다. logic이 제목을 적었으면
        // 그 글이다(문서 실행 파일 신뢰, S1). 돌려준 글은 `dialog`를 가리키므로 받는 쪽이 복사해 둔다.
        [[nodiscard]] std::u8string_view dialog_title(const function_confirm_dialog_view& dialog) noexcept
        {
            if (dialog.title.empty() == false)
                return dialog.title;
            if (unsaved_only(dialog))
                return u8"저장하지 못한 변경";
            return dialog.mode == function_confirm_mode::running ? std::u8string_view { u8"실행 중인 기능" } : std::u8string_view { u8"기능 실행 확인" };
        }

        // panel의 화면 읽기 이름 앞머리다. logic이 제목을 적었으면 무엇을 신뢰하는지가 그 제목에 있으므로 그 글을 읽는다
        // (`실행 파일 신뢰 확인`, S1). 돌려준 글은 `dialog`를 가리키므로 받는 쪽이 복사해 둔다.
        [[nodiscard]] std::u8string_view panel_access_prefix(const function_confirm_dialog_view& dialog) noexcept
        {
            if (dialog.title.empty() == false)
                return dialog.title;
            if (unsaved_only(dialog))
                return u8"저장하지 못한 변경 확인";
            switch (dialog.mode)
            {
            case function_confirm_mode::trust:
                return u8"신뢰 확인";
            case function_confirm_mode::preview:
                return u8"실행 전 확인";
            case function_confirm_mode::running:
                return u8"실행 중인 기능 확인";
            case function_confirm_mode::correction:
                return u8"보정 확인";
            }
            return u8"기능 실행 확인";
        }

        std::u8string item_owner_value(const std::size_t index)
        {
            std::u8string value { u8"function-confirm-item-" };
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

        // 목록 내용의 높이다 (논리 픽셀). logic의 스크롤 한계(`handle_function_confirm_scroll`)와 같은 식이다.
        [[nodiscard]] float content_height(const function_confirm_dialog_view& dialog) noexcept
        {
            float height { 0.0f };
            for (std::size_t index = 0; index < dialog.items.size(); ++index)
            {
                if (index > 0)
                    height += layout_function_confirm_item_gap;
                height += layout_function_confirm_item_height(dialog.items[index].lines.size());
            }
            return height;
        }

        // 목록 영역의 높이다. 항목이 없으면 목록 자리를 두지 않는다 (`function_confirm_compact_dialog_height`). 내용도
        // 0이라 logic의 스크롤 한계(`handle_function_confirm_scroll`)와 어긋나지 않는다.
        [[nodiscard]] float list_height(const function_confirm_dialog_view& dialog, const float window_height) noexcept
        {
            if (dialog.items.empty())
                return 0.0f;
            return function_confirm_list_height(window_height, dialog.description.size() + 1, dialog.note.size());
        }

        // 항목의 전체 글이다 (tooltip, 화면 읽기). logic이 폭에 맞춰 나눈 줄은 다시 이어 붙여 원래 줄로
        // 읽히게 한다.
        [[nodiscard]] std::u8string joined_item_lines(const function_confirm_item_view& item, const std::u8string_view separator)
        {
            std::u8string text {};
            for (const function_confirm_line& line : item.lines)
            {
                if (line.continuation == false && text.empty() == false)
                    text += separator;
                text += line.text;
            }
            return text;
        }

        // 기능 하나의 상자다. 라벨과 실행 방식 배지, 그 아래 코드 줄을 그린다. 누를 것이 없는
        // 읽기 전용 항목이라 Tab 자리가 아니다.
        class function_confirm_item_element final : public ui_element
        {
        public:
            function_confirm_item_element(const std::size_t index, const function_confirm_item_view& item)
                : ui_element { function_confirm_dialog_item_id(index) }
                , item_ { item }
            {
                set_tab_stop(false);
                // 라벨이 길면 줄이므로 tooltip은 라벨부터 전체 글을 보인다. 코드 줄은 logic이 나눠 두어
                // 보통은 다 보이지만, 글자 폭 어림이 빗나가 말줄임될 때의 길이기도 하다.
                std::u8string tooltip { item_.label };
                tooltip += u8"\n";
                tooltip += joined_item_lines(item_, u8"\n");
                set_tooltip(std::move(tooltip));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            // 화면 읽기는 라벨과 실행 방식, 이어서 줄을 그대로 읽는다. 보이는 글이 곧 확인할 내용이다.
            [[nodiscard]] access_info accessibility() const override
            {
                std::u8string name { item_.label };
                name += u8", ";
                name += item_.mode_text;
                if (item_.lines.empty() == false)
                {
                    name += u8", ";
                    name += joined_item_lines(item_, u8", ");
                }
                return { .role = access_role::list_item, .name = std::move(name) };
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                static_cast<void>(interaction);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 4.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(with_alpha(context.palette.primary_foreground, 0.035f)));
                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.12f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const float padding { layout_function_confirm_item_padding * scale };
                const float inset { layout_function_confirm_item_inset * scale };
                const float header_height { layout_function_confirm_item_header_height() * scale };
                const float header_top { box.y + padding };
                const float right { box.x + box.width - inset };

                // 배지는 라벨 바로 뒤에 붙는다. 라벨이 길면 라벨을 줄이고 배지는 남긴다.
                const text_line_style badge_style { .size = 11.0f };
                const float badge_text_width { measure_text(item_.mode_text, ui_font(context, badge_style.size)) };
                const float badge_width { badge_text_width + 12.0f * scale };
                const text_line_style label_style { .size = 12.0f };
                const float label_left { box.x + inset };
                const float label_width_needed { measure_text(item_.label, ui_font(context, label_style.size)) };
                float label_width { right - label_left - badge_width - 6.0f * scale };
                if (label_width_needed < label_width)
                    label_width = label_width_needed;
                if (label_width < 0.0f)
                    label_width = 0.0f;
                draw_text_line(context, item_.label, { label_left, header_top, label_width, header_height }, solid_paint(context.palette.primary_foreground), label_style);

                const float badge_height { 18.0f * scale };
                const SkRect badge_box { SkRect::MakeXYWH(label_left + label_width + 6.0f * scale, header_top + (header_height - badge_height) / 2.0f, badge_width, badge_height) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(badge_box, badge_height / 2.0f, badge_height / 2.0f), solid_paint(with_alpha(context.palette.accent, 0.18f)));
                draw_text_line(context, item_.mode_text, { badge_box.left(), badge_box.top(), badge_box.width(), badge_box.height() }, solid_paint(context.palette.accent_emphasis_foreground),
                    { .size = badge_style.size, .align = luil::text_align::center });

                // 코드 줄은 logic이 창 폭에 맞춰 나눠 둔 그대로 한 줄씩 그린다. 항목 높이가 줄 수로만
                // 정해져야 logic이 스크롤 한계를 글 폭을 재지 않고 셀 수 있다. 이어지는 줄은 고정폭 두 칸만큼
                // 들여 앞 줄의 뒷부분임을 보인다. 말줄임은 글자 폭 어림이 빗나갈 때의 마지막 방어다.
                const text_line_style code { .size = 10.5f, .face = luil::text_layout_face::code };
                const float line_height { layout_function_confirm_code_line_height() * scale };
                const float indent { measure_text(u8"  ", code_font(context, code.size)) };
                float line_top { header_top + header_height };
                for (const function_confirm_line& line : item_.lines)
                {
                    // cmd 보정의 경고(D30)는 gitman이 고칠 수 없는 위험이라 panel 경고 줄과 같은 경고색으로 그린다.
                    SkPaint paint { solid_paint(line.warning ? context.palette.warning_accent : context.palette.primary_foreground) };
                    if (line.warning == false)
                        paint.setAlphaf(line.dim ? 0.5f : 0.8f);
                    const float left { line.continuation ? label_left + indent : label_left };
                    draw_text_line(context, line.text, { left, line_top, right - left, line_height }, paint, code);
                    line_top += line_height;
                }
            }

        private:
            function_confirm_item_view item_ {};
        };

        // panel이다. 제목·대상·안내·경고를 그리고 목록 항목(자식)을 목록 영역으로 잘라 그린다.
        // 버튼은 clip을 피해 dialog의 자식이다 (탐색 dialog와 같은 구조).
        class function_confirm_panel_element final : public ui_element
        {
        public:
            explicit function_confirm_panel_element(const function_confirm_dialog_view& dialog)
                : ui_element { ui_element_id { kind_function_confirm_dialog_panel } }
                , mode_ { dialog.mode }
                , title_ { dialog_title(dialog) }
                , access_prefix_ { panel_access_prefix(dialog) }
                , subject_ { dialog.subject }
                , description_ { dialog.description }
                , note_ { dialog.note }
            {
                // 흡수는 host가 content(본체)에 건다 — panel이 다시 걸 필요가 없다.
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

            // 화면 읽기가 대상(앞을 줄인 문서 경로의 전체), 안내, 경고 줄을 읽는다. 그리기만 하는 글이라
            // 이 이름이 없으면 경고가 보조 기술에 닿지 않는다.
            [[nodiscard]] access_info accessibility() const override
            {
                std::u8string name { access_prefix_ };
                name += u8", ";
                name += subject_;
                for (const std::u8string& line : description_)
                {
                    name += u8" ";
                    name += line;
                }
                for (const std::u8string& line : note_)
                {
                    name += u8" ";
                    name += line;
                }
                return { .role = access_role::group, .name = std::move(name) };
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

                const float padding { layout_function_confirm_padding * scale };
                const float text_width { box.width - padding * 2.0f };
                const float title_height { layout_function_confirm_title_height * scale };
                draw_text_line(context, title_, { box.x + padding, box.y + padding, text_width, title_height }, solid_paint(context.palette.primary_foreground), { .size = 13.0f });

                // 대상(문서 경로·카드 이름)은 앞을 줄여 끝의 파일 이름을 남긴다. 실행 중인 기능 요약(`빌드 (frontend) 외 1개`)은
                // 앞의 기능 이름이 먼저 사라지지 않게 끝을 줄인다.
                SkPaint dim { solid_paint(context.palette.primary_foreground) };
                dim.setAlphaf(0.65f);
                const float line_height { layout_function_confirm_text_line_height() * scale };
                float line_top { box.y + padding + title_height };
                draw_text_line(context, subject_, { box.x + padding, line_top, text_width, line_height }, dim, { .size = 11.0f, .elide = mode_ == function_confirm_mode::running ? luil::text_elide::end : luil::text_elide::start });
                line_top += line_height;
                for (const std::u8string& line : description_)
                {
                    draw_text_line(context, line, { box.x + padding, line_top, text_width, line_height }, dim, { .size = 11.0f });
                    line_top += line_height;
                }

                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(list_area.x, list_area.y, list_area.width, list_area.height));
                draw_children(context, interaction);
                context.canvas.restore();

                // 신뢰가 덮지 않는 것을 경고색으로 알린다 (3.10의 "덮지 않는 것").
                if (note_.empty())
                    return;
                const ui_color warning { context.palette.warning_accent };
                float note_top { list_area.y + list_area.height + 8.0f * scale };
                float note_left { box.x + padding };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont glyph_font { icon_font(context, 12.0f) };
                    draw_centered_glyph(context.canvas, codicons::icon_warning, { note_left, note_top, 14.0f * scale, line_height }, glyph_font, solid_paint(warning));
                    note_left += 20.0f * scale;
                }
                for (const std::u8string& line : note_)
                {
                    draw_text_line(context, line, { note_left, note_top, box.x + box.width - padding - note_left, line_height }, solid_paint(warning), { .size = 11.0f });
                    note_top += line_height;
                }
            }

        private:
            function_confirm_mode mode_ { function_confirm_mode::trust };
            std::u8string title_ {};
            std::u8string access_prefix_ {};
            std::u8string subject_ {};
            std::vector<std::u8string> description_ {};
            std::vector<std::u8string> note_ {};
        };
    }

    ui_element_id function_confirm_dialog_item_id(const std::size_t index)
    {
        return { kind_function_confirm_dialog_item, item_owner_value(index) };
    }

    function_confirm_dialog_element::function_confirm_dialog_element(const function_confirm_dialog_view& dialog, const float window_height)
        : ui_element { ui_element_id { kind_function_confirm_dialog } }
        , dialog_ { dialog }
        , window_height_ { window_height }
    {
        // 본체는 Tab 자리가 아니다. 초점은 안의 버튼이 받는다 (설계 D1-B).
        set_tab_stop(false);

        auto panel { std::make_unique<function_confirm_panel_element>(dialog_) };
        for (std::size_t index = 0; index < dialog_.items.size(); ++index)
            panel->adopt(std::make_unique<function_confirm_item_element>(index, dialog_.items[index]));
        panel_ = panel.get();
        add_child(std::move(panel));

        // 확인 버튼에 `default_button`을 걸지 않는다. 이 dialog는 누른 기능을 실수로 실행하지 않게
        // 하려고 뜨므로, 버튼에 초점을 옮기지 않은 Enter가 곧바로 실행이 되면 안 된다.
        auto confirm {
            std::make_unique<luil::text_button_element>(
                ui_element_id { kind_function_confirm_dialog_confirm }, luil::text_button_config { .text = dialog_.confirm_label, .visual = luil::text_button_visual::accent }),
        };
        // 앱 종료에서 연 실행 중인 기능 확인은 UI thread의 앱 끝내기 명령을 곧바로 낸다 (4.10). 종료는 주 창의 닫힘 훅을
        // 받아 간 뒤라 logic이 끝낼 길이 없고, 끝내기는 그 훅을 비켜 기존 종료 순서(`cancel`이 출력 수집 기능을 취소 →
        // close → worker join)를 밟는다.
        if (dialog_.exit_application)
        {
            confirm->set_action(ui_trigger::left_click
                , [](const luil::ui_action_context&) -> std::vector<input_action>
                {
                    return { input_action { luil::ui_command::application_exit } };
                }
            );
        }
        else
        {
            confirm->set_action(ui_trigger::left_click, make_logic_click(confirm_function_run_intent {}));
        }
        // 앱 종료 확인이 진행 중인 저장의 결과를 기다리는 동안은 누를 수 없다. 끝내기 명령은 logic을 거치지 않으므로 logic이
        // 막을 수 없고, 저장 결과를 보기 전에 끝내면 실패한 변경이 말없이 사라진다 (S3, 4.10).
        confirm->set_enabled(dialog_.confirm_enabled);
        add_child(std::move(confirm));

        auto cancel {
            std::make_unique<luil::text_button_element>(
                ui_element_id { kind_function_confirm_dialog_cancel }, luil::text_button_config { .text = std::u8string { u8"취소" }, .visual = luil::text_button_visual::normal }),
        };
        cancel->set_action(ui_trigger::left_click, make_logic_click(cancel_function_run_intent {}));
        add_child(std::move(cancel));

        // 목록의 스크롤 막대다. logic의 clamp 계산과 같은 content·viewport를 쓴다 (논리 픽셀).
        const float bar_content { content_height(dialog_) };
        const float bar_viewport { list_height(dialog_, window_height_) };
        auto bar {
            std::make_unique<luil::scrollbar_element>(ui_element_id { kind_scrollbar, std::u8string { scrollbar_owner_function_confirm_dialog } },
                luil::scrollbar_config {
                    .scroll = [](const float delta) -> input_action { return make_logic_action(function_confirm_scroll_intent { delta }); },
                    .scroll_to = [](const float offset) -> input_action { return make_logic_action(function_confirm_scroll_intent { 0.0f, function_confirm_scroll_step::delta, offset }); },
                    .content_height = bar_content,
                    .viewport_height = bar_viewport,
                    .scroll_offset = clamp_scroll_offset(dialog_.scroll_offset, bar_content, bar_viewport),
                }),
        };
        // 목록에 딸린 막대는 Tab 자리가 아니다 (luil list_element와 같은 관례, D1-B).
        bar->set_tab_stop(false);
        // 막대도 이름이 있어야 보조 기술이 어느 목록의 막대인지 읽는다 (S4).
        bar->set_access_name(std::u8string { scrollbar_access_name(bar->id().owner) });
        bar->set_visible(bar_content > bar_viewport);
        scrollbar_ = bar.get();
        add_child(std::move(bar));
    }

    void function_confirm_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        // 받은 자리가 곧 panel 상자다 — 크기와 가운데 배치는 host가 끝냈다.
        const float left { context.slot.x };
        const float top { context.slot.y };
        const float width { context.slot.width };
        const float height { context.slot.height };
        panel_->arrange({ context.slot, scale });

        const float padding { layout_function_confirm_padding * scale };
        auto* const panel { static_cast<function_confirm_panel_element*>(panel_) };
        const float viewport { list_height(dialog_, window_height_) };
        panel->list_area = { left + padding, top + function_confirm_list_top(dialog_.description.size() + 1) * scale, width - padding * 2.0f, viewport * scale };

        // 항목 배치: 스크롤을 반영하고 목록 영역을 벗어난 항목은 숨긴다. 막대가 보이면 항목을
        // 그만큼 좁혀 막대와 겹치지 않게 한다.
        const float offset { clamp_scroll_offset(dialog_.scroll_offset, content_height(dialog_), viewport) * scale };
        const float scrollbar_reserved { scrollbar_->visible() ? layout_scrollbar_hit_width * scale : 0.0f };
        const std::span<const std::unique_ptr<ui_element>> items { panel_->children() };
        float item_top { panel->list_area.y - offset };
        for (std::size_t index = 0; index < items.size() && index < dialog_.items.size(); ++index)
        {
            const float item_height { layout_function_confirm_item_height(dialog_.items[index].lines.size()) * scale };
            const bool inside { item_top + item_height > panel->list_area.y && item_top < panel->list_area.y + panel->list_area.height };
            items[index]->set_visible(inside);
            items[index]->arrange({ { panel->list_area.x, item_top, panel->list_area.width - scrollbar_reserved, item_height }, scale });
            item_top += item_height + layout_function_confirm_item_gap * scale;
        }

        const float bar_width { layout_scrollbar_hit_width * scale };
        scrollbar_->arrange({ { panel->list_area.x + panel->list_area.width - bar_width, panel->list_area.y, bar_width, panel->list_area.height }, scale });

        // 버튼은 panel 오른쪽 아래다 (확인, 취소 순서, 탐색 dialog와 같다).
        const std::span<const std::unique_ptr<ui_element>> children { this->children() };
        if (children.size() >= 3)
        {
            const float button_width { action_button_width * scale };
            const float button_height { layout_function_confirm_button_height * scale };
            const float button_top { top + height - padding - button_height };
            children[1]->arrange({ { left + width - padding - button_width * 2.0f - action_button_gap * scale, button_top, button_width, button_height }, scale });
            children[2]->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
        }
    }

    void function_confirm_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 뒤를 덮는 scrim은 host가 그린다. 본체는 자식(panel·버튼·막대)만 그린다.
        draw_children(context, interaction);
    }

    std::unique_ptr<luil::modal_host_element> make_function_confirm_dialog(const function_confirm_dialog_view& view, const float window_width, const float window_height)
    {
        float width { layout_function_confirm_dialog_width };
        if (width > window_width)
            width = window_width;
        // 창이 낮으면 위아래 여백을 남기고 줄인다. 목록 높이를 정하는 함수가 같은 높이를 쓴다. 항목이 없는 확인(저장하지
        // 못한 변경만 묻는 것)은 빈 목록 자리 없이 글과 버튼 높이만이다.
        const float height {
            view.items.empty()
                ? function_confirm_compact_dialog_height(window_height, view.description.size() + 1, view.note.size())
                : function_confirm_dialog_height(window_height)
        };

        dialog_host_spec spec {};
        spec.owner = std::u8string { dialog_owner_function_confirm };
        spec.name = std::u8string { dialog_title(view) };
        spec.content_width = width;
        spec.content_height = height;
        // 배경 클릭과 Esc는 모두 취소다. 실행하지 않는 쪽이 안전하다.
        spec.outside = make_logic_click(cancel_function_run_intent {});
        spec.dismiss = make_logic_click(cancel_function_run_intent {});
        return make_dialog_host(std::move(spec), std::make_unique<function_confirm_dialog_element>(view, window_height));
    }
}
