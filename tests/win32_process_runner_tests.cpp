#include "platform/win32/win32_process_runner.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {
    constexpr std::size_t default_capture_limit { 32u * 1024u * 1024u };

    class recording_sink final : public gitman::process_output_sink
    {
    public:
        void on_record(const gitman::process_output_record& record) override
        {
            records.push_back(record);
        }

        [[nodiscard]] std::vector<std::u8string> texts(const gitman::process_stream stream) const
        {
            std::vector<std::u8string> values {};
            for (const gitman::process_output_record& record : records)
                if (record.stream == stream)
                    values.push_back(record.text);
            return values;
        }

        std::vector<gitman::process_output_record> records {};
    };

    // 도우미는 test 실행 파일과 같은 출력 디렉터리에 만들어진다. 경로를 compile
    // definition으로 넘기면 backslash 이스케이프 문제가 생기므로 runtime에 찾는다.
    std::u8string test_child_path()
    {
        std::array<wchar_t, MAX_PATH> module_path {};
        const DWORD length { GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size())) };
        REQUIRE(length > 0);

        std::filesystem::path path { std::wstring_view { module_path.data(), static_cast<std::size_t>(length) } };
        path.replace_filename(L"gitman_process_test_child.exe");
        REQUIRE(std::filesystem::exists(path));
        return path.u8string();
    }

    std::u8string windows_directory()
    {
        std::array<wchar_t, MAX_PATH> buffer {};
        const UINT length { GetSystemWindowsDirectoryW(buffer.data(), static_cast<UINT>(buffer.size())) };
        REQUIRE(length > 0);
        return std::filesystem::path { std::wstring_view { buffer.data(), length } }.u8string();
    }

    class temporary_directory_fixture
    {
    public:
        temporary_directory_fixture()
        {
            std::error_code error {};
            const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
            REQUIRE_FALSE(static_cast<bool>(error));

            const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
            for (std::size_t attempt = 0; attempt < 100; ++attempt)
            {
                error.clear();
                // 공백과 한글이 포함된 이름으로 경로 처리도 함께 검증한다.
                const std::filesystem::path candidate { base / (L"gitman 프로세스 test-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
                if (std::filesystem::create_directory(candidate, error))
                {
                    root_ = candidate;
                    break;
                }
            }
            REQUIRE_FALSE(root_.empty());
        }

        ~temporary_directory_fixture()
        {
            std::error_code error {};
            std::filesystem::remove_all(root_, error);
        }

        temporary_directory_fixture(const temporary_directory_fixture&) = delete;
        temporary_directory_fixture(temporary_directory_fixture&&) = delete;
        temporary_directory_fixture& operator=(const temporary_directory_fixture&) = delete;
        temporary_directory_fixture& operator=(temporary_directory_fixture&&) = delete;

        [[nodiscard]] const std::filesystem::path& root() const noexcept
        {
            return root_;
        }

    private:
        std::filesystem::path root_ {};
    };

    // 도우미의 `emit-bytes`에 넘길 hex 문자열을 만든다.
    std::u8string to_hex(const std::string_view bytes)
    {
        constexpr std::u8string_view digits { u8"0123456789ABCDEF" };
        std::u8string text {};
        for (const char value : bytes)
        {
            const auto byte { static_cast<unsigned char>(value) };
            text.push_back(digits[byte >> 4U]);
            text.push_back(digits[byte & 0x0FU]);
        }
        return text;
    }

    // 활성 code page로 인코딩한 byte를 만든다. code page가 UTF-8이면 UTF-8 byte가 되어
    // 같은 test가 두 환경에서 모두 의미를 갖는다.
    std::optional<std::string> to_active_code_page(const std::wstring_view text)
    {
        const int size { WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr) };
        if (size <= 0)
            return std::nullopt;

        std::string bytes(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), bytes.data(), size, nullptr, nullptr) != size)
            return std::nullopt;
        return bytes;
    }

    struct runner_fixture
    {
        runner_fixture()
            : runner { gitman::win32::make_process_runner() }
            , child { test_child_path() }
            , working_directory { windows_directory() }
        {
            REQUIRE(runner != nullptr);
        }

        [[nodiscard]] gitman::process_request request(std::vector<std::u8string> arguments) const
        {
            gitman::process_request value {};
            value.executable = child;
            value.arguments = std::move(arguments);
            value.working_directory = working_directory;
            value.maximum_captured_bytes_per_stream = default_capture_limit;
            return value;
        }

        std::unique_ptr<gitman::process_runner> runner {};
        std::u8string child {};
        std::u8string working_directory {};
    };
} // namespace

