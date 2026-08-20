#pragma once

#include "domain/diagnostic.h"
#include "domain/repository_snapshot.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    enum class switch_candidate_kind
    {
        git_remote_branch,
        git_local_branch,
        subversion_url,
    };

    struct switch_candidate
    {
        switch_candidate_kind kind { switch_candidate_kind::git_local_branch };
        // 사용자에게 보여 주는 이름이다. remote branch는 `origin/main` 형태다.
        std::u8string display_name {};
        // 실제 전환 대상이다. Git은 완전한 ref, SVN은 repo-browser가 조회한 URL이다.
        std::u8string target {};
        // remote branch 후보에서만 채운다. 같은 이름이 여러 remote에 있어도 합치지
        // 않고 remote별 후보로 남겨 자동 선택을 막는다.
        std::u8string remote_name {};
        // remote branch에 대응하는 기존 local branch가 있으면 그 이름이다.
        std::u8string local_branch {};
        // local tracking branch를 새로 만들어야 전환할 수 있다. 사용자 확인이 필요하다.
        bool requires_tracking_branch { false };
        // 사용자가 tracking branch 생성을 확인했다. 후보 조회는 이 값을 채우지 않으며
        // dialog가 확인을 받은 뒤에만 켠다. 이 값이 꺼져 있으면 provider는 branch를
        // 만들지 않고 `tracking_branch_confirmation_required`로 되돌려 보낸다.
        bool tracking_branch_confirmed { false };
        // fetch가 실패했거나 이번 조회에서 갱신하지 않은 remote의 tracking ref로만 만든
        // 후보다.
        bool stale { false };
    };

    enum class switch_rejection
    {
        none,
        // 후보 목록에 없는 대상이다.
        target_not_found,
        already_on_target,
        // 다른 worktree가 사용 중인 branch다.
        target_in_use,
        working_tree_unsafe,
        // remote branch를 골랐지만 대응하는 local branch가 없어 확인이 필요하다.
        tracking_branch_confirmation_required,
        // 같은 이름의 local branch가 다른 upstream을 가리킨다.
        tracking_branch_conflict,
        // 여러 remote에 같은 이름이 있어 자동으로 고를 수 없다.
        ambiguous_remote,
        // 지원하지 않는 SVN URL 형식이다.
        target_not_allowed,
        target_unreachable,
        // 저장소 root 또는 UUID가 다르다.
        repository_mismatch,
        tool_unavailable,
        // 저장소를 조회할 수 없어 검증 자체를 할 수 없다.
        repository_unavailable,
    };

    struct switch_validation_result
    {
        bool approved { false };
        switch_rejection rejection { switch_rejection::none };
        // 사용자에게 보여 줄 한국어 메시지다.
        std::u8string message {};
        // 사용자가 확인하면 tracking branch를 만들고 진행할 수 있다.
        bool requires_tracking_branch_confirmation { false };
        std::vector<diagnostic> diagnostics {};
    };

    enum class update_block_reason
    {
        none,
        tool_unavailable,
        repository_unavailable,
        working_tree_conflicted,
        working_tree_dirty,
        operation_in_progress,
        index_locked,
        detached_head,
        diverged,
        // 비교할 remote target이 없어 fast-forward 대상을 정할 수 없다.
        no_remote_target,
        submodule_unsafe,
        switched_subtree,
        mixed_revision,
    };

    struct update_options
    {
        // ADR-003에 따라 기본값은 off다.
        bool update_submodules { false };
    };

    [[nodiscard]] std::u8string_view switch_candidate_kind_name(switch_candidate_kind kind) noexcept;
    [[nodiscard]] std::u8string_view switch_rejection_name(switch_rejection rejection) noexcept;
    [[nodiscard]] std::u8string_view switch_rejection_message(switch_rejection rejection) noexcept;
    [[nodiscard]] std::u8string_view update_block_reason_name(update_block_reason reason) noexcept;
    [[nodiscard]] std::u8string_view update_block_reason_message(update_block_reason reason) noexcept;
} // namespace gitman
