#pragma once

#include "application/process_cancellation.h"
#include "application/process_runner.h"
#include "application/repository_provider.h"
#include "application/vcs_file_probe.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "domain/vcs_tool.h"

#include <string>
#include <string_view>

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

    // Git provider다. `S4-D2-CODE` 구간에서는 로컬 조회만 구현한다. 원격 판정은
    // `S4-D3`, update는 `S4-D5`, switch는 `S4-D6` 구간에서 채운다. 아직 구현하지 않은
    // 동작은 어떤 process request도 만들지 않는다.
    class git_repository_provider final : public repository_provider
    {
    public:
        // `runner`, `probe`와 `log`는 provider보다 오래 살아 있어야 한다. `log`는 카드
        // 로그로 출력을 넘길 sink이며 단계 4에서는 test 수집 sink만 사용한다.
        git_repository_provider(vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* log = nullptr) noexcept;

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

        vcs_tool_info tool_ {};
        process_runner* runner_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
        process_output_sink* log_ { nullptr };
    };

    // 등록 경로로 사용할 절대 경로를 고른다. 해석된 경로가 있으면 그것을, 없으면 원문을
    // 쓴다. 단계 2의 해석기는 test와 후속 조립에서 주입되므로 두 경우가 모두 있다.
    [[nodiscard]] std::u8string_view git_working_directory(const project_definition& project) noexcept;
} // namespace gitman
