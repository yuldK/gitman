#include "presentation/ui/command_editor_dialog_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/dialog_host.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/status_glyphs.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/dialog_elements.h"

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
        constexpr float small_button_width { 78.0f };
        constexpr float first_row_top { 40.0f };
        constexpr float row_gap { 6.0f };
        constexpr float field_height { 26.0f };
        constexpr float choice_button_width { 168.0f };
        constexpr float icon_preview_width { 26.0f };

        // 창의 줄 하나가 무엇인지다. 명령과 훅이 쓰는 줄이 달라 목록으로 받는다.
        enum class editor_field
        {
            name,
            phase,
            script,
            arguments,
            icon,
            working_directory,
            target_vcs,
            path_pattern,
            projects,
            timeout,
            flags,
        };

        std::vector<editor_field> rows_for(const bool hook)
        {
            if (hook)
                return { editor_field::phase, editor_field::script, editor_field::arguments, editor_field::path_pattern, editor_field::timeout, editor_field::flags };
            return {
                editor_field::name,
                editor_field::script,
                editor_field::arguments,
                editor_field::icon,
                editor_field::working_directory,
                editor_field::target_vcs,
                editor_field::path_pattern,
                editor_field::projects,
                editor_field::timeout,
                editor_field::flags,
            };
        }

        std::u8string_view label_of(const editor_field field) noexcept
        {
            switch (field)
            {
            case editor_field::name:
                return u8"이름";
            case editor_field::phase:
                return u8"단계";
            case editor_field::script:
                return u8"스크립트";
            case editor_field::arguments:
                return u8"인자";
            case editor_field::icon:
                return u8"아이콘";
            case editor_field::working_directory:
                return u8"작업 폴더";
            case editor_field::target_vcs:
                return u8"대상";
            case editor_field::path_pattern:
                return u8"경로 패턴";
            case editor_field::projects:
                return u8"카드";
            case editor_field::timeout:
                return u8"제한 시간";
            case editor_field::flags:
            default:
                return u8"옵션";
            }
        }

        float row_top(const std::size_t index)
        {
            return first_row_top + static_cast<float>(index) * (layout_command_editor_row_height + row_gap);
        }

        class command_editor_panel_element final : public ui_element
        {
        public:
            explicit command_editor_panel_element(command_editor_dialog_view dialog)
                : ui_element { ui_element_id { kind_command_editor_panel } }
                , dialog_ { std::move(dialog) }
            {}

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
                    context.canvas, dialog_.title, box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font, solid_paint(context.palette.primary_foreground)));

                const SkFont label_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                const float label_left { box.x + padding };
                const std::vector<editor_field> rows { rows_for(dialog_.hook) };
                for (std::size_t index = 0; index < rows.size(); ++index)
                {
                    const float top { box.y + row_top(index) * scale };
                    SkPaint paint { solid_paint(context.palette.primary_foreground) };
                    paint.setAlphaf(0.8f);
                    draw_text(context.canvas, label_of(rows[index]), label_left, top + centered_text_baseline(label_font, layout_command_editor_row_height * scale), label_font, paint);
                }

                // 아이콘 칸 옆에 고른 글리프를 그대로 보여 준다. 이름만으로는 무엇이
                // 나올지 알 수 없다.
                if (dialog_.hook == false && context.codicon_typeface != nullptr)
                {
                    const std::vector<editor_field> fields { rows_for(false) };
                    for (std::size_t index = 0; index < fields.size(); ++index)
                    {
                        if (fields[index] != editor_field::icon)
                            continue;
                        const float top { box.y + row_top(index) * scale };
                        const rect_f slot { box.x + box.width - padding - icon_preview_width * scale, top, icon_preview_width * scale, layout_command_editor_row_height * scale };
                        const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 14.0f * scale };
                        draw_centered_glyph(context.canvas, codicon_for_configured_name(dialog_.icon_preview, default_command_codicon()), slot, icon_font, solid_paint(context.palette.accent));
                        break;
                    }
                }

                if (dialog_.message.empty() == false)
                {
                    const float message_top { box.y + row_top(rows.size()) * scale };
                    static_cast<void>(draw_text_within(context.canvas, dialog_.message, label_left, message_top + centered_text_baseline(label_font, layout_command_editor_row_height * scale),
                        box.width - padding * 2.0f, label_font, solid_paint(context.palette.warning_accent)));
                }

                draw_children(context, interaction);
            }

        private:
            command_editor_dialog_view dialog_ {};
        };
    } // namespace

    std::size_t command_editor_row_count(const bool hook) noexcept
    {
        return rows_for(hook).size();
    }

    command_editor_dialog_element::command_editor_dialog_element(command_editor_dialog_view dialog)
        : ui_element { ui_element_id { kind_command_editor_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 본체는 Tab 자리가 아니다. 초점은 안의 칸·버튼이 받는다 (설계 D1-B).
        set_tab_stop(false);

        auto panel { std::make_unique<command_editor_panel_element>(dialog_) };
        panel_ = panel.get();
        add_child(std::move(panel));

        const auto add_input = [this](const ui_element_kind kind, const text_input_view& value, const std::u8string_view placeholder, const std::u8string_view tooltip) {
            auto input { std::make_unique<luil::text_input_element>(ui_element_id { kind }, value, luil::text_input_config { .placeholder = std::u8string { placeholder } }) };
            input->set_tooltip(std::u8string { tooltip });
            ui_element* const raw { input.get() };
            add_child(std::move(input));
            return raw;
        };

        const auto add_button = [this](const ui_element_kind kind, const std::u8string_view text, const bool accent, const bool enabled, ui_action click, const bool default_button = false) {
            auto button {
                std::make_unique<luil::text_button_element>(ui_element_id { kind },
                    luil::text_button_config {
                        .text = std::u8string { text },
                        .visual = accent ? luil::text_button_visual::accent : luil::text_button_visual::normal,
                        .default_button = default_button,
                    }),
            };
            button->set_enabled(enabled);
            if (enabled)
                button->set_action(ui_trigger::left_click, std::move(click));
            ui_element* const raw { button.get() };
            add_child(std::move(button));
            return raw;
        };

        for (const editor_field field : rows_for(dialog_.hook))
        {
            std::vector<ui_element*> controls {};
            switch (field)
            {
            case editor_field::name:
                controls.push_back(add_input(kind_command_editor_name_input, dialog_.name, u8"메뉴에 뜰 이름", u8"카드 우클릭 메뉴와 버튼 툴팁에 나옵니다"));
                break;
            case editor_field::phase:
                controls.push_back(add_button(kind_command_editor_phase, dialog_.phase_label, false, true, make_logic_click(cycle_command_choice_intent { command_choice::hook_phase })));
                break;
            case editor_field::script: {
                controls.push_back(add_input(kind_command_editor_script_input, dialog_.script, u8"절대 경로 또는 문서 폴더 기준 상대 경로", u8"확장자가 해석기를 정합니다 (.py .js .ps1 .bat .cmd .exe)"));
                controls.push_back(
                    add_button(kind_command_editor_script_browse, u8"찾아보기…", false, true, make_app_command_click(app_command::show_command_script_picker, dialog_.script_browse_folder)));
                // 스크립트 **내용**은 이 창에서 고치지 않는다 (2026-09-10 지시).
                // 편집은 편집기가 훨씬 잘한다.
                controls.push_back(add_button(kind_command_editor_script_open, u8"편집기", false, dialog_.script_open_path.empty() == false,
                    make_app_command_click(app_command::open_in_vscode, dialog_.script_open_path)));
                break;
            }
            case editor_field::arguments:
                controls.push_back(add_input(kind_command_editor_arguments_input, dialog_.arguments, u8"공백으로 나눕니다. 공백이 든 인자는 따옴표로", u8"스크립트 뒤에 그대로 붙습니다"));
                break;
            case editor_field::icon:
                controls.push_back(add_input(kind_command_editor_icon_input, dialog_.icon, u8"codicon 이름 (run-all, beaker, tools …)", u8"모르는 이름이면 기본 아이콘을 씁니다"));
                break;
            case editor_field::working_directory:
                controls.push_back(add_button(
                    kind_command_editor_working_directory, dialog_.working_directory_label, false, true, make_logic_click(cycle_command_choice_intent { command_choice::working_directory })));
                break;
            case editor_field::target_vcs:
                controls.push_back(
                    add_button(kind_command_editor_vcs, dialog_.vcs_label, false, true, make_logic_click(cycle_command_choice_intent { command_choice::target_vcs })));
                break;
            case editor_field::path_pattern:
                controls.push_back(add_input(kind_command_editor_path_pattern_input, dialog_.path_pattern, u8"비우면 모든 카드 (예: C:/work/*)", u8"저장소 경로나 카드 이름에 맞춰 봅니다"));
                break;
            case editor_field::projects:
                controls.push_back(add_input(kind_command_editor_projects_input, dialog_.projects, u8"비우면 모든 카드. 쉼표로 나눕니다", u8"문서의 카드 id를 적습니다"));
                break;
            case editor_field::timeout:
                controls.push_back(add_input(kind_command_editor_timeout_input, dialog_.timeout, u8"비우면 제한 없음 (초)", u8"넘기면 취소와 같은 방식으로 끝냅니다"));
                break;
            case editor_field::flags:
                if (dialog_.hook)
                {
                    controls.push_back(add_button(kind_command_editor_enabled, u8"이 훅 사용", dialog_.enabled, true, make_logic_click(toggle_command_flag_intent { command_flag::hook_enabled })));
                    break;
                }
                controls.push_back(
                    add_button(kind_command_editor_show_on_card, u8"카드 표면에 표시", dialog_.show_on_card, true, make_logic_click(toggle_command_flag_intent { command_flag::show_on_card })));
                controls.push_back(
                    add_button(kind_command_editor_refresh_after, u8"끝난 뒤 새로 고침", dialog_.refresh_after, true, make_logic_click(toggle_command_flag_intent { command_flag::refresh_after })));
                break;
            }
            row_controls_.push_back(std::move(controls));
        }

        confirm_ = add_button(kind_command_editor_confirm, u8"확인", true, dialog_.can_confirm, make_logic_click(confirm_command_editor_intent {}), true);
        cancel_ = add_button(kind_command_editor_cancel, u8"취소", false, true, make_logic_click(close_command_editor_intent {}));
    }

    void command_editor_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);
        panel_->arrange({ context.slot, scale });

        const float left { context.slot.x };
        const float top { context.slot.y };
        const float width { context.slot.width };
        const float height { context.slot.height };
        const float padding { dialog_padding * scale };
        const float field_left { left + padding + layout_command_editor_label_width * scale };
        const float field_right { left + width - padding };
        const float row { layout_command_editor_row_height * scale };
        const float field { field_height * scale };
        const auto centered = [row, field](const float row_y) { return row_y + (row - field) / 2.0f; };

        const std::vector<editor_field> rows { rows_for(dialog_.hook) };
        for (std::size_t index = 0; index < rows.size() && index < row_controls_.size(); ++index)
        {
            const std::vector<ui_element*>& controls { row_controls_[index] };
            if (controls.empty())
                continue;
            const float row_y { centered(top + row_top(index) * scale) };

            switch (rows[index])
            {
            case editor_field::script: {
                if (controls.size() < 3)
                    break;
                // 칸 하나와 버튼 둘이 한 줄을 나눈다. 칸이 남는 폭을 다 쓴다.
                const float button { small_button_width * scale };
                const float gap { 6.0f * scale };
                float input_width { field_right - field_left - (button + gap) * 2.0f };
                if (input_width < 0.0f)
                    input_width = 0.0f;
                controls[0]->arrange({ { field_left, row_y, input_width, field }, scale });
                controls[1]->arrange({ { field_right - button * 2.0f - gap, row_y, button, field }, scale });
                controls[2]->arrange({ { field_right - button, row_y, button, field }, scale });
                break;
            }
            case editor_field::icon: {
                // 오른쪽 끝은 글리프 미리 보기 자리다 (panel이 그린다).
                float input_width { field_right - field_left - (icon_preview_width + 6.0f) * scale };
                if (input_width < 0.0f)
                    input_width = 0.0f;
                controls[0]->arrange({ { field_left, row_y, input_width, field }, scale });
                break;
            }
            case editor_field::phase:
            case editor_field::working_directory:
            case editor_field::target_vcs:
                controls[0]->arrange({ { field_left, row_y, choice_button_width * scale, field }, scale });
                break;
            case editor_field::flags: {
                const float gap { 8.0f * scale };
                float button_left { field_left };
                for (ui_element* const control : controls)
                {
                    control->arrange({ { button_left, row_y, choice_button_width * scale, field }, scale });
                    button_left += choice_button_width * scale + gap;
                }
                break;
            }
            default:
                controls[0]->arrange({ { field_left, row_y, field_right - field_left, field }, scale });
                break;
            }
        }

        const float button_width { action_button_width * scale };
        const float button_height { action_button_height * scale };
        const float button_top { top + height - padding - button_height };
        confirm_->arrange({ { left + width - padding - button_width * 2.0f - 8.0f * scale, button_top, button_width, button_height }, scale });
        cancel_->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
    }

    void command_editor_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        draw_children(context, interaction);
    }

    std::unique_ptr<luil::modal_host_element> make_command_editor_dialog(const command_editor_dialog_view& view, const float window_width, const float window_height)
    {
        float width { layout_command_editor_width };
        if (width > window_width)
            width = window_width;

        // 줄 수가 명령과 훅에서 달라 높이를 그때그때 센다. 메시지 줄 하나와 버튼
        // 줄이 마지막 줄 아래에 붙는다.
        const std::size_t rows { command_editor_row_count(view.hook) };
        float height { row_top(rows) + layout_command_editor_row_height + row_gap + action_button_height + dialog_padding };
        if (height > window_height)
            height = window_height;

        dialog_host_spec spec {};
        spec.owner = std::u8string { dialog_owner_command_editor };
        spec.content_width = width;
        spec.content_height = height;
        // 배경 클릭과 Esc가 모두 닫기다. 환경설정 초안은 그대로 남는다.
        spec.outside = make_logic_click(close_command_editor_intent {});
        spec.dismiss = make_logic_click(close_command_editor_intent {});
        return make_dialog_host(std::move(spec), std::make_unique<command_editor_dialog_element>(view));
    }
} // namespace gitman::ui
