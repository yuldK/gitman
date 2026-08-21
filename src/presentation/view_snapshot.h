#pragma once

#include "application/svn_repository_browser.h"
#include "domain/discovery.h"
#include "domain/operation_log.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "presentation/diff_presentation.h"
#include "presentation/status_presentation.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gitman {
    // 렌더러가 그대로 그리는 카드 하나의 불변 표시 모델이다 (REQ-005).
    struct card_view_model
    {
        project_id id {};
        std::u8string display_name {};
        std::u8string path {};
        repository_kind kind { repository_kind::unknown };
        card_view_state state { card_view_state::loading };
        status_glyph status {};
        // Git은 현재 branch, SVN은 URL이다. provider가 채운 current_reference를 쓴다.
        std::u8string reference {};
        std::u8string revision {};
        std::u8string working_tree_text {};
        std::u8string comparison_target {};
        std::optional<std::chrono::system_clock::time_point> local_checked_at {};
        std::optional<std::chrono::system_clock::time_point> remote_checked_at {};
        bool busy { false };
        bool selected { false };
        bool enabled { true };
        // 변경 작업(update·switch)을 시작할 수 있는 상태다. 로컬 조회가 끝난 준비
        // 상태의 활성 카드만 참이다. 실제 보호 정책은 provider가 실행 직전에 다시
        // 검사한다 (stage-7-plan 4.4).
        bool can_change { false };
        // 변경 작업이 실행 중이다. update 버튼이 중지 버튼으로 바뀐다.
        bool change_running { false };
    };

    enum class view_empty_state
    {
        none,
        no_document,
        document_loading,
        no_projects,
        no_filter_match,
    };

    // 하단 로그 뷰의 스트림 필터다 (plan 3.9). `output`은 stderr를 제외하고,
    // `errors`는 stderr와 경고 이상 수명 주기만 남긴다.
    enum class log_stream_filter
    {
        all,
        output,
        errors,
    };

    // 로그 뷰 본문의 표시 줄 하나다 (stage-8-plan 5.3). 같은 작업의 연속된 progress
    // record는 마지막 하나로 접히고, collapsed가 이 줄로 접힌 앞선 record 수다.
    struct log_display_line
    {
        operation_log_record record {};
        std::size_t collapsed { 0 };
    };

    // 선택 카드 전용 하단 로그 뷰의 불변 표시 모델이다 (REQ-008). records는 필터를
    // 통과한 것만 담고 scroll_offset은 이미 범위 안으로 고정된 논리 픽셀 값이다.
    // lines는 records에서 progress 접기를 적용한 표시 목록이다. 렌더링과 스크롤
    // 높이는 lines를, 복사는 records를 기준으로 한다 (buffer 보존, stage-8-plan
    // 5.3).
    struct log_view_model
    {
        project_id card {};
        std::u8string title {};
        std::vector<operation_log_record> records {};
        std::vector<log_display_line> lines {};
        log_stream_filter filter { log_stream_filter::all };
        bool auto_scroll { true };
        float scroll_offset { 0.0f };
        // 상한 초과로 오래된 record가 제거된 적이 있다.
        bool truncated { false };
        // 이 카드에서 변경 작업(update·switch)이 실행 중이면 그 시작 시각이다.
        // 헤더의 경과 시간(MM:SS) 표시 기준이며, 렌더러가 draw 시각과의 차이를
        // 그린다 (실시간 갱신은 UI thread의 timer가 일으킨다).
        std::optional<std::chrono::steady_clock::time_point> change_started_at {};
    };

    // switch dialog의 불변 표시 모델이다 (REQ-007, stage-7-plan 4.5). 후보 목록은
    // provider가 만든 순서 그대로다 (remote group 먼저). confirm_label과 message는
    // logic이 검증 상태에서 계산해 UI는 그대로 그린다.
    struct switch_dialog_view
    {
        project_id card {};
        std::u8string title {};
        // 후보 조회가 아직 진행 중이다.
        bool loading { true };
        // fetch 실패로 cache된 tracking ref만으로 만든 목록이다.
        bool stale { false };
        // true이면 candidates 대신 svn_rows를 그리는 저장소 브라우저다. 초기 저장소
        // 정보 조회 중에도 true라 제목과 안내 문구가 SVN용으로 유지된다.
        bool svn_browser { false };
        std::vector<switch_candidate> candidates {};
        std::vector<svn_repository_browser_row> svn_rows {};
        std::optional<std::size_t> selected {};
        bool can_confirm { false };
        std::u8string confirm_label {};
        // 검증·거부·안내 메시지다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        // 전환 실행이 제출되어 결과를 기다리는 중이다.
        bool executing { false };
        // 후보 목록의 스크롤 위치다 (논리 픽셀, 이미 고정됨).
        float scroll_offset { 0.0f };
    };

    // 로컬 변경 확인 dialog의 목록 한 행이다 (field-feedback-design 2.3).
    struct local_change_row_view
    {
        // "수정"·"미추적" 같은 종류 배지다.
        std::u8string badge {};
        std::u8string path {};
        // 외부 열기(VSCode·탐색기)에 쓰는 절대 경로다.
        std::u8string absolute_path {};
        // 미추적 항목은 흐리게 그린다.
        bool untracked { false };
        bool directory { false };
        bool selected { false };
    };

    // 로컬 변경 확인 dialog의 불변 표시 모델이다 (field-feedback-design 2.3).
    // 상단이 변경 목록, 하단이 선택 항목의 diff viewer다. diff 줄의 색은 element가
    // 줄 첫 문자로 판정한다.
    struct local_changes_dialog_view
    {
        project_id card {};
        std::u8string title {};
        bool loading { true };
        std::vector<local_change_row_view> rows {};
        // 목록 수준 오류·안내다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        float list_scroll { 0.0f };
        bool diff_loading { false };
        // 2-way diff의 행이다 (diff_presentation::build_two_way_diff).
        std::vector<two_way_diff_row> diff_rows {};
        // 이진 파일·디렉터리·생략 같은 diff pane의 안내다.
        std::u8string diff_notice {};
        float diff_scroll { 0.0f };
    };

    // 카드 컨텍스트 메뉴의 항목 종류다 (field-feedback-design 3장). element가 이
    // 값으로 클릭 액션과 아이콘을 정한다.
    enum class context_menu_entry
    {
        open_repository,
        show_local_changes,
        refresh,
        update,
        switch_to,
    };

    struct context_menu_item_view
    {
        context_menu_entry entry { context_menu_entry::open_repository };
        std::u8string label {};
        // 진행 중 작업으로 카드 버튼이 비활성이면 해당 항목도 비활성이다.
        bool enabled { true };
    };

    // 카드 body 우클릭이 여는 컨텍스트 메뉴의 불변 표시 모델이다 (3장). anchor는
    // 우클릭 지점의 창 좌표(물리 픽셀)이며, panel이 창 밖으로 나가면 element가
    // 안쪽으로 민다.
    struct context_menu_view
    {
        project_id owner {};
        float anchor_x { 0.0f };
        float anchor_y { 0.0f };
        // "저장소 열기"가 탐색기로 여는 작업 복사본 절대 경로다.
        std::u8string repository_path {};
        std::vector<context_menu_item_view> items {};
    };

    // 확인 버튼 하나짜리 알림 다이얼로그다 (app-shell-design A3.2). 파일 연결
    // 결과처럼 UI thread가 곧바로 수행한 작업의 결과를 앱 스타일로 알린다.
    struct notice_dialog_view
    {
        std::u8string title {};
        // 본문 줄 목록이다. 실패는 진단 메시지를 줄 단위로 담는다.
        std::vector<std::u8string> lines {};
        // 실패 알림이면 아이콘과 강조 색이 오류 색이다.
        bool error { false };
    };

    // 시작 페이지의 최근 항목 한 행이다 (app-shell-design A1.3).
    struct recent_document_view
    {
        // 확장자를 뗀 문서 이름이다.
        std::u8string display_name {};
        // 문서가 있는 폴더다. 이름 뒤에 흐리게 붙는다.
        std::u8string folder {};
        // 클릭했을 때 여는 문서의 절대 경로다.
        std::u8string path {};
    };

    // 열린 문서가 없을 때 카드 목록 자리를 채우는 시작 페이지다 (VSCode 시작
    // 페이지 모방, app-shell-design A1.3). 값이 있으면 빈 상태 문구 대신 그린다.
    struct start_page_view
    {
        std::vector<recent_document_view> recents {};
        // 앱 설정 파일을 아직 읽는 중이다. 목록 자리에 안내 문구를 그린다.
        bool loading { false };
    };

    // 탐색 dialog의 후보 한 행이다. 제외 사유가 있는 후보는 체크할 수 없고 표시만
    // 된다 (stage-8-plan 5.2).
    struct discovery_row_view
    {
        discovery_candidate candidate {};
        bool checked { false };
    };

    // 탐색 후보 선택 등록 dialog의 불변 표시 모델이다 (REQ-004, stage-8-plan 5.2).
    // 행 순서는 탐색 결과의 결정적 정렬 그대로다. message와 can_confirm은 logic이
    // 계산해 UI는 그대로 그린다.
    struct discovery_dialog_view
    {
        std::u8string scan_root {};
        // 탐색이 아직 진행 중이다.
        bool loading { true };
        // 등록 실행이 제출되어 결과를 기다리는 중이다.
        bool executing { false };
        std::vector<discovery_row_view> rows {};
        // 검증·실패·안내 메시지다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        bool can_confirm { false };
        // 후보 목록의 스크롤 위치다 (논리 픽셀, 이미 고정됨).
        float scroll_offset { 0.0f };
    };

    // 환경설정 dialog의 불변 표시 모델이다 (REQ-017, stage-8-plan 5.1). 경로가
    // 비어 있으면 자동 탐색을 뜻하고 element가 안내 문구를 대신 그린다. message와
    // can_confirm은 logic이 초안 검증에서 계산해 UI는 그대로 그린다.
    struct settings_dialog_view
    {
        std::u8string git_path {};
        std::u8string svn_path {};
        // 상태 확인 제한 시간 텍스트 박스의 초안이다 (field-feedback-design 1.3).
        // 숫자만 담기며, 비어 있으면 기본값(600초)을 뜻해 element가 안내 문구를
        // 대신 그린다.
        std::u8string timeout_text {};
        // 업데이트 시 submodule 갱신 여부의 초안이다 (확인 overlay 대체).
        bool update_submodules { false };
        // 로컬 변경을 상관하지 않음 여부의 초안이다. status 순회를 건너뛴다.
        bool ignore_local_changes { false };
        // 카드 로그를 문서 폴더에 파일로 남길지의 초안이다 (app-shell-design A4.5).
        bool write_log_files { true };
        // 검증 오류다. 비어 있으면 표시하지 않는다.
        std::u8string message {};
        bool can_confirm { true };
    };

    // logic thread가 게시하고 UI thread가 그대로 그리는 불변 snapshot이다 (ADR-004).
    // 렌더러와 input thread가 같은 layout을 계산할 수 있도록 창 크기와 스크롤 값을
    // 함께 담는다.
    struct view_snapshot
    {
        std::u8string document_path {};
        std::vector<card_view_model> cards {};
        std::optional<project_id> selected {};
        std::u8string filter_text {};
        view_empty_state empty_state { view_empty_state::no_document };
        // 문서 수준 진단의 한국어 요약이다.
        std::vector<std::u8string> notices {};
        float window_width { 0.0f };
        float window_height { 0.0f };
        float scale { 1.0f };
        float scroll_offset { 0.0f };
        // 카드 경로가 문서 기준 상대 경로로 표시되고 있다. toolbar 토글의 표시
        // 상태이며, 카드의 `path`에는 이미 적용된 문자열이 들어 있다.
        bool relative_paths { false };
        // 문서에서 읽은 창 배치다. UI thread는 revision이 바뀔 때 한 번만 적용한다.
        // snapshot마다 창을 다시 배치하지 않도록 revision 비교가 경계를 만든다.
        std::optional<window_placement> window_placement_request {};
        std::uint64_t window_placement_revision { 0 };
        // `.version-list` 생성이 진행 중이다. toolbar 생성 버튼을 비활성화한다.
        bool document_generating { false };
        // 선택 카드가 있으면 그 카드의 로그 뷰다. 없으면 하단 pane을 그리지 않는다.
        std::optional<log_view_model> log {};
        // 값이 있으면 switch dialog가 화면을 덮는다.
        std::optional<switch_dialog_view> switch_dialog {};
        // 값이 있으면 환경설정 dialog가 화면을 덮는다.
        std::optional<settings_dialog_view> settings_dialog {};
        // 값이 있으면 로컬 변경 확인 dialog가 화면을 덮는다 (2.3).
        std::optional<local_changes_dialog_view> local_changes_dialog {};
        // 열린 문서가 없을 때의 시작 페이지다 (A1.3). 값이 있으면 카드 목록 자리를
        // 대신 채운다.
        std::optional<start_page_view> start_page {};
        // 값이 있으면 알림 다이얼로그가 다른 dialog 위에 떠 있다 (A3.2).
        std::optional<notice_dialog_view> notice_dialog {};
        // 값이 있으면 카드 컨텍스트 메뉴가 앵커 좌표에 떠 있다 (3장).
        std::optional<context_menu_view> context_menu {};
        // 값이 있으면 탐색 후보 선택 등록 dialog가 화면을 덮는다.
        std::optional<discovery_dialog_view> discovery_dialog {};
        bool shutting_down { false };
    };
} // namespace gitman
