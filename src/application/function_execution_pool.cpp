#include "application/function_execution_pool.h"

#include <utility>
#include <variant>

namespace gitman {
    function_execution_pool::function_execution_pool(function_executor& executor, std::function<void(logic_message)> sink, const std::size_t capacity)
        : executor_ { &executor }
        , sink_ { std::move(sink) }
        , capacity_ { capacity == 0 ? 1 : capacity }
    {
        workers_.reserve(capacity_);
        try
        {
            for (std::size_t index = 0; index < capacity_; ++index)
                workers_.emplace_back(&function_execution_pool::worker_loop, this);
        }
        catch (...)
        {
            // 일부 worker만 만들어진 채 예외가 나가면 joinable thread가 남아 종료된다.
            shutdown();
            throw;
        }
    }

    function_execution_pool::~function_execution_pool()
    {
        shutdown();
    }

    bool function_execution_pool::submit(function_run_request request)
    {
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            if (closed_ || in_flight_ >= capacity_)
                return false;

            queue_.push_back(std::move(request));
            ++in_flight_;
        }
        condition_.notify_one();
        return true;
    }

    void function_execution_pool::shutdown() noexcept
    {
        {
            const std::lock_guard<std::mutex> lock { mutex_ };
            if (closed_ && workers_.empty())
                return;
            closed_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_)
        {
            if (worker.joinable())
                worker.join();
        }
        workers_.clear();
    }

    std::size_t function_execution_pool::capacity() const noexcept
    {
        return capacity_;
    }

    std::size_t function_execution_pool::in_flight()
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        return in_flight_;
    }

    void function_execution_pool::worker_loop()
    {
        while (true)
        {
            function_run_request request {};
            {
                std::unique_lock<std::mutex> lock { mutex_ };
                condition_.wait(lock, [this] { return queue_.empty() == false || closed_; });
                // 닫힌 뒤에도 남은 요청은 꺼내 실행한다. 버리면 completed가 나가지 않아
                // logic의 `function_runs`에 항목이 남는다.
                if (queue_.empty())
                    return;

                request = std::move(queue_.front());
                queue_.pop_front();
            }

            bool released { false };
            const auto emit = [this, &released](logic_message message) {
                // completed를 넘기기 전에 자리를 돌려준다. logic이 completed를 받은 순간에는
                // 이미 자리가 비어 있다.
                // 종류를 먼저 본다. 로그 event는 forwarder timer thread에서도 오므로
                // worker만 쓰는 `released`를 읽지 않게 한다.
                if (std::holds_alternative<function_completed_event>(message) && released == false)
                {
                    release_slot();
                    released = true;
                }
                if (sink_ != nullptr)
                    sink_(std::move(message));
            };
            executor_->execute(request, emit);

            // executor가 계약을 어겨 completed를 보내지 않았어도 자리는 돌려준다.
            if (released == false)
                release_slot();
        }
    }

    void function_execution_pool::release_slot() noexcept
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        if (in_flight_ > 0)
            --in_flight_;
    }
}
