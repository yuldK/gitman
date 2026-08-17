#include "application/task_scheduler.h"

#include <chrono>
#include <utility>

namespace gitman {
    std::size_t operation_lane(const project_id& id, const std::size_t worker_count) noexcept
    {
        if (worker_count <= 1)
            return 0;

        // FNV-1a. 같은 id는 항상 같은 lane이라는 것만 중요하며 분포는 보조 목표다.
        std::uint64_t hash { 14695981039346656037ull };
        for (const char8_t value : id.value)
        {
            hash ^= static_cast<std::uint64_t>(value);
            hash *= 1099511628211ull;
        }
        return static_cast<std::size_t>(hash % worker_count);
    }

    task_scheduler::task_scheduler(operation_executor& executor, messaging::channel<logic_message>& logic_inbox, const std::size_t worker_count)
        : executor_ { &executor }
        , logic_inbox_ { &logic_inbox }
    {
        const std::size_t count { worker_count == 0 ? 1 : worker_count };
        inboxes_.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            messaging::channel_options options {};
            // scheduler가 lane당 동시 1개만 흘리는 것이 정상이라 사실상 도달하지 않는
            // 방어 상한이다 (ADR-005 설계 문서 4.1).
            options.capacity = 64;
            inboxes_.push_back(std::make_unique<messaging::channel<operation_request>>(options));
        }
        workers_.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
            workers_.emplace_back(&task_scheduler::worker_loop, this, index);
    }

    task_scheduler::~task_scheduler()
    {
        shutdown();
    }

    bool task_scheduler::submit(operation_request request)
    {
        // 문서 단위 작업(load·generate·save)은 0번 lane에서 서로 직렬화된다.
        const bool document_operation { request.kind == operation_kind::load_document || request.kind == operation_kind::generate_document || request.kind == operation_kind::save_document };
        const std::size_t lane { document_operation ? 0 : operation_lane(request.project.id, inboxes_.size()) };
        return inboxes_[lane]->post(std::move(request)) == messaging::post_result::posted;
    }

    void task_scheduler::shutdown() noexcept
    {
        if (shut_down_)
            return;
        shut_down_ = true;

        for (const std::unique_ptr<messaging::channel<operation_request>>& inbox : inboxes_)
            inbox->close();
        for (std::thread& worker : workers_)
            if (worker.joinable())
                worker.join();
    }

    std::size_t task_scheduler::worker_count() const noexcept
    {
        return inboxes_.size();
    }

    void task_scheduler::worker_loop(const std::size_t index)
    {
        messaging::channel<operation_request>& inbox { *inboxes_[index] };
        messaging::envelope<operation_request> received {};
        while (true)
        {
            const messaging::receive_status status { inbox.receive_wait(received, std::chrono::milliseconds { 250 }) };
            if (status == messaging::receive_status::closed)
                return;
            if (status != messaging::receive_status::received)
                continue;

            executor_->execute(received.payload, [this](logic_message message) { emit_to_logic(std::move(message)); });
        }
    }

    void task_scheduler::emit_to_logic(logic_message message)
    {
        // event는 버리면 안 된다. inbox가 가득 차면 짧게 물러났다 다시 시도하고,
        // 닫힌 inbox(종료 중)로 가는 event만 조용히 버린다 (ADR-005 7.3).
        while (true)
        {
            logic_message attempt { message };
            const messaging::post_result result { logic_inbox_->post(std::move(attempt)) };
            if (result != messaging::post_result::channel_full)
                return;
            std::this_thread::sleep_for(std::chrono::milliseconds { 1 });
        }
    }
} // namespace gitman
