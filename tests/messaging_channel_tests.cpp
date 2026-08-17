#include "messaging/channel.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace {
    // 시각을 고정해 envelope timestamp를 결정적으로 만든다.
    messaging::channel_options fixed_clock_options(const std::size_t capacity, const messaging::overflow_policy policy = messaging::overflow_policy::reject_newest)
    {
        messaging::channel_options options {};
        options.capacity = capacity;
        options.policy = policy;
        options.clock = [] { return std::chrono::steady_clock::time_point { std::chrono::milliseconds { 777 } }; };
        return options;
    }

    struct produced
    {
        int producer { 0 };
        int index { 0 };
    };

    void close_after_delay(messaging::channel<int>& channel)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds { 50 });
        channel.close();
    }

    void post_after_delay(messaging::channel<int>& channel)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds { 50 });
        REQUIRE(channel.post(7) == messaging::post_result::posted);
    }

    void consume_until_closed(messaging::channel<produced>& channel, std::atomic<std::size_t>& consumed)
    {
        messaging::envelope<produced> received {};
        while (true)
        {
            const messaging::receive_status status { channel.receive_wait(received, std::chrono::milliseconds { 1000 }) };
            if (status == messaging::receive_status::received)
                consumed.fetch_add(1);
            else if (status == messaging::receive_status::closed)
                return;
        }
    }
} // namespace

TEST_CASE("A channel delivers messages in FIFO order with contiguous sequences", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(8) };
    REQUIRE(channel.post(10) == messaging::post_result::posted);
    REQUIRE(channel.post(20) == messaging::post_result::posted);
    REQUIRE(channel.post(30) == messaging::post_result::posted);

    messaging::envelope<int> received {};
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 10);
    REQUIRE(received.sequence == 0u);
    REQUIRE(received.enqueued_at == std::chrono::steady_clock::time_point { std::chrono::milliseconds { 777 } });

    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 20);
    REQUIRE(received.sequence == 1u);

    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.sequence == 2u);
    REQUIRE(channel.try_receive(received) == messaging::receive_status::empty);
}

TEST_CASE("A full reject_newest channel refuses the post and keeps its contents", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(2) };
    REQUIRE(channel.post(1) == messaging::post_result::posted);
    REQUIRE(channel.post(2) == messaging::post_result::posted);
    REQUIRE(channel.post(3) == messaging::post_result::channel_full);
    REQUIRE(channel.post(4) == messaging::post_result::channel_full);

    const messaging::channel_statistics statistics { channel.statistics() };
    REQUIRE(statistics.posted == 2u);
    REQUIRE(statistics.rejected == 2u);
    REQUIRE(statistics.dropped_oldest == 0u);

    messaging::envelope<int> received {};
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 1);
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 2);
}

TEST_CASE("A full drop_oldest channel drops the head and shows a sequence gap", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(2, messaging::overflow_policy::drop_oldest) };
    REQUIRE(channel.post(1) == messaging::post_result::posted);
    REQUIRE(channel.post(2) == messaging::post_result::posted);
    REQUIRE(channel.post(3) == messaging::post_result::posted_after_drop);

    const messaging::channel_statistics statistics { channel.statistics() };
    REQUIRE(statistics.posted == 3u);
    REQUIRE(statistics.dropped_oldest == 1u);

    messaging::envelope<int> received {};
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 2);
    // 버린 항목의 sequence 0이 비고 1부터 이어진다. 소비자가 구멍을 관찰할 수 있다.
    REQUIRE(received.sequence == 1u);
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 3);
    REQUIRE(received.sequence == 2u);
}

TEST_CASE("A closed channel refuses posts but drains its remaining messages", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(8) };
    REQUIRE(channel.post(1) == messaging::post_result::posted);
    REQUIRE(channel.post(2) == messaging::post_result::posted);

    channel.close();
    channel.close();
    REQUIRE(channel.closed());
    REQUIRE(channel.post(3) == messaging::post_result::channel_closed);

    messaging::envelope<int> received {};
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 1);
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload == 2);
    REQUIRE(channel.try_receive(received) == messaging::receive_status::closed);
}

TEST_CASE("The signal callback fires on the empty to non empty transition and on close", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(8) };
    int signal_count { 0 };
    channel.set_signal_callback([&signal_count] { ++signal_count; });

    REQUIRE(channel.post(1) == messaging::post_result::posted);
    REQUIRE(signal_count == 1);

    // 이미 채워진 채널의 연속 post는 다시 부르지 않는다. wake 하나로 병합된다.
    REQUIRE(channel.post(2) == messaging::post_result::posted);
    REQUIRE(channel.post(3) == messaging::post_result::posted);
    REQUIRE(signal_count == 1);

    // 소비자가 비운 뒤의 post는 다시 부른다.
    messaging::envelope<int> received {};
    while (channel.try_receive(received) == messaging::receive_status::received)
    {
    }
    REQUIRE(channel.post(4) == messaging::post_result::posted);
    REQUIRE(signal_count == 2);

    channel.close();
    REQUIRE(signal_count == 3);
}

TEST_CASE("Drain honours its bound and preserves order", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(16) };
    for (int value = 0; value < 10; ++value)
        REQUIRE(channel.post(value) == messaging::post_result::posted);

    std::vector<messaging::envelope<int>> drained {};
    REQUIRE(channel.drain(drained, 4) == 4u);
    REQUIRE(drained.size() == 4u);
    for (int value = 0; value < 4; ++value)
        REQUIRE(drained[static_cast<std::size_t>(value)].payload == value);

    REQUIRE(channel.drain(drained, 100) == 6u);
    REQUIRE(drained.size() == 10u);
    REQUIRE(drained.back().payload == 9);
}

