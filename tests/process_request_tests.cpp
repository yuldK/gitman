#include "application/process_request.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string_view>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    gitman::process_request make_valid_request()
    {
        gitman::process_request request {};
        request.executable = u8"C:/Program Files/Git/cmd/git.exe";
        request.arguments = { u8"status", u8"--porcelain=v2" };
        request.working_directory = u8"D:/작업 공간/example";
        request.maximum_captured_bytes_per_stream = 1024;
        return request;
    }

    bool is_invalid_request_error(const gitman::diagnostic& value) noexcept
    {
        return value.code == gitman::diagnostic_code::invalid_process_request && value.severity == gitman::diagnostic_severity::error;
    }

    bool all_invalid_request_errors(const std::vector<gitman::diagnostic>& diagnostics)
    {
        return std::ranges::all_of(diagnostics, is_invalid_request_error);
    }
} // namespace

TEST_CASE("Process requests expose approved defaults", "[application][process]")
{
    REQUIRE(gitman::default_process_record_byte_limit == 8u * 1024u);
    REQUIRE(gitman::minimum_process_record_byte_limit == 4u);
    REQUIRE(gitman::process_read_block_byte_size == 64u * 1024u);

    const gitman::process_request request {};
    REQUIRE(request.executable.empty());
    REQUIRE(request.arguments.empty());
    REQUIRE(request.working_directory.empty());
    REQUIRE(request.environment_overrides.empty());
    REQUIRE_FALSE(request.timeout.has_value());
    REQUIRE(request.maximum_captured_bytes_per_stream == 0);
    REQUIRE(request.maximum_record_bytes == gitman::default_process_record_byte_limit);
    REQUIRE(request.text_encoding == gitman::process_text_encoding::utf8);

    REQUIRE(u8_equal(gitman::process_text_encoding_name(gitman::process_text_encoding::utf8), u8"utf8"));
    REQUIRE(u8_equal(gitman::process_text_encoding_name(gitman::process_text_encoding::active_code_page_fallback), u8"active_code_page_fallback"));
    REQUIRE(u8_equal(gitman::process_text_encoding_name(static_cast<gitman::process_text_encoding>(-1)), u8"unknown"));
}

TEST_CASE("Absolute Windows paths are recognised without touching the file system", "[application][process][path]")
{
    REQUIRE(gitman::is_absolute_windows_path(u8"C:\\tools\\git.exe"));
    REQUIRE(gitman::is_absolute_windows_path(u8"c:/tools/git.exe"));
    REQUIRE(gitman::is_absolute_windows_path(u8"Z:\\"));
    REQUIRE(gitman::is_absolute_windows_path(u8"\\\\server\\share\\git.exe"));
    REQUIRE(gitman::is_absolute_windows_path(u8"//server/share"));
    REQUIRE(gitman::is_absolute_windows_path(u8"\\\\?\\C:\\긴 경로\\git.exe"));
    REQUIRE(gitman::is_absolute_windows_path(u8"\\\\.\\pipe\\example"));

    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8""));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"C:"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"C:relative\\git.exe"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"\\rooted\\git.exe"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"/rooted/git.exe"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"tools\\git.exe"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"..\\git.exe"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"1:\\git.exe"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"\\\\"));
    REQUIRE_FALSE(gitman::is_absolute_windows_path(u8"\\\\\\share"));
}

