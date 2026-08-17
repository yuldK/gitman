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

    struct workspace_document
    {
        std::int32_t schema_version { current_workspace_schema_version };
        std::u8string document_path {};
        workspace_settings settings {};
        std::vector<project_definition> projects {};
    };

    [[nodiscard]] std::optional<vcs_hint> parse_vcs_hint(std::u8string_view value) noexcept;
    [[nodiscard]] std::u8string_view vcs_hint_name(vcs_hint hint) noexcept;
    [[nodiscard]] std::u8string_view configured_path_state_name(configured_path_state state) noexcept;
} // namespace gitman
