#pragma once

#include "application/directory_enumerator.h"
#include "application/operation_executor.h"
#include "application/process_runner.h"
#include "application/project_path_resolver.h"
#include "application/project_store.h"
#include "application/vcs_file_probe.h"
#include "domain/vcs_tool.h"
#include "infrastructure/vcs_tool_discovery.h"

#include <functional>
#include <mutex>
#include <string>

namespace gitman {
    // 실제 VCS 실행 executor다. worker thread에서 단계 2의 store, 단계 4의 provider를
    // 동기 호출한다. 여러 worker가 공유하므로 도구 cache만 잠금으로 보호하고 나머지는
    // 호출별 지역 상태다. runner와 probe는 단계 3~4가 동시 사용을 검증했다.
    class vcs_operation_executor final : public operation_executor
    {
    public:
        vcs_operation_executor(project_store& store, process_runner& runner, const vcs_file_probe& probe, vcs_tool_environment environment) noexcept;
        // `generate_document`까지 지원하는 조립이다. 열거와 경로 해석 계약이 없는
        // 기존 조립(테스트 포함)은 위 생성자를 그대로 쓴다.
        vcs_operation_executor(project_store& store, process_runner& runner, const vcs_file_probe& probe, const directory_enumerator& enumerator, project_path_resolver& resolver,
            vcs_tool_environment environment) noexcept;

        void execute(const operation_request& request, const std::function<void(logic_message)>& emit) noexcept override;

    private:
        [[nodiscard]] vcs_tool_set tools_for(const workspace_settings& settings, const process_cancellation_token& token);
        [[nodiscard]] repository_kind decide_kind(const project_definition& project) const;
        void execute_query(const operation_request& request, const std::function<void(logic_message)>& emit);
        // update와 switch_to다. 프로세스 출력을 로그 sink로 스트리밍하고 마지막에
        // change_completed_event를 보낸다 (단계 7).
        void execute_change(const operation_request& request, const std::function<void(logic_message)>& emit);
        void execute_switch_candidates(const operation_request& request, const std::function<void(logic_message)>& emit);
        // 로컬 변경 확인 dialog의 목록·diff 조회다 (field-feedback-design 2.3).
        void execute_local_changes(const operation_request& request, const std::function<void(logic_message)>& emit);
        void execute_file_diff(const operation_request& request, const std::function<void(logic_message)>& emit);
        void execute_generate_document(const operation_request& request, const std::function<void(logic_message)>& emit);
        // 탐색 dialog의 깊이 1 탐색과 선택 등록이다 (REQ-004, 단계 8). 단계 5의
        // service를 그대로 조립한다.
        void execute_discover_projects(const operation_request& request, const std::function<void(logic_message)>& emit);
        void execute_register_projects(const operation_request& request, const std::function<void(logic_message)>& emit);

        project_store* store_ { nullptr };
        process_runner* runner_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
        const directory_enumerator* enumerator_ { nullptr };
        project_path_resolver* resolver_ { nullptr };
        vcs_tool_environment environment_ {};

        // settings가 같으면 도구 재조사를 하지 않는다. 매 조회마다 `--version`을
        // 실행하지 않기 위한 cache이며(단계 4 정책) settings가 바뀌면 무효가 된다.
        std::mutex cache_mutex_ {};
        bool cache_valid_ { false };
        std::u8string cached_git_setting_ {};
        std::u8string cached_svn_setting_ {};
        vcs_tool_set cached_tools_ {};
    };
} // namespace gitman
