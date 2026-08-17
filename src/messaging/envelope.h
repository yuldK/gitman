#pragma once

#include <chrono>
#include <cstdint>

// ADR-005의 범용 스레드 메시지 component다. 어떤 gitman 코드에도 의존하지 않으며,
// 이 디렉터리를 다른 프로젝트에 복사해도 그대로 컴파일되는 것이 분리 기준이다.
namespace messaging {
    template<typename payload_type>
    struct envelope
    {
        // 채널별 단조 증가 접수 번호다. drop_oldest 정책에서만 건너뜀이 생긴다.
        std::uint64_t sequence { 0 };
        std::chrono::steady_clock::time_point enqueued_at {};
        payload_type payload {};
    };
} // namespace messaging
