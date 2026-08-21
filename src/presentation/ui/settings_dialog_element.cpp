#include "presentation/ui/settings_dialog_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/ui/dialog_elements.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        // panel의 논리 치수다. 이 dialog에서만 쓰므로 이 파일에 둔다.
        constexpr float panel_width { 460.0f };
        constexpr float panel_height { 440.0f };
        constexpr float panel_padding { 14.0f };
        // 행 배치: 제목 아래에서 시작해 행마다 label 한 줄과 값 한 줄을 그린다.
        constexpr float first_row_top { 40.0f };
        constexpr float row_height { 52.0f };
        constexpr float row_button_width { 68.0f };
        constexpr float row_button_height { 22.0f };
        // 행 버튼·토글을 세부 기능 타이틀 줄과 겹치지 않게 내리는 거리다.
        constexpr float row_control_offset { 15.0f };
        constexpr float timeout_input_width { 140.0f };
        constexpr float toggle_width { 38.0f };
        constexpr float toggle_height { 20.0f };
        constexpr float action_button_width { 88.0f };
        constexpr float action_button_height { 28.0f };
        constexpr std::size_t row_count { 6 };

        // 상태 확인 제한 시간의 숫자 전용 텍스트 박스다 (field-feedback-design
        // 1.3). 키 입력은 dialog가 열려 있는 동안 interaction이 그대로 intent로
        // 보내고 logic이 숫자만 초안에 반영한다. 상태가 없어 클릭·초점 개념이
        // 필요하지 않다 — dialog의 유일한 입력 칸이다.
        class timeout_input_element final : public ui_element
        {
        public:
            explicit timeout_input_element(std::u8string text)
                : ui_element { ui_element_id { ui_element_kind::settings_timeout_input } }
                , text_ { std::move(text) }
            {
                set_tooltip(u8"숫자만 입력합니다 (10~3600초). 비우면 기본값 600초입니다.");
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const bool focused { interaction.focused_input == id() };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 3.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(with_alpha(context.palette.primary_foreground, 0.08f)));
                // 초점을 받은 칸은 테두리를 강조해 입력이 이곳으로 간다는 것을 보인다.
                SkPaint border { solid_paint(focused ? context.palette.positive_accent : with_alpha(context.palette.primary_foreground, 0.35f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                const float text_left { box.x + 7.0f * scale };
                const float baseline { box.y + centered_text_baseline(font, box.height) };

                float caret_left { text_left };
                if (text_.empty())
                {
                    // 빈 값은 기본값이다. 초점이 없을 때만 안내를 흐리게 그린다.
                    if (focused == false)
                    {
                        SkPaint placeholder { solid_paint(context.palette.primary_foreground) };
                        placeholder.setAlphaf(0.5f);
                        draw_text(context.canvas, u8"600 (기본값)", text_left, baseline, font, placeholder);
                    }
                }
                else
                {
                    draw_text(context.canvas, text_, text_left, baseline, font, solid_paint(context.palette.primary_foreground));
                    caret_left += measure_text(text_, font) + 1.0f * scale;
                }

                // caret은 초점을 받은 동안 글 끝에서 깜빡인다. 초점을 받은 시각이
                // 위상 기준이라 받는 순간에는 항상 켜져 있다. 다시 그리기는 UI
                // thread의 caret timer가 반주기마다 일으킨다. 커서 이동은 지원하지
                // 않으므로 위치는 항상 끝이다.
                const bool caret_on { focused && interaction.focus_started_at.has_value() && ((context.now - *interaction.focus_started_at) / caret_blink_interval) % 2 == 0 };
                if (caret_on)
                {
                    const float caret_top { box.y + 4.0f * scale };
                    context.canvas.drawRect(
                        SkRect::MakeXYWH(caret_left, caret_top, 1.0f * scale, box.height - 8.0f * scale), solid_paint(context.palette.primary_foreground));
                }
            }

        private:
            std::u8string text_ {};
        };

        // 켬·끔 상태가 한눈에 보이는 토글 스위치다. 트랙 색과 손잡이 위치가 현재
        // 값을 보여 주고, 클릭이 값을 뒤집는 intent를 낸다.
        class toggle_element final : public ui_element
        {
        public:
            toggle_element(const ui_element_id id, const bool on, std::u8string tooltip, logic_message message)
                : ui_element { id }
                , on_ { on }
            {
                set_tooltip(std::move(tooltip));
                set_action(ui_trigger::left_click, [message = std::move(message)](const ui_action_context&) -> std::vector<input_action> { return { input_action { message } }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const float radius { box.height / 2.0f };

                // 트랙: 켬이면 강조색, 끔이면 흐린 중립색이다. hover 시 살짝 밝힌다.
                const bool hovered { interaction.hovered == id() };
                ui_color track { on_ ? context.palette.positive_accent : context.palette.primary_foreground };
                SkPaint track_paint { solid_paint(track) };
                track_paint.setAlphaf(on_ ? (hovered ? 1.0f : 0.85f) : (hovered ? 0.4f : 0.28f));
                context.canvas.drawRRect(SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), radius, radius), track_paint);

                // 손잡이: 켬이면 오른쪽, 끔이면 왼쪽이다.
                const float knob_radius { radius - 2.0f * scale };
                const float knob_x { on_ ? box.x + box.width - radius : box.x + radius };
                context.canvas.drawCircle(knob_x, box.y + radius, knob_radius, solid_paint(context.palette.surface_background));
            }

        private:
            bool on_ { false };
        };

        // 문서 모드에서 앱 설정을 따르는 행의 값 문구다. 유효 값 앞에 출처를 밝힌다.
        std::u8string with_follows_hint(std::u8string text, const bool follows_app)
        {
            if (follows_app == false)
                return text;
            return std::u8string { u8"앱 설정 따름 · " } + text;
        }

        // panel 배경이다. 클릭을 흡수해 배경 닫기로 흐르지 않게 하고 제목, 행 label,
        // 경로 값, 검증 메시지를 그린다. 버튼은 dialog가 직접 배치하는 자식이다.
        class settings_panel_element final : public ui_element
        {
        public:
            explicit settings_panel_element(const settings_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::settings_dialog_panel } }
                , title_ { dialog.document_mode ? std::u8string { u8"환경설정 (문서)" } : std::u8string { u8"환경설정 (전역)" } }
                , git_path_ { with_follows_hint(dialog.git_path.empty() ? std::u8string { u8"자동 탐색 (지정되지 않음)" } : dialog.git_path, dialog.git_follows_app) }
                , svn_path_ { with_follows_hint(dialog.svn_path.empty() ? std::u8string { u8"자동 탐색 (지정되지 않음)" } : dialog.svn_path, dialog.svn_follows_app) }
                , timeout_follows_ { dialog.timeout_follows_app }
                , submodules_text_ { with_follows_hint(
                      dialog.update_submodules ? std::u8string { u8"켬 - git pull --recurse-submodules=on-demand" } : std::u8string { u8"끔 - submodule을 건드리지 않음" },
                      dialog.submodules_follows_app) }
                , ignore_local_text_ { with_follows_hint(
                      dialog.ignore_local_changes ? std::u8string { u8"켬 - status 확인 없이 깨끗하다고 믿고 진행" } : std::u8string { u8"끔 - 로컬 변경을 확인한 뒤 진행" },
                      dialog.ignore_local_follows_app) }
                , log_files_text_ { with_follows_hint(
                      dialog.write_log_files ? std::u8string { u8"켬 - .<문서>.version-list.log 폴더에 저장소별로 남김" } : std::u8string { u8"끔 - 화면 로그만 유지" },
                      dialog.log_files_follows_app) }
                , message_ { dialog.message }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
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
                const float radius { 5.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.surface_background));
                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const float padding { panel_padding * scale };
                // 제목: `$(settings-gear)` codicon + bold로 제목임을 강조한다.
                float title_left { box.x + padding };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    draw_centered_glyph(context.canvas, codicons::icon_settings_gear, { title_left, box.y + padding - 2.0f * scale, 15.0f * scale, 17.0f * scale }, icon_font,
                        solid_paint(context.palette.primary_foreground));
                    title_left += 19.0f * scale;
                }
                SkFont title_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                title_font.setEmbolden(true);
                static_cast<void>(draw_text_within(
                    context.canvas, title_, title_left, box.y + padding + 11.0f * scale, box.x + box.width - padding - title_left, title_font, solid_paint(context.palette.primary_foreground)));

                draw_row(context, 0, u8"Git 실행 파일", git_path_, u8"자동 탐색 (지정되지 않음)");
                draw_row(context, 1, u8"SVN 실행 파일", svn_path_, u8"자동 탐색 (지정되지 않음)");
                // 대형 저장소는 status만 5~10분 걸릴 수 있어 제한 시간을 문서 단위로
                // 조정한다 (field-feedback-design 1장). 값 칸은 텍스트 박스 element가
                // 대신 그린다.
                draw_row(context, 2, u8"상태 확인 제한 시간 (초)", u8"", u8"");
                // 텍스트 박스 오른쪽에 따름 표시를 그린다 (다른 행은 값 문구에 담긴다).
                if (timeout_follows_)
                {
                    const SkFont hint_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                    SkPaint hint { solid_paint(context.palette.primary_foreground) };
                    hint.setAlphaf(0.45f);
                    const float input_top { box.y + (first_row_top + row_height * 2.0f + 20.0f) * scale };
                    draw_text(context.canvas, u8"앱 설정 따름", box.x + padding + (timeout_input_width + 8.0f) * scale,
                        input_top + centered_text_baseline(hint_font, row_button_height * scale), hint_font, hint);
                }
                // 업데이트마다 묻지 않고 여기서 정한다 (2026-08-20 검수, ADR-003
                // 기본 off 유지).
                draw_row(context, 3, u8"업데이트 시 submodule 갱신", submodules_text_, u8"");
                // 대형 저장소에서 status 순회(로컬 변경 확인)가 분 단위로 걸릴 때 아예
                // 건너뛰는 선택지다. 깨끗하다고 믿고 진행하며 문제는 사후에 알린다.
                // 현재 SVN provider에만 배선되어 있어 문구도 SVN으로 한정한다.
                draw_row(context, 4, u8"로컬 변경을 상관하지 않음 (SVN)", ignore_local_text_, u8"");
                // 카드 로그를 문서 폴더에 파일로 남긴다 (app-shell-design A4).
                draw_row(context, 5, u8"로그를 문서 폴더에 파일로 남김", log_files_text_, u8"");

                if (message_.empty() == false)
                {
                    const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                    const float message_top { box.y + (first_row_top + row_height * static_cast<float>(row_count)) * scale };
                    static_cast<void>(
                        draw_text_within(context.canvas, message_, box.x + padding, message_top + 10.0f * scale, box.width - padding * 2.0f, body_font, solid_paint(context.palette.warning_accent)));
                }

                draw_children(context, interaction);
            }

        private:
            void draw_row(draw_context& context, const std::size_t index, const std::u8string_view label, const std::u8string_view value, const std::u8string_view placeholder) const
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const float padding { panel_padding * scale };
                const float row_top { box.y + (first_row_top + row_height * static_cast<float>(index)) * scale };

                // 세부 기능 타이틀은 키 컬러 + semi-bold로 강조한다.
                SkFont label_font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                label_font.setEmbolden(true);
                static_cast<void>(
                    draw_text_within(context.canvas, label, box.x + padding, row_top + 11.0f * scale, box.width - padding * 2.0f, label_font, solid_paint(context.palette.positive_accent)));

                // 타이틀이 아닌 본문은 흐리게 그려 위계를 만든다. 빈 값은 자동
                // 탐색 안내 문구다 (REQ-017).
                const SkFont value_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                SkPaint value_paint { solid_paint(context.palette.primary_foreground) };
                value_paint.setAlphaf(value.empty() ? 0.45f : 0.65f);
                const std::u8string_view shown { value.empty() ? placeholder : value };
                // 값 줄은 행 버튼(row_button_height) 아래에서 시작해 겹치지 않는다.
                static_cast<void>(draw_text_within(context.canvas, shown, box.x + padding, row_top + 33.0f * scale, box.width - padding * 2.0f, value_font, value_paint));
            }

            std::u8string title_ {};
            std::u8string git_path_ {};
            std::u8string svn_path_ {};
            bool timeout_follows_ { false };
            std::u8string submodules_text_ {};
            std::u8string ignore_local_text_ {};
            std::u8string log_files_text_ {};
            std::u8string message_ {};
        };
    } // namespace

    settings_dialog_element::settings_dialog_element(settings_dialog_view dialog)
        : ui_element { ui_element_id { ui_element_kind::settings_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 배경 클릭은 취소다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_settings_dialog_intent {} } } }; });

        auto panel { std::make_unique<settings_panel_element>(dialog_) };
        panel_ = panel.get();
        add_child(std::move(panel));

        // 행 버튼: 찾아보기는 UI thread의 파일 선택으로, 지우기는 intent로 처리한다.
        auto git_browse { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_git_browse }, std::u8string { u8"찾아보기" }, false) };
        git_browse->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::show_git_executable_picker } }; });
        add_child(std::move(git_browse));

        // 지우기의 의미는 모드에 따라 다르다: 전역은 빈 값(자동 탐색), 문서는 문서
        // 정의를 거둬 앱 설정을 따른다 (G3.2).
        auto git_clear { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_git_clear }, std::u8string { u8"지우기" }, false) };
        git_clear->set_tooltip(dialog_.document_mode ? std::u8string { u8"지우면 앱 설정을 따릅니다" } : std::u8string { u8"지우면 자동 탐색을 사용합니다" });
        git_clear->set_enabled(dialog_.document_mode ? dialog_.git_follows_app == false : dialog_.git_path.empty() == false);
        git_clear->set_action(ui_trigger::left_click,
            [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { clear_settings_executable_intent { repository_kind::git } } } }; });
        add_child(std::move(git_clear));

        auto svn_browse { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_svn_browse }, std::u8string { u8"찾아보기" }, false) };
        svn_browse->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::show_svn_executable_picker } }; });
        add_child(std::move(svn_browse));

        auto svn_clear { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_svn_clear }, std::u8string { u8"지우기" }, false) };
        svn_clear->set_tooltip(dialog_.document_mode ? std::u8string { u8"지우면 앱 설정을 따릅니다" } : std::u8string { u8"지우면 자동 탐색을 사용합니다" });
        svn_clear->set_enabled(dialog_.document_mode ? dialog_.svn_follows_app == false : dialog_.svn_path.empty() == false);
        svn_clear->set_action(ui_trigger::left_click,
            [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { clear_settings_executable_intent { repository_kind::subversion } } } }; });
        add_child(std::move(svn_clear));

        // 상태 확인 제한 시간의 숫자 전용 텍스트 박스다 (field-feedback-design 1.3).
        add_child(std::make_unique<timeout_input_element>(dialog_.timeout_text));

        // submodule 갱신 토글 스위치다 (2026-08-20 검수: 현재 값이 보이는 토글).
        add_child(std::make_unique<toggle_element>(ui_element_id { ui_element_kind::settings_submodules_toggle }, dialog_.update_submodules,
            std::u8string { u8"업데이트 실행 시 submodule을 함께 갱신할지 정합니다" }, logic_message { toggle_settings_submodules_intent {} }));

        // 로컬 변경을 상관하지 않음 토글이다. 대형 저장소의 status 순회를 건너뛴다.
        add_child(std::make_unique<toggle_element>(ui_element_id { ui_element_kind::settings_ignore_local_toggle }, dialog_.ignore_local_changes,
            std::u8string { u8"켜면 SVN 저장소에서 로컬 변경 확인(status)을 건너뛰고 깨끗하다고 믿은 채 조회·업데이트·스위치를 진행합니다 (git에는 적용되지 않음)" },
            logic_message { toggle_settings_ignore_local_intent {} }));

        // 카드 로그를 문서 폴더에 파일로 남기는 토글이다 (app-shell-design A4.5).
        add_child(std::make_unique<toggle_element>(ui_element_id { ui_element_kind::settings_log_files_toggle }, dialog_.write_log_files,
            std::u8string { u8"켜면 문서 폴더에 .<문서>.version-list.log 폴더를 만들고 저장소별로 로그 파일을 남깁니다" }, logic_message { toggle_settings_log_files_intent {} }));

        // file association 등록·제거다 (REQ-016). registry 작업은 UI thread의
        // ui_command로 수행되고 결과는 시스템 dialog로 알린다.
        auto associate { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_associate }, std::u8string { u8"연결 등록" }, false) };
        associate->set_tooltip(u8".version-list 문서를 이 프로그램에 연결합니다 (현재 사용자)");
        associate->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::register_file_association } }; });
        add_child(std::move(associate));

        auto dissociate { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_dissociate }, std::u8string { u8"연결 해제" }, false) };
        dissociate->set_tooltip(u8"이 프로그램이 등록한 .version-list 연결을 제거합니다");
        dissociate->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::unregister_file_association } }; });
        add_child(std::move(dissociate));

        auto confirm { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_dialog_confirm }, std::u8string { u8"저장" }, true) };
        confirm->set_enabled(dialog_.can_confirm);
        confirm->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { confirm_settings_intent {} } } }; });
        add_child(std::move(confirm));

        auto cancel { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_dialog_cancel }, std::u8string { u8"취소" }, false) };
        cancel->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_settings_dialog_intent {} } } }; });
        add_child(std::move(cancel));
    }

    void settings_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { panel_width * scale };
        float height { panel_height * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { panel_padding * scale };
        const std::span<const std::unique_ptr<ui_element>> children { this->children() };
        if (children.size() >= 13)
        {
            // 행 버튼은 행 오른쪽에 두되 세부 기능 타이틀 줄과 겹치지 않게 약간
            // 내린다 (panel의 draw_row 배치와 같은 좌표 기준이다).
            const float row_button { row_button_width * scale };
            const float row_button_gap { 6.0f * scale };
            const float control_offset { row_control_offset * scale };
            for (std::size_t row = 0; row < 2; ++row)
            {
                const float row_top { top + (first_row_top + row_height * static_cast<float>(row)) * scale + control_offset };
                children[1 + row * 2]->arrange({ { left + width - padding - row_button * 2.0f - row_button_gap, row_top, row_button, row_button_height * scale }, scale });
                children[2 + row * 2]->arrange({ { left + width - padding - row_button, row_top, row_button, row_button_height * scale }, scale });
            }

            // 제한 시간 텍스트 박스는 2행의 값 줄 자리에 둔다 (draw_row의 값 줄과
            // 같은 좌표 기준이다).
            const float timeout_row_top { top + (first_row_top + row_height * 2.0f) * scale };
            children[5]->arrange({ { left + padding, timeout_row_top + 20.0f * scale, timeout_input_width * scale, row_button_height * scale }, scale });

            // submodule 토글 스위치도 타이틀 줄 아래로 내려 오른쪽에 둔다.
            const float submodules_row_top { top + (first_row_top + row_height * 3.0f) * scale + control_offset };
            children[6]->arrange({ { left + width - padding - toggle_width * scale, submodules_row_top, toggle_width * scale, toggle_height * scale }, scale });

            // 로컬 변경 무시 토글은 5행이다.
            const float ignore_local_row_top { top + (first_row_top + row_height * 4.0f) * scale + control_offset };
            children[7]->arrange({ { left + width - padding - toggle_width * scale, ignore_local_row_top, toggle_width * scale, toggle_height * scale }, scale });

            // 파일 로그 토글은 6행이다 (app-shell-design A4.5).
            const float log_files_row_top { top + (first_row_top + row_height * 5.0f) * scale + control_offset };
            children[8]->arrange({ { left + width - padding - toggle_width * scale, log_files_row_top, toggle_width * scale, toggle_height * scale }, scale });

            const float button_width { action_button_width * scale };
            const float button_height { action_button_height * scale };
            const float button_top { top + height - padding - button_height };
            // 아래 왼쪽은 연결 등록·해제, 오른쪽은 저장·취소다.
            children[9]->arrange({ { left + padding, button_top, button_width, button_height }, scale });
            children[10]->arrange({ { left + padding + button_width + 8.0f * scale, button_top, button_width, button_height }, scale });
            children[11]->arrange({ { left + width - padding - button_width * 2.0f - 8.0f * scale, button_top, button_width, button_height }, scale });
            children[12]->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
        }
    }

    void settings_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        // 화면 전체를 어둡게 덮어 뒤 내용이 비활성임을 보인다.
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
