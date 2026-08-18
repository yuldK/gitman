#include "domain/operation_log.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {
    gitman::operation_log_entry make_entry(const std::u8string_view text, const gitman::log_entry_kind kind = gitman::log_entry_kind::standard_output)
    {
        gitman::operation_log_entry entry {};
        entry.kind = kind;
        entry.text = text;
        return entry;
    }
} // namespace

TEST_CASE("The log buffer assigns monotonic sequences per card", "[log][domain]")
{
    gitman::operation_log_buffer buffer {};
    buffer.append(make_entry(u8"first"));
    buffer.append(make_entry(u8"second", gitman::log_entry_kind::standard_error));

    REQUIRE(buffer.records().size() == 2u);
    REQUIRE(buffer.records()[0].sequence == 1u);
    REQUIRE(buffer.records()[0].entry.text == u8"first");
    REQUIRE(buffer.records()[1].sequence == 2u);
    REQUIRE(buffer.records()[1].entry.kind == gitman::log_entry_kind::standard_error);
    REQUIRE(buffer.total_appended() == 2u);
    REQUIRE(buffer.dropped_count() == 0u);
}

TEST_CASE("The log buffer drops the oldest records over its capacity", "[log][domain]")
{
    gitman::operation_log_buffer buffer { 3 };
    for (int index = 0; index < 5; ++index)
        buffer.append(make_entry(u8"line"));

    REQUIRE(buffer.records().size() == 3u);
    // 남은 record는 최근 3개이며 sequence가 이어진다.
    REQUIRE(buffer.records().front().sequence == 3u);
    REQUIRE(buffer.records().back().sequence == 5u);
    REQUIRE(buffer.total_appended() == 5u);
    REQUIRE(buffer.dropped_count() == 2u);
}

TEST_CASE("Clearing the log keeps the sequence numbering", "[log][domain]")
{
    gitman::operation_log_buffer buffer {};
    buffer.append(make_entry(u8"first"));
    buffer.clear();
    REQUIRE(buffer.records().empty());

    // 지운 뒤에도 sequence는 이어져 record 순서의 기준이 유지된다.
    buffer.append(make_entry(u8"second"));
    REQUIRE(buffer.records().front().sequence == 2u);
    REQUIRE(buffer.total_appended() == 2u);
    // clear는 상한 제거가 아니므로 dropped에 세지 않는다.
    REQUIRE(buffer.dropped_count() == 0u);
}
