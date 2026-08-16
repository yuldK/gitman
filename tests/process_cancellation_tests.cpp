#include "application/process_cancellation.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <utility>
#include <vector>

TEST_CASE("A default token is never cancelled and ignores callbacks", "[application][process][cancellation]")
{
    const gitman::process_cancellation_token token {};
    REQUIRE_FALSE(token.cancellable());
    REQUIRE_FALSE(token.cancelled());

    int invoked { 0 };
    {
        const auto registration { token.register_callback([&invoked]() { ++invoked; }) };
        REQUIRE(invoked == 0);
    }
    REQUIRE(invoked == 0);
}

TEST_CASE("Sources observe and publish cancellation through their tokens", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    const gitman::process_cancellation_token token { source.token() };
    const gitman::process_cancellation_token copy { token };
    REQUIRE(token.cancellable());
    REQUIRE_FALSE(source.cancellation_requested());
    REQUIRE_FALSE(token.cancelled());
    REQUIRE_FALSE(copy.cancelled());

    source.request_cancellation();
    REQUIRE(source.cancellation_requested());
    REQUIRE(token.cancelled());
    REQUIRE(copy.cancelled());
}

TEST_CASE("Registered callbacks run once when cancellation is requested", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    const gitman::process_cancellation_token token { source.token() };

    int first { 0 };
    int second { 0 };
    const auto first_registration { token.register_callback([&first]() { ++first; }) };
    const auto second_registration { token.register_callback([&second]() { ++second; }) };
    REQUIRE(first == 0);
    REQUIRE(second == 0);

    source.request_cancellation();
    REQUIRE(first == 1);
    REQUIRE(second == 1);

    // 반복 요청은 이미 취소된 상태를 다시 통지하지 않는다.
    source.request_cancellation();
    REQUIRE(first == 1);
    REQUIRE(second == 1);
}

TEST_CASE("Callbacks registered after cancellation run immediately", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    source.request_cancellation();

    int invoked { 0 };
    const auto registration { source.token().register_callback([&invoked]() { ++invoked; }) };
    REQUIRE(invoked == 1);

    source.request_cancellation();
    REQUIRE(invoked == 1);
}

TEST_CASE("Released registrations no longer receive cancellation", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    const gitman::process_cancellation_token token { source.token() };

    int scoped { 0 };
    int explicit_release { 0 };
    {
        const auto registration { token.register_callback([&scoped]() { ++scoped; }) };
    }
    auto releasable { token.register_callback([&explicit_release]() { ++explicit_release; }) };
    releasable.release();
    releasable.release();

    source.request_cancellation();
    REQUIRE(scoped == 0);
    REQUIRE(explicit_release == 0);
}

TEST_CASE("Moving a registration transfers exactly one subscription", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    const gitman::process_cancellation_token token { source.token() };

    int moved { 0 };
    int replaced { 0 };
    auto original { token.register_callback([&moved]() { ++moved; }) };
    gitman::process_cancellation_registration target { std::move(original) };
    auto replacement { token.register_callback([&replaced]() { ++replaced; }) };
    // 대입 대상의 기존 구독은 해제되고 새 구독만 남는다.
    replacement = std::move(target);

    source.request_cancellation();
    REQUIRE(moved == 1);
    REQUIRE(replaced == 0);
}

TEST_CASE("A moved from source stops publishing cancellation", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    const gitman::process_cancellation_token token { source.token() };
    const gitman::process_cancellation_source moved { std::move(source) };

    REQUIRE_FALSE(source.cancellation_requested());
    REQUIRE_FALSE(source.token().cancellable());
    source.request_cancellation();
    REQUIRE_FALSE(token.cancelled());

    REQUIRE(moved.token().cancellable());
}

TEST_CASE("Tokens stay valid after their source is destroyed", "[application][process][cancellation]")
{
    gitman::process_cancellation_token token {};
    {
        gitman::process_cancellation_source source {};
        token = source.token();
        REQUIRE(token.cancellable());
    }

    // token이 상태를 살려 두므로 접근은 안전하지만 다시 취소될 수는 없다.
    REQUIRE(token.cancellable());
    REQUIRE_FALSE(token.cancelled());

    int invoked { 0 };
    const auto registration { token.register_callback([&invoked]() { ++invoked; }) };
    REQUIRE(invoked == 0);
}

TEST_CASE("Concurrent cancellation requests notify each callback once", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    const gitman::process_cancellation_token token { source.token() };

    std::atomic_int invoked { 0 };
    const auto registration { token.register_callback([&invoked]() { invoked.fetch_add(1, std::memory_order_relaxed); }) };

    std::vector<std::thread> threads {};
    threads.reserve(8);
    for (int index = 0; index < 8; ++index)
        threads.emplace_back([&source]() { source.request_cancellation(); });
    for (std::thread& worker : threads)
        worker.join();

    REQUIRE(invoked.load(std::memory_order_relaxed) == 1);
    REQUIRE(token.cancelled());
}

TEST_CASE("Registrations may be created and released while cancellation races", "[application][process][cancellation]")
{
    gitman::process_cancellation_source source {};
    const gitman::process_cancellation_token token { source.token() };

    std::atomic_int invoked { 0 };
    std::atomic_int registered { 0 };
    std::vector<std::thread> threads {};
    threads.reserve(4);
    for (int index = 0; index < 4; ++index)
        threads.emplace_back([&token, &invoked, &registered]() {
            for (int repeat = 0; repeat < 200; ++repeat)
            {
                const auto registration { token.register_callback([&invoked]() { invoked.fetch_add(1, std::memory_order_relaxed); }) };
                registered.fetch_add(1, std::memory_order_relaxed);
            }
        });

    source.request_cancellation();
    for (std::thread& worker : threads)
        worker.join();

    // 취소 시점에 살아 있던 구독과 이후 즉시 실행된 구독만 통지되므로 총 등록 수를 넘지 않는다.
    REQUIRE(registered.load(std::memory_order_relaxed) == 800);
    REQUIRE(invoked.load(std::memory_order_relaxed) <= 800);
    REQUIRE(token.cancelled());
}
