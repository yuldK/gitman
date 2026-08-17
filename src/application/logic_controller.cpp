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
                {
                    if (value.id.has_value() && find_card(*value.id) == nullptr)
                        selected_.reset();
                    else
                        selected_ = value.id;
                }
                else if constexpr (std::is_same_v<value_type, set_filter_intent>)
                    filter_ = std::move(value.text);
                else if constexpr (std::is_same_v<value_type, set_sort_intent>)
                    sort_ = value.key;
                else if constexpr (std::is_same_v<value_type, window_metrics_intent>)
                {
                    window_width_ = value.width;
                    window_height_ = value.height;
                    scale_ = value.scale > 0.0f ? value.scale : 1.0f;
                }
                else if constexpr (std::is_same_v<value_type, scroll_intent>)
                {
                    const float viewport { window_height_ / scale_ - layout_caption_height - layout_toolbar_height };
                    const float content { card_list_content_height(cards_.size(), 1.0f) };
                    scroll_offset_ = clamp_scroll_offset(scroll_offset_ + value.delta, content, viewport);
                }
                else if constexpr (std::is_same_v<value_type, close_intent> || std::is_same_v<value_type, shutdown_message>)
                    begin_shutdown();
                else if constexpr (std::is_same_v<value_type, document_loaded_event>)
                    handle_document_loaded(std::move(value));
                else if constexpr (std::is_same_v<value_type, query_completed_event>)
                    handle_query_completed(std::move(value));
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
        cards_.clear();
        notices_.clear();
        selected_.reset();
        scroll_offset_ = 0.0f;
        document_loading_ = true;

        operation_request request { make_request(operation_kind::load_document, nullptr, 0) };
        request.document_path = document_path_;
        static_cast<void>(submitter_->submit(std::move(request)));
    }

    void logic_controller::handle_document_loaded(document_loaded_event event)
    {
        document_loading_ = false;
        notices_.clear();
        for (const diagnostic& value : event.diagnostics)
            if (value.severity != diagnostic_severity::information)
                notices_.push_back(value.message);

        if (event.document.has_value() == false)
            return;

        document_ = std::move(*event.document);
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

    std::shared_ptr<const view_snapshot> logic_controller::make_view_snapshot() const
    {
        auto snapshot { std::make_shared<view_snapshot>() };
        snapshot->document_path = document_path_;
        snapshot->selected = selected_;
        snapshot->filter_text = filter_;
        snapshot->sort = sort_;
        snapshot->notices = notices_;
        snapshot->window_width = window_width_;
        snapshot->window_height = window_height_;
        snapshot->scale = scale_;
        snapshot->scroll_offset = scroll_offset_;
        snapshot->shutting_down = shutting_down_;

        for (const card_state& card : cards_)
        {
            if (contains_ignoring_ascii_case(card.project.display_name, filter_) == false && contains_ignoring_ascii_case(card.project.path.original, filter_) == false)
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
            snapshot->cards.push_back(std::move(model));
        }

        if (sort_ == card_sort_key::name)
            std::sort(snapshot->cards.begin(), snapshot->cards.end(), name_before);
        else
            std::sort(snapshot->cards.begin(), snapshot->cards.end(), status_before);

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
