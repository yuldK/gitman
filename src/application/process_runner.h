#pragma once

#include "application/process_cancellation.h"
#include "application/process_request.h"
#include "domain/process_execution.h"

namespace gitman {
    // runner가 내부 mutex로 호출을 직렬화하므로 sink 구현은 스스로 동기화하지 않아도
    // 된다. 대신 sink 안에서 오래 기다리면 자식 프로세스가 backpressure를 받는다.
    class process_output_sink
    {
    public:
        process_output_sink() = default;
        process_output_sink(const process_output_sink&) = delete;
        process_output_sink(process_output_sink&&) = delete;
        process_output_sink& operator=(const process_output_sink&) = delete;
        process_output_sink& operator=(process_output_sink&&) = delete;
        virtual ~process_output_sink() = default;

        virtual void on_record(const process_output_record& record) = 0;
    };

    // 단계 3의 실행 계약은 호출 스레드를 점유하는 동기 API 하나다. 카드별 lane,
    // 동시 실행 상한과 결과 전달 경로는 단계 6~7의 scheduler와 message 설계가 정한다.
    // 하나의 구현 instance는 여러 스레드에서 동시에 `run`을 호출해도 안전해야 한다.
    class process_runner
    {
    public:
        process_runner() = default;
        process_runner(const process_runner&) = delete;
        process_runner(process_runner&&) = delete;
        process_runner& operator=(const process_runner&) = delete;
        process_runner& operator=(process_runner&&) = delete;
        virtual ~process_runner() = default;

        // `sink`는 null일 수 있으며 그때도 출력은 계속 비우고 캡처 통계만 갱신한다.
        [[nodiscard]] virtual process_result run(const process_request& request, process_output_sink* sink, const process_cancellation_token& token) noexcept = 0;
    };
} // namespace gitman
