#pragma once

#include "application/process_cancellation.h"
#include "application/process_runner.h"
#include "application/repository_provider.h"
#include "application/vcs_file_probe.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "domain/vcs_tool.h"

namespace gitman {
    // update 전에 작업 복사본 상태만으로 판정할 수 있는 차단 사유다.
    //
    // switched subtree와 mixed revision은 값이 있을 때만 차단한다. `svnversion`이 없는
    // 환경에서는 판정 자체가 불가능한데, 그 이유로 update를 영영 막으면 도구 구성 문제로
    // 기능이 사라진다. 대신 조회가 그 사실을 진단으로 남긴다.
    [[nodiscard]] update_block_reason evaluate_svn_update_preflight(const repository_snapshot& snapshot) noexcept;

    // SVN provider다. 구조는 Git provider와 같고 명령과 파서만 다르다. `S4-D4-CODE`가
    // 조회를, `S4-D5-CODE`가 update를 구현했다. switch는 `S4-D6` 구간에서 채운다.
    //
    // 이 호스트에는 SVN이 설치되어 있지 않다. 계획 8.4에 따라 명령 조립, 파서와 상태
    // 변환은 fake runner로 검증하고 실제 `svn.exe` 실행 경로는 미검증으로 남긴다.
    class svn_repository_provider final : public repository_provider
    {
    public:
        svn_repository_provider(vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* log = nullptr) noexcept;

        [[nodiscard]] const vcs_tool_info& tool() const noexcept;
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
        [[nodiscard]] repository_change_result update_impl(const project_definition& project, const process_cancellation_token& token);

        vcs_tool_info tool_ {};
        process_runner* runner_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
        process_output_sink* log_ { nullptr };
    };
} // namespace gitman
