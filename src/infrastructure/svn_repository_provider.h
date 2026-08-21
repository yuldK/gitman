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
#include "infrastructure/vcs_execution_policy.h"

#include <string>
#include <vector>

namespace gitman {
    // update 전에 작업 복사본 상태만으로 판정할 수 있는 차단 사유다.
    //
    // switched subtree와 mixed revision은 값이 있을 때만 차단한다. `svnversion`이 없는
    // 환경에서는 판정 자체가 불가능한데, 그 이유로 update를 영영 막으면 도구 구성 문제로
    // 기능이 사라진다. 대신 조회가 그 사실을 진단으로 남긴다.
    [[nodiscard]] update_block_reason evaluate_svn_update_preflight(const repository_snapshot& snapshot) noexcept;

    // SVN provider다. 구조는 Git provider와 같고 명령과 파서만 다르다. `S4-D4-CODE`가
    // 조회를, `S4-D5-CODE`가 update를, `S4-D6-CODE`가 switch를 구현했다.
    //
    // 이 호스트에는 SVN이 설치되어 있지 않다. 계획 8.4에 따라 명령 조립, 파서와 상태
    // 변환은 fake runner로 검증하고 실제 `svn.exe` 실행 경로는 미검증으로 남긴다.
    class svn_repository_provider final : public repository_provider
    {
    public:
        svn_repository_provider(
            vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* log = nullptr, vcs_timeout_overrides timeouts = {},
            bool ignore_local_changes = false) noexcept;

        [[nodiscard]] const vcs_tool_info& tool() const noexcept;
        void set_tool(vcs_tool_info tool);

        [[nodiscard]] repository_kind kind() const noexcept override;
        [[nodiscard]] bool available() const noexcept override;

        [[nodiscard]] repository_query_result query_local(const project_definition& project, const process_cancellation_token& token) noexcept override;
        // refresh 병렬화를 위한 2단계 로컬 조회다. `query_local_metadata`는 `svn info`까지만
        // 수행해 빠르게 끝나고, `finish_local_query`가 남은 status 순회를 이어받아 작업
        // 트리 요약을 채운다. 대형 작업 복사본에서 status가 분 단위로 걸리는 동안
        // 호출자가 원격 조회를 병렬로 돌릴 수 있게 한다.
        [[nodiscard]] repository_query_result query_local_metadata(const project_definition& project, const process_cancellation_token& token) noexcept;
        void finish_local_query(repository_query_result& result, const project_definition& project, const process_cancellation_token& token) noexcept;
        [[nodiscard]] repository_query_result query_remote(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] switch_candidate_result query_switch_candidates(const project_definition& project, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] svn_directory_query_result query_directory(
            const project_definition& project, std::u8string_view repository_root_url, std::u8string_view url, const process_cancellation_token& token) noexcept;
        [[nodiscard]] local_changes_result query_local_changes(const project_definition& project, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] file_diff_result query_file_diff(const project_definition& project, const local_change_entry& entry, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] repository_change_result update(const project_definition& project, const update_options& options, const process_cancellation_token& token) noexcept override;
        [[nodiscard]] repository_change_result switch_to(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token) noexcept override;

    private:
        // `include_revision_scan`이 참이면 `svnversion`으로 mixed revision과 switched를
        // 판정한다. 이 검사는 작업 복사본 전체를 한 번 더 걷으므로 update·switch
        // 직전 검증에서만 켜고 일반 조회에서는 끈다.
        [[nodiscard]] repository_query_result query_local_impl(const project_definition& project, const process_cancellation_token& token, bool include_revision_scan);
        [[nodiscard]] repository_query_result query_local_metadata_impl(const project_definition& project, const process_cancellation_token& token);
        void finish_local_query_impl(repository_query_result& result, const project_definition& project, const process_cancellation_token& token, bool include_revision_scan);
        [[nodiscard]] repository_query_result query_remote_impl(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token);
        [[nodiscard]] repository_change_result update_impl(const project_definition& project, const process_cancellation_token& token);
        [[nodiscard]] repository_change_result switch_to_impl(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token);

        vcs_tool_info tool_ {};
        process_runner* runner_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
        process_output_sink* log_ { nullptr };
        // 문서 settings의 조회 제한 시간이다. 요청마다 builder로 전달된다 (1.3).
        vcs_timeout_overrides timeouts_ {};
        // 로컬 변경을 상관하지 않는 설정이다. 참이면 status 순회를 건너뛰고 작업
        // 트리를 깨끗하다고 믿으며, update·switch의 실패·충돌은 사후에만 알린다.
        // update·switch 직전의 svnversion 검증(mixed revision·switched 차단)은
        // 로컬 변경이 아니라서 이 설정과 무관하게 수행한다.
        bool ignore_local_changes_ { false };
    };
} // namespace gitman
