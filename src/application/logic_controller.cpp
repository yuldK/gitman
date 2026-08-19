#include "application/logic_controller.h"

#include "domain/path_syntax.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace gitman {
    namespace {
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

        bool name_before(const card_view_model& left, const card_view_model& right) noexcept
        {
            const std::size_t common { left.display_name.size() < right.display_name.size() ? left.display_name.size() : right.display_name.size() };
            for (std::size_t index = 0; index < common; ++index)
            {
                const char8_t left_value { ascii_lowercase(left.display_name[index]) };
                const char8_t right_value { ascii_lowercase(right.display_name[index]) };
                if (left_value != right_value)
                    return left_value < right_value;
            }
            if (left.display_name.size() != right.display_name.size())
                return left.display_name.size() < right.display_name.size();
            return left.id.value < right.id.value;
        }

        bool status_before(const card_view_model& left, const card_view_model& right) noexcept;

        // 상태 정렬은 손볼 일이 많은 카드를 위로 올린다.
        int state_rank(const card_view_state state) noexcept
        {
            switch (state)
            {
            case card_view_state::failed:
                return 0;
            case card_view_state::warning:
                return 1;
            case card_view_state::loading:
                return 2;
            case card_view_state::running:
                return 3;
            case card_view_state::ready:
                return 4;
            case card_view_state::disabled:
                return 5;
            }
            return 6;
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

        bool status_before(const card_view_model& left, const card_view_model& right) noexcept
        {
            const int left_rank { state_rank(left.state) };
            const int right_rank { state_rank(right.state) };
            if (left_rank != right_rank)
                return left_rank < right_rank;
            return name_before(left, right);
        }

        // dialog 수준에서 곧바로 판정할 수 있는 검증이다. 존재·저장소 일치 같은
        // 나머지 검증은 provider가 실행 직전에 다시 수행한다 (REQ-007).
        bool candidate_is_current(const repository_snapshot& snapshot, const switch_candidate& candidate) noexcept
        {
            if (candidate.kind == switch_candidate_kind::git_local_branch)
                return candidate.display_name == snapshot.current_reference;
            if (candidate.kind == switch_candidate_kind::subversion_url)
                return candidate.target == snapshot.current_reference;
            return false;
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

    logic_controller::logic_controller(operation_submitter& submitter) noexcept
        : submitter_ { &submitter }
    {}

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
                else if constexpr (std::is_same_v<value_type, set_sort_intent>)
                {
                    sort_ = value.key;
                    scroll_selected_into_view();
                }
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
                else if constexpr (std::is_same_v<value_type, show_update_options_intent>)
                {
                    // busy 카드는 버튼이 이미 비활성이지만 늦게 도착한 클릭도 막는다.
                    const card_state* const card { find_card(value.id) };
                    if (shutting_down_ == false && card != nullptr && card->project.enabled && card->busy == false)
                    {
                        update_overlay_card_ = { value.id };
                        // ADR-003: submodule 갱신은 매번 명시적으로 켜는 기본 off다.
                        update_overlay_submodules_ = false;
                    }
                }
                else if constexpr (std::is_same_v<value_type, set_update_submodules_intent>)
                {
                    if (update_overlay_card_.has_value())
                        update_overlay_submodules_ = value.enabled;
                }
                else if constexpr (std::is_same_v<value_type, confirm_update_intent>)
                {
                    if (update_overlay_card_.has_value())
                    {
                        card_state* const card { find_card(*update_overlay_card_) };
                        update_overlay_card_.reset();
                        if (card != nullptr && shutting_down_ == false)
                        {
                            update_options options {};
                            options.update_submodules = update_overlay_submodules_;
                            begin_change(*card, operation_kind::update, options, nullptr);
                        }
                    }
                }
                else if constexpr (std::is_same_v<value_type, cancel_update_options_intent>)
                    update_overlay_card_.reset();
                else if constexpr (std::is_same_v<value_type, begin_switch_intent>)
                    handle_begin_switch(value);
                else if constexpr (std::is_same_v<value_type, select_switch_candidate_intent>)
                    handle_select_switch_candidate(value.index);
                else if constexpr (std::is_same_v<value_type, confirm_switch_intent>)
                    handle_confirm_switch();
                else if constexpr (std::is_same_v<value_type, cancel_switch_dialog_intent>)
                    switch_dialog_.reset();
                else if constexpr (std::is_same_v<value_type, switch_dialog_scroll_intent>)
                    handle_switch_dialog_scroll(value.delta);
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
                else if constexpr (std::is_same_v<value_type, open_settings_intent>)
                    handle_open_settings();
                else if constexpr (std::is_same_v<value_type, set_settings_executable_intent>)
                    handle_set_settings_executable(std::move(value));
                else if constexpr (std::is_same_v<value_type, clear_settings_executable_intent>)
                    handle_clear_settings_executable(value);
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
                else if constexpr (std::is_same_v<value_type, discovery_completed_event>)
                    handle_discovery_completed(std::move(value));
                else if constexpr (std::is_same_v<value_type, projects_registered_event>)
                    handle_projects_registered(std::move(value));
            },
            std::move(message));
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
            // 실패는 현재 문서를 유지한 채 진단만 알린다.
            notices_.clear();
            for (const diagnostic& value : event.diagnostics)
                if (value.severity != diagnostic_severity::information)
                    notices_.push_back(value.message);
            return;
        }

        // 생성된 문서를 곧바로 연다. 이전 문서의 진행 중 결과가 새 문서에 적용되지
        // 않도록 열기와 같은 규칙으로 대기 상태를 정리한다.
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
                notices_.push_back(value.message);

        save_notice_.clear();
        selected_.reset();
        scroll_offset_ = 0.0f;
        document_ = std::move(document);
        revision_ = std::move(revision);
        // 이전 문서의 카드를 가리키던 overlay와 dialog는 의미가 없다. 환경설정
        // 초안도 이전 문서의 settings 기준이라 함께 버린다. 탐색 dialog는 진행 중
        // 탐색을 취소하고 닫는다.
        update_overlay_card_.reset();
        switch_dialog_.reset();
        settings_dialog_.reset();
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
        if (from == to)
        {
            // 위치가 그대로여도 사용자가 순서를 확정한 것이므로 문서 순서 보기로
            // 전환한다.
            sort_ = card_sort_key::custom;
            return;
        }

        card_state moved { std::move(cards_[from]) };
        cards_.erase(cards_.begin() + static_cast<std::ptrdiff_t>(from));
        cards_.insert(cards_.begin() + static_cast<std::ptrdiff_t>(to), std::move(moved));

        // 문서의 프로젝트 순서가 진실이다. 카드 순서에서 다시 만들어 저장한다.
        document_->projects.clear();
        document_->projects.reserve(cards_.size());
        for (const card_state& card : cards_)
            document_->projects.push_back(card.project);

        sort_ = card_sort_key::custom;
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
        if (selected_.has_value() == false)
            return false;
        for (const card_state& card : cards_)
            if (card.project.id == *selected_)
                return true;
        return false;
    }

    float logic_controller::log_content_height() const noexcept
    {
        if (selected_.has_value() == false)
            return 0.0f;
        const operation_log_buffer* const buffer { card_log(*selected_) };
        if (buffer == nullptr)
            return 0.0f;

        std::size_t count { 0 };
        for (const operation_log_record& record : buffer->records())
            if (log_entry_matches_filter(record.entry, log_filter_))
                ++count;
        return static_cast<float>(count) * layout_log_line_height + layout_log_text_inset * 2.0f;
    }

    float logic_controller::log_viewport_height() const noexcept
    {
        // pane 높이는 창이 작으면 줄어든다. layout과 같은 함수로 계산해야 스크롤
        // 한계가 그리기와 일치한다.
        const list_layout layout { compute_list_layout(window_height_ / scale_, 1.0f, has_notice(), has_log_pane()) };
        const float body { layout.log_height - layout_log_header_height };
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
        document_->settings.show_relative_paths = document_->settings.show_relative_paths == false;
        request_save();
    }

    void logic_controller::handle_window_placement(const window_placement_intent& intent)
    {
        if (intent.placement.valid() == false || document_.has_value() == false)
            return;
        if (document_->window.has_value() && *document_->window == intent.placement)
            return;

        document_->window = intent.placement;
        window_placement_ = intent.placement;
        window_placement_dirty_ = true;
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

        begin_change(*card, operation_kind::update, intent.options, nullptr);
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
        card.change_cancellation = std::move(cancellation);
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
            card->log.append(std::move(entry));
    }

    void logic_controller::handle_change_completed(change_completed_event event)
    {
        card_state* const card { find_card(event.id) };
        if (card == nullptr || event.operation_id != card->change_operation_id)
            return;

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

        const float content { static_cast<float>(discovery_dialog_->result.candidates.size()) * layout_discovery_dialog_row_height };
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

    void logic_controller::handle_open_settings()
    {
        // 환경설정은 문서 수준 값이라 열린 문서가 있어야 편집할 수 있다 (REQ-017).
        if (shutting_down_ || document_.has_value() == false)
            return;

        settings_dialog_state dialog {};
        dialog.git_path = document_->settings.git_executable;
        dialog.svn_path = document_->settings.svn_executable;
        settings_dialog_ = { std::move(dialog) };
    }

    void logic_controller::handle_set_settings_executable(set_settings_executable_intent intent)
    {
        // 닫힌 뒤 도착한 파일 선택 결과는 버린다.
        if (settings_dialog_.has_value() == false)
            return;

        if (intent.tool == repository_kind::git)
            settings_dialog_->git_path = std::move(intent.path);
        else if (intent.tool == repository_kind::subversion)
            settings_dialog_->svn_path = std::move(intent.path);
    }

    void logic_controller::handle_clear_settings_executable(const clear_settings_executable_intent& intent)
    {
        if (settings_dialog_.has_value() == false)
            return;

        if (intent.tool == repository_kind::git)
            settings_dialog_->git_path.clear();
        else if (intent.tool == repository_kind::subversion)
            settings_dialog_->svn_path.clear();
    }

    void logic_controller::handle_confirm_settings()
    {
        if (settings_dialog_.has_value() == false || shutting_down_ || document_.has_value() == false)
            return;
        // 버튼 비활성과 별개로 늦게 도착한 확인도 막는다 (view의 can_confirm과 같은
        // 판정이다).
        if (settings_executable_error(settings_dialog_->git_path).empty() == false || settings_executable_error(settings_dialog_->svn_path).empty() == false)
            return;

        const bool changed { document_->settings.git_executable != settings_dialog_->git_path || document_->settings.svn_executable != settings_dialog_->svn_path };
        if (changed)
        {
            document_->settings.git_executable = settings_dialog_->git_path;
            document_->settings.svn_executable = settings_dialog_->svn_path;
            request_save();
            // 도구 경로가 바뀌었으니 모든 활성 카드를 새 settings로 재조회한다.
            // 요청마다 settings 사본이 실리므로 저장 완료를 기다릴 필요가 없다.
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

        // dialog를 열면서 곧바로 remote-first 후보 조회를 제출한다 (plan 5.3의 2).
        // 조회는 카드를 busy로 만들지 않아 dialog가 열린 동안에도 UI가 멈추지 않는다.
        operation_request request { make_request(operation_kind::query_switch_candidates, card, card->generation) };
        const std::uint64_t operation_id { request.operation_id };
        if (submitter_->submit(std::move(request)) == false)
            return;

        switch_dialog_state dialog {};
        dialog.card = intent.id;
        dialog.candidates_operation_id = operation_id;
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
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->scroll_offset = 0.0f;
    }

    void logic_controller::handle_select_switch_candidate(const std::size_t index)
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->executing || switch_dialog_->loading)
            return;
        if (index >= switch_dialog_->candidates.candidates.size())
            return;

        switch_dialog_->selected = { index };
        // 다른 후보를 고르면 이전 검증·거부 메시지와 확인 단계는 의미가 없다.
        switch_dialog_->tracking_confirm_pending = false;
        switch_dialog_->message.clear();
    }

    void logic_controller::handle_confirm_switch()
    {
        if (switch_dialog_.has_value() == false || switch_dialog_->loading || switch_dialog_->executing || switch_dialog_->selected.has_value() == false)
            return;

        card_state* const card { find_card(switch_dialog_->card) };
        if (card == nullptr)
        {
            switch_dialog_.reset();
            return;
        }

        switch_candidate target { switch_dialog_->candidates.candidates[*switch_dialog_->selected] };
        if (candidate_is_current(card->snapshot, target))
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

    void logic_controller::handle_switch_dialog_scroll(const float delta)
    {
        if (switch_dialog_.has_value() == false)
            return;

        const float content { static_cast<float>(switch_dialog_->candidates.candidates.size()) * layout_switch_dialog_row_height };
        float maximum { content - layout_switch_dialog_list_height };
        if (maximum < 0.0f)
            maximum = 0.0f;
        float offset { switch_dialog_->scroll_offset + delta };
        if (offset < 0.0f)
            offset = 0.0f;
        if (offset > maximum)
            offset = maximum;
        switch_dialog_->scroll_offset = offset;
    }

    void logic_controller::append_lifecycle_log(card_state& card, const diagnostic_severity severity, std::u8string text)
    {
        operation_log_entry entry {};
        entry.kind = log_entry_kind::lifecycle;
        entry.severity = severity;
        entry.text = std::move(text);
        entry.time = std::chrono::system_clock::now();
        card.log.append(std::move(entry));
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
        if (window_placement_dirty_ == false || document_.has_value() == false)
            return;
        window_placement_dirty_ = false;

        operation_request request { make_request(operation_kind::save_document, nullptr, 0) };
        request.document = document_;
        request.revision = revision_;
        // 저장은 취소 token을 보지 않지만, 종료 저장이 취소 대상이 아니라는 의도를
        // 요청에 남긴다.
        request.token = {};
        pending_save_operation_id_ = request.operation_id;
        static_cast<void>(submitter_->submit(std::move(request)));
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
        if (document_.has_value())
            request.settings = document_->settings;
        request.token = cancellation_source_.token();
        return request;
    }

    bool logic_controller::matches_filter(const card_state& card) const noexcept
    {
        return contains_ignoring_ascii_case(card.project.display_name, filter_) || contains_ignoring_ascii_case(card.project.path.original, filter_);
    }

    bool logic_controller::relative_paths() const noexcept
    {
        return document_.has_value() && document_->settings.show_relative_paths;
    }

    std::u8string logic_controller::display_path(const project_definition& project) const
    {
        if (relative_paths() == false)
            return project.path.original;
        // 문서가 있는 폴더가 기준이다. 문서 경로를 모르면 전체 경로를 그대로 쓴다.
        const std::u8string_view base { windows_parent_directory(document_path_) };
        if (base.empty())
            return project.path.original;
        return relative_windows_path(project.path.original, base);
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
            model.working_tree_text = working_tree_summary_text(card.snapshot.working_tree);
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

        // custom은 문서(카드) 순서를 그대로 둔다.
        if (sort_ == card_sort_key::name)
            std::sort(ordered.begin(), ordered.end(), name_before);
        else if (sort_ == card_sort_key::status)
            std::sort(ordered.begin(), ordered.end(), status_before);
        return ordered;
    }

    std::shared_ptr<const view_snapshot> logic_controller::make_view_snapshot() const
    {
        auto snapshot { std::make_shared<view_snapshot>() };
        snapshot->document_path = document_path_;
        snapshot->selected = selected_;
        snapshot->filter_text = filter_;
        snapshot->sort = sort_;
        snapshot->notices = notices_;
        // 저장 실패는 문서 진단보다 먼저 보인다. UI는 첫 notice만 표시한다.
        if (save_notice_.empty() == false)
            snapshot->notices.insert(snapshot->notices.begin(), save_notice_);
        snapshot->window_width = window_width_;
        snapshot->window_height = window_height_;
        snapshot->scale = scale_;
        snapshot->scroll_offset = scroll_offset_;
        snapshot->relative_paths = relative_paths();
        snapshot->window_placement_request = window_placement_;
        snapshot->window_placement_revision = window_placement_revision_;
        snapshot->document_generating = pending_generation_operation_id_ != 0;
        snapshot->shutting_down = shutting_down_;
        snapshot->cards = build_ordered_cards();

        // 선택 카드의 로그 뷰다. 필터를 통과한 record만 담고 스크롤은 이미 고정된
        // 값이라 렌더러는 그대로 그린다 (REQ-008).
        if (selected_.has_value())
        {
            for (const card_state& card : cards_)
            {
                if ((card.project.id == *selected_) == false)
                    continue;

                log_view_model log {};
                log.card = card.project.id;
                log.title = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
                for (const operation_log_record& record : card.log.records())
                    if (log_entry_matches_filter(record.entry, log_filter_))
                        log.records.push_back(record);
                log.filter = log_filter_;
                log.auto_scroll = log_auto_scroll_;
                log.truncated = card.log.dropped_count() > 0;

                float maximum { log_content_height() - log_viewport_height() };
                if (maximum < 0.0f)
                    maximum = 0.0f;
                log.scroll_offset = log_auto_scroll_ ? maximum : (log_scroll_offset_ > maximum ? maximum : log_scroll_offset_);
                snapshot->log = { std::move(log) };
                break;
            }
        }

        if (update_overlay_card_.has_value())
        {
            for (const card_state& card : cards_)
            {
                if ((card.project.id == *update_overlay_card_) == false)
                    continue;

                update_overlay_view overlay {};
                overlay.card = card.project.id;
                overlay.title = card.project.display_name.empty() ? card.project.id.value : card.project.display_name;
                overlay.update_submodules = update_overlay_submodules_;
                snapshot->update_overlay = { std::move(overlay) };
                break;
            }
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
                dialog.candidates = switch_dialog_->candidates.candidates;
                dialog.selected = switch_dialog_->selected;
                dialog.executing = switch_dialog_->executing;
                dialog.message = switch_dialog_->message;

                // 확인 버튼 상태와 label은 logic이 한곳에서 정한다 (plan 5.3의 4~5).
                if (switch_dialog_->loading == false && switch_dialog_->selected.has_value() && switch_dialog_->executing == false)
                {
                    const switch_candidate& candidate { switch_dialog_->candidates.candidates[*switch_dialog_->selected] };
                    if (candidate_is_current(card.snapshot, candidate))
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

                const float content { static_cast<float>(dialog.candidates.size()) * layout_switch_dialog_row_height };
                float maximum { content - layout_switch_dialog_list_height };
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

            const float content { static_cast<float>(dialog.rows.size()) * layout_discovery_dialog_row_height };
            float maximum { content - layout_discovery_dialog_list_height };
            if (maximum < 0.0f)
                maximum = 0.0f;
            dialog.scroll_offset = discovery_dialog_->scroll_offset > maximum ? maximum : discovery_dialog_->scroll_offset;
            snapshot->discovery_dialog = { std::move(dialog) };
        }

        if (settings_dialog_.has_value())
        {
            settings_dialog_view dialog {};
            dialog.git_path = settings_dialog_->git_path;
            dialog.svn_path = settings_dialog_->svn_path;
            // 검증 메시지와 확인 가능 여부는 logic이 한곳에서 정한다. 첫 오류만
            // 표시해도 확인이 막혀 있어 사용자는 고칠 것을 하나씩 안내받는다.
            const std::u8string_view git_error { settings_executable_error(settings_dialog_->git_path) };
            const std::u8string_view svn_error { settings_executable_error(settings_dialog_->svn_path) };
            if (git_error.empty() == false)
                dialog.message = std::u8string { u8"Git: " } + std::u8string { git_error };
            else if (svn_error.empty() == false)
                dialog.message = std::u8string { u8"SVN: " } + std::u8string { svn_error };
            dialog.can_confirm = git_error.empty() && svn_error.empty();
            snapshot->settings_dialog = { std::move(dialog) };
        }

        if (document_loading_)
            snapshot->empty_state = view_empty_state::document_loading;
        else if (document_.has_value() == false)
            snapshot->empty_state = view_empty_state::no_document;
        else if (cards_.empty())
            snapshot->empty_state = view_empty_state::no_projects;
        else if (snapshot->cards.empty())
            snapshot->empty_state = view_empty_state::no_filter_match;
        else
            snapshot->empty_state = view_empty_state::none;
        return snapshot;
    }
} // namespace gitman
