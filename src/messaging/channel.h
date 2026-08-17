#pragma once

#include "messaging/envelope.h"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace messaging {
    enum class overflow_policy
    {
        // 가득 찬 채널의 post가 실패한다. 버리면 안 되는 메시지의 기본값이다.
        reject_newest,
        // 가장 오래된 항목을 버리고 받는다. 입력 이벤트처럼 최신이 더 중요한 흐름용이다.
        drop_oldest,
    };

    enum class post_result
    {
        posted,
        posted_after_drop,
        channel_full,
        channel_closed,
    };

    enum class receive_status
    {
        received,
        empty,
        timed_out,
        closed,
    };

    struct channel_options
    {
        std::size_t capacity { 1024 };
        overflow_policy policy { overflow_policy::reject_newest };
        // envelope 시각의 주입점이다. test가 고정 시계를 넣는다. 비어 있으면
        // steady_clock을 쓴다.
        std::function<std::chrono::steady_clock::time_point()> clock {};
    };

    struct channel_statistics
    {
        std::uint64_t posted { 0 };
        std::uint64_t dropped_oldest { 0 };
        std::uint64_t rejected { 0 };
        std::size_t peak_depth { 0 };
    };

    // consumer 스레드 하나가 소유하는 MPSC FIFO 채널이다 (ADR-005).
    //
    // - post()는 임의 스레드에서 호출할 수 있고 절대 블로킹하지 않는다.
    // - receive 계열은 소비 스레드 전용이다. debug build는 첫 호출의 thread id를
    //   기록하고 이후 불일치를 assert한다.
    // - close() 이후의 post는 channel_closed를 돌려주고 남은 메시지는 계속 소비할
    //   수 있다. 비어 있고 닫힌 채널의 receive는 closed를 돌려준다.
    // - signal callback은 비어 있던 채널이 채워질 때와 close에서 post/close 호출
    //   스레드로 호출된다. callback은 짧아야 하고 예외를 던지면 안 되며 채널 API를
    //   재호출하면 안 된다.
    template<typename payload_type>
    class channel
    {
    public:
        explicit channel(channel_options options)
            : options_ { std::move(options) }
        {
            if (options_.capacity == 0)
                options_.capacity = 1;
        }

        channel(const channel&) = delete;
        channel(channel&&) = delete;
        channel& operator=(const channel&) = delete;
        channel& operator=(channel&&) = delete;
        ~channel() = default;

        [[nodiscard]] post_result post(payload_type payload)
        {
            std::function<void()> signal {};
            post_result result { post_result::posted };
            {
                std::lock_guard<std::mutex> lock { mutex_ };
                if (closed_)
                    return post_result::channel_closed;

                const bool was_empty { queue_.empty() };
                if (queue_.size() >= options_.capacity)
                {
                    if (options_.policy == overflow_policy::reject_newest)
                    {
                        ++statistics_.rejected;
                        return post_result::channel_full;
                    }
                    queue_.pop_front();
                    ++statistics_.dropped_oldest;
                    result = post_result::posted_after_drop;
                }

                queue_.push_back(envelope<payload_type> {
                    next_sequence_++,
                    options_.clock ? options_.clock() : std::chrono::steady_clock::now(),
                    std::move(payload),
                });
                ++statistics_.posted;
                if (queue_.size() > statistics_.peak_depth)
                    statistics_.peak_depth = queue_.size();

                if (was_empty)
                    signal = signal_callback_;
            }

            condition_.notify_one();
            if (signal)
                signal();
            return result;
        }

        [[nodiscard]] receive_status try_receive(envelope<payload_type>& out)
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            assert_consumer_thread();
            if (queue_.empty())
                return closed_ ? receive_status::closed : receive_status::empty;

            out = std::move(queue_.front());
            queue_.pop_front();
            return receive_status::received;
        }

        [[nodiscard]] receive_status receive_wait(envelope<payload_type>& out, const std::chrono::milliseconds wait_limit)
        {
            std::unique_lock<std::mutex> lock { mutex_ };
            assert_consumer_thread();
            condition_.wait_for(lock, wait_limit, [this] { return queue_.empty() == false || closed_; });
            if (queue_.empty() == false)
            {
                out = std::move(queue_.front());
                queue_.pop_front();
                return receive_status::received;
            }
            return closed_ ? receive_status::closed : receive_status::timed_out;
        }

        // 한 번의 잠금으로 최대 max_count개를 꺼낸다. 반환값은 꺼낸 수다. 상한이 한
        // 번의 wake에서 무한히 소비하는 것을 막아 렌더링 기회를 보존한다.
        [[nodiscard]] std::size_t drain(std::vector<envelope<payload_type>>& out, const std::size_t max_count)
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            assert_consumer_thread();
            std::size_t count { 0 };
            while (count < max_count && queue_.empty() == false)
            {
                out.push_back(std::move(queue_.front()));
                queue_.pop_front();
                ++count;
            }
            return count;
        }

        void close() noexcept
        {
            std::function<void()> signal {};
            {
                std::lock_guard<std::mutex> lock { mutex_ };
                if (closed_)
                    return;
                closed_ = true;
                signal = signal_callback_;
            }
            condition_.notify_all();

            // callback 계약은 예외 금지지만 close는 noexcept이므로 방어한다.
            if (signal)
            {
                try
                {
                    signal();
                }
                catch (...)
                {}
            }
        }

        [[nodiscard]] bool closed() const noexcept
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            return closed_;
        }

        // 조립 순서는 "채널 생성 → callback 설정 → 스레드 시작"으로 고정한다.
        void set_signal_callback(std::function<void()> callback)
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            signal_callback_ = std::move(callback);
        }

        [[nodiscard]] channel_statistics statistics() const
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            return statistics_;
        }

    private:
        void assert_consumer_thread() noexcept
        {
#ifndef NDEBUG
            const std::thread::id current { std::this_thread::get_id() };
            if (consumer_thread_ == std::thread::id {})
                consumer_thread_ = current;
            else
                assert(consumer_thread_ == current && "channel consumer must stay on a single thread");
#endif
        }

        channel_options options_ {};
        mutable std::mutex mutex_ {};
        std::condition_variable condition_ {};
        std::deque<envelope<payload_type>> queue_ {};
        std::function<void()> signal_callback_ {};
        channel_statistics statistics_ {};
        std::uint64_t next_sequence_ { 0 };
        bool closed_ { false };
        std::thread::id consumer_thread_ {};
    };
} // namespace messaging
