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
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gitman::ui {
    namespace {
        // panel의 논리 치수다 (settings-tabs-and-appearance-scope-design S1.3).
        // 이 dialog에서만 쓰므로 이 파일에 둔다.
        constexpr float panel_width { 600.0f };
        constexpr float panel_padding { 14.0f };
        // 제목 아래에서 탭 rail과 내용이 함께 시작한다.
        constexpr float content_top { 44.0f };
        constexpr float rail_width { 132.0f };
        constexpr float rail_row_height { 32.0f };
        constexpr float rail_gap { 14.0f };
        constexpr float content_left { panel_padding + rail_width + rail_gap };
        constexpr float content_width { panel_width - content_left - panel_padding };
        constexpr float section_header_height { 26.0f };
        constexpr float section_gap { 10.0f };
        // 항목은 제목 줄과 값 줄로 이루어진다. 컨트롤은 제목 줄 오른쪽이거나 값 줄
        // 자리다.
        constexpr float item_title_height { 22.0f };
        constexpr float item_value_height { 18.0f };
        constexpr float item_bottom_margin { 8.0f };
        // 제한 시간 항목만 값 줄에 텍스트 박스가 들어가 조금 높다.
        constexpr float timeout_value_height { 26.0f };
        // 문서 모드에서 항목 제목 **바로 위**에 오는 배지 줄이다 (E3). 제목 줄
        // 오른쪽은 행 컨트롤이 그대로 쓴다 — 배지 때문에 조작 요소가 밀리지 않는다.
        constexpr float badge_width { 62.0f };
        constexpr float badge_height { 17.0f };
        constexpr float badge_row_height { 20.0f };
        constexpr float row_button_width { 68.0f };
        constexpr float row_button_height { 22.0f };
        constexpr float row_button_gap { 6.0f };
        constexpr float timeout_input_width { 140.0f };
        constexpr float toggle_width { 38.0f };
        constexpr float toggle_height { 20.0f };
        constexpr float action_button_width { 88.0f };
        constexpr float action_button_height { 28.0f };
        constexpr float action_button_gap { 8.0f };
        // 검증 메시지 자리다. 메시지가 없어도 높이를 비워 두어 버튼 줄이 움직이지
        // 않는다.
        constexpr float message_height { 18.0f };
        constexpr float theme_option_width { 34.0f };
        constexpr float theme_option_height { 22.0f };
        constexpr float swatch_diameter { 24.0f };
        constexpr float swatch_gap { 8.0f };
        constexpr float swatch_top_margin { 6.0f };

        // 항목 종류다. 값 줄의 뜻과 컨트롤 배치를 이것으로 정한다.
        enum class item_kind
        {
            git_executable,
            svn_executable,
            query_timeout,
            update_submodules,
            ignore_local_changes,
            write_log_files,
            theme,
            accent,
            file_association,
        };

        struct item_model
        {
            item_kind kind { item_kind::git_executable };
            std::u8string title {};
            // 값 또는 설명 줄이다. 비어 있으면 placeholder를 흐리게 그린다.
            std::u8string value {};
            std::u8string placeholder {};
            // 문서가 덮어쓸 수 있는 항목만 값을 갖는다 (S4.2).
            std::optional<settings_override_field> override_field {};
            // 배지 element의 정체성이다. 항목마다 다르다.
            std::u8string override_owner {};
            bool overridden { false };
        };

        struct section_model
        {
            std::u8string title {};
            std::vector<item_model> items {};
        };

        struct tab_model
        {
            settings_tab tab { settings_tab::tools };
            std::u8string label {};
            char32_t glyph { 0 };
            std::vector<section_model> sections {};
        };

        // 항목 하나의 사각형이다. 제목 줄과 값 줄은 문서 모드에서 배지 열만큼
        // 들여쓴 위치다.
        struct item_bounds
        {
            rect_f block {};
            rect_f title_line {};
            rect_f value_line {};
            // 제목 바로 위의 배지 자리다. 배지가 없는 항목은 폭이 0이다 (E3).
            rect_f badge {};
            // 행 컨트롤의 오른쪽 한계다. 제목 줄의 오른쪽 끝을 그대로 쓴다.
            float controls_right { 0.0f };
        };

        // 활성 탭의 배치다. panel이 계산해 그리기와 dialog의 컨트롤 배치가 함께 쓴다.
        struct tab_layout
        {
            std::vector<rect_f> section_headers {};
            // 모든 섹션의 항목을 섹션 순서대로 이어 붙인 것이다.
            std::vector<item_bounds> items {};
        };

        std::u8string theme_description(const theme_preference value)
        {
            switch (value)
            {
            case theme_preference::light:
                return std::u8string { u8"라이트 - 밝은 화면" };
            case theme_preference::dark:
                return std::u8string { u8"다크 - 어두운 화면" };
            case theme_preference::system:
            default:
                return std::u8string { u8"시스템 - Windows 앱 모드를 따름" };
            }
        }

        // 색 동그라미가 한 줄에 몇 개 들어가는지다. 격자가 되도록 정사각형에 가깝게
        // `ceil(sqrt(개수))`를 쓰고, 내용 폭이 모자라면 줄인다 (D3). 줄 묶음은 목록
        // 순서가 정한다 — assets/accents.json이 색상환 순서라 한 줄이 한 계열이 된다.
        std::size_t swatch_columns(const std::size_t count, const float available_width)
        {
            const float step { swatch_diameter + swatch_gap };
            const float fitting { (available_width + swatch_gap) / step };
            std::size_t columns { fitting < 1.0f ? 1u : static_cast<std::size_t>(fitting) };
            if (count == 0)
                return columns;

            std::size_t square { 1 };
            while (square * square < count)
                ++square;
            if (square < columns)
                columns = square;
            return columns;
        }

        std::size_t swatch_rows(const std::size_t count, const float available_width)
        {
            if (count == 0)
                return 0;
            const std::size_t columns { swatch_columns(count, available_width) };
            return (count + columns - 1) / columns;
        }

        float value_height_for(const item_kind kind)
        {
            return kind == item_kind::query_timeout ? timeout_value_height : item_value_height;
        }

        // 덮어쓸 수 있는 항목은 문서 모드에서 제목 위에 배지 줄을 하나 갖는다 (E3).
        float badge_row_for(const item_model& item, const bool document_mode)
        {
            return document_mode && item.override_field.has_value() ? badge_row_height : 0.0f;
        }

        // 항목 높이다. 색 격자와 파일 연결 버튼만 기본 높이보다 크다.
        float item_height_for(const item_model& item, const bool document_mode, const float available_width, const std::size_t accent_count)
        {
            const float base { badge_row_for(item, document_mode) + item_title_height + value_height_for(item.kind) };
            if (item.kind == item_kind::accent)
            {
                const std::size_t rows { swatch_rows(accent_count, available_width) };
                const float grid { rows == 0 ? 0.0f : static_cast<float>(rows) * swatch_diameter + static_cast<float>(rows - 1) * swatch_gap };
                return base + swatch_top_margin + grid + item_bottom_margin;
            }
            if (item.kind == item_kind::file_association)
                return base + row_button_gap + action_button_height + item_bottom_margin;
            return base + item_bottom_margin;
        }

        float tab_content_height(const tab_model& tab, const bool document_mode, const std::size_t accent_count)
        {
            float total { 0.0f };
            for (std::size_t index = 0; index < tab.sections.size(); ++index)
            {
                total += section_header_height;
                for (const item_model& item : tab.sections[index].items)
                    total += item_height_for(item, document_mode, content_width, accent_count);
                if (index + 1 < tab.sections.size())
                    total += section_gap;
            }
            return total;
        }

        // 탭을 옮길 때 창이 튀지 않도록 가장 높은 탭에 맞춘다 (S1.3).
        float panel_height_for(const std::vector<tab_model>& tabs, const bool document_mode, const std::size_t accent_count)
        {
            float content { static_cast<float>(tabs.size()) * rail_row_height };
            for (const tab_model& tab : tabs)
            {
                const float height { tab_content_height(tab, document_mode, accent_count) };
                if (height > content)
                    content = height;
            }
            return content_top + content + message_height + action_button_height + panel_padding;
        }

        tab_layout layout_for(const rect_f& panel, const float scale, const tab_model& tab, const bool document_mode, const std::size_t accent_count)
        {
            tab_layout layout {};
            float top { panel.y + content_top * scale };
            for (std::size_t section = 0; section < tab.sections.size(); ++section)
            {
                layout.section_headers.push_back({ panel.x + content_left * scale, top, content_width * scale, section_header_height * scale });
                top += section_header_height * scale;
                for (const item_model& item : tab.sections[section].items)
                {
                    const float height { item_height_for(item, document_mode, content_width, accent_count) * scale };
                    const float badge_row { badge_row_for(item, document_mode) * scale };
                    item_bounds bounds {};
                    bounds.block = { panel.x + content_left * scale, top, content_width * scale, height };
                    // 배지는 항목 제목 바로 위 왼쪽이다 (E3). 없으면 빈 사각형이다.
                    if (badge_row > 0.0f)
                        bounds.badge = { bounds.block.x, top, badge_width * scale, badge_height * scale };
                    bounds.title_line = { bounds.block.x, top + badge_row, content_width * scale, item_title_height * scale };
                    bounds.value_line = { bounds.title_line.x, bounds.title_line.y + item_title_height * scale, bounds.title_line.width, value_height_for(item.kind) * scale };
                    bounds.controls_right = bounds.title_line.x + bounds.title_line.width;
                    layout.items.push_back(bounds);
                    top += height;
                }
                if (section + 1 < tab.sections.size())
                    top += section_gap * scale;
            }
            return layout;
        }

        // 문서가 덮어쓴 항목인지다. 전역 모드에서는 항상 거짓이다.
        bool overridden(const settings_dialog_view& dialog, const bool follows_app)
        {
            return dialog.document_mode && follows_app == false;
        }

        // 문자열은 view로 받는다. 빈 인자(`{}`)가 널 포인터가 되지 않게 한다.
        item_model make_item(const item_kind kind, const std::u8string_view title, std::u8string value, const std::u8string_view placeholder, const std::optional<settings_override_field> field,
            const std::u8string_view owner, const bool overridden_by_document)
        {
            item_model item {};
            item.kind = kind;
            item.title = title;
            item.value = std::move(value);
            item.placeholder = placeholder;
            item.override_field = field;
            item.override_owner = owner;
            item.overridden = overridden_by_document;
            return item;
        }

        std::vector<tab_model> build_tabs(const settings_dialog_view& dialog)
        {
            std::vector<tab_model> tabs {};

            tab_model tools { settings_tab::tools, std::u8string { u8"도구" }, codicons::icon_tools, {} };
            section_model executables { std::u8string { u8"실행 파일" }, {} };
            executables.items.push_back(make_item(item_kind::git_executable, u8"Git 실행 파일", dialog.git_path, u8"자동 탐색 (지정되지 않음)", { settings_override_field::git_executable }, u8"git",
                overridden(dialog, dialog.git_follows_app)));
            executables.items.push_back(make_item(item_kind::svn_executable, u8"SVN 실행 파일", dialog.svn_path, u8"자동 탐색 (지정되지 않음)", { settings_override_field::svn_executable }, u8"svn",
                overridden(dialog, dialog.svn_follows_app)));
            tools.sections.push_back(std::move(executables));
            // 대형 저장소는 status만 5~10분 걸릴 수 있어 제한 시간을 조정한다
            // (field-feedback-design 1장). 값 칸은 텍스트 박스 element가 그린다.
            section_model queries { std::u8string { u8"조회" }, {} };
            queries.items.push_back(
                make_item(item_kind::query_timeout, u8"상태 확인 제한 시간 (초)", {}, {}, { settings_override_field::query_timeout }, u8"timeout", overridden(dialog, dialog.timeout_follows_app)));
            tools.sections.push_back(std::move(queries));
            tabs.push_back(std::move(tools));

            tab_model operations { settings_tab::operations, std::u8string { u8"작업" }, codicons::icon_sync, {} };
            // 업데이트마다 묻지 않고 여기서 정한다 (2026-08-20 검수, ADR-003 기본 off
            // 유지).
            section_model update { std::u8string { u8"업데이트" }, {} };
            std::u8string submodules_value { dialog.update_submodules ? u8"켬 - git pull --recurse-submodules=on-demand" : u8"끔 - submodule을 건드리지 않음" };
            update.items.push_back(make_item(item_kind::update_submodules, u8"업데이트 시 submodule 갱신", std::move(submodules_value), {}, { settings_override_field::update_submodules },
                u8"submodules", overridden(dialog, dialog.submodules_follows_app)));
            operations.sections.push_back(std::move(update));
            // 대형 저장소에서 status 순회(로컬 변경 확인)가 분 단위로 걸릴 때 아예
            // 건너뛰는 선택지다. 현재 SVN provider에만 배선되어 있어 문구도 SVN으로
            // 한정한다.
            section_model status { std::u8string { u8"상태 확인" }, {} };
            std::u8string ignore_local_value { dialog.ignore_local_changes ? u8"켬 - status 확인 없이 깨끗하다고 믿고 진행" : u8"끔 - 로컬 변경을 확인한 뒤 진행" };
            status.items.push_back(make_item(item_kind::ignore_local_changes, u8"로컬 변경을 상관하지 않음 (SVN)", std::move(ignore_local_value), {}, { settings_override_field::ignore_local_changes },
                u8"ignore-local", overridden(dialog, dialog.ignore_local_follows_app)));
            operations.sections.push_back(std::move(status));
            // 카드 로그를 문서 폴더에 파일로 남긴다 (app-shell-design A4).
            section_model logs { std::u8string { u8"로그" }, {} };
            std::u8string log_files_value { dialog.write_log_files ? u8"켬 - .<문서>.version-list.log 폴더에 저장소별로 남김" : u8"끔 - 화면 로그만 유지" };
            logs.items.push_back(make_item(item_kind::write_log_files, u8"로그를 문서 폴더에 파일로 남김", std::move(log_files_value), {}, { settings_override_field::write_log_files }, u8"log-files",
                overridden(dialog, dialog.log_files_follows_app)));
            operations.sections.push_back(std::move(logs));
            tabs.push_back(std::move(operations));

            // 탭 이름은 `테마`다 (E1). 열거자 이름(`appearance`)은 저장 값·id로 이미
            // 쓰이고 있어 표시 문자열만 바꾼다.
            tab_model appearance { settings_tab::appearance, std::u8string { u8"테마" }, codicons::icon_symbol_color, {} };
            section_model theme { std::u8string { u8"테마" }, {} };
            theme.items.push_back(
                make_item(item_kind::theme, u8"테마", theme_description(dialog.theme), {}, { settings_override_field::theme }, u8"theme", overridden(dialog, dialog.theme_follows_app)));
            appearance.sections.push_back(std::move(theme));
            section_model accent { std::u8string { u8"키 컬러" }, {} };
            accent.items.push_back(make_item(item_kind::accent, u8"키 컬러", std::u8string { accent_for(dialog.accent_id).label }, {}, { settings_override_field::accent }, u8"accent",
                overridden(dialog, dialog.accent_follows_app)));
            appearance.sections.push_back(std::move(accent));
            tabs.push_back(std::move(appearance));

            // 파일 연결은 문서가 아니라 현재 사용자 registry의 상태다 (REQ-016).
            tab_model system { settings_tab::system, std::u8string { u8"시스템" }, codicons::icon_link, {} };
            section_model association { std::u8string { u8"파일 연결" }, {} };
            association.items.push_back(make_item(item_kind::file_association, u8".version-list 파일 연결", u8"현재 사용자 범위에서만 등록·해제합니다", {}, {}, u8"", false));
            system.sections.push_back(std::move(association));
            tabs.push_back(std::move(system));

            return tabs;
        }

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
                SkPaint border { solid_paint(focused ? context.palette.accent : with_alpha(context.palette.primary_foreground, 0.35f)) };
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
                    context.canvas.drawRect(SkRect::MakeXYWH(caret_left, caret_top, 1.0f * scale, box.height - 8.0f * scale), solid_paint(context.palette.primary_foreground));
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
                ui_color track { on_ ? (hovered ? context.palette.accent_hover : context.palette.accent) : context.palette.primary_foreground };
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

        // 왼쪽 탭 rail의 한 칸이다 (S1.2). 고른 탭만 강조 바탕과 강조 글자를 갖는다.
        class tab_item_element final : public ui_element
        {
        public:
            tab_item_element(const settings_tab tab, const bool selected, const char32_t glyph, std::u8string label)
                : ui_element { ui_element_id { ui_element_kind::settings_tab_item, project_id { std::u8string { settings_tab_name(tab) } } } }
                , selected_ { selected }
                , glyph_ { glyph }
                , label_ { std::move(label) }
            {
                set_action(ui_trigger::left_click, [tab](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { select_settings_tab_intent { tab } } } }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const bool hovered { interaction.hovered == id() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 4.0f * scale };
                if (selected_)
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(with_alpha(context.palette.accent_soft, 0.30f)));
                else if (hovered)
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.button_hover_background));

                const ui_color foreground { selected_ ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground };
                float text_left { box.x + 10.0f * scale };
                if (context.codicon_typeface != nullptr)
                {
                    SkPaint icon { solid_paint(foreground) };
                    if (selected_ == false)
                        icon.setAlphaf(hovered ? 0.9f : 0.7f);
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    draw_centered_glyph(context.canvas, glyph_, { text_left, box.y, 16.0f * scale, box.height }, icon_font, icon);
                    text_left += 22.0f * scale;
                }

                SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                if (selected_)
                    font.setEmbolden(true);
                SkPaint text { solid_paint(foreground) };
                if (selected_ == false)
                    text.setAlphaf(hovered ? 0.95f : 0.75f);
                static_cast<void>(draw_text_within(context.canvas, label_, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - text_left - 6.0f * scale, font, text));
            }

        private:
            bool selected_ { false };
            char32_t glyph_ { 0 };
            std::u8string label_ {};
        };

        // 테마 세 값을 붙여 놓은 세그먼트 토글의 한 칸이다. 고른 칸만 강조 바탕과
        // 강조 글자색을 갖는다. 아이콘은 codicon이다.
        class theme_option_element final : public ui_element
        {
        public:
            theme_option_element(const theme_preference value, const bool selected, const char32_t glyph, std::u8string tooltip)
                : ui_element { ui_element_id { ui_element_kind::settings_theme_option, project_id { std::u8string { theme_preference_name(value) } } } }
                , selected_ { selected }
                , glyph_ { glyph }
            {
                set_tooltip(std::move(tooltip));
                set_action(
                    ui_trigger::left_click, [value](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { set_theme_preference_intent { value } } } }; });
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
                const float radius { 3.0f * scale };
                const bool hovered { interaction.hovered == id() };

                ui_color background { with_alpha(context.palette.primary_foreground, 0.08f) };
                if (selected_)
                    background = with_alpha(context.palette.accent_soft, 0.30f);
                else if (hovered)
                    background = context.palette.button_hover_background;
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(background));

                if (context.codicon_typeface == nullptr)
                    return;
                SkPaint icon { solid_paint(selected_ ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
                if (selected_ == false)
                    icon.setAlphaf(hovered ? 0.85f : 0.6f);
                const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                draw_centered_glyph(context.canvas, glyph_, box, icon_font, icon);
            }

        private:
            bool selected_ { false };
            char32_t glyph_ { 0 };
        };

        // 키 컬러 하나를 고르는 색 동그라미다. 고른 색만 바깥 링을 갖는다.
        class accent_swatch_element final : public ui_element
        {
        public:
            accent_swatch_element(const accent_definition& accent, const bool selected)
                : ui_element { ui_element_id { ui_element_kind::settings_accent_swatch, project_id { std::u8string { accent.id } } } }
                , swatch_ { accent.swatch }
                , selected_ { selected }
            {
                set_tooltip(std::u8string { accent.label });
                set_action(ui_trigger::left_click,
                    [id = std::u8string { accent.id }](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { set_accent_intent { id } } } }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const float center_x { box.x + box.width / 2.0f };
                const float center_y { box.y + box.height / 2.0f };
                const float outer { (box.width < box.height ? box.width : box.height) / 2.0f };

                // 고른 색은 바깥 링으로, hover는 옅은 링으로 알린다.
                if (selected_ || interaction.hovered == id())
                {
                    SkPaint ring { solid_paint(selected_ ? context.palette.accent_emphasis_foreground : with_alpha(context.palette.primary_foreground, 0.45f)) };
                    ring.setStyle(SkPaint::kStroke_Style);
                    ring.setStrokeWidth(1.5f * scale);
                    context.canvas.drawCircle(center_x, center_y, outer - 1.0f * scale, ring);
                }
                context.canvas.drawCircle(center_x, center_y, outer - 4.0f * scale, solid_paint(swatch_));
            }

        private:
            ui_color swatch_ { 0 };
            bool selected_ { false };
        };

        // 항목 제목 줄 오른쪽 끝의 범위 배지다 (D2). 문서가 정의한 항목은
        // `문서 설정`이고 누르면 정의를 거둔다. 정의하지 않은 항목은 비활성 색
        // `전역 설정`이며 누를 수 없다.
        class override_badge_element final : public ui_element
        {
        public:
            override_badge_element(std::u8string owner, const settings_override_field field, const bool defined)
                : ui_element { ui_element_id { ui_element_kind::settings_override_badge, project_id { std::move(owner) } } }
                , defined_ { defined }
            {
                if (defined == false)
                {
                    set_tooltip(u8"앱 전역 설정을 따릅니다. 값을 바꾸면 이 문서만의 설정이 됩니다.");
                    return;
                }
                set_tooltip(u8"이 문서가 전역 설정을 덮어쓰고 있습니다. 클릭하면 삭제하고 전역 설정을 따릅니다.");
                set_action(
                    ui_trigger::left_click, [field](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { clear_settings_override_intent { field } } } }; });
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
                const float radius { box.height / 2.0f };
                const bool hovered { interaction.hovered == id() };
                if (defined_)
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(with_alpha(context.palette.accent_soft, hovered ? 0.34f : 0.22f)));

                const ui_color border_color { defined_ ? with_alpha(context.palette.accent, hovered ? 0.9f : 0.55f) : with_alpha(context.palette.primary_foreground, 0.25f) };
                SkPaint border { solid_paint(border_color) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                SkFont font { sk_ref_sp(context.ui_typeface), 10.0f * scale };
                if (defined_)
                    font.setEmbolden(true);
                // 전역을 따르는 항목은 비활성 색이라 배지가 앞으로 나오지 않는다.
                SkPaint text_paint { solid_paint(defined_ ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
                if (defined_ == false)
                    text_paint.setAlphaf(0.45f);
                const std::u8string_view text { defined_ ? u8"문서 설정" : u8"전역 설정" };
                const float width { measure_text(text, font) };
                draw_text(context.canvas, text, box.x + (box.width - width) / 2.0f, box.y + centered_text_baseline(font, box.height), font, text_paint);
            }

        private:
            bool defined_ { false };
        };

        // panel 배경이다. 클릭을 흡수해 배경 닫기로 흐르지 않게 하고 제목, 탭 rail의
        // 구분선, 섹션 제목과 항목의 글자를 그린다. 컨트롤은 dialog가 이 element가
        // 계산한 배치 위에 얹는 자식이다.
        class settings_panel_element final : public ui_element
        {
        public:
            settings_panel_element(const settings_dialog_view& dialog, tab_model active, const std::size_t accent_count)
                : ui_element { ui_element_id { ui_element_kind::settings_dialog_panel } }
                , title_ { dialog.document_mode ? std::u8string { u8"환경설정 (문서)" } : std::u8string { u8"환경설정 (전역)" } }
                , document_mode_ { dialog.document_mode }
                , active_ { std::move(active) }
                , accent_count_ { accent_count }
                , message_ { dialog.message }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
            }

            void arrange(const arrange_context& context) override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                set_bounds(context.slot);
                layout_ = layout_for(context.slot, scale, active_, document_mode_, accent_count_);
            }

            [[nodiscard]] const tab_layout& layout() const noexcept
            {
                return layout_;
            }

            [[nodiscard]] const tab_model& active() const noexcept
            {
                return active_;
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

                draw_title(context, box, scale);
                draw_rail_separator(context, box, scale);
                draw_sections(context, scale);
                draw_message(context, box, scale);
                draw_children(context, interaction);
            }

        private:
            void draw_title(draw_context& context, const rect_f& box, const float scale) const
            {
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
            }

            void draw_rail_separator(draw_context& context, const rect_f& box, const float scale) const
            {
                const float line_x { box.x + (content_left - rail_gap / 2.0f) * scale };
                const float top { box.y + content_top * scale };
                const float bottom { box.y + box.height - (panel_padding + action_button_height + message_height) * scale };
                if (bottom <= top)
                    return;
                context.canvas.drawRect(SkRect::MakeXYWH(line_x, top, 1.0f * scale, bottom - top), solid_paint(with_alpha(context.palette.primary_foreground, 0.12f)));
            }

            void draw_sections(draw_context& context, const float scale) const
            {
                SkFont header_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                header_font.setEmbolden(true);
                SkFont title_font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                title_font.setEmbolden(true);
                const SkFont value_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };

                std::size_t item_index { 0 };
                for (std::size_t section = 0; section < active_.sections.size(); ++section)
                {
                    if (section >= layout_.section_headers.size())
                        break;
                    const rect_f header { layout_.section_headers[section] };
                    SkPaint header_paint { solid_paint(context.palette.primary_foreground) };
                    header_paint.setAlphaf(0.55f);
                    static_cast<void>(draw_text_within(context.canvas, active_.sections[section].title, header.x, header.y + 13.0f * scale, header.width, header_font, header_paint));
                    // 섹션 제목 아래 실선이 항목 묶음의 경계를 만든다.
                    context.canvas.drawRect(
                        SkRect::MakeXYWH(header.x, header.y + header.height - 6.0f * scale, header.width, 1.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.12f)));

                    for (const item_model& item : active_.sections[section].items)
                    {
                        if (item_index >= layout_.items.size())
                            break;
                        draw_item(context, scale, item, layout_.items[item_index], title_font, value_font);
                        ++item_index;
                    }
                }
            }

            void draw_item(draw_context& context, const float scale, const item_model& item, const item_bounds& bounds, const SkFont& title_font, const SkFont& value_font) const
            {
                // 덮어쓴 항목은 블록 자체에 옅은 강조 바탕을 깔아 배지와 이어 보인다
                // (S4.2).
                if (item.overridden)
                {
                    const SkRect block { SkRect::MakeXYWH(bounds.block.x, bounds.block.y, bounds.block.width, bounds.block.height - 4.0f * scale) };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(block, 4.0f * scale, 4.0f * scale), solid_paint(with_alpha(context.palette.accent_soft, 0.10f)));
                }

                // 세부 기능 타이틀은 키 컬러 + semi-bold로 강조한다.
                static_cast<void>(draw_text_within(context.canvas, item.title, bounds.title_line.x, bounds.title_line.y + 15.0f * scale, bounds.title_line.width, title_font,
                    solid_paint(context.palette.accent_emphasis_foreground)));

                // 타이틀이 아닌 본문은 흐리게 그려 위계를 만든다. 빈 값은 안내 문구다.
                if (item.value.empty() && item.placeholder.empty())
                    return;
                SkPaint value_paint { solid_paint(context.palette.primary_foreground) };
                value_paint.setAlphaf(item.value.empty() ? 0.45f : 0.65f);
                const std::u8string& shown { item.value.empty() ? item.placeholder : item.value };
                static_cast<void>(draw_text_within(context.canvas, shown, bounds.value_line.x, bounds.value_line.y + 13.0f * scale, bounds.value_line.width, value_font, value_paint));
            }

            void draw_message(draw_context& context, const rect_f& box, const float scale) const
            {
                if (message_.empty())
                    return;
                const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                const float top { box.y + box.height - (panel_padding + action_button_height + message_height) * scale };
                static_cast<void>(draw_text_within(
                    context.canvas, message_, box.x + panel_padding * scale, top + 12.0f * scale, box.width - panel_padding * 2.0f * scale, body_font, solid_paint(context.palette.warning_accent)));
            }

            std::u8string title_ {};
            bool document_mode_ { false };
            tab_model active_ {};
            std::size_t accent_count_ { 0 };
            std::u8string message_ {};
            tab_layout layout_ {};
        };
    } // namespace

    settings_dialog_element::settings_dialog_element(settings_dialog_view dialog)
        : ui_element { ui_element_id { ui_element_kind::settings_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 배경 클릭은 취소다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_settings_dialog_intent {} } } }; });

        const std::vector<tab_model> tabs { build_tabs(dialog_) };
        const std::size_t accent_count { accent_catalog().size() };
        panel_height_ = panel_height_for(tabs, dialog_.document_mode, accent_count);

        const tab_model* active { &tabs.front() };
        for (const tab_model& tab : tabs)
            if (tab.tab == dialog_.active_tab)
                active = &tab;

        auto panel { std::make_unique<settings_panel_element>(dialog_, *active, accent_count) };
        panel_ = panel.get();
        add_child(std::move(panel));

        for (const tab_model& tab : tabs)
        {
            auto item { std::make_unique<tab_item_element>(tab.tab, tab.tab == active->tab, tab.glyph, tab.label) };
            tab_items_.push_back(item.get());
            add_child(std::move(item));
        }

        // 활성 탭의 항목만 컨트롤을 만든다. 다른 탭의 값은 dialog 초안에 그대로 남아
        // 있고, 탭을 옮기면 그 탭의 컨트롤이 만들어진다 (S1.2).
        const auto add_toggle = [this](const ui_element_kind kind, const bool on, const char8_t* const tooltip, logic_message message, std::vector<ui_element*>& controls) {
            auto toggle { std::make_unique<toggle_element>(ui_element_id { kind }, on, std::u8string { tooltip }, std::move(message)) };
            controls.push_back(toggle.get());
            add_child(std::move(toggle));
        };

        const auto add_text_button = [this](const ui_element_kind kind, const char8_t* const text, std::vector<ui_element*>& controls) -> text_button_element* {
            auto button { std::make_unique<text_button_element>(ui_element_id { kind }, std::u8string { text }, false) };
            text_button_element* const raw { button.get() };
            controls.push_back(raw);
            add_child(std::move(button));
            return raw;
        };

        for (const section_model& section : active->sections)
            for (const item_model& item : section.items)
            {
                const std::size_t index { item_controls_.size() };
                std::vector<ui_element*> controls {};
                switch (item.kind)
                {
                case item_kind::git_executable:
                case item_kind::svn_executable: {
                    const bool git { item.kind == item_kind::git_executable };
                    // 찾아보기는 UI thread의 파일 선택으로, 지우기는 intent로 처리한다.
                    text_button_element* const browse { add_text_button(git ? ui_element_kind::settings_git_browse : ui_element_kind::settings_svn_browse, u8"찾아보기", controls) };
                    const ui_command picker { git ? ui_command::show_git_executable_picker : ui_command::show_svn_executable_picker };
                    browse->set_action(ui_trigger::left_click, [picker](const ui_action_context&) -> std::vector<input_action> { return { input_action { picker } }; });

                    // 지우기는 두 모드 모두 빈 값(자동 탐색)이다. 문서 정의 삭제는
                    // `덮어씀` 배지의 몫이다 (S4.2).
                    text_button_element* const clear { add_text_button(git ? ui_element_kind::settings_git_clear : ui_element_kind::settings_svn_clear, u8"지우기", controls) };
                    clear->set_tooltip(u8"지우면 자동 탐색을 사용합니다");
                    clear->set_enabled(item.value.empty() == false);
                    const repository_kind tool { git ? repository_kind::git : repository_kind::subversion };
                    clear->set_action(ui_trigger::left_click,
                        [tool](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { clear_settings_executable_intent { tool } } } }; });
                    break;
                }
                case item_kind::query_timeout: {
                    auto input { std::make_unique<timeout_input_element>(dialog_.timeout_text) };
                    controls.push_back(input.get());
                    add_child(std::move(input));
                    break;
                }
                case item_kind::update_submodules:
                    // 업데이트 실행 overlay 대신 이 토글이 값을 정한다 (2026-08-20 검수).
                    add_toggle(ui_element_kind::settings_submodules_toggle, dialog_.update_submodules, u8"업데이트 실행 시 submodule을 함께 갱신할지 정합니다",
                        logic_message { toggle_settings_submodules_intent {} }, controls);
                    break;
                case item_kind::ignore_local_changes:
                    add_toggle(ui_element_kind::settings_ignore_local_toggle, dialog_.ignore_local_changes,
                        u8"켜면 SVN 저장소에서 로컬 변경 확인(status)을 건너뛰고 깨끗하다고 믿은 채 조회·업데이트·스위치를 진행합니다 (git에는 적용되지 않음)",
                        logic_message { toggle_settings_ignore_local_intent {} }, controls);
                    break;
                case item_kind::write_log_files:
                    add_toggle(ui_element_kind::settings_log_files_toggle, dialog_.write_log_files, u8"켜면 문서 폴더에 .<문서>.version-list.log 폴더를 만들고 저장소별로 로그 파일을 남깁니다",
                        logic_message { toggle_settings_log_files_intent {} }, controls);
                    break;
                case item_kind::theme: {
                    const auto add_theme = [this](const theme_preference value, const char32_t glyph, const char8_t* const tooltip) {
                        auto option { std::make_unique<theme_option_element>(value, dialog_.theme == value, glyph, std::u8string { tooltip }) };
                        theme_options_.push_back(option.get());
                        add_child(std::move(option));
                    };
                    // codicon에는 sun/moon이 없어 의미가 가장 가까운 3종을 골랐다
                    // (theme-and-banner-menu-design 검수 결정).
                    add_theme(theme_preference::light, codicons::icon_lightbulb, u8"밝은 화면을 씁니다");
                    add_theme(theme_preference::system, codicons::icon_device_desktop, u8"Windows의 앱 모드를 따릅니다");
                    add_theme(theme_preference::dark, codicons::icon_color_mode, u8"어두운 화면을 씁니다");
                    break;
                }
                case item_kind::accent: {
                    for (const accent_definition& accent : accent_catalog())
                    {
                        auto swatch { std::make_unique<accent_swatch_element>(accent, accent.id == dialog_.accent_id) };
                        swatches_.push_back(swatch.get());
                        add_child(std::move(swatch));
                    }
                    break;
                }
                case item_kind::file_association: {
                    // registry 작업은 UI thread의 ui_command로 수행되고 결과는 시스템
                    // dialog로 알린다 (REQ-016).
                    text_button_element* const associate { add_text_button(ui_element_kind::settings_associate, u8"연결 등록", controls) };
                    associate->set_tooltip(u8".version-list 문서를 이 프로그램에 연결합니다 (현재 사용자)");
                    associate->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::register_file_association } }; });

                    text_button_element* const dissociate { add_text_button(ui_element_kind::settings_dissociate, u8"연결 해제", controls) };
                    dissociate->set_tooltip(u8"이 프로그램이 등록한 .version-list 연결을 제거합니다");
                    dissociate->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::unregister_file_association } }; });
                    break;
                }
                }
                item_controls_.push_back(std::move(controls));

                // 문서 모드에서는 덮어쓸 수 있는 모든 항목이 범위 배지를 갖는다 (D2).
                // 배치는 항목 index로 하므로 자식 순서에 매이지 않는다.
                if (dialog_.document_mode && item.override_field.has_value())
                {
                    auto badge { std::make_unique<override_badge_element>(item.override_owner, *item.override_field, item.overridden) };
                    badges_.push_back({ index, badge.get() });
                    add_child(std::move(badge));
                }
            }

        auto confirm { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_dialog_confirm }, std::u8string { u8"저장" }, true) };
        confirm->set_enabled(dialog_.can_confirm);
        confirm->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { confirm_settings_intent {} } } }; });
        confirm_ = confirm.get();
        add_child(std::move(confirm));

        auto cancel { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::settings_dialog_cancel }, std::u8string { u8"취소" }, false) };
        cancel->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_settings_dialog_intent {} } } }; });
        cancel_ = cancel.get();
        add_child(std::move(cancel));
    }

    void settings_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { panel_width * scale };
        float height { panel_height_ * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        const float padding { panel_padding * scale };
        panel_->arrange({ { left, top, width, height }, scale });

        float rail_top { top + content_top * scale };
        for (ui_element* const item : tab_items_)
        {
            item->arrange({ { left + padding, rail_top, rail_width * scale, rail_row_height * scale }, scale });
            rail_top += rail_row_height * scale;
        }

        const auto* const panel { static_cast<const settings_panel_element*>(panel_) };
        const tab_layout& layout { panel->layout() };

        // 항목 컨트롤은 제목 줄 오른쪽이거나 값 줄 자리다. 좌표는 panel이 계산한 항목
        // 사각형을 그대로 쓴다 (S1.3).
        const auto place_item = [&](const item_model& item, const item_bounds& bounds, const std::vector<ui_element*>& controls) {
            const float title_center { bounds.title_line.y + (bounds.title_line.height - row_button_height * scale) / 2.0f };
            switch (item.kind)
            {
            case item_kind::git_executable:
            case item_kind::svn_executable: {
                if (controls.size() < 2)
                    break;
                const float button { row_button_width * scale };
                const float right { bounds.controls_right };
                controls[0]->arrange({ { right - button * 2.0f - row_button_gap * scale, title_center, button, row_button_height * scale }, scale });
                controls[1]->arrange({ { right - button, title_center, button, row_button_height * scale }, scale });
                break;
            }
            case item_kind::query_timeout: {
                if (controls.empty())
                    break;
                const float input_top { bounds.value_line.y + (bounds.value_line.height - row_button_height * scale) / 2.0f };
                controls[0]->arrange({ { bounds.value_line.x, input_top, timeout_input_width * scale, row_button_height * scale }, scale });
                break;
            }
            case item_kind::update_submodules:
            case item_kind::ignore_local_changes:
            case item_kind::write_log_files: {
                if (controls.empty())
                    break;
                const float toggle_top { bounds.title_line.y + (bounds.title_line.height - toggle_height * scale) / 2.0f };
                controls[0]->arrange({ { bounds.controls_right - toggle_width * scale, toggle_top, toggle_width * scale, toggle_height * scale }, scale });
                break;
            }
            case item_kind::theme: {
                // 세그먼트 세 칸이 제목 줄 오른쪽에 붙어 있다.
                const float option_width { theme_option_width * scale };
                const float option_top { bounds.title_line.y + (bounds.title_line.height - theme_option_height * scale) / 2.0f };
                float option_left { bounds.controls_right - option_width * static_cast<float>(theme_options_.size()) };
                for (ui_element* const option : theme_options_)
                {
                    option->arrange({ { option_left, option_top, option_width, theme_option_height * scale }, scale });
                    option_left += option_width;
                }
                break;
            }
            case item_kind::accent: {
                // 색 동그라미는 값 줄 아래의 격자다. 한 줄에 담기지 않으면 다음 줄로
                // 내려간다 (S2.4).
                const float diameter { swatch_diameter * scale };
                const float gap { swatch_gap * scale };
                const std::size_t columns { swatch_columns(swatches_.size(), bounds.title_line.width / scale) };
                const float grid_top { bounds.value_line.y + bounds.value_line.height + swatch_top_margin * scale };
                for (std::size_t index = 0; index < swatches_.size(); ++index)
                {
                    const float column { static_cast<float>(index % columns) };
                    const float row { static_cast<float>(index / columns) };
                    swatches_[index]->arrange({ { bounds.title_line.x + column * (diameter + gap), grid_top + row * (diameter + gap), diameter, diameter }, scale });
                }
                break;
            }
            case item_kind::file_association: {
                if (controls.size() < 2)
                    break;
                const float button { action_button_width * scale };
                const float button_top { bounds.value_line.y + bounds.value_line.height + row_button_gap * scale };
                controls[0]->arrange({ { bounds.value_line.x, button_top, button, action_button_height * scale }, scale });
                controls[1]->arrange({ { bounds.value_line.x + button + action_button_gap * scale, button_top, button, action_button_height * scale }, scale });
                break;
            }
            }
        };

        std::size_t item_index { 0 };
        for (const section_model& section : panel->active().sections)
            for (const item_model& item : section.items)
            {
                if (item_index >= layout.items.size() || item_index >= item_controls_.size())
                    break;
                place_item(item, layout.items[item_index], item_controls_[item_index]);
                ++item_index;
            }

        // 배지는 항목 제목 왼쪽의 고정 열이다 (S4.2).
        for (const auto& [index, badge] : badges_)
        {
            if (index >= layout.items.size())
                continue;
            const item_bounds& bounds { layout.items[index] };
            if (bounds.badge.width <= 0.0f)
                continue;
            badge->arrange({ bounds.badge, scale });
        }

        const float button_width { action_button_width * scale };
        const float button_height { action_button_height * scale };
        const float button_top { top + height - padding - button_height };
        confirm_->arrange({ { left + width - padding - button_width * 2.0f - action_button_gap * scale, button_top, button_width, button_height }, scale });
        cancel_->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
    }

    void settings_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        // 화면 전체를 어둡게 덮어 뒤 내용이 비활성임을 보인다.
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
