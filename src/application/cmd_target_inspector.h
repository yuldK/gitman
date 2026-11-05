#pragma once

#include "application/process_cancellation.h"
#include "domain/cmd_command_line.h"

#include <chrono>

namespace gitman {
    inline constexpr std::chrono::milliseconds cmd_target_inspection_timeout { 5000 };

    enum class cmd_inspection_outcome
    {
        completed,
        cancelled,
        timed_out,
        failed,
    };

    struct cmd_inspection_result
    {
        cmd_inspection_outcome outcome { cmd_inspection_outcome::failed };
        cmd_target_facts facts {};
    };

    // 파일 열기 자체가 막혀도 호출 worker는 취소·제한 시간에 돌아와야 한다. 구현은 조회 I/O를 별도 수명으로 격리한다.
    class cmd_target_inspector
    {
    public:
        virtual ~cmd_target_inspector() = default;
        [[nodiscard]] virtual cmd_inspection_result inspect(const cmd_target_query& query, const process_cancellation_token& token) = 0;
    };
}