TEST_CASE("Child exit codes and durations are reported", "[win32][process][runner]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    const gitman::process_result failure { fixture.runner->run(fixture.request({ u8"exit", u8"42" }), &sink, token) };
    REQUIRE(failure.completion == gitman::process_completion::exited);
    REQUIRE(failure.exit_code.has_value());
    REQUIRE(*failure.exit_code == 42);
    REQUIRE_FALSE(failure.succeeded());
    REQUIRE(failure.diagnostics.empty());
    REQUIRE(failure.masked_command_line.empty() == false);
    REQUIRE(failure.duration.count() >= 0);
    REQUIRE(failure.finished_at >= failure.started_at);
    REQUIRE(sink.records.empty());

    const gitman::process_result success { fixture.runner->run(fixture.request({ u8"exit", u8"0" }), nullptr, token) };
    REQUIRE(success.succeeded());
    REQUIRE(success.record_count == 0);
}

TEST_CASE("Arguments round trip through the command line", "[win32][process][runner]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    const std::vector<std::u8string> arguments { u8"echo-args", u8"plain", u8"with space", u8"quote\"inside", u8"trailing\\", u8"", u8"한글 인자", u8"a&b|c^d" };
    const gitman::process_result result { fixture.runner->run(fixture.request(arguments), &sink, token) };

    REQUIRE(result.succeeded());
    const std::vector<std::u8string> expected {
        u8"[plain]",
        u8"[with space]",
        u8"[quote\"inside]",
        u8"[trailing\\]",
        u8"[]",
        u8"[한글 인자]",
        u8"[a&b|c^d]",
    };
    REQUIRE(sink.texts(gitman::process_stream::standard_output) == expected);
    REQUIRE(result.record_count == expected.size());

    // 실행 단위 sequence는 1부터 증가한다.
    for (std::size_t index = 0; index < sink.records.size(); ++index)
        REQUIRE(sink.records[index].sequence == index + 1);
}

TEST_CASE("The working directory is applied even with spaces and Korean names", "[win32][process][runner][path]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};
    const temporary_directory_fixture directory {};

    recording_sink sink {};
    gitman::process_request request { fixture.request({ u8"echo-cwd" }) };
    request.working_directory = directory.root().u8string();
    const gitman::process_result result { fixture.runner->run(request, &sink, token) };

    REQUIRE(result.succeeded());
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].text == directory.root().u8string());
}

TEST_CASE("Environment overrides set remove and inherit variables", "[win32][process][runner][environment]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};
    REQUIRE(SetEnvironmentVariableW(L"GITMAN_PARENT_VARIABLE", L"parent") != FALSE);

    recording_sink inherited {};
    const gitman::process_result inherited_result { fixture.runner->run(fixture.request({ u8"echo-env", u8"GITMAN_PARENT_VARIABLE" }), &inherited, token) };
    REQUIRE(inherited_result.succeeded());
    REQUIRE(inherited.records.size() == 1);
    REQUIRE(inherited.records[0].text == u8"parent");

    recording_sink applied {};
    gitman::process_request set_request { fixture.request({ u8"echo-env", u8"GITMAN_CHILD_VARIABLE" }) };
    set_request.environment_overrides.push_back({ u8"GITMAN_CHILD_VARIABLE", u8"자식 값" });
    const gitman::process_result set_result { fixture.runner->run(set_request, &applied, token) };
    REQUIRE(set_result.succeeded());
    REQUIRE(applied.records.size() == 1);
    REQUIRE(applied.records[0].text == u8"자식 값");

    recording_sink removed {};
    gitman::process_request remove_request { fixture.request({ u8"echo-env", u8"GITMAN_PARENT_VARIABLE" }) };
    remove_request.environment_overrides.push_back({ u8"GITMAN_PARENT_VARIABLE", std::nullopt });
    const gitman::process_result remove_result { fixture.runner->run(remove_request, &removed, token) };
    REQUIRE(remove_result.succeeded());
    REQUIRE(removed.records.size() == 1);
    REQUIRE(removed.records[0].text == u8"<unset>");

    // 무관한 부모 변수는 override가 있어도 그대로 남는다.
    recording_sink survived {};
    gitman::process_request survive_request { fixture.request({ u8"echo-env", u8"GITMAN_PARENT_VARIABLE" }) };
    survive_request.environment_overrides.push_back({ u8"GITMAN_CHILD_VARIABLE", u8"other" });
    const gitman::process_result survive_result { fixture.runner->run(survive_request, &survived, token) };
    REQUIRE(survive_result.succeeded());
    REQUIRE(survived.records.size() == 1);
    REQUIRE(survived.records[0].text == u8"parent");

    SetEnvironmentVariableW(L"GITMAN_PARENT_VARIABLE", nullptr);
}

