#pragma once

#include "application/process_cancellation.h"
#include "application/process_runner.h"
#include "application/repository_provider.h"
#include "application/switch_validation_service.h"
#include "application/vcs_file_probe.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "domain/vcs_tool.h"
#include "infrastructure/git_status_parser.h"
#include "infrastructure/vcs_execution_policy.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // Git은 진행 중인 작업을 알려 주는 기계 판독 명령을 제공하지 않는다. 표식 파일을
    // 직접 확인하는 수밖에 없으며, 계층 규칙을 지키려고 filesystem 접근은 주입받은
    // `vcs_file_probe`로만 한다.
    struct git_in_progress_markers
    {
        bool merge { false };
        bool rebase { false };
        bool cherry_pick { false };
        bool revert { false };
        bool bisect { false };
        // 다른 Git 프로세스가 index를 잠그고 있다. 진행 중 작업과 구분해 보고한다.
        bool index_locked { false };

        [[nodiscard]] bool operation_in_progress() const noexcept;
    };

    // `git_directory`는 `rev-parse --absolute-git-dir`이 보고한 절대 경로다. linked
    // worktree에서는 worktree 전용 디렉터리이며 표식 파일도 그곳에 있다.
    [[nodiscard]] git_in_progress_markers detect_git_in_progress_markers(const vcs_file_probe& probe, std::u8string_view git_directory);

    // 원격 비교 대상을 어떻게 골랐는지 나타낸다. 자동으로 고를 수 없는 경우를 구분해야
    // 카드가 사용자에게 무엇을 해야 하는지 알려 줄 수 있다.
    enum class git_remote_target_reason
    {
        // 현재 branch의 upstream을 그대로 쓴다.
        upstream,
        // 프로젝트가 지정한 `preferred_remote`를 골랐다.
        preferred_remote,
        // 관례에 따라 `origin`을 골랐다.
        origin,
        // remote가 하나뿐이라 그것을 골랐다.
        only_remote,
        // remote가 하나도 없다. 원격을 확인하지 않는다.
        no_remote,
        // remote가 여럿인데 규칙으로 좁혀지지 않는다. 자동으로 고르지 않는다.
        ambiguous_remote,
        // detached HEAD에는 비교할 branch 이름이 없다.
        detached_head,
        // 현재 branch 이름을 알 수 없다.
        no_branch,
    };

    struct git_remote_target
    {
        bool resolved { false };
        // 지정한 `preferred_remote`가 저장소에 없다. 다른 규칙으로 대상을 골랐더라도
        // 사용자가 지정한 값이 무시된 사실은 알려야 한다.
        bool preferred_remote_missing { false };
        std::u8string remote {};
        std::u8string branch {};
        // `refs/remotes/<remote>/<branch>` 형태의 완전한 ref다.
        std::u8string tracking_reference {};
        // 사용자에게 보여 줄 `<remote>/<branch>` 이름이다.
        std::u8string display_name {};
        git_remote_target_reason reason { git_remote_target_reason::no_remote };
    };

    // ADR-003의 remote-first 순서를 그대로 구현한 순수 함수다. filesystem과 프로세스를
    // 쓰지 않으므로 선택 규칙만 따로 검증할 수 있다.
    //
    // `upstream`이 remote를 가리키면 그대로 쓴다. `branch.<name>.remote = .`처럼 local
    // branch를 가리키는 upstream은 원격 비교에 쓸 수 없으므로 나머지 규칙으로 넘어간다.
    [[nodiscard]] git_remote_target select_git_remote_target(
        const std::vector<std::u8string>& remotes, std::u8string_view branch, std::u8string_view upstream, std::u8string_view preferred_remote, bool detached);

    // update 전에 저장소 상태만으로 판정할 수 있는 차단 사유다. 순수 함수라 보호 정책
    // 자체를 프로세스 없이 검증할 수 있다. 사유가 여럿이면 사용자가 먼저 해결해야 하는
    // 것을 돌려준다.
    //
    // `working_tree_state::unknown`도 차단한다. 모르는 상태에서 변경 명령을 실행하는
    // 편이 더 위험하다.
    [[nodiscard]] update_block_reason evaluate_git_update_preflight(const repository_snapshot& snapshot) noexcept;

    // submodule을 함께 갱신할 때만 사용한다. 하나라도 위험하면 parent pull을 시작하지
    // 않는다. 부분적으로 갱신된 상태가 가장 되돌리기 어렵기 때문이다.
    [[nodiscard]] update_block_reason evaluate_git_submodule_preflight(const std::vector<submodule_status>& submodules) noexcept;

    // 후보 목록을 새로 고칠 remote를 고른다. upstream은 현재 branch에 종속된 값이라
    // 후보 조회에는 쓰지 않고 `preferred_remote` → `origin` → 유일한 remote 순서만
    // 적용한다. 좁혀지지 않으면 빈 값이며 이때는 fetch하지 않고 이미 받아 둔 tracking
    // ref로만 목록을 만든다.
    [[nodiscard]] std::u8string select_git_candidate_fetch_remote(const std::vector<std::u8string>& remotes, std::u8string_view preferred_remote);

    // `for-each-ref` 결과를 전환 후보로 옮긴다. 순수 함수라 정렬과 제외 규칙을 프로세스
    // 없이 검증할 수 있다.
    //
    // - 심볼릭 ref(`refs/remotes/<remote>/HEAD`)는 후보가 아니다.
    // - 같은 branch 이름이 여러 remote에 있어도 합치지 않고 remote별 후보로 남긴다.
    // - remote 후보를 먼저, 그 뒤에 local branch를 둔다.
    // - remote 후보로 도달할 수 있는 local branch는 목록에 두 번 넣지 않는다. upstream이
    //   그 remote와 다른 local branch는 remote 후보로 도달할 수 없으므로 그대로 남는다.
    // - `refreshed_remote`가 아닌 remote의 후보는 `stale`이다. 이번 조회에서 갱신하지
    //   않았다는 사실을 카드가 표시할 수 있어야 한다.
    [[nodiscard]] std::vector<switch_candidate> build_git_switch_candidates(
        const std::vector<git_reference_entry>& references, const std::vector<std::u8string>& remotes, std::u8string_view refreshed_remote);

    // 검증에 필요한 local branch와 upstream만 뽑는다.
    [[nodiscard]] std::vector<git_local_branch_state> collect_git_local_branches(const std::vector<git_reference_entry>& references);

    // Git provider다. `S4-D2-CODE`가 로컬 조회를, `S4-D3-CODE`가 remote-first 판정을,
    // `S4-D5-CODE`가 update를, `S4-D6-CODE`가 switch를 구현했다. 계약의 모든 동작이
    // 채워졌으며 검증에 실패한 전환은 어떤 process request도 만들지 않는다.
    class git_repository_provider final : public repository_provider
    {
    public:
        // `runner`, `probe`와 `log`는 provider보다 오래 살아 있어야 한다. `log`는 카드
        // 로그로 출력을 넘길 sink이며 단계 4에서는 test 수집 sink만 사용한다.
        git_repository_provider(vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* log = nullptr, vcs_timeout_overrides timeouts = {}) noexcept;

        [[nodiscard]] const vcs_tool_info& tool() const noexcept;
        // 명시적 재조사 결과를 반영한다. 도구를 설치한 뒤 앱을 다시 시작하지 않아도
        // 카드가 다시 동작할 수 있어야 한다.
        void set_tool(vcs_tool_info tool);

        [[nodiscard]] repository_kind kind() const noexcept override;
        [[nodiscard]] bool available() const noexcept override;

        [[nodiscard]] repository_query_result query_local(const project_definition& project, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] repository_query_result query_remote(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] switch_candidate_result query_switch_candidates(const project_definition& project, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] repository_change_result update(const project_definition& project, const update_options& options, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] repository_change_result switch_to(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token) noexcept override;

    private:
        [[nodiscard]] repository_query_result query_local_impl(const project_definition& project, const process_cancellation_token& token);
        [[nodiscard]] repository_query_result query_remote_impl(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token);
        [[nodiscard]] switch_candidate_result query_switch_candidates_impl(const project_definition& project, const process_cancellation_token& token);
        [[nodiscard]] repository_change_result update_impl(const project_definition& project, const update_options& options, const process_cancellation_token& token);
        [[nodiscard]] repository_change_result switch_to_impl(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token);

        vcs_tool_info tool_ {};
        process_runner* runner_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
        process_output_sink* log_ { nullptr };
        // 문서 settings의 조회 제한 시간이다. 요청마다 builder로 전달된다 (1.3).
        vcs_timeout_overrides timeouts_ {};
    };

    // 등록 경로로 사용할 절대 경로를 고른다. 해석된 경로가 있으면 그것을, 없으면 원문을
    // 쓴다. 단계 2의 해석기는 test와 후속 조립에서 주입되므로 두 경우가 모두 있다.
    [[nodiscard]] std::u8string_view git_working_directory(const project_definition& project) noexcept;
} // namespace gitman
