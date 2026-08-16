#include "infrastructure/process_output_pipeline.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
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

    // 파이프라인의 판정 순서를 OS 설정과 무관하게 확인하기 위한 대역이다.
    class fake_transcoder final : public gitman::text_transcoder
    {
    public:
        [[nodiscard]] std::optional<std::u8string> to_utf8(const std::u8string_view bytes) noexcept override
        {
            ++call_count;
            last_input.assign(bytes);
            inputs.emplace_back(bytes);
            if (succeeds == false)
                return std::nullopt;
            return std::u8string { u8"복원된 텍스트" };
        }

        [[nodiscard]] std::size_t safe_split_position(const std::u8string_view bytes) const noexcept override
        {
            // 0xC7을 lead byte로 취급하는 2 byte 문자 규칙을 흉내 낸다.
            std::size_t index { 0 };
            while (index < bytes.size())
            {
                const std::size_t length { bytes[index] == char8_t { 0xC7 } ? std::size_t { 2 } : std::size_t { 1 } };
                if (index + length > bytes.size())
                    return index;
                index += length;
            }
            return bytes.size();
        }

        std::size_t call_count {};
        std::u8string last_input {};
        std::vector<std::u8string> inputs {};
        bool succeeds { true };
    };

    std::u8string normalize(const std::u8string_view input, bool& replaced)
    {
        return gitman::normalize_utf8_text(input, replaced);
    }

    std::u8string invalid_utf8_line()
    {
        return std::u8string { char8_t { 0xC7 }, char8_t { 0xD1 }, char8_t { 0x0A } };
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

TEST_CASE("UTF-8 validation rejects the same sequences as normalization", "[infrastructure][pipeline][utf8]")
{
    REQUIRE(gitman::is_valid_utf8_text(u8""));
    REQUIRE(gitman::is_valid_utf8_text(u8"plain ascii"));
    REQUIRE(gitman::is_valid_utf8_text(u8"한글과 emoji 🙂"));

    // 단독 continuation, overlong, surrogate, 범위 초과와 미완결 sequence
    REQUIRE_FALSE(gitman::is_valid_utf8_text(std::u8string { char8_t { 0x80 } }));
    REQUIRE_FALSE(gitman::is_valid_utf8_text(std::u8string { char8_t { 0xC0 }, char8_t { 0xAF } }));
    REQUIRE_FALSE(gitman::is_valid_utf8_text(std::u8string { char8_t { 0xED }, char8_t { 0xA0 }, char8_t { 0x80 } }));
    REQUIRE_FALSE(gitman::is_valid_utf8_text(std::u8string { char8_t { 0xF5 }, char8_t { 0x80 }, char8_t { 0x80 }, char8_t { 0x80 } }));
    REQUIRE_FALSE(gitman::is_valid_utf8_text(std::u8string { char8_t { 0xED }, char8_t { 0x95 } }));
    // CP949 `한` byte는 유효한 UTF-8이 아니다.
    REQUIRE_FALSE(gitman::is_valid_utf8_text(std::u8string { char8_t { 0xC7 }, char8_t { 0xD1 } }));
}

TEST_CASE("Code page fallback only converts records that are not valid UTF-8", "[infrastructure][pipeline][encoding]")
{
    fake_transcoder transcoder {};
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline {
        gitman::process_stream::standard_output,
        4096,
        large_capture_limit,
        gitman::process_text_encoding::active_code_page_fallback,
        &transcoder,
    };

    pipeline.append(invalid_utf8_line(), recorder.handler());
    REQUIRE(transcoder.call_count == 1);
    REQUIRE(transcoder.last_input == std::u8string { char8_t { 0xC7 }, char8_t { 0xD1 } });
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].text == u8"복원된 텍스트");
    REQUIRE(recorder.records[0].transcoded_from_active_code_page);
    REQUIRE_FALSE(recorder.records[0].replaced_invalid_bytes);

    // 이미 유효한 UTF-8은 transcoder를 거치지 않는다.
    pipeline.append(u8"한글 그대로\n", recorder.handler());
    REQUIRE(transcoder.call_count == 1);
    REQUIRE(recorder.records.size() == 2);
    REQUIRE(recorder.records[1].text == u8"한글 그대로");
    REQUIRE_FALSE(recorder.records[1].transcoded_from_active_code_page);
}

TEST_CASE("Failed transcoding falls back to replacement characters", "[infrastructure][pipeline][encoding]")
{
    fake_transcoder transcoder {};
    transcoder.succeeds = false;
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline {
        gitman::process_stream::standard_output,
        4096,
        large_capture_limit,
        gitman::process_text_encoding::active_code_page_fallback,
        &transcoder,
    };

    pipeline.append(invalid_utf8_line(), recorder.handler());
    REQUIRE(transcoder.call_count == 1);
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].replaced_invalid_bytes);
    REQUIRE_FALSE(recorder.records[0].transcoded_from_active_code_page);
    REQUIRE(recorder.records[0].text == std::u8string { replacement } + std::u8string { replacement });
}

TEST_CASE("The default encoding never consults a transcoder", "[infrastructure][pipeline][encoding]")
{
    fake_transcoder transcoder {};
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline {
        gitman::process_stream::standard_output,
        4096,
        large_capture_limit,
        gitman::process_text_encoding::utf8,
        &transcoder,
    };

    pipeline.append(invalid_utf8_line(), recorder.handler());
    REQUIRE(transcoder.call_count == 0);
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].replaced_invalid_bytes);
    REQUIRE_FALSE(recorder.records[0].transcoded_from_active_code_page);
}

TEST_CASE("Fallback without a transcoder still replaces invalid bytes", "[infrastructure][pipeline][encoding]")
{
    record_recorder recorder {};
    gitman::process_output_pipeline pipeline {
        gitman::process_stream::standard_output,
        4096,
        large_capture_limit,
        gitman::process_text_encoding::active_code_page_fallback,
        nullptr,
    };

    pipeline.append(invalid_utf8_line(), recorder.handler());
    REQUIRE(recorder.records.size() == 1);
    REQUIRE(recorder.records[0].replaced_invalid_bytes);
    REQUIRE_FALSE(recorder.records[0].transcoded_from_active_code_page);
}

TEST_CASE("Forced splits in fallback mode follow the code page character boundary", "[infrastructure][pipeline][encoding]")
{
    fake_transcoder transcoder {};
    record_recorder recorder {};
    // 상한 5 byte에 2 byte 문자 세 개를 넣으면 경계를 무시할 때 세 번째 문자 가운데서
    // 잘린다. transcoder의 문자 경계를 따르면 4 byte에서 끊겨 양쪽이 온전해야 한다.
    gitman::process_output_pipeline pipeline {
        gitman::process_stream::standard_output,
        5,
        large_capture_limit,
        gitman::process_text_encoding::active_code_page_fallback,
        &transcoder,
    };

    const std::u8string bytes { char8_t { 0xC7 }, char8_t { 0xD1 }, char8_t { 0xC7 }, char8_t { 0xD1 }, char8_t { 0xC7 }, char8_t { 0xD1 }, char8_t { 0x0A } };
    pipeline.append(bytes, recorder.handler());
    pipeline.flush(recorder.handler());

    REQUIRE(recorder.records.size() == 2);
    REQUIRE(recorder.records[0].continued);
    REQUIRE(recorder.records[0].transcoded_from_active_code_page);
    REQUIRE(recorder.records[1].transcoded_from_active_code_page);
    REQUIRE(transcoder.inputs.size() == 2);
    REQUIRE(transcoder.inputs[0].size() == 4);
    REQUIRE(transcoder.inputs[1].size() == 2);
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
