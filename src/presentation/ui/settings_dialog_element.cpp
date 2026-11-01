#include "presentation/ui/settings_dialog_element.h"

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

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
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
        // 항목 블록의 안쪽 여백이다 (2026-08-23 지시). 문서 모드에서 덮어쓴 항목은
        // 블록에 옅은 키 컬러를 까는데, 배지와 버튼이 그 가장자리에 딱 붙으면
        // 빡빡해 보인다. 모든 항목에 같은 값을 주어 행끼리 어긋나지 않게 한다.
        constexpr float item_top_margin { 6.0f };
        constexpr float item_bottom_margin { 10.0f };
        constexpr float item_inner_padding { 8.0f };
        // 강조 블록끼리 붙지 않게 남기는 세로 간격이다. 블록 높이에서 이만큼 덜
        // 칠한다 — 아래쪽 여백은 `item_bottom_margin`에서 이 값을 뺀 만큼이다.
        constexpr float item_block_gap { 4.0f };
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
        // panel 높이에서 항목 열이 아닌 부분이다 — 제목 줄, 메시지 줄, 버튼 줄과
        // 아래 여백이다. logic의 스크롤 한계와 element의 배치가 같은 값을 쓴다 (E1).
        constexpr float chrome_height { content_top + message_height + action_button_height + panel_padding };
        constexpr float theme_option_width { 34.0f };
        constexpr float theme_option_height { 22.0f };
        constexpr float swatch_diameter { 24.0f };
        constexpr float swatch_gap { 8.0f };
        constexpr float swatch_top_margin { 6.0f };

        // 항목 종류다. 값 줄의 뜻과 컨트롤 배치를 이것으로 정한다.
        // 글꼴 항목의 값 줄이다. 빈 이름은 내장 글꼴을 뜻한다.
        std::u8string font_value(const std::u8string_view family)
        {
            return family.empty() ? std::u8string { u8"(기본 글꼴)" } : std::u8string { family };
        }

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
            ui_font,
            code_font,
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

        // 항목 블록 안에서 글과 컨트롤이 실제로 쓰는 폭이다. 좌우 안쪽 여백을 뺀
        // 값이며, 높이 계산과 배치가 **같은 값**을 써야 색 격자가 겹치지 않는다.
        float item_content_width(const float column_width)
        {
            const float width { column_width - item_inner_padding * 2.0f };
            return width > 0.0f ? width : 0.0f;
        }

        // 항목 높이다. 색 격자와 파일 연결 버튼만 기본 높이보다 크다.
        // `available_width`는 `item_content_width`를 거친 값이다.
        float item_height_for(const item_model& item, const bool document_mode, const float available_width, const std::size_t accent_count)
        {
            const float base { item_top_margin + badge_row_for(item, document_mode) + item_title_height + value_height_for(item.kind) };
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

        float tab_content_height(const tab_model& tab, const bool document_mode, const std::size_t accent_count, const float column_width)
        {
            float total { 0.0f };
            for (std::size_t index = 0; index < tab.sections.size(); ++index)
            {
                total += section_header_height;
                for (const item_model& item : tab.sections[index].items)
                    total += item_height_for(item, document_mode, item_content_width(column_width), accent_count);
                if (index + 1 < tab.sections.size())
                    total += section_gap;
            }
            return total;
        }

        // 탭을 옮길 때 창이 튀지 않도록 가장 높은 탭에 맞춘다 (S1.3). 탭 rail도
        // 같은 열 높이를 차지하므로 함께 센다.
        float tallest_content_height(const std::vector<tab_model>& tabs, const bool document_mode, const std::size_t accent_count, const float column_width)
        {
            float content { static_cast<float>(tabs.size()) * rail_row_height };
            for (const tab_model& tab : tabs)
            {
                const float height { tab_content_height(tab, document_mode, accent_count, column_width) };
                if (height > content)
                    content = height;
            }
            return content;
        }

        // 지금 보고 있는 탭이다. 없는 값이면 첫 탭이다 (탭 목록은 항상 비지 않는다).
        const tab_model& active_tab(const std::vector<tab_model>& tabs, const settings_tab wanted)
        {
            for (const tab_model& tab : tabs)
                if (tab.tab == wanted)
                    return tab;
            return tabs.front();
        }

        // 스크롤 막대가 차지하는 논리 폭이다. 막대가 보이면 항목 열이 그만큼 좁아진다.
        constexpr float scrollbar_inset { layout_scrollbar_hit_width };

        // 항목 열이 실제로 보이는 높이다. 창이 낮으면 chrome을 뺀 나머지이고, 한 줄도
        // 못 보일 만큼 낮으면 그 한 줄만큼은 남긴다.
        float viewport_height_for(const float content_height, const float window_height)
        {
            // 창 위아래 여백을 뺀 나머지가 panel이 쓸 수 있는 높이다 (2026-08-23
            // 지시). 배치도 같은 함수를 쓴다.
            const float usable { dialog_available_height(window_height) };
            const float wanted { chrome_height + content_height };
            const float panel { wanted < usable ? wanted : usable };
            const float viewport { panel - chrome_height };
            return viewport > item_title_height ? viewport : item_title_height;
        }

        // `right_inset`은 스크롤 막대가 차지하는 논리 폭이다. 막대가 없으면 0이다.
        tab_layout layout_for(
            const rect_f& panel, const float scale, const tab_model& tab, const bool document_mode, const std::size_t accent_count, const float scroll_offset, const float right_inset)
        {
            tab_layout layout {};
            const float column_width { content_width - right_inset };
            // 스크롤은 항목 열만 움직인다. 제목·탭 rail·버튼 줄은 제자리다 (E1.2).
            float top { panel.y + (content_top - scroll_offset) * scale };
            for (std::size_t section = 0; section < tab.sections.size(); ++section)
            {
                layout.section_headers.push_back({ panel.x + content_left * scale, top, column_width * scale, section_header_height * scale });
                top += section_header_height * scale;
                for (const item_model& item : tab.sections[section].items)
                {
                    const float inner_width { item_content_width(column_width) };
                    const float height { item_height_for(item, document_mode, inner_width, accent_count) * scale };
                    const float badge_row { badge_row_for(item, document_mode) * scale };
                    item_bounds bounds {};
                    bounds.block = { panel.x + content_left * scale, top, column_width * scale, height };

                    // 글과 컨트롤은 블록 안쪽 여백만큼 들어와 앉는다. 덮어쓴 항목의
                    // 강조 바탕이 배지·버튼에 딱 붙지 않는다 (2026-08-23 지시).
                    const float inner_left { bounds.block.x + item_inner_padding * scale };
                    const float inner_top { top + item_top_margin * scale };
                    // 배지는 항목 제목 바로 위 왼쪽이다 (E3). 없으면 빈 사각형이다.
                    if (badge_row > 0.0f)
                        bounds.badge = { inner_left, inner_top, badge_width * scale, badge_height * scale };
                    bounds.title_line = { inner_left, inner_top + badge_row, inner_width * scale, item_title_height * scale };
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
            accent.items.push_back(make_item(item_kind::accent, u8"키 컬러", std::u8string { luil::accent_for(dialog.accent_id).label }, {}, { settings_override_field::accent }, u8"accent",
                overridden(dialog, dialog.accent_follows_app)));
            appearance.sections.push_back(std::move(accent));
            // 글꼴도 문서가 덮어쓸 수 있다 (2026-08-23 지시). 테마·키 컬러와 같은
            // 배지 규칙을 쓴다.
            section_model fonts { std::u8string { u8"글꼴" }, {} };
            fonts.items.push_back(make_item(item_kind::ui_font, u8"UI 글꼴", font_value(dialog.ui_font_family), {}, settings_override_field::ui_font, u8"ui-font",
                overridden(dialog, dialog.ui_font_follows_app)));
            fonts.items.push_back(make_item(item_kind::code_font, u8"코드 글꼴", font_value(dialog.code_font_family), {}, settings_override_field::code_font, u8"code-font",
                overridden(dialog, dialog.code_font_follows_app)));
            appearance.sections.push_back(std::move(fonts));
            tabs.push_back(std::move(appearance));

            // 파일 연결은 문서가 아니라 현재 사용자 registry의 상태다 (REQ-016).
            tab_model system { settings_tab::system, std::u8string { u8"시스템" }, codicons::icon_link, {} };
            section_model association { std::u8string { u8"파일 연결" }, {} };
            association.items.push_back(make_item(item_kind::file_association, u8".version-list 파일 연결", u8"현재 사용자 범위에서만 등록·해제합니다", {}, {}, u8"", false));
            system.sections.push_back(std::move(association));
            tabs.push_back(std::move(system));

            return tabs;
        }

        // 켬·끔 상태가 한눈에 보이는 토글 스위치다. 트랙 색과 손잡이 위치가 현재
        // 값을 보여 주고, 클릭이 값을 뒤집는 intent를 낸다.
        class toggle_element final : public ui_element
        {
        public:
            toggle_element(ui_element_id id, const bool on, std::u8string tooltip, logic_message message)
                : ui_element { std::move(id) }
                , on_ { on }
            {
                set_tooltip(std::move(tooltip));
                set_action(ui_trigger::left_click, make_logic_click(std::move(message)));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            // 켬·끔을 보조 기술이 읽는다. 이름은 항목 제목이다 (`set_access_name`).
            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::toggle_switch, .name = access_name(), .checked = on_ };
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
                : ui_element { ui_element_id { kind_settings_tab_item, std::u8string { settings_tab_name(tab) } } }
                , selected_ { selected }
                , glyph_ { glyph }
                , label_ { std::move(label) }
            {
                set_action(ui_trigger::left_click, make_logic_click(select_settings_tab_intent { tab }));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            // 툴팁이 없어 기본 구현으로는 접근 tree에서 접혔다. 라벨이 이름이다.
            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::tab, .name = label_, .selected = selected_ };
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
                    const SkFont glyph_font { icon_font(context, 13.0f) };
                    draw_centered_glyph(context.canvas, glyph_, { text_left, box.y, 16.0f * scale, box.height }, glyph_font, icon);
                    text_left += 22.0f * scale;
                }

                const SkFont font { ui_font(context, 12.0f, selected_ ? luil::font_weight::bold : luil::font_weight::regular) };
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
                : ui_element { ui_element_id { kind_settings_theme_option, std::u8string { theme_preference_name(value) } } }
                , selected_ { selected }
                , glyph_ { glyph }
            {
                set_tooltip(std::move(tooltip));
                set_action(ui_trigger::left_click, make_logic_click(set_theme_preference_intent { value }));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::radio_button, .name = access_name(), .selected = selected_ };
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
                const SkFont glyph_font { icon_font(context, 13.0f) };
                draw_centered_glyph(context.canvas, glyph_, box, glyph_font, icon);
            }

        private:
            bool selected_ { false };
            char32_t glyph_ { 0 };
        };

        // 키 컬러 하나를 고르는 색 동그라미다. 고른 색만 바깥 링을 갖는다.
        class accent_swatch_element final : public ui_element
        {
        public:
            accent_swatch_element(const luil::accent_definition& accent, const bool selected)
                : ui_element { ui_element_id { kind_settings_accent_swatch, std::u8string { accent.id } } }
                , swatch_ { accent.swatch }
                , selected_ { selected }
            {
                set_tooltip(std::u8string { accent.label });
                set_action(ui_trigger::left_click, make_logic_click(set_accent_intent { std::u8string { accent.id } }));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::radio_button, .name = access_name(), .selected = selected_ };
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
                : ui_element { ui_element_id { kind_settings_override_badge, std::move(owner) } }
                , defined_ { defined }
            {
                if (defined == false)
                {
                    set_tooltip(u8"앱 전역 설정을 따릅니다. 값을 바꾸면 이 문서만의 설정이 됩니다.");
                    return;
                }
                set_tooltip(u8"이 문서가 전역 설정을 덮어쓰고 있습니다. 클릭하면 삭제하고 전역 설정을 따릅니다.");
                set_action(ui_trigger::left_click, make_logic_click(clear_settings_override_intent { field }));
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

                const SkFont font { ui_font(context, 10.0f, defined_ ? luil::font_weight::bold : luil::font_weight::regular) };
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

        // panel 배경이다. 제목, 탭 rail의 구분선, 섹션 제목과 항목의 글자를 그린다.
        // 컨트롤은 dialog가 이 element가 계산한 배치 위에 얹는 자식이다.
        class settings_panel_element final : public ui_element
        {
        public:
            settings_panel_element(const settings_dialog_view& dialog, tab_model active, const std::size_t accent_count, const float right_inset)
                : ui_element { ui_element_id { kind_settings_dialog_panel } }
                , title_ { dialog.document_mode ? std::u8string { u8"환경설정 (문서)" } : std::u8string { u8"환경설정 (전역)" } }
                , document_mode_ { dialog.document_mode }
                , active_ { std::move(active) }
                , accent_count_ { accent_count }
                , message_ { dialog.message }
                , scroll_offset_ { dialog.scroll_offset }
                , right_inset_ { right_inset }
            {
                // 흡수는 host가 content(본체)에 건다 — panel이 다시 걸 필요가 없다.
            }

            // 탭 rail과 항목 컨트롤은 panel의 자식이다. 그래야 viewport 하나로
            // 잘라 그리고 hit test도 같은 경계를 쓴다 (E1.2).
            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            // 탭 rail과 항목 열이 함께 보이는 영역이다. 창이 낮으면 여기부터 줄어들고
            // 제목·메시지·버튼 줄은 제자리에 남는다. dialog가 배치하며 정한다.
            rect_f content_viewport {};

            void arrange(const arrange_context& context) override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                set_bounds(context.slot);
                layout_ = layout_for(context.slot, scale, active_, document_mode_, accent_count_, scroll_offset_, right_inset_);
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
                draw_message(context, box, scale);

                // 항목과 그 컨트롤은 viewport 안에서만 보인다. 창이 낮아 내용이
                // 넘치더라도 버튼 줄을 뚫고 나가지 않는다 (E1.1이 지적한 깨짐).
                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(content_viewport.x, content_viewport.y, content_viewport.width, content_viewport.height));
                draw_sections(context, scale);
                draw_children(context, interaction);
                context.canvas.restore();
            }

            // 자를 자리가 자기 bounds가 아니라 `content_viewport`(제목·버튼 줄을 뺀
            // 항목 열)라 `set_clip_children`으로는 대신할 수 없다. 나머지 규칙은
            // luil 기본 hit test와 같다 — 자식은 역순, 마지막에 흡수다.
            const ui_element* hit_test(const float x, const float y) const override
            {
                if (visible() == false)
                    return nullptr;
                // 잘려서 보이지 않는 컨트롤은 눌리지도 않아야 한다.
                if (content_viewport.contains(x, y))
                {
                    const std::span<const std::unique_ptr<ui_element>> items { children() };
                    for (std::size_t index = items.size(); index > 0; --index)
                        if (const ui_element* const hit { items[index - 1]->hit_test(x, y) }; hit != nullptr)
                            return hit;
                }
                if ((interactive() || hit_opaque()) && bounds().contains(x, y))
                    return this;
                return nullptr;
            }

        private:
            void draw_title(draw_context& context, const rect_f& box, const float scale) const
            {
                const float padding { panel_padding * scale };
                // 제목: `$(settings-gear)` codicon + bold로 제목임을 강조한다.
                float title_left { box.x + padding };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont glyph_font { icon_font(context, 13.0f) };
                    draw_centered_glyph(context.canvas, codicons::icon_settings_gear, { title_left, box.y + padding - 2.0f * scale, 15.0f * scale, 17.0f * scale }, glyph_font,
                        solid_paint(context.palette.primary_foreground));
                    title_left += 19.0f * scale;
                }
                const SkFont title_font { ui_font(context, 13.0f, luil::font_weight::bold) };
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
                const SkFont header_font { ui_font(context, 11.0f, luil::font_weight::bold) };
                const SkFont title_font { ui_font(context, 12.0f, luil::font_weight::bold) };
                const SkFont value_font { ui_font(context, 11.0f) };

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
                    const SkRect block { SkRect::MakeXYWH(bounds.block.x, bounds.block.y, bounds.block.width, bounds.block.height - item_block_gap * scale) };
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
                const SkFont body_font { ui_font(context, 11.0f) };
                const float top { box.y + box.height - (panel_padding + action_button_height + message_height) * scale };
                static_cast<void>(draw_text_within(
                    context.canvas, message_, box.x + panel_padding * scale, top + 12.0f * scale, box.width - panel_padding * 2.0f * scale, body_font, solid_paint(context.palette.warning_accent)));
            }

            std::u8string title_ {};
            bool document_mode_ { false };
            tab_model active_ {};
            std::size_t accent_count_ { 0 };
            std::u8string message_ {};
            float scroll_offset_ { 0.0f };
            float right_inset_ { 0.0f };
            tab_layout layout_ {};
        };
    } // namespace

    settings_dialog_scroll_metrics settings_dialog_metrics(const settings_dialog_view& dialog, const float window_height)
    {
        const std::vector<tab_model> tabs { build_tabs(dialog) };
        const std::size_t accent_count { luil::accent_catalog().size() };

        // panel 크기는 가장 높은 탭에 맞춰 고정한다 (S1.3). 탭을 옮길 때 dialog가
        // 커졌다 작아졌다 하면 읽기 어렵다.
        settings_dialog_scroll_metrics metrics {};
        metrics.panel_content_height = tallest_content_height(tabs, dialog.document_mode, accent_count, content_width);
        metrics.viewport_height = viewport_height_for(metrics.panel_content_height, window_height);

        // 스크롤 한계는 **지금 보고 있는 탭**의 내용으로 센다 (2026-08-23 지시).
        // 가장 높은 탭으로 세면 짧은 탭에서 빈 공간까지 스크롤된다. 탭을 누르면
        // 다음 snapshot이 이 함수를 다시 거치므로 그 시점에 새로 계산된다.
        const tab_model& active { active_tab(tabs, dialog.active_tab) };

        // 두 번 센다. 막대가 보이면 항목 열이 좁아지고, 좁아지면 색 격자가 한 줄 더
        // 늘 수 있다. 막대가 필요한지 먼저 판정한 뒤 그 폭으로 다시 센다.
        metrics.content_height = tab_content_height(active, dialog.document_mode, accent_count, content_width);
        if (metrics.content_height > metrics.viewport_height)
        {
            metrics.content_height = tab_content_height(active, dialog.document_mode, accent_count, content_width - scrollbar_inset);
            metrics.maximum_scroll = metrics.content_height - metrics.viewport_height;
            if (metrics.maximum_scroll < 0.0f)
                metrics.maximum_scroll = 0.0f;
        }
        return metrics;
    }

    settings_dialog_element::settings_dialog_element(settings_dialog_view dialog, const float window_height)
        : ui_element { ui_element_id { kind_settings_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 배경 클릭(취소)은 host의 scrim이 맡는다 — 본체에는 액션을 걸지 않는다
        // (설계 D2-A). 누름 흡수도 host의 `set_content`가 걸어 준다.
        // 본체는 판일 뿐이라 Tab 자리가 아니다. Tab은 탭 rail·토글·배지·버튼 같은
        // dialog 컨트롤만 돈다 (설계 D1-B).
        set_tab_stop(false);

        const std::vector<tab_model> tabs { build_tabs(dialog_) };
        const std::size_t accent_count { luil::accent_catalog().size() };
        metrics_ = settings_dialog_metrics(dialog_, window_height);

        const tab_model& active { active_tab(tabs, dialog_.active_tab) };

        auto panel { std::make_unique<settings_panel_element>(dialog_, active, accent_count, metrics_.maximum_scroll > 0.0f ? scrollbar_inset : 0.0f) };
        settings_panel_element* const panel_raw { panel.get() };
        panel_ = panel_raw;
        add_child(std::move(panel));

        // 탭 rail과 항목 컨트롤은 panel의 자식이다. panel이 viewport 하나로 잘라
        // 그리므로 창이 낮아도 버튼 줄을 침범하지 않는다 (E1.2).
        for (const tab_model& tab : tabs)
        {
            auto item { std::make_unique<tab_item_element>(tab.tab, tab.tab == active.tab, tab.glyph, tab.label) };
            tab_items_.push_back(item.get());
            panel_raw->adopt(std::move(item));
        }

        // 활성 탭의 항목만 컨트롤을 만든다. 다른 탭의 값은 dialog 초안에 그대로 남아
        // 있고, 탭을 옮기면 그 탭의 컨트롤이 만들어진다 (S1.2).
        // 토글·입력 칸의 접근 이름은 항목 제목이다. 툴팁은 설명이라 이름으로 읽히면 길다.
        // 글자 버튼(찾아보기·지우기)은 luil이 적힌 글을 이름으로 쓴다.
        const auto add_toggle = [panel_raw](const ui_element_kind kind, const bool on, const char8_t* const tooltip, logic_message message, std::vector<ui_element*>& controls,
                                    const std::u8string& title) {
            auto toggle { std::make_unique<toggle_element>(ui_element_id { kind }, on, std::u8string { tooltip }, std::move(message)) };
            toggle->set_access_name(title);
            controls.push_back(toggle.get());
            panel_raw->adopt(std::move(toggle));
        };

        const auto add_text_button = [panel_raw](const ui_element_kind kind, const char8_t* const text, std::vector<ui_element*>& controls) -> luil::text_button_element* {
            auto button { std::make_unique<luil::text_button_element>(ui_element_id { kind }, luil::text_button_config { .text = std::u8string { text } }) };
            luil::text_button_element* const raw { button.get() };
            controls.push_back(raw);
            panel_raw->adopt(std::move(button));
            return raw;
        };

        for (const section_model& section : active.sections)
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
                    luil::text_button_element* const browse { add_text_button(git ? kind_settings_git_browse : kind_settings_svn_browse, u8"찾아보기", controls) };
                    const app_command picker { git ? app_command::show_git_executable_picker : app_command::show_svn_executable_picker };
                    browse->set_action(ui_trigger::left_click, make_app_command_click(picker));

                    // 지우기는 두 모드 모두 빈 값(자동 탐색)이다. 문서 정의 삭제는
                    // `덮어씀` 배지의 몫이다 (S4.2).
                    luil::text_button_element* const clear { add_text_button(git ? kind_settings_git_clear : kind_settings_svn_clear, u8"지우기", controls) };
                    clear->set_tooltip(u8"지우면 자동 탐색을 사용합니다");
                    clear->set_enabled(item.value.empty() == false);
                    const repository_kind tool { git ? repository_kind::git : repository_kind::subversion };
                    clear->set_action(ui_trigger::left_click, make_logic_click(clear_settings_executable_intent { tool }));
                    break;
                }
                case item_kind::query_timeout: {
                    // 숫자 전용 초안은 logic이 거른다 (handle_edit_settings_timeout).
                    auto input {
                        std::make_unique<luil::text_input_element>(
                            ui_element_id { kind_settings_timeout_input }, dialog_.timeout, luil::text_input_config { .placeholder = std::u8string { u8"600 (기본값)" } }),
                    };
                    input->set_tooltip(u8"숫자만 입력합니다 (10~3600초). 비우면 기본값 600초입니다.");
                    input->set_access_name(item.title);
                    controls.push_back(input.get());
                    panel_raw->adopt(std::move(input));
                    break;
                }
                case item_kind::update_submodules:
                    // 업데이트 실행 overlay 대신 이 토글이 값을 정한다 (2026-08-20 검수).
                    add_toggle(kind_settings_submodules_toggle, dialog_.update_submodules, u8"업데이트 실행 시 submodule을 함께 갱신할지 정합니다",
                        logic_message { toggle_settings_submodules_intent {} }, controls, item.title);
                    break;
                case item_kind::ignore_local_changes:
                    add_toggle(kind_settings_ignore_local_toggle, dialog_.ignore_local_changes,
                        u8"켜면 SVN 저장소에서 로컬 변경 확인(status)을 건너뛰고 깨끗하다고 믿은 채 조회·업데이트·스위치를 진행합니다 (git에는 적용되지 않음)",
                        logic_message { toggle_settings_ignore_local_intent {} }, controls, item.title);
                    break;
                case item_kind::write_log_files:
                    add_toggle(kind_settings_log_files_toggle, dialog_.write_log_files, u8"켜면 문서 폴더에 .<문서>.version-list.log 폴더를 만들고 저장소별로 로그 파일을 남깁니다",
                        logic_message { toggle_settings_log_files_intent {} }, controls, item.title);
                    break;
                case item_kind::theme: {
                    const auto add_theme = [this, panel_raw](const theme_preference value, const char32_t glyph, const char8_t* const tooltip, const char8_t* const name) {
                        auto option { std::make_unique<theme_option_element>(value, dialog_.theme == value, glyph, std::u8string { tooltip }) };
                        option->set_access_name(name);
                        theme_options_.push_back(option.get());
                        panel_raw->adopt(std::move(option));
                    };
                    // codicon에는 sun/moon이 없어 의미가 가장 가까운 3종을 골랐다
                    // (theme-and-banner-menu-design 검수 결정).
                    add_theme(theme_preference::light, codicons::icon_lightbulb, u8"밝은 화면을 씁니다", u8"밝게");
                    add_theme(theme_preference::system, codicons::icon_device_desktop, u8"Windows의 앱 모드를 따릅니다", u8"시스템 설정 따르기");
                    add_theme(theme_preference::dark, codicons::icon_color_mode, u8"어두운 화면을 씁니다", u8"어둡게");
                    break;
                }
                case item_kind::accent: {
                    for (const luil::accent_definition& accent : luil::accent_catalog())
                    {
                        auto swatch { std::make_unique<accent_swatch_element>(accent, accent.id == dialog_.accent_id) };
                        swatches_.push_back(swatch.get());
                        panel_raw->adopt(std::move(swatch));
                    }
                    break;
                }
                case item_kind::ui_font:
                case item_kind::code_font: {
                    const bool code { item.kind == item_kind::code_font };
                    luil::text_button_element* const pick {
                        add_text_button(code ? kind_settings_code_font_pick : kind_settings_ui_font_pick, u8"선택…", controls),
                    };
                    pick->set_tooltip(code ? u8"로그와 diff 본문에 쓸 글꼴을 고릅니다" : u8"창 전체에 쓸 글꼴을 고릅니다");
                    pick->set_action(ui_trigger::left_click, make_logic_click(open_font_dialog_intent { code }));
                    break;
                }
                case item_kind::file_association: {
                    // registry 작업은 UI thread의 앱 명령으로 수행되고 결과는 시스템
                    // dialog로 알린다 (REQ-016).
                    luil::text_button_element* const associate { add_text_button(kind_settings_associate, u8"연결 등록", controls) };
                    associate->set_tooltip(u8".version-list 문서를 이 프로그램에 연결합니다 (현재 사용자)");
                    associate->set_action(ui_trigger::left_click, make_app_command_click(app_command::register_file_association));

                    luil::text_button_element* const dissociate { add_text_button(kind_settings_dissociate, u8"연결 해제", controls) };
                    dissociate->set_tooltip(u8"이 프로그램이 등록한 .version-list 연결을 제거합니다");
                    dissociate->set_action(ui_trigger::left_click, make_app_command_click(app_command::unregister_file_association));
                    break;
                }
                }
                item_controls_.push_back(std::move(controls));

                // 문서 모드에서는 덮어쓸 수 있는 모든 항목이 범위 배지를 갖는다 (D2).
                // 배치는 항목 index로 하므로 자식 순서에 매이지 않는다.
                if (dialog_.document_mode && item.override_field.has_value())
                {
                    auto badge { std::make_unique<override_badge_element>(item.override_owner, *item.override_field, item.overridden) };
                    badge->set_access_name(item.title + u8" 문서 설정 지우기");
                    badges_.push_back({ index, badge.get() });
                    panel_raw->adopt(std::move(badge));
                }
            }

        auto confirm {
            std::make_unique<luil::text_button_element>(
                ui_element_id { kind_settings_dialog_confirm },
                // Enter의 기본 자리다 — 제한 시간 칸에서 Enter를 쳐도 저장으로 간다
                // (enter-default-design.md).
                luil::text_button_config { .text = std::u8string { u8"저장" }, .visual = luil::text_button_visual::accent, .default_button = true }),
        };
        confirm->set_enabled(dialog_.can_confirm);
        confirm->set_action(ui_trigger::left_click, make_logic_click(confirm_settings_intent {}));
        confirm_ = confirm.get();
        add_child(std::move(confirm));

        auto cancel { std::make_unique<luil::text_button_element>(ui_element_id { kind_settings_dialog_cancel }, luil::text_button_config { .text = std::u8string { u8"취소" } }) };
        cancel->set_action(ui_trigger::left_click, make_logic_click(cancel_settings_dialog_intent {}));
        cancel_ = cancel.get();
        add_child(std::move(cancel));

        // 항목이 창에 다 들어가지 않을 때만 막대를 만든다. 다른 dialog와 같은
        // element를 쓰고 폭도 같다 (E1.4). 치수는 전부 논리 픽셀이고 배율은 막대의
        // arrange가 곱한다.
        auto bar {
            std::make_unique<luil::scrollbar_element>(ui_element_id { kind_scrollbar, std::u8string { scrollbar_owner_settings_dialog } },
                luil::scrollbar_config {
                    .scroll = [](const float delta) -> input_action { return make_logic_action(settings_scroll_intent { delta }); },
                    .content_height = metrics_.content_height,
                    .viewport_height = metrics_.viewport_height,
                    .scroll_offset = dialog_.scroll_offset,
                }),
        };
        // 목록에 딸린 막대는 Tab 자리가 아니다 (luil list_element와 같은 관례, D1-B).
        bar->set_tab_stop(false);
        bar->set_visible(metrics_.maximum_scroll > 0.0f);
        scrollbar_ = bar.get();
        add_child(std::move(bar));
    }

    void settings_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        // `context.slot`이 곧 panel 상자다 — host가 `make_settings_dialog`가 준 논리
        // 크기를 창 가운데에 놓아 건네준다 (설계 D2-A). 창보다 크지 않게 줄이는
        // 일도 factory가 이미 마쳤다.
        const float left { context.slot.x };
        const float top { context.slot.y };
        const float width { context.slot.width };
        const float height { context.slot.height };
        const float padding { panel_padding * scale };

        auto* const panel { static_cast<settings_panel_element*>(panel_) };
        // 항목 열과 탭 rail이 함께 보이는 영역이다. 창이 낮으면 여기부터 줄어들고
        // 제목·메시지·버튼 줄은 제자리에 남는다 (E1.2).
        const float viewport_top { top + content_top * scale };
        float viewport_height { height - chrome_height * scale };
        if (viewport_height < item_title_height * scale)
            viewport_height = item_title_height * scale;
        panel->content_viewport = { left + padding, viewport_top, width - padding * 2.0f, viewport_height };
        panel->arrange({ { left, top, width, height }, scale });

        // 탭 rail은 스크롤하지 않는다. 항목만 흐른다.
        float rail_top { viewport_top };
        for (ui_element* const item : tab_items_)
        {
            item->arrange({ { left + padding, rail_top, rail_width * scale, rail_row_height * scale }, scale });
            rail_top += rail_row_height * scale;
        }

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
            case item_kind::ui_font:
            case item_kind::code_font:
                // `선택…` 버튼 하나가 제목 줄 오른쪽 끝에 붙는다.
                if (controls.empty() == false)
                {
                    const float button { action_button_width * scale };
                    const float button_top { bounds.title_line.y + (bounds.title_line.height - action_button_height * scale) / 2.0f };
                    controls.front()->arrange({ { bounds.controls_right - button, button_top, button, action_button_height * scale }, scale });
                }
                break;
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

        // 막대는 항목 열의 오른쪽 끝, viewport 세로 범위에 붙는다.
        const float bar_width { layout_scrollbar_hit_width * scale };
        scrollbar_->arrange({ { left + width - padding - bar_width, viewport_top, bar_width, viewport_height }, scale });

        const float button_width { action_button_width * scale };
        const float button_height { action_button_height * scale };
        const float button_top { top + height - padding - button_height };
        confirm_->arrange({ { left + width - padding - button_width * 2.0f - action_button_gap * scale, button_top, button_width, button_height }, scale });
        cancel_->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
    }

    void settings_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 화면을 덮는 scrim은 host가 그린다 (설계 D2-A). 본체는 자식만 그린다.
        draw_children(context, interaction);
    }

    std::unique_ptr<luil::modal_host_element> make_settings_dialog(const settings_dialog_view& view, const float window_width, const float window_height)
    {
        // panel 논리 크기다. 높이는 가장 높은 탭에 맞춘 뒤(S1.3) 창 위아래 여백을 뺀
        // 만큼으로 자른다 — 스크롤 한계를 세는 `viewport_height_for`와 **같은 함수**를
        // 쓴다.
        const settings_dialog_scroll_metrics metrics { settings_dialog_metrics(view, window_height) };
        float width { panel_width };
        if (width > window_width)
            width = window_width;
        float height { chrome_height + metrics.panel_content_height };
        const float available_height { dialog_available_height(window_height) };
        if (height > available_height)
            height = available_height;

        dialog_host_spec spec {};
        spec.owner = std::u8string { dialog_owner_settings };
        spec.name = view.document_mode ? std::u8string { u8"환경설정 (문서)" } : std::u8string { u8"환경설정 (전역)" };
        spec.content_width = width;
        spec.content_height = height;
        // 배경 클릭도 Esc도 취소다 — 예전 overlay의 배경 클릭 액션을 그대로 옮겼다.
        spec.outside = make_logic_click(cancel_settings_dialog_intent {});
        spec.dismiss = make_logic_click(cancel_settings_dialog_intent {});
        return make_dialog_host(std::move(spec), std::make_unique<settings_dialog_element>(view, window_height));
    }
} // namespace gitman::ui
