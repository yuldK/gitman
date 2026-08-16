#include "infrastructure/process_output_pipeline.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace {
    constexpr std::size_t large_capture_limit { 1u << 20 };
    constexpr std::u8string_view replacement { u8"�" };

    class record_recorder
    {
    public:
        [[nodiscard]] gitman::process_output_pipeline::record_handler handler()
        {
            return [this](gitman::process_output_record& record) { records.push_back(record); };
        }

        std::vector<gitman::process_output_record> records {};
    };

    std::u8string normalize(const std::u8string_view input, bool& replaced)
    {
        return gitman::normalize_utf8_text(input, replaced);
    }
} // namespace

TEST_CASE("Valid UTF-8 text passes through normalization unchanged", "[infrastructure][pipeline][utf8]")
{
    bool replaced { false };
    REQUIRE(normalize(u8"plain ascii", replaced) == u8"plain ascii");
    REQUIRE(normalize(u8"한글과 emoji 🙂", replaced) == u8"한글과 emoji 🙂");
    REQUIRE(normalize(u8"", replaced).empty());
    REQUIRE_FALSE(replaced);
}

TEST_CASE("Invalid UTF-8 byte sequences are replaced", "[infrastructure][pipeline][utf8]")
{
    bool replaced { false };
    const std::u8string lone_continuation { normalize(std::u8string { char8_t { 0x80 } }, replaced) };
    REQUIRE(replaced);
    REQUIRE(lone_continuation == replacement);

    replaced = false;
    // overlong 표현, surrogate, U+10FFFF 초과와 미완결 sequence를 모두 거부한다.
    REQUIRE(normalize(std::u8string { char8_t { 0xC0 }, char8_t { 0xAF } }, replaced) == std::u8string { replacement } + std::u8string { replacement });
    replaced = false;
    REQUIRE(normalize(std::u8string { char8_t { 0xED }, char8_t { 0xA0 }, char8_t { 0x80 } }, replaced).empty() == false);
    REQUIRE(replaced);
    replaced = false;
    REQUIRE(normalize(std::u8string { char8_t { 0xF5 }, char8_t { 0x80 }, char8_t { 0x80 }, char8_t { 0x80 } }, replaced).empty() == false);
    REQUIRE(replaced);
    replaced = false;
    REQUIRE(normalize(std::u8string { char8_t { 0xED }, char8_t { 0x95 } }, replaced) == std::u8string { replacement } + std::u8string { replacement });
    REQUIRE(replaced);

    replaced = false;
    const std::u8string mixed { normalize(std::u8string { u8"ok " } + std::u8string { char8_t { 0xFF } } + std::u8string { u8" end" }, replaced) };
    REQUIRE(replaced);
    REQUIRE(mixed == std::u8string { u8"ok " } + std::u8string { replacement } + std::u8string { u8" end" });
}

TEST_CASE("Line feeds and carriage returns split records", "[infrastructure][pipeline]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_error, 4096, large_capture_limit };
    pipeline.append(u8"first\nsecond\r\n\nthird", recorder.handler());
    pipeline.flush(recorder.handler());

    REQUIRE(recorder.records.size() == 4);
    REQUIRE(recorder.records[0].text == u8"first");
    REQUIRE(recorder.records[1].text == u8"second");
    // `\n`으로 끝난 빈 줄은 보존한다.
    REQUIRE(recorder.records[2].text.empty());
    REQUIRE(recorder.records[3].text == u8"third");
    for (const gitman::process_output_record& record : recorder.records)
    {
        REQUIRE(record.stream == gitman::process_stream::standard_error);
        REQUIRE_FALSE(record.progress);
        REQUIRE_FALSE(record.continued);
        // sequence는 runner가 부여하므로 파이프라인 단계에서는 0이다.
        REQUIRE(record.sequence == 0);
    }
}

