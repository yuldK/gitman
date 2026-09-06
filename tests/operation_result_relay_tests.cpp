#include "platform/win32/operation_result_relay.h"

#include "application/app_messages.h"

#include "luil/ui/ui_interaction.h"
#include "luil/win32/app_host.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    // 배출된 메시지에서 카드 id만 뽑는다. 순서 확인에 쓴다.
    [[nodiscard]] gitman::logic_message make_result(const std::u8string_view id)
    {
        gitman::query_completed_event event {};
        event.id = gitman::project_id { std::u8string { id } };
        return gitman::logic_message { std::move(event) };
    }

    [[nodiscard]] std::u8string id_of(const gitman::logic_message& message)
    {
        const auto* const event { std::get_if<gitman::query_completed_event>(&message) };
        return event != nullptr ? event->id.value : std::u8string {};
    }

    // 깨우기만 세는 최소 driver다. relay가 host를 어떻게 두드리는지가 관심사라
    // frame은 비운다.
    class waking_driver final : public luil::win32::logic_driver
    {
    public:
        void handle(luil::app_message message) override
        {
            const gitman::logic_message* const payload { message.get<gitman::logic_message>() };
            if (payload == nullptr)
                return;
            if (std::holds_alternative<gitman::relay_drain_intent>(*payload))
                wakes.fetch_add(1);
            if (std::holds_alternative<gitman::close_intent>(*payload))
                closed.store(true);
        }

        [[nodiscard]] std::shared_ptr<const luil::win32::ui_frame> make_frame() override
        {
            return std::make_shared<luil::win32::ui_frame>();
        }

        [[nodiscard]] luil::app_message make_close_message() override
        {
            return luil::app_message { gitman::logic_message { gitman::close_intent {} } };
        }

        [[nodiscard]] bool shutdown_completed() const override
        {
            return closed.load();
        }

        std::atomic<int> wakes { 0 };
        std::atomic<bool> closed { false };
    };

    template<typename condition_type>
    [[nodiscard]] bool wait_until(const condition_type& condition)
    {
        const auto deadline { std::chrono::steady_clock::now() + 5s };
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (condition())
                return true;
            std::this_thread::sleep_for(1ms);
        }
        return condition();
    }

    // 깨우기가 더 오지 않는 것을 확인하는 짧은 정지다. 유실이 아니라 "한 번뿐"을
    // 단정하는 자리에만 쓴다.
    void settle()
    {
        std::this_thread::sleep_for(60ms);
    }
} // namespace

TEST_CASE("The relay drains posted messages in arrival order", "[win32][relay]")
{
    // 큐가 정본이다. host가 없어도 결과는 남고 배출 순서는 도착 순서다.
    gitman::win32::operation_result_relay relay {};
    relay.post(make_result(u8"alpha"));
    relay.post(make_result(u8"beta"));
    relay.post(make_result(u8"gamma"));
    REQUIRE(relay.pending() == 3u);

    std::vector<std::u8string> drained {};
    relay.drain([&drained](gitman::logic_message message) { drained.push_back(id_of(message)); });

    REQUIRE(drained.size() == 3u);
    REQUIRE(drained[0] == u8"alpha");
    REQUIRE(drained[1] == u8"beta");
    REQUIRE(drained[2] == u8"gamma");
    REQUIRE(relay.pending() == 0u);

    // 비운 뒤 다시 배출하면 아무 일도 없다.
    bool called { false };
    relay.drain([&called](gitman::logic_message) { called = true; });
    REQUIRE(called == false);
}

TEST_CASE("Messages posted while the sink runs come out in the next drain", "[win32][relay]")
{
    // sink는 lock 밖에서 불린다. 그 안에서 더 들어온 결과가 사라지지 않고 다음
    // 배출에 순서대로 나온다.
    gitman::win32::operation_result_relay relay {};
    relay.post(make_result(u8"first"));

    std::vector<std::u8string> drained {};
    relay.drain([&](gitman::logic_message message) {
        drained.push_back(id_of(message));
        relay.post(make_result(u8"second"));
    });
    REQUIRE(drained.size() == 1u);
    REQUIRE(relay.pending() == 1u);

    relay.drain([&drained](gitman::logic_message message) { drained.push_back(id_of(message)); });
    REQUIRE(drained.size() == 2u);
    REQUIRE(drained[1] == u8"second");
}

