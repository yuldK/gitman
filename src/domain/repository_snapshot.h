#pragma once

#include "domain/project.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gitman {
    enum class repository_kind
    {
        unknown,
        git,
        subversion,
    };

    enum class comparison_source
    {
        none,
        remote,
        local,
    };

    enum class remote_sync_state
    {
        unknown,
        up_to_date,
        behind,
        ahead,
        diverged,
        local_only,
        remote_target_missing,
        offline,
        error,
    };

    enum class working_tree_state
    {
        unknown,
        clean,
        modified,
        conflicted,
    };

    struct working_tree_summary
    {
        working_tree_state state { working_tree_state::unknown };
        std::uint64_t modified_count { 0 };
        std::uint64_t untracked_count { 0 };
        std::uint64_t conflicted_count { 0 };
    };

    struct repository_snapshot
    {
        project_id project {};
        repository_kind kind { repository_kind::unknown };
        std::u8string repository_root {};
        std::u8string current_reference {};
        std::u8string local_revision {};
        comparison_source comparison { comparison_source::none };
        std::u8string comparison_target {};
        remote_sync_state sync_state { remote_sync_state::unknown };
        std::uint64_t ahead_count { 0 };
        std::uint64_t behind_count { 0 };
        working_tree_summary working_tree {};
        std::optional<std::chrono::system_clock::time_point> local_checked_at {};
        std::optional<std::chrono::system_clock::time_point> remote_checked_at {};
    };

    [[nodiscard]] std::u8string_view repository_kind_name(repository_kind kind) noexcept;
    [[nodiscard]] std::u8string_view comparison_source_name(comparison_source source) noexcept;
    [[nodiscard]] std::u8string_view remote_sync_state_name(remote_sync_state state) noexcept;
    [[nodiscard]] std::u8string_view working_tree_state_name(working_tree_state state) noexcept;
} // namespace gitman
