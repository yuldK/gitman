#pragma once

#include "application/process_cancellation.h"
#include "application/project_store.h"
#include "application/repository_provider.h"
#include "domain/diagnostic.h"
#include "domain/project.h"
#include "presentation/view_snapshot.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gitman {
    // input thread가 만드는 사용자 의도다. 좌표가 아니라 의미를 담는다 (ADR-004).
    struct open_document_intent
    {
        std::u8string path {};
    };

    struct refresh_all_intent
    {};

    struct refresh_card_intent
    {
        project_id id {};
    };

    struct select_card_intent
    {
        // 값이 없으면 선택 해제다.
        std::optional<project_id> id {};
    };

    struct set_filter_intent
    {
        std::u8string text {};
    };

    struct set_sort_intent
    {
        card_sort_key key { card_sort_key::name };
    };

    // UI thread가 전달하는 창 크기와 DPI 배율이다. layout snapshot 계산의 입력이다.
    struct window_metrics_intent
    {
        float width { 0.0f };
        float height { 0.0f };
        float scale { 1.0f };
    };

    struct scroll_intent
    {
        // 논리 픽셀 단위의 스크롤 변화량이다. 양수가 아래로 이동이다.
        float delta { 0.0f };
    };

    struct close_intent
    {};

    // worker가 수행할 작업이다. logic만 만들고 scheduler가 lane 정책으로 배정한다.
    enum class operation_kind
    {
        load_document,
        // 로컬 조회만 수행한다. 문서를 연 직후의 초기 표시다 (plan 5.1).
        query_local,
        // 로컬 조회 후 remote-first 원격 판정까지 수행한다. refresh 버튼의 동작이다.
        refresh,
    };

    struct operation_request
    {
        std::uint64_t operation_id { 0 };
        std::uint64_t generation { 0 };
        operation_kind kind { operation_kind::query_local };
        std::u8string document_path {};
        project_definition project {};
        // worker가 도구 발견에 사용할 문서 수준 설정의 사본이다.
        workspace_settings settings {};
        process_cancellation_token token {};
    };

    // worker가 돌려주는 결과다. generation 검사는 logic이 한다 (ADR-005 7.2).
    struct document_loaded_event
    {
        std::uint64_t operation_id { 0 };
        std::optional<workspace_document> document {};
        workspace_revision_token revision {};
        std::vector<diagnostic> diagnostics {};
    };

    struct query_completed_event
    {
        std::uint64_t operation_id { 0 };
        std::uint64_t generation { 0 };
        project_id id {};
        // refresh는 로컬 결과와 원격 결과를 순서대로 두 번 보고한다. 원격 보고가
        // 그 작업의 마지막 event다.
        bool remote { false };
        bool final_event { true };
        repository_query_result result {};
    };

    struct shutdown_message
    {};

    // logic thread의 단일 inbox payload다 (ADR-005 topology). 도착 순서 그대로
    // 처리된다.
    using logic_message = std::variant<open_document_intent, refresh_all_intent, refresh_card_intent, select_card_intent, set_filter_intent, set_sort_intent, window_metrics_intent, scroll_intent,
        close_intent, document_loaded_event, query_completed_event, shutdown_message>;

    // logic이 만든 작업을 실행 계층으로 넘기는 경계다. 단계 6의 scheduler가 구현하고
    // test는 기록 대역을 주입한다.
    class operation_submitter
    {
    public:
        operation_submitter() = default;
        operation_submitter(const operation_submitter&) = delete;
        operation_submitter(operation_submitter&&) = delete;
        operation_submitter& operator=(const operation_submitter&) = delete;
        operation_submitter& operator=(operation_submitter&&) = delete;
        virtual ~operation_submitter() = default;

        [[nodiscard]] virtual bool submit(operation_request request) = 0;
    };
} // namespace gitman
