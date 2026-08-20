#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    inline constexpr std::int32_t current_workspace_schema_version { 1 };
    inline constexpr std::u8string_view workspace_document_extension { u8".version-list" };

    struct project_id
    {
        std::u8string value {};

        [[nodiscard]] bool operator==(const project_id&) const noexcept = default;
    };

    enum class vcs_hint
    {
        automatic,
        git,
        subversion,
    };

    enum class configured_path_state
    {
        unchecked,
        available,
        missing,
        inaccessible,
        not_directory,
        invalid,
    };

    struct project_path
    {
        std::u8string original {};
        std::u8string normalized {};
        configured_path_state state { configured_path_state::unchecked };
    };

    struct project_definition
    {
        project_id id {};
        project_path path {};
        std::u8string display_name {};
        vcs_hint hint { vcs_hint::automatic };
        bool enabled { true };
        std::optional<std::u8string> preferred_remote {};
        // 이전 문서와의 round-trip 호환을 위해 읽고 보존한다. F6 repo-browser는 이
        // 목록을 후보나 검증 입력으로 사용하지 않는다.
        std::vector<std::u8string> svn_switch_targets {};
    };

    // 문서가 지정할 수 있는 조회 제한 시간의 허용 범위다 (field-feedback-design
    // 1.3). 벗어난 값은 파서가 경고와 함께 무시하고 기본값을 쓴다.
    inline constexpr std::int32_t minimum_query_timeout_seconds { 10 };
    inline constexpr std::int32_t maximum_query_timeout_seconds { 3600 };

    // 문서 수준 환경설정이다. `.version-list`의 optional `settings` object에 대응하며
    // 값이 없으면 전부 기본값(자동 탐색)이다. 후속 단계가 항목을 계속 추가한다.
    struct workspace_settings
    {
        // 빈 값은 "지정하지 않음"이며 자동 탐색으로 간다. 값이 있으면 절대 경로여야
        // 하고, 그 경로만 사용한다. 실행할 수 없어도 자동 탐색으로 물러서지 않는다.
        std::u8string git_executable {};
        std::u8string svn_executable {};
        // 카드의 경로를 문서가 있는 폴더 기준 상대 경로로 표시한다. toolbar 토글이
        // 바꾸고 문서에 남는다.
        bool show_relative_paths { false };
        // 업데이트 시 submodule도 함께 갱신한다(git pull --recurse-submodules=
        // on-demand). 매번 묻지 않고 이 값을 쓴다 (2026-08-20 검수). 기본 off는
        // ADR-003의 보호 정책을 따른다.
        bool update_submodules { false };
        // 상태 확인(로컬·원격 조회) 명령의 제한 시간(초)이다. 값이 없으면 실행
        // 정책의 기본값(600초)을 쓴다. 대형 저장소의 status가 5~10분 걸리는 실측을
        // 반영한 항목이다 (field-feedback-design 1장).
        std::optional<std::int32_t> query_timeout_seconds {};

        [[nodiscard]] bool operator==(const workspace_settings&) const noexcept = default;
        [[nodiscard]] bool is_default() const noexcept;
    };

    // 문서를 마지막으로 닫을 때의 창 배치다. `.version-list`의 optional `window`
    // object에 대응한다. 좌표는 `WINDOWPLACEMENT::rcNormalPosition`의 값 그대로이며
    // (물리 픽셀, 작업 영역 기준) 최대화 상태에서도 복원 크기를 담는다.
    struct window_placement
    {
        std::int32_t x { 0 };
        std::int32_t y { 0 };
        std::int32_t width { 0 };
        std::int32_t height { 0 };
        bool maximized { false };

        [[nodiscard]] bool operator==(const window_placement&) const noexcept = default;
        // 크기가 양수여야 복원에 쓸 수 있다. 위치는 모니터 구성이 바뀔 수 있어
        // 적용하는 쪽이 따로 검사한다.
        [[nodiscard]] bool valid() const noexcept;
    };

    struct workspace_document
    {
        std::int32_t schema_version { current_workspace_schema_version };
        std::u8string document_path {};
        workspace_settings settings {};
        // 값이 없으면 문서에 배치가 없거나 읽을 수 없었다는 뜻이다. 저장 시 기존
        // 문서의 `window` 필드를 지우지 않는다.
        std::optional<window_placement> window {};
        std::vector<project_definition> projects {};
    };

    [[nodiscard]] std::optional<vcs_hint> parse_vcs_hint(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view vcs_hint_name(vcs_hint hint) noexcept;
    [[nodiscard]] std::u8string_view configured_path_state_name(configured_path_state state) noexcept;
} // namespace gitman
