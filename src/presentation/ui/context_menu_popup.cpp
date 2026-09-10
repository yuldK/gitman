#include "presentation/ui/context_menu_popup.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/status_glyphs.h"
#include "presentation/ui/ui_actions.h"

#include "luil/generated/codicons.h"
#include "luil/ui/menu_element.h"
#include "luil/ui/ui_tree.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        // 항목 아이콘 표다 (예전 `context_menu_element`의 것을 그대로 옮겼다).
        char32_t entry_glyph(const context_menu_entry entry) noexcept
        {
            switch (entry)
            {
            case context_menu_entry::open_repository:
                return luil::codicons::icon_folder_opened;
            case context_menu_entry::open_in_vscode:
                return luil::codicons::icon_vscode;
            case context_menu_entry::show_local_changes:
                return luil::codicons::icon_diff;
            case context_menu_entry::refresh:
                return luil::codicons::icon_refresh;
            case context_menu_entry::update:
                return luil::codicons::icon_repo_pull;
            case context_menu_entry::switch_to:
                return luil::codicons::icon_source_control;
            case context_menu_entry::open_document_folder:
                return luil::codicons::icon_folder_opened;
            case context_menu_entry::open_document_in_vscode:
                return luil::codicons::icon_vscode;
            case context_menu_entry::custom_command:
                // 여기 오지 않는다 — 사용자 명령은 `item_glyph`가 먼저 가른다.
                break;
            }
            return luil::codicons::icon_question;
        }

        // 사용자 명령은 설정에 적힌 codicon 이름을 따르므로 종류가 아니라 항목을 본다.
        char32_t item_glyph(const context_menu_item_view& item) noexcept
        {
            if (item.entry == context_menu_entry::custom_command)
                return codicon_for_configured_name(item.icon, default_command_codicon());
            return entry_glyph(item.entry);
        }

        // 항목 하나를 고른 결과다. 외부 열기 3종은 UI thread가 셸을 부르는 앱
        // 명령이라 여기서 곧바로 내고(설계 L7), 메뉴 닫기는 delegate가 명령을
        // 실행한 뒤 잇는다. 나머지는 logic이 닫기와 본 동작을 함께 처리한다.
        luil::input_action entry_action(const context_menu_entry entry, const std::u8string& target_path)
        {
            switch (entry)
            {
            case context_menu_entry::open_repository:
                return make_app_command_action(app_command::open_in_explorer_folder, target_path);
            case context_menu_entry::open_in_vscode:
            case context_menu_entry::open_document_in_vscode:
                // 로컬 변경 dialog의 파일 열기와 같은 UI thread 경로다. 폴더를 주면
                // VSCode가 workspace로 연다.
                return make_app_command_action(app_command::open_in_vscode, target_path);
            case context_menu_entry::open_document_folder:
                // 문서 파일을 선택 상태로 두고 그 폴더를 연다 (T1).
                return make_app_command_action(app_command::open_in_explorer_select, target_path);
            case context_menu_entry::show_local_changes:
            case context_menu_entry::refresh:
            case context_menu_entry::update:
            case context_menu_entry::switch_to:
            case context_menu_entry::custom_command:
                break;
            }
            return make_logic_action(select_context_menu_item_intent { entry });
        }

        luil::input_action item_action(const project_id& owner, const context_menu_item_view& item)
        {
            // 사용자 명령은 카드와 명령 id를 함께 실어 logic으로 간다. 메뉴 닫기는
            // handler가 함께 한다 (내장 logic 항목과 같은 규칙).
            if (item.entry == context_menu_entry::custom_command)
                return make_logic_action(run_custom_command_intent { owner, item.command_id });
            return entry_action(item.entry, item.target_path);
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
        case context_menu_entry::custom_command:
            // 실제 키는 `context_menu_item_key`가 항목마다 만든다. 이 이름은 표의
            // 자리를 채워 종류 전수를 유지하기 위한 것이다.
            return u8"custom_command";
        }
        return u8"";
    }

    std::u8string context_menu_item_key(const context_menu_item_view& item)
    {
        if (item.entry != context_menu_entry::custom_command)
            return std::u8string { context_menu_entry_key(item.entry) };

        std::u8string key { custom_command_key_prefix };
        key.append(item.command_id);
        return key;
    }

    std::optional<context_menu_entry> parse_context_menu_entry(const std::u8string_view key) noexcept
    {
        // 표는 `context_menu_entry_key` 한 곳뿐이다 — 되돌리기는 그 표를 훑는다.
        constexpr std::array<context_menu_entry, 9> entries {
            context_menu_entry::open_repository,
            context_menu_entry::open_in_vscode,
            context_menu_entry::show_local_changes,
            context_menu_entry::refresh,
            context_menu_entry::update,
            context_menu_entry::switch_to,
            context_menu_entry::open_document_folder,
            context_menu_entry::open_document_in_vscode,
            context_menu_entry::custom_command,
        };
        for (const context_menu_entry entry : entries)
            if (context_menu_entry_key(entry) == key)
                return entry;
        return std::nullopt;
    }

    luil::win32::ui_popup build_context_menu_popup(const context_menu_view& menu, const float scale)
    {
        const float effective_scale { scale > 0.0f ? scale : 1.0f };
        // 카드 메뉴만 대상 카드를 갖는다. 배너(문서) 메뉴는 owner가 비어 있다 (T1).
        const std::u8string popup_id { menu.owner.value.empty() ? document_menu_popup_id : card_menu_popup_id };

        luil::menu_config config {};
        // menu의 owner는 비워 둔다. luil controller의 `find_menu`는 정책이 준 kind 짝을
        // owner 없는 id로 찾으므로 owner를 채우면 ↑/↓/Enter/Esc가 메뉴로 가지 않는다.
        // popup 구분은 `ui_popup::id`가, 항목 구분은 항목 owner(entry 키)가 한다.
        for (const context_menu_item_view& item : menu.items)
        {
            luil::menu_item_config row {};
            row.key = context_menu_item_key(item);
            row.label = item.label;
            row.icon = item_glyph(item);
            row.enabled = item.enabled;
            config.items.push_back(std::move(row));
        }
        // factory는 키 하나만 받으므로 항목을 값으로 들고 있다가 키로 찾는다 —
        // 외부 열기 항목의 `target_path`와 사용자 명령의 id가 항목마다 다르기
        // 때문이다. 종류가 아니라 **키**로 찾는 것이 핵심이다: 사용자 명령은 여럿이
        // 같은 entry 값을 쓰므로 종류로 찾으면 전부 첫 항목으로 몰린다.
        config.select = [items = menu.items, owner = menu.owner](const std::u8string& key) -> luil::input_action {
            for (const context_menu_item_view& item : items)
                if (context_menu_item_key(item) == key)
                    return item_action(owner, item);
            return {};
        };

        luil::win32::ui_popup popup {};
        popup.id = popup_id;
        // anchor는 비운다 — 메뉴는 주 창 위에 뜬다.
        popup.x = menu.anchor_x / effective_scale;
        popup.y = menu.anchor_y / effective_scale;
        popup.width = layout_context_menu_width;
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
