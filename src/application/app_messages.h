#pragma once

#include "application/process_cancellation.h"
#include "application/process_request.h"
#include "application/project_store.h"
#include "application/repository_provider.h"
#include "domain/app_settings.h"
#include "domain/diagnostic.h"
#include "domain/discovery.h"
#include "domain/operation_log.h"
#include "domain/project.h"
#include "domain/text_edit.h"
#include "domain/vcs_operation.h"
#include "presentation/view_snapshot.h"

#include <chrono>
#include <cstddef>
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

    // 카드 경로를 전체 경로와 문서 기준 상대 경로 사이에서 바꾼다. 표시 방식은 문서
    // `settings`에 남으므로 처리 시 저장이 예약된다.
    struct toggle_path_display_intent
    {};

    // 카드 drag & drop이 만드는 순서 변경이다. `id` 카드를 `target` 카드의 앞 또는
    // 뒤로 옮긴다. 카드는 항상 문서 순서로 표시되므로 처리 시 문서 저장이 예약된다.
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

    // 카드의 기능 버튼 실행이다 (function-buttons-design 4.5). 카드 버튼, 기능 줄 버튼,
    // 기능 메뉴, 카드 메뉴, `Alt+N`이 같은 intent를 낸다. 그 기능이 이미 실행 중이면 아무것도
    // 하지 않는다. 중지는 `stop_function_intent`다 (`Alt+N`이 중지하지 않게, 2.3.4).
    struct run_function_intent
    {
        project_id id {};
        std::u8string button_id {};
    };

    // 실행 중인 출력 수집 기능의 중지다. 실행 중인 버튼이 중지 버튼으로 바뀌어 낸다.
    // 분리 실행은 취소가 없어 대상이 아니다.
    struct stop_function_intent
    {
        project_id id {};
        std::u8string button_id {};
    };

    // 기능 실행 확인 dialog(신뢰 확인, `confirm` 미리보기)의 확인이다 (function-buttons-design
    // 3.10, 4.5). logic은 다시 치환해 dialog를 열 때와 같은지 본 뒤에만 실행한다. 확인 한 번은 언제나
    // 실행 한 번으로 끝나고 다른 dialog를 잇따라 열지 않으므로, 두 번 누른 확인이 다음 dialog를
    // 지나가는 일이 없다 — 그래서 dialog를 가르는 번호를 싣지 않는다.
    struct confirm_function_run_intent
    {};

    // 기능 실행 확인 dialog의 취소다. 취소 버튼·Esc·배경 클릭이 같은 intent를 낸다.
    struct cancel_function_run_intent
    {};

    // 기능 실행 확인 dialog 목록의 스크롤 단위다. 휠·막대는 거리(`delta`)를 싣고, 키는 단위만 싣는다 —
    // 한 쪽의 높이는 목록 높이를 아는 logic이 정한다.
    enum class function_confirm_scroll_step
    {
        delta,
        line_up,
        line_down,
        page_up,
        page_down,
        start,
        end,
    };

    struct function_confirm_scroll_intent
    {
        float delta { 0.0f };
        function_confirm_scroll_step step { function_confirm_scroll_step::delta };
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

    // 카드의 switch 버튼이 여는 switch dialog다. Git은 remote-first 후보 조회를,
    // SVN은 repository root/current URL 초기 조회를 곧바로 제출한다.
    struct begin_switch_intent
    {
        project_id id {};
    };

    struct select_switch_candidate_intent
    {
        std::size_t index { 0 };
    };

    // SVN repo-browser의 디렉터리 행 선택과 펼침 글리프다. URL을 직접 담아 tree가
    // 펼쳐지며 표시 행 index가 바뀌어도 같은 노드를 가리킨다.
    struct select_svn_browser_node_intent
    {
        std::u8string url {};
    };

    struct toggle_svn_browser_node_intent
    {
        std::u8string url {};
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

    // Enter와 SVN의 이동 버튼이다. Git은 걸러진 첫 후보를 고르고, SVN은 박스의
    // 경로로 트리를 펼쳐 내려간다.
    struct submit_switch_query_intent
    {};

    // Git 질의 막대의 지우기 버튼이다. SVN에는 없다.
    struct clear_switch_query_intent
    {};

    // 질의 막대 왼쪽의 갱신 버튼이다. Git은 후보를 다시 조회하고, SVN은 선택 노드의
    // cache를 버리고 다시 조회한다.
    struct refresh_switch_dialog_intent
    {};

    // 도구 막대·시작 페이지의 `새 문서 만들기`가 여는 앱 UI dialog다
    // (fonts-and-in-app-dialogs-design F3). 이전의 Win32 모달 팝업을 대신한다.
    struct open_generate_document_intent
    {};

    // Win32 폴더 dialog가 고른 경로가 돌아온다. 취소하면 오지 않는다.
    struct set_generate_scan_root_intent
    {
        std::u8string path {};
    };

    struct set_generate_location_intent
    {
        std::u8string path {};
    };

    // `스캔 폴더에 만들기`다. 켜면 저장 위치 칸이 비활성이 된다.
    struct toggle_generate_same_folder_intent
    {};

    struct confirm_generate_document_intent
    {};

    struct cancel_generate_document_intent
    {};

    // 환경설정의 `선택…` 버튼이 여는 글꼴 선택 dialog다 (F2.5). `code`이면 코드
    // 글꼴을, 아니면 UI 글꼴을 고른다.
    struct open_font_dialog_intent
    {
        bool code { false };
    };

    // 목록 행 선택이다. index는 필터를 통과한 목록 기준이다.
    struct select_font_family_intent
    {
        std::size_t index { 0 };
    };

    // `선택` 버튼이다. 고른 이름을 앱 설정에 넣고 저장을 예약한다.
    struct confirm_font_dialog_intent
    {};

    struct cancel_font_dialog_intent
    {};

    struct font_dialog_scroll_intent
    {
        float delta { 0.0f };
    };

    // toolbar 탐색 버튼이 여는 탐색 후보 선택 등록 dialog다 (REQ-004, stage-8-plan
    // 5.2). 스캔 루트는 UI thread의 폴더 선택 dialog가 채운다. 열면서 곧바로 깊이 1
    // 탐색을 worker에 제출한다.
    struct begin_discovery_intent
    {
        std::u8string scan_root {};
    };

    // 후보 행의 체크 상태 전환이다. 제외 사유가 있는 후보는 무시된다.
    struct toggle_discovery_candidate_intent
    {
        std::size_t index { 0 };
    };

    // 선택 후보의 등록 실행이다. 단계 5의 원자적 선택 등록 API로 문서를 갱신한다.
    struct confirm_discovery_intent
    {};

    struct cancel_discovery_dialog_intent
    {};

    struct discovery_dialog_scroll_intent
    {
        float delta { 0.0f };
    };

    // toolbar의 환경설정 버튼이 여는 환경설정 dialog다 (REQ-017, stage-8-plan
    // 5.1). 문서 `settings`의 Git/SVN 실행 파일 경로를 편집한다.
    struct open_settings_intent
    {};

    // 환경설정 dialog의 경로 초안 변경이다. UI thread의 파일 선택 dialog가 고른
    // 경로를 담아 보낸다. 파일 선택 dialog가 존재하는 파일만 돌려주므로 logic은
    // 형식(절대 경로)만 다시 검증한다.
    struct set_settings_executable_intent
    {
        repository_kind tool { repository_kind::git };
        std::u8string path {};
    };

    // 경로 초안을 비운다. 빈 값은 자동 탐색을 뜻한다 (REQ-017).
    struct clear_settings_executable_intent
    {
        repository_kind tool { repository_kind::git };
    };

    // 환경설정의 행 하나를 가리키는 열쇠다 (global-settings-and-ui-fixes-design
    // G3.2). `덮어씀` 배지가 어느 행의 문서 override를 지울지 담는다.
    enum class settings_override_field
    {
        git_executable,
        svn_executable,
        query_timeout,
        update_submodules,
        ignore_local_changes,
        write_log_files,
        // 외양 항목이다 (settings-tabs-and-appearance-scope-design S2.3). 초안이
        // 아니라 문서를 곧바로 고친다.
        theme,
        accent,
        // 글꼴도 같은 계층이다 (2026-08-23 지시). 문서가 덮어쓰지 않으면 앱 설정을
        // 따른다.
        ui_font,
        code_font,
    };

    // 환경설정 dialog의 탭을 바꾼다 (settings-tabs-and-appearance-scope-design
    // S1.2). dialog 상태만 바꾸며 초안은 건드리지 않는다.
    struct select_settings_tab_intent
    {
        settings_tab tab { settings_tab::tools };
    };

    // 문서 모드에서 그 행의 문서 override를 삭제해 앱 설정을 따르게 한다.
    // `덮어씀` 배지 클릭이 보낸다 (2026-08-22 지시).
    struct clear_settings_override_intent
    {
        settings_override_field field { settings_override_field::git_executable };
    };

    // 환경설정 항목 열의 세로 스크롤이다 (text-input-and-font-fallback-design E1).
    // 창이 낮아 항목이 다 들어가지 않을 때만 움직인다.
    struct settings_scroll_intent
    {
        float delta { 0.0f };
    };

    // 초점을 받을 수 있는 텍스트 박스다 (switch-dialog-query-bar-design R2.2).
    // 편집 intent 하나가 대상을 담고 다니므로 박스가 늘어도 intent는 늘지 않는다.
    //
    // 값은 luil의 불투명 `text_input_target`(uint32)에 그대로 실린다 — 0은 luil의
    // `none`과 겹치므로 1부터 매긴다. 변환은 gitman_policy 한 곳에서만 한다 (설계 L4).
    enum class text_input_target : std::uint32_t
    {
        none = 0,
        // 환경설정의 상태 확인 제한 시간이다. 숫자 4자리만 받는다
        // (field-feedback-design 1.3). 빈 초안은 기본값을 뜻한다.
        settings_timeout = 1,
        // switch dialog의 질의 막대다. Git은 필터, SVN은 이동할 경로다.
        switch_query,
        // 글꼴 선택 dialog의 이름 필터다 (fonts-and-in-app-dialogs-design F2.5).
        font_filter,
        // 새 문서 dialog의 문서 이름 칸이다 (F3).
        generate_document_name,
    };

    // 편집 명령의 어휘는 luil의 것이다 (`domain/text_edit.h`가 전달한다). 적용은
    // logic이 지금처럼 `text_edit_*` 함수로 한다.

    // IME 조합 상태를 UI thread가 알린다 (text-input-and-font-fallback-design E2.3).
    // TSF는 문서를 동기로 읽고 쓰므로 초점을 가진 박스의 그림자 문서를 UI thread가
    // 갖는다. 그 그림자가 바뀔 때마다 이 intent가 온다.
    //
    // 조합 글은 아직 사용자의 글이 아니라 `text_edit_state`에 넣지 않는다 — 실행
    // 취소 기록이 조합 단계마다 더럽혀진다. 확정되면 평범한 `edit_text_input_intent`
    // (insert)가 따로 온다.
    struct set_text_composition_intent
    {
        text_input_target target { text_input_target::settings_timeout };
        // 조합 중 화면에 보이는 글 전체다 (확정된 글 + 조합 글).
        std::u8string text {};
        std::size_t caret { 0 };
        // `text` 안에서 밑줄을 그을 구간이다.
        std::size_t composing_begin { 0 };
        std::size_t composing_end { 0 };
        // false면 조합이 끝났다는 뜻이라 상태를 버린다.
        bool composing { false };
    };

    struct edit_text_input_intent
    {
        text_input_target target { text_input_target::settings_timeout };
        text_edit_command command { text_edit_command::insert };
        std::u8string text {};
        std::size_t offset { 0 };
        bool extend { false };
    };

    // 환경설정의 "업데이트 시 submodule 갱신" 토글이다 (2026-08-20 검수: update
    // 확인 overlay 대신 문서 settings가 정한다).
    struct toggle_settings_submodules_intent
    {};

    // 환경설정의 "로컬 변경을 상관하지 않음" 토글이다. status 순회를 건너뛰고
    // 작업 트리를 깨끗하다고 믿는다 (대형 저장소 대응).
    struct toggle_settings_ignore_local_intent
    {};

    // 환경설정의 "로그를 문서 폴더에 파일로 남김" 토글이다 (app-shell-design A4.5).
    struct toggle_settings_log_files_intent
    {};

    struct confirm_settings_intent
    {};

    struct cancel_settings_dialog_intent
    {};

    // 카드의 로컬 변경 확인 dialog를 연다 (field-feedback-design 2.3). 카드 body
    // 더블 클릭과 (F5) 컨텍스트 메뉴가 보낸다.
    struct open_local_changes_intent
    {
        project_id id {};
    };

    // 목록 행 선택이다. 선택된 항목의 diff 조회가 lazy로 제출된다.
    struct select_local_change_intent
    {
        std::size_t index { 0 };
    };

    struct cancel_local_changes_dialog_intent
    {};

    // 상단 목록과 하단 diff pane의 스크롤이다 (논리 픽셀).
    struct local_changes_scroll_intent
    {
        float delta { 0.0f };
    };

    struct local_changes_diff_scroll_intent
    {
        float delta { 0.0f };
    };

    // 카드 body 우클릭이 여는 컨텍스트 메뉴다 (field-feedback-design 3장). 좌표는
    // 우클릭 지점의 창 좌표(물리 픽셀) 앵커다. logic은 카드를 선택 카드로 만든 뒤
    // 메뉴를 연다.
    struct open_context_menu_intent
    {
        project_id id {};
        float anchor_x { 0.0f };
        float anchor_y { 0.0f };
    };

    // 카드의 `⋯`와 `Alt+↓`이 여는 기능 메뉴다 (function-buttons-design 2.3.2). 좌표는 카드 메뉴와 같은 창 좌표
    // (물리 픽셀) 앵커이고 `⋯` 버튼의 왼쪽 아래다. 카드 안 버튼이라 카드 메뉴와 달리 선택을 바꾸지 않는다
    // (2.2.6).
    struct open_function_menu_intent
    {
        project_id id {};
        float anchor_x { 0.0f };
        float anchor_y { 0.0f };
    };

    // `Alt+1..3`이다 (2.3.4). `number`는 문서 전체의 보이는 `card` 배치 순서(1부터)이고, 버튼 id는 logic이
    // 정한다 — policy는 문서를 모른다. 그 기능이 카드의 범위 밖이거나 이미 실행 중이면 아무것도 하지 않는다.
    struct run_function_shortcut_intent
    {
        project_id id {};
        std::size_t number { 0 };
    };

    // 배너(도구 막대) 우클릭이 여는 문서 컨텍스트 메뉴다
    // (theme-and-banner-menu-design T1). 좌표 규칙은 카드 메뉴와 같다. 열린 문서가
    // 없으면 logic이 메뉴를 열지 않는다.
    struct open_document_context_menu_intent
    {
        float anchor_x { 0.0f };
        float anchor_y { 0.0f };
    };

    // 환경설정의 외양 행이 보내는 값이다 (theme-and-banner-menu-design T3.3).
    // 저장·취소 초안을 거치지 않고 곧바로 앱 설정에 반영·저장한다 — 색은 눌러 보고
    // 고르는 항목이라 미리 보기가 곧 값이어야 한다.
    struct set_theme_preference_intent
    {
        theme_preference theme { theme_preference::system };
    };

    struct set_accent_intent
    {
        std::u8string accent_id {};
    };

    // 바깥 클릭·Esc·항목 실행이 보내는 메뉴 닫기다.
    struct close_context_menu_intent
    {};

    // popup 메뉴의 항목 선택이다 (luil-migration-design L6). logic이 메뉴를 닫고
    // 항목의 intent를 이어서 처리한다. 외부 열기 항목은 여기로 오지 않는다 — UI
    // thread가 실행하는 앱 명령이라 메뉴의 select factory가 곧바로 낸다.
    struct select_context_menu_item_intent
    {
        context_menu_entry entry { context_menu_entry::refresh };
        // 고른 항목의 `context_menu_item_view::argument`다 (function-buttons-design T8). logic은
        // 하위 동작과 이 값이 같은 항목을 지금 메뉴에서 다시 찾아 처리한다.
        std::u8string argument {};
    };

    // 열린 문서를 명시적으로 닫는다 (2026-08-21 사용자 지시). 문서 상태와 카드를
    // 버리고 시작 페이지로 돌아간다. 최근 목록은 그대로 둔다.
    struct close_document_intent
    {};

    // UI thread가 곧바로 수행한 작업(파일 연결 등록·제거)의 결과를 앱 스타일
    // 다이얼로그로 알린다 (app-shell-design A3.2).
    struct show_notice_intent
    {
        std::u8string title {};
        std::vector<std::u8string> lines {};
        bool error { false };
    };

    // 확인 버튼·Esc·배경 클릭이 보내는 알림 닫기다.
    struct dismiss_notice_intent
    {};

    // 시작 페이지 최근 항목의 제거다 (app-shell-design A1.3). 경로가 목록에 없으면
    // 아무 일도 하지 않는다.
    struct remove_recent_document_intent
    {
        std::u8string path {};
    };

    // 시작 페이지 최근 항목의 고정 토글이다 (2026-08-23 지시). 고정한 항목은 목록
    // 앞에 모이고 상한에 걸려도 버려지지 않는다. 고정을 풀면 가장 최근에 연 문서로
    // 쳐서 비고정 무리의 맨 앞에 선다.
    struct toggle_recent_document_pin_intent
    {
        std::u8string path {};
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

    // logic thread가 시작된 뒤 delegate가 host를 묶고 처음 보내는 메시지다 (설계
    // L3). 앱 단위 설정 읽기 제출이 여기서 일어나 relay가 host를 알기 전에 결과가
    // 도착하는 창이 없다.
    struct startup_intent
    {};

    // worker 결과 relay의 깨우기다. 내용은 relay 큐에 있고 driver가 `handle` 첫
    // 줄에서 비운다. logic은 이 메시지 자체로는 아무 일도 하지 않는다.
    struct relay_drain_intent
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
        // Git 후보 또는 SVN repo-browser 초기 정보 조회다. switch dialog가 요청한다.
        query_switch_candidates,
        // SVN repo-browser에서 펼친 디렉터리 하나의 자식 목록 조회다 (F6).
        query_svn_directory,
        // 로컬 변경 확인 dialog의 목록 조회다 (field-feedback-design 2.3).
        query_local_changes,
        // 목록에서 고른 항목 하나의 diff(미추적은 파일 내용) 조회다.
        query_file_diff,
        // 깊이 1 자식 탐색이다. 탐색 dialog가 요청하며 프로세스를 만들지 않는다
        // (REQ-004, 단계 8). generate처럼 문서 lane에서 실행한다.
        discover_projects,
        // 선택 후보의 원자적 등록이다. save와 같은 lane에서 직렬화된다 (단계 8).
        register_projects,
        // 앱 단위 설정 파일의 읽기·쓰기다 (app-shell-design A1.2). 파일 경로는
        // executor가 알고 있어 요청에는 담지 않는다. 문서 lane에서 직렬화된다.
        load_app_settings,
        save_app_settings,
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
        // query_svn_directory 전용: 브라우저 루트와 조회할 노드 URL이다. executor가
        // 루트 밖 요청을 process 생성 전에 거부한다.
        std::u8string svn_repository_root_url {};
        std::u8string svn_directory_url {};
        // query_file_diff 전용: 목록에서 고른 항목이다.
        std::optional<local_change_entry> diff_target {};
        // register_projects 전용: 사용자가 선택한 탐색 후보다. 문서와 revision은
        // save처럼 `document`·`revision` 필드에 실린다.
        std::vector<discovery_candidate> discovery_selection {};
        // save_app_settings 전용: 저장할 앱 설정과 마지막으로 읽은 원본 JSON이다.
        // 원본은 알 수 없는 키를 보존하는 데 쓴다.
        std::optional<app_settings> app_settings_payload {};
        std::u8string app_settings_shadow {};
        process_cancellation_token token {};
    };

    // 기능 버튼 실행 요청이다 (function-buttons-design 4.5). `operation_request`와
    // `operation_kind`를 쓰지 않고 기능 전용 실행 풀에 직접 제출한다. 그래서 기능 실행이
    // scheduler와 VCS executor로 샐 길이 형식 수준에서 없다. 치환·허용 목록·cmd 방어는
    // logic이 마쳤고, 실행 쪽은 다시 치환하지 않는다.
    struct function_run_request
    {
        project_id id {};
        // logic이 발급한다. 늦게 온 event를 버리는 기준이다.
        std::uint64_t run_id { 0 };
        // 출력 수집은 `process_request`, 분리 실행은 `process_launch_request`다.
        // 모드를 따로 들지 않아 모드와 요청 형식이 어긋날 수 없다.
        std::variant<process_request, process_launch_request> command {};
        // 대기 중 취소도 이 token으로 본다. 분리 실행은 취소가 없어 빈 token이어도 된다.
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

    // Git 후보 또는 SVN repo-browser 초기 정보 조회 결과다.
    struct switch_candidates_event
    {
        std::uint64_t operation_id { 0 };
        project_id id {};
        switch_candidate_result result {};
    };

    struct svn_directory_event
    {
        std::uint64_t operation_id { 0 };
        project_id id {};
        std::u8string url {};
        svn_directory_query_result result {};
    };

    // 로컬 변경 목록 조회 결과다. 로컬 변경 확인 dialog 상태가 소비한다 (2.3).
    struct local_changes_event
    {
        std::uint64_t operation_id { 0 };
        project_id id {};
        local_changes_result result {};
    };

    // 항목 하나의 diff 조회 결과다.
    struct file_diff_event
    {
        std::uint64_t operation_id { 0 };
        project_id id {};
        file_diff_result result {};
    };

    // 깊이 1 탐색 결과다. 탐색 dialog 상태가 소비한다 (REQ-004).
    struct discovery_completed_event
    {
        std::uint64_t operation_id { 0 };
        discovery_result result {};
    };

    // 선택 등록 결과다. 성공 시 문서와 revision이 있고 logic이 활성 문서를 이것으로
    // 바꾼다 (단계 5 계약). 실패(저장 충돌 포함)는 diagnostics로 사유를 알린다.
    struct projects_registered_event
    {
        std::uint64_t operation_id { 0 };
        std::optional<workspace_document> document {};
        std::optional<workspace_revision_token> revision {};
        std::vector<diagnostic> diagnostics {};
    };

    // UI thread가 한 번 열거한 시스템 글꼴 가족 목록이다
    // (fonts-and-in-app-dialogs-design F2.4). 열거는 OS 호출이라 logic이 할 수 없다.
    // 실패하면 빈 목록이고 dialog가 안내를 띄운다.
    struct font_families_event
    {
        std::vector<std::u8string> families {};
    };

    // 앱 설정 읽기 결과다. 파일이 없으면 기본값이 실려 온다 (실패가 아니다).
    struct app_settings_loaded_event
    {
        std::uint64_t operation_id { 0 };
        app_settings settings {};
        std::u8string shadow_source_json {};
        std::vector<diagnostic> diagnostics {};
    };

    // 앱 설정 저장 결과다. 실패해도 앱 상태는 그대로 두고 진단만 알린다.
    struct app_settings_saved_event
    {
        std::uint64_t operation_id { 0 };
        bool succeeded { false };
        std::u8string shadow_source_json {};
        std::vector<diagnostic> diagnostics {};
    };

    // 출력 수집 실행이 runner를 부르기 직전에 나간다 (4.5). 경과 시간과 소요 시간의
    // 기준점이라 대기 시간이 섞이지 않는다. 분리 실행과 대기 중 취소에는 없다.
    struct function_started_event
    {
        project_id id {};
        std::uint64_t run_id { 0 };
        std::chrono::steady_clock::time_point started_at {};
    };

    // 출력 수집 실행의 출력 묶음이다. forwarder가 16건마다, 그리고 출력이 없어도
    // 250ms마다 보낸다 (4.6). 항목은 마스킹을 마친 값이다.
    struct function_log_event
    {
        project_id id {};
        std::uint64_t run_id { 0 };
        std::vector<operation_log_entry> entries {};
    };

    enum class function_run_outcome
    {
        // 출력 수집 실행이 끝났다. 종료 코드가 있다.
        exited,
        timed_out,
        // 실행 중 취소와 대기 중 취소를 함께 뜻한다.
        cancelled,
        start_failed,
        // 요청 값이 계약을 어겼거나 대기·수집이 실패해 결과를 믿을 수 없다.
        failed,
        // 분리 실행이 시작됐다. 종료 코드와 소요 시간이 없다.
        launched,
    };

    // 기능 실행 하나의 마지막 event다. 어떤 경로에서도 반드시 한 번 나가야 logic의
    // `function_runs`에서 그 항목이 빠진다 (4.5).
    struct function_completed_event
    {
        project_id id {};
        std::uint64_t run_id { 0 };
        function_run_outcome outcome { function_run_outcome::failed };
        // `exited`일 때만 값이 있다.
        std::optional<std::int32_t> exit_code {};
        // `function_started_event`부터 잰 시간이다. started가 없는 결과는 0이다.
        std::chrono::milliseconds duration {};
        bool output_truncated { false };
        // runner나 launcher가 남긴 진단이다. 실패 줄의 사유가 된다.
        std::vector<diagnostic> diagnostics {};
    };

    struct shutdown_message
    {};

    // logic thread의 단일 inbox payload다 (ADR-005 topology). 도착 순서 그대로
    // 처리된다.
    using logic_message = std::variant<open_document_intent, generate_document_intent, refresh_all_intent, refresh_card_intent, select_card_intent, set_filter_intent, toggle_path_display_intent,
        reorder_card_intent, request_update_intent, request_switch_intent, cancel_operation_intent, run_function_intent, stop_function_intent, confirm_function_run_intent, cancel_function_run_intent,
        function_confirm_scroll_intent, clear_log_intent, set_log_filter_intent, set_log_auto_scroll_intent, log_scroll_intent,
        begin_switch_intent, select_switch_candidate_intent, select_svn_browser_node_intent, toggle_svn_browser_node_intent, confirm_switch_intent, cancel_switch_dialog_intent,
        switch_dialog_scroll_intent, edit_text_input_intent, set_text_composition_intent, submit_switch_query_intent, clear_switch_query_intent, refresh_switch_dialog_intent, open_font_dialog_intent, select_font_family_intent,
        confirm_font_dialog_intent, cancel_font_dialog_intent, font_dialog_scroll_intent, font_families_event, open_generate_document_intent, set_generate_scan_root_intent,
        set_generate_location_intent, toggle_generate_same_folder_intent, confirm_generate_document_intent, cancel_generate_document_intent, begin_discovery_intent, toggle_discovery_candidate_intent,
        confirm_discovery_intent, cancel_discovery_dialog_intent, discovery_dialog_scroll_intent, open_settings_intent, select_settings_tab_intent, set_settings_executable_intent,
        clear_settings_executable_intent, clear_settings_override_intent, toggle_settings_submodules_intent, toggle_settings_ignore_local_intent, toggle_settings_log_files_intent,
        confirm_settings_intent, cancel_settings_dialog_intent, settings_scroll_intent, open_local_changes_intent, select_local_change_intent, cancel_local_changes_dialog_intent, local_changes_scroll_intent,
        local_changes_diff_scroll_intent, open_context_menu_intent, open_function_menu_intent, run_function_shortcut_intent, open_document_context_menu_intent, close_context_menu_intent, select_context_menu_item_intent, set_theme_preference_intent, set_accent_intent,
        remove_recent_document_intent, toggle_recent_document_pin_intent, close_document_intent, show_notice_intent, dismiss_notice_intent, window_metrics_intent, scroll_intent, window_placement_intent, close_intent,
        startup_intent, relay_drain_intent,
        document_loaded_event, document_generated_event, query_completed_event, document_saved_event, operation_log_event, change_completed_event, switch_candidates_event, svn_directory_event,
        local_changes_event, file_diff_event, discovery_completed_event, projects_registered_event, app_settings_loaded_event, app_settings_saved_event,
        function_started_event, function_log_event, function_completed_event, shutdown_message>;

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

    // logic이 기능 실행을 전용 실행 풀로 넘기는 경계다 (function-buttons-design 4.4).
    // false는 풀이 꽉 찼거나 닫혔다는 뜻이고, logic은 자기 판정의 거절과 같게 처리한다.
    // test는 기록 대역을 주입한다.
    class function_run_submitter
    {
    public:
        function_run_submitter() = default;
        function_run_submitter(const function_run_submitter&) = delete;
        function_run_submitter(function_run_submitter&&) = delete;
        function_run_submitter& operator=(const function_run_submitter&) = delete;
        function_run_submitter& operator=(function_run_submitter&&) = delete;
        virtual ~function_run_submitter() = default;

        [[nodiscard]] virtual bool submit(function_run_request request) = 0;
    };
} // namespace gitman
