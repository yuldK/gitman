#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace messaging {
    template<typename value_type>
    struct versioned
    {
        std::uint64_t version { 0 };
        value_type value {};
    };

    // 최신 값 하나만 유지하는 상태 mailbox다 (ADR-005). 게시가 이전 값을 덮어쓰므로
    // 소비자는 항상 최신만 본다. view/layout snapshot 전달용이며 값은 복사 가능해야
    // 한다. Gitman은 `std::shared_ptr<const T>`를 담는다.
    //
    // signal callback은 "소비자가 아직 가져가지 않은 새 값"이 처음 생길 때와 close에서
    // 호출된다. 연속 게시는 callback을 다시 부르지 않으므로 wake 하나로 병합된다.
    template<typename value_type>
    class latest_slot
    {
    public:
        latest_slot() = default;
        latest_slot(const latest_slot&) = delete;
        latest_slot(latest_slot&&) = delete;
        latest_slot& operator=(const latest_slot&) = delete;
        latest_slot& operator=(latest_slot&&) = delete;
        ~latest_slot() = default;

        // 새 version을 돌려준다. 닫힌 slot은 저장하지 않고 0을 돌려준다. version은
        // 1부터 시작하므로 0은 "게시되지 않음"이다.
        [[nodiscard]] std::uint64_t publish(value_type value)
        {
            std::function<void()> signal {};
            std::uint64_t version { 0 };
            {
                std::lock_guard<std::mutex> lock { mutex_ };
                if (closed_)
                    return 0;

                value_ = std::move(value);
                version = ++version_;
                has_value_ = true;
                if (pending_ == false)
                {
                    pending_ = true;
                    signal = signal_callback_;
                }
            }

            if (signal)
                signal();
            return version;
        }

        // 마지막으로 본 version보다 새 값이 있을 때만 돌려준다. 닫힌 뒤에도 남은
        // 값은 한 번 더 가져갈 수 있다 (drain 의미론).
        [[nodiscard]] std::optional<versioned<value_type>> take_newer(const std::uint64_t last_seen_version)
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            if (has_value_ == false || version_ <= last_seen_version)
                return std::nullopt;

            pending_ = false;
            return versioned<value_type> { version_, value_ };
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

        void set_signal_callback(std::function<void()> callback)
        {
            std::lock_guard<std::mutex> lock { mutex_ };
            signal_callback_ = std::move(callback);
        }

    private:
        mutable std::mutex mutex_ {};
        std::function<void()> signal_callback_ {};
        value_type value_ {};
        std::uint64_t version_ { 0 };
        bool has_value_ { false };
        bool pending_ { false };
        bool closed_ { false };
    };
} // namespace messaging
