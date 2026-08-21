#pragma once

#include "application/app_messages.h"
#include "application/process_cancellation.h"
#include "domain/operation_log.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
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

        // 카드의 로그 buffer다. 없는 카드는 nullptr다. logic thread에서만 호출한다
        // (view snapshot 구성과 test 관찰용 읽기 전용 접근).
        [[nodiscard]] const operation_log_buffer* card_log(const project_id& id) const noexcept;

    private:
        struct card_state
        {
            project_definition project {};
            repository_snapshot snapshot {};
            std::vector<diagnostic> diagnostics {};
            operation_log_buffer log {};
            std::uint64_t generation { 0 };
            // 진행 중인 변경 작업(update·switch)이다. 0이면 없다. 로그와 완료 event를
            // 이 id로 대조해 늦은 결과를 버린다.
            std::uint64_t change_operation_id { 0 };
            operation_kind change_kind { operation_kind::update };
            // 변경 작업별 취소 source다. 조회와 달리 사용자가 카드 단위로 취소할 수
            // 있다 (stage-7-plan 4.4).
            std::optional<process_cancellation_source> change_cancellation {};
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
        void handle_select_card(const select_card_intent& intent);
        void handle_toggle_path_display();
        void handle_window_placement(const window_placement_intent& intent);
        void handle_document_saved(document_saved_event event);
        void handle_request_update(const request_update_intent& intent);
        void handle_request_switch(const request_switch_intent& intent);
        void handle_cancel_operation(const cancel_operation_intent& intent);
        void handle_operation_log(operation_log_event event);
        void handle_change_completed(change_completed_event event);
        void handle_begin_discovery(const begin_discovery_intent& intent);
        void handle_toggle_discovery_candidate(std::size_t index);
        void handle_confirm_discovery();
        void handle_cancel_discovery_dialog();
        void handle_discovery_completed(discovery_completed_event event);
        void handle_projects_registered(projects_registered_event event);
        void handle_discovery_dialog_scroll(float delta);
        void handle_open_settings();
        void handle_open_context_menu(const open_context_menu_intent& intent);
        void handle_open_local_changes(const open_local_changes_intent& intent);
        void handle_select_local_change(std::size_t index);
        void handle_local_changes(local_changes_event event);
        void handle_file_diff(file_diff_event event);
        void handle_local_changes_scroll(float delta);
        void handle_local_changes_diff_scroll(float delta);
        void request_selected_file_diff();
        void handle_set_settings_executable(set_settings_executable_intent intent);
        void handle_clear_settings_executable(const clear_settings_executable_intent& intent);
        void handle_edit_settings_timeout(const edit_settings_timeout_intent& intent);
        void handle_confirm_settings();
        void handle_begin_switch(const begin_switch_intent& intent);
        void handle_select_switch_candidate(std::size_t index);
        void handle_select_svn_browser_node(std::u8string_view url);
        void handle_toggle_svn_browser_node(std::u8string_view url);
        void handle_confirm_switch();
        void handle_switch_candidates(switch_candidates_event event);
        void handle_svn_directory(svn_directory_event event);
        void handle_switch_dialog_scroll(float delta);
        void submit_svn_directory_query(std::u8string url);
        [[nodiscard]] std::size_t switch_dialog_row_count() const;
        void install_document(workspace_document document, workspace_revision_token revision, std::vector<diagnostic> diagnostics);
        void request_refresh(card_state& card);
        void request_save();
        // 변경 작업(update·switch)을 시작한다. busy 카드는 거부하고 사유를 로그에
        // 남긴다.
        void begin_change(card_state& card, operation_kind kind, const update_options& options, const switch_candidate* target);
        void append_lifecycle_log(card_state& card, diagnostic_severity severity, std::u8string text);
        void cancel_running_changes() noexcept;
        void begin_shutdown();

        // 필터를 통과한 카드를 정렬 규칙대로 담은 표시 목록이다. view snapshot과
        // 스크롤 계산이 같은 순서를 보도록 한 곳에서 만든다.
        [[nodiscard]] std::vector<card_view_model> build_ordered_cards() const;
        [[nodiscard]] bool matches_filter(const card_state& card) const noexcept;
        // 카드에 그릴 경로 문자열이다. 문서 설정에 따라 전체 경로 또는 문서가 있는
        // 폴더 기준 상대 경로다.
        [[nodiscard]] std::u8string display_path(const project_definition& project) const;
        [[nodiscard]] bool relative_paths() const noexcept;
        // 선택 카드가 있어 하단 로그 pane이 보이는 상태다. layout 계산과 view 구성이
        // 같은 판정을 써야 스크롤 한계가 어긋나지 않는다.
        [[nodiscard]] bool has_log_pane() const noexcept;
        void handle_log_scroll(float delta);
        // 선택 카드 로그의 필터 적용 후 내용 높이다 (논리 픽셀).
        [[nodiscard]] float log_content_height() const noexcept;
        [[nodiscard]] float log_viewport_height() const noexcept;
        void reset_log_view_state() noexcept;
        // 스크롤 한계만 필요한 곳은 표시 모델을 만들지 않는다. 창 크기 변경처럼
        // 자주 오는 입력이 이 경로를 쓴다.
        [[nodiscard]] std::size_t visible_card_count() const noexcept;
        // 필터·정렬·창 크기가 바뀐 뒤 저장된 스크롤 값을 다시 범위 안으로 넣는다.
        void clamp_scroll();
        void scroll_selected_into_view();
        [[nodiscard]] float list_viewport_height() const noexcept;
        [[nodiscard]] bool has_notice() const noexcept;

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
        // 하단 로그 뷰의 표시 상태다. 선택이 바뀌면 기본값으로 돌아간다.
        log_stream_filter log_filter_ { log_stream_filter::all };
        bool log_auto_scroll_ { true };
        float log_scroll_offset_ { 0.0f };
        // switch dialog의 상태다 (stage-7-plan 4.5). 값이 있으면 dialog가 열려 있다.
        struct switch_dialog_state
        {
            project_id card {};
            // 후보 조회 작업의 id다. 늦은 결과를 구분한다.
            std::uint64_t candidates_operation_id { 0 };
            bool loading { true };
            bool subversion { false };
            switch_candidate_result candidates {};
            std::optional<svn_repository_browser_state> svn_browser {};
            std::optional<std::size_t> selected {};
            // tracking branch 생성의 두 단계 확인 중 첫 단계가 끝났다.
            bool tracking_confirm_pending { false };
            // 전환 실행이 제출되어 결과를 기다린다.
            bool executing { false };
            // 실행 거부·실패 사유다. 후보를 다시 고르면 지워진다.
            std::u8string message {};
            float scroll_offset { 0.0f };
            struct directory_query
            {
                std::uint64_t operation_id { 0 };
                std::u8string url {};
            };
            std::vector<directory_query> directory_queries {};
        };

        std::optional<switch_dialog_state> switch_dialog_ {};

        // 탐색 후보 선택 등록 dialog의 상태다 (REQ-004, stage-8-plan 5.2). 값이
        // 있으면 dialog가 열려 있다.
        struct discovery_dialog_state
        {
            std::u8string scan_root {};
            // 진행 중인 탐색 작업의 id다. 0이면 탐색이 끝났다. 늦은 결과를 구분한다.
            std::uint64_t scan_operation_id { 0 };
            // 탐색 전용 취소 source다. dialog 취소와 종료가 신호한다.
            std::optional<process_cancellation_source> scan_cancellation {};
            bool loading { true };
            discovery_result result {};
            // 후보와 같은 길이의 체크 상태다. 제외 사유가 없는 후보만 참일 수 있다.
            std::vector<bool> checked {};
            // 진행 중인 등록 작업의 id다. 0이면 등록 실행 중이 아니다.
            std::uint64_t register_operation_id { 0 };
            // 탐색·등록 실패 사유다. 후보를 다시 고르면 지워진다.
            std::u8string message {};
            float scroll_offset { 0.0f };
        };

        std::optional<discovery_dialog_state> discovery_dialog_ {};

        // 환경설정 dialog의 경로 초안이다 (REQ-017). 값이 있으면 dialog가 열려
        // 있다. 저장 전까지 문서 settings를 건드리지 않는다.
        struct settings_dialog_state
        {
            std::u8string git_path {};
            std::u8string svn_path {};
            // 상태 확인 제한 시간 텍스트 박스의 초안이다 (field-feedback-design
            // 1.3). 숫자만 담기며 비어 있으면 기본값이다.
            std::u8string timeout_text {};
            // 업데이트 시 submodule 갱신 여부의 초안이다 (확인 overlay 대체).
            bool update_submodules { false };
            // 로컬 변경을 상관하지 않음 여부의 초안이다. status 순회를 건너뛴다.
            bool ignore_local_changes { false };
        };

        std::optional<settings_dialog_state> settings_dialog_ {};

        // 로컬 변경 확인 dialog의 상태다 (field-feedback-design 2.3). 값이 있으면
        // dialog가 열려 있다. 목록·diff는 각각의 조회 operation id로 늦은 결과를
        // 걸러낸다.
        struct local_changes_dialog_state
        {
            project_id card {};
            std::u8string title {};
            std::uint64_t list_operation_id { 0 };
            std::uint64_t diff_operation_id { 0 };
            bool loading { true };
            std::vector<local_change_entry> entries {};
            std::optional<std::size_t> selected {};
            bool diff_loading { false };
            std::vector<two_way_diff_row> diff_rows {};
            std::u8string diff_notice {};
            std::u8string message {};
            float list_scroll { 0.0f };
            float diff_scroll { 0.0f };
        };

        std::optional<local_changes_dialog_state> local_changes_dialog_ {};

        // 카드 컨텍스트 메뉴의 상태다 (field-feedback-design 3장). 값이 있으면
        // 메뉴가 앵커 좌표(창 좌표, 물리 픽셀)에 떠 있다. 항목 목록과 활성 여부는
        // view 구성 시 카드 상태로 그때그때 계산한다.
        struct context_menu_state
        {
            project_id card {};
            float anchor_x { 0.0f };
            float anchor_y { 0.0f };
        };

        std::optional<context_menu_state> context_menu_ {};
        float window_width_ { 0.0f };
        float window_height_ { 0.0f };
        float scale_ { 1.0f };
        float scroll_offset_ { 0.0f };
        // 문서에서 읽은 창 배치와 그 게시 번호다. UI thread는 번호가 바뀔 때만
        // 창을 다시 배치한다.
        std::optional<window_placement> window_placement_ {};
        std::uint64_t window_placement_revision_ { 0 };
        // 종료 시 배치를 문서에 저장해야 하는지다. UI thread가 보낸 배치가 문서의
        // 값과 다를 때만 켜진다.
        bool window_placement_dirty_ { false };
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