TEST_CASE("A complete request with Unicode paths validates cleanly", "[application][process]")
{
    gitman::process_request request { make_valid_request() };
    request.arguments.push_back(u8"경로/이름 with spaces");
    request.arguments.push_back(u8"");
    request.environment_overrides.push_back({ u8"GIT_TERMINAL_PROMPT", u8"0" });
    request.environment_overrides.push_back({ u8"GIT_ASKPASS", std::nullopt });
    request.timeout = std::chrono::milliseconds { 30000 };
    request.text_encoding = gitman::process_text_encoding::active_code_page_fallback;

    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Executable paths must be present absolute and free of NUL", "[application][process]")
{
    gitman::process_request request { make_valid_request() };
    request.executable.clear();
    const auto missing { gitman::validate_process_request(request) };
    REQUIRE(missing.size() == 1);
    REQUIRE(all_invalid_request_errors(missing));

    request.executable = u8"git.exe";
    const auto relative { gitman::validate_process_request(request) };
    REQUIRE(relative.size() == 1);
    REQUIRE(all_invalid_request_errors(relative));

    // NUL을 포함한 값은 Win32 command line에서 잘리므로 절대 경로 검사보다 먼저 거부한다.
    request.executable = std::u8string { u8"C:/tools/git.exe" } + std::u8string(1, char8_t {}) + u8"suffix";
    const auto embedded_null { gitman::validate_process_request(request) };
    REQUIRE(embedded_null.size() == 1);
    REQUIRE(all_invalid_request_errors(embedded_null));
}

TEST_CASE("Working directories must be present and absolute", "[application][process]")
{
    gitman::process_request request { make_valid_request() };
    request.working_directory.clear();
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    request.working_directory = u8"relative\\path";
    const auto relative { gitman::validate_process_request(request) };
    REQUIRE(relative.size() == 1);
    REQUIRE(all_invalid_request_errors(relative));

    request.working_directory = std::u8string { u8"D:/work" } + std::u8string(1, char8_t {});
    REQUIRE(gitman::validate_process_request(request).size() == 1);
}

TEST_CASE("Arguments reject embedded NUL for every position", "[application][process]")
{
    gitman::process_request request { make_valid_request() };
    request.arguments = { u8"status", std::u8string { u8"--branch" } + std::u8string(1, char8_t {}), std::u8string(1, char8_t {}) };

    const auto diagnostics { gitman::validate_process_request(request) };
    REQUIRE(diagnostics.size() == 2);
    REQUIRE(all_invalid_request_errors(diagnostics));
}

TEST_CASE("Environment overrides reject malformed and duplicate names", "[application][process]")
{
    gitman::process_request request { make_valid_request() };
    request.environment_overrides.push_back({ u8"", u8"value" });
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    request.environment_overrides.clear();
    request.environment_overrides.push_back({ u8"NAME=VALUE", u8"value" });
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    request.environment_overrides.clear();
    request.environment_overrides.push_back({ std::u8string { u8"NAME" } + std::u8string(1, char8_t {}), u8"value" });
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    request.environment_overrides.clear();
    request.environment_overrides.push_back({ u8"NAME", std::u8string { u8"value" } + std::u8string(1, char8_t {}) });
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    // Windows 환경 변수 이름은 대소문자를 구분하지 않으므로 중복은 적용 순서에 의존한다.
    request.environment_overrides.clear();
    request.environment_overrides.push_back({ u8"Git_Terminal_Prompt", u8"0" });
    request.environment_overrides.push_back({ u8"GIT_TERMINAL_PROMPT", u8"1" });
    const auto duplicate { gitman::validate_process_request(request) };
    REQUIRE(duplicate.size() == 1);
    REQUIRE(all_invalid_request_errors(duplicate));

    request.environment_overrides.clear();
    request.environment_overrides.push_back({ u8"GIT_TERMINAL_PROMPT", u8"0" });
    request.environment_overrides.push_back({ u8"GIT_ASKPASS", std::nullopt });
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Timeout and capture limits reject unusable values", "[application][process]")
{
    gitman::process_request request { make_valid_request() };
    request.timeout = std::chrono::milliseconds { 0 };
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    request.timeout = std::chrono::milliseconds { -1 };
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    request.timeout = std::chrono::milliseconds { 1 };
    REQUIRE(gitman::validate_process_request(request).empty());

    request.timeout.reset();
    request.maximum_captured_bytes_per_stream = 0;
    REQUIRE(gitman::validate_process_request(request).size() == 1);

    request.maximum_captured_bytes_per_stream = 1;
    REQUIRE(gitman::validate_process_request(request).empty());

    // 강제 분할이 UTF-8 sequence 하나를 쪼개지 않도록 최소 4 byte를 요구한다.
    request.maximum_record_bytes = gitman::minimum_process_record_byte_limit - 1;
    const auto too_small { gitman::validate_process_request(request) };
    REQUIRE(too_small.size() == 1);
    REQUIRE(all_invalid_request_errors(too_small));

    request.maximum_record_bytes = gitman::minimum_process_record_byte_limit;
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Independent request problems are reported together", "[application][process]")
{
    gitman::process_request request {};
    request.arguments = { std::u8string(1, char8_t {}) };
    request.environment_overrides.push_back({ u8"", u8"value" });
    request.timeout = std::chrono::milliseconds { 0 };
    request.maximum_record_bytes = 0;

    const auto diagnostics { gitman::validate_process_request(request) };
    // 실행 파일, 작업 디렉터리, 인자, 환경 변수, timeout, 캡처 상한과 레코드 상한이다.
    REQUIRE(diagnostics.size() == 7);
    REQUIRE(all_invalid_request_errors(diagnostics));
    REQUIRE(std::ranges::all_of(diagnostics, [](const gitman::diagnostic& value) { return value.message.empty() == false; }));
}