TEST_CASE("Move only payloads travel through the channel", "[messaging][channel]")
{
    messaging::channel<std::unique_ptr<int>> channel { messaging::channel_options { 4, messaging::overflow_policy::reject_newest, {} } };
    REQUIRE(channel.post(std::make_unique<int>(42)) == messaging::post_result::posted);

    messaging::envelope<std::unique_ptr<int>> received {};
    REQUIRE(channel.try_receive(received) == messaging::receive_status::received);
    REQUIRE(received.payload != nullptr);
    REQUIRE(*received.payload == 42);
}

TEST_CASE("receive_wait times out on an empty channel and wakes on close", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(4) };

    messaging::envelope<int> received {};
    REQUIRE(channel.receive_wait(received, std::chrono::milliseconds { 30 }) == messaging::receive_status::timed_out);

    // 다른 스레드의 close가 대기를 깨운다.
    std::thread closer { &close_after_delay, std::ref(channel) };
    REQUIRE(channel.receive_wait(received, std::chrono::milliseconds { 5000 }) == messaging::receive_status::closed);
    closer.join();
}

TEST_CASE("receive_wait wakes when another thread posts", "[messaging][channel]")
{
    messaging::channel<int> channel { fixed_clock_options(4) };
    std::thread producer { &post_after_delay, std::ref(channel) };

    messaging::envelope<int> received {};
    REQUIRE(channel.receive_wait(received, std::chrono::milliseconds { 5000 }) == messaging::receive_status::received);
    REQUIRE(received.payload == 7);
    producer.join();
}

TEST_CASE("Multiple producers keep their per producer order", "[messaging][channel]")
{
    constexpr int producer_count { 4 };
    constexpr int per_producer { 500 };
    messaging::channel<produced> channel { messaging::channel_options { producer_count * per_producer, messaging::overflow_policy::reject_newest, {} } };

    std::vector<std::thread> producers {};
    producers.reserve(producer_count);
    for (int producer = 0; producer < producer_count; ++producer)
    {
        producers.emplace_back([&channel, producer] {
            for (int index = 0; index < per_producer; ++index)
                while (channel.post(produced { producer, index }) != messaging::post_result::posted)
                    std::this_thread::yield();
        });
    }
    for (std::thread& thread : producers)
        thread.join();

    // 전역 순서는 계약이 아니다. producer별 부분 순서와 총 개수만 단정한다.
    std::vector<int> last_index(producer_count, -1);
    std::size_t received_count { 0 };
    messaging::envelope<produced> received {};
    while (channel.try_receive(received) == messaging::receive_status::received)
    {
        ++received_count;
        REQUIRE(received.payload.index == last_index[static_cast<std::size_t>(received.payload.producer)] + 1);
        last_index[static_cast<std::size_t>(received.payload.producer)] = received.payload.index;
    }
    REQUIRE(received_count == static_cast<std::size_t>(producer_count * per_producer));
    REQUIRE(channel.statistics().posted == static_cast<std::uint64_t>(producer_count * per_producer));
}

TEST_CASE("Producers racing a close never lose accepted messages", "[messaging][channel]")
{
    constexpr int producer_count { 4 };
    messaging::channel<int> channel { messaging::channel_options { 1u << 16u, messaging::overflow_policy::reject_newest, {} } };

    std::atomic<int> accepted { 0 };
    std::atomic<bool> stop { false };
    std::vector<std::thread> producers {};
    producers.reserve(producer_count);
    for (int producer = 0; producer < producer_count; ++producer)
    {
        producers.emplace_back([&channel, &accepted, &stop] {
            for (int index = 0; stop.load() == false && index < 100000; ++index)
            {
                const messaging::post_result result { channel.post(index) };
                if (result == messaging::post_result::posted)
                    accepted.fetch_add(1);
                else if (result == messaging::post_result::channel_closed)
                    return;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
    channel.close();
    stop.store(true);
    for (std::thread& thread : producers)
        thread.join();

    // 받아들여진 메시지는 close 후에도 전부 소비된다.
    std::size_t received_count { 0 };
    messaging::envelope<int> received {};
    while (channel.try_receive(received) == messaging::receive_status::received)
        ++received_count;
    REQUIRE(received_count == static_cast<std::size_t>(accepted.load()));
    REQUIRE(channel.try_receive(received) == messaging::receive_status::closed);
}

TEST_CASE("A stressed channel accounts for every message", "[messaging][channel][stress]")
{
    constexpr int producer_count { 8 };
    constexpr int per_producer { 20000 };
    messaging::channel<produced> channel { messaging::channel_options { 4096, messaging::overflow_policy::reject_newest, {} } };

    std::atomic<std::size_t> consumed { 0 };
    std::thread consumer { &consume_until_closed, std::ref(channel), std::ref(consumed) };

    std::vector<std::thread> producers {};
    producers.reserve(producer_count);
    for (int producer = 0; producer < producer_count; ++producer)
    {
        producers.emplace_back([&channel, producer] {
            for (int index = 0; index < per_producer; ++index)
                while (channel.post(produced { producer, index }) != messaging::post_result::posted)
                    std::this_thread::yield();
        });
    }
    for (std::thread& thread : producers)
        thread.join();
    channel.close();
    consumer.join();

    REQUIRE(consumed.load() == static_cast<std::size_t>(producer_count * per_producer));
    REQUIRE(channel.statistics().posted == static_cast<std::uint64_t>(producer_count * per_producer));
}