TEST_CASE("Large output is captured without deadlocking", "[win32][process][runner][output]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"emit", u8"4000000" }), &sink, token) };

    REQUIRE(result.succeeded());
    REQUIRE(result.captured_bytes >= 4000000);
    REQUIRE_FALSE(result.output_truncated);
    REQUIRE(result.record_count == sink.records.size());
    REQUIRE(sink.records.size() > 1000);
}

TEST_CASE("Capture limits truncate output but keep the run successful", "[win32][process][runner][output]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    gitman::process_request request { fixture.request({ u8"emit", u8"200000" }) };
    request.maximum_captured_bytes_per_stream = 4096;
    const gitman::process_result result { fixture.runner->run(request, &sink, token) };

    REQUIRE(result.completion == gitman::process_completion::exited);
    REQUIRE(result.output_truncated);
    REQUIRE(result.captured_bytes == 4096);
    REQUIRE(result.has_warnings());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.succeeded());
}

TEST_CASE("Both streams are collected with a stable per stream order", "[win32][process][runner][output]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"interleave", u8"40" }), &sink, token) };
    REQUIRE(result.succeeded());

    const std::vector<std::u8string> output { sink.texts(gitman::process_stream::standard_output) };
    const std::vector<std::u8string> errors { sink.texts(gitman::process_stream::standard_error) };
    REQUIRE(output.size() == 40);
    REQUIRE(errors.size() == 40);
    REQUIRE(output.front() == u8"out 0");
    REQUIRE(output.back() == u8"out 39");
    REQUIRE(errors.front() == u8"err 0");
    REQUIRE(errors.back() == u8"err 39");

    std::uint64_t previous { 0 };
    for (const gitman::process_output_record& record : sink.records)
    {
        REQUIRE(record.sequence > previous);
        previous = record.sequence;
    }
}

TEST_CASE("Mixed output reports invalid bytes progress and unterminated tails", "[win32][process][runner][output]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"emit-mixed" }), &sink, token) };
    REQUIRE(result.succeeded());
    REQUIRE(sink.records.size() == 5);

    REQUIRE(sink.records[0].text == u8"한글 line");
    REQUIRE_FALSE(sink.records[0].progress);
    REQUIRE_FALSE(sink.records[0].replaced_invalid_bytes);
    REQUIRE(sink.records[1].replaced_invalid_bytes);
    REQUIRE(sink.records[2].text == u8"progress 10%");
    REQUIRE(sink.records[2].progress);
    REQUIRE(sink.records[3].text == u8"progress 100%");
    REQUIRE(sink.records[3].progress);
    REQUIRE(sink.records[4].text == u8"no newline tail");
    REQUIRE_FALSE(sink.records[4].progress);
}

TEST_CASE("Characters split across pipe reads stay intact", "[win32][process][runner][output][utf8]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"emit-split" }), &sink, token) };
    REQUIRE(result.succeeded());
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].text == u8"한");
    REQUIRE_FALSE(sink.records[0].replaced_invalid_bytes);
}

