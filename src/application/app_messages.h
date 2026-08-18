#pragma once

#include "application/process_cancellation.h"
#include "application/project_store.h"
#include "application/repository_provider.h"
#include "domain/diagnostic.h"
#include "domain/operation_log.h"
#include "domain/project.h"
#include "domain/vcs_operation.h"
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

    // 지정한 폴더의 깊이 1 하위 디렉터리에서 저장소를 찾아 새 `.version-list` 문서를
    // 만들라는 의도다. UI thread의 생성 dialog가 만든다.
    struct generate_document_intent
    {
        std::u8string scan_root {};
        std::u8string document_path {};
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

    // 카드 경로를 전체 경로와 문서 기준 상대 경로 사이에서 바꾼다. 표시 방식은 문서
    // `settings`에 남으므로 처리 시 저장이 예약된다.
    struct toggle_path_display_intent
    {};

    // 카드 drag & drop이 만드는 순서 변경이다. `id` 카드를 `target` 카드의 앞 또는
    // 뒤로 옮긴다. 처리 시 정렬이 custom(문서 순서)으로 바뀌고 문서 저장이 예약된다.
    struct reorder_card_intent
    {
        project_id id {};
        project_id target {};
        bool place_after { false };
    };

    // 카드의 update 실행 요청이다. Git 카드는 확인 overlay가 option을 채워 보내고
    // SVN 카드는 기본 option으로 곧바로 보낸다 (stage-7-plan 4.4).
    struct request_update_intent
    {
        project_id id {};
        update_options options {};
    };

    // 카드의 switch 실행 요청이다. switch dialog가 검증을 통과한 후보를 담아 보낸다.
    // provider가 실행 직전에 같은 조건을 다시 검증한다 (REQ-007).
    struct request_switch_intent
    {
        project_id id {};
        switch_candidate target {};
    };

    // 진행 중인 변경 작업(update·switch)의 취소 요청이다. 조회는 취소 대상이 아니다.
    struct cancel_operation_intent
    {
        project_id id {};
    };

    struct clear_log_intent
    {
        project_id id {};
    };

    // 하단 로그 뷰의 스트림 필터 변경이다. 선택 카드의 로그 뷰 상태에 적용된다.
    struct set_log_filter_intent
    {
        log_stream_filter filter { log_stream_filter::all };
    };

    struct set_log_auto_scroll_intent
    {
        bool enabled { true };
    };

    // 로그 뷰의 스크롤 변화량이다 (논리 픽셀, 양수가 아래). 위로 굴리면 자동
    // 스크롤이 꺼지고 맨 아래에 닿으면 다시 켜진다.
    struct log_scroll_intent
    {
        float delta { 0.0f };
    };

    // Git 카드의 update 버튼이 여는 확인 overlay다 (stage-7-plan 4.4). SVN 카드는
    // overlay 없이 `request_update_intent`를 바로 보낸다.
    struct show_update_options_intent
    {
        project_id id {};
    };

    struct set_update_submodules_intent
    {
        bool enabled { false };
    };

    struct confirm_update_intent
    {};

    struct cancel_update_options_intent
    {};

    // 카드의 switch 버튼이 여는 switch dialog다 (REQ-007, stage-7-plan 4.5). 열면
    // 곧바로 remote-first 후보 조회를 제출한다.
    struct begin_switch_intent
    {
        project_id id {};
    };

    struct select_switch_candidate_intent
    {
        std::size_t index { 0 };
    };

    // 확인 버튼이다. tracking branch가 필요한 후보는 첫 확인이 생성 안내를 띄우고
    // 두 번째 확인이 실행한다 (단계 4의 tracking_branch_confirmed 계약).
    struct confirm_switch_intent
    {};

    struct cancel_switch_dialog_intent
    {};

    struct switch_dialog_scroll_intent
    {
        float delta { 0.0f };
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

    // UI thread가 종료 직전에 읽은 창 배치다. logic이 현재 문서에 반영하고 종료
    // 저장에 싣는다. close intent보다 먼저 게시해야 한다 (같은 채널이라 순서가
    // 보장된다).
    struct window_placement_intent
    {
        window_placement placement {};
    };

    struct close_intent
    {};

    // worker가 수행할 작업이다. logic만 만들고 scheduler가 lane 정책으로 배정한다.
    enum class operation_kind
    {
        load_document,
        // 깊이 1 저장소 탐색 결과로 새 `.version-list` 문서를 만든다. load·save와
        // 같은 lane에서 직렬화된다.
        generate_document,
        // 순서 변경 등으로 바뀐 문서를 원자적으로 다시 쓴다. load와 같은 lane에서
        // 직렬화된다.
        save_document,
        // 로컬 조회만 수행한다. 문서를 연 직후의 초기 표시다 (plan 5.1).
        query_local,
        // 로컬 조회 후 remote-first 원격 판정까지 수행한다. refresh 버튼의 동작이다.
        refresh,
        // 사전 검사를 포함한 update 실행이다. 카드 lane에서 직렬화된다 (단계 7).
        update,
        // 실행 직전 재검증을 포함한 switch 실행이다 (단계 7).
        switch_to,
        // remote-first 전환 후보 조회다. switch dialog가 요청한다 (단계 7).
        query_switch_candidates,
    };

    struct operation_request
    {
        std::uint64_t operation_id { 0 };
        std::uint64_t generation { 0 };
        operation_kind kind { operation_kind::query_local };
        std::u8string document_path {};
        // generate_document 전용: 저장소를 찾을 스캔 루트다.
        std::u8string scan_root {};
        project_definition project {};
        // worker가 도구 발견에 사용할 문서 수준 설정의 사본이다.
        workspace_settings settings {};
        // save_document 전용: 저장할 문서 내용과 낙관적 잠금의 기준 revision이다.
        std::optional<workspace_document> document {};
        workspace_revision_token revision {};
        // update 전용 option이다 (ADR-003, 기본 off).
        update_options options {};
        // switch_to 전용: 검증을 통과한 전환 대상이다.
        std::optional<switch_candidate> switch_target {};
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

    struct document_generated_event
    {
        std::uint64_t operation_id { 0 };
        std::u8string document_path {};
        // 성공 시에만 값이 있다. 생성된 문서와 다음 저장의 기준 revision이다.
        std::optional<workspace_document> document {};
        std::optional<workspace_revision_token> revision {};
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

    struct document_saved_event
    {
        std::uint64_t operation_id { 0 };
        // 저장이 성공하면 다음 저장의 기준이 되는 새 revision이다.
        std::optional<workspace_revision_token> revision {};
        std::vector<diagnostic> diagnostics {};
    };

    // 변경 작업(update·switch)의 프로세스 출력 배치다. worker의 로그 sink가 모아
    // 보내고 logic이 카드 buffer에 sequence를 부여하며 담는다 (stage-7-plan 4.1).
    struct operation_log_event
    {
        std::uint64_t operation_id { 0 };
        project_id id {};
        std::vector<operation_log_entry> entries {};
    };

    // 변경 작업(update·switch)의 최종 결과다. 그 작업의 마지막 event이며 snapshot은
    // 실행 직후 재조회한 로컬 상태다.
    struct change_completed_event
    {
        std::uint64_t operation_id { 0 };
        std::uint64_t generation { 0 };
        project_id id {};
        operation_kind kind { operation_kind::update };
        repository_change_result result {};
    };

    // remote-first 전환 후보 조회 결과다. switch dialog 상태가 소비한다.
    struct switch_candidates_event
    {
        std::uint64_t operation_id { 0 };
        project_id id {};
        switch_candidate_result result {};
    };

    struct shutdown_message
    {};

    // logic thread의 단일 inbox payload다 (ADR-005 topology). 도착 순서 그대로
    // 처리된다.
    using logic_message = std::variant<open_document_intent, generate_document_intent, refresh_all_intent, refresh_card_intent, select_card_intent, set_filter_intent, set_sort_intent,
        toggle_path_display_intent, reorder_card_intent, request_update_intent, request_switch_intent, cancel_operation_intent, clear_log_intent, set_log_filter_intent, set_log_auto_scroll_intent,
        log_scroll_intent, show_update_options_intent, set_update_submodules_intent, confirm_update_intent, cancel_update_options_intent, begin_switch_intent, select_switch_candidate_intent,
        confirm_switch_intent, cancel_switch_dialog_intent, switch_dialog_scroll_intent, window_metrics_intent, scroll_intent, window_placement_intent, close_intent, document_loaded_event,
        document_generated_event, query_completed_event, document_saved_event, operation_log_event, change_completed_event, switch_candidates_event, shutdown_message>;

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