TEST_CASE("Binding after early posts wakes the host exactly once", "[win32][relay]")
{
    // host가 묶이기 전에 도착한 결과가 다음 입력까지 잠들지 않게 하는 조치다.
    // 대기 중인 개수와 무관하게 깨우기는 하나다 (내용은 큐가 나른다).
    waking_driver driver {};
    luil::interaction_policy policy {};
    luil::win32::app_host host { luil::win32::app_host::config {}, driver, &policy };

    gitman::win32::operation_result_relay relay {};
    relay.post(make_result(u8"early-one"));
    relay.post(make_result(u8"early-two"));
    settle();
    REQUIRE(driver.wakes.load() == 0);

    relay.bind(host);
    REQUIRE(wait_until([&] { return driver.wakes.load() >= 1; }));
    settle();
    REQUIRE(driver.wakes.load() == 1);
    REQUIRE(relay.pending() == 2u);

    host.shutdown();
}

TEST_CASE("Binding before any post leaves the host alone until a result arrives", "[win32][relay]")
{
    // bind가 첫 post보다 먼저인 통상 경로다. 빈 큐를 두고 깨우지 않는다.
    waking_driver driver {};
    luil::interaction_policy policy {};
    luil::win32::app_host host { luil::win32::app_host::config {}, driver, &policy };

    gitman::win32::operation_result_relay relay {};
    relay.bind(host);
    settle();
    REQUIRE(driver.wakes.load() == 0);

    // 묶인 뒤의 post는 건마다 깨운다. 유실돼도 큐에 남아 다음 깨우기에서 함께
    // 배출되므로 개수 자체는 계약이 아니다 — 여기서는 결과가 닿는 것만 본다.
    relay.post(make_result(u8"late"));
    REQUIRE(wait_until([&] { return driver.wakes.load() >= 1; }));
    REQUIRE(relay.pending() == 1u);

    host.shutdown();
}

TEST_CASE("After unbind results only queue up", "[win32][relay]")
{
    // 종료의 `stop_workers`가 host를 놓은 뒤다. 늦게 끝난 worker의 결과는 큐에만
    // 쌓이고 아무도 깨우지 않는다.
    waking_driver driver {};
    luil::interaction_policy policy {};
    luil::win32::app_host host { luil::win32::app_host::config {}, driver, &policy };

    gitman::win32::operation_result_relay relay {};
    relay.bind(host);
    relay.post(make_result(u8"before-unbind"));
    REQUIRE(wait_until([&] { return driver.wakes.load() >= 1; }));

    const int wakes_before { driver.wakes.load() };
    relay.unbind();
    relay.post(make_result(u8"after-unbind"));
    relay.post(make_result(u8"after-unbind-2"));
    settle();
    REQUIRE(driver.wakes.load() == wakes_before);
    REQUIRE(relay.pending() == 3u);

    host.shutdown();
}

TEST_CASE("Concurrent posts keep every result", "[win32][relay]")
{
    // worker 여럿이 동시에 결과를 넣는 실제 모양이다. 순서는 스레드 간에는 정하지
    // 않지만 어느 것도 사라지지 않는다.
    gitman::win32::operation_result_relay relay {};
    constexpr int poster_count { 4 };
    constexpr int per_poster { 50 };

    std::vector<std::thread> posters {};
    for (int index = 0; index < poster_count; ++index)
        posters.emplace_back([&relay] {
            for (int value = 0; value < per_poster; ++value)
                relay.post(make_result(u8"card"));
        });
    for (std::thread& poster : posters)
        poster.join();

    std::size_t drained { 0 };
    relay.drain([&drained](gitman::logic_message) { ++drained; });
    REQUIRE(drained == static_cast<std::size_t>(poster_count * per_poster));
    REQUIRE(relay.pending() == 0u);
}
