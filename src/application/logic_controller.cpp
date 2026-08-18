#include "application/logic_controller.h"

#include "presentation/list_metrics.h"

#include <algorithm>
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
                else if constexpr (std::is_same_v<value_type, set_sort_intent>)
                {
                    sort_ = value.key;
                    scroll_selected_into_view();
                }
                else if constexpr (std::is_same_v<value_type, reorder_card_intent>)
                    handle_reorder_card(value);
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
        notices_.clear();
        for (const diagnostic& value : diagnostics)
            if (value.severity != diagnostic_severity::information)
                notices_.push_back(value.message);

        save_notice_.clear();
        selected_.reset();
        scroll_offset_ = 0.0f;
        document_ = std::move(document);
        revision_ = std::move(revision);

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
        if (intent.id.has_value() && find_card(*intent.id) == nullptr)
            selected_.reset();
        else
            selected_ = intent.id;

        // 키보드 순회는 화면 밖 카드도 고를 수 있다. 선택이 보이도록 따라간다.
        scroll_selected_into_view();
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
        return compute_list_layout(window_height_ / scale_, 1.0f, has_notice()).viewport_height;
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
            model.path = card.project.path.original;
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
        snapshot->window_placement_request = window_placement_;
        snapshot->window_placement_revision = window_placement_revision_;
        snapshot->document_generating = pending_generation_operation_id_ != 0;
        snapshot->shutting_down = shutting_down_;
        snapshot->cards = build_ordered_cards();

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
