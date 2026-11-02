#include "domain/function_button.h"
#include "domain/project.h"
#include "infrastructure/function_execution_policy.h"
#include "platform/win32/win32_process_launcher.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <vector>

// 운영 launcher는 `CREATE_NO_WINDOW`를 주지 않으므로 (설계 4.1), 이 파일의 test가 띄우는
// 콘솔 자식은 gitman_tests를 콘솔 없이 돌릴 때(IDE test explorer 등) 콘솔 창을 잠깐 띄울
// 수 있다. 콘솔에서 돌리면 그 콘솔을 함께 쓴다. test를 위해 운영 플래그를 바꾸지 않는다.

namespace {
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
                // 공백과 한글이 든 작업 폴더도 그대로 넘어가는지 함께 본다.
                const std::filesystem::path candidate { base / (L"gitman 분리 실행 test-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
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

    // test 프로세스의 환경 변수를 잠시 바꾸고 원래대로 되돌린다. 분리 실행은 이 값을 부모
    // 환경으로 물려받는다.
    class scoped_environment_variable
    {
    public:
        scoped_environment_variable(const wchar_t* const name, const wchar_t* const value)
            : name_ { name }
        {
            std::wstring buffer(32u * 1024u, L'\0');
            const DWORD length { GetEnvironmentVariableW(name, buffer.data(), static_cast<DWORD>(buffer.size())) };
            if (length != 0)
                previous_ = buffer.substr(0, static_cast<std::size_t>(length));
            SetEnvironmentVariableW(name, value);
        }

        ~scoped_environment_variable()
        {
            SetEnvironmentVariableW(name_.c_str(), previous_.has_value() ? previous_->c_str() : nullptr);
        }

        scoped_environment_variable(const scoped_environment_variable&) = delete;
        scoped_environment_variable(scoped_environment_variable&&) = delete;
        scoped_environment_variable& operator=(const scoped_environment_variable&) = delete;
        scoped_environment_variable& operator=(scoped_environment_variable&&) = delete;

    private:
        std::wstring name_ {};
        std::optional<std::wstring> previous_ {};
    };

    // 분리 실행은 기다리지 않으므로 자식이 남긴 보고 파일을 일정 시간 기다린다.
    std::optional<std::string> wait_for_report(const std::filesystem::path& path)
    {
        const auto deadline { std::chrono::steady_clock::now() + std::chrono::seconds { 10 } };
        while (std::chrono::steady_clock::now() < deadline)
        {
            std::error_code error {};
            if (std::filesystem::exists(path, error))
            {
                std::ifstream stream { path, std::ios::binary };
                return std::string { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
            }
            std::this_thread::sleep_for(std::chrono::milliseconds { 20 });
        }
        return std::nullopt;
    }

    std::string as_bytes(const std::u8string_view text)
    {
        return std::string { text.begin(), text.end() };
    }
} // namespace

TEST_CASE("A detached launch runs the absolute program with its cwd and GITMAN environment", "[function_buttons][t4]")
{
    const temporary_directory_fixture directory {};
    const std::filesystem::path working_directory { directory.root() / L"작업 폴더" };
    REQUIRE(std::filesystem::create_directory(working_directory));
    const std::filesystem::path report { directory.root() / L"report.txt" };

    gitman::project_definition project {};
    project.id = { u8"frontend" };
    project.path.original = u8"frontend";
    project.path.normalized = working_directory.u8string();
    project.display_name = u8"Frontend Web";
    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"E:/repos/team.version-list", project, nullptr) };

    // 조회 전이라 `GITMAN_BRANCH`는 내보내지 않는다. 부모가 물려준 낡은 값도 자식에게 가지 않아야 한다.
    const scoped_environment_variable stale_branch { L"GITMAN_BRANCH", L"stale" };
    const scoped_environment_variable no_prompt { L"GIT_TERMINAL_PROMPT", nullptr };

    gitman::function_command command {};
    command.program = test_child_path();
    command.arguments = {
        u8"write-report",
        report.u8string(),
        u8"GITMAN_PROJECT_ID",
        u8"GITMAN_PROJECT_ROOT",
        u8"GITMAN_PROJECT_NAME",
        u8"GITMAN_WORKSPACE_FOLDER",
        u8"GITMAN_BRANCH",
        u8"GIT_TERMINAL_PROMPT",
    };
    command.working_directory = working_directory.u8string();

    const std::unique_ptr<gitman::process_launcher> launcher { gitman::win32::make_process_launcher() };
    const gitman::process_launch_result result { launcher->launch(gitman::make_function_launch_request(command, builtins)) };
    REQUIRE(result.started());
    REQUIRE(result.diagnostics.empty());

    const std::optional<std::string> written { wait_for_report(report) };
    REQUIRE(written.has_value());
    const std::string expected {
        "cwd=" + as_bytes(working_directory.u8string()) + "\n"
        + "GITMAN_PROJECT_ID=frontend\n"
        + "GITMAN_PROJECT_ROOT=" + as_bytes(working_directory.u8string()) + "\n"
        + "GITMAN_PROJECT_NAME=Frontend Web\n"
        + "GITMAN_WORKSPACE_FOLDER=E:\\repos\n"
        + "GITMAN_BRANCH=<unset>\n"
        + "GIT_TERMINAL_PROMPT=<unset>\n"
    };
    REQUIRE(*written == expected);
}

TEST_CASE("A detached launch returns without waiting for the child", "[function_buttons][t4]")
{
    // 자식이 test보다 오래 살므로 지울 임시 폴더를 작업 폴더로 잡지 않는다. 작업 폴더로 쓰는
    // 동안에는 그 폴더를 지울 수 없다.
    std::array<wchar_t, MAX_PATH> windows_directory {};
    const UINT windows_length { GetSystemWindowsDirectoryW(windows_directory.data(), static_cast<UINT>(windows_directory.size())) };
    REQUIRE(windows_length > 0);

    gitman::process_launch_request request {};
    request.executable = test_child_path();
    // 자식은 10초를 자고, 판정은 2초 안에 돌아왔는지만 본다. `CreateProcessW`가 느린
    // 기계에서도 거짓 실패하지 않도록 둘 사이를 넉넉히 벌린다.
    request.arguments = { u8"hold-handles", u8"10000" };
    request.working_directory = std::filesystem::path { std::wstring_view { windows_directory.data(), windows_length } }.u8string();

    const std::unique_ptr<gitman::process_launcher> launcher { gitman::win32::make_process_launcher() };
    const auto started { std::chrono::steady_clock::now() };
    const gitman::process_launch_result result { launcher->launch(request) };
    const auto elapsed { std::chrono::steady_clock::now() - started };
    REQUIRE(result.started());
    REQUIRE(elapsed < std::chrono::milliseconds { 2000 });
}

TEST_CASE("A detached launch does not infer a missing extension", "[function_buttons][t4]")
{
    const temporary_directory_fixture directory {};
    std::u8string executable { test_child_path() };
    REQUIRE(executable.ends_with(u8".exe"));
    executable.resize(executable.size() - 4);

    gitman::process_launch_request request {};
    request.executable = executable;
    request.arguments = { u8"exit", u8"0" };
    request.working_directory = directory.root().u8string();

    const std::unique_ptr<gitman::process_launcher> launcher { gitman::win32::make_process_launcher() };
    const gitman::process_launch_result result { launcher->launch(request) };
    REQUIRE(result.status == gitman::process_launch_status::start_failed);
    REQUIRE(result.native_error == std::optional<std::uint32_t> { ERROR_FILE_NOT_FOUND });
    REQUIRE(result.diagnostics.size() == 1u);
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::process_start_failed);
}

