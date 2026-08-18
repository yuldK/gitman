#pragma once

#include "domain/operation_log.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "presentation/status_presentation.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gitman {
    enum class card_sort_key
    {
        name,
        status,
        // 문서의 프로젝트 순서 그대로다. 카드 drag & drop 순서 변경이 이 모드로
        // 전환한다.
        custom,
    };

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

    // 선택 카드 전용 하단 로그 뷰의 불변 표시 모델이다 (REQ-008). records는 필터를
    // 통과한 것만 담고 scroll_offset은 이미 범위 안으로 고정된 논리 픽셀 값이다.
    struct log_view_model
    {
        project_id card {};
        std::u8string title {};
        std::vector<operation_log_record> records {};
        log_stream_filter filter { log_stream_filter::all };
        bool auto_scroll { true };
        float scroll_offset { 0.0f };
        // 상한 초과로 오래된 record가 제거된 적이 있다.
        bool truncated { false };
    };

    // Git 카드의 update 확인 overlay다 (stage-7-plan 4.4). submodule option을
    // 정한 뒤 실행하며, SVN 카드는 overlay 없이 곧바로 실행한다.
    struct update_overlay_view
    {
        project_id card {};
        std::u8string title {};
        bool update_submodules { false };
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
        std::vector<switch_candidate> candidates {};
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

    // logic thread가 게시하고 UI thread가 그대로 그리는 불변 snapshot이다 (ADR-004).
    // 렌더러와 input thread가 같은 layout을 계산할 수 있도록 창 크기와 스크롤 값을
    // 함께 담는다.
    struct view_snapshot
    {
        std::u8string document_path {};
        std::vector<card_view_model> cards {};
        std::optional<project_id> selected {};
        std::u8string filter_text {};
        card_sort_key sort { card_sort_key::name };
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
        // 값이 있으면 update 확인 overlay가 화면을 덮는다.
        std::optional<update_overlay_view> update_overlay {};
        // 값이 있으면 switch dialog가 화면을 덮는다.
        std::optional<switch_dialog_view> switch_dialog {};
        bool shutting_down { false };
    };
} // namespace gitman
