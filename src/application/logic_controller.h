#pragma once

#include "application/app_messages.h"
#include "application/cancellation_registry.h"
#include "application/function_run_services.h"
#include "application/log_file_sink.h"
#include "application/process_cancellation.h"
#include "domain/operation_log.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/text_edit.h"
#include "presentation/view_snapshot.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
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
        // `registry`는 종료 시 UI thread가 실행 중 작업을 한꺼번에 취소하는 보관소다
        // (설계 L3). controller보다 오래 살아 있어야 한다.
        logic_controller(operation_submitter& submitter, cancellation_registry& registry) noexcept;
        // 카드 로그를 파일로도 적재하는 조립이다 (app-shell-design A4). sink는
        // controller보다 오래 살아 있어야 한다.
        logic_controller(operation_submitter& submitter, log_file_sink& log_sink, cancellation_registry& registry) noexcept;
        // 보관소를 스스로 드는 편의 생성자다 (test·단독 조립). 앱은 driver가 보관소를
        // 소유해 종료 시 UI thread에서 취소를 걸므로 위의 생성자를 쓴다.
        explicit logic_controller(operation_submitter& submitter);
        logic_controller(operation_submitter& submitter, log_file_sink& log_sink);

        void handle(logic_message message);

        // 기능 버튼 실행 경계를 묶는다 (function-buttons-design 4.4, 4.5). 풀(`submitter`)은
        // controller보다 오래 살아 있어야 한다. 묶지 않으면 실행 intent는 로그 한 줄로 거절된다
        // (기능 버튼을 쓰지 않는 test 조립). `services`의 함수가 하나라도 비어 있어도 묶지 않은
        // 것으로 둔다. 생성자를 늘리지 않으려고 따로 둔다.
        void bind_function_execution(function_run_submitter& submitter, function_run_services services);

        // 카드 하나에서 본 기능 버튼 하나의 상태다 (4.8). 판정은 배치와 무관하다.
        struct function_button_state
        {
            bool enabled { false };
            // 비활성 사유다. tooltip과 메뉴 라벨 접미가 쓴다 (T9).
            std::u8string disabled_reason {};
            // 그 기능의 출력 수집 실행이 이 카드에서 돌고 있다. 버튼은 중지 버튼으로 활성이다.
            bool running { false };
            // 마지막 실행이 실패했다 (실패 점). 다시 실행하거나 카드를 선택하면 지운다.
            bool failed { false };
            // 실패한 마지막 실행의 종료 코드다. 시간 초과·시작 실패·거부는 값이 없다 (메뉴 라벨, 접근 이름, T9).
            std::optional<std::int32_t> exit_code {};
        };

        // 그 카드에 그 기능이 적용되지 않거나(범위 밖, 문서·카드·기능 없음) 검증에 실패한
        // 항목이면 값이 없다. logic thread에서만 호출한다 (카드 view 구성과 test 관찰용).
        [[nodiscard]] std::optional<function_button_state> function_state(const project_id& id, std::u8string_view button_id) const;

        // `startup_intent`를 받으면 한 번 호출한다 (설계 L3). 앱 단위 설정을 읽는
        // 작업을 제출한다 (app-shell-design A1.2).
        void start();

        [[nodiscard]] bool shutdown_requested() const noexcept;
        // 종료 시 실행 중 작업에 전파되는 취소 token이다. 모든 요청에 실려 나간다.
        [[nodiscard]] process_cancellation_token cancellation() const noexcept;

        [[nodiscard]] std::shared_ptr<const view_snapshot> make_view_snapshot() const;

        // 카드의 로그 buffer다. 없는 카드는 nullptr다. logic thread에서만 호출한다
        // (view snapshot 구성과 test 관찰용 읽기 전용 접근).
        [[nodiscard]] const operation_log_buffer* card_log(const project_id& id) const noexcept;

    private:
        // 카드에서 실행 중인 기능 하나다 (4.7). `capture`는 completed까지, `detached`는 결과
        // event가 올 때까지만 있다.
        enum class function_run_phase
        {
            // 풀에 제출했고 started 전이다.
            queued,
            // started를 받았다. 경과 시간의 기준점이 있다.
            running,
            // 분리 실행을 제출했고 결과 전이다.
            launching,
        };

        struct function_run_state
        {
            std::uint64_t run_id { 0 };
            std::u8string button_id {};
            // 끝 줄은 실행을 시작할 때의 라벨과 timeout으로 남긴다 (4.11, 2.3.6).
            std::u8string label {};
            function_run_mode mode { function_run_mode::capture };
            std::int32_t timeout_seconds { default_function_timeout_seconds };
            function_run_phase phase { function_run_phase::queued };
            std::chrono::steady_clock::time_point started_at {};
            // 분리 실행의 `시작:` 줄은 결과를 받은 뒤에 남기므로 표시용 명령줄을 들고 있는다.
            std::u8string display_command_line {};
            // 출력 수집만 있다. `registry_`에도 등록해 종료 취소가 logic thread를 거치지 않는다.
            std::optional<process_cancellation_source> cancellation {};
        };

        // 버튼별 마지막 결과다 (4.7). 메모리에만 있다.
        struct function_result_state
        {
            std::u8string button_id {};
            bool failed { false };
            std::optional<std::int32_t> exit_code {};
        };

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
            // 변경 작업의 시작 시각이다. 로그 헤더의 경과 시간 표시 기준이며
            // change_operation_id가 0이 아닐 때만 의미가 있다.
            std::chrono::steady_clock::time_point change_started_at {};
            // 마지막으로 끝난 변경 작업이 걸린 시간이다 (2026-08-22 지시). 명령이
            // 끝나도 얼마나 걸렸는지 확인할 수 있도록 남기고, 다음 명령이 시작될 때
            // 지운다. 문서를 열고 아직 아무 명령도 실행하지 않았으면 값이 없다.
            std::optional<std::chrono::steady_clock::duration> change_elapsed {};
            // 변경 작업별 취소 source다. 조회와 달리 사용자가 카드 단위로 취소할 수
            // 있다 (stage-7-plan 4.4).
            std::optional<process_cancellation_source> change_cancellation {};
            // 이 카드의 기능 실행이다 (4.7). `busy`와 따로 둔다. 기능 실행은 `busy`를 세우지
            // 않아 저장소 상태 때문에 IDE 열기 같은 기능이 막히지 않는다 (B8).
            std::vector<function_run_state> function_runs {};
            std::vector<function_result_state> function_results {};
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
        // 문서를 열기 직전의 창 배치를 앱 설정에 남긴다 (S3.2). 문서가 열려 있으면
        // 아무것도 하지 않는다.
        void record_app_window_placement();
        void handle_document_saved(document_saved_event event);
        void handle_request_update(const request_update_intent& intent);
        void handle_request_switch(const request_switch_intent& intent);
        void handle_cancel_operation(const cancel_operation_intent& intent);
        // 클릭 시점에 치환과 검사를 마친 실행 값이다 (4.5). 확인 dialog를 열 때 하나를 들고, 확인할 때
        // 다시 만들어 같은지 본다.
        struct prepared_function_run
        {
            // 실제 argv에 들어갈 치환 결과다. env 값이 들어 있다.
            variable_expansion program {};
            std::vector<variable_expansion> arguments {};
            variable_expansion working_directory {};
            // 요청 조립이 `GITMAN_*`로 내보낼 내장 변수 표다.
            std::vector<variable_binding> builtins {};
            std::u8string display_command_line {};
        };

        // 기능 버튼 실행이다 (4.5). 활성 재판정 → 치환 → 허용 목록·절대 경로·cmd 방어 → 신뢰 확인
        // 또는 `confirm` 미리보기 → 풀 자리 → 제출 순서다.
        void handle_run_function(const run_function_intent& intent);
        // 활성 재판정과 클릭 시점 검사다. 걸리면 로그를 남기고(늦은 클릭은 아무것도 남기지 않고)
        // 값이 없다.
        [[nodiscard]] std::optional<prepared_function_run> prepare_function_run(card_state& card, const function_button& button);
        // 풀 자리를 보고 제출한다. 확인 dialog를 거쳤으면 확인할 때 다시 만든 값이다.
        void submit_function_run(card_state& card, const function_button& button, const prepared_function_run& prepared);
        // 지금 문서의 신뢰 해시다 (3.10). 기능 버튼 절이 없으면 비어 있다. 입력은 `function_trust.h`
        // 주석대로 읽을 때의 `source_digest_input`과 지금 project의 `{id, path.original, ${projectName} 값}`이다.
        [[nodiscard]] std::u8string current_function_digest() const;
        void open_function_confirm(function_confirm_mode mode, const card_state& card, const function_button& button, const prepared_function_run& prepared, std::u8string digest);
        [[nodiscard]] function_confirm_dialog_view build_function_trust_view(const function_button& clicked, const prepared_function_run& prepared) const;
        [[nodiscard]] function_confirm_dialog_view build_function_preview_view(const card_state& card, const function_button& button, const prepared_function_run& prepared) const;
        // `confirm` 미리보기의 줄(표시용 명령줄, 치환한 작업 폴더)이다. 미리보기와, 신뢰하지 않은
        // `confirm` 버튼의 신뢰 dialog가 같은 글을 쓴다.
        [[nodiscard]] std::vector<function_confirm_line> function_preview_lines(const prepared_function_run& prepared, std::u8string_view command_prefix, std::u8string_view folder_prefix) const;
        // 지금 창 폭으로 긴 줄을 나눈 dialog view다. snapshot과 스크롤 한계가 같은 값을 본다.
        [[nodiscard]] function_confirm_dialog_view wrapped_function_confirm_view() const;
        void handle_confirm_function_run();
        void handle_function_confirm_scroll(const function_confirm_scroll_intent& intent);
        void handle_stop_function(const stop_function_intent& intent);
        void handle_function_started(const function_started_event& event);
        void handle_function_log(function_log_event event);
        void handle_function_completed(function_completed_event event);
        // 클릭 시점 검사에 걸린 실행 거부다. 로그 한 줄(error)과 실패 점을 남긴다 (3.7).
        void reject_function_run(card_state& card, const function_button& button, std::u8string_view reason);
        void set_function_result(card_state& card, std::u8string_view button_id, bool failed, std::optional<std::int32_t> exit_code);
        [[nodiscard]] const function_button* find_function_button(std::u8string_view button_id) const noexcept;
        // 활성 조건(4.8)이다. 범위 밖이면 값이 없다. `${env:}`는 보지 않는다 (클릭 시점에 본다). `preview`는
        // `make_function_preview_lookup`으로 카드마다 한 번 만든 치환 값 묶음이다 — snapshot마다 카드 × 기능 수만큼
        // 부르므로 버튼마다 내장·사용자 변수 표를 새로 짓지 않는다 (T6에서 넘어온 일).
        [[nodiscard]] std::optional<function_button_state> evaluate_function(const card_state& card, const function_button& button, const variable_lookup& preview) const;
        // 치환 값 묶음이다. 로컬 조회 전이면 런타임 변수는 풀리지 않는다.
        [[nodiscard]] variable_lookup make_function_lookup(const card_state& card, function_environment_lookup environment) const;
        // 활성 판정용 치환 값 묶음이다. `${env:}`는 어떤 이름이든 정의된 것으로 친다 (3.7).
        [[nodiscard]] variable_lookup make_function_preview_lookup(const card_state& card) const;
        // 카드에 적용되는 기능의 표시 모델이다 (2.3). 문서 순서이고 카드당 20개까지다. `placements`는 문서의
        // `buttons`와 같은 순서의 보이는 배치(`resolve_visible_placements`)이고, snapshot이 한 번 만들어 모든 카드에
        // 넘긴다. 비어 있으면 이 함수가 만든다.
        [[nodiscard]] std::vector<card_function_view> card_function_views(const card_state& card, std::span<const std::optional<function_placement>> placements = {}) const;
        // 카드의 `⋯`·`Alt+↓`이 여는 기능 메뉴다 (2.3.2). 카드 안 버튼이라 선택을 바꾸지 않는다 (2.2.6).
        void handle_open_function_menu(const open_function_menu_intent& intent);
        // `Alt+1..3`이다 (2.3.4). N번째 보이는 `card` 기능이 그 카드에 적용되면 실행 intent와 같다.
        void handle_run_function_shortcut(const run_function_shortcut_intent& intent);
        // 활성 `card`·`bar` 버튼의 배치 메뉴다 (2.2.5). 카드 안 버튼이라 선택을 바꾸지 않는다.
        void handle_open_function_placement_menu(const open_function_placement_menu_intent& intent);
        // 배치를 바꾸고 곧바로 저장한다 (4.11). `focus_card`가 있으면 그 카드의 기능 줄 버튼에서 연 배치 메뉴로
        // 고른 것이라, 그 버튼이 줄을 떠나면 초점 옮기기를 싣는다 (2.2.5).
        void handle_set_function_placement(const set_function_placement_intent& intent, const std::optional<project_id>& focus_card);
        // 문서의 카드 중 하나에라도 적용되는 `bar` 기능(보이는 배치 기준)이 있다 (2.2.2 규칙 5, D23-A). 그때만
        // 기능 줄이 서고 로그 본문이 준다. 스크롤 한계와 view가 같은 판정을 쓴다.
        [[nodiscard]] bool has_function_bar() const;
        // 기능 줄의 표시 모델이다. `has_function_bar()`가 참일 때만 부른다.
        [[nodiscard]] function_bar_view build_function_bar_view() const;
        // 모든 카드에서 풀 자리를 차지한 실행 수다. 풀의 상한과 같은 기준이다 (4.4).
        [[nodiscard]] std::size_t function_runs_in_flight() const noexcept;
        [[nodiscard]] static bool function_running(const card_state& card) noexcept;
        // run id로 실행 항목을 찾는다. 끝났거나 다른 문서의 run이면 null이다.
        [[nodiscard]] static function_run_state* find_function_run(card_state& card, std::uint64_t run_id) noexcept;
        // update·switch를 시작할 수 있는 상태다. 카드 view, 카드 메뉴 view, 메뉴 클릭 재판정이
        // 같은 식을 쓴다. 같은 카드의 출력 수집 실행과 서로 막는다 (4.7, D6-A).
        [[nodiscard]] static bool change_allowed(const card_state& card) noexcept;
        void handle_operation_log(operation_log_event event);
        void handle_change_completed(change_completed_event event);
        void handle_begin_discovery(const begin_discovery_intent& intent);
        void handle_toggle_discovery_candidate(std::size_t index);
        void handle_confirm_discovery();
        void handle_cancel_discovery_dialog();
        void handle_discovery_completed(discovery_completed_event event);
        void handle_projects_registered(projects_registered_event event);
        void handle_close_document();
        void handle_show_notice(show_notice_intent intent);
        void handle_app_settings_loaded(app_settings_loaded_event event);
        void handle_app_settings_saved(app_settings_saved_event event);
        void handle_remove_recent_document(const remove_recent_document_intent& intent);
        void handle_toggle_recent_document_pin(const toggle_recent_document_pin_intent& intent);
        // 문서를 성공적으로 연 뒤 최근 목록 맨 앞으로 올리고 저장을 예약한다.
        void record_recent_document();
        void request_app_settings_save();
        void handle_discovery_dialog_scroll(float delta);
        void handle_open_settings();
        void handle_open_context_menu(const open_context_menu_intent& intent);
        void handle_open_document_context_menu(const open_document_context_menu_intent& intent);
        void handle_select_context_menu_item(const select_context_menu_item_intent& intent);
        void handle_set_theme_preference(const set_theme_preference_intent& intent);
        void handle_set_accent(const set_accent_intent& intent);
        void handle_open_local_changes(const open_local_changes_intent& intent);
        void handle_select_local_change(std::size_t index);
        void handle_local_changes(local_changes_event event);
        void handle_file_diff(file_diff_event event);
        void handle_local_changes_scroll(float delta);
        void handle_local_changes_diff_scroll(float delta);
        void request_selected_file_diff();
        void handle_set_settings_executable(set_settings_executable_intent intent);
        void handle_clear_settings_executable(const clear_settings_executable_intent& intent);
        void handle_select_settings_tab(const select_settings_tab_intent& intent);
        void handle_settings_scroll(float delta);
        // 환경설정 dialog의 표시 모델이다. snapshot 게시와 스크롤 한계 계산이 같은
        // 값을 봐야 하므로 한곳에서 만든다 (E1.3).
        [[nodiscard]] settings_dialog_view build_settings_dialog_view() const;
        void handle_clear_settings_override(const clear_settings_override_intent& intent);
        void handle_confirm_settings();
        void handle_begin_switch(const begin_switch_intent& intent);
        void handle_select_switch_candidate(std::size_t index);
        void handle_select_svn_browser_node(std::u8string_view url);
        void handle_toggle_svn_browser_node(std::u8string_view url);
        void handle_confirm_switch();
        void handle_switch_candidates(switch_candidates_event event);
        void handle_svn_directory(svn_directory_event event);
        void handle_switch_dialog_scroll(float delta);
        // 초점을 가진 텍스트 박스의 편집이다 (switch-dialog-query-bar-design R2.2).
        // 대상별 초안을 고른 뒤 순수 편집 모델에 맡기고, 그 대상의 규칙(숫자만,
        // 필터 재적용)을 뒤에 덧붙인다.
        void handle_edit_text_input(const edit_text_input_intent& intent);
        void handle_set_text_composition(const set_text_composition_intent& intent);
        // 조합 중인 박스의 표시 상태를 `view`에 실어 준다. 대상이 다르면 아무 일도
        // 하지 않는다 (E2.6).
        void apply_composition(text_input_target target, text_input_view& view) const;
        // 그 칸의 화면 글이다 — 조합 중이면 조합 글이 섞인 표시 글, 아니면 확정 글.
        [[nodiscard]] std::u8string displayed_text_of(text_input_target target) const;
        // 점진 검색 목록이 달라질 때 사라질 수 있는 선택·확인 단계·스크롤을 지운다
        // (`handle_edit_text_input`과 같은 규칙). 조합 한 단계마다 목록이 바뀔 수 있다 (D5).
        void reset_search_selection(text_input_target target);
        // 점진 검색(검색 칸이 목록을 좁히는 것)이 쓸 질의다. luil `search_query`의
        // 두 줄 규칙을 그대로 옮겨 적었다 (luil UI 헤더는 application에 들이지 않는다).
        //  - 조합 중이면 조합 글이 섞인 표시 글로 먼저 거른다.
        //  - 그 글로 하나도 맞지 않으면 확정 글로 물러선다. 한글은 한 글자가 여러
        //    단계로 완성되어 "토스"의 중간인 "토ㅅ"은 어디에도 없으므로, 물러서지
        //    않으면 목록이 사라졌다 나타나기를 반복한다.
        //  - 단 확정 글이 비어 있으면 물러서지 않는다. 빈 질의는 좁히기가 아니라
        //    "전부"라서 첫 글자를 조합하는 동안 목록 전체가 되살아난다.
        // 무엇이 맞는지는 부르는 쪽이 정한다 — `any_match`는 그 질의로 맞는 것이
        // 하나라도 있는지만 답한다.
        [[nodiscard]] std::u8string search_query_for(text_input_target target, const std::u8string& committed, const std::function<bool(std::u8string_view)>& any_match) const;
        // 글꼴 선택 dialog다 (fonts-and-in-app-dialogs-design F2). 목록은 UI thread가
        // 한 번 열거해 보낸 것을 그대로 쓴다.
        // 새 문서 만들기 dialog다 (fonts-and-in-app-dialogs-design F3). 폴더는 UI
        // thread의 Win32 dialog가 고르고 검증과 경로 조립은 여기서 한다.
        void handle_open_generate_document();
        void handle_confirm_generate_document();
        // 지금 값으로 만들어질 문서 경로다. 검증을 통과하지 못하면 빈 값이다.
        [[nodiscard]] std::u8string generate_document_path() const;
        void handle_open_font_dialog(const open_font_dialog_intent& intent);
        void handle_select_font_family(std::size_t index);
        void handle_confirm_font_dialog();
        void handle_font_dialog_scroll(float delta);
        // 필터를 통과한 글꼴 index다. 첫 항목은 항상 `(기본 글꼴)`을 뜻하는 빈 이름이다.
        [[nodiscard]] std::vector<std::u8string> filtered_font_families() const;
        void handle_submit_switch_query();
        void handle_clear_switch_query();
        void handle_refresh_switch_dialog();
        // reveal 한 걸음의 결과를 dialog 상태에 반영한다. 조회가 필요하면 제출하고,
        // 없는 경로면 안내를 남긴다.
        void apply_svn_browser_reveal(const svn_browser_reveal_progress& progress);
        // 이동 목표에 도달한 뒤 그 행이 목록 밖에 있으면 보이도록 스크롤한다.
        // 선택 표시만으로는 깊은 경로가 화면 아래에 숨는다 (2026-08-23 지시).
        void scroll_svn_browser_selection_into_view();
        void submit_svn_directory_query(std::u8string url);
        [[nodiscard]] std::size_t switch_dialog_row_count() const;
        // Git 질의로 거른 후보의 원본 index다. 질의가 비면 전체 순서 그대로다.
        // view의 목록·선택 index가 모두 이 목록 기준이다.
        [[nodiscard]] std::vector<std::size_t> filtered_switch_candidates() const;
        void install_document(workspace_document document, workspace_revision_token revision, std::vector<diagnostic> diagnostics);
        void request_refresh(card_state& card);
        void request_save();
        // 변경 작업(update·switch)을 시작한다. busy 카드는 거부하고 사유를 로그에
        // 남긴다.
        void begin_change(card_state& card, operation_kind kind, const update_options& options, const switch_candidate* target);
        void append_lifecycle_log(card_state& card, diagnostic_severity severity, std::u8string text);
        // 카드 buffer와 파일 sink에 같은 항목을 넣는다. sink가 알린 실패는 카드
        // 로그에 한 번만 남긴다 (A4.4).
        void append_card_log(card_state& card, operation_log_entry entry);
        // 문서가 바뀔 때 sink에 적재 대상을 알린다. 문서가 없으면 빈 목록이다.
        void publish_log_targets();
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
        // 전역 설정 위에 문서 override를 얹은 유효 설정이다. 조회·작업 요청과 표시,
        // 파일 로그 여부가 모두 이 값을 쓴다 (G3.1).
        [[nodiscard]] workspace_settings effective_settings() const;
        // 앱 외양 위에 문서 override를 얹은 유효 값이다 (S2.2).
        [[nodiscard]] appearance_settings effective_appearance() const;
        // 앱 글꼴 위에 문서 override를 얹은 유효 값이다 (2026-08-23 지시).
        [[nodiscard]] font_settings effective_fonts() const;
        // 선택 카드가 있어 하단 로그 pane이 보이는 상태다. layout 계산과 view 구성이
        // 같은 판정을 써야 스크롤 한계가 어긋나지 않는다.
        [[nodiscard]] bool has_log_pane() const noexcept;
        void handle_log_scroll(float delta);
        // 선택 카드 로그의 필터 적용 후 내용 높이다 (논리 픽셀).
        [[nodiscard]] float log_content_height() const noexcept;
        // 로그 본문 높이다 (논리 픽셀). 기능 줄 유무(`has_function_bar()`)는 카드를 훑는 판정이라 부르는 쪽이 한 번 구해 넘긴다.
        [[nodiscard]] float log_viewport_height(bool function_bar) const noexcept;
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
        // 기능 전용 실행 풀이다 (4.4). 묶지 않았으면 null이다.
        function_run_submitter* function_submitter_ { nullptr };
        function_run_services function_services_ {};
        log_file_sink* log_sink_ { nullptr };
        // 만든 취소 source를 등록해 두는 보관소다. 종료 취소가 logic thread를 거치지
        // 않아도 되게 한다 (설계 L3).
        cancellation_registry* registry_ { nullptr };
        // 편의 생성자가 만든 보관소다. registry_가 이것을 가리킨다.
        std::unique_ptr<cancellation_registry> owned_registry_ {};
        process_cancellation_source cancellation_source_ {};

        std::u8string document_path_ {};
        std::optional<workspace_document> document_ {};
        // 마지막 load 또는 save가 확인한 문서 revision이다. save의 낙관적 잠금 기준이다.
        workspace_revision_token revision_ {};
        std::vector<card_state> cards_ {};
        std::vector<notice_view_model> notices_ {};
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
            // 질의 막대의 글이다 (switch-dialog-query-bar-design Q1). Git은 후보
            // 필터, SVN은 저장소 root 기준 경로다. SVN은 dialog가 열릴 때 현재
            // 위치로 채워지고 노드를 고를 때마다 그 경로로 덮어쓴다.
            text_edit_state query {};
            float scroll_offset { 0.0f };
            struct directory_query
            {
                std::uint64_t operation_id { 0 };
                std::u8string url {};
            };
            std::vector<directory_query> directory_queries {};
        };

        std::optional<switch_dialog_state> switch_dialog_ {};

        // 글꼴 선택 dialog의 상태다 (F2.5). 값이 있으면 dialog가 열려 있다.
        struct font_dialog_state
        {
            // true이면 코드 글꼴을 고르는 중이다.
            bool code { false };
            text_edit_state query {};
            // 필터를 통과한 목록 기준 index다. 질의가 바뀌면 지운다.
            std::optional<std::size_t> selected {};
            float scroll_offset { 0.0f };
        };

        std::optional<font_dialog_state> font_dialog_ {};

        // 새 문서 만들기 dialog의 상태다 (F3). 값이 있으면 dialog가 열려 있다.
        struct generate_dialog_state
        {
            text_edit_state name {};
            std::u8string scan_root {};
            std::u8string location {};
            // 켜면 문서를 스캔 폴더 안에 만든다. 저장 위치 칸은 비활성이 된다.
            bool same_folder { true };
            std::u8string message {};
            bool executing { false };
        };

        std::optional<generate_dialog_state> generate_dialog_ {};
        // UI thread가 열거해 보낸 시스템 글꼴 가족 이름이다 (F2.4). 앱이 사는 동안
        // 바뀌지 않는다고 본다.
        std::vector<std::u8string> font_families_ {};

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
        // 있다. 저장 전까지 문서·앱 settings를 건드리지 않는다.
        struct settings_dialog_state
        {
            // 보고 있는 탭이다 (S1.2). 초안과 무관하다.
            settings_tab active_tab { settings_tab::tools };
            // 문서가 열려 있으면 문서 override를, 없으면 전역 설정을 편집한다
            // (global-settings-and-ui-fixes-design G3.2).
            bool document_mode { false };
            std::u8string git_path {};
            std::u8string svn_path {};
            // 상태 확인 제한 시간 텍스트 박스의 초안이다 (field-feedback-design
            // 1.3). 숫자만 담기며 비어 있으면 기본값이다.
            text_edit_state timeout {};
            // 업데이트 시 submodule 갱신 여부의 초안이다 (확인 overlay 대체).
            bool update_submodules { false };
            // 로컬 변경을 상관하지 않음 여부의 초안이다. status 순회를 건너뛴다.
            bool ignore_local_changes { false };
            // 카드 로그를 문서 폴더에 파일로 남길지의 초안이다 (A4.5).
            bool write_log_files { true };
            // 테마와 키 컬러의 초안이다 (D4). 다른 항목과 같이 `저장`에서만 반영되며
            // 화면 색은 그때까지 그대로다.
            theme_preference theme { theme_preference::system };
            std::u8string accent_id {};
            // 글꼴 초안이다 (2026-08-23 지시). 글꼴 선택 dialog가 여기에 쓰고
            // `저장`이 앱 설정 또는 문서 override로 옮긴다.
            std::u8string ui_font_family {};
            std::u8string code_font_family {};
            // 문서 모드에서 "문서에 정의됨" 표시다 (G3.2 암묵 덮어쓰기: 값을
            // 건드리면 켜지고, 저장 시 켜진 행만 override로 남는다). 전역 모드에서는
            // 쓰지 않는다.
            bool git_defined { false };
            bool svn_defined { false };
            bool timeout_defined { false };
            bool submodules_defined { false };
            bool ignore_local_defined { false };
            bool log_files_defined { false };
            bool theme_defined { false };
            bool accent_defined { false };
            bool ui_font_defined { false };
            bool code_font_defined { false };
            // 항목 열의 스크롤 위치다 (논리 픽셀). 창이 낮아 항목이 다 들어가지
            // 않을 때만 0이 아니다 (text-input-and-font-fallback-design E1).
            float scroll_offset { 0.0f };
        };

        std::optional<settings_dialog_state> settings_dialog_ {};

        // IME 조합 상태다 (text-input-and-font-fallback-design E2.3). UI thread의
        // 그림자 문서가 보내 주며, 조합이 끝나면 비워진다. 확정 전의 글이라 어느
        // 초안(`text_edit_state`)에도 들어가지 않는다 — 실행 취소 기록을 더럽히지
        // 않기 위해서다.
        struct text_composition_state
        {
            text_input_target target { text_input_target::settings_timeout };
            std::u8string text {};
            std::size_t caret { 0 };
            std::size_t composing_begin { 0 };
            std::size_t composing_end { 0 };
        };

        std::optional<text_composition_state> composition_ {};

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

        // 컨텍스트 메뉴의 상태다 (field-feedback-design 3장,
        // theme-and-banner-menu-design T1). 값이 있으면 메뉴가 앵커 좌표(창 좌표,
        // 물리 픽셀)에 떠 있다. 항목 목록과 활성 여부는 view 구성 시 카드·문서
        // 상태로 그때그때 계산한다.
        enum class context_menu_kind
        {
            card,
            // 배너(도구 막대) 우클릭이 연 문서 메뉴다. `card`는 비어 있다.
            document,
            // 카드의 `⋯`·`Alt+↓`·카드 메뉴의 `기능 더 보기…`가 연 기능 메뉴다 (function-buttons-design 2.3.2).
            functions,
            // 활성 `card`·`bar` 버튼이 연 배치 메뉴다 (function-buttons-design 2.2.5). `button`이 그 기능이다.
            placement,
        };

        struct context_menu_state
        {
            context_menu_kind kind { context_menu_kind::card };
            project_id card {};
            float anchor_x { 0.0f };
            float anchor_y { 0.0f };
            // 메뉴를 연 시각이다. 기능 항목 라벨의 경과 시간(`빌드 — 실행 중 00:12`)은 이 시각에서 멈춘다. snapshot은
            // 기능 출력·조회 event마다 다시 지어지므로 지을 때의 시각을 쓰면 떠 있는 메뉴의 라벨과 접근 이름이
            // 불규칙하게 바뀐다 (function-buttons-design 2.3.4).
            std::chrono::steady_clock::time_point opened_at {};
            // 배치 메뉴의 기능과, 그 메뉴를 기능 줄 버튼에서 열었는지다.
            std::u8string button {};
            bool from_bar { false };
        };

        std::optional<context_menu_state> context_menu_ {};
        // 마지막 초점 옮기기 요청이다 (function-buttons-design 2.2.5). 번호가 같은 요청은 UI가 한 번만 이행하므로
        // 이행한 뒤에도 남겨 두어도 된다. 번호는 문서를 바꿔도 되돌리지 않는다.
        std::optional<function_focus_request> function_focus_ {};
        std::uint64_t function_focus_revision_ { 0 };

        // 열린 메뉴의 항목 목록이다 (function-buttons-design T8). view와 고른 항목의 처리가 같은
        // 목록을 써서, 메뉴가 떠 있는 사이 바뀐 활성 여부를 고른 시점에 다시 본다. 대상이
        // 사라졌으면(카드가 없거나 문서 경로가 비었으면) 비어 있고, 그러면 메뉴를 그리지 않는다.
        [[nodiscard]] std::vector<context_menu_item_view> context_menu_items(const context_menu_state& menu) const;
        [[nodiscard]] std::vector<context_menu_item_view> card_context_menu_items(const card_state& card, std::chrono::steady_clock::time_point opened_at) const;
        [[nodiscard]] std::vector<context_menu_item_view> document_context_menu_items() const;
        // 기능 메뉴다 (2.3.2). 그 카드의 적용 기능 전체(배치와 상관없이 문서 순서) → 구분선 → `기능 편집…` 자리,
        // `문서 다시 읽기`다.
        [[nodiscard]] std::vector<context_menu_item_view> function_context_menu_items(const card_state& card, std::chrono::steady_clock::time_point opened_at) const;
        // 기능 항목 하나다. 기능 메뉴와 카드 메뉴 기능 절이 같은 항목을 쓴다. 경과 시간은 메뉴를 연 시각(`opened_at`)까지다.
        [[nodiscard]] static context_menu_item_view function_menu_item(const card_function_view& function, std::chrono::steady_clock::time_point opened_at);
        // T12 전의 `기능 편집…` 자리다. 배너 메뉴의 `VSCode로 열기`와 같은 동작이다 (2.3.2).
        [[nodiscard]] std::optional<context_menu_item_view> function_edit_menu_item() const;
        // 배치 메뉴다 (2.2.5). `카드에 두기`·`기능 줄에 두기`·`메뉴에만 두기`(radio, 보이는 배치에 `checked`) → 구분선 →
        // `기능 편집…` 자리다. 그 기능이 이 카드에 `card`나 `bar` 버튼으로 활성이 아니면 비어 있다(메뉴를 닫는다).
        [[nodiscard]] std::vector<context_menu_item_view> placement_context_menu_items(const card_state& card, std::u8string_view button_id, bool from_bar) const;
        // 고른 항목의 하위 동작을 한다. 항목은 지금 활성인 것이다. 외부 열기는 UI thread가
        // 이미 처리했으므로 여기서는 하지 않는다.
        void run_context_menu_item(const context_menu_state& menu, const context_menu_item_view& item);

        // 기능 실행 확인 dialog의 상태다 (function-buttons-design 3.10, 4.5). 값이 있으면 dialog가
        // 열려 있고, 그동안 다른 기능 실행 intent는 받지 않는다.
        struct function_confirm_state
        {
            function_confirm_mode mode { function_confirm_mode::trust };
            project_id card {};
            std::u8string button_id {};
            // dialog를 열 때의 신뢰 해시다. 신뢰 모드는 확인하면 이 값을 기록한다 — 사용자가 본
            // 정의의 해시다.
            std::u8string digest {};
            // dialog를 열 때의 실행 값이다. 확인할 때 다시 만든 값과 다르면 실행하지 않는다.
            prepared_function_run prepared {};
            // 열 때 만든 표시 모델이다. 줄은 나누기 전의 글이고 스크롤 위치(`view.scroll_offset`)만
            // 바뀐다. 창 폭에 맞춘 줄 나누기는 `wrapped_function_confirm_view`가 그때그때 한다.
            function_confirm_dialog_view view {};
        };

        std::optional<function_confirm_state> function_confirm_ {};
        // 확인 버튼 하나짜리 알림 다이얼로그다 (app-shell-design A3.2). 값이
        // 있으면 다른 dialog 위에 떠 있다.
        std::optional<notice_dialog_view> notice_dialog_ {};
        float window_width_ { 0.0f };
        float window_height_ { 0.0f };
        float scale_ { 1.0f };
        float scroll_offset_ { 0.0f };
        // 문서에서 읽은 창 배치와 그 게시 번호다. UI thread는 번호가 바뀔 때만
        // 창을 다시 배치한다.
        std::optional<window_placement> window_placement_ {};
        // UI thread가 마지막으로 알린 실제 창 배치다 (S3.2). 문서를 여는 순간 앱
        // 설정에 남길 값이라 문서 유무와 무관하게 갱신한다.
        std::optional<window_placement> current_placement_ {};
        std::uint64_t window_placement_revision_ { 0 };
        // 종료 시 배치를 문서에 저장해야 하는지다. UI thread가 보낸 배치가 문서의
        // 값과 다를 때만 켜진다.
        bool window_placement_dirty_ { false };
        // 앱 단위 설정이다 (app-shell-design A1). 파일을 읽기 전에도 기본값으로
        // 동작하며, 저장 실패는 문서 흐름을 막지 않는다.
        app_settings app_settings_ {};
        // 마지막으로 읽거나 쓴 원본 JSON이다. 알 수 없는 키를 보존하는 기준이다.
        std::u8string app_settings_shadow_ {};
        // 읽기가 끝나기 전에 문서를 열면 최근 항목이 덮어써질 수 있다. 읽기 완료
        // 전의 갱신은 대기시켰다가 결과가 도착하면 합친다.
        bool app_settings_loaded_ { false };
        std::uint64_t pending_app_settings_load_id_ { 0 };
        std::uint64_t pending_app_settings_save_id_ { 0 };
        bool app_settings_save_queued_ { false };
        // 저장 실패 문구는 한 번만 알린다. 반복 실패가 배너를 도배하지 않게 한다.
        bool app_settings_save_notified_ { false };
        // 종료 시 받은 창 배치가 앱 설정에 아직 저장되지 않았다는 표시다 (G1).
        // begin_shutdown이 이 값을 보고 저장을 한 번 내보낸다.
        bool app_settings_window_dirty_ { false };
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
