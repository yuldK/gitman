#pragma once

#include "domain/project.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    enum class repository_kind
    {
        unknown,
        git,
        subversion,
    };

    // 저장소를 조회할 수 있는 상태인지 나타낸다. Git과 SVN이 모두 설치되지 않은
    // 환경에서도 앱은 계속 동작해야 하므로, 도구 부재는 오류가 아니라 이 값으로
    // 표현하고 카드는 목록에 남은 채 동작만 비활성화된다.
    enum class repository_availability
    {
        unknown,
        ready,
        tool_unavailable,
        not_a_repository,
        // 저장소이긴 하지만 카드가 다루는 작업 트리 배치가 아니다. bare 저장소와 git
        // dir 안을 가리키는 등록 경로가 여기에 해당한다. 지원 범위 자체는 단계 5에서
        // 정하며, 단계 4는 이 상태를 잘못된 경로나 미설치와 구분해 보고만 한다.
        unsupported_layout,
        path_unavailable,
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
        // 자격 증명이 없거나 거부되어 원격 확인에 실패했다. 네트워크 자체가 끊긴
        // `offline`과 구분해야 사용자가 할 일이 달라진다.
        authentication_required,
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
        // merge, rebase, cherry-pick, revert, bisect 등 중단된 작업이 남아 있다.
        bool operation_in_progress { false };
        // 다른 Git 프로세스가 index를 잠그고 있다.
        bool has_index_lock { false };
        bool is_detached { false };

        [[nodiscard]] bool is_safe_for_change() const noexcept;
    };

    // `git submodule status`가 보고하는 항목이다. 상태 문자를 그대로 노출하지 않고
    // 의미로 옮겨 담아 parent 카드가 사전 검사에 사용한다.
    struct submodule_status
    {
        std::u8string relative_path {};
        std::u8string revision {};
        bool initialized { true };
        // 등록된 커밋과 checkout된 커밋이 다르다.
        bool revision_mismatch { false };
        bool conflicted { false };
    };

    struct repository_snapshot
    {
        project_id project {};
        repository_kind kind { repository_kind::unknown };
        repository_availability availability { repository_availability::unknown };
        std::u8string repository_root {};
        std::u8string current_reference {};
        std::u8string local_revision {};
        comparison_source comparison { comparison_source::none };
        std::u8string comparison_target {};
        remote_sync_state sync_state { remote_sync_state::unknown };
        std::uint64_t ahead_count { 0 };
        std::uint64_t behind_count { 0 };
        working_tree_summary working_tree {};
        std::vector<submodule_status> submodules {};
        std::u8string svn_repository_root {};
        std::u8string svn_repository_uuid {};
        // `svnversion`이 없는 환경에서는 판정할 수 없으므로 값이 비어 있다.
        std::optional<bool> has_switched_subtree {};
        std::optional<bool> has_mixed_revision {};
        std::optional<std::chrono::system_clock::time_point> local_checked_at {};
        std::optional<std::chrono::system_clock::time_point> remote_checked_at {};
    };

    [[nodiscard]] std::u8string_view repository_kind_name(repository_kind kind) noexcept;
    [[nodiscard]] std::u8string_view repository_availability_name(repository_availability availability) noexcept;
    [[nodiscard]] std::u8string_view comparison_source_name(comparison_source source) noexcept;
    [[nodiscard]] std::u8string_view remote_sync_state_name(remote_sync_state state) noexcept;
    [[nodiscard]] std::u8string_view working_tree_state_name(working_tree_state state) noexcept;
} // namespace gitman
