#pragma once

#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 저장소에 있는 local branch와 그 upstream이다. 후보를 만든 시점이 아니라 검증
    // 시점의 값이어야 한다.
    struct git_local_branch_state
    {
        std::u8string name {};
        // `refs/remotes/<remote>/<branch>` 형태의 완전한 ref다. upstream이 없거나
        // 다른 local branch를 가리키면 비어 있다.
        std::u8string upstream {};
    };

    struct git_switch_context
    {
        // 검증 시점에 다시 조회한 상태다. dialog가 들고 있는 값을 쓰면 그 사이에 바뀐
        // 저장소에서 전환 명령이 나갈 수 있다.
        repository_snapshot snapshot {};
        std::vector<git_local_branch_state> local_branches {};
        // `worktree list`가 보고한, 어딘가에 checkout되어 있는 local branch 이름이다.
        // 현재 worktree의 branch도 들어 있지만 `already_on_target`이 먼저 판정한다.
        std::vector<std::u8string> checked_out_branches {};
    };

    // switch 검증은 순수 함수다. 프로세스를 만들지 않으므로 규칙 자체를 실제 저장소
    // 없이 검증할 수 있고, 호출자는 결과가 승인일 때만 명령을 만든다. 이것이 REQ-007의
    // 핵심 수용 기준이다.
    //
    // `candidates`는 검증 시점에 다시 만든 목록이며 `target`은 dialog가 고른 값이다.
    // 목록에서 찾은 후보의 값으로 판단하고, `target`에서는 사용자 확인 여부만 읽는다.
    // 후보 조회와 실행 사이에 tracking branch가 생기거나 사라졌을 수 있기 때문이다.
    //
    // 판정 순서는 `target_not_found` → `already_on_target` → `target_in_use` →
    // `working_tree_unsafe` → tracking branch 판정이다. 사용자가 먼저 해결해야 하는
    // 사유를 돌려주며, 확인 요구는 실제 차단 사유가 없을 때만 나온다.
    //
    // 진단은 프로젝트 문맥을 아는 provider가 만든다. 이 함수는 코드와 메시지만 채운다.
    [[nodiscard]] switch_validation_result validate_git_switch(const std::vector<switch_candidate>& candidates, const switch_candidate& target, const git_switch_context& context);

    // 지원하는 SVN URL 형식인지 본다. 알려진 scheme과 `://` 뒤의 비어 있지 않은 나머지만
    // 확인하며, 접근 가능 여부는 실제 조회가 판정한다.
    [[nodiscard]] bool is_supported_svn_url(std::u8string_view value) noexcept;

    // 네트워크를 쓰기 전에 판정할 수 있는 SVN 검증이다. 허용 목록은 프로젝트 문서의
    // `svn_switch_targets`이며 저장소 layout을 자동으로 가정하지 않는다.
    [[nodiscard]] switch_validation_result validate_svn_switch_target(
        const std::vector<std::u8string>& allowed_targets, const switch_candidate& target, const repository_snapshot& snapshot, std::u8string_view current_url);

    // 대상 URL을 조회해 얻은 저장소 값과 현재 작업 복사본을 대조한다. 어느 한쪽 값이라도
    // 비어 있으면 확인할 수 없으므로 통과시키지 않는다. 전환은 되돌리기 어려운 동작이라
    // 확인하지 못한 것을 안전하다고 보지 않는다.
    [[nodiscard]] switch_validation_result validate_svn_repository_identity(const repository_snapshot& snapshot, std::u8string_view target_repository_root, std::u8string_view target_repository_uuid);
} // namespace gitman
