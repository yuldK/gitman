#include "presentation/ui/context_menu_popup.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/function_icon_catalog.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/menu_element.h"
#include "luil/ui/ui_tree.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        // 항목 아이콘이다. 실행 항목의 아이콘 이름은 기능 버튼의 `icon`이라 D16 표로 찾고 모르는 이름도 기본 `play`로
        // 그린다 — 카드 버튼과 같은 글리프여야 한다. 중지 항목(`stop-circle`)과 나머지 항목은 메뉴 이름까지 든 전체
        // 표로 찾고 모르는 이름이면 아이콘이 없다.
        char32_t item_icon(const context_menu_item_view& item) noexcept
        {
            if (item.entry == context_menu_entry::run_function)
                return function_icon_glyph(item.icon);
            return context_menu_icon_glyph(item.icon);
        }

        // 항목 하나를 고른 결과다. 외부 열기 3종은 UI thread가 셸을 부르는 앱
        // 명령이라 여기서 곧바로 내고(설계 L7), 메뉴 닫기는 delegate가 명령을 실행한
        // 뒤 잇는다. 나머지는 logic이 닫기와 본 동작을 함께 처리한다. 하위 동작이 어느
        // 길로 가는지는 동작의 성질이라 항목 데이터가 아니라 이 표가 정한다.
        luil::input_action item_action(const context_menu_item_view& item)
        {
            switch (item.entry)
            {
            case context_menu_entry::open_repository:
                return make_app_command_action(app_command::open_in_explorer_folder, item.target_path);
            case context_menu_entry::open_in_vscode:
            case context_menu_entry::open_document_in_vscode:
                // 로컬 변경 dialog의 파일 열기와 같은 UI thread 경로다. 폴더를 주면
                // VSCode가 workspace로 연다.
                return make_app_command_action(app_command::open_in_vscode, item.target_path);
            case context_menu_entry::open_document_folder:
                // 문서 파일을 선택 상태로 두고 그 폴더를 연다 (T1).
                return make_app_command_action(app_command::open_in_explorer_select, item.target_path);
            case context_menu_entry::show_local_changes:
            case context_menu_entry::refresh:
            case context_menu_entry::update:
            case context_menu_entry::switch_to:
            case context_menu_entry::run_function:
            case context_menu_entry::stop_function:
            case context_menu_entry::more_functions:
            case context_menu_entry::reload_document:
            case context_menu_entry::place_function_on_card:
            case context_menu_entry::place_function_on_bar:
            case context_menu_entry::place_function_in_menu:
            case context_menu_entry::add_function:
            case context_menu_entry::edit_functions:
            case context_menu_entry::trust_document_executables:
                break;
            }
            return make_logic_action(select_context_menu_item_intent { item.entry, item.argument });
        }

        luil::menu_item_kind item_kind(const context_menu_item_kind kind) noexcept
        {
            switch (kind)
            {
            case context_menu_item_kind::action:
                return luil::menu_item_kind::action;
            case context_menu_item_kind::radio:
                return luil::menu_item_kind::radio;
            }
            return luil::menu_item_kind::action;
        }
    } // namespace

    std::u8string_view context_menu_entry_key(const context_menu_entry entry) noexcept
    {
        switch (entry)
        {
        case context_menu_entry::open_repository:
            return u8"open_repository";
        case context_menu_entry::open_in_vscode:
            return u8"open_in_vscode";
        case context_menu_entry::show_local_changes:
            return u8"show_local_changes";
        case context_menu_entry::refresh:
            return u8"refresh";
        case context_menu_entry::update:
            return u8"update";
        case context_menu_entry::switch_to:
            return u8"switch_to";
        case context_menu_entry::open_document_folder:
            return u8"open_document_folder";
        case context_menu_entry::open_document_in_vscode:
            return u8"open_document_in_vscode";
        case context_menu_entry::run_function:
            return u8"run_function";
        case context_menu_entry::stop_function:
            return u8"stop_function";
        case context_menu_entry::more_functions:
            return u8"more_functions";
        case context_menu_entry::reload_document:
            return u8"reload_document";
        case context_menu_entry::place_function_on_card:
            return u8"place_function_on_card";
        case context_menu_entry::place_function_on_bar:
            return u8"place_function_on_bar";
        case context_menu_entry::place_function_in_menu:
            return u8"place_function_in_menu";
        case context_menu_entry::add_function:
            return u8"add_function";
        case context_menu_entry::edit_functions:
            return u8"edit_functions";
        case context_menu_entry::trust_document_executables:
            return u8"trust_document_executables";
        }
        return u8"";
    }

    std::optional<context_menu_entry> parse_context_menu_entry(const std::u8string_view key) noexcept
    {
        // 표는 `context_menu_entry_key` 한 곳뿐이다 — 되돌리기는 그 표를 훑는다. 값을 더하고 이 배열을 늘리지
        // 않으면 아래 검사가 빌드를 멈춘다 (function-buttons-design T8 검토에서 넘어온 일).
        constexpr std::array<context_menu_entry, 18> entries {
            context_menu_entry::open_repository,
            context_menu_entry::open_in_vscode,
            context_menu_entry::show_local_changes,
            context_menu_entry::refresh,
            context_menu_entry::update,
            context_menu_entry::switch_to,
            context_menu_entry::open_document_folder,
            context_menu_entry::open_document_in_vscode,
            context_menu_entry::run_function,
            context_menu_entry::stop_function,
            context_menu_entry::more_functions,
            context_menu_entry::reload_document,
            context_menu_entry::place_function_on_card,
            context_menu_entry::place_function_on_bar,
            context_menu_entry::place_function_in_menu,
            context_menu_entry::add_function,
            context_menu_entry::edit_functions,
            context_menu_entry::trust_document_executables,
        };
        static_assert(entries.size() == context_menu_entry_count);
        for (const context_menu_entry entry : entries)
            if (context_menu_entry_key(entry) == key)
                return entry;
        return std::nullopt;
    }

    std::u8string context_menu_item_key(const context_menu_item_view& item)
    {
        std::u8string key { context_menu_entry_key(item.entry) };
        if (item.argument.empty() == false)
        {
            // entry 키에는 `:`가 없으므로 첫 `:` 앞이 늘 entry다.
            key += u8':';
            key += item.argument;
        }
        return key;
    }

    char32_t context_menu_icon_glyph(const std::u8string_view name) noexcept
    {
        // 기능 버튼 아이콘과 한 표다 (`function_icon_catalog.cpp`). 메뉴의 기존 항목은 모르는 이름이면 아이콘이 없다.
        return icon_glyph_by_name(name);
    }

    luil::win32::ui_popup build_context_menu_popup(const context_menu_view& menu, const float scale)
    {
        const float effective_scale { scale > 0.0f ? scale : 1.0f };
        // 카드 메뉴만 대상 카드를 갖는다. 배너(문서) 메뉴는 owner가 비어 있다 (T1).
        const std::u8string popup_id { menu.owner.value.empty() ? document_menu_popup_id : card_menu_popup_id };

        luil::menu_config config {};
        // menu의 owner는 비워 둔다. luil controller의 `find_menu`는 정책이 준 kind 짝을
        // owner 없는 id로 찾으므로 owner를 채우면 ↑/↓/Enter/Esc가 메뉴로 가지 않는다.
        // popup 구분은 `ui_popup::id`가, 항목 구분은 항목 owner(항목 키)가 한다.
        for (const context_menu_item_view& item : menu.items)
        {
            luil::menu_item_config row {};
            row.key = context_menu_item_key(item);
            row.label = item.label;
            row.icon = item_icon(item);
            row.enabled = item.enabled;
            row.separator_above = item.separator_above;
            row.kind = item_kind(item.kind);
            row.checked = item.checked;
            row.shortcut_text = item.shortcut_text;
            config.items.push_back(std::move(row));
        }
        if (menu.default_item.has_value() && *menu.default_item < config.items.size())
            config.default_key = config.items[*menu.default_item].key;
        // factory는 키 하나만 받으므로 항목을 값으로 들고 있다가 키로 찾는다 — 외부 열기
        // 항목의 `target_path`와 기능 항목의 `argument`가 항목마다 다르기 때문이다. 같은
        // 동작을 여러 항목이 나눠 쓰므로 entry가 아니라 항목 키로 찾는다.
        config.select = [items = menu.items](const std::u8string& key) -> luil::input_action {
            for (const context_menu_item_view& item : items)
                if (context_menu_item_key(item) == key)
                    return item_action(item);
            return {};
        };

        luil::win32::ui_popup popup {};
        popup.id = popup_id;
        // anchor는 비운다 — 메뉴는 주 창 위에 뜬다.
        popup.x = menu.anchor_x / effective_scale;
        popup.y = menu.anchor_y / effective_scale;
        // 폭은 가장 긴 `라벨 + 단축키 열`을 담되 190~320이다 (function-buttons-design 2.3.2). 짧은 기존 메뉴(카드 메뉴
        // 6항목, 배너 메뉴)는 하한 190 그대로다. 320을 넘는 라벨은 메뉴 항목이 말줄임한다. 글은 설정한 UI 글꼴로
        // 재고(`width_for`의 기본 측정기, 어느 thread에서나 부를 수 있다), 시스템 글자 크기만큼 넓어진다.
        popup.width = std::clamp(luil::menu_element::width_for(config), layout_context_menu_width, layout_context_menu_maximum_width);
        popup.height = luil::menu_element::height_for(config);
        auto root { std::make_unique<luil::menu_element>(std::move(config)) };
        popup.tree = std::make_shared<const luil::ui_tree>(
            luil::make_arranged_tree(std::move(root), { 0.0f, 0.0f, popup.width * effective_scale, popup.height * effective_scale }, effective_scale));
        // 계기를 가리지 않는다. 바깥 클릭·휠·Esc·창 이동·활성 전환 어디서든 닫는다
        // (메뉴는 앵커에 매인 짧은 선택이다).
        popup.dismiss = [](luil::win32::popup_dismiss_reason) { return make_logic_action(close_context_menu_intent {}); };
        return popup;
    }
} // namespace gitman::ui
