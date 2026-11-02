#pragma once

#include "application/process_runner.h"
#include "domain/operation_log.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gitman {
    // 묶음 하나로 보내는 로그 record 수다. record마다 메시지를 만들면 대용량
    // 출력에서 logic inbox가 넘치므로 모아서 보낸다 (stage-7-plan 4.1).
    inline constexpr std::size_t operation_log_flush_count { 16 };
    // 출력이 끊긴 동안에도 모인 record를 보내는 주기다 (function-buttons-design 4.6).
    inline constexpr std::chrono::milliseconds operation_log_flush_interval { 250 };

    // runner의 출력 sink 자리에 꽂혀 프로세스 출력을 로그 항목 묶음으로 옮긴다. 묶음을
    // 어떤 event로 보낼지는 호출자가 `batch_sink`로 정한다 (VCS는 `operation_log_event`,
    // 기능 실행은 `function_log_event`).
    //
    // 주기를 주면 자기 timer thread가 레코드가 오지 않아도 그 주기마다 flush한다. runner의
    // `run`은 호출 스레드를 점유하므로 worker가 실행 중에 깨어날 수 없기 때문이다. 그래서
    // reader thread의 `on_record`와 timer flush가 함께 돈다. `entries_`와 묶음 전달은
    // forwarder 자신의 mutex 하나로 지킨다. 묶음을 잠금 안에서 넘겨야 두 flush가 엇갈려
    // 나중 묶음이 먼저 도착하는 일이 없다. `batch_sink`는 짧아야 한다(relay 게시).
    class operation_log_forwarder final : public process_output_sink
    {
    public:
        using batch_sink = std::function<void(std::vector<operation_log_entry>)>;

        // 주기 flush가 없다. 16건이 모이거나 `flush`를 부를 때만 보낸다.
        explicit operation_log_forwarder(batch_sink sink);
        // 주기 flush를 켠다. timer thread를 만들지 못하면 예외가 나간다.
        operation_log_forwarder(batch_sink sink, std::chrono::milliseconds flush_interval);
        ~operation_log_forwarder() override;

        void on_record(const process_output_record& record) override;
        void flush();
        // timer를 멈추고 join한 뒤 마지막 flush를 한다. 멱등이다. 마지막 event보다 로그가
        // 먼저 도착하도록 호출자가 마지막 event를 보내기 전에 부른다.
        void stop();

    private:
        void flush_locked();
        void timer_loop(std::chrono::milliseconds interval) noexcept;

        batch_sink sink_ {};
        std::mutex mutex_ {};
        std::vector<operation_log_entry> entries_ {};

        std::mutex timer_mutex_ {};
        std::condition_variable timer_condition_ {};
        bool stopping_ { false };
        std::thread timer_ {};
    };
}
