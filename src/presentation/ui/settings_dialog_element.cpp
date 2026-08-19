#include "presentation/ui/settings_dialog_element.h"

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
        // panel의 논리 치수다. 이 dialog에서만 쓰므로 이 파일에 둔다.
        constexpr float panel_width { 460.0f };
        constexpr float panel_height { 232.0f };
        constexpr float panel_padding { 14.0f };
        // 행 배치: 제목 아래에서 시작해 행마다 label 한 줄과 경로 한 줄을 그린다.
        constexpr float first_row_top { 40.0f };
        constexpr float row_height { 52.0f };
        constexpr float row_button_width { 68.0f };
        constexpr float row_button_height { 22.0f };
        constexpr float action_button_width { 88.0f };
        constexpr float action_button_height { 28.0f };

        // panel 배경이다. 클릭을 흡수해 배경 닫기로 흐르지 않게 하고 제목, 행 label,
        // 경로 값, 검증 메시지를 그린다. 버튼은 dialog가 직접 배치하는 자식이다.
        class settings_panel_element final : public ui_element
        {
        public:
            explicit settings_panel_element(const settings_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::settings_dialog_panel } }
                , git_path_ { dialog.git_path }
                , svn_path_ { dialog.svn_path }
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
                const SkFont title_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                static_cast<void>(draw_text_within(
                    context.canvas, u8"환경설정", box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font, solid_paint(context.palette.primary_foreground)));

                draw_row(context, 0, u8"Git 실행 파일", git_path_);
                draw_row(context, 1, u8"SVN 실행 파일", svn_path_);

                if (message_.empty() == false)
                {
                    const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                    const float message_top { box.y + (first_row_top + row_height * 2.0f) * scale };
                    static_cast<void>(
                        draw_text_within(context.canvas, message_, box.x + padding, message_top + 10.0f * scale, box.width - padding * 2.0f, body_font, solid_paint(context.palette.warning_accent)));
                }

                draw_children(context, interaction);
            }

        private:
            void draw_row(draw_context& context, const std::size_t index, const std::u8string_view label, const std::u8string_view path) const
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const float padding { panel_padding * scale };
                const float row_top { box.y + (first_row_top + row_height * static_cast<float>(index)) * scale };

                const SkFont label_font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                static_cast<void>(
                    draw_text_within(context.canvas, label, box.x + padding, row_top + 11.0f * scale, box.width - padding * 2.0f, label_font, solid_paint(context.palette.primary_foreground)));

                // 빈 값은 자동 탐색이다 (REQ-017). 값 대신 안내 문구를 흐리게 그린다.
                const SkFont value_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                SkPaint value_paint { solid_paint(context.palette.primary_foreground) };
                value_paint.setAlphaf(path.empty() ? 0.5f : 0.8f);
                const std::u8string_view value { path.empty() ? std::u8string_view { u8"자동 탐색 (지정되지 않음)" } : path };
                // 값 줄은 행 버튼(row_button_height) 아래에서 시작해 겹치지 않는다.
                static_cast<void>(draw_text_within(context.canvas, value, box.x + padding, row_top + 33.0f * scale, box.width - padding * 2.0f, value_font, value_paint));
            }

            std::u8string git_path_ {};
            std::u8string svn_path_ {};
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

        auto git_clear { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_git_clear }, std::u8string { u8"지우기" }, false) };
        git_clear->set_tooltip(u8"지우면 자동 탐색을 사용합니다");
        git_clear->set_enabled(dialog_.git_path.empty() == false);
        git_clear->set_action(ui_trigger::left_click,
            [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { clear_settings_executable_intent { repository_kind::git } } } }; });
        add_child(std::move(git_clear));

        auto svn_browse { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_svn_browse }, std::u8string { u8"찾아보기" }, false) };
        svn_browse->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::show_svn_executable_picker } }; });
        add_child(std::move(svn_browse));

        auto svn_clear { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_svn_clear }, std::u8string { u8"지우기" }, false) };
        svn_clear->set_tooltip(u8"지우면 자동 탐색을 사용합니다");
        svn_clear->set_enabled(dialog_.svn_path.empty() == false);
        svn_clear->set_action(ui_trigger::left_click,
            [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { clear_settings_executable_intent { repository_kind::subversion } } } }; });
        add_child(std::move(svn_clear));

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
        if (children.size() >= 9)
        {
            // 행 버튼은 행 label 줄의 오른쪽에 나란히 둔다 (panel의 draw_row 배치와
            // 같은 좌표 기준이다).
            const float row_button { row_button_width * scale };
            const float row_button_gap { 6.0f * scale };
            for (std::size_t row = 0; row < 2; ++row)
            {
                const float row_top { top + (first_row_top + row_height * static_cast<float>(row)) * scale };
                children[1 + row * 2]->arrange({ { left + width - padding - row_button * 2.0f - row_button_gap, row_top, row_button, row_button_height * scale }, scale });
                children[2 + row * 2]->arrange({ { left + width - padding - row_button, row_top, row_button, row_button_height * scale }, scale });
            }

            const float button_width { action_button_width * scale };
            const float button_height { action_button_height * scale };
            const float button_top { top + height - padding - button_height };
            // 아래 왼쪽은 연결 등록·해제, 오른쪽은 저장·취소다.
            children[5]->arrange({ { left + padding, button_top, button_width, button_height }, scale });
            children[6]->arrange({ { left + padding + button_width + 8.0f * scale, button_top, button_width, button_height }, scale });
            children[7]->arrange({ { left + width - padding - button_width * 2.0f - 8.0f * scale, button_top, button_width, button_height }, scale });
            children[8]->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
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