TEST_CASE("Active code page output is recovered when the fallback is requested", "[win32][process][runner][encoding]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    const std::optional<std::string> encoded { to_active_code_page(L"한글 상태") };
    REQUIRE(encoded.has_value());

    recording_sink sink {};
    gitman::process_request request { fixture.request({ u8"emit-bytes", to_hex(*encoded) + u8"0A" }) };
    request.text_encoding = gitman::process_text_encoding::active_code_page_fallback;
    const gitman::process_result result { fixture.runner->run(request, &sink, token) };

    REQUIRE(result.succeeded());
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].text == u8"한글 상태");
    REQUIRE_FALSE(sink.records[0].replaced_invalid_bytes);
    // 활성 code page가 UTF-8이면 변환 없이 그대로 유효하므로 표시가 서지 않는다.
    REQUIRE(sink.records[0].transcoded_from_active_code_page == (GetACP() != CP_UTF8));
}

TEST_CASE("Active code page output is replaced without the fallback", "[win32][process][runner][encoding]")
{
    if (GetACP() == CP_UTF8)
    {
        WARN("활성 code page가 UTF-8이어서 대체 단정을 건너뛴다.");
        return;
    }

    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    const std::optional<std::string> encoded { to_active_code_page(L"한글 상태") };
    REQUIRE(encoded.has_value());

    recording_sink sink {};
    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"emit-bytes", to_hex(*encoded) + u8"0A" }), &sink, token) };

    REQUIRE(result.succeeded());
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].replaced_invalid_bytes);
    REQUIRE_FALSE(sink.records[0].transcoded_from_active_code_page);
    REQUIRE(sink.records[0].text != u8"한글 상태");
}

TEST_CASE("The fallback leaves valid UTF-8 output untouched", "[win32][process][runner][encoding]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    gitman::process_request request { fixture.request({ u8"echo-args", u8"한글 인자" }) };
    request.text_encoding = gitman::process_text_encoding::active_code_page_fallback;
    const gitman::process_result result { fixture.runner->run(request, &sink, token) };

    REQUIRE(result.succeeded());
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].text == u8"[한글 인자]");
    REQUIRE_FALSE(sink.records[0].transcoded_from_active_code_page);
    REQUIRE_FALSE(sink.records[0].replaced_invalid_bytes);
}

TEST_CASE("A null sink still drains the pipes and counts output", "[win32][process][runner][output]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"emit", u8"200000" }), nullptr, token) };
    REQUIRE(result.succeeded());
    REQUIRE(result.captured_bytes >= 200000);
    REQUIRE(result.record_count > 0);
}

TEST_CASE("Standard input is at end of file so prompts cannot block", "[win32][process][runner]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"read-stdin" }), nullptr, token) };
    REQUIRE(result.succeeded());
}

TEST_CASE("Missing executables report a start failure", "[win32][process][runner][failure]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    gitman::process_request request { fixture.request({ u8"exit", u8"0" }) };
    request.executable = fixture.working_directory + u8"/gitman-missing-child.exe";
    const gitman::process_result result { fixture.runner->run(request, &sink, token) };

    REQUIRE(result.completion == gitman::process_completion::start_failed);
    REQUIRE(result.native_error.has_value());
    REQUIRE(*result.native_error == ERROR_FILE_NOT_FOUND);
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::process_start_failed);
    REQUIRE(result.has_errors());
    REQUIRE(sink.records.empty());
    REQUIRE_FALSE(result.exit_code.has_value());
}

TEST_CASE("Invalid requests never start a process", "[win32][process][runner][failure]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    gitman::process_request request { fixture.request({ u8"exit", u8"0" }) };
    request.executable = u8"gitman_process_test_child.exe";
    const gitman::process_result result { fixture.runner->run(request, &sink, token) };

    REQUIRE(result.completion == gitman::process_completion::invalid_request);
    REQUIRE(result.diagnostics.size() == 1);
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::invalid_process_request);
    // 명령줄을 만들지 않았으므로 기록도 없다.
    REQUIRE(result.masked_command_line.empty());
    REQUIRE(sink.records.empty());
}

