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
        void handle_generate_document(const generate_document_intent& intent);
        void handle_document_loaded(document_loaded_event event);
        void handle_document_generated(document_generated_event event);
        void handle_query_completed(query_completed_event event);
        void handle_reorder_card(const reorder_card_intent& intent);
        void handle_document_saved(document_saved_event event);
        void install_document(workspace_document document, workspace_revision_token revision, std::vector<diagnostic> diagnostics);
        void request_refresh(card_state& card);
        void request_save();
        void begin_shutdown();

        [[nodiscard]] card_state* find_card(const project_id& id) noexcept;
        [[nodiscard]] operation_request make_request(operation_kind kind, const card_state* card, std::uint64_t generation);

        operation_submitter* submitter_ { nullptr };
        process_cancellation_source cancellation_source_ {};

        std::u8string document_path_ {};
        std::optional<workspace_document> document_ {};
        // 마지막 load 또는 save가 확인한 문서 revision이다. save의 낙관적 잠금 기준이다.
        workspace_revision_token revision_ {};
        std::vector<card_state> cards_ {};
        std::vector<std::u8string> notices_ {};
        // 마지막 저장 실패의 요약이다. 성공하면 비워지고 view notices의 맨 앞에 실린다.
        std::u8string save_notice_ {};
        std::optional<project_id> selected_ {};
        std::u8string filter_ {};
        card_sort_key sort_ { card_sort_key::name };
        float window_width_ { 0.0f };
        float window_height_ { 0.0f };
        float scale_ { 1.0f };
        float scroll_offset_ { 0.0f };
        std::uint64_t next_operation_id_ { 1 };
        bool document_loading_ { false };
        // 생성은 한 번에 하나만 진행한다. id가 0이 아니면 진행 중이며, id 비교로
        // 다른 문서를 연 뒤 도착한 늦은 생성 결과를 구분해 버린다.
        std::uint64_t pending_generation_operation_id_ { 0 };
        // 저장은 한 번에 하나만 내보내고, 진행 중 들어온 변경은 한 번으로 병합한다.
        // operation id가 0이 아니면 저장이 진행 중이며, id 비교로 다른 문서를 열기
        // 전의 늦은 저장 결과를 구분해 버린다.
        std::uint64_t pending_save_operation_id_ { 0 };
        bool save_queued_ { false };
        bool shutting_down_ { false };
    };
} // namespace gitman