TEST_CASE("A detached launch rejects relative paths before starting", "[function_buttons][t4]")
{
    const std::unique_ptr<gitman::process_launcher> launcher { gitman::win32::make_process_launcher() };

    gitman::process_launch_request relative_program {};
    relative_program.executable = u8"gitman_process_test_child.exe";
    relative_program.working_directory = u8"C:\\";
    const gitman::process_launch_result program_result { launcher->launch(relative_program) };
    REQUIRE(program_result.status == gitman::process_launch_status::invalid_request);
    REQUIRE(program_result.diagnostics.front().code == gitman::diagnostic_code::invalid_process_request);

    gitman::process_launch_request relative_directory {};
    relative_directory.executable = test_child_path();
    relative_directory.working_directory = u8"tools";
    const gitman::process_launch_result directory_result { launcher->launch(relative_directory) };
    REQUIRE(directory_result.status == gitman::process_launch_status::invalid_request);
}

TEST_CASE("A detached launch reports a missing working directory as a start failure", "[function_buttons][t4]")
{
    const temporary_directory_fixture directory {};
    gitman::process_launch_request request {};
    request.executable = test_child_path();
    request.arguments = { u8"exit", u8"0" };
    request.working_directory = (directory.root() / L"없는 폴더").u8string();

    const std::unique_ptr<gitman::process_launcher> launcher { gitman::win32::make_process_launcher() };
    const gitman::process_launch_result result { launcher->launch(request) };
    REQUIRE(result.status == gitman::process_launch_status::start_failed);
    REQUIRE(result.native_error.has_value());
}