TEST_CASE("Credentials are masked in the recorded command line and the output", "[win32][process][runner][masking]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"echo-args", u8"https://user:s3cr3t@example.com/repo.git", u8"--password=hunter2" }), &sink, token) };

    REQUIRE(result.succeeded());
    // 기록용 명령줄에는 비밀이 남지 않지만 자식은 원본 인자를 그대로 받는다.
    REQUIRE(result.masked_command_line.find(u8"s3cr3t") == std::u8string::npos);
    REQUIRE(result.masked_command_line.find(u8"hunter2") == std::u8string::npos);
    REQUIRE(result.masked_command_line.find(u8"https://user:***@example.com/repo.git") != std::u8string::npos);

    REQUIRE(sink.records.size() == 2);
    REQUIRE(sink.records[0].text == u8"[https://user:***@example.com/repo.git]");
    REQUIRE(sink.records[1].text.find(u8"hunter2") == std::u8string::npos);
    REQUIRE(sink.records[1].text.starts_with(u8"[--password=***"));
}

TEST_CASE("Timeouts terminate the child and keep the output collected so far", "[win32][process][runner][timeout]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    recording_sink sink {};
    gitman::process_request request { fixture.request({ u8"sleep", u8"60000" }) };
    request.timeout = std::chrono::milliseconds { 400 };
    const std::chrono::steady_clock::time_point start { std::chrono::steady_clock::now() };
    const gitman::process_result result { fixture.runner->run(request, &sink, token) };
    const std::chrono::milliseconds elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start) };

    REQUIRE(result.completion == gitman::process_completion::timed_out);
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE_FALSE(result.succeeded());
    REQUIRE(result.has_errors());
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::process_timed_out);
    // 자식은 60초를 자므로 곧바로 돌아오지 않으면 종료가 동작하지 않은 것이다.
    REQUIRE(elapsed < std::chrono::milliseconds { 20000 });
    REQUIRE(result.duration >= std::chrono::milliseconds { 400 });
    // 제한 시간 전에 나온 줄은 그대로 전달된다.
    REQUIRE(sink.records.size() == 1);
    REQUIRE(sink.records[0].text == u8"sleeping");
}

TEST_CASE("Cancellation during a run terminates the child", "[win32][process][runner][cancellation]")
{
    const runner_fixture fixture {};
    gitman::process_cancellation_source source {};

    recording_sink sink {};
    const gitman::process_request request { fixture.request({ u8"sleep", u8"60000" }) };
    const std::chrono::steady_clock::time_point start { std::chrono::steady_clock::now() };

    const auto cancel_after_delay = [&source]() {
        std::this_thread::sleep_for(std::chrono::milliseconds { 300 });
        source.request_cancellation();
    };
    std::thread canceller { cancel_after_delay };
    const gitman::process_result result { fixture.runner->run(request, &sink, source.token()) };
    canceller.join();
    const std::chrono::milliseconds elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start) };

    REQUIRE(result.completion == gitman::process_completion::cancelled);
    REQUIRE_FALSE(result.exit_code.has_value());
    REQUIRE_FALSE(result.succeeded());
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::process_cancelled);
    REQUIRE(elapsed < std::chrono::milliseconds { 20000 });
}

TEST_CASE("An already cancelled token never starts a process", "[win32][process][runner][cancellation]")
{
    const runner_fixture fixture {};
    gitman::process_cancellation_source source {};
    source.request_cancellation();

    recording_sink sink {};
    const std::chrono::steady_clock::time_point start { std::chrono::steady_clock::now() };
    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"sleep", u8"60000" }), &sink, source.token()) };
    const std::chrono::milliseconds elapsed { std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start) };

    REQUIRE(result.completion == gitman::process_completion::cancelled);
    REQUIRE(sink.records.empty());
    REQUIRE(result.record_count == 0);
    REQUIRE(result.masked_command_line.empty());
    REQUIRE(elapsed < std::chrono::milliseconds { 5000 });
}

