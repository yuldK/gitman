#pragma once

#include "application/operation_executor.h"
#include "application/process_runner.h"
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

        void execute(const operation_request& request, const std::function<void(logic_message)>& emit) noexcept override;

    private:
        [[nodiscard]] vcs_tool_set tools_for(const workspace_settings& settings, const process_cancellation_token& token);
        [[nodiscard]] repository_kind decide_kind(const project_definition& project) const;
        void execute_query(const operation_request& request, const std::function<void(logic_message)>& emit);

        project_store* store_ { nullptr };
        process_runner* runner_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
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
