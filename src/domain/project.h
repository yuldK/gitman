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
        std::vector<std::u8string> svn_switch_targets {};
    };

    // 문서 수준 환경설정이다. `.version-list`의 optional `settings` object에 대응하며
    // 값이 없으면 전부 기본값(자동 탐색)이다. 후속 단계가 항목을 계속 추가한다.
    struct workspace_settings
    {
        // 빈 값은 "지정하지 않음"이며 자동 탐색으로 간다. 값이 있으면 절대 경로여야
        // 하고, 그 경로만 사용한다. 실행할 수 없어도 자동 탐색으로 물러서지 않는다.
        std::u8string git_executable {};
        std::u8string svn_executable {};

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