TEST_CASE("Grandchild processes are terminated with the job", "[win32][process][runner][timeout]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};
    const temporary_directory_fixture directory {};

    const std::filesystem::path survivor { directory.root() / L"survivor.marker" };
    const std::filesystem::path victim { directory.root() / L"victim.marker" };

    // 대조군: 손자가 종료 전에 marker를 만들 시간이 있으면 파일이 생긴다. 경로와
    // 손자 생성 자체가 동작한다는 것을 먼저 확인한다.
    gitman::process_request control { fixture.request({ u8"spawn-child", u8"200", survivor.u8string() }) };
    control.timeout = std::chrono::milliseconds { 2000 };
    const gitman::process_result control_result { fixture.runner->run(control, nullptr, token) };
    REQUIRE(control_result.completion == gitman::process_completion::timed_out);
    REQUIRE(std::filesystem::exists(survivor));

    // 손자가 marker를 만들기 전에 트리를 종료하면 이후에도 파일이 생기지 않는다.
    gitman::process_request killed { fixture.request({ u8"spawn-child", u8"2000", victim.u8string() }) };
    killed.timeout = std::chrono::milliseconds { 300 };
    const gitman::process_result killed_result { fixture.runner->run(killed, nullptr, token) };
    REQUIRE(killed_result.completion == gitman::process_completion::timed_out);

    std::this_thread::sleep_for(std::chrono::milliseconds { 3500 });
    REQUIRE_FALSE(std::filesystem::exists(victim));
}

TEST_CASE("A child that finishes before its timeout keeps its exit code", "[win32][process][runner][timeout]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    gitman::process_request request { fixture.request({ u8"exit", u8"7" }) };
    request.timeout = std::chrono::milliseconds { 30000 };
    const gitman::process_result result { fixture.runner->run(request, nullptr, token) };

    REQUIRE(result.completion == gitman::process_completion::exited);
    REQUIRE(result.exit_code.has_value());
    REQUIRE(*result.exit_code == 7);
    REQUIRE(result.duration < std::chrono::milliseconds { 30000 });
}

TEST_CASE("An unused cancellation source does not disturb a normal run", "[win32][process][runner][cancellation]")
{
    const runner_fixture fixture {};
    gitman::process_cancellation_source source {};

    const gitman::process_result result { fixture.runner->run(fixture.request({ u8"emit", u8"4096" }), nullptr, source.token()) };
    REQUIRE(result.succeeded());
    REQUIRE(result.record_count > 0);
    REQUIRE_FALSE(source.cancellation_requested());
}

TEST_CASE("Repeated runs do not leak process handles", "[win32][process][runner][threading]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    DWORD before { 0 };
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &before) != FALSE);
    for (std::size_t index = 0; index < 20; ++index)
        REQUIRE(fixture.runner->run(fixture.request({ u8"exit", u8"0" }), nullptr, token).succeeded());

    DWORD after { 0 };
    REQUIRE(GetProcessHandleCount(GetCurrentProcess(), &after) != FALSE);
    // 실행마다 process, thread, pipe, job과 event handle을 열고 닫는다. 누수가 있으면
    // 20회 반복에서 수십 개가 남는다.
    REQUIRE(after <= before + 8);
}

TEST_CASE("One runner instance serves concurrent runs", "[win32][process][runner][threading]")
{
    const runner_fixture fixture {};
    const gitman::process_cancellation_token token {};

    constexpr std::size_t worker_count { 4 };
    std::array<gitman::process_result, worker_count> results {};
    std::array<recording_sink, worker_count> sinks {};
    std::vector<std::thread> workers {};
    workers.reserve(worker_count);

    for (std::size_t index = 0; index < worker_count; ++index)
        workers.emplace_back([&fixture, &results, &sinks, &token, index]() {
            const gitman::process_request request { fixture.request({ u8"emit", u8"120000" }) };
            results[index] = fixture.runner->run(request, &sinks[index], token);
        });
    for (std::thread& worker : workers)
        worker.join();

    for (std::size_t index = 0; index < worker_count; ++index)
    {
        REQUIRE(results[index].succeeded());
        REQUIRE(results[index].captured_bytes >= 120000);
        REQUIRE(results[index].record_count == sinks[index].records.size());
        // 실행마다 독립된 sequence를 부여한다.
        REQUIRE(sinks[index].records.front().sequence == 1);
    }
}
