#pragma once

#include "luil/ui/ui_element_id.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace gitman::ui {
    // 앱이 정의하는 element kind 표다. luil은 0~63을 예약하고(창 root·caption 계열은
    // 이름·값이 gitman의 예전 enum과 같아 그대로 쓴다) 앱 kind는
    // `application_element_kind(index)`로 64부터 정의한다.
    //
    // index는 아래 enum이 매긴다. 열거자가 곧 index라 값이 겹칠 수 없고, 새 kind는
    // 끝에 더한다. 예전 enum의 스크롤 막대 8종은 luil `scrollbar_element`의 앱 kind
    // 하나(`kind_scrollbar`)와 owner 문자열(`scrollbar_owner_*`)로 구분한다.
    namespace detail {
        enum class application_kind_index : std::uint32_t
        {
            toolbar,
            toolbar_document_path,
            toolbar_refresh_all,
            toolbar_open_document,
            toolbar_toggle_path_display,
            toolbar_close_document,
            toolbar_settings,
            toolbar_discover,
            notice,
            card_list,
            scrollbar,
            card_body,
            card_refresh,
            card_update,
            card_switch,
            empty_state,
            start_page,
            start_page_open_document,
            start_page_generate_document,
            start_page_recent_item,
            start_page_recent_pin,
            start_page_recent_remove,
            log_pane,
            log_title,
            log_filter,
            log_autoscroll,
            log_copy,
            log_clear,
            switch_dialog,
            switch_dialog_panel,
            switch_dialog_item,
            switch_dialog_svn_expand,
            switch_dialog_svn_status,
            switch_dialog_query_input,
            switch_dialog_query_action,
            switch_dialog_query_refresh,
            switch_dialog_confirm,
            switch_dialog_cancel,
            settings_dialog,
            settings_dialog_panel,
            settings_tab_item,
            settings_git_browse,
            settings_git_clear,
            settings_svn_browse,
            settings_svn_clear,
            settings_timeout_input,
            settings_override_badge,
            settings_submodules_toggle,
            settings_ignore_local_toggle,
            settings_log_files_toggle,
            settings_theme_option,
            settings_accent_swatch,
            settings_dialog_confirm,
            settings_dialog_cancel,
            settings_associate,
            settings_dissociate,
            settings_ui_font_pick,
            settings_code_font_pick,
            generate_dialog,
            generate_dialog_panel,
            generate_dialog_name_input,
            generate_dialog_scan_browse,
            generate_dialog_same_folder,
            generate_dialog_location_browse,
            generate_dialog_confirm,
            generate_dialog_cancel,
            font_dialog,
            font_dialog_panel,
            font_dialog_item,
            font_dialog_query_input,
            font_dialog_query_clear,
            font_dialog_confirm,
            font_dialog_cancel,
            local_changes_dialog,
            local_changes_dialog_panel,
            local_changes_item,
            local_changes_open_vscode,
            local_changes_open_explorer,
            local_changes_diff,
            local_changes_dialog_close,
            notice_dialog,
            notice_dialog_panel,
            notice_dialog_confirm,
            discovery_dialog,
            discovery_dialog_panel,
            discovery_dialog_item,
            discovery_dialog_confirm,
            discovery_dialog_cancel,
            function_confirm_dialog,
            function_confirm_dialog_panel,
            function_confirm_dialog_item,
            function_confirm_dialog_confirm,
            function_confirm_dialog_cancel,
            card_function,
            card_function_more,
            card_function_marker,
            card_function_more_marker,
            function_bar,
            function_bar_button,
            function_bar_more,
            function_bar_marker,
            function_editor_dialog,
            function_editor_panel,
            function_editor_list,
            function_editor_item,
            function_editor_form,
            function_editor_tool,
            function_editor_label_input,
            function_editor_program_input,
            function_editor_program_browse,
            function_editor_arguments_input,
            function_editor_working_directory_input,
            function_editor_timeout_input,
            function_editor_icon,
            function_editor_icons,
            function_editor_text,
            function_editor_preview_step,
            function_editor_apply,
            function_editor_cancel,
            count,
        };

        [[nodiscard]] constexpr luil::ui_element_kind application_kind(const application_kind_index index) noexcept
        {
            return luil::application_element_kind(static_cast<std::uint32_t>(index));
        }
    } // namespace detail

    inline constexpr luil::ui_element_kind kind_toolbar { detail::application_kind(detail::application_kind_index::toolbar) };
    inline constexpr luil::ui_element_kind kind_toolbar_document_path { detail::application_kind(detail::application_kind_index::toolbar_document_path) };
    inline constexpr luil::ui_element_kind kind_toolbar_refresh_all { detail::application_kind(detail::application_kind_index::toolbar_refresh_all) };
    inline constexpr luil::ui_element_kind kind_toolbar_open_document { detail::application_kind(detail::application_kind_index::toolbar_open_document) };
    inline constexpr luil::ui_element_kind kind_toolbar_toggle_path_display { detail::application_kind(detail::application_kind_index::toolbar_toggle_path_display) };
    inline constexpr luil::ui_element_kind kind_toolbar_close_document { detail::application_kind(detail::application_kind_index::toolbar_close_document) };
    inline constexpr luil::ui_element_kind kind_toolbar_settings { detail::application_kind(detail::application_kind_index::toolbar_settings) };
    inline constexpr luil::ui_element_kind kind_toolbar_discover { detail::application_kind(detail::application_kind_index::toolbar_discover) };
    inline constexpr luil::ui_element_kind kind_notice { detail::application_kind(detail::application_kind_index::notice) };
    inline constexpr luil::ui_element_kind kind_card_list { detail::application_kind(detail::application_kind_index::card_list) };
    inline constexpr luil::ui_element_kind kind_scrollbar { detail::application_kind(detail::application_kind_index::scrollbar) };
    inline constexpr luil::ui_element_kind kind_card_body { detail::application_kind(detail::application_kind_index::card_body) };
    inline constexpr luil::ui_element_kind kind_card_refresh { detail::application_kind(detail::application_kind_index::card_refresh) };
    inline constexpr luil::ui_element_kind kind_card_update { detail::application_kind(detail::application_kind_index::card_update) };
    inline constexpr luil::ui_element_kind kind_card_switch { detail::application_kind(detail::application_kind_index::card_switch) };
    inline constexpr luil::ui_element_kind kind_empty_state { detail::application_kind(detail::application_kind_index::empty_state) };
    inline constexpr luil::ui_element_kind kind_start_page { detail::application_kind(detail::application_kind_index::start_page) };
    inline constexpr luil::ui_element_kind kind_start_page_open_document { detail::application_kind(detail::application_kind_index::start_page_open_document) };
    inline constexpr luil::ui_element_kind kind_start_page_generate_document { detail::application_kind(detail::application_kind_index::start_page_generate_document) };
    inline constexpr luil::ui_element_kind kind_start_page_recent_item { detail::application_kind(detail::application_kind_index::start_page_recent_item) };
    inline constexpr luil::ui_element_kind kind_start_page_recent_pin { detail::application_kind(detail::application_kind_index::start_page_recent_pin) };
    inline constexpr luil::ui_element_kind kind_start_page_recent_remove { detail::application_kind(detail::application_kind_index::start_page_recent_remove) };
    inline constexpr luil::ui_element_kind kind_log_pane { detail::application_kind(detail::application_kind_index::log_pane) };
    inline constexpr luil::ui_element_kind kind_log_title { detail::application_kind(detail::application_kind_index::log_title) };
    inline constexpr luil::ui_element_kind kind_log_filter { detail::application_kind(detail::application_kind_index::log_filter) };
    inline constexpr luil::ui_element_kind kind_log_autoscroll { detail::application_kind(detail::application_kind_index::log_autoscroll) };
    inline constexpr luil::ui_element_kind kind_log_copy { detail::application_kind(detail::application_kind_index::log_copy) };
    inline constexpr luil::ui_element_kind kind_log_clear { detail::application_kind(detail::application_kind_index::log_clear) };
    inline constexpr luil::ui_element_kind kind_switch_dialog { detail::application_kind(detail::application_kind_index::switch_dialog) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_panel { detail::application_kind(detail::application_kind_index::switch_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_item { detail::application_kind(detail::application_kind_index::switch_dialog_item) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_svn_expand { detail::application_kind(detail::application_kind_index::switch_dialog_svn_expand) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_svn_status { detail::application_kind(detail::application_kind_index::switch_dialog_svn_status) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_query_input { detail::application_kind(detail::application_kind_index::switch_dialog_query_input) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_query_action { detail::application_kind(detail::application_kind_index::switch_dialog_query_action) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_query_refresh { detail::application_kind(detail::application_kind_index::switch_dialog_query_refresh) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_confirm { detail::application_kind(detail::application_kind_index::switch_dialog_confirm) };
    inline constexpr luil::ui_element_kind kind_switch_dialog_cancel { detail::application_kind(detail::application_kind_index::switch_dialog_cancel) };
    inline constexpr luil::ui_element_kind kind_settings_dialog { detail::application_kind(detail::application_kind_index::settings_dialog) };
    inline constexpr luil::ui_element_kind kind_settings_dialog_panel { detail::application_kind(detail::application_kind_index::settings_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_settings_tab_item { detail::application_kind(detail::application_kind_index::settings_tab_item) };
    inline constexpr luil::ui_element_kind kind_settings_git_browse { detail::application_kind(detail::application_kind_index::settings_git_browse) };
    inline constexpr luil::ui_element_kind kind_settings_git_clear { detail::application_kind(detail::application_kind_index::settings_git_clear) };
    inline constexpr luil::ui_element_kind kind_settings_svn_browse { detail::application_kind(detail::application_kind_index::settings_svn_browse) };
    inline constexpr luil::ui_element_kind kind_settings_svn_clear { detail::application_kind(detail::application_kind_index::settings_svn_clear) };
    inline constexpr luil::ui_element_kind kind_settings_timeout_input { detail::application_kind(detail::application_kind_index::settings_timeout_input) };
    inline constexpr luil::ui_element_kind kind_settings_override_badge { detail::application_kind(detail::application_kind_index::settings_override_badge) };
    inline constexpr luil::ui_element_kind kind_settings_submodules_toggle { detail::application_kind(detail::application_kind_index::settings_submodules_toggle) };
    inline constexpr luil::ui_element_kind kind_settings_ignore_local_toggle { detail::application_kind(detail::application_kind_index::settings_ignore_local_toggle) };
    inline constexpr luil::ui_element_kind kind_settings_log_files_toggle { detail::application_kind(detail::application_kind_index::settings_log_files_toggle) };
    inline constexpr luil::ui_element_kind kind_settings_theme_option { detail::application_kind(detail::application_kind_index::settings_theme_option) };
    inline constexpr luil::ui_element_kind kind_settings_accent_swatch { detail::application_kind(detail::application_kind_index::settings_accent_swatch) };
    inline constexpr luil::ui_element_kind kind_settings_dialog_confirm { detail::application_kind(detail::application_kind_index::settings_dialog_confirm) };
    inline constexpr luil::ui_element_kind kind_settings_dialog_cancel { detail::application_kind(detail::application_kind_index::settings_dialog_cancel) };
    inline constexpr luil::ui_element_kind kind_settings_associate { detail::application_kind(detail::application_kind_index::settings_associate) };
    inline constexpr luil::ui_element_kind kind_settings_dissociate { detail::application_kind(detail::application_kind_index::settings_dissociate) };
    inline constexpr luil::ui_element_kind kind_settings_ui_font_pick { detail::application_kind(detail::application_kind_index::settings_ui_font_pick) };
    inline constexpr luil::ui_element_kind kind_settings_code_font_pick { detail::application_kind(detail::application_kind_index::settings_code_font_pick) };
    inline constexpr luil::ui_element_kind kind_generate_dialog { detail::application_kind(detail::application_kind_index::generate_dialog) };
    inline constexpr luil::ui_element_kind kind_generate_dialog_panel { detail::application_kind(detail::application_kind_index::generate_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_generate_dialog_name_input { detail::application_kind(detail::application_kind_index::generate_dialog_name_input) };
    inline constexpr luil::ui_element_kind kind_generate_dialog_scan_browse { detail::application_kind(detail::application_kind_index::generate_dialog_scan_browse) };
    inline constexpr luil::ui_element_kind kind_generate_dialog_same_folder { detail::application_kind(detail::application_kind_index::generate_dialog_same_folder) };
    inline constexpr luil::ui_element_kind kind_generate_dialog_location_browse { detail::application_kind(detail::application_kind_index::generate_dialog_location_browse) };
    inline constexpr luil::ui_element_kind kind_generate_dialog_confirm { detail::application_kind(detail::application_kind_index::generate_dialog_confirm) };
    inline constexpr luil::ui_element_kind kind_generate_dialog_cancel { detail::application_kind(detail::application_kind_index::generate_dialog_cancel) };
    inline constexpr luil::ui_element_kind kind_font_dialog { detail::application_kind(detail::application_kind_index::font_dialog) };
    inline constexpr luil::ui_element_kind kind_font_dialog_panel { detail::application_kind(detail::application_kind_index::font_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_font_dialog_item { detail::application_kind(detail::application_kind_index::font_dialog_item) };
    inline constexpr luil::ui_element_kind kind_font_dialog_query_input { detail::application_kind(detail::application_kind_index::font_dialog_query_input) };
    inline constexpr luil::ui_element_kind kind_font_dialog_query_clear { detail::application_kind(detail::application_kind_index::font_dialog_query_clear) };
    inline constexpr luil::ui_element_kind kind_font_dialog_confirm { detail::application_kind(detail::application_kind_index::font_dialog_confirm) };
    inline constexpr luil::ui_element_kind kind_font_dialog_cancel { detail::application_kind(detail::application_kind_index::font_dialog_cancel) };
    inline constexpr luil::ui_element_kind kind_local_changes_dialog { detail::application_kind(detail::application_kind_index::local_changes_dialog) };
    inline constexpr luil::ui_element_kind kind_local_changes_dialog_panel { detail::application_kind(detail::application_kind_index::local_changes_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_local_changes_item { detail::application_kind(detail::application_kind_index::local_changes_item) };
    inline constexpr luil::ui_element_kind kind_local_changes_open_vscode { detail::application_kind(detail::application_kind_index::local_changes_open_vscode) };
    inline constexpr luil::ui_element_kind kind_local_changes_open_explorer { detail::application_kind(detail::application_kind_index::local_changes_open_explorer) };
    inline constexpr luil::ui_element_kind kind_local_changes_diff { detail::application_kind(detail::application_kind_index::local_changes_diff) };
    inline constexpr luil::ui_element_kind kind_local_changes_dialog_close { detail::application_kind(detail::application_kind_index::local_changes_dialog_close) };
    inline constexpr luil::ui_element_kind kind_notice_dialog { detail::application_kind(detail::application_kind_index::notice_dialog) };
    inline constexpr luil::ui_element_kind kind_notice_dialog_panel { detail::application_kind(detail::application_kind_index::notice_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_notice_dialog_confirm { detail::application_kind(detail::application_kind_index::notice_dialog_confirm) };
    inline constexpr luil::ui_element_kind kind_discovery_dialog { detail::application_kind(detail::application_kind_index::discovery_dialog) };
    inline constexpr luil::ui_element_kind kind_discovery_dialog_panel { detail::application_kind(detail::application_kind_index::discovery_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_discovery_dialog_item { detail::application_kind(detail::application_kind_index::discovery_dialog_item) };
    inline constexpr luil::ui_element_kind kind_discovery_dialog_confirm { detail::application_kind(detail::application_kind_index::discovery_dialog_confirm) };
    inline constexpr luil::ui_element_kind kind_discovery_dialog_cancel { detail::application_kind(detail::application_kind_index::discovery_dialog_cancel) };
    inline constexpr luil::ui_element_kind kind_function_confirm_dialog { detail::application_kind(detail::application_kind_index::function_confirm_dialog) };
    inline constexpr luil::ui_element_kind kind_function_confirm_dialog_panel { detail::application_kind(detail::application_kind_index::function_confirm_dialog_panel) };
    inline constexpr luil::ui_element_kind kind_function_confirm_dialog_item { detail::application_kind(detail::application_kind_index::function_confirm_dialog_item) };
    inline constexpr luil::ui_element_kind kind_function_confirm_dialog_confirm { detail::application_kind(detail::application_kind_index::function_confirm_dialog_confirm) };
    inline constexpr luil::ui_element_kind kind_function_confirm_dialog_cancel { detail::application_kind(detail::application_kind_index::function_confirm_dialog_cancel) };
    // 카드의 기능 버튼(`card` 배치), `⋯`, 실행 중·실패 점 표식이다 (function-buttons-design 2.3). 기능 버튼과 그 점의
    // owner는 `card_function_owner`(카드 id + `:` + 버튼 id), `⋯`와 그 점의 owner는 카드 id다. `⋯`의 점은 kind를
    // 따로 둔다 — 같은 kind면 카드 `team`의 버튼 `build` 점과 카드 `team:build`의 `⋯` 점이 같은 id가 된다.
    inline constexpr luil::ui_element_kind kind_card_function { detail::application_kind(detail::application_kind_index::card_function) };
    inline constexpr luil::ui_element_kind kind_card_function_more { detail::application_kind(detail::application_kind_index::card_function_more) };
    inline constexpr luil::ui_element_kind kind_card_function_marker { detail::application_kind(detail::application_kind_index::card_function_marker) };
    inline constexpr luil::ui_element_kind kind_card_function_more_marker { detail::application_kind(detail::application_kind_index::card_function_more_marker) };
    // 선택 카드 기능 줄과 그 `bar` 버튼, 줄의 `⋯`, 실패 점이다 (function-buttons-design 2.4). 버튼과 점의 owner는
    // `card_function_owner`(선택 카드 id + `:` + 버튼 id)이고 줄과 줄의 `⋯`는 owner가 없다 — 줄은 하나뿐이다. 카드의
    // 버튼과 kind를 나눠 같은 기능이 두 자리에 있어도(선택 카드의 기능 메뉴 등) id가 겹치지 않는다.
    inline constexpr luil::ui_element_kind kind_function_bar { detail::application_kind(detail::application_kind_index::function_bar) };
    inline constexpr luil::ui_element_kind kind_function_bar_button { detail::application_kind(detail::application_kind_index::function_bar_button) };
    inline constexpr luil::ui_element_kind kind_function_bar_more { detail::application_kind(detail::application_kind_index::function_bar_more) };
    inline constexpr luil::ui_element_kind kind_function_bar_marker { detail::application_kind(detail::application_kind_index::function_bar_marker) };
    // 기능 편집 dialog다 (function-buttons-design 2.7). 목록 행(`kind_function_editor_item`)의 owner는 `function_editor_row_view::key`, 도구 버튼
    // (`kind_function_editor_tool`)의 owner는 동작 이름, 아이콘 버튼의 owner는 아이콘 이름, 미리보기의 `<` `>`는 `previous`·`next`다.
    inline constexpr luil::ui_element_kind kind_function_editor_dialog { detail::application_kind(detail::application_kind_index::function_editor_dialog) };
    inline constexpr luil::ui_element_kind kind_function_editor_panel { detail::application_kind(detail::application_kind_index::function_editor_panel) };
    inline constexpr luil::ui_element_kind kind_function_editor_list { detail::application_kind(detail::application_kind_index::function_editor_list) };
    inline constexpr luil::ui_element_kind kind_function_editor_item { detail::application_kind(detail::application_kind_index::function_editor_item) };
    inline constexpr luil::ui_element_kind kind_function_editor_form { detail::application_kind(detail::application_kind_index::function_editor_form) };
    inline constexpr luil::ui_element_kind kind_function_editor_tool { detail::application_kind(detail::application_kind_index::function_editor_tool) };
    inline constexpr luil::ui_element_kind kind_function_editor_label_input { detail::application_kind(detail::application_kind_index::function_editor_label_input) };
    inline constexpr luil::ui_element_kind kind_function_editor_program_input { detail::application_kind(detail::application_kind_index::function_editor_program_input) };
    inline constexpr luil::ui_element_kind kind_function_editor_program_browse { detail::application_kind(detail::application_kind_index::function_editor_program_browse) };
    inline constexpr luil::ui_element_kind kind_function_editor_arguments_input { detail::application_kind(detail::application_kind_index::function_editor_arguments_input) };
    inline constexpr luil::ui_element_kind kind_function_editor_working_directory_input { detail::application_kind(detail::application_kind_index::function_editor_working_directory_input) };
    inline constexpr luil::ui_element_kind kind_function_editor_timeout_input { detail::application_kind(detail::application_kind_index::function_editor_timeout_input) };
    inline constexpr luil::ui_element_kind kind_function_editor_icon { detail::application_kind(detail::application_kind_index::function_editor_icon) };
    // 아이콘 버튼 묶음이다. 폼 안에서 Tab의 한 자리다.
    inline constexpr luil::ui_element_kind kind_function_editor_icons { detail::application_kind(detail::application_kind_index::function_editor_icons) };
    // 그려진 글을 보조 기술에 내는 자리다 (그리지 않는다). owner는 `preview`·`message`·`variables`다.
    inline constexpr luil::ui_element_kind kind_function_editor_text { detail::application_kind(detail::application_kind_index::function_editor_text) };
    inline constexpr luil::ui_element_kind kind_function_editor_preview_step { detail::application_kind(detail::application_kind_index::function_editor_preview_step) };
    inline constexpr luil::ui_element_kind kind_function_editor_apply { detail::application_kind(detail::application_kind_index::function_editor_apply) };
    inline constexpr luil::ui_element_kind kind_function_editor_cancel { detail::application_kind(detail::application_kind_index::function_editor_cancel) };

    inline constexpr std::size_t application_kind_count { static_cast<std::size_t>(detail::application_kind_index::count) };

    // 그리기 순서와 무관한 전체 목록이다. test가 중복·예약 대역 침범을 확인한다.
    inline constexpr std::array<luil::ui_element_kind, application_kind_count> application_kinds {
        kind_toolbar,
        kind_toolbar_document_path,
        kind_toolbar_refresh_all,
        kind_toolbar_open_document,
        kind_toolbar_toggle_path_display,
        kind_toolbar_close_document,
        kind_toolbar_settings,
        kind_toolbar_discover,
        kind_notice,
        kind_card_list,
        kind_scrollbar,
        kind_card_body,
        kind_card_refresh,
        kind_card_update,
        kind_card_switch,
        kind_empty_state,
        kind_start_page,
        kind_start_page_open_document,
        kind_start_page_generate_document,
        kind_start_page_recent_item,
        kind_start_page_recent_pin,
        kind_start_page_recent_remove,
        kind_log_pane,
        kind_log_title,
        kind_log_filter,
        kind_log_autoscroll,
        kind_log_copy,
        kind_log_clear,
        kind_switch_dialog,
        kind_switch_dialog_panel,
        kind_switch_dialog_item,
        kind_switch_dialog_svn_expand,
        kind_switch_dialog_svn_status,
        kind_switch_dialog_query_input,
        kind_switch_dialog_query_action,
        kind_switch_dialog_query_refresh,
        kind_switch_dialog_confirm,
        kind_switch_dialog_cancel,
        kind_settings_dialog,
        kind_settings_dialog_panel,
        kind_settings_tab_item,
        kind_settings_git_browse,
        kind_settings_git_clear,
        kind_settings_svn_browse,
        kind_settings_svn_clear,
        kind_settings_timeout_input,
        kind_settings_override_badge,
        kind_settings_submodules_toggle,
        kind_settings_ignore_local_toggle,
        kind_settings_log_files_toggle,
        kind_settings_theme_option,
        kind_settings_accent_swatch,
        kind_settings_dialog_confirm,
        kind_settings_dialog_cancel,
        kind_settings_associate,
        kind_settings_dissociate,
        kind_settings_ui_font_pick,
        kind_settings_code_font_pick,
        kind_generate_dialog,
        kind_generate_dialog_panel,
        kind_generate_dialog_name_input,
        kind_generate_dialog_scan_browse,
        kind_generate_dialog_same_folder,
        kind_generate_dialog_location_browse,
        kind_generate_dialog_confirm,
        kind_generate_dialog_cancel,
        kind_font_dialog,
        kind_font_dialog_panel,
        kind_font_dialog_item,
        kind_font_dialog_query_input,
        kind_font_dialog_query_clear,
        kind_font_dialog_confirm,
        kind_font_dialog_cancel,
        kind_local_changes_dialog,
        kind_local_changes_dialog_panel,
        kind_local_changes_item,
        kind_local_changes_open_vscode,
        kind_local_changes_open_explorer,
        kind_local_changes_diff,
        kind_local_changes_dialog_close,
        kind_notice_dialog,
        kind_notice_dialog_panel,
        kind_notice_dialog_confirm,
        kind_discovery_dialog,
        kind_discovery_dialog_panel,
        kind_discovery_dialog_item,
        kind_discovery_dialog_confirm,
        kind_discovery_dialog_cancel,
        kind_function_confirm_dialog,
        kind_function_confirm_dialog_panel,
        kind_function_confirm_dialog_item,
        kind_function_confirm_dialog_confirm,
        kind_function_confirm_dialog_cancel,
        kind_card_function,
        kind_card_function_more,
        kind_card_function_marker,
        kind_card_function_more_marker,
        kind_function_bar,
        kind_function_bar_button,
        kind_function_bar_more,
        kind_function_bar_marker,
        kind_function_editor_dialog,
        kind_function_editor_panel,
        kind_function_editor_list,
        kind_function_editor_item,
        kind_function_editor_form,
        kind_function_editor_tool,
        kind_function_editor_label_input,
        kind_function_editor_program_input,
        kind_function_editor_program_browse,
        kind_function_editor_arguments_input,
        kind_function_editor_working_directory_input,
        kind_function_editor_timeout_input,
        kind_function_editor_icon,
        kind_function_editor_icons,
        kind_function_editor_text,
        kind_function_editor_preview_step,
        kind_function_editor_apply,
        kind_function_editor_cancel,
    };

    static_assert(application_kinds.size() == application_kind_count);
    static_assert(static_cast<std::uint32_t>(application_kinds.front()) == static_cast<std::uint32_t>(luil::ui_element_kind::first_application_kind));

    // luil `scrollbar_element`가 쓰는 owner 문자열이다. 어느 목록의 막대인지를 구분한다.
    inline constexpr std::u8string_view scrollbar_owner_cards { u8"cards" };
    inline constexpr std::u8string_view scrollbar_owner_log { u8"log" };
    inline constexpr std::u8string_view scrollbar_owner_switch_dialog { u8"switch" };
    inline constexpr std::u8string_view scrollbar_owner_settings_dialog { u8"settings" };
    inline constexpr std::u8string_view scrollbar_owner_font_dialog { u8"font" };
    inline constexpr std::u8string_view scrollbar_owner_local_changes_list { u8"local-changes-list" };
    inline constexpr std::u8string_view scrollbar_owner_local_changes_diff { u8"local-changes-diff" };
    inline constexpr std::u8string_view scrollbar_owner_discovery_dialog { u8"discovery" };
    inline constexpr std::u8string_view scrollbar_owner_function_confirm_dialog { u8"function-confirm" };
    inline constexpr std::u8string_view scrollbar_owner_function_editor_list { u8"function-editor-list" };
    inline constexpr std::u8string_view scrollbar_owner_function_editor_form { u8"function-editor-form" };

    // luil `modal_host_element`(scrim 포함)가 쓰는 owner 문자열이다. dialog를 가른다
    // (설계 L6, D2-A). 본체 element의 kind(`kind_*_dialog`)는 그대로다.
    inline constexpr std::u8string_view dialog_owner_notice { u8"notice" };
    inline constexpr std::u8string_view dialog_owner_switch { u8"switch" };
    inline constexpr std::u8string_view dialog_owner_settings { u8"settings" };
    inline constexpr std::u8string_view dialog_owner_generate { u8"generate" };
    inline constexpr std::u8string_view dialog_owner_font { u8"font" };
    inline constexpr std::u8string_view dialog_owner_discovery { u8"discovery" };
    inline constexpr std::u8string_view dialog_owner_local_changes { u8"local-changes" };
    // 기능 실행 확인 dialog다 (function-buttons-design 3.10). 신뢰 확인과 `confirm` 미리보기가 같은 owner다.
    inline constexpr std::u8string_view dialog_owner_function_confirm { u8"function-confirm" };
    // 기능 편집 dialog다 (function-buttons-design 2.7).
    inline constexpr std::u8string_view dialog_owner_function_editor { u8"function-editor" };

    // 카드 기능 버튼의 owner다. tree 안에서 카드마다 같은 버튼 id가 되풀이되므로 카드 id를 앞에 붙인다.
    // 버튼 id는 `[a-z0-9_-]`라 `:`가 없고 project id에는 있을 수 있으므로, 되돌릴 때는 **마지막** `:`에서 나눈다.
    [[nodiscard]] inline std::u8string card_function_owner(const std::u8string_view card, const std::u8string_view button)
    {
        std::u8string owner { card };
        owner += u8':';
        owner += button;
        return owner;
    }

    // `card_function_owner`의 카드 id 부분이다. 기능 버튼과 그 점의 owner에만 쓴다 — `⋯`의 owner는 카드 id
    // 그대로라 나누면 `:`가 든 카드 id가 잘린다. 구분자가 없으면 owner 전체다.
    [[nodiscard]] inline std::u8string_view card_of_function_owner(const std::u8string_view owner) noexcept
    {
        const std::size_t separator { owner.rfind(u8':') };
        return separator == std::u8string_view::npos ? owner : owner.substr(0, separator);
    }
} // namespace gitman::ui
