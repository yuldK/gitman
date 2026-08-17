#pragma once

#include "application/app_messages.h"
#include "application/process_cancellation.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "presentation/view_snapshot.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gitman {
    // 유일한 mutable app state의 소유자다 (ADR-004). logic thread에서만 사용하며
    // 모든 입력을 단일 inbox의 도착 순서대로 처리한다. 파일과 프로세스 I/O는 하지
    // 않고 `operation_submitter`로 worker에 위임한다.
    class logic_controller
    {
    public:
        explicit logic_controller(operation_submitter& submitter) noexcept;

        void handle(logic_message message);

        [[nodiscard]] bool shutdown_requested() const noexcept;
        // 종료 시 실행 중 작업에 전파되는 취소 token이다. 모든 요청에 실려 나간다.
        [[nodiscard]] process_cancellation_token cancellation() const noexcept;

        [[nodiscard]] std::shared_ptr<const view_snapshot> make_view_snapshot() const;

    private:
        struct card_state
        {
            project_definition project {};
            repository_snapshot snapshot {};
            std::vector<diagnostic> diagnostics {};
            std::uint64_t generation { 0 };
            bool busy { false };
            bool refresh_queued { false };
            bool has_local_result { false };
        };

        void handle_open_document(const open_document_intent& intent);
        void handle_document_loaded(document_loaded_event event);
        void handle_query_completed(query_completed_event event);
        void request_refresh(card_state& card);
        void begin_shutdown();

        [[nodiscard]] card_state* find_card(const project_id& id) noexcept;
        [[nodiscard]] operation_request make_request(operation_kind kind, const card_state* card, std::uint64_t generation);

        operation_submitter* submitter_ { nullptr };
        process_cancellation_source cancellation_source_ {};

        std::u8string document_path_ {};
        std::optional<workspace_document> document_ {};
        std::vector<card_state> cards_ {};
        std::vector<std::u8string> notices_ {};
        std::optional<project_id> selected_ {};
        std::u8string filter_ {};
        card_sort_key sort_ { card_sort_key::name };
        float window_width_ { 0.0f };
        float window_height_ { 0.0f };
        float scale_ { 1.0f };
        float scroll_offset_ { 0.0f };
        std::uint64_t next_operation_id_ { 1 };
        bool document_loading_ { false };
        bool shutting_down_ { false };
    };
} // namespace gitman
