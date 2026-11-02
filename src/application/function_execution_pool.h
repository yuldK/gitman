#pragma once

#include "application/app_messages.h"
#include "application/function_executor.h"

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace gitman {
    // 동시에 실행할 수 있는 기능의 수다 (function-buttons-design 4.4, D5).
    inline constexpr std::size_t function_execution_capacity { 4 };

    // 기능 전용 실행 풀이다. scheduler lane을 쓰면 긴 스크립트가 해시가 같은 다른 카드와
    // 0번 lane의 문서 저장을 막으므로 따로 둔다 (4.4). worker 수와 상한이 같다.
    //
    // 상한은 대기열 길이가 아니라 접수했고 아직 끝나지 않은 실행 수로 센다. 꽉 차면
    // `submit`이 거절하고 대기열에 넣지 않는다. 보이지 않는 대기열을 두지 않기 위해서다.
    // 자리는 그 실행의 `function_completed_event`를 sink에 넘기기 직전에 돌려준다. 그래서
    // logic이 completed를 받은 뒤 곧바로 낸 다음 제출이 낡은 자리 수 때문에 거절되지 않는다.
    class function_execution_pool final : public function_run_submitter
    {
    public:
        // `executor`는 풀보다 오래 살아 있어야 하고, `sink`도 worker가 마지막 결과를 넘길
        // 때까지 유효해야 한다 (relay의 `post`다, 설계 L3).
        function_execution_pool(function_executor& executor, std::function<void(logic_message)> sink, std::size_t capacity = function_execution_capacity);
        ~function_execution_pool() override;

        [[nodiscard]] bool submit(function_run_request request) override;

        // 새 제출을 막고 worker를 join한다. 멱등이지만 여러 스레드가 동시에 부르면 안 된다
        // (`workers_`를 잠금 밖에서 join한다). 대기열에 남은 요청은 그대로 소진된다.
        // 종료 때는 registry가 token을 먼저 취소하므로 남은 요청은 실행 없이
        // `completed{cancelled}`로 빠르게 끝난다.
        void shutdown() noexcept;

        [[nodiscard]] std::size_t capacity() const noexcept;
        // 접수했고 아직 끝나지 않은 실행 수다 (test 관찰용).
        [[nodiscard]] std::size_t in_flight();

    private:
        void worker_loop();
        void release_slot() noexcept;

        function_executor* executor_ { nullptr };
        std::function<void(logic_message)> sink_ {};
        std::size_t capacity_ { function_execution_capacity };
        std::mutex mutex_ {};
        std::condition_variable condition_ {};
        std::deque<function_run_request> queue_ {};
        std::size_t in_flight_ { 0 };
        bool closed_ { false };
        std::vector<std::thread> workers_ {};
    };
}
