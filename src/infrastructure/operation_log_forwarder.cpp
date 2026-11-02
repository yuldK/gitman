#include "infrastructure/operation_log_forwarder.h"

#include <utility>

namespace gitman {
    operation_log_forwarder::operation_log_forwarder(batch_sink sink)
        : sink_ { std::move(sink) }
    {}

    operation_log_forwarder::operation_log_forwarder(batch_sink sink, const std::chrono::milliseconds flush_interval)
        : sink_ { std::move(sink) }
    {
        // 멤버가 모두 준비된 뒤에 thread를 시작한다. 초기화 목록에서 만들면 선언 순서에 따라
        // 아직 만들어지지 않은 mutex를 thread가 볼 수 있다.
        timer_ = std::thread { &operation_log_forwarder::timer_loop, this, flush_interval };
    }

    operation_log_forwarder::~operation_log_forwarder()
    {
        try
        {
            stop();
        }
        catch (...)
        {}
    }

    void operation_log_forwarder::on_record(const process_output_record& record)
    {
        operation_log_entry entry {};
        entry.kind = record.stream == process_stream::standard_error ? log_entry_kind::standard_error : log_entry_kind::standard_output;
        entry.severity = diagnostic_severity::information;
        entry.text = record.text;
        entry.progress = record.progress;
        entry.time = std::chrono::system_clock::now();

        const std::lock_guard<std::mutex> lock { mutex_ };
        entries_.push_back(std::move(entry));
        if (entries_.size() >= operation_log_flush_count)
            flush_locked();
    }

    void operation_log_forwarder::flush()
    {
        const std::lock_guard<std::mutex> lock { mutex_ };
        flush_locked();
    }

    void operation_log_forwarder::stop()
    {
        {
            const std::lock_guard<std::mutex> lock { timer_mutex_ };
            stopping_ = true;
        }
        timer_condition_.notify_all();
        if (timer_.joinable())
            timer_.join();
        flush();
    }

    void operation_log_forwarder::flush_locked()
    {
        if (entries_.empty())
            return;

        std::vector<operation_log_entry> batch { std::move(entries_) };
        entries_ = {};
        if (sink_ != nullptr)
            sink_(std::move(batch));
    }

    void operation_log_forwarder::timer_loop(const std::chrono::milliseconds interval) noexcept
    {
        try
        {
            std::unique_lock<std::mutex> lock { timer_mutex_ };
            while (true)
            {
                if (timer_condition_.wait_for(lock, interval, [this] { return stopping_; }))
                    return;

                // flush는 timer 잠금 밖에서 한다. `stop`이 이 잠금을 기다리며 sink 전달에
                // 묶이지 않게 하려는 것이다.
                lock.unlock();
                try
                {
                    flush();
                }
                catch (...)
                {
                    // 한 번 실패해도 주기 flush를 이어 간다. 넘기지 못한 묶음은 이미
                    // 비워졌으므로 잃는다(16건 flush의 실패와 같다).
                }
                lock.lock();
            }
        }
        catch (...)
        {
            // 잠금을 다시 잡지 못하면 timer가 끝난다. `stop`의 마지막 flush가 남은 레코드를 보낸다.
        }
    }
}
