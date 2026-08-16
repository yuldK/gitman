#include "domain/process_execution.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string_view>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    gitman::diagnostic make_diagnostic(const gitman::diagnostic_severity severity)
    {
        gitman::diagnostic value {};
        value.code = gitman::diagnostic_code::process_start_failed;
        value.severity = severity;
        return value;
    }

    struct completion_name_case
    {
        gitman::process_completion completion { gitman::process_completion::exited };
        std::u8string_view name {};
    };
} // namespace

TEST_CASE("Process output records start from neutral defaults", "[domain][process]")
{
    const gitman::process_output_record record {};
    REQUIRE(record.sequence == 0);
    REQUIRE(record.stream == gitman::process_stream::standard_output);
    REQUIRE(record.text.empty());
    REQUIRE_FALSE(record.progress);
    REQUIRE_FALSE(record.continued);
    REQUIRE_FALSE(record.replaced_invalid_bytes);
    REQUIRE_FALSE(record.transcoded_from_active_code_page);
}

TEST_CASE("Process results default to an unexecuted request", "[domain][process]")
{
    const gitman::process_result result {};
    REQUIRE(result.completion == gitman::process_completion::invalid_request);
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE(result.duration.count() == 0);
    REQUIRE(result.record_count == 0);
    REQUIRE(result.captured_bytes == 0);
    REQUIRE_FALSE(result.output_truncated);
    REQUIRE_FALSE(result.native_error.has_value());
    REQUIRE(result.masked_command_line.empty());
    REQUIRE(result.diagnostics.empty());
    REQUIRE_FALSE(result.succeeded());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE_FALSE(result.has_warnings());
}

TEST_CASE("Process success requires a zero exit code without error diagnostics", "[domain][process]")
{
    gitman::process_result result {};
    result.completion = gitman::process_completion::exited;
    result.exit_code = 0;
    REQUIRE(result.succeeded());

    result.exit_code = 1;
    REQUIRE_FALSE(result.succeeded());

    result.exit_code = 0;
    result.diagnostics.push_back(make_diagnostic(gitman::diagnostic_severity::warning));
    REQUIRE(result.succeeded());
    REQUIRE(result.has_warnings());
    REQUIRE_FALSE(result.has_errors());

    result.diagnostics.push_back(make_diagnostic(gitman::diagnostic_severity::error));
    REQUIRE_FALSE(result.succeeded());
    REQUIRE(result.has_errors());
}

TEST_CASE("Cancelled and timed out runs are never reported as success", "[domain][process]")
{
    // 자식이 우연히 0을 남겨도 완료 사유가 정상 종료가 아니면 성공으로 보지 않는다.
    constexpr std::array unsuccessful_completions {
        gitman::process_completion::timed_out,
        gitman::process_completion::cancelled,
        gitman::process_completion::start_failed,
        gitman::process_completion::invalid_request,
        gitman::process_completion::internal_error,
    };
    for (const gitman::process_completion completion : unsuccessful_completions)
    {
        gitman::process_result result {};
        result.completion = completion;
        result.exit_code = 0;
        REQUIRE_FALSE(result.succeeded());
    }
}

TEST_CASE("Process streams and completions have stable names", "[domain][process]")
{
    REQUIRE(u8_equal(gitman::process_stream_name(gitman::process_stream::standard_output), u8"stdout"));
    REQUIRE(u8_equal(gitman::process_stream_name(gitman::process_stream::standard_error), u8"stderr"));
    REQUIRE(u8_equal(gitman::process_stream_name(static_cast<gitman::process_stream>(-1)), u8"unknown"));

    constexpr std::array completion_names {
        completion_name_case { gitman::process_completion::exited, u8"exited" },
        completion_name_case { gitman::process_completion::start_failed, u8"start_failed" },
        completion_name_case { gitman::process_completion::timed_out, u8"timed_out" },
        completion_name_case { gitman::process_completion::cancelled, u8"cancelled" },
        completion_name_case { gitman::process_completion::invalid_request, u8"invalid_request" },
        completion_name_case { gitman::process_completion::internal_error, u8"internal_error" },
    };
    for (const auto& completion_name : completion_names)
        REQUIRE(u8_equal(gitman::process_completion_name(completion_name.completion), completion_name.name));
    REQUIRE(u8_equal(gitman::process_completion_name(static_cast<gitman::process_completion>(-1)), u8"unknown"));
}
