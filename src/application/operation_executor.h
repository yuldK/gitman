#pragma once

#include "application/app_messages.h"

#include <functional>

namespace gitman {
    // worker thread가 request 하나를 동기로 수행하는 경계다. scheduler의 스레드·lane
    // 정책과 실제 VCS 실행을 분리해, scheduler를 프로세스 없이 결정적으로 검증할 수
    // 있게 한다.
    class operation_executor
    {
    public:
        operation_executor() = default;
        operation_executor(const operation_executor&) = delete;
        operation_executor(operation_executor&&) = delete;
        operation_executor& operator=(const operation_executor&) = delete;
        operation_executor& operator=(operation_executor&&) = delete;
        virtual ~operation_executor() = default;

        // 결과 event를 emit으로 보낸다. refresh는 로컬과 원격 결과를 순서대로 두 번
        // 보내며, 어떤 경로에서도 마지막 event(final_event)는 반드시 나가야 logic의
        // busy 상태가 풀린다. 예외를 던지지 않는다.
        virtual void execute(const operation_request& request, const std::function<void(logic_message)>& emit) noexcept = 0;
    };
} // namespace gitman