TEST_CASE("Lone carriage returns become progress records", "[infrastructure][pipeline]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_output, 4096, large_capture_limit };
    pipeline.append(u8"10%\r50%\r100%\r\ndone\n", recorder.handler());
    pipeline.flush(recorder.handler());

    REQUIRE(recorder.records.size() == 4);
    REQUIRE(recorder.records[0].text == u8"10%");
    REQUIRE(recorder.records[0].progress);
    REQUIRE(recorder.records[1].text == u8"50%");
    REQUIRE(recorder.records[1].progress);
    // `\r\n`으로 끝나면 진행 표시가 아니라 완성된 줄이다.
    REQUIRE(recorder.records[2].text == u8"100%");
    REQUIRE_FALSE(recorder.records[2].progress);
    REQUIRE(recorder.records[3].text == u8"done");
}

TEST_CASE("Empty progress markers do not create records", "[infrastructure][pipeline]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_output, 4096, large_capture_limit };
    pipeline.append(u8"\r\r\r", recorder.handler());
    pipeline.flush(recorder.handler());
    REQUIRE(recorder.records.empty());
}

TEST_CASE("Multi byte characters survive chunk boundaries", "[infrastructure][pipeline][utf8]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_output, 4096, large_capture_limit };
    // 3 byte 한글 문자를 세 번의 append로 나눠 넣는다.
    pipeline.append(std::u8string { char8_t { 0xED } }, recorder.handler());
    pipeline.append(std::u8string { char8_t { 0x95 } }, recorder.handler());
    pipeline.append(std::u8string { char8_t { 0x9C } }, recorder.handler());
    REQUIRE(recorder.records.empty());

    pipeline.append(u8"\n", recorder.handler());
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].text == u8"한");
    REQUIRE_FALSE(recorder.records[0].replaced_invalid_bytes);
}

TEST_CASE("Long lines split at record limits without breaking sequences", "[infrastructure][pipeline][utf8]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_output, 4, large_capture_limit };
    // 한글 두 글자는 6 byte이므로 4 byte 상한에서 강제 분할이 일어난다.
    pipeline.append(u8"한글\n", recorder.handler());

    REQUIRE(recorder.records.size() == 2);
    REQUIRE(recorder.records[0].text == u8"한");
    REQUIRE(recorder.records[0].continued);
    REQUIRE_FALSE(recorder.records[0].replaced_invalid_bytes);
    REQUIRE(recorder.records[1].text == u8"글");
    REQUIRE_FALSE(recorder.records[1].continued);
    REQUIRE_FALSE(recorder.records[1].replaced_invalid_bytes);
}

TEST_CASE("Capture limits truncate later output without losing pending text", "[infrastructure][pipeline]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_output, 4096, 8 };
    pipeline.append(u8"12345678discarded\n", recorder.handler());

    REQUIRE(pipeline.truncated());
    REQUIRE(pipeline.captured_bytes() == 8);
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].text == u8"12345678");

    // 상한 도달 뒤의 append는 레코드를 만들지 않는다.
    pipeline.append(u8"more\n", recorder.handler());
    pipeline.flush(recorder.handler());
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(pipeline.captured_bytes() == 8);
}

TEST_CASE("Flush emits the unterminated tail once", "[infrastructure][pipeline]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_output, 4096, large_capture_limit };
    pipeline.append(u8"tail without newline", recorder.handler());
    REQUIRE(recorder.records.empty());

    pipeline.flush(recorder.handler());
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].text == u8"tail without newline");
    REQUIRE_FALSE(recorder.records[0].progress);

    // 두 번째 flush는 아무 것도 내보내지 않는다.
    pipeline.flush(recorder.handler());
    REQUIRE(recorder.records.size() == 1);
}

TEST_CASE("Flush reports a pending carriage return as progress", "[infrastructure][pipeline]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline { gitman::process_stream::standard_output, 4096, large_capture_limit };
    pipeline.append(u8"working\r", recorder.handler());
    pipeline.flush(recorder.handler());

    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].text == u8"working");
    REQUIRE(recorder.records[0].progress);
}
