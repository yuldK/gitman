#pragma once

#include "application/app_messages.h"
#include "application/operation_executor.h"
#include "domain/project.h"
#include "messaging/channel.h"

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace gitman {
    // 카드(lane)를 worker에 배정하는 규칙이다. 같은 카드가 항상 같은 worker의 MPSC
    // inbox로 가므로 카드별 직렬화가 FIFO 소비로 저절로 보장된다 (ADR-005의 worker
    // 분배 결정). 순수 함수라 배정 자체를 test할 수 있다.
    [[nodiscard]] std::size_t operation_lane(const project_id& id, std::size_t worker_count) noexcept;

    // worker pool과 lane 배정이다. 전체 동시 실행 상한은 worker 수와 같다
    // (단계 6 계획 4.4). 서로 다른 lane은 병렬, 같은 lane은 접수 순서로 직렬이다.
    class task_scheduler final : public operation_submitter
    {
    public:
        // `executor`와 `logic_inbox`는 scheduler보다 오래 살아 있어야 한다. 문서 단위
        // 작업(load·save)은 항상 0번 worker의 lane이라 서로 직렬화된다.
        task_scheduler(operation_executor& executor, messaging::channel<logic_message>& logic_inbox, std::size_t worker_count);
        ~task_scheduler() override;

        [[nodiscard]] bool submit(operation_request request) override;

        // worker inbox를 닫고 join한다. 멱등이다. inbox에 남은 요청은 취소된 token과
        // 함께 빠르게 소진되고, 닫힌 logic inbox로 가는 event는 조용히 버려진다.
        void shutdown() noexcept;

        [[nodiscard]] std::size_t worker_count() const noexcept;

    private:
        void worker_loop(std::size_t index);
        void emit_to_logic(logic_message message);

        operation_executor* executor_ { nullptr };
        messaging::channel<logic_message>* logic_inbox_ { nullptr };
        std::vector<std::unique_ptr<messaging::channel<operation_request>>> inboxes_ {};
        std::vector<std::thread> workers_ {};
        bool shut_down_ { false };
    };
} // namespace gitman
