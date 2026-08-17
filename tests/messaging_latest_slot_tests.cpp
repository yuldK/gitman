#include "messaging/latest_slot.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

TEST_CASE("A latest slot keeps only the newest value", "[messaging][slot]")
{
    messaging::latest_slot<int> slot {};
    REQUIRE(slot.take_newer(0).has_value() == false);

    REQUIRE(slot.publish(10) == 1u);
    REQUIRE(slot.publish(20) == 2u);
    REQUIRE(slot.publish(30) == 3u);

    const auto taken { slot.take_newer(0) };
    REQUIRE(taken.has_value());
    REQUIRE(taken->version == 3u);
    REQUIRE(taken->value == 30);

    // 이미 본 version보다 새 값이 없으면 돌려주지 않는다.
    REQUIRE(slot.take_newer(taken->version).has_value() == false);

    REQUIRE(slot.publish(40) == 4u);
    const auto newer { slot.take_newer(taken->version) };
    REQUIRE(newer.has_value());
    REQUIRE(newer->version == 4u);
    REQUIRE(newer->value == 40);
}

TEST_CASE("The slot signal fires once per unseen batch", "[messaging][slot]")
{
    messaging::latest_slot<int> slot {};
    int signal_count { 0 };
    slot.set_signal_callback([&signal_count] { ++signal_count; });

    // 소비되지 않은 새 값이 처음 생길 때 한 번만 부른다. 연속 게시는 wake 하나로
    // 병합된다.
    REQUIRE(slot.publish(1) == 1u);
    REQUIRE(slot.publish(2) == 2u);
    REQUIRE(slot.publish(3) == 3u);
    REQUIRE(signal_count == 1);

    REQUIRE(slot.take_newer(0).has_value());
    REQUIRE(slot.publish(4) == 4u);
    REQUIRE(signal_count == 2);
}

TEST_CASE("A closed slot refuses publishes but hands out the remaining value", "[messaging][slot]")
{
    messaging::latest_slot<std::string> slot {};
    REQUIRE(slot.publish("before") == 1u);
    slot.close();
    slot.close();
    REQUIRE(slot.closed());

    // 닫힌 slot의 publish는 저장하지 않고 0을 돌려준다.
    REQUIRE(slot.publish("after") == 0u);

    const auto taken { slot.take_newer(0) };
    REQUIRE(taken.has_value());
    REQUIRE(taken->value == "before");
}

TEST_CASE("Shared pointer snapshots share without copying the payload", "[messaging][slot]")
{
    messaging::latest_slot<std::shared_ptr<const std::string>> slot {};
    const auto snapshot { std::make_shared<const std::string>("snapshot") };
    REQUIRE(slot.publish(snapshot) == 1u);

    const auto taken { slot.take_newer(0) };
    REQUIRE(taken.has_value());
    REQUIRE(taken->value == snapshot);
    REQUIRE(snapshot.use_count() >= 2);
}

TEST_CASE("Close also fires the slot signal", "[messaging][slot]")
{
    messaging::latest_slot<int> slot {};
    int signal_count { 0 };
    slot.set_signal_callback([&signal_count] { ++signal_count; });
    slot.close();
    REQUIRE(signal_count == 1);
}
