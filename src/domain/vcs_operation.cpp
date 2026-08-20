#include "domain/vcs_operation.h"

namespace gitman {
    std::u8string_view switch_candidate_kind_name(const switch_candidate_kind kind) noexcept
    {
        switch (kind)
        {
        case switch_candidate_kind::git_remote_branch:
            return u8"git_remote_branch";
        case switch_candidate_kind::git_local_branch:
            return u8"git_local_branch";
        case switch_candidate_kind::subversion_url:
            return u8"subversion_url";
        }
        return u8"git_local_branch";
    }

    std::u8string_view switch_rejection_name(const switch_rejection rejection) noexcept
    {
        switch (rejection)
        {
        case switch_rejection::none:
            return u8"none";
        case switch_rejection::target_not_found:
            return u8"target_not_found";
        case switch_rejection::already_on_target:
            return u8"already_on_target";
        case switch_rejection::target_in_use:
            return u8"target_in_use";
        case switch_rejection::working_tree_unsafe:
            return u8"working_tree_unsafe";
        case switch_rejection::tracking_branch_confirmation_required:
            return u8"tracking_branch_confirmation_required";
        case switch_rejection::tracking_branch_conflict:
            return u8"tracking_branch_conflict";
        case switch_rejection::ambiguous_remote:
            return u8"ambiguous_remote";
        case switch_rejection::target_not_allowed:
            return u8"target_not_allowed";
        case switch_rejection::target_unreachable:
            return u8"target_unreachable";
        case switch_rejection::repository_mismatch:
            return u8"repository_mismatch";
        case switch_rejection::tool_unavailable:
            return u8"tool_unavailable";
        case switch_rejection::repository_unavailable:
            return u8"repository_unavailable";
        }
        return u8"none";
    }

    std::u8string_view switch_rejection_message(const switch_rejection rejection) noexcept
    {
        switch (rejection)
        {
        case switch_rejection::none:
            return u8"전환할 수 있습니다.";
        case switch_rejection::target_not_found:
            return u8"선택한 전환 대상을 찾을 수 없습니다.";
        case switch_rejection::already_on_target:
            return u8"이미 선택한 대상에 있습니다.";
        case switch_rejection::target_in_use:
            return u8"다른 worktree가 이 branch를 사용 중입니다.";
        case switch_rejection::working_tree_unsafe:
            return u8"작업 트리에 변경이나 진행 중인 작업이 있어 전환할 수 없습니다.";
        case switch_rejection::tracking_branch_confirmation_required:
            return u8"대응하는 로컬 branch가 없습니다. 새 tracking branch를 만들지 확인해야 합니다.";
        case switch_rejection::tracking_branch_conflict:
            return u8"같은 이름의 로컬 branch가 다른 upstream을 추적하고 있습니다.";
        case switch_rejection::ambiguous_remote:
            return u8"여러 remote에 같은 이름의 branch가 있어 remote를 직접 선택해야 합니다.";
        case switch_rejection::target_not_allowed:
            return u8"문서의 svn_switch_targets에 없는 URL입니다.";
        case switch_rejection::target_unreachable:
            return u8"전환 대상에 접근할 수 없습니다.";
        case switch_rejection::repository_mismatch:
            return u8"저장소 root 또는 UUID가 현재 작업 복사본과 다릅니다.";
        case switch_rejection::tool_unavailable:
            return u8"이 저장소 종류의 명령줄 도구를 사용할 수 없습니다.";
        case switch_rejection::repository_unavailable:
            return u8"저장소를 조회할 수 없어 전환하지 않았습니다.";
        }
        return u8"전환할 수 없습니다.";
    }

    std::u8string_view update_block_reason_name(const update_block_reason reason) noexcept
    {
        switch (reason)
        {
        case update_block_reason::none:
            return u8"none";
        case update_block_reason::tool_unavailable:
            return u8"tool_unavailable";
        case update_block_reason::repository_unavailable:
            return u8"repository_unavailable";
        case update_block_reason::working_tree_conflicted:
            return u8"working_tree_conflicted";
        case update_block_reason::working_tree_dirty:
            return u8"working_tree_dirty";
        case update_block_reason::operation_in_progress:
            return u8"operation_in_progress";
        case update_block_reason::index_locked:
            return u8"index_locked";
        case update_block_reason::detached_head:
            return u8"detached_head";
        case update_block_reason::diverged:
            return u8"diverged";
        case update_block_reason::no_remote_target:
            return u8"no_remote_target";
        case update_block_reason::submodule_unsafe:
            return u8"submodule_unsafe";
        case update_block_reason::switched_subtree:
            return u8"switched_subtree";
        case update_block_reason::mixed_revision:
            return u8"mixed_revision";
        }
        return u8"none";
    }

    std::u8string_view update_block_reason_message(const update_block_reason reason) noexcept
    {
        switch (reason)
        {
        case update_block_reason::none:
            return u8"갱신할 수 있습니다.";
        case update_block_reason::tool_unavailable:
            return u8"이 저장소 종류의 명령줄 도구를 사용할 수 없습니다.";
        case update_block_reason::repository_unavailable:
            return u8"저장소를 조회할 수 없어 갱신하지 않았습니다.";
        case update_block_reason::working_tree_conflicted:
            return u8"충돌이 남아 있어 갱신하지 않았습니다.";
        case update_block_reason::working_tree_dirty:
            // 미추적 파일만으로는 차단하지 않으므로(2.2) "수정"으로 정확히 말한다.
            // 확인 경로 안내는 F5에서 확정된 메뉴 명칭을 쓴다.
            return u8"커밋하지 않은 수정이 있어 갱신하지 않았습니다. 우클릭 메뉴의 \"로컬 변경 확인\"에서 내용을 볼 수 있습니다.";
        case update_block_reason::operation_in_progress:
            return u8"진행 중인 merge, rebase 또는 cherry-pick이 있어 갱신하지 않았습니다.";
        case update_block_reason::index_locked:
            return u8"다른 Git 프로세스가 저장소를 사용 중입니다.";
        case update_block_reason::detached_head:
            return u8"detached HEAD 상태에서는 갱신하지 않습니다.";
        case update_block_reason::diverged:
            return u8"로컬과 원격이 분기되어 fast-forward 갱신을 할 수 없습니다.";
        case update_block_reason::no_remote_target:
            return u8"비교할 원격 branch가 없어 갱신 대상을 정할 수 없습니다.";
        case update_block_reason::submodule_unsafe:
            return u8"submodule에 변경이나 충돌이 있어 갱신을 시작하지 않았습니다.";
        case update_block_reason::switched_subtree:
            return u8"switched된 하위 경로가 있어 갱신하지 않았습니다.";
        case update_block_reason::mixed_revision:
            return u8"작업 복사본의 리비전이 섞여 있어 갱신하지 않았습니다.";
        }
        return u8"갱신할 수 없습니다.";
    }
} // namespace gitman
