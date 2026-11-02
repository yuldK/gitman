#pragma once

#include "application/app_messages.h"

#include <functional>

namespace gitman {
    // 기능 실행 풀의 worker가 요청 하나를 동기로 수행하는 경계다 (function-buttons-design 4.5).
    // 풀의 스레드·상한 정책과 실제 프로세스 실행을 나눠, 풀을 프로세스 없이 검증할 수 있게 한다.
    class function_executor
    {
    public:
        function_executor() = default;
        function_executor(const function_executor&) = delete;
        function_executor(function_executor&&) = delete;
        function_executor& operator=(const function_executor&) = delete;
        function_executor& operator=(function_executor&&) = delete;
        virtual ~function_executor() = default;

        // 결과 event를 emit으로 보낸다. 어떤 경로에서도 마지막에 `function_completed_event`를
        // 한 번 보내야 logic의 `function_runs`에서 그 항목이 빠진다. 예외를 던지지 않는다.
        virtual void execute(const function_run_request& request, const std::function<void(logic_message)>& emit) noexcept = 0;
    };
}
