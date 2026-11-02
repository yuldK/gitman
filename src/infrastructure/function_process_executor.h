#pragma once

#include "application/function_executor.h"
#include "application/process_launcher.h"
#include "application/process_runner.h"
#include "infrastructure/operation_log_forwarder.h"

#include <chrono>

namespace gitman {
    // 기능 실행 요청을 프로세스로 옮기는 executor다 (function-buttons-design 4.1, 4.5).
    // 출력 수집은 `process_runner`, 분리 실행은 `process_launcher`가 한다. 요청은 logic이
    // `function_execution_policy`로 이미 조립했으므로 여기서는 다시 치환하거나 검사하지 않는다.
    class function_process_executor final : public function_executor
    {
    public:
        // `runner`와 `launcher`는 executor보다 오래 살아 있어야 한다. 둘 다 여러 스레드의
        // 동시 호출을 허용하므로 풀의 worker들이 함께 쓴다. `log_flush_interval`은 test가 줄인다.
        function_process_executor(process_runner& runner, process_launcher& launcher, std::chrono::milliseconds log_flush_interval = operation_log_flush_interval) noexcept;

        void execute(const function_run_request& request, const std::function<void(logic_message)>& emit) noexcept override;

    private:
        void execute_capture(const function_run_request& request, const process_request& command, const std::function<void(logic_message)>& emit);
        void execute_detached(const function_run_request& request, const process_launch_request& command, const std::function<void(logic_message)>& emit);

        process_runner* runner_ { nullptr };
        process_launcher* launcher_ { nullptr };
        std::chrono::milliseconds log_flush_interval_ { operation_log_flush_interval };
    };
}
