#include "application/cancellation_registry.h"
#include "application/process_cancellation.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstddef>
#include <functional>
#include <optional>
#include <thread>
#include <vector>

TEST_CASE("The registry cancels every live source at once", "[app][cancellation]")
{
    // driver의 `cancel()`이 UI thread에서 부르는 경로다 (설계 L3). close 메시지가
    // 유실돼도 worker가 매달리지 않도록 살아 있는 source 전부에 취소가 걸린다.
    gitman::cancellation_registry registry {};
    gitman::process_cancellation_source first {};
    gitman::process_cancellation_source second {};

    registry.add(first);
    registry.add(second);
    REQUIRE(registry.size() == 2u);
    REQUIRE(first.cancellation_requested() == false);
    REQUIRE(second.cancellation_requested() == false);

    registry.cancel_all();
    REQUIRE(first.cancellation_requested());
    REQUIRE(second.cancellation_requested());

    // 취소는 상태를 바꾸는 일이라 다시 불러도 안전하다 (종료 경로는 여러 번 지난다).
    registry.cancel_all();
    REQUIRE(registry.size() == 2u);
}

TEST_CASE("Sources that disappeared are dropped from the registry", "[app][cancellation]")
{
    // 보관소는 약한 손잡이만 든다. 변경 작업이 끝나 source가 사라지면 항목도
    // 함께 빠져야 보관소가 무한히 자라지 않는다.
    gitman::cancellation_registry registry {};
    std::optional<gitman::process_cancellation_source> transient { std::in_place };
    gitman::process_cancellation_source lasting {};

    registry.add(*transient);
    registry.add(lasting);
    REQUIRE(registry.size() == 2u);

    transient.reset();
    REQUIRE(registry.size() == 1u);

    // 사라진 항목이 있어도 남은 것은 정상적으로 취소된다.
    registry.cancel_all();
    REQUIRE(lasting.cancellation_requested());
}

TEST_CASE("An empty registry cancels nothing and stays empty", "[app][cancellation]")
{
    gitman::cancellation_registry registry {};
    REQUIRE(registry.size() == 0u);
    registry.cancel_all();
    REQUIRE(registry.size() == 0u);
}

TEST_CASE("A weak canceller cancels while its source lives and reports death after", "[app][cancellation]")
{
    // 보관소가 드는 손잡이 자체의 계약이다. 살아 있으면 취소를 걸고 true를,
    // 사라졌으면 아무 일도 하지 않고 false를 돌려준다.
    std::function<bool()> canceller {};
    {
        gitman::process_cancellation_source source {};
        canceller = source.weak_canceller();
        REQUIRE(gitman::weak_canceller_alive(canceller));
        REQUIRE(canceller());
        REQUIRE(source.cancellation_requested());
        REQUIRE(source.token().cancelled());
    }

    REQUIRE(gitman::weak_canceller_alive(canceller) == false);
    REQUIRE(canceller() == false);
}

TEST_CASE("The registry takes concurrent adds and a concurrent cancel", "[app][cancellation]")
{
    // `add`는 logic thread가, `cancel_all`은 UI thread가 부른다. 자료구조가
    // mutex 하나로 지켜지는지를 확인한다 (값이 아니라 안전성이 관심사다).
    gitman::cancellation_registry registry {};
    std::vector<gitman::process_cancellation_source> sources { 32 };

    std::atomic<bool> go { false };
    const auto add_all = [&] {
        while (go.load() == false)
            std::this_thread::yield();
        for (gitman::process_cancellation_source& source : sources)
            registry.add(source);
    };
    std::thread adder { add_all };

    go.store(true);
    for (int index = 0; index < 32; ++index)
        registry.cancel_all();
    adder.join();

    registry.cancel_all();
    REQUIRE(registry.size() == sources.size());
    for (const gitman::process_cancellation_source& source : sources)
        REQUIRE(source.cancellation_requested());
}
