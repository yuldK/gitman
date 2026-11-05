#include "platform/win32/win32_cmd_target_inspector.h"
#include "platform/win32/win32_file_association.h"
#include "helpers/discovery_test_doubles.h"

#include <catch2/catch_test_macros.hpp>
#include <windows.h>
#include <winioctl.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <string>
#include <utility>
#include <vector>

namespace {
    std::u8string app_path()
    {
        const std::filesystem::path executable { gitman::win32::current_executable_path() };
        const auto configuration { executable.parent_path().filename() };
        return (executable.parent_path().parent_path().parent_path() / "src" / configuration / "gitman.exe").u8string();
    }

    void write_batch(const std::u8string& path)
    {
        std::ofstream stream { std::filesystem::path { path }, std::ios::binary };
        stream << "@echo off\r\nsetlocal EnableDelayedExpansion\r\ncall other.cmd\r\n";
    }

    gitman::cmd_target_query query_for(const std::u8string& path)
    {
        gitman::cmd_target_query query {};
        query.working_directory = std::filesystem::path { path }.parent_path().u8string();
        query.batch_if_path = path;
        query.argument = 2;
        return query;
    }

    class held_batch
    {
    public:
        explicit held_batch(const std::u8string& path)
        {
            file_ = CreateFileW(std::filesystem::path { path }.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE
                , nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr
            );
            request_.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            DWORD bytes {};
            if (file_ != INVALID_HANDLE_VALUE && request_.hEvent != nullptr)
                armed_ = DeviceIoControl(file_, FSCTL_REQUEST_OPLOCK_LEVEL_1, nullptr, 0, nullptr, 0, &bytes, &request_) == FALSE && GetLastError() == ERROR_IO_PENDING;
        }

        ~held_batch()
        {
            release();
            if (request_.hEvent != nullptr)
                CloseHandle(request_.hEvent);
        }

        bool armed() const noexcept
        {
            return armed_;
        }

        bool wait_until_blocked() const noexcept
        {
            return WaitForSingleObject(request_.hEvent, 3000) == WAIT_OBJECT_0;
        }

        void release() noexcept
        {
            if (file_ != INVALID_HANDLE_VALUE)
            {
                CancelIoEx(file_, &request_);
                CloseHandle(file_);
                file_ = INVALID_HANDLE_VALUE;
            }
        }

    private:
        HANDLE file_ { INVALID_HANDLE_VALUE };
        OVERLAPPED request_ {};
        bool armed_ { false };
    };
}

TEST_CASE("An isolated cmd probe preserves Unicode paths and batch findings", "[cmd_inspection][win32]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const auto path { directory.path_of(u8"한글 & space.bat") };
    write_batch(path);
    auto query { query_for(path) };
    SECTION("상대 경로가 없는 조회")
    {
    }
    SECTION("공백이 든 상대 경로도 원문대로 나름")
    {
        query.relative_path = u8"한글 & space.bat";
        query.sentence = true;
    }
    const auto inspector { gitman::win32::make_cmd_target_inspector(app_path()) };
    const auto result { inspector->inspect(query, {}) };
    REQUIRE(result.outcome == gitman::cmd_inspection_outcome::completed);
    REQUIRE(result.facts.query == query);
    REQUIRE(result.facts.path_exists == query.relative_path.has_value());
    REQUIRE(result.facts.batch == gitman::cmd_batch_findings { true, true, false });
}

TEST_CASE("A cancelled isolated cmd probe releases the caller before a held open is released", "[cmd_inspection][win32]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const auto path { directory.path_of(u8"held.bat") };
    write_batch(path);
    held_batch held { path };
    REQUIRE(held.armed());
    const auto inspector { gitman::win32::make_cmd_target_inspector(app_path(), std::chrono::seconds { 3 }) };
    gitman::process_cancellation_source cancellation {};
    auto pending { std::async(std::launch::async, [&]
        {
            return inspector->inspect(query_for(path), cancellation.token());
        })
    };
    REQUIRE(held.wait_until_blocked());
    cancellation.request_cancellation();
    const bool returned { pending.wait_for(std::chrono::seconds { 2 }) == std::future_status::ready };
    if (returned == false)
        held.release();
    REQUIRE(returned);
    REQUIRE(pending.get().outcome == gitman::cmd_inspection_outcome::cancelled);
    held.release();
    REQUIRE(inspector->inspect(query_for(path), {}).outcome == gitman::cmd_inspection_outcome::completed);
}

TEST_CASE("An isolated cmd probe times out while a file open is held", "[cmd_inspection][win32]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const auto path { directory.path_of(u8"held.bat") };
    write_batch(path);
    held_batch held { path };
    REQUIRE(held.armed());
    const auto inspector { gitman::win32::make_cmd_target_inspector(app_path(), std::chrono::seconds { 1 }) };
    auto pending { std::async(std::launch::async, [&]
        {
            return inspector->inspect(query_for(path), {});
        })
    };
    REQUIRE(held.wait_until_blocked());
    const bool returned { pending.wait_for(std::chrono::seconds { 3 }) == std::future_status::ready };
    if (returned == false)
        held.release();
    REQUIRE(returned);
    REQUIRE(pending.get().outcome == gitman::cmd_inspection_outcome::timed_out);
}

TEST_CASE("A missing loopback UNC share does not hold an isolated cmd probe caller", "[cmd_inspection][win32]")
{
    const auto inspector { gitman::win32::make_cmd_target_inspector(app_path(), std::chrono::milliseconds { 300 }) };
    const auto query { query_for(u8"\\\\127.0.0.1\\gitman-no-such-share\\missing.bat") };
    const auto start { std::chrono::steady_clock::now() };
    const auto result { inspector->inspect(query, {}) };
    REQUIRE(std::chrono::steady_clock::now() - start < std::chrono::seconds { 3 });
    REQUIRE((result.outcome == gitman::cmd_inspection_outcome::completed || result.outcome == gitman::cmd_inspection_outcome::timed_out));
    REQUIRE(result.facts.batch.has_value() == false);
}

TEST_CASE("Internal cmd probe dispatch rejects bad handles without app startup", "[cmd_inspection][win32]")
{
    const std::vector<std::u8string> normal { u8"gitman.exe", u8"--smoke-test" };
    REQUIRE(gitman::win32::run_cmd_target_probe(normal).has_value() == false);
    const std::vector<std::u8string> invalid { u8"gitman.exe", u8"--internal-cmd-target-probe", u8"0", u8"0" };
    REQUIRE(gitman::win32::run_cmd_target_probe(invalid) == 2);
    const auto inspector { gitman::win32::make_cmd_target_inspector(app_path()) };
    gitman::process_cancellation_source cancelled {};
    cancelled.request_cancellation();
    REQUIRE(inspector->inspect({}, cancelled.token()).outcome == gitman::cmd_inspection_outcome::cancelled);
}

TEST_CASE("A cmd probe does not try to launch its helper from a UNC executable", "[cmd_inspection][win32]")
{
    const auto inspector { gitman::win32::make_cmd_target_inspector(u8"\\\\127.0.0.1\\missing\\gitman.exe") };
    REQUIRE(inspector->inspect({}, {}).outcome == gitman::cmd_inspection_outcome::failed);
}
