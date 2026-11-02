#include "application/logic_controller.h"

#include "domain/text_edit.h"

#include "application/svn_repository_browser.h"
#include "domain/log_file_naming.h"
#include "domain/path_syntax.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"
#include "presentation/ui/settings_dialog_element.h"

#include <chrono>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace gitman {
    namespace {
        // 문서 이름의 어휘 검증이다 (F3). 파일 이름으로 쓸 수 없는 문자를 막는다.
        // 존재·중복 확인은 worker의 생성 service가 원자적으로 한다.
        std::u8string_view generate_document_name_error(const std::u8string_view name) noexcept
        {
            if (name.empty())
                return u8"";
            if (name == u8"." || name == u8"..")
                return u8"그 이름은 파일 이름으로 쓸 수 없습니다.";
            for (const char8_t value : name)
                if (value < u8' ' || value == u8'\\' || value == u8'/' || value == u8':' || value == u8'*' || value == u8'?' || value == u8'"' || value == u8'<' || value == u8'>' || value == u8'|')
                    return u8"이름에 파일 이름으로 쓸 수 없는 문자가 있습니다.";
            return u8"";
        }

        // 배너에 싣는 문서 진단 한 줄이다. project 하나에 걸린 진단은 메시지만으로는
        // 어느 카드의 일인지 알 수 없어 그 project의 표시 이름을 앞에 붙인다.
        std::u8string document_notice_text(const diagnostic& value, const workspace_document& document)
        {
            if (value.source.project_id.has_value() == false)
                return value.message;
            std::u8string name { *value.source.project_id };
            for (const project_definition& project : document.projects)
                if (project.id.value == *value.source.project_id && project.display_name.empty() == false)
                    name = project.display_name;
            return name + u8": " + value.message;
        }

        // 글꼴 목록 첫 항목의 표시 이름이다. 고르면 빈 이름 = 내장 글꼴이다.
        constexpr std::u8string_view default_font_display_name { u8"(기본 글꼴)" };
        // 글꼴 미리 보기 문구다. 한글·영문·숫자·기호를 한 줄에 담아 코드 글꼴 판단에 쓴다.
        constexpr std::u8string_view font_preview_text { u8"다람쥐 헌 쳇바퀴에 타고파 Illegal1O0 {}(); 😀" };

        char8_t ascii_lowercase(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        bool contains_ignoring_ascii_case(const std::u8string_view text, const std::u8string_view needle) noexcept
        {
            if (needle.empty())
                return true;
            if (text.size() < needle.size())
                return false;
            for (std::size_t start = 0; start + needle.size() <= text.size(); ++start)
            {
                bool matched { true };
                for (std::size_t index = 0; index < needle.size(); ++index)
                {
                    if (ascii_lowercase(text[start + index]) != ascii_lowercase(needle[index]))
                    {
                        matched = false;
                        break;
                    }
                }
                if (matched)
                    return true;
            }
            return false;
        }

        // card_state에서 표시 상태를 정한다. 우선순위: 비활성 → 실행 중 → 초기 조회
        // 전 → 조회 불가 → 오류 → 주의 → 정상.
        struct card_view_inputs
        {
            bool enabled { true };
            bool busy { false };
            bool has_local_result { false };
            repository_availability availability { repository_availability::unknown };
            remote_sync_state sync_state { remote_sync_state::unknown };
            working_tree_state tree_state { working_tree_state::unknown };
        };

        // dialog 수준에서 곧바로 판정할 수 있는 검증이다. 존재·저장소 일치 같은
        // 나머지 검증은 provider가 실행 직전에 다시 수행한다 (REQ-007).
        //
        // 원격을 추적하는 local branch는 local 후보로 따로 서지 않고 그 remote 후보
        // (`origin/main`)로만 선다 (`build_git_switch_candidates`). 그래서 remote 후보도
        // 대응하는 local branch가 체크아웃돼 있으면 현재다. 같은 이름의 local 후보가
        // 따로 있으면 그 remote로는 닿지 않는 branch라 local 후보만 현재다.
        bool candidate_is_current(const repository_snapshot& snapshot, const switch_candidate& candidate, const std::vector<switch_candidate>& candidates) noexcept
        {
            if (candidate.kind == switch_candidate_kind::git_local_branch)
                return candidate.display_name == snapshot.current_reference;
            if (candidate.kind == switch_candidate_kind::subversion_url)
                return candidate.target == snapshot.current_reference;
            if (candidate.local_branch.empty() || candidate.local_branch != snapshot.current_reference)
                return false;
            for (const switch_candidate& other : candidates)
                if (other.kind == switch_candidate_kind::git_local_branch && other.display_name == candidate.local_branch)
                    return false;
            return true;
        }

        // 환경설정 경로 초안의 형식 검증이다 (stage-8-plan 5.1). 파일 선택 dialog가
        // 존재하는 파일만 돌려주므로 logic은 blocking I/O 없이 형식만 본다. 빈 값은
        // 자동 탐색이라 항상 유효하다.
        std::u8string_view settings_executable_error(const std::u8string_view path) noexcept
        {
            if (path.empty() || is_absolute_windows_path(path))
                return u8"";
            return u8"실행 파일 경로는 절대 경로여야 합니다.";
        }

        // 상태 확인 제한 시간 텍스트 박스의 초안을 초 값으로 바꾼다. 초안에는
        // 숫자만 담기므로(handle_edit_settings_timeout) 자리올림만 하면 된다. 빈
        // 초안은 기본값이라 값이 없다.
        std::optional<std::int32_t> parse_settings_timeout(const std::u8string_view text) noexcept
        {
            if (text.empty())
                return std::nullopt;
            std::int32_t value { 0 };
            for (const char8_t digit : text)
                value = value * 10 + static_cast<std::int32_t>(digit - u8'0');
            return value;
        }

        std::u8string_view settings_timeout_error(const std::u8string_view text) noexcept
        {
            const std::optional<std::int32_t> value { parse_settings_timeout(text) };
            if (value.has_value() == false || (*value >= minimum_query_timeout_seconds && *value <= maximum_query_timeout_seconds))
                return u8"";
            return u8"제한 시간은 10~3600초 사이여야 합니다.";
        }

        card_view_state derive_card_state(const card_view_inputs& inputs) noexcept
        {
            if (inputs.enabled == false)
                return card_view_state::disabled;
            if (inputs.busy)
                return card_view_state::running;
            if (inputs.has_local_result == false)
                return card_view_state::loading;
            if (inputs.availability != repository_availability::ready)
                return card_view_state::failed;
            if (inputs.sync_state == remote_sync_state::error)
                return card_view_state::failed;
            if (inputs.tree_state == working_tree_state::conflicted || inputs.sync_state == remote_sync_state::authentication_required || inputs.sync_state == remote_sync_state::offline
                || inputs.sync_state == remote_sync_state::remote_target_missing)
                return card_view_state::warning;
            return card_view_state::ready;
        }
    } // namespace

    logic_controller::logic_controller(operation_submitter& submitter, cancellation_registry& registry) noexcept
        : submitter_ { &submitter }
        , registry_ { &registry }
    {
        // 종료 시 UI thread가 전역 token까지 함께 취소한다 (설계 L3).
        registry_->add(cancellation_source_);
    }

    logic_controller::logic_controller(operation_submitter& submitter, log_file_sink& log_sink, cancellation_registry& registry) noexcept
        : submitter_ { &submitter }
        , log_sink_ { &log_sink }
        , registry_ { &registry }
    {
        registry_->add(cancellation_source_);
    }

    logic_controller::logic_controller(operation_submitter& submitter)
        : submitter_ { &submitter }
        , owned_registry_ { std::make_unique<cancellation_registry>() }
    {
        registry_ = owned_registry_.get();
        registry_->add(cancellation_source_);
    }

    logic_controller::logic_controller(operation_submitter& submitter, log_file_sink& log_sink)
        : submitter_ { &submitter }
        , log_sink_ { &log_sink }
        , owned_registry_ { std::make_unique<cancellation_registry>() }
    {
        registry_ = owned_registry_.get();
        registry_->add(cancellation_source_);
    }

    void logic_controller::handle(logic_message message)
    {
        std::visit(
            [this](auto&& value) {
                using value_type = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<value_type, open_document_intent>)
                    handle_open_document(value);
                else if constexpr (std::is_same_v<value_type, generate_document_intent>)
                    handle_generate_document(value);
                else if constexpr (std::is_same_v<value_type, refresh_all_intent>)
                {
                    for (card_state& card : cards_)
                        if (card.project.enabled)
                            request_refresh(card);
                }
                else if constexpr (std::is_same_v<value_type, refresh_card_intent>)
                {
                    card_state* const card { find_card(value.id) };
                    if (card != nullptr && card->project.enabled)
                        request_refresh(*card);
                }
                else if constexpr (std::is_same_v<value_type, select_card_intent>)
                    handle_select_card(value);
                else if constexpr (std::is_same_v<value_type, set_filter_intent>)
                {
                    filter_ = std::move(value.text);
                    clamp_scroll();
                }
                else if constexpr (std::is_same_v<value_type, toggle_path_display_intent>)
                    handle_toggle_path_display();
                else if constexpr (std::is_same_v<value_type, reorder_card_intent>)
                    handle_reorder_card(value);
                else if constexpr (std::is_same_v<value_type, request_update_intent>)
                    handle_request_update(value);
                else if constexpr (std::is_same_v<value_type, request_switch_intent>)
                    handle_request_switch(value);
                else if constexpr (std::is_same_v<value_type, cancel_operation_intent>)
                    handle_cancel_operation(value);
                else if constexpr (std::is_same_v<value_type, clear_log_intent>)
                {
                    card_state* const card { find_card(value.id) };
                    if (card != nullptr)
                        card->log.clear();
                }
                else if constexpr (std::is_same_v<value_type, set_log_filter_intent>)
                {
                    log_filter_ = value.filter;
                    // 필터가 바뀌면 내용 높이가 달라진다. 저장된 위치를 다시 고정한다.
                    handle_log_scroll(0.0f);
                }
                else if constexpr (std::is_same_v<value_type, set_log_auto_scroll_intent>)
                    log_auto_scroll_ = value.enabled;
                else if constexpr (std::is_same_v<value_type, log_scroll_intent>)
                    handle_log_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, begin_switch_intent>)
                    handle_begin_switch(value);
                else if constexpr (std::is_same_v<value_type, select_switch_candidate_intent>)
                    handle_select_switch_candidate(value.index);
                else if constexpr (std::is_same_v<value_type, select_svn_browser_node_intent>)
                    handle_select_svn_browser_node(value.url);
                else if constexpr (std::is_same_v<value_type, toggle_svn_browser_node_intent>)
                    handle_toggle_svn_browser_node(value.url);
                else if constexpr (std::is_same_v<value_type, confirm_switch_intent>)
                    handle_confirm_switch();
                else if constexpr (std::is_same_v<value_type, cancel_switch_dialog_intent>)
                    switch_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, switch_dialog_scroll_intent>)
                    handle_switch_dialog_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, set_text_composition_intent>)
                    handle_set_text_composition(value);
                else if constexpr (std::is_same_v<value_type, edit_text_input_intent>)
                    handle_edit_text_input(value);
                else if constexpr (std::is_same_v<value_type, submit_switch_query_intent>)
                    handle_submit_switch_query();
                else if constexpr (std::is_same_v<value_type, clear_switch_query_intent>)
                    handle_clear_switch_query();
                else if constexpr (std::is_same_v<value_type, refresh_switch_dialog_intent>)
                    handle_refresh_switch_dialog();
                else if constexpr (std::is_same_v<value_type, open_generate_document_intent>)
                    handle_open_generate_document();
                else if constexpr (std::is_same_v<value_type, set_generate_scan_root_intent>)
                {
                    if (generate_dialog_.has_value())
                    {
                        generate_dialog_->scan_root = std::move(value.path);
                        generate_dialog_->message.clear();
                    }
                }
                else if constexpr (std::is_same_v<value_type, set_generate_location_intent>)
                {
                    if (generate_dialog_.has_value())
                    {
                        generate_dialog_->location = std::move(value.path);
                        generate_dialog_->same_folder = false;
                        generate_dialog_->message.clear();
                    }
                }
                else if constexpr (std::is_same_v<value_type, toggle_generate_same_folder_intent>)
                {
                    if (generate_dialog_.has_value())
                    {
                        generate_dialog_->same_folder = generate_dialog_->same_folder == false;
                        generate_dialog_->message.clear();
                    }
                }
                else if constexpr (std::is_same_v<value_type, confirm_generate_document_intent>)
                    handle_confirm_generate_document();
                else if constexpr (std::is_same_v<value_type, cancel_generate_document_intent>)
                    generate_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, open_font_dialog_intent>)
                    handle_open_font_dialog(value);
                else if constexpr (std::is_same_v<value_type, select_font_family_intent>)
                    handle_select_font_family(value.index);
                else if constexpr (std::is_same_v<value_type, confirm_font_dialog_intent>)
                    handle_confirm_font_dialog();
                else if constexpr (std::is_same_v<value_type, cancel_font_dialog_intent>)
                    font_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, font_dialog_scroll_intent>)
                    handle_font_dialog_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, font_families_event>)
                    font_families_ = std::move(value.families);
                else if constexpr (std::is_same_v<value_type, begin_discovery_intent>)
                    handle_begin_discovery(value);
                else if constexpr (std::is_same_v<value_type, toggle_discovery_candidate_intent>)
                    handle_toggle_discovery_candidate(value.index);
                else if constexpr (std::is_same_v<value_type, confirm_discovery_intent>)
                    handle_confirm_discovery();
                else if constexpr (std::is_same_v<value_type, cancel_discovery_dialog_intent>)
                    handle_cancel_discovery_dialog();
                else if constexpr (std::is_same_v<value_type, discovery_dialog_scroll_intent>)
                    handle_discovery_dialog_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, open_context_menu_intent>)
                    handle_open_context_menu(value);
                else if constexpr (std::is_same_v<value_type, open_document_context_menu_intent>)
                    handle_open_document_context_menu(value);
                else if constexpr (std::is_same_v<value_type, set_theme_preference_intent>)
                    handle_set_theme_preference(value);
                else if constexpr (std::is_same_v<value_type, set_accent_intent>)
                    handle_set_accent(value);
                else if constexpr (std::is_same_v<value_type, close_context_menu_intent>)
                    context_menu_.reset();
                else if constexpr (std::is_same_v<value_type, select_context_menu_item_intent>)
                    handle_select_context_menu_item(value);
                else if constexpr (std::is_same_v<value_type, remove_recent_document_intent>)
                    handle_remove_recent_document(value);
                else if constexpr (std::is_same_v<value_type, toggle_recent_document_pin_intent>)
                    handle_toggle_recent_document_pin(value);
                else if constexpr (std::is_same_v<value_type, close_document_intent>)
                    handle_close_document();
                else if constexpr (std::is_same_v<value_type, show_notice_intent>)
                    handle_show_notice(std::move(value));
                else if constexpr (std::is_same_v<value_type, dismiss_notice_intent>)
                    notice_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, open_local_changes_intent>)
                    handle_open_local_changes(value);
                else if constexpr (std::is_same_v<value_type, select_local_change_intent>)
                    handle_select_local_change(value.index);
                else if constexpr (std::is_same_v<value_type, cancel_local_changes_dialog_intent>)
                    local_changes_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, local_changes_scroll_intent>)
                    handle_local_changes_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, local_changes_diff_scroll_intent>)
                    handle_local_changes_diff_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, local_changes_event>)
                    handle_local_changes(std::move(value));
                else if constexpr (std::is_same_v<value_type, file_diff_event>)
                    handle_file_diff(std::move(value));
                else if constexpr (std::is_same_v<value_type, open_settings_intent>)
                    handle_open_settings();
                else if constexpr (std::is_same_v<value_type, set_settings_executable_intent>)
                    handle_set_settings_executable(std::move(value));
                else if constexpr (std::is_same_v<value_type, clear_settings_executable_intent>)
                    handle_clear_settings_executable(value);
                else if constexpr (std::is_same_v<value_type, clear_settings_override_intent>)
                    handle_clear_settings_override(value);
                else if constexpr (std::is_same_v<value_type, select_settings_tab_intent>)
                    handle_select_settings_tab(value);
                else if constexpr (std::is_same_v<value_type, settings_scroll_intent>)
                    handle_settings_scroll(value.delta);
                else if constexpr (std::is_same_v<value_type, toggle_settings_submodules_intent>)
                {
                    // 값을 건드리면 문서 모드에서는 그 행이 "문서에 정의됨"이 된다
                    // (G3.2 암묵 덮어쓰기).
                    if (settings_dialog_.has_value())
                    {
                        settings_dialog_->update_submodules = settings_dialog_->update_submodules == false;
                        settings_dialog_->submodules_defined = true;
                    }
                }
                else if constexpr (std::is_same_v<value_type, toggle_settings_ignore_local_intent>)
                {
                    if (settings_dialog_.has_value())
                    {
                        settings_dialog_->ignore_local_changes = settings_dialog_->ignore_local_changes == false;
                        settings_dialog_->ignore_local_defined = true;
                    }
                }
                else if constexpr (std::is_same_v<value_type, toggle_settings_log_files_intent>)
                {
                    if (settings_dialog_.has_value())
                    {
                        settings_dialog_->write_log_files = settings_dialog_->write_log_files == false;
                        settings_dialog_->log_files_defined = true;
                    }
                }
                else if constexpr (std::is_same_v<value_type, confirm_settings_intent>)
                    handle_confirm_settings();
                else if constexpr (std::is_same_v<value_type, cancel_settings_dialog_intent>)
                    settings_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, window_metrics_intent>)
                {
                    window_width_ = value.width;
                    window_height_ = value.height;
                    scale_ = value.scale > 0.0f ? value.scale : 1.0f;
                    // 창이 커지면 남은 스크롤 여유가 줄어든다. 다시 고정하지 않으면
                    // 화면은 맞게 그려지지만 다음 휠 입력이 한참 동안 헛돈다.
                    clamp_scroll();
                }
                else if constexpr (std::is_same_v<value_type, scroll_intent>)
                {
                    scroll_offset_ += value.delta;
                    clamp_scroll();
                }
                else if constexpr (std::is_same_v<value_type, window_placement_intent>)
                    handle_window_placement(value);
                else if constexpr (std::is_same_v<value_type, close_intent> || std::is_same_v<value_type, shutdown_message>)
                    begin_shutdown();
                else if constexpr (std::is_same_v<value_type, startup_intent>)
                    start();
                else if constexpr (std::is_same_v<value_type, relay_drain_intent>)
                {
                    // 깨우기 전용 메시지다 (설계 L3). 내용은 driver가 relay 큐에서
                    // 이미 꺼내 따로 넘겼다.
                }
                else if constexpr (std::is_same_v<value_type, document_loaded_event>)
                    handle_document_loaded(std::move(value));
                else if constexpr (std::is_same_v<value_type, document_generated_event>)
                    handle_document_generated(std::move(value));
                else if constexpr (std::is_same_v<value_type, query_completed_event>)
                    handle_query_completed(std::move(value));
                else if constexpr (std::is_same_v<value_type, document_saved_event>)
                    handle_document_saved(std::move(value));
                else if constexpr (std::is_same_v<value_type, operation_log_event>)
                    handle_operation_log(std::move(value));
                else if constexpr (std::is_same_v<value_type, change_completed_event>)
                    handle_change_completed(std::move(value));
                else if constexpr (std::is_same_v<value_type, switch_candidates_event>)
                    handle_switch_candidates(std::move(value));
                else if constexpr (std::is_same_v<value_type, svn_directory_event>)
                    handle_svn_directory(std::move(value));
                else if constexpr (std::is_same_v<value_type, discovery_completed_event>)
                    handle_discovery_completed(std::move(value));
                else if constexpr (std::is_same_v<value_type, projects_registered_event>)
                    handle_projects_registered(std::move(value));
                else if constexpr (std::is_same_v<value_type, app_settings_loaded_event>)
                    handle_app_settings_loaded(std::move(value));
                else if constexpr (std::is_same_v<value_type, app_settings_saved_event>)
                    handle_app_settings_saved(std::move(value));
            },
            std::move(message));
    }

    void logic_controller::start()
    {
        if (shutting_down_ || app_settings_loaded_ || pending_app_settings_load_id_ != 0)
            return;

        operation_request request { make_request(operation_kind::load_app_settings, nullptr, 0) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;
        pending_app_settings_load_id_ = operation_id;
    }

    bool logic_controller::shutdown_requested() const noexcept
    {
        return shutting_down_;
    }

    process_cancellation_token logic_controller::cancellation() const noexcept
    {
        return cancellation_source_.token();
    }

    void logic_controller::handle_open_document(const open_document_intent& intent)
    {
        if (shutting_down_)
            return;

        // 문서를 열기 전의 배치를 앱 설정에 남긴다 (S3.2). 아래에서 document_를
        // 비우므로 열려 있었는지 판정이 가능한 지금 호출한다.
        record_app_window_placement();

        // 이전 문서에서 진행 중인 변경 작업은 결과가 버려질 것이므로 프로세스도
        // 계속 둘 이유가 없다. 카드가 사라지기 전에 취소를 전파한다.
        cancel_running_changes();

        document_path_ = intent.path;
        document_.reset();
        revision_ = {};
        cards_.clear();
        notices_.clear();
        save_notice_.clear();
        selected_.reset();
        scroll_offset_ = 0.0f;
        document_loading_ = true;
        // 이전 문서의 진행 중 저장·생성 결과는 도착해도 버린다.
        pending_save_operation_id_ = 0;
        save_queued_ = false;
        pending_generation_operation_id_ = 0;

        operation_request request { make_request(operation_kind::load_document, nullptr, 0) };
        request.document_path = document_path_;
        static_cast<void>(submitter_->submit(std::move(request)));
    }

    void logic_controller::handle_generate_document(const generate_document_intent& intent)
    {
        // 생성은 현재 문서를 건드리지 않고 worker에서 진행되므로 화면은 그대로 둔다.
        if (shutting_down_ || pending_generation_operation_id_ != 0)
            return;

        operation_request request { make_request(operation_kind::generate_document, nullptr, 0) };
        request.document_path = intent.document_path;
        request.scan_root = intent.scan_root;
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;
        pending_generation_operation_id_ = operation_id;
    }

    void logic_controller::handle_document_loaded(document_loaded_event event)
    {
        document_loading_ = false;
        if (event.document.has_value() == false)
        {
            notices_.clear();
            for (const diagnostic& value : event.diagnostics)
                if (value.severity != diagnostic_severity::information)
                    notices_.push_back(value.message);
            return;
        }

        install_document(std::move(*event.document), std::move(event.revision), std::move(event.diagnostics));
    }

    void logic_controller::handle_document_generated(document_generated_event event)
    {
        // 다른 문서를 연 뒤 도착한 이전 생성 결과는 현재 상태에 적용하지 않는다.
        if (event.operation_id != pending_generation_operation_id_)
            return;
        pending_generation_operation_id_ = 0;

        if (event.document.has_value() == false || event.revision.has_value() == false)
        {
            // 실패는 현재 문서를 유지한 채 진단만 알린다. dialog는 열어 둔 채 사유를
            // 보여 주어 이름이나 위치를 고쳐 다시 시도할 수 있게 한다 (F3).
            notices_.clear();
            for (const diagnostic& value : event.diagnostics)
                if (value.severity != diagnostic_severity::information)
                    notices_.push_back(value.message);
            if (generate_dialog_.has_value())
            {
                generate_dialog_->executing = false;
                generate_dialog_->message = notices_.empty() ? std::u8string { u8"문서를 만들지 못했습니다." } : notices_.front();
            }
            return;
        }
        // 성공하면 만든 문서를 곧바로 열므로 dialog는 닫는다.
        generate_dialog_.reset();

        // 생성된 문서를 곧바로 연다. 이전 문서의 진행 중 결과가 새 문서에 적용되지
        // 않도록 열기와 같은 규칙으로 대기 상태를 정리한다. 문서를 여는 것이므로
        // 열기와 같은 배치 기록 규칙을 따른다 (S3.2).
        record_app_window_placement();
        document_path_ = std::move(event.document_path);
        document_loading_ = false;
        pending_save_operation_id_ = 0;
        save_queued_ = false;
        install_document(std::move(*event.document), std::move(*event.revision), std::move(event.diagnostics));
    }

    void logic_controller::install_document(workspace_document document, workspace_revision_token revision, std::vector<diagnostic> diagnostics)
    {
        // 생성된 문서 채택처럼 open을 거치지 않는 교체 경로도 이전 카드의 변경
        // 작업을 취소해야 한다.
        cancel_running_changes();

        notices_.clear();
        for (const diagnostic& value : diagnostics)
            if (value.severity != diagnostic_severity::information)
                notices_.push_back(document_notice_text(value, document));

        save_notice_.clear();
        selected_.reset();
        scroll_offset_ = 0.0f;
        document_ = std::move(document);
        revision_ = std::move(revision);
        // 이전 문서의 카드를 가리키던 overlay와 dialog는 의미가 없다. 환경설정
        // 초안도 이전 문서의 settings 기준이라 함께 버린다. 탐색 dialog는 진행 중
        // 탐색을 취소하고 닫는다.
        switch_dialog_.reset();
        settings_dialog_.reset();
        local_changes_dialog_.reset();
        context_menu_.reset();
        if (discovery_dialog_.has_value() && discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();
        discovery_dialog_.reset();

        // 문서가 배치를 담고 있으면 UI thread가 한 번 적용하도록 게시 번호를 올린다.
        window_placement_ = document_->window;
        window_placement_dirty_ = false;
        if (window_placement_.has_value())
            ++window_placement_revision_;
        cards_.clear();
        cards_.reserve(document_->projects.size());
        for (const project_definition& project : document_->projects)
        {
            card_state card {};
            card.project = project;
            card.snapshot.project = project.id;
            card.generation = 1;
            cards_.push_back(std::move(card));
        }

        // 문서를 실제로 연 시점에만 최근 목록에 남긴다 (app-shell-design A1.2).
        record_recent_document();
        // 로그 파일 적재 대상도 문서 단위로 다시 정해진다 (A4.1).
        publish_log_targets();

        // 시작 시 로컬 상태를 먼저 표시한다 (plan 5.1). 원격 판정은 명시적 refresh다.
        for (card_state& card : cards_)
        {
            if (card.project.enabled == false)
                continue;
            card.busy = true;
            static_cast<void>(submitter_->submit(make_request(operation_kind::query_local, &card, card.generation)));
        }
    }

    void logic_controller::handle_query_completed(query_completed_event event)
    {
        card_state* const card { find_card(event.id) };

        // 카드가 삭제됐거나 그 사이 refresh로 세대가 넘어간 결과는 폐기한다
        // (ADR-004 검증 항목, ADR-005 7.2).
        if (card == nullptr || event.generation != card->generation)
            return;

        card->snapshot = std::move(event.result.snapshot);
        card->diagnostics = std::move(event.result.diagnostics);
        card->has_local_result = true;

        // 조회 실패·경고는 카드 로그에도 남긴다 (2026-08-21 검수: 파일 로그에 조회
        // 결과까지 포함). 성공은 카드 표시로 충분하므로 남기지 않는다.
        for (const diagnostic& value : card->diagnostics)
            if (value.severity != diagnostic_severity::information)
                append_lifecycle_log(*card, value.severity, value.message);

        if (event.final_event)
        {
            card->busy = false;
            if (card->refresh_queued)
            {
                // 실행 중 들어온 중복 refresh는 한 번으로 병합해 뒤이어 실행한다
                // (plan 5.1).
                card->refresh_queued = false;
                request_refresh(*card);
            }
        }
    }

    void logic_controller::handle_reorder_card(const reorder_card_intent& intent)
    {
        if (shutting_down_ || document_.has_value() == false || intent.id == intent.target)
            return;

        std::size_t from { cards_.size() };
        std::size_t target { cards_.size() };
        for (std::size_t index = 0; index < cards_.size(); ++index)
        {
            if (cards_[index].project.id == intent.id)
                from = index;
            if (cards_[index].project.id == intent.target)
                target = index;
        }
        if (from == cards_.size() || target == cards_.size())
            return;

        // 꺼낸 뒤의 삽입 위치다. 앞에서 빼면 뒤 index가 하나 당겨진다.
        std::size_t to { target + (intent.place_after ? 1u : 0u) };
        if (from < to)
            --to;
        // 위치가 그대로면 문서도 그대로다. 저장할 것이 없다.
        if (from == to)
            return;

        card_state moved { std::move(cards_[from]) };
        cards_.erase(cards_.begin() + static_cast<std::ptrdiff_t>(from));
        cards_.insert(cards_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));

        // 문서의 프로젝트 순서가 진실이다. 카드 순서에서 다시 만들어 저장한다.
        document_->projects.clear();
        document_->projects.reserve(cards_.size());
        for (const card_state& card : cards_)
            document_->projects.push_back(card.project);

        request_save();
    }

    void logic_controller::handle_select_card(const select_card_intent& intent)
    {
        const std::optional<project_id> previous { selected_ };
        if (intent.id.has_value() && find_card(*intent.id) == nullptr)
            selected_.reset();
        else
            selected_ = intent.id;

        // 다른 카드의 로그를 이전 카드의 필터·스크롤로 보지 않도록 뷰 상태를
        // 기본값으로 되돌린다. 진행 중 작업은 건드리지 않는다 (plan 3.9).
        if ((selected_ == previous) == false)
            reset_log_view_state();

        // 키보드 순회는 화면 밖 카드도 고를 수 있다. 선택이 보이도록 따라간다.
        scroll_selected_into_view();
    }

    void logic_controller::reset_log_view_state() noexcept
    {
        log_filter_ = log_stream_filter::all;
        log_auto_scroll_ = true;
        log_scroll_offset_ = 0.0f;
    }

    bool logic_controller::has_log_pane() const noexcept
    {
        // 로그 pane은 문서가 열려 있는 동안 항상 열려 있다 (2026-08-20 검수:
        // 클릭해야 열리는 UX 제거). 선택 카드가 없으면 안내 제목의 빈 pane이다.
        // 시작 페이지에는 로그가 없으므로 pane도 두지 않는다 (app-shell-design A1.3).
        return document_.has_value();
    }

    float logic_controller::log_content_height() const noexcept
    {
        if (selected_.has_value() == false)
            return 0.0f;
        const operation_log_buffer* const buffer { card_log(*selected_) };
        if (buffer == nullptr)
            return 0.0f;

        // 스크롤 한계는 progress 접기 후의 표시 줄 수 기준이다 (stage-8-plan 5.3).
        const std::size_t count { log_display_line_count(buffer->records(), log_filter_) };
        return static_cast<float>(count) * layout_log_line_height() + layout_log_text_inset * 2.0f;
    }

    float logic_controller::log_viewport_height() const noexcept
    {
        // pane 높이는 창이 작으면 줄어든다. layout과 같은 함수로 계산해야 스크롤
        // 한계가 그리기와 일치한다.
        const list_layout layout { compute_list_layout(window_height_ / scale_, 1.0f, has_notice(), has_log_pane()) };
        const float body { layout.log_height - layout_log_header_height() };
        return body > 0.0f ? body : 0.0f;
    }

    void logic_controller::handle_log_scroll(const float delta)
    {
        if (has_log_pane() == false)
            return;

        float maximum { log_content_height() - log_viewport_height() };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { log_auto_scroll_ ? maximum : log_scroll_offset_ };
        offset += delta;
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        log_scroll_offset_ = offset;
        // 맨 아래에 닿으면 자동 스크롤로 돌아간다. 위로 올리면 꺼진다.
        log_auto_scroll_ = offset >= maximum - 0.5f;
    }

    void logic_controller::handle_toggle_path_display()
    {
        if (shutting_down_ || document_.has_value() == false)
            return;

        // 표시 방식은 문서에 남는 설정이라 순서 변경과 같은 저장 경로를 탄다.
        // 토글은 유효 값을 뒤집은 결과를 문서 override로 정의한다 (G3.1).
        document_->settings.show_relative_paths = { relative_paths() == false };
        request_save();
    }

    void logic_controller::handle_window_placement(const window_placement_intent& intent)
    {
        if (intent.placement.valid() == false)
            return;

        // 문서를 여는 순간 앱 설정에 남길 값이라 문서 유무와 무관하게 기억한다
        // (settings-tabs-and-appearance-scope-design S3.2).
        current_placement_ = intent.placement;

        if (document_.has_value())
        {
            // 문서가 열려 있는 동안의 배치는 문서에만 남는다. 앱 단위 배치는 문서
            // 없이 시작할 때의 값이라 문서의 크기·위치로 덮이면 안 된다 (S3.2).
            if (document_->window.has_value() && *document_->window == intent.placement)
                return;

            document_->window = intent.placement;
            window_placement_ = intent.placement;
            window_placement_dirty_ = true;
            return;
        }

        // 문서가 없을 때만 앱 설정에 남긴다. 저장은 begin_shutdown이 한 번 내보낸다.
        if (app_settings_.window.has_value() == false || *app_settings_.window != intent.placement)
        {
            app_settings_.window = { intent.placement };
            app_settings_window_dirty_ = true;
        }
    }

    void logic_controller::record_app_window_placement()
    {
        // 문서를 여는 순간의 배치가 "문서 없이 쓰던" 마지막 배치다 (S3.2). 문서가
        // 이미 열려 있었다면 그 배치는 문서의 것이라 앱 설정으로 올리지 않는다.
        if (document_.has_value() || current_placement_.has_value() == false)
            return;
        if (app_settings_.window.has_value() && *app_settings_.window == *current_placement_)
            return;

        app_settings_.window = current_placement_;
        app_settings_window_dirty_ = true;
    }

    void logic_controller::handle_document_saved(document_saved_event event)
    {
        // 다른 문서를 연 뒤 도착한 이전 저장 결과는 버린다.
        if (event.operation_id != pending_save_operation_id_)
            return;
        pending_save_operation_id_ = 0;

        save_notice_.clear();
        if (event.revision.has_value())
            revision_ = *event.revision;
        else
            for (const diagnostic& value : event.diagnostics)
                if (value.severity == diagnostic_severity::error)
                {
                    save_notice_ = value.message;
                    break;
                }

        if (save_queued_)
        {
            save_queued_ = false;
            request_save();
        }
    }

    void logic_controller::request_save()
    {
        if (shutting_down_ || document_.has_value() == false)
            return;
        if (pending_save_operation_id_ != 0)
        {
            save_queued_ = true;
            return;
        }

        operation_request request { make_request(operation_kind::save_document, nullptr, 0) };
        request.document = document_;
        request.revision = revision_;
        pending_save_operation_id_ = request.operation_id;
        static_cast<void>(submitter_->submit(std::move(request)));
    }

    void logic_controller::handle_request_update(const request_update_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->project.enabled == false)
            return;

        // submodule 갱신 여부는 매번 묻지 않고 유효 설정이 정한다 (2026-08-20
        // 검수: 확인 overlay 제거).
        update_options options { intent.options };
        options.update_submodules = effective_settings().update_submodules;
        begin_change(*card, operation_kind::update, options, nullptr);
    }

    void logic_controller::handle_request_switch(const request_switch_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->project.enabled == false)
            return;

        begin_change(*card, operation_kind::switch_to, {}, &intent.target);
    }

    void logic_controller::begin_change(card_state& card, const operation_kind kind, const update_options& options, const switch_candidate* const target)
    {
        const bool is_update { kind == operation_kind::update };
        if (card.busy)
        {
            // lane 직렬화와 별개로 대기열 폭주를 막는다 (stage-7-plan 4.4). 사유는
            // 로그로 남겨 사용자가 클릭이 무시된 이유를 알 수 있다.
            append_lifecycle_log(card, diagnostic_severity::warning,
                is_update ? std::u8string { u8"이미 작업이 진행 중이라 update를 시작하지 않습니다." } : std::u8string { u8"이미 작업이 진행 중이라 switch를 시작하지 않습니다." });
            return;
        }

        process_cancellation_source cancellation {};
        operation_request request { make_request(kind, &card, card.generation) };
        request.options = options;
        if (target != nullptr)
            request.switch_target = *target;
        // 변경 작업은 카드 단위 취소가 가능해야 하므로 전역 token 대신 작업별
        // token을 싣는다. 종료 시에는 cancel_running_changes가 함께 취소한다.
        request.token = cancellation.token();

        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
        {
            append_lifecycle_log(card, diagnostic_severity::error, u8"작업을 제출하지 못했습니다.");
            return;
        }

        card.busy = true;
        card.change_operation_id = operation_id;
        card.change_kind = kind;
        // 경과 시간 표시의 기준 시각이다 (로그 헤더의 MM:SS). 새 명령이 시작되면
        // 직전 명령이 남긴 경과 시간을 지운다.
        card.change_started_at = std::chrono::steady_clock::now();
        card.change_elapsed.reset();
        card.change_cancellation = std::move(cancellation);
        // 종료가 close 메시지 없이 일어나도 실행 중 프로세스가 취소되도록 등록한다.
        registry_->add(*card.change_cancellation);
        if (is_update)
            append_lifecycle_log(
                card, diagnostic_severity::information, options.update_submodules ? std::u8string { u8"update를 시작합니다 (submodule 함께 갱신)." } : std::u8string { u8"update를 시작합니다." });
        else
            append_lifecycle_log(card, diagnostic_severity::information, std::u8string { u8"switch를 시작합니다: " } + (target != nullptr ? target->display_name : std::u8string {}));
    }

    void logic_controller::handle_cancel_operation(const cancel_operation_intent& intent)
    {
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->change_operation_id == 0 || card->change_cancellation.has_value() == false)
            return;

        card->change_cancellation->request_cancellation();
        append_lifecycle_log(*card, diagnostic_severity::warning, u8"취소를 요청했습니다.");
    }

    void logic_controller::handle_operation_log(operation_log_event event)
    {
        card_state* const card { find_card(event.id) };
        // 끝난 작업이나 다른 문서의 늦은 로그는 버린다. 같은 작업의 로그는 executor가
        // 완료 event보다 먼저 보내므로 (같은 producer, FIFO) 유실되지 않는다.
        if (card == nullptr || event.operation_id != card->change_operation_id)
            return;

        for (operation_log_entry& entry : event.entries)
            append_card_log(*card, std::move(entry));
    }

    void logic_controller::handle_change_completed(change_completed_event event)
    {
        card_state* const card { find_card(event.id) };
        if (card == nullptr || event.operation_id != card->change_operation_id)
            return;

        // 명령이 끝나도 걸린 시간은 남긴다. 작업이 끝난 뒤에 확인할 수 있어야 한다
        // (2026-08-22 지시). 다음 명령이 시작될 때 지워진다.
        card->change_elapsed = { std::chrono::steady_clock::now() - card->change_started_at };
        card->change_operation_id = 0;
        card->change_cancellation.reset();
        card->busy = false;

        // 실행 직후 재조회한 상태만 반영한다. 도구 부재처럼 조회 없이 차단된 결과의
        // 빈 snapshot으로 카드 상태를 지우지 않는다.
        card->diagnostics = std::move(event.result.diagnostics);
        if (event.result.snapshot.availability != repository_availability::unknown)
        {
            card->snapshot = std::move(event.result.snapshot);
            card->has_local_result = true;
        }

        const bool is_update { event.kind == operation_kind::update };
        if (event.result.executed && event.result.succeeded)
            append_lifecycle_log(*card, diagnostic_severity::information, is_update ? std::u8string { u8"update가 완료되었습니다." } : std::u8string { u8"switch가 완료되었습니다." });
        else if (event.result.executed)
            append_lifecycle_log(*card, diagnostic_severity::error, is_update ? std::u8string { u8"update가 실패했습니다." } : std::u8string { u8"switch가 실패했습니다." });
        else if (event.result.blocked_by != update_block_reason::none)
            append_lifecycle_log(*card, diagnostic_severity::warning, std::u8string { update_block_reason_message(event.result.blocked_by) });
        else if (event.result.rejected_by != switch_rejection::none)
            append_lifecycle_log(*card, diagnostic_severity::warning, std::u8string { switch_rejection_message(event.result.rejected_by) });
        else
            append_lifecycle_log(*card, diagnostic_severity::error, is_update ? std::u8string { u8"update가 실행되지 않았습니다." } : std::u8string { u8"switch가 실행되지 않았습니다." });

        // 실패 원인 진단도 로그에서 추적할 수 있어야 한다 (REQ-008).
        for (const diagnostic& value : card->diagnostics)
            if (value.severity != diagnostic_severity::information)
                append_lifecycle_log(*card, value.severity, value.message);

        // 실행을 기다리던 switch dialog에 결과를 반영한다. 재검증 거부는 dialog가
        // 사유를 표시한 채 남고 (REQ-007), 실행된 전환은 성패와 관계없이 dialog를
        // 닫는다. 결과는 카드 로그와 상태로 확인한다.
        if (switch_dialog_.has_value() && switch_dialog_->card == event.id && switch_dialog_->executing && event.kind == operation_kind::switch_to)
        {
            switch_dialog_->executing = false;
            if (event.result.executed)
                switch_dialog_.reset();
            else
            {
                switch_dialog_->tracking_confirm_pending = false;
                if (event.result.rejected_by != switch_rejection::none)
                    switch_dialog_->message = std::u8string { switch_rejection_message(event.result.rejected_by) };
                else
                    switch_dialog_->message = u8"전환이 실행되지 않았습니다. 카드 로그를 확인하세요.";
            }
        }

        // 성공 여부와 관계없이 상태를 다시 조회한다 (plan 5.2의 8, 5.3의 9).
        // provider의 재조회는 로컬뿐이므로 remote-first 판정까지 이어 실행한다.
        if (shutting_down_ == false && card->project.enabled)
        {
            card->refresh_queued = false;
            request_refresh(*card);
        }
    }

    void logic_controller::handle_begin_discovery(const begin_discovery_intent& intent)
    {
        // 탐색 등록은 열린 문서에 추가하는 경로다 (stage-8-plan 5.2). 이미 열린
        // dialog가 있으면 무시한다 (dialog가 화면을 덮어 정상 경로에서는 오지 않는다).
        if (shutting_down_ || document_.has_value() == false || discovery_dialog_.has_value())
            return;

        process_cancellation_source cancellation {};
        operation_request request { make_request(operation_kind::discover_projects, nullptr, 0) };
        request.scan_root = intent.scan_root;
        // 탐색은 기존 등록 항목과의 중복 판정을 위해 문서 사본이 필요하다 (단계 5).
        request.document = document_;
        // dialog 취소가 탐색을 함께 취소할 수 있게 작업별 token을 싣는다.
        request.token = cancellation.token();
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        discovery_dialog_state dialog {};
        dialog.scan_root = intent.scan_root;
        dialog.scan_operation_id = operation_id;
        dialog.scan_cancellation = std::move(cancellation);
        discovery_dialog_ = { std::move(dialog) };
        // 변경 작업과 같은 이유로 등록한다 (설계 L3).
        registry_->add(*discovery_dialog_->scan_cancellation);
    }

    void logic_controller::handle_discovery_completed(discovery_completed_event event)
    {
        // 닫힌 dialog나 다른 탐색의 늦은 결과는 버린다.
        if (discovery_dialog_.has_value() == false || event.operation_id != discovery_dialog_->scan_operation_id)
            return;

        discovery_dialog_->scan_operation_id = 0;
        discovery_dialog_->scan_cancellation.reset();
        discovery_dialog_->loading = false;
        discovery_dialog_->result = std::move(event.result);
        discovery_dialog_->scroll_offset = 0.0f;

        // 등록 가능한 후보는 기본 체크다 (stage-8-plan 5.2).
        discovery_dialog_->checked.assign(discovery_dialog_->result.candidates.size(), false);
        for (std::size_t index = 0; index < discovery_dialog_->result.candidates.size(); ++index)
            if (discovery_dialog_->result.candidates[index].selectable())
                discovery_dialog_->checked[index] = true;

        if (discovery_dialog_->result.completed == false)
        {
            discovery_dialog_->message = u8"탐색이 끝까지 완료되지 않았습니다.";
            for (const diagnostic& value : discovery_dialog_->result.diagnostics)
                if (value.severity == diagnostic_severity::error)
                {
                    discovery_dialog_->message = value.message;
                    break;
                }
        }
    }

    void logic_controller::handle_toggle_discovery_candidate(const std::size_t index)
    {
        if (discovery_dialog_.has_value() == false || discovery_dialog_->loading || discovery_dialog_->register_operation_id != 0)
            return;
        if (index >= discovery_dialog_->result.candidates.size() || discovery_dialog_->result.candidates[index].selectable() == false)
            return;

        discovery_dialog_->checked[index] = discovery_dialog_->checked[index] == false;
        discovery_dialog_->message.clear();
    }

    void logic_controller::handle_confirm_discovery()
    {
        if (discovery_dialog_.has_value() == false || discovery_dialog_->loading || discovery_dialog_->register_operation_id != 0 || shutting_down_ || document_.has_value() == false)
            return;

        std::vector<discovery_candidate> selected {};
        for (std::size_t index = 0; index < discovery_dialog_->result.candidates.size(); ++index)
            if (discovery_dialog_->checked[index])
                selected.push_back(discovery_dialog_->result.candidates[index]);
        if (selected.empty())
            return;

        // 진행 중인 일반 저장과 등록이 겹치면 store의 revision 충돌로 실패한다.
        // 자초한 충돌 대신 안내를 표시하고 저장이 끝난 뒤 다시 시도하게 한다.
        if (pending_save_operation_id_ != 0)
        {
            discovery_dialog_->message = u8"문서 저장이 진행 중입니다. 잠시 후 다시 시도하세요.";
            return;
        }

        operation_request request { make_request(operation_kind::register_projects, nullptr, 0) };
        request.document = document_;
        request.revision = revision_;
        request.discovery_selection = std::move(selected);
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
        {
            discovery_dialog_->message = u8"등록 작업을 제출하지 못했습니다.";
            return;
        }

        discovery_dialog_->register_operation_id = operation_id;
        discovery_dialog_->message.clear();
    }

    void logic_controller::handle_projects_registered(projects_registered_event event)
    {
        if (discovery_dialog_.has_value() == false || event.operation_id != discovery_dialog_->register_operation_id)
            return;
        discovery_dialog_->register_operation_id = 0;

        if (event.document.has_value() == false || event.revision.has_value() == false)
        {
            // 실패(저장 충돌 포함)는 dialog를 유지한 채 사유를 표시한다. 사용자는
            // 그대로 다시 시도할 수 있다 (stage-8-plan 5.2).
            discovery_dialog_->message = u8"등록에 실패했습니다.";
            for (const diagnostic& value : event.diagnostics)
                if (value.severity == diagnostic_severity::error)
                {
                    discovery_dialog_->message = value.message;
                    break;
                }
            return;
        }

        // 등록된 문서를 활성 문서로 바꾼다 (단계 5 계약). 기존 카드의 상태·로그를
        // 보존하기 위해 install_document 대신 새 프로젝트만 카드로 추가한다.
        document_ = std::move(*event.document);
        revision_ = std::move(*event.revision);
        for (const project_definition& project : document_->projects)
        {
            if (find_card(project.id) != nullptr)
                continue;

            card_state card {};
            card.project = project;
            card.snapshot.project = project.id;
            card.generation = 1;
            cards_.push_back(std::move(card));

            // 새 카드는 로컬 상태를 곧바로 조회한다 (plan 5.1).
            card_state& added { cards_.back() };
            if (added.project.enabled)
            {
                added.busy = true;
                static_cast<void>(submitter_->submit(make_request(operation_kind::query_local, &added, added.generation)));
            }
        }
        discovery_dialog_.reset();
        // 등록으로 카드가 늘었으면 로그 폴더 이름도 다시 계산한다 (A4.1).
        publish_log_targets();
    }

    void logic_controller::handle_close_document()
    {
        if (shutting_down_ || document_.has_value() == false)
            return;

        // 카드가 사라지기 전에 진행 중인 변경 작업을 취소한다 (문서 열기와 같다).
        cancel_running_changes();
        // 아직 문서에 반영되지 않은 창 배치는 닫기 전에 한 번 내보낸다. 이미 제출된
        // 저장은 worker가 끝까지 수행하고 늦은 결과는 id 비교로 버려진다.
        if (window_placement_dirty_)
        {
            window_placement_dirty_ = false;
            request_save();
        }

        document_path_.clear();
        document_.reset();
        revision_ = {};
        cards_.clear();
        notices_.clear();
        save_notice_.clear();
        selected_.reset();
        filter_.clear();
        scroll_offset_ = 0.0f;
        document_loading_ = false;
        reset_log_view_state();
        // 이전 문서를 가리키던 dialog와 메뉴는 의미가 없다.
        switch_dialog_.reset();
        settings_dialog_.reset();
        local_changes_dialog_.reset();
        context_menu_.reset();
        if (discovery_dialog_.has_value() && discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();
        discovery_dialog_.reset();
        // 문서를 닫으면 다시 "문서 없이 쓰는" 상태다. 문서가 남긴 크기·위치를 그대로
        // 두지 않고 앱 단위 배치로 되돌린다 (D1). 게시 번호를 올려 UI thread가 한 번
        // 적용한다 — 문서를 열 때 문서 배치를 적용하는 것과 같은 경로다.
        window_placement_ = app_settings_.window;
        current_placement_ = app_settings_.window;
        if (window_placement_.has_value())
            ++window_placement_revision_;
        // 진행 중이던 저장·생성 결과는 도착해도 버린다.
        pending_save_operation_id_ = 0;
        save_queued_ = false;
        pending_generation_operation_id_ = 0;
        // 문서를 닫으면 더 쌓을 로그가 없다. 파일은 그대로 두고 적재만 멈춘다.
        publish_log_targets();
    }

    void logic_controller::handle_show_notice(show_notice_intent intent)
    {
        notice_dialog_view dialog {};
        dialog.title = std::move(intent.title);
        dialog.lines = std::move(intent.lines);
        dialog.error = intent.error;
        notice_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_app_settings_loaded(app_settings_loaded_event event)
    {
        if (event.operation_id != pending_app_settings_load_id_)
            return;
        pending_app_settings_load_id_ = 0;

        // 읽기가 끝나기 전에 문서를 열었으면 그 항목이 파일의 어떤 항목보다 최신이다.
        // 읽은 목록을 바탕으로 깔고 그 위에 오래된 것부터 다시 올린다.
        const std::vector<recent_document> pending { std::move(app_settings_.recent_documents) };
        // 읽기 전에 이미 받은 창 배치(시작 직후 종료의 드문 경로)도 파일 값보다
        // 최신이라 보존한다.
        const std::optional<window_placement> pending_window { app_settings_window_dirty_ ? app_settings_.window : std::nullopt };
        app_settings_ = std::move(event.settings);
        app_settings_shadow_ = std::move(event.shadow_source_json);
        app_settings_loaded_ = true;
        if (pending_window.has_value())
            app_settings_.window = pending_window;
        for (std::size_t index = pending.size(); index > 0; --index)
        {
            const recent_document& value { pending[index - 1] };
            touch_recent_document(app_settings_, value.path, value.opened_at);
        }

        for (const diagnostic& value : event.diagnostics)
            if (value.severity != diagnostic_severity::information)
                notices_.push_back(value.message);

        // 아직 어떤 배치도 적용되지 않았으면 앱 설정의 마지막 배치를 복원한다 (G1).
        // 문서가 자기 배치를 이미 적용했으면(=revision이 올라갔으면) 문서가 우선이다.
        if (window_placement_revision_ == 0 && app_settings_.window.has_value())
        {
            window_placement_ = app_settings_.window;
            ++window_placement_revision_;
        }

        if (pending.empty() == false || app_settings_save_queued_)
        {
            app_settings_save_queued_ = false;
            request_app_settings_save();
        }
    }

    void logic_controller::handle_app_settings_saved(app_settings_saved_event event)
    {
        if (event.operation_id != pending_app_settings_save_id_)
            return;
        pending_app_settings_save_id_ = 0;

        if (event.succeeded)
            app_settings_shadow_ = std::move(event.shadow_source_json);
        else if (app_settings_save_notified_ == false)
        {
            // 실행 파일 폴더가 보호된 위치면 매번 실패한다. 사유는 한 번만 알리고
            // 최근 목록은 이 세션의 메모리에만 남긴다 (app-shell-design A1.1).
            app_settings_save_notified_ = true;
            for (const diagnostic& value : event.diagnostics)
                if (value.severity != diagnostic_severity::information)
                {
                    notices_.push_back(value.message);
                    break;
                }
        }

        if (app_settings_save_queued_)
        {
            app_settings_save_queued_ = false;
            request_app_settings_save();
        }
    }

    void logic_controller::handle_remove_recent_document(const remove_recent_document_intent& intent)
    {
        if (remove_recent_document(app_settings_, intent.path) == false)
            return;
        request_app_settings_save();
    }

    void logic_controller::handle_toggle_recent_document_pin(const toggle_recent_document_pin_intent& intent)
    {
        // 지금 상태의 반대로 바꾼다. 목록에 없는 경로는 아무 일도 하지 않는다.
        const std::u8string key { recent_document_key(intent.path) };
        const auto found { std::ranges::find_if(app_settings_.recent_documents, [&key](const recent_document& value) { return recent_document_key(value.path) == key; }) };
        if (found == app_settings_.recent_documents.end())
            return;
        if (set_recent_document_pinned(app_settings_, intent.path, found->pinned == false) == false)
            return;
        request_app_settings_save();
    }

    void logic_controller::record_recent_document()
    {
        if (document_path_.empty())
            return;

        touch_recent_document(app_settings_, document_path_, format_utc_timestamp(std::chrono::system_clock::now()));
        request_app_settings_save();
    }

    void logic_controller::request_app_settings_save()
    {
        if (shutting_down_)
            return;
        // 읽기가 끝나기 전에 저장하면 파일에 있던 항목을 지운다. 결과가 도착한 뒤
        // 합쳐서 한 번만 쓴다.
        if (app_settings_loaded_ == false || pending_app_settings_save_id_ != 0)
        {
            app_settings_save_queued_ = true;
            return;
        }

        operation_request request { make_request(operation_kind::save_app_settings, nullptr, 0) };
        request.app_settings_payload = app_settings_;
        request.app_settings_shadow = app_settings_shadow_;
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;
        pending_app_settings_save_id_ = operation_id;
    }

    void logic_controller::handle_cancel_discovery_dialog()
    {
        if (discovery_dialog_.has_value() == false)
            return;
        // 등록 실행 중에는 닫지 않는다. 결과를 dialog가 보고해야 하고 store 저장은
        // 취소할 수 없다.
        if (discovery_dialog_->register_operation_id != 0)
            return;

        if (discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();
        discovery_dialog_.reset();
    }

    void logic_controller::handle_discovery_dialog_scroll(const float delta)
    {
        if (discovery_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(discovery_dialog_->result.candidates.size()) * layout_discovery_dialog_row_height() };
        float maximum { content - layout_discovery_dialog_list_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { discovery_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        discovery_dialog_->scroll_offset = offset;
    }

    std::vector<std::u8string> logic_controller::filtered_font_families() const
    {
        std::vector<std::u8string> families {};
        if (font_dialog_.has_value() == false)
            return families;

        // 조합 중이면 조합 글이 섞인 표시 글로 먼저 거르고, 그 글로 하나도 맞지
        // 않으면 확정 글로 물러선다 (search_query_for).
        const auto any_match = [this](const std::u8string_view probe) {
            if (contains_ignoring_ascii_case(default_font_display_name, probe))
                return true;
            for (const std::u8string& family : font_families_)
                if (contains_ignoring_ascii_case(family, probe))
                    return true;
            return false;
        };
        const std::u8string query { search_query_for(text_input_target::font_filter, font_dialog_->query.text, any_match) };

        // 첫 항목은 내장 글꼴로 되돌리는 선택지다. 표시 이름으로 걸러진다.
        families.reserve(font_families_.size() + 1u);
        if (contains_ignoring_ascii_case(default_font_display_name, query))
            families.emplace_back();
        for (const std::u8string& family : font_families_)
            if (contains_ignoring_ascii_case(family, query))
                families.push_back(family);
        return families;
    }

    void logic_controller::handle_open_generate_document()
    {
        if (shutting_down_)
            return;
        generate_dialog_ = { generate_dialog_state {} };
    }

    void logic_controller::handle_confirm_generate_document()
    {
        if (generate_dialog_.has_value() == false || generate_dialog_->executing)
            return;
        const std::u8string path { generate_document_path() };
        if (path.empty())
            return;

        generate_document_intent intent {};
        intent.scan_root = generate_dialog_->scan_root;
        intent.document_path = path;
        handle_generate_document(intent);
        generate_dialog_->executing = pending_generation_operation_id_ != 0;
        if (generate_dialog_->executing == false)
            generate_dialog_->message = u8"문서 생성을 시작하지 못했습니다.";
    }

    std::u8string logic_controller::generate_document_path() const
    {
        if (generate_dialog_.has_value() == false)
            return {};
        const std::u8string_view name { generate_dialog_->name.text };
        if (name.empty() || generate_dialog_->scan_root.empty())
            return {};
        if (generate_dialog_->same_folder == false && generate_dialog_->location.empty())
            return {};
        if (generate_document_name_error(name).empty() == false)
            return {};

        std::u8string folder { generate_dialog_->same_folder ? generate_dialog_->scan_root : generate_dialog_->location };
        while (folder.empty() == false && (folder.back() == u8'\\' || folder.back() == u8'/'))
            folder.pop_back();
        if (folder.empty())
            return {};
        folder.push_back(u8'\\');
        folder.append(name);
        folder.append(u8".version-list");
        return folder;
    }

    void logic_controller::handle_open_font_dialog(const open_font_dialog_intent& intent)
    {
        if (shutting_down_)
            return;
        font_dialog_state dialog {};
        dialog.code = intent.code;
        font_dialog_ = { std::move(dialog) };

        // 지금 쓰는 글꼴을 미리 골라 둔다. 목록에 없으면 아무것도 고르지 않는다.
        // 환경설정 dialog의 **초안**을 기준으로 한다 — 문서 override를 편집 중일 수
        // 있다 (2026-08-23 지시).
        const font_settings current_fonts { settings_dialog_.has_value() ? font_settings { settings_dialog_->ui_font_family, settings_dialog_->code_font_family } : effective_fonts() };
        const std::u8string& current { intent.code ? current_fonts.code_family : current_fonts.ui_family };
        const std::vector<std::u8string> families { filtered_font_families() };
        for (std::size_t index = 0; index < families.size(); ++index)
            if (families[index] == current)
            {
                font_dialog_->selected = index;
                break;
            }
        handle_font_dialog_scroll(0.0f);
    }

    void logic_controller::handle_select_font_family(const std::size_t index)
    {
        if (font_dialog_.has_value() == false || index >= filtered_font_families().size())
            return;
        font_dialog_->selected = { index };
    }

    void logic_controller::handle_confirm_font_dialog()
    {
        if (font_dialog_.has_value() == false || font_dialog_->selected.has_value() == false)
            return;
        const std::vector<std::u8string> families { filtered_font_families() };
        if (*font_dialog_->selected >= families.size())
            return;

        // 글꼴도 테마·키 컬러와 같은 초안이다 (2026-08-23 지시). 고른 값은 환경설정
        // dialog의 초안으로 가고 `저장`에서 앱 설정 또는 문서 override가 된다.
        // 건드린 행은 문서 모드에서 "문서에 정의됨"이 된다 (G3.2 암묵 덮어쓰기).
        if (settings_dialog_.has_value())
        {
            if (font_dialog_->code)
            {
                settings_dialog_->code_font_family = families[*font_dialog_->selected];
                settings_dialog_->code_font_defined = true;
            }
            else
            {
                settings_dialog_->ui_font_family = families[*font_dialog_->selected];
                settings_dialog_->ui_font_defined = true;
            }
        }
        font_dialog_.reset();
    }

    void logic_controller::handle_font_dialog_scroll(const float delta)
    {
        if (font_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(filtered_font_families().size()) * layout_font_dialog_row_height() };
        float maximum { content - layout_font_dialog_list_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { font_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        font_dialog_->scroll_offset = offset;
    }

    void logic_controller::handle_open_settings()
    {
        // 문서가 열려 있으면 문서 override를, 없으면 전역 설정을 편집한다 (G3.2).
        if (shutting_down_)
            return;

        settings_dialog_state dialog {};
        dialog.document_mode = document_.has_value();
        // 초안은 유효 값에서 시작한다. 문서 모드에서 건드리지 않은 행은 정의되지
        // 않은 채 남아 앱 설정을 따른다.
        const workspace_settings effective { effective_settings() };
        dialog.git_path = effective.git_executable;
        dialog.svn_path = effective.svn_executable;
        if (effective.query_timeout_seconds.has_value())
        {
            const std::string digits { std::to_string(*effective.query_timeout_seconds) };
            dialog.timeout.text.append(digits.begin(), digits.end());
        }
        dialog.update_submodules = effective.update_submodules;
        dialog.ignore_local_changes = effective.ignore_local_changes;
        dialog.write_log_files = effective.write_log_files;
        // 외양도 같은 초안 규칙이다 (D4). 유효 값에서 시작한다.
        const appearance_settings appearance { effective_appearance() };
        dialog.theme = appearance.theme;
        dialog.accent_id = appearance.accent_id;
        const font_settings fonts { effective_fonts() };
        dialog.ui_font_family = fonts.ui_family;
        dialog.code_font_family = fonts.code_family;
        if (dialog.document_mode)
        {
            dialog.git_defined = document_->settings.git_executable.has_value();
            dialog.svn_defined = document_->settings.svn_executable.has_value();
            dialog.timeout_defined = document_->settings.query_timeout_seconds.has_value();
            dialog.submodules_defined = document_->settings.update_submodules.has_value();
            dialog.ignore_local_defined = document_->settings.ignore_local_changes.has_value();
            dialog.log_files_defined = document_->settings.write_log_files.has_value();
            dialog.theme_defined = document_->appearance.theme.has_value();
            dialog.accent_defined = document_->appearance.accent_id.has_value();
            dialog.ui_font_defined = document_->fonts.ui_family.has_value();
            dialog.code_font_defined = document_->fonts.code_family.has_value();
        }
        settings_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_set_settings_executable(set_settings_executable_intent intent)
    {
        // 닫힌 뒤 도착한 파일 선택 결과는 버린다.
        if (settings_dialog_.has_value() == false)
            return;

        if (intent.tool == repository_kind::git)
        {
            settings_dialog_->git_path = std::move(intent.path);
            settings_dialog_->git_defined = true;
        }
        else if (intent.tool == repository_kind::subversion)
        {
            settings_dialog_->svn_path = std::move(intent.path);
            settings_dialog_->svn_defined = true;
        }
    }

    void logic_controller::handle_clear_settings_executable(const clear_settings_executable_intent& intent)
    {
        if (settings_dialog_.has_value() == false)
            return;

        // 지우기는 두 모드 모두 빈 값(자동 탐색)이다. 문서 모드에서는 값을 건드린
        // 것이므로 그 행이 정의된다 — 문서 정의를 삭제하려면 `덮어씀` 배지를 쓴다
        // (2026-08-22 지시).
        if (intent.tool == repository_kind::git)
        {
            settings_dialog_->git_path.clear();
            settings_dialog_->git_defined = true;
        }
        else if (intent.tool == repository_kind::subversion)
        {
            settings_dialog_->svn_path.clear();
            settings_dialog_->svn_defined = true;
        }
    }

    void logic_controller::handle_select_settings_tab(const select_settings_tab_intent& intent)
    {
        // 탭은 초안이 아니라 보기 상태다 (S1.2). 다른 탭에서 고친 초안은 남는다.
        if (settings_dialog_.has_value() == false)
            return;
        settings_dialog_->active_tab = intent.tab;
        // 탭을 옮기면 항목 열의 맨 위부터 본다. 앞 탭의 스크롤을 물려받으면 짧은
        // 탭에서 빈 화면이 보인다 (E1.4). 스크롤 **한계**도 이 시점부터 새 탭의
        // 내용으로 다시 계산된다 — `settings_dialog_metrics`가 활성 탭을 본다
        // (2026-08-23 지시).
        settings_dialog_->scroll_offset = 0.0f;
    }

    // 환경설정 dialog의 표시 모델이다. snapshot 게시와 스크롤 한계 계산이 같은
    // 값을 봐야 하므로 한곳에서 만든다 (E1.3). `scroll_offset`은 호출자가 채운다 —
    // 한계를 구하는 쪽이 아직 그 값을 모른다.
    settings_dialog_view logic_controller::build_settings_dialog_view() const
    {
        settings_dialog_view dialog {};
        dialog.active_tab = settings_dialog_->active_tab;
        dialog.document_mode = settings_dialog_->document_mode;
        dialog.git_path = settings_dialog_->git_path;
        dialog.svn_path = settings_dialog_->svn_path;
        dialog.timeout = { settings_dialog_->timeout.text, settings_dialog_->timeout.caret, settings_dialog_->timeout.anchor };
        apply_composition(text_input_target::settings_timeout, dialog.timeout);
        // 글꼴도 테마·키 컬러와 같은 초안이다 (2026-08-23 지시).
        dialog.ui_font_family = settings_dialog_->ui_font_family;
        dialog.code_font_family = settings_dialog_->code_font_family;
        dialog.update_submodules = settings_dialog_->update_submodules;
        dialog.ignore_local_changes = settings_dialog_->ignore_local_changes;
        dialog.write_log_files = settings_dialog_->write_log_files;
        // 외양도 초안이다 (D4). 화면 색은 `저장` 전까지 바뀌지 않는다.
        dialog.theme = settings_dialog_->theme;
        dialog.accent_id = settings_dialog_->accent_id;
        // 문서 모드에서 정의되지 않은 행은 "앱 설정 따름"으로 표시된다 (G3.2).
        if (settings_dialog_->document_mode)
        {
            dialog.git_follows_app = settings_dialog_->git_defined == false;
            dialog.svn_follows_app = settings_dialog_->svn_defined == false;
            dialog.timeout_follows_app = settings_dialog_->timeout_defined == false;
            dialog.submodules_follows_app = settings_dialog_->submodules_defined == false;
            dialog.ignore_local_follows_app = settings_dialog_->ignore_local_defined == false;
            dialog.log_files_follows_app = settings_dialog_->log_files_defined == false;
            dialog.theme_follows_app = settings_dialog_->theme_defined == false;
            dialog.accent_follows_app = settings_dialog_->accent_defined == false;
            dialog.ui_font_follows_app = settings_dialog_->ui_font_defined == false;
            dialog.code_font_follows_app = settings_dialog_->code_font_defined == false;
        }
        // 검증 메시지와 확인 가능 여부는 logic이 한곳에서 정한다. 첫 오류만
        // 표시해도 확인이 막혀 있어 사용자는 고칠 것을 하나씩 안내받는다.
        const std::u8string_view git_error { settings_executable_error(settings_dialog_->git_path) };
        const std::u8string_view svn_error { settings_executable_error(settings_dialog_->svn_path) };
        const std::u8string_view timeout_error { settings_timeout_error(settings_dialog_->timeout.text) };
        if (git_error.empty() == false)
            dialog.message = std::u8string { u8"Git: " } + std::u8string { git_error };
        else if (svn_error.empty() == false)
            dialog.message = std::u8string { u8"SVN: " } + std::u8string { svn_error };
        else if (timeout_error.empty() == false)
            dialog.message = std::u8string { timeout_error };
        dialog.can_confirm = git_error.empty() && svn_error.empty() && timeout_error.empty();
        return dialog;
    }
    void logic_controller::handle_settings_scroll(const float delta)
    {
        if (settings_dialog_.has_value() == false)
            return;

        // element와 같은 함수로 한계를 구한다. 다르면 휠이 헛돌거나 마지막 항목이
        // 잘린다 (E1.3).
        const float maximum { ui::settings_dialog_metrics(build_settings_dialog_view(), window_height_ / scale_).maximum_scroll };
        float offset { settings_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        settings_dialog_->scroll_offset = offset;
    }

    void logic_controller::handle_clear_settings_override(const clear_settings_override_intent& intent)
    {
        // 문서 모드에서만 의미가 있다. 그 행의 문서 정의를 지우고 초안에는 앱의
        // 값이 다시 보인다 (G3.2 `덮어씀` 배지). 외양 항목은 문서를 곧바로 고치므로
        // 문서가 사라진 뒤 도착한 늦은 배지 클릭도 막는다.
        if (settings_dialog_.has_value() == false || settings_dialog_->document_mode == false || document_.has_value() == false)
            return;

        switch (intent.field)
        {
        case settings_override_field::git_executable:
            settings_dialog_->git_path = app_settings_.settings.git_executable;
            settings_dialog_->git_defined = false;
            break;
        case settings_override_field::svn_executable:
            settings_dialog_->svn_path = app_settings_.settings.svn_executable;
            settings_dialog_->svn_defined = false;
            break;
        case settings_override_field::query_timeout:
            settings_dialog_->timeout.text.clear();
            if (app_settings_.settings.query_timeout_seconds.has_value())
            {
                const std::string digits { std::to_string(*app_settings_.settings.query_timeout_seconds) };
                settings_dialog_->timeout.text.append(digits.begin(), digits.end());
            }
            settings_dialog_->timeout_defined = false;
            break;
        case settings_override_field::update_submodules:
            settings_dialog_->update_submodules = app_settings_.settings.update_submodules;
            settings_dialog_->submodules_defined = false;
            break;
        case settings_override_field::ignore_local_changes:
            settings_dialog_->ignore_local_changes = app_settings_.settings.ignore_local_changes;
            settings_dialog_->ignore_local_defined = false;
            break;
        case settings_override_field::write_log_files:
            settings_dialog_->write_log_files = app_settings_.settings.write_log_files;
            settings_dialog_->log_files_defined = false;
            break;
        case settings_override_field::theme:
            settings_dialog_->theme = app_settings_.appearance.theme;
            settings_dialog_->theme_defined = false;
            break;
        case settings_override_field::accent:
            settings_dialog_->accent_id = app_settings_.appearance.accent_id;
            settings_dialog_->accent_defined = false;
            break;
        case settings_override_field::ui_font:
            settings_dialog_->ui_font_family = app_settings_.fonts.ui_family;
            settings_dialog_->ui_font_defined = false;
            break;
        case settings_override_field::code_font:
            settings_dialog_->code_font_family = app_settings_.fonts.code_family;
            settings_dialog_->code_font_defined = false;
            break;
        }
    }

    void logic_controller::handle_confirm_settings()
    {
        if (settings_dialog_.has_value() == false || shutting_down_)
            return;
        // 문서 모드 dialog가 열린 채 문서가 사라진 경우(닫기 등)의 늦은 확인은
        // 버린다.
        if (settings_dialog_->document_mode && document_.has_value() == false)
            return;
        // 버튼 비활성과 별개로 늦게 도착한 확인도 막는다 (view의 can_confirm과 같은
        // 판정이다).
        if (settings_executable_error(settings_dialog_->git_path).empty() == false || settings_executable_error(settings_dialog_->svn_path).empty() == false
            || settings_timeout_error(settings_dialog_->timeout.text).empty() == false)
            return;

        const std::optional<std::int32_t> timeout { parse_settings_timeout(settings_dialog_->timeout.text) };
        const workspace_settings previous_effective { effective_settings() };

        if (settings_dialog_->document_mode)
        {
            // 건드린(정의된) 행만 문서 override로 남는다 (G3.2 암묵 덮어쓰기).
            // show_relative_paths는 도구 막대 토글의 몫이라 그대로 둔다. 제한 시간은
            // 빈 칸이면 "따로 정하지 않음"이라 정의를 거둔다.
            workspace_settings_overrides overrides { document_->settings };
            overrides.git_executable = settings_dialog_->git_defined ? std::optional<std::u8string> { settings_dialog_->git_path } : std::nullopt;
            overrides.svn_executable = settings_dialog_->svn_defined ? std::optional<std::u8string> { settings_dialog_->svn_path } : std::nullopt;
            overrides.query_timeout_seconds = settings_dialog_->timeout_defined && timeout.has_value() ? timeout : std::nullopt;
            overrides.update_submodules = settings_dialog_->submodules_defined ? std::optional<bool> { settings_dialog_->update_submodules } : std::nullopt;
            overrides.ignore_local_changes = settings_dialog_->ignore_local_defined ? std::optional<bool> { settings_dialog_->ignore_local_changes } : std::nullopt;
            overrides.write_log_files = settings_dialog_->log_files_defined ? std::optional<bool> { settings_dialog_->write_log_files } : std::nullopt;

            appearance_overrides appearance { document_->appearance };
            appearance.theme = settings_dialog_->theme_defined ? std::optional<theme_preference> { settings_dialog_->theme } : std::nullopt;
            appearance.accent_id = settings_dialog_->accent_defined ? std::optional<std::u8string> { settings_dialog_->accent_id } : std::nullopt;

            // 글꼴도 같은 규칙이다 (2026-08-23 지시). 빈 이름도 정의다 — "이 문서는
            // 내장 글꼴을 쓴다"는 뜻이라 앱 설정을 따르는 것과 구별된다.
            font_overrides fonts { document_->fonts };
            fonts.ui_family = settings_dialog_->ui_font_defined ? std::optional<std::u8string> { settings_dialog_->ui_font_family } : std::nullopt;
            fonts.code_family = settings_dialog_->code_font_defined ? std::optional<std::u8string> { settings_dialog_->code_font_family } : std::nullopt;

            const bool settings_changed { (overrides == document_->settings) == false };
            const bool appearance_changed { (appearance == document_->appearance) == false };
            const bool fonts_changed { (fonts == document_->fonts) == false };
            if (settings_changed)
                document_->settings = std::move(overrides);
            if (appearance_changed)
                document_->appearance = std::move(appearance);
            if (fonts_changed)
                document_->fonts = std::move(fonts);
            if (settings_changed || appearance_changed || fonts_changed)
                request_save();
        }
        else
        {
            // 전역 모드는 모든 행이 구체 값이다. show_relative_paths는 dialog에
            // 행이 없어 유지된다.
            workspace_settings global { app_settings_.settings };
            global.git_executable = settings_dialog_->git_path;
            global.svn_executable = settings_dialog_->svn_path;
            global.query_timeout_seconds = timeout;
            global.update_submodules = settings_dialog_->update_submodules;
            global.ignore_local_changes = settings_dialog_->ignore_local_changes;
            global.write_log_files = settings_dialog_->write_log_files;
            // 전역 모드는 외양도 구체 값이다 (D4).
            appearance_settings appearance { app_settings_.appearance };
            appearance.theme = settings_dialog_->theme;
            if (settings_dialog_->accent_id.empty() == false)
                appearance.accent_id = settings_dialog_->accent_id;

            // 전역 모드는 글꼴도 구체 값이다.
            font_settings fonts { app_settings_.fonts };
            fonts.ui_family = settings_dialog_->ui_font_family;
            fonts.code_family = settings_dialog_->code_font_family;

            const bool settings_changed { (global == app_settings_.settings) == false };
            const bool appearance_changed { (appearance == app_settings_.appearance) == false };
            const bool fonts_changed { (fonts == app_settings_.fonts) == false };
            if (settings_changed)
                app_settings_.settings = std::move(global);
            if (appearance_changed)
                app_settings_.appearance = std::move(appearance);
            if (fonts_changed)
                app_settings_.fonts = std::move(fonts);
            if (settings_changed || appearance_changed || fonts_changed)
                request_app_settings_save();
        }

        // 유효 설정이 실제로 바뀐 경우에만 적재 대상을 갱신하고 활성 카드를
        // 재조회한다 (G3.2 — 문서가 전부 덮어쓰고 있으면 전역 변경은 재조회하지
        // 않는다). 요청마다 유효 설정 사본이 실리므로 저장 완료를 기다릴 필요가
        // 없다.
        if ((effective_settings() == previous_effective) == false)
        {
            publish_log_targets();
            for (card_state& card : cards_)
                if (card.project.enabled)
                    request_refresh(card);
        }
        settings_dialog_.reset();
    }

    void logic_controller::handle_begin_switch(const begin_switch_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr || card->project.enabled == false)
            return;

        // Git은 remote-first 후보를, SVN은 browser root/current URL을 조회한다. 조회는
        // 카드를 busy로 만들지 않아 dialog가 열린 동안에도 UI가 멈추지 않는다.
        operation_request request { make_request(operation_kind::query_switch_candidates, card, card->generation) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        switch_dialog_state dialog {};
        dialog.card = intent.id;
        dialog.candidates_operation_id = operation_id;
        dialog.subversion = card->snapshot.kind == repository_kind::subversion;
        switch_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_switch_candidates(switch_candidates_event event)
    {
        // 닫힌 dialog나 다른 조회의 늦은 결과는 버린다.
        if (switch_dialog_.has_value() == false || event.operation_id != switch_dialog_->candidates_operation_id)
            return;

        switch_dialog_->loading = false;
        switch_dialog_->candidates = std::move(event.result);
        switch_dialog_->selected.reset();
        switch_dialog_->svn_browser.reset();
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->scroll_offset = 0.0f;

        if (switch_dialog_->subversion == false)
            return;

        if (switch_dialog_->candidates.svn_browser.has_value() == false)
        {
            if (switch_dialog_->candidates.diagnostics.empty() == false)
                switch_dialog_->message = switch_dialog_->candidates.diagnostics.front().message;
            else
                switch_dialog_->message = u8"SVN 저장소 정보를 조회하지 못했습니다.";
            return;
        }

        switch_dialog_->svn_browser = { make_svn_repository_browser(*switch_dialog_->candidates.svn_browser) };
        svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
        // 주소 막대는 현재 작업 복사본의 경로로 시작한다.
        text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(browser.root_url, browser.current_url), true);

        // 창을 열 때는 현재 위치까지 펼쳐 둔다 (2026-08-22 검수 정정: 자기 branch
        // 까지는 재귀 조회가 필요하다). 그 아래와 다른 가지는 여전히 사용자가 열
        // 때만 조회한다 — 저장소 전체를 미리 읽지 않는다.
        const std::u8string root_url { browser.root_url };
        const svn_browser_reveal_progress progress { begin_svn_browser_reveal(browser, browser.current_url) };
        if (progress.query_url.has_value())
            submit_svn_directory_query(*progress.query_url);
        else if (begin_svn_browser_query(browser, root_url))
            submit_svn_directory_query(root_url);
    }

    void logic_controller::handle_select_switch_candidate(const std::size_t index)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading)
            return;
        // index는 거른 목록 기준이다 (view가 그린 것과 같은 순서).
        if (index >= filtered_switch_candidates().size())
            return;

        switch_dialog_->selected = { index };
        // 다른 후보를 고르면 이전 검증·거부 메시지와 확인 단계는 의미가 없다.
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->message.clear();
    }

    void logic_controller::handle_select_svn_browser_node(const std::u8string_view url)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading || switch_dialog_->svn_browser.has_value() == false)
            return;
        if (select_svn_browser_node(*switch_dialog_->svn_browser, url) == false)
            return;
        switch_dialog_->message.clear();
        // 주소 막대는 고른 노드의 실제 경로를 따라간다 (repo-browser 원형 동작).
        text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(switch_dialog_->svn_browser->root_url, url), true);
    }

    void logic_controller::handle_toggle_svn_browser_node(const std::u8string_view url)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading || switch_dialog_->svn_browser.has_value() == false)
            return;
        const std::optional<std::u8string> query_url { toggle_svn_browser_node(*switch_dialog_->svn_browser, url) };
        switch_dialog_->message.clear();
        handle_switch_dialog_scroll(0.0f);
        if (query_url.has_value())
            submit_svn_directory_query(*query_url);
    }

    void logic_controller::handle_confirm_switch()
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->loading || switch_dialog_->executing)
            return;

        card_state* const card { find_card(switch_dialog_->card) };
        if (card == nullptr)
        {
            switch_dialog_.reset();
            return;
        }

        switch_candidate target {};
        if (switch_dialog_->svn_browser.has_value())
        {
            const svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
            if (browser.selected_url.empty())
                return;
            target.kind = switch_candidate_kind::subversion_url;
            target.display_name = browser.selected_url;
            target.target = browser.selected_url;
            if (target.target == browser.current_url)
            {
                switch_dialog_->message = u8"이미 현재 위치입니다. 다른 디렉터리를 선택하세요.";
                return;
            }
        }
        else
        {
            const std::vector<std::size_t> visible { filtered_switch_candidates() };
            if (switch_dialog_->selected.has_value() == false || *switch_dialog_->selected >= visible.size())
                return;
            target = switch_dialog_->candidates.candidates[visible[*switch_dialog_->selected]];
        }

        if (switch_dialog_->svn_browser.has_value() == false && candidate_is_current(card->snapshot, target, switch_dialog_->candidates.candidates))
        {
            switch_dialog_->message = u8"이미 현재 참조입니다. 다른 후보를 선택하세요.";
            return;
        }

        // tracking branch 생성은 명시적인 두 단계 확인을 거친다 (plan 3.3).
        if (target.requires_tracking_branch && switch_dialog_->tracking_confirm_pending == false)
        {
            switch_dialog_->tracking_confirm_pending = true;
            switch_dialog_->message = std::u8string { u8"local tracking branch를 만들고 전환합니다. 확인하려면 한 번 더 누르세요." };
            return;
        }
        if (target.requires_tracking_branch)
            target.tracking_branch_confirmed = true;

        if (card->busy)
        {
            switch_dialog_->message = u8"카드가 다른 작업을 실행 중입니다. 끝난 뒤 다시 시도하세요.";
            return;
        }

        begin_change(*card, operation_kind::switch_to, {}, &target);
        if (card->change_operation_id != 0)
        {
            switch_dialog_->executing = true;
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->message.clear();
        }
    }

    void logic_controller::handle_svn_directory(svn_directory_event event)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false || event.id != switch_dialog_->card)
            return;

        std::size_t query_index { switch_dialog_->directory_queries.size() };
        for (std::size_t index = 0; index < switch_dialog_->directory_queries.size(); ++index)
            if (switch_dialog_->directory_queries[index].operation_id == event.operation_id)
            {
                query_index = index;
                break;
            }
        if (query_index == switch_dialog_->directory_queries.size())
            return;

        const std::u8string expected_url { switch_dialog_->directory_queries[query_index].url };
        if (event.url != expected_url)
            return;
        switch_dialog_->directory_queries.erase(switch_dialog_->directory_queries.begin() + static_cast<std::ptrdiff_t>(query_index));

        const svn_browser_reveal_progress progress { complete_svn_browser_query(*switch_dialog_->svn_browser, expected_url, event.result) };
        handle_switch_dialog_scroll(0.0f);
        apply_svn_browser_reveal(progress);
    }

    void logic_controller::apply_svn_browser_reveal(const svn_browser_reveal_progress& progress)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false)
            return;
        if (progress.reached)
        {
            text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(switch_dialog_->svn_browser->root_url, switch_dialog_->svn_browser->selected_url), true);
            switch_dialog_->message.clear();
            // 도달한 곳을 눈으로 확인할 수 있어야 한다 — 창을 열 때와 주소 막대
            // 이동은 사용자가 그 행을 보려고 한 것이다 (2026-08-23 지시).
            scroll_svn_browser_selection_into_view();
            return;
        }
        if (progress.not_found)
        {
            switch_dialog_->message = std::u8string { u8"그 경로를 찾을 수 없습니다: " } + switch_dialog_->query.text;
            return;
        }
        if (progress.query_url.has_value())
        {
            // 한 걸음 내려갈 때마다 그 깊이를 따라간다 (2026-08-23 지시). 조회 하나가
            // 초 단위로 걸려서, 목표에 닿을 때까지 아무 데도 붙지 않으면 한참 동안
            // 어디까지 갔는지 알 수 없다. 주소 막대는 건드리지 않는다 — 사용자가 친
            // 목표가 도착할 때까지 보여야 한다.
            static_cast<void>(select_svn_browser_node(*switch_dialog_->svn_browser, *progress.query_url));
            scroll_svn_browser_selection_into_view();
            submit_svn_directory_query(*progress.query_url);
        }
    }

    void logic_controller::scroll_svn_browser_selection_into_view()
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false)
            return;
        // 행 index는 화면에 그려질 목록 그대로여야 한다 — 접힌 가지는 행이 없다.
        const std::u8string& selected { switch_dialog_->svn_browser->selected_url };
        const std::vector<svn_repository_browser_row> rows { build_svn_repository_browser_rows(*switch_dialog_->svn_browser) };
        std::optional<std::size_t> index {};
        if (selected.empty() == false)
            for (std::size_t row = 0; row < rows.size(); ++row)
                if (rows[row].kind == svn_browser_row_kind::directory && rows[row].url == selected)
                {
                    index = row;
                    break;
                }

        // 도달한 행을 목록 **가운데**에 놓는다 (2026-08-23 지시). 끝에 붙이면 그
        // 위나 아래가 보이지 않아 어디에 있는 가지인지 읽히지 않는다. 목록의 처음·
        // 끝 근처는 아래의 한계 고정이 자연스럽게 위아래로 붙인다.
        if (index.has_value())
        {
            const float viewport { switch_dialog_list_height(switch_dialog_->subversion, window_height_ / scale_) };
            const float top { static_cast<float>(*index) * layout_switch_dialog_row_height() };
            switch_dialog_->scroll_offset = top - (viewport - layout_switch_dialog_row_height()) / 2.0f;
        }
        // 찾지 못했어도 한계 고정은 한다 — 목록이 줄어든 뒤일 수 있다. 계산은 휠과
        // 같은 handler를 그대로 쓴다.
        handle_switch_dialog_scroll(0.0f);
    }

    void logic_controller::handle_set_text_composition(const set_text_composition_intent& intent)
    {
        const std::u8string before { displayed_text_of(intent.target) };
        if (intent.composing == false)
        {
            // 조합이 끝났으면 상태를 버린다. 확정된 글은 별도의 insert intent로 온다.
            if (composition_.has_value() && composition_->target == intent.target)
                composition_.reset();
        }
        else
        {
            // 조합 글은 초안에 넣지 않는다 (E2.3). 화면에 보일 글만 따로 들고 있다가
            // 조합이 끝나면 버린다.
            text_composition_state state {};
            state.target = intent.target;
            state.text = intent.text;
            state.caret = text_edit_clamp_offset(state.text, intent.caret);
            state.composing_begin = text_edit_clamp_offset(state.text, intent.composing_begin);
            state.composing_end = text_edit_clamp_offset(state.text, intent.composing_end);
            if (state.composing_end < state.composing_begin)
                state.composing_end = state.composing_begin;
            composition_ = std::move(state);
        }

        // 화면 글이 달라지면 점진 검색 목록도 달라진다 (D5). 편집과 같은 규칙으로
        // 사라질 수 있는 선택과 tracking 확인 단계를 남겨 두지 않는다 — 확인한 적
        // 없는 후보로 branch를 만들면 안 된다.
        if (displayed_text_of(intent.target) != before)
            reset_search_selection(intent.target);
    }

    std::u8string logic_controller::displayed_text_of(const text_input_target target) const
    {
        if (composition_.has_value() && composition_->target == target)
            return composition_->text;
        if (target == text_input_target::switch_query && switch_dialog_.has_value())
            return switch_dialog_->query.text;
        if (target == text_input_target::font_filter && font_dialog_.has_value())
            return font_dialog_->query.text;
        return {};
    }

    void logic_controller::reset_search_selection(const text_input_target target)
    {
        // SVN 질의 막대는 주소 막대라 목록을 거르지 않는다 (트리거를 눌러야 움직인다).
        if (target == text_input_target::switch_query && switch_dialog_.has_value() && switch_dialog_->subversion == false)
        {
            switch_dialog_->selected.reset();
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->scroll_offset = 0.0f;
        }
        else if (target == text_input_target::font_filter && font_dialog_.has_value())
        {
            font_dialog_->selected.reset();
            font_dialog_->scroll_offset = 0.0f;
        }
    }

    void logic_controller::apply_composition(const text_input_target target, text_input_view& view) const
    {
        if (composition_.has_value() == false || composition_->target != target)
            return;
        view.composing = true;
        view.composition_text = composition_->text;
        view.composition_caret = composition_->caret;
        view.composing_begin = composition_->composing_begin;
        view.composing_end = composition_->composing_end;
    }

    std::u8string logic_controller::search_query_for(const text_input_target target, const std::u8string& committed, const std::function<bool(std::u8string_view)>& any_match) const
    {
        // 조합이 없거나 다른 박스의 것이면 고를 것이 확정 글뿐이다.
        if (composition_.has_value() == false || composition_->target != target)
            return committed;

        // 조합 글이 섞인 표시 글이 먼저다.
        const std::u8string& displayed { composition_->text };
        // 확정 글이 비어 있으면 물러설 자리가 없다 — 빈 질의는 좁히기가 아니라
        // "전부"라서 첫 글자를 조합하는 동안 목록 전체가 되살아난다.
        if (committed.empty() || any_match == nullptr)
            return displayed;
        if (any_match(displayed))
            return displayed;
        // 하나도 맞지 않으면 조합 중인 글자를 뺀 자리로 물러선다.
        return committed;
    }

    void logic_controller::handle_edit_text_input(const edit_text_input_intent& intent)
    {
        // 닫힌 뒤 도착한 키 입력은 버린다. 초안을 가진 dialog가 대상이다.
        text_edit_state* state { nullptr };
        if (intent.target == text_input_target::settings_timeout && settings_dialog_.has_value())
            state = &settings_dialog_->timeout;
        else if (intent.target == text_input_target::switch_query && switch_dialog_.has_value() && switch_dialog_->executing == false)
            state = &switch_dialog_->query;
        else if (intent.target == text_input_target::font_filter && font_dialog_.has_value())
            state = &font_dialog_->query;
        else if (intent.target == text_input_target::generate_document_name && generate_dialog_.has_value() && generate_dialog_->executing == false)
            state = &generate_dialog_->name;
        if (state == nullptr)
            return;

        const std::u8string before { state->text };
        switch (intent.command)
        {
        case text_edit_command::insert:
            text_edit_insert(*state, intent.text);
            break;
        case text_edit_command::backspace:
            text_edit_backspace(*state);
            break;
        case text_edit_command::delete_forward:
            text_edit_delete_forward(*state);
            break;
        case text_edit_command::delete_word_left:
            text_edit_delete_word_left(*state);
            break;
        case text_edit_command::delete_word_right:
            text_edit_delete_word_right(*state);
            break;
        case text_edit_command::undo:
            static_cast<void>(text_edit_undo(*state));
            break;
        case text_edit_command::redo:
            static_cast<void>(text_edit_redo(*state));
            break;
        case text_edit_command::cut:
            static_cast<void>(text_edit_erase_selection(*state));
            break;
        case text_edit_command::clear:
            text_edit_select_all(*state);
            static_cast<void>(text_edit_erase_selection(*state));
            break;
        case text_edit_command::replace_all:
            // IME 조합이 확정한 글이다 (E2.3). 조합 중의 중간 단계는 초안에 들어오지
            // 않았으므로 조합 한 번이 실행 취소 한 덩어리가 된다.
            text_edit_replace_all(*state, intent.text, intent.offset);
            break;
        case text_edit_command::move_left:
            text_edit_move(*state, text_edit_motion::left, intent.extend);
            break;
        case text_edit_command::move_right:
            text_edit_move(*state, text_edit_motion::right, intent.extend);
            break;
        case text_edit_command::move_word_left:
            text_edit_move(*state, text_edit_motion::word_left, intent.extend);
            break;
        case text_edit_command::move_word_right:
            text_edit_move(*state, text_edit_motion::word_right, intent.extend);
            break;
        case text_edit_command::move_line_start:
            text_edit_move(*state, text_edit_motion::line_start, intent.extend);
            break;
        case text_edit_command::move_line_end:
            text_edit_move(*state, text_edit_motion::line_end, intent.extend);
            break;
        case text_edit_command::select_all:
            text_edit_select_all(*state);
            break;
        case text_edit_command::select_word:
            text_edit_select_word(*state, intent.offset);
            break;
        case text_edit_command::place_caret:
            text_edit_place_caret(*state, intent.offset, intent.extend);
            break;
        }

        if (intent.target == text_input_target::font_filter)
        {
            // 목록이 달라지므로 사라질 수 있는 선택을 남겨 두지 않는다.
            if (state->text != before)
            {
                font_dialog_->selected.reset();
                font_dialog_->scroll_offset = 0.0f;
            }
            return;
        }
        if (intent.target == text_input_target::generate_document_name)
        {
            if (state->text != before)
                generate_dialog_->message.clear();
            return;
        }
        if (intent.target == text_input_target::settings_timeout)
        {
            // 숫자만 받는 칸이다. 최대값(3600)이 4자리라 그 이상은 버린다. 붙여넣기가
            // 섞인 글을 넣어도 여기서 걸러진다 (field-feedback-design 1.3).
            std::u8string digits {};
            for (const char8_t value : state->text)
                if (value >= u8'0' && value <= u8'9' && digits.size() < 4)
                    digits.push_back(value);
            if (digits != state->text)
                text_edit_set_text(*state, digits, true);
            if (state->text != before)
                settings_dialog_->timeout_defined = true;
            return;
        }

        if (state->text == before)
            return;
        switch_dialog_->message.clear();
        // Git 필터는 즉시 적용되어 목록이 달라진다. 사라질 수 있는 선택과 확인
        // 단계를 남겨 두지 않는다. SVN은 트리거를 눌러야 움직인다.
        if (switch_dialog_->subversion == false)
        {
            switch_dialog_->selected.reset();
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->scroll_offset = 0.0f;
        }
    }

    void logic_controller::handle_submit_switch_query()
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading)
            return;

        if (switch_dialog_->subversion == false)
        {
            // Git은 이미 걸러져 있다. Enter는 첫 후보를 잡는 몫이다.
            const std::vector<std::size_t> visible { filtered_switch_candidates() };
            if (visible.empty())
                return;
            switch_dialog_->selected = { std::size_t { 0 } };
            switch_dialog_->tracking_confirm_pending = false;
            switch_dialog_->message.clear();
            return;
        }

        if (switch_dialog_->svn_browser.has_value() == false)
            return;
        const std::u8string target { resolve_svn_browser_path(*switch_dialog_->svn_browser, switch_dialog_->query.text) };
        if (target.empty())
        {
            switch_dialog_->message = std::u8string { u8"그 경로를 찾을 수 없습니다: " } + switch_dialog_->query.text;
            return;
        }
        switch_dialog_->message.clear();
        apply_svn_browser_reveal(begin_svn_browser_reveal(*switch_dialog_->svn_browser, target));
    }

    void logic_controller::handle_clear_switch_query()
    {
        // 지우기 버튼은 Git 필터에만 있다. SVN 주소는 비우면 뜻이 없다.
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->subversion)
            return;
        text_edit_set_text(switch_dialog_->query, {}, true);
        switch_dialog_->selected.reset();
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->scroll_offset = 0.0f;
        switch_dialog_->message.clear();
    }

    void logic_controller::handle_refresh_switch_dialog()
    {
        if (shutting_down_ || switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading)
            return;
        card_state* const card { find_card(switch_dialog_->card) };
        if (card == nullptr)
            return;

        if (switch_dialog_->svn_browser.has_value())
        {
            // 선택 노드(없으면 root)의 cache를 버리고 다시 조회한다. 진행 중이던
            // 조회의 결과는 bookkeeping을 비워 늦은 결과로 떨어뜨린다.
            svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
            std::u8string target { browser.selected_url.empty() ? browser.root_url : browser.selected_url };
            if (find_svn_browser_node(browser, target) == nullptr)
                target = browser.root_url;
            if (reset_svn_browser_node(browser, target) == false)
                return;
            switch_dialog_->directory_queries.clear();
            switch_dialog_->message.clear();
            text_edit_set_text(switch_dialog_->query, svn_browser_relative_path(browser.root_url, browser.selected_url), true);
            if (begin_svn_browser_query(browser, target))
                submit_svn_directory_query(target);
            handle_switch_dialog_scroll(0.0f);
            return;
        }

        // Git은 후보 목록 전체를 다시 조회한다. 필터 글은 그대로 둔다.
        operation_request request { make_request(operation_kind::query_switch_candidates, card, card->generation) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
        {
            switch_dialog_->message = u8"후보를 다시 조회하지 못했습니다.";
            return;
        }
        switch_dialog_->candidates_operation_id = operation_id;
        switch_dialog_->loading = true;
        switch_dialog_->selected.reset();
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->message.clear();
        switch_dialog_->scroll_offset = 0.0f;
    }

    void logic_controller::submit_svn_directory_query(std::u8string url)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->svn_browser.has_value() == false)
            return;
        card_state* const card { find_card(switch_dialog_->card) };
        if (card == nullptr)
            return;

        operation_request request { make_request(operation_kind::query_svn_directory, card, card->generation) };
        request.svn_repository_root_url = switch_dialog_->svn_browser->root_url;
        request.svn_directory_url = url;
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)))
        {
            switch_dialog_->directory_queries.push_back({ operation_id, std::move(url) });
            return;
        }

        svn_directory_query_result failure {};
        failure.error = svn_browser_query_error::failed;
        static_cast<void>(complete_svn_browser_query(*switch_dialog_->svn_browser, url, failure));
    }

    std::size_t logic_controller::switch_dialog_row_count() const
    {
        if (switch_dialog_.has_value() == false)
            return 0;
        if (switch_dialog_->svn_browser.has_value())
            return build_svn_repository_browser_rows(*switch_dialog_->svn_browser).size();
        return filtered_switch_candidates().size();
    }

    std::vector<std::size_t> logic_controller::filtered_switch_candidates() const
    {
        std::vector<std::size_t> visible {};
        if (switch_dialog_.has_value() == false)
            return visible;
        const std::vector<switch_candidate>& candidates { switch_dialog_->candidates.candidates };
        visible.reserve(candidates.size());

        // SVN의 질의 막대는 필터가 아니라 주소 막대라 목록을 거르지 않는다.
        if (switch_dialog_->subversion)
        {
            for (std::size_t index = 0; index < candidates.size(); ++index)
                visible.push_back(index);
            return visible;
        }

        // 조합 중이면 조합 글이 섞인 표시 글로 먼저 거르고, 그 글로 하나도 맞지
        // 않으면 확정 글로 물러선다 (search_query_for).
        const auto any_match = [&candidates](const std::u8string_view probe) {
            for (const switch_candidate& candidate : candidates)
                if (contains_ignoring_ascii_case(candidate.display_name, probe))
                    return true;
            return false;
        };
        const std::u8string query { search_query_for(text_input_target::switch_query, switch_dialog_->query.text, any_match) };

        for (std::size_t index = 0; index < candidates.size(); ++index)
            if (contains_ignoring_ascii_case(candidates[index].display_name, query))
                visible.push_back(index);
        return visible;
    }

    void logic_controller::handle_switch_dialog_scroll(const float delta)
    {
        if (switch_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(switch_dialog_row_count()) * layout_switch_dialog_row_height() };
        const float viewport { switch_dialog_list_height(switch_dialog_->subversion, window_height_ / scale_) };
        float maximum { content - viewport };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { switch_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        switch_dialog_->scroll_offset = offset;
    }

    void logic_controller::handle_open_context_menu(const open_context_menu_intent& intent)
    {
        if (shutting_down_)
            return;
        if (find_card(intent.id) == nullptr)
        {
            context_menu_.reset();
            return;
        }

        // 우클릭한 카드를 선택 카드로 만든 뒤 연다 (로그 pane 연동 유지, 3장).
        handle_select_card(select_card_intent { { intent.id } });
        context_menu_ = { context_menu_state { context_menu_kind::card, intent.id, intent.anchor_x, intent.anchor_y } };
    }

    void logic_controller::handle_open_document_context_menu(const open_document_context_menu_intent& intent)
    {
        if (shutting_down_)
            return;
        // 열린 문서가 없으면 열 메뉴가 없다 (T1). 배너도 같은 조건에서 intent를
        // 내지 않지만 늦게 도착한 요청을 여기서 한 번 더 막는다.
        if (document_.has_value() == false || document_path_.empty())
        {
            context_menu_.reset();
            return;
        }

        // 문서 메뉴는 선택 카드를 바꾸지 않는다 — 카드와 무관한 대상이다.
        context_menu_ = { context_menu_state { context_menu_kind::document, {}, intent.anchor_x, intent.anchor_y } };
    }

    void logic_controller::handle_select_context_menu_item(const select_context_menu_item_intent& intent)
    {
        // 메뉴가 이미 닫혔으면 늦게 도착한 클릭이다. 버린다.
        if (context_menu_.has_value() == false)
            return;

        // 문서(배너) 메뉴의 두 항목은 외부 열기뿐이라 UI thread가 앱 명령으로 곧바로
        // 처리한다. 여기로 오면 닫기만 한다.
        if ((context_menu_->kind == context_menu_kind::card) == false)
        {
            context_menu_.reset();
            return;
        }

        // 항목을 고르면 메뉴는 어느 경우에나 닫힌다 (예전 element가 닫기 액션을 함께
        // 내던 것과 같다). 대상 카드를 먼저 잡아 둔다.
        const project_id card { context_menu_->card };
        context_menu_.reset();

        card_state* const state { find_card(card) };
        if (state == nullptr)
            return;

        // 비활성 항목은 view에서 이미 걸렀지만, 메뉴가 떠 있는 동안 카드 상태가
        // 바뀌었을 수 있다. view를 만들 때와 같은 판정을 다시 본다 (늦은 클릭 방어).
        const bool can_change { state->project.enabled && state->busy == false && state->has_local_result && state->snapshot.availability == repository_availability::ready };
        const bool change_running { state->change_operation_id != 0 };

        switch (intent.entry)
        {
        case context_menu_entry::show_local_changes:
            handle_open_local_changes(open_local_changes_intent { card });
            break;
        case context_menu_entry::refresh:
            // `refresh_card_intent`의 처리와 같다.
            if (state->project.enabled)
                request_refresh(*state);
            break;
        case context_menu_entry::update:
            // submodule 여부는 logic이 문서 settings로 채운다 (카드 버튼과 동일).
            if (can_change && change_running == false)
                handle_request_update(request_update_intent { card, {} });
            break;
        case context_menu_entry::switch_to:
            if (can_change)
                handle_begin_switch(begin_switch_intent { card });
            break;
        case context_menu_entry::open_repository:
        case context_menu_entry::open_in_vscode:
        case context_menu_entry::open_document_folder:
        case context_menu_entry::open_document_in_vscode:
            // 외부 열기는 UI thread의 앱 명령이라 logic으로 오지 않는다. 닫기만 한다.
            break;
        }
    }

    void logic_controller::handle_set_theme_preference(const set_theme_preference_intent& intent)
    {
        // 외양도 다른 항목과 같은 초안이다 (D4). 화면 색은 `저장` 전까지 그대로이고,
        // 닫힌 뒤 도착한 클릭은 버린다.
        if (settings_dialog_.has_value() == false)
            return;

        settings_dialog_->theme = intent.theme;
        settings_dialog_->theme_defined = true;
    }

    void logic_controller::handle_set_accent(const set_accent_intent& intent)
    {
        if (settings_dialog_.has_value() == false || intent.accent_id.empty())
            return;

        settings_dialog_->accent_id = intent.accent_id;
        settings_dialog_->accent_defined = true;
    }

    void logic_controller::handle_open_local_changes(const open_local_changes_intent& intent)
    {
        if (shutting_down_)
            return;
        card_state* const card { find_card(intent.id) };
        if (card == nullptr)
            return;

        operation_request request { make_request(operation_kind::query_local_changes, card, card->generation) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        local_changes_dialog_state dialog {};
        dialog.card = intent.id;
        dialog.title = card->project.display_name;
        dialog.list_operation_id = operation_id;
        local_changes_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_local_changes(local_changes_event event)
    {
        // 닫힌 dialog나 다른 조회의 늦은 결과는 버린다.
        if (local_changes_dialog_.has_value() == false || event.operation_id != local_changes_dialog_->list_operation_id)
            return;

        local_changes_dialog_->loading = false;
        local_changes_dialog_->entries = std::move(event.result.entries);
        local_changes_dialog_->list_scroll = 0.0f;
        for (const diagnostic& value : event.result.diagnostics)
            if (value.severity != diagnostic_severity::information)
            {
                local_changes_dialog_->message = value.message;
                break;
            }
        if (local_changes_dialog_->entries.empty())
        {
            if (local_changes_dialog_->message.empty())
                local_changes_dialog_->message = u8"표시할 로컬 변경이 없습니다.";
            return;
        }

        // 첫 항목을 바로 선택해 diff까지 이어서 보여 준다.
        local_changes_dialog_->selected = { 0 };
        request_selected_file_diff();
    }

    void logic_controller::handle_select_local_change(const std::size_t index)
    {
        if (local_changes_dialog_.has_value() == false || local_changes_dialog_->loading)
            return;
        if (index >= local_changes_dialog_->entries.size() || local_changes_dialog_->selected == index)
            return;

        local_changes_dialog_->selected = { index };
        request_selected_file_diff();
    }

    void logic_controller::request_selected_file_diff()
    {
        if (shutting_down_ || local_changes_dialog_.has_value() == false || local_changes_dialog_->selected.has_value() == false)
            return;
        card_state* const card { find_card(local_changes_dialog_->card) };
        if (card == nullptr)
            return;

        operation_request request { make_request(operation_kind::query_file_diff, card, card->generation) };
        request.diff_target = { local_changes_dialog_->entries[*local_changes_dialog_->selected] };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        local_changes_dialog_->diff_operation_id = operation_id;
        local_changes_dialog_->diff_loading = true;
        local_changes_dialog_->diff_rows.clear();
        local_changes_dialog_->diff_notice.clear();
        local_changes_dialog_->diff_scroll = 0.0f;
    }

    void logic_controller::handle_file_diff(file_diff_event event)
    {
        if (local_changes_dialog_.has_value() == false || event.operation_id != local_changes_dialog_->diff_operation_id)
            return;

        local_changes_dialog_->diff_loading = false;
        local_changes_dialog_->diff_rows = build_two_way_diff(event.result.lines);
        local_changes_dialog_->diff_scroll = 0.0f;

        // 안내는 하나만 보여 준다. 오류 > 이진/디렉터리 > 생략 > 변화 없음 순서다.
        std::u8string notice {};
        for (const diagnostic& value : event.result.diagnostics)
            if (value.severity != diagnostic_severity::information)
            {
                notice = value.message;
                break;
            }
        if (notice.empty() && event.result.binary)
            notice = u8"이진 파일 - 내용을 표시하지 않습니다.";
        if (notice.empty() && event.result.directory)
            notice = u8"미추적 디렉터리 - 내부 파일은 표시하지 않습니다.";
        if (notice.empty() && event.result.truncated)
            notice = u8"표시 상한(256 KiB)을 넘어 이후 내용을 생략했습니다.";
        if (notice.empty() && local_changes_dialog_->diff_rows.empty())
            notice = u8"표시할 차이가 없습니다.";
        local_changes_dialog_->diff_notice = std::move(notice);
    }

    void logic_controller::handle_local_changes_scroll(const float delta)
    {
        if (local_changes_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(local_changes_dialog_->entries.size()) * layout_local_changes_row_height() };
        float maximum { content - layout_local_changes_list_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { local_changes_dialog_->list_scroll + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        local_changes_dialog_->list_scroll = offset;
    }

    void logic_controller::handle_local_changes_diff_scroll(const float delta)
    {
        if (local_changes_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(local_changes_dialog_->diff_rows.size()) * layout_local_changes_diff_line_height() };
        float maximum { content - layout_local_changes_diff_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { local_changes_dialog_->diff_scroll + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        local_changes_dialog_->diff_scroll = offset;
    }

    void logic_controller::append_lifecycle_log(card_state& card, const diagnostic_severity severity, std::u8string text)
    {
        operation_log_entry entry {};
        entry.kind = log_entry_kind::lifecycle;
        entry.severity = severity;
        entry.text = std::move(text);
        entry.time = std::chrono::system_clock::now();
        append_card_log(card, std::move(entry));
    }

    void logic_controller::append_card_log(card_state& card, operation_log_entry entry)
    {
        if (log_sink_ != nullptr)
            log_sink_->append(card.project.id, entry);
        card.log.append(std::move(entry));
        if (log_sink_ == nullptr)
            return;

        // 파일 로그가 꺼진 사유는 화면 로그에만 남긴다. 파일로 다시 보내면 실패가
        // 반복되므로 buffer에 직접 넣는다.
        std::optional<std::u8string> failure { log_sink_->take_failure() };
        if (failure.has_value() == false)
            return;

        operation_log_entry notice {};
        notice.kind = log_entry_kind::lifecycle;
        notice.severity = diagnostic_severity::warning;
        notice.text = std::move(*failure);
        notice.time = std::chrono::system_clock::now();
        card.log.append(std::move(notice));
    }

    void logic_controller::publish_log_targets()
    {
        if (log_sink_ == nullptr)
            return;

        std::vector<log_file_target> targets {};
        targets.reserve(cards_.size());
        for (const card_state& card : cards_)
        {
            log_file_target target {};
            target.id = card.project.id;
            target.display_name = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
            target.repository_path = card.project.path.normalized.empty() ? card.project.path.original : card.project.path.normalized;
            targets.push_back(std::move(target));
        }
        // 유효 설정이 꺼져 있으면 폴더를 만들지 않도록 빈 문서를 알린다 (A4.5).
        const bool enabled { document_.has_value() && effective_settings().write_log_files };
        log_sink_->set_document(enabled ? document_path_ : std::u8string {}, targets);
    }

    void logic_controller::cancel_running_changes() noexcept
    {
        for (card_state& card : cards_)
            if (card.change_cancellation.has_value())
                card.change_cancellation->request_cancellation();
    }

    void logic_controller::request_refresh(card_state& card)
    {
        if (shutting_down_)
            return;
        if (card.busy)
        {
            card.refresh_queued = true;
            return;
        }

        ++card.generation;
        card.busy = true;
        static_cast<void>(submitter_->submit(make_request(operation_kind::refresh, &card, card.generation)));
    }

    void logic_controller::begin_shutdown()
    {
        if (shutting_down_)
            return;
        shutting_down_ = true;
        cancellation_source_.request_cancellation();
        // 변경 작업은 작업별 token을 쓰므로 전역 취소와 별도로 전파한다. 탐색도
        // 작업별 token이라 함께 취소한다.
        cancel_running_changes();
        if (discovery_dialog_.has_value() && discovery_dialog_->scan_cancellation.has_value())
            discovery_dialog_->scan_cancellation->request_cancellation();

        // 종료 저장은 취소 전파 뒤에도 한 번 나간다. runtime의 종료 순서가 이 요청이
        // worker inbox에 들어간 뒤에 inbox를 닫으므로 join 안에서 끝까지 실행된다.
        if (window_placement_dirty_ && document_.has_value())
        {
            window_placement_dirty_ = false;
            operation_request request { make_request(operation_kind::save_document, nullptr, 0) };
            request.document = document_;
            request.revision = revision_;
            // 저장은 취소 token을 보지 않지만, 종료 저장이 취소 대상이 아니라는
            // 의도를 요청에 남긴다.
            request.token = {};
            pending_save_operation_id_ = request.operation_id;
            static_cast<void>(submitter_->submit(std::move(request)));
        }

        // 앱 설정의 마지막 창 배치도 같은 구간에서 한 번 저장한다 (G1). worker
        // lane이 FIFO라 진행 중이던 일반 저장 뒤에 실행되어 마지막 상태가 남는다.
        // 읽기가 끝나기 전이면 파일의 다른 항목을 지울 수 있어 포기한다 (시작
        // 직후 종료의 드문 경로).
        if (app_settings_window_dirty_ && app_settings_loaded_)
        {
            app_settings_window_dirty_ = false;
            operation_request request { make_request(operation_kind::save_app_settings, nullptr, 0) };
            request.app_settings_payload = app_settings_;
            request.app_settings_shadow = app_settings_shadow_;
            request.token = {};
            pending_app_settings_save_id_ = request.operation_id;
            static_cast<void>(submitter_->submit(std::move(request)));
        }
    }

    bool logic_controller::has_notice() const noexcept
    {
        return notices_.empty() == false || save_notice_.empty() == false;
    }

    float logic_controller::list_viewport_height() const noexcept
    {
        // logic은 논리 픽셀로 계산한다. 창 크기만 물리 픽셀이라 배율로 되돌린다.
        return compute_list_layout(window_height_ / scale_, 1.0f, has_notice(), has_log_pane()).viewport_height;
    }

    void logic_controller::clamp_scroll()
    {
        const float content { card_list_content_height(visible_card_count(), 1.0f) };
        scroll_offset_ = clamp_scroll_offset(scroll_offset_, content, list_viewport_height());
    }

    void logic_controller::scroll_selected_into_view()
    {
        if (selected_.has_value() == false)
        {
            clamp_scroll();
            return;
        }

        const std::vector<card_view_model> ordered { build_ordered_cards() };
        std::size_t index { ordered.size() };
        for (std::size_t position = 0; position < ordered.size(); ++position)
            if (ordered[position].id == *selected_)
                index = position;

        scroll_offset_ = scroll_offset_showing_card(scroll_offset_, index, ordered.size(), list_viewport_height(), 1.0f);
    }

    logic_controller::card_state* logic_controller::find_card(const project_id& id) noexcept
    {
        for (card_state& card : cards_)
            if (card.project.id == id)
                return &card;
        return nullptr;
    }

    const operation_log_buffer* logic_controller::card_log(const project_id& id) const noexcept
    {
        for (const card_state& card : cards_)
            if (card.project.id == id)
                return &card.log;
        return nullptr;
    }

    operation_request logic_controller::make_request(const operation_kind kind, const card_state* const card, const std::uint64_t generation)
    {
        operation_request request {};
        request.operation_id = next_operation_id_++;
        request.generation = generation;
        request.kind = kind;
        request.document_path = document_path_;
        if (card != nullptr)
            request.project = card->project;
        // worker는 유효 설정(전역 + 문서 override)의 사본을 받는다. 층 구분은
        // logic 안에서 끝난다.
        request.settings = effective_settings();
        request.token = cancellation_source_.token();
        return request;
    }

    bool logic_controller::matches_filter(const card_state& card) const noexcept
    {
        return contains_ignoring_ascii_case(card.project.display_name, filter_) || contains_ignoring_ascii_case(card.project.path.original, filter_);
    }

    bool logic_controller::relative_paths() const noexcept
    {
        return document_.has_value() && document_->settings.show_relative_paths.value_or(app_settings_.settings.show_relative_paths);
    }

    workspace_settings logic_controller::effective_settings() const
    {
        if (document_.has_value())
            return apply_overrides(app_settings_.settings, document_->settings);
        return app_settings_.settings;
    }

    appearance_settings logic_controller::effective_appearance() const
    {
        // 외양도 다른 설정과 같은 계층이다 (settings-tabs-and-appearance-scope-
        // design S2.2). 문서가 덮어쓴 항목만 앱 값 위에 얹힌다.
        if (document_.has_value())
            return apply_overrides(app_settings_.appearance, document_->appearance);
        return app_settings_.appearance;
    }

    font_settings logic_controller::effective_fonts() const
    {
        // 글꼴도 테마·키 컬러와 같은 계층이다 (2026-08-23 지시). 문서가 덮어쓴
        // 항목만 앱 값 위에 얹힌다.
        if (document_.has_value())
            return apply_overrides(app_settings_.fonts, document_->fonts);
        return app_settings_.fonts;
    }

    std::u8string logic_controller::display_path(const project_definition& project) const
    {
        // 표시 전용이라 구분자를 `/`로 통일한다 (T2). 원형 경로는 project가 그대로
        // 들고 있어 실행·저장 경로에는 영향이 없다.
        if (relative_paths() == false)
            return to_display_path(project.path.original);
        // 문서가 있는 폴더가 기준이다. 문서 경로를 모르면 전체 경로를 그대로 쓴다.
        const std::u8string_view base { windows_parent_directory(document_path_) };
        if (base.empty())
            return to_display_path(project.path.original);
        return to_display_path(relative_windows_path(project.path.original, base));
    }

    std::size_t logic_controller::visible_card_count() const noexcept
    {
        std::size_t count { 0 };
        for (const card_state& card : cards_)
            if (matches_filter(card))
                ++count;
        return count;
    }

    std::vector<card_view_model> logic_controller::build_ordered_cards() const
    {
        std::vector<card_view_model> ordered {};
        for (const card_state& card : cards_)
        {
            if (matches_filter(card) == false)
                continue;

            card_view_model model {};
            model.id = card.project.id;
            model.display_name = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
            model.path = display_path(card.project);
            model.kind = card.snapshot.kind;
            model.reference = card.snapshot.current_reference;
            model.revision = card.snapshot.local_revision;
            // status 순회가 원격 조회와 병렬로 도는 동안에는 요약 대신 진행 표시를
            // 그린다 (대형 작업 복사본에서 분 단위로 걸린다).
            model.working_tree_text = card.snapshot.working_tree_scan_pending ? std::u8string { u8"로컬 변경 확인 중" } : working_tree_summary_text(card.snapshot.working_tree);
            model.comparison_target = card.snapshot.comparison_target;
            model.local_checked_at = card.snapshot.local_checked_at;
            model.remote_checked_at = card.snapshot.remote_checked_at;
            model.busy = card.busy;
            model.selected = selected_.has_value() && *selected_ == card.project.id;
            model.enabled = card.project.enabled;
            model.can_change = card.project.enabled && card.busy == false && card.has_local_result && card.snapshot.availability == repository_availability::ready;
            model.change_running = card.change_operation_id != 0;

            card_view_inputs inputs {};
            inputs.enabled = card.project.enabled;
            inputs.busy = card.busy;
            inputs.has_local_result = card.has_local_result;
            inputs.availability = card.snapshot.availability;
            inputs.sync_state = card.snapshot.sync_state;
            inputs.tree_state = card.snapshot.working_tree.state;
            model.state = derive_card_state(inputs);

            const bool availability_problem { card.snapshot.availability != repository_availability::ready && card.snapshot.availability != repository_availability::unknown };
            model.status = availability_problem ? availability_glyph(card.snapshot.availability) : sync_state_glyph(card.snapshot.sync_state, card.snapshot.ahead_count, card.snapshot.behind_count);
            ordered.push_back(std::move(model));
        }

        // 카드는 항상 문서의 프로젝트 순서 그대로다. drag & drop이 문서를 바꾼다.
        return ordered;
    }

    std::shared_ptr<const view_snapshot> logic_controller::make_view_snapshot() const
    {
        auto snapshot { std::make_shared<view_snapshot>() };
        snapshot->document_path = document_path_;
        snapshot->document_display_path = to_display_path(document_path_);
        snapshot->selected = selected_;
        snapshot->filter_text = filter_;
        snapshot->notices = notices_;
        // 저장 실패는 문서 진단보다 먼저 보인다. UI는 첫 notice만 표시한다.
        if (save_notice_.empty() == false)
            snapshot->notices.insert(snapshot->notices.begin(), save_notice_);
        snapshot->window_width = window_width_;
        snapshot->window_height = window_height_;
        snapshot->scale = scale_;
        snapshot->scroll_offset = scroll_offset_;
        snapshot->relative_paths = relative_paths();
        snapshot->appearance = effective_appearance();
        // 글꼴은 UI thread가 이 값으로 registry를 갱신한다 (F1). 문서가 덮어쓴 값이
        // 있으면 그것이 유효 값이다 (2026-08-23 지시).
        snapshot->fonts = effective_fonts();
        // 환경설정이 열려 있는 동안에는 초안을 그대로 미리 보여 준다 (E2). `저장`이
        // 초안을 문서·앱 설정에 남기고, `취소`는 초안을 버려 원래 값으로 돌아간다.
        if (settings_dialog_.has_value())
        {
            snapshot->appearance.theme = settings_dialog_->theme;
            if (settings_dialog_->accent_id.empty() == false)
                snapshot->appearance.accent_id = settings_dialog_->accent_id;
            // 글꼴도 같은 규칙이다 (2026-08-23 지시). 고른 글꼴이 곧바로 화면에
            // 보이고 `취소`면 되돌아간다.
            snapshot->fonts.ui_family = settings_dialog_->ui_font_family;
            snapshot->fonts.code_family = settings_dialog_->code_font_family;
        }
        if (generate_dialog_.has_value())
        {
            generate_document_dialog_view dialog {};
            dialog.name = { generate_dialog_->name.text, generate_dialog_->name.caret, generate_dialog_->name.anchor };
            apply_composition(text_input_target::generate_document_name, dialog.name);
            dialog.name_placeholder = u8"확장자 없이 문서 이름";
            dialog.scan_root = generate_dialog_->scan_root;
            dialog.location = generate_dialog_->location;
            dialog.same_folder = generate_dialog_->same_folder;
            dialog.executing = generate_dialog_->executing;
            dialog.document_path = generate_document_path();
            dialog.can_confirm = dialog.document_path.empty() == false && dialog.executing == false;

            // 안내는 실패 사유가 있으면 그것이고, 없으면 아직 덜 채운 것을 알린다.
            dialog.message = generate_dialog_->message;
            if (dialog.message.empty() && dialog.can_confirm == false && dialog.executing == false)
            {
                if (const std::u8string_view error { generate_document_name_error(generate_dialog_->name.text) }; error.empty() == false)
                    dialog.message = error;
                else if (generate_dialog_->name.text.empty())
                    dialog.message = u8"만들 문서의 이름을 입력하세요.";
                else if (generate_dialog_->scan_root.empty())
                    dialog.message = u8"저장소들을 담은 폴더를 지정하세요.";
                else
                    dialog.message = u8"문서를 저장할 폴더를 지정하세요.";
            }
            snapshot->generate_dialog = { std::move(dialog) };
        }

        if (font_dialog_.has_value())
        {
            font_dialog_view dialog {};
            dialog.title = font_dialog_->code ? u8"코드 글꼴 선택" : u8"UI 글꼴 선택";
            dialog.query = { font_dialog_->query.text, font_dialog_->query.caret, font_dialog_->query.anchor };
            apply_composition(text_input_target::font_filter, dialog.query);
            dialog.query_placeholder = u8"글꼴 이름으로 거르기";
            dialog.query_action_enabled = dialog.query.text.empty() == false;
            dialog.preview = std::u8string { font_preview_text };

            const std::vector<std::u8string> families { filtered_font_families() };
            dialog.names.reserve(families.size());
            for (const std::u8string& family : families)
                dialog.names.push_back(family.empty() ? std::u8string { default_font_display_name } : family);
            dialog.families = families;
            if (font_dialog_->selected.has_value() && *font_dialog_->selected < families.size())
            {
                dialog.selected = font_dialog_->selected;
                dialog.preview_family = families[*font_dialog_->selected];
                dialog.can_confirm = true;
            }
            if (families.empty())
                dialog.list_note = font_families_.empty() ? u8"시스템 글꼴 목록을 읽지 못했습니다." : u8"이름이 맞는 글꼴이 없습니다.";

            const float content { static_cast<float>(families.size()) * layout_font_dialog_row_height() };
            float maximum { content - layout_font_dialog_list_height };
            if (maximum < 0.0f)
                maximum = 0.0f;
            dialog.scroll_offset = font_dialog_->scroll_offset > maximum ? maximum : font_dialog_->scroll_offset;
            snapshot->font_dialog = { std::move(dialog) };
        }

        snapshot->window_placement_request = window_placement_;
        snapshot->window_placement_revision = window_placement_revision_;
        snapshot->document_generating = pending_generation_operation_id_ != 0;
        snapshot->shutting_down = shutting_down_;
        snapshot->cards = build_ordered_cards();

        // 선택 카드의 로그 뷰다. 필터를 통과한 record만 담고 스크롤은 이미 고정된
        // 값이라 렌더러는 그대로 그린다 (REQ-008).
        if (has_log_pane())
        {
            // pane은 항상 열려 있다. 선택 카드가 없으면 제목만 있는 빈 모델을 게시하고,
            // 고르라는 안내는 본문이 한 번만 한다 (제목과 본문이 같은 말을 둘로 했다).
            log_view_model log {};
            log.title = u8"로그";
            log.filter = log_filter_;
            log.auto_scroll = log_auto_scroll_;
            if (selected_.has_value())
                for (const card_state& card : cards_)
                {
                    if ((card.project.id == *selected_) == false)
                        continue;

                    log.card = card.project.id;
                    log.title = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
                    for (const operation_log_record& record : card.log.records())
                        if (log_entry_matches_filter(record.entry, log_filter_))
                            log.records.push_back(record);
                    // 렌더링·스크롤은 접힌 표시 목록을, 복사는 records를 쓴다.
                    log.lines = build_log_display_lines(card.log.records(), log_filter_);
                    log.truncated = card.log.dropped_count() > 0;
                    // 실행 중이면 헤더의 경과 시간이 흐르고, 끝난 뒤에는 마지막
                    // 명령이 걸린 시간을 그대로 세워 둔다. 둘 다 없으면 표시하지
                    // 않는다 (아직 아무 명령도 실행하지 않았다).
                    if (card.change_operation_id != 0)
                        log.change_started_at = { card.change_started_at };
                    else
                        log.change_elapsed = card.change_elapsed;

                    float maximum { log_content_height() - log_viewport_height() };
                    if (maximum < 0.0f)
                        maximum = 0.0f;
                    log.scroll_offset = log_auto_scroll_ ? maximum : (log_scroll_offset_ > maximum ? maximum : log_scroll_offset_);
                    break;
                }
            snapshot->log = { std::move(log) };
        }

        if (switch_dialog_.has_value())
        {
            for (const card_state& card : cards_)
            {
                if ((card.project.id == switch_dialog_->card) == false)
                    continue;

                switch_dialog_view dialog {};
                dialog.card = card.project.id;
                dialog.title = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
                dialog.loading = switch_dialog_->loading;
                dialog.stale = switch_dialog_->candidates.stale;
                dialog.svn_browser = switch_dialog_->subversion;
                const std::vector<std::size_t> visible { filtered_switch_candidates() };
                dialog.candidates.reserve(visible.size());
                for (const std::size_t index : visible)
                {
                    const switch_candidate& candidate { switch_dialog_->candidates.candidates[index] };
                    if (candidate_is_current(card.snapshot, candidate, switch_dialog_->candidates.candidates))
                        dialog.current = dialog.candidates.size();
                    dialog.candidates.push_back(candidate);
                }
                if (switch_dialog_->svn_browser.has_value())
                    dialog.svn_rows = build_svn_repository_browser_rows(*switch_dialog_->svn_browser);
                dialog.selected = switch_dialog_->selected;
                dialog.executing = switch_dialog_->executing;
                dialog.message = switch_dialog_->message;

                // 질의 막대다 (switch-dialog-query-bar-design Q1). Git은 필터,
                // SVN은 실제 경로 주소 막대라 문구와 우측 버튼의 몫이 다르다.
                dialog.query = { switch_dialog_->query.text, switch_dialog_->query.caret, switch_dialog_->query.anchor };
                apply_composition(text_input_target::switch_query, dialog.query);
                dialog.query_enabled = switch_dialog_->loading == false && switch_dialog_->executing == false;
                if (dialog.svn_browser)
                {
                    dialog.query_placeholder = u8"이동할 경로 (예: /branches/2026-08)";
                    dialog.query_action = switch_query_action::navigate;
                    dialog.query_action_enabled = dialog.query.text.empty() == false && switch_dialog_->svn_browser.has_value();
                }
                else
                {
                    dialog.query_placeholder = u8"브랜치 이름으로 필터링";
                    dialog.query_action = dialog.query.text.empty() ? switch_query_action::search : switch_query_action::clear;
                    dialog.query_action_enabled = dialog.query.text.empty() == false;
                }

                // 목록이 비었을 때의 안내다. 필터가 비운 것과 후보가 없는 것은 다른
                // 말이라 logic이 구분해 정한다.
                const bool list_empty { dialog.svn_browser ? dialog.svn_rows.empty() : dialog.candidates.empty() };
                if (switch_dialog_->loading)
                    dialog.list_note = dialog.svn_browser ? u8"SVN 저장소 정보를 조회하는 중입니다..." : u8"전환 후보를 조회하는 중입니다...";
                else if (list_empty && dialog.svn_browser)
                    dialog.list_note = u8"SVN 저장소를 탐색할 수 없습니다.";
                else if (list_empty && dialog.query.text.empty() == false)
                    dialog.list_note = u8"필터에 맞는 후보가 없습니다.";
                else if (list_empty)
                    dialog.list_note = u8"전환할 수 있는 후보가 없습니다.";

                // 확인 버튼 상태와 label은 logic이 한곳에서 정한다 (plan 5.3의 4~5).
                if (switch_dialog_->svn_browser.has_value())
                {
                    const svn_repository_browser_state& browser { *switch_dialog_->svn_browser };
                    dialog.confirm_label = u8"전환";
                    // 이동이 걸어 내려가는 동안의 선택은 지나가는 자리다 (2026-08-23
                    // 지시). 그 사이 `전환`을 받으면 목표가 아니라 중간 폴더로 간다.
                    if (switch_dialog_->loading == false && switch_dialog_->executing == false && browser.reveal_url.empty() && browser.selected_url.empty() == false
                        && browser.selected_url != browser.current_url)
                        dialog.can_confirm = true;
                    else if (browser.selected_url == browser.current_url && dialog.message.empty())
                        dialog.message = u8"현재 위치입니다.";
                }
                else if (switch_dialog_->loading == false && switch_dialog_->selected.has_value() && *switch_dialog_->selected < dialog.candidates.size() && switch_dialog_->executing == false)
                {
                    const switch_candidate& candidate { dialog.candidates[*switch_dialog_->selected] };
                    if (candidate_is_current(card.snapshot, candidate, switch_dialog_->candidates.candidates))
                    {
                        dialog.can_confirm = false;
                        if (dialog.message.empty())
                            dialog.message = u8"이미 현재 참조입니다.";
                    }
                    else
                    {
                        dialog.can_confirm = true;
                        if (switch_dialog_->tracking_confirm_pending)
                            dialog.confirm_label = u8"생성 확인";
                        else if (candidate.requires_tracking_branch)
                            dialog.confirm_label = u8"브랜치 만들고 전환";
                        else
                            dialog.confirm_label = u8"전환 실행";
                    }
                }
                if (dialog.confirm_label.empty())
                    dialog.confirm_label = u8"전환 실행";

                const std::size_t row_count { dialog.svn_browser ? dialog.svn_rows.size() : dialog.candidates.size() };
                const float content { static_cast<float>(row_count) * layout_switch_dialog_row_height() };
                const float viewport { switch_dialog_list_height(dialog.svn_browser, window_height_ / scale_) };
                float maximum { content - viewport };
                if (maximum < 0.0f)
                    maximum = 0.0f;
                dialog.scroll_offset = switch_dialog_->scroll_offset > maximum ? maximum : switch_dialog_->scroll_offset;
                snapshot->switch_dialog = { std::move(dialog) };
                break;
            }
        }

        if (discovery_dialog_.has_value())
        {
            discovery_dialog_view dialog {};
            dialog.scan_root = discovery_dialog_->scan_root;
            dialog.loading = discovery_dialog_->loading;
            dialog.executing = discovery_dialog_->register_operation_id != 0;
            dialog.message = discovery_dialog_->message;
            dialog.rows.reserve(discovery_dialog_->result.candidates.size());
            bool any_checked { false };
            for (std::size_t index = 0; index < discovery_dialog_->result.candidates.size(); ++index)
            {
                discovery_row_view row {};
                row.candidate = discovery_dialog_->result.candidates[index];
                row.checked = discovery_dialog_->checked[index];
                any_checked = any_checked || row.checked;
                dialog.rows.push_back(std::move(row));
            }
            dialog.can_confirm = dialog.loading == false && dialog.executing == false && any_checked;

            const float content { static_cast<float>(dialog.rows.size()) * layout_discovery_dialog_row_height() };
            float maximum { content - layout_discovery_dialog_list_height };
            if (maximum < 0.0f)
                maximum = 0.0f;
            dialog.scroll_offset = discovery_dialog_->scroll_offset > maximum ? maximum : discovery_dialog_->scroll_offset;
            snapshot->discovery_dialog = { std::move(dialog) };
        }

        if (settings_dialog_.has_value())
        {
            settings_dialog_view dialog { build_settings_dialog_view() };
            const float maximum { ui::settings_dialog_metrics(dialog, window_height_ / scale_).maximum_scroll };
            dialog.scroll_offset = settings_dialog_->scroll_offset > maximum ? maximum : settings_dialog_->scroll_offset;
            snapshot->settings_dialog = { std::move(dialog) };
        }

        if (local_changes_dialog_.has_value())
        {
            local_changes_dialog_view dialog {};
            dialog.card = local_changes_dialog_->card;
            dialog.title = local_changes_dialog_->title;
            dialog.loading = local_changes_dialog_->loading;
            dialog.message = local_changes_dialog_->message;
            dialog.list_scroll = local_changes_dialog_->list_scroll;
            dialog.diff_loading = local_changes_dialog_->diff_loading;
            dialog.diff_rows = local_changes_dialog_->diff_rows;
            dialog.diff_notice = local_changes_dialog_->diff_notice;
            dialog.diff_scroll = local_changes_dialog_->diff_scroll;
            std::u8string_view card_root {};
            for (const card_state& card : cards_)
                if (card.project.id == local_changes_dialog_->card)
                {
                    card_root = card.project.path.normalized.empty() ? std::u8string_view { card.project.path.original } : std::u8string_view { card.project.path.normalized };
                    break;
                }
            dialog.rows.reserve(local_changes_dialog_->entries.size());
            for (std::size_t index = 0; index < local_changes_dialog_->entries.size(); ++index)
            {
                const local_change_entry& entry { local_changes_dialog_->entries[index] };
                local_change_row_view row {};
                row.badge = std::u8string { local_change_kind_badge(entry.kind) };
                row.untracked = entry.kind == local_change_kind::untracked;
                // Git status는 미추적 디렉터리를 `/`로 끝나는 항목 하나로 접는다.
                row.directory = entry.path.empty() == false && entry.path.back() == u8'/';
                row.path = entry.path;
                row.absolute_path = join_local_change_path(card_root, entry.path);
                row.selected = local_changes_dialog_->selected == index;
                dialog.rows.push_back(std::move(row));
            }
            snapshot->local_changes_dialog = { std::move(dialog) };
        }

        if (notice_dialog_.has_value())
            snapshot->notice_dialog = notice_dialog_;

        if (context_menu_.has_value() && context_menu_->kind == context_menu_kind::document)
        {
            // 배너 우클릭 메뉴다 (T1). 탐색기는 문서 파일을 선택 상태로 열고,
            // VSCode는 문서가 있는 폴더를 workspace로 연다. 두 경로 모두 Windows
            // 원형이라 셸이 그대로 받는다.
            const std::u8string folder { windows_parent_directory(document_path_) };
            if (document_path_.empty() == false && folder.empty() == false)
            {
                context_menu_view menu {};
                menu.anchor_x = context_menu_->anchor_x;
                menu.anchor_y = context_menu_->anchor_y;
                menu.items.push_back({ context_menu_entry::open_document_folder, u8"경로를 탐색기로 열기", true, document_path_ });
                menu.items.push_back({ context_menu_entry::open_document_in_vscode, u8"VSCode로 열기", true, folder });
                snapshot->context_menu = { std::move(menu) };
            }
        }
        else if (context_menu_.has_value())
        {
            for (const card_state& card : cards_)
            {
                if ((card.project.id == context_menu_->card) == false)
                    continue;

                context_menu_view menu {};
                menu.owner = card.project.id;
                menu.anchor_x = context_menu_->anchor_x;
                menu.anchor_y = context_menu_->anchor_y;
                const std::u8string repository_path { card.project.path.normalized.empty() ? card.project.path.original : card.project.path.normalized };

                // 항목 활성 여부는 카드 버튼과 같은 판정이다 (3장: 버튼이 비활성이면
                // 메뉴 항목도 비활성). 실행 중에는 update 버튼이 취소 버튼으로
                // 바뀌므로 메뉴의 업데이트는 비활성이다.
                const bool can_change { card.project.enabled && card.busy == false && card.has_local_result && card.snapshot.availability == repository_availability::ready };
                const bool change_running { card.change_operation_id != 0 };
                menu.items.push_back({ context_menu_entry::open_repository, u8"저장소 열기", true, repository_path });
                // 미추적·변경 정리는 편집기에서 하는 편이 낫다 (2026-08-22 지시).
                menu.items.push_back({ context_menu_entry::open_in_vscode, u8"VSCode로 열기", true, repository_path });
                menu.items.push_back({ context_menu_entry::show_local_changes, u8"로컬 변경 확인", true });
                menu.items.push_back({ context_menu_entry::refresh, u8"상태 갱신", true });
                menu.items.push_back({ context_menu_entry::update, u8"업데이트", can_change && change_running == false });
                menu.items.push_back({ context_menu_entry::switch_to, u8"전환…", can_change });
                snapshot->context_menu = { std::move(menu) };
                break;
            }
        }

        if (document_loading_)
            snapshot->empty_state = view_empty_state::document_loading;
        else if (document_.has_value() == false)
        {
            snapshot->empty_state = view_empty_state::no_document;
            // 열린 문서가 없으면 빈 문구 대신 시작 페이지를 그린다 (A1.3).
            start_page_view page {};
            page.loading = app_settings_loaded_ == false;
            page.recents.reserve(app_settings_.recent_documents.size());
            for (const recent_document& value : app_settings_.recent_documents)
            {
                recent_document_view row {};
                row.display_name = value.display_name.empty() ? recent_document_display_name(value.path) : value.display_name;
                row.folder = to_display_path(windows_parent_directory(value.path));
                row.path = value.path;
                row.pinned = value.pinned;
                page.recents.push_back(std::move(row));
            }
            snapshot->start_page = { std::move(page) };
        }
        else if (cards_.empty())
            snapshot->empty_state = view_empty_state::no_projects;
        else if (snapshot->cards.empty())
            snapshot->empty_state = view_empty_state::no_filter_match;
        else
            snapshot->empty_state = view_empty_state::none;
        return snapshot;
    }
} // namespace gitman
