#include "helpers/git_repository_fixture.h"

#include "application/process_cancellation.h"
#include "application/process_request.h"
#include "domain/project.h"
#include "infrastructure/vcs_command_runner.h"
#include "infrastructure/vcs_execution_policy.h"
#include "infrastructure/vcs_tool_discovery.h"
#include "platform/win32/win32_process_runner.h"
#include "platform/win32/win32_vcs_file_probe.h"

#include <chrono>
#include <cstddef>
#include <fstream>
#include <system_error>
#include <utility>

namespace gitman::testing {
    namespace {
        std::filesystem::path to_path(const std::u8string_view value)
        {
            return std::filesystem::path { std::u8string { value } };
        }

        std::u8string to_utf8(const std::filesystem::path& value)
        {
            return value.u8string();
        }

        void set_variable(std::vector<process_environment_override>& overrides, const std::u8string_view name, const std::u8string_view value)
        {
            overrides.push_back({ std::u8string { name }, std::u8string { value } });
        }

        // 호스트의 사용자 설정과 분리한다. 전역 설정을 그대로 쓰면 `core.autocrlf`나
        // 서명 설정에 따라 저장소 상태가 달라져 test가 환경에 의존하게 된다.
        void add_isolation_overrides(std::vector<process_environment_override>& overrides, const std::u8string_view root)
        {
            set_variable(overrides, u8"HOME", root);
            set_variable(overrides, u8"USERPROFILE", root);
            set_variable(overrides, u8"XDG_CONFIG_HOME", root);
            set_variable(overrides, u8"GIT_CONFIG_NOSYSTEM", u8"1");
            set_variable(overrides, u8"GIT_CONFIG_GLOBAL", std::u8string { root } + u8"\\absent-gitconfig");

            // 커밋 저자와 시각을 고정해 같은 준비가 항상 같은 저장소를 만들게 한다.
            set_variable(overrides, u8"GIT_AUTHOR_NAME", u8"gitman test");
            set_variable(overrides, u8"GIT_AUTHOR_EMAIL", u8"gitman@example.invalid");
            set_variable(overrides, u8"GIT_AUTHOR_DATE", u8"2026-01-01T00:00:00+00:00");
            set_variable(overrides, u8"GIT_COMMITTER_NAME", u8"gitman test");
            set_variable(overrides, u8"GIT_COMMITTER_EMAIL", u8"gitman@example.invalid");
            set_variable(overrides, u8"GIT_COMMITTER_DATE", u8"2026-01-01T00:00:00+00:00");
        }

        std::vector<std::u8string> with_fixed_configuration(std::vector<std::u8string> arguments)
        {
            std::vector<std::u8string> full {};
            full.push_back(u8"-c");
            full.push_back(u8"core.autocrlf=false");
            full.push_back(u8"-c");
            full.push_back(u8"commit.gpgsign=false");
            full.push_back(u8"-c");
            full.push_back(u8"init.defaultBranch=main");
            full.reserve(full.size() + arguments.size());
            for (std::u8string& argument : arguments)
                full.push_back(std::move(argument));
            return full;
        }
    } // namespace

    git_repository_fixture::git_repository_fixture()
    {
        std::error_code error {};
        const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
        if (static_cast<bool>(error))
            return;

        const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
        for (std::size_t attempt = 0; attempt < 100; ++attempt)
        {
            error.clear();
            const std::filesystem::path candidate { base / (L"gitman-git-tests-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
            if (std::filesystem::create_directory(candidate, error))
            {
                root_ = candidate;
                break;
            }
        }
        if (root_.empty())
            return;

        runner_ = win32::make_process_runner();
        probe_ = win32::make_vcs_file_probe();
        if (runner_ == nullptr || probe_ == nullptr)
            return;

        const vcs_tool_set tools { resolve_vcs_tools({}, win32::current_vcs_tool_environment(), *runner_, *probe_, {}) };
        tool_ = tools.git;
    }

    git_repository_fixture::~git_repository_fixture()
    {
        if (root_.empty())
            return;

        // 실패한 test에서도 임시 저장소가 남지 않게 한다.
        std::error_code error {};
        std::filesystem::remove_all(root_, error);
    }

    bool git_repository_fixture::available() const noexcept
    {
        return root_.empty() == false && runner_ != nullptr && probe_ != nullptr && tool_.usable();
    }

    const vcs_tool_info& git_repository_fixture::tool() const noexcept
    {
        return tool_;
    }

    process_runner& git_repository_fixture::runner() const noexcept
    {
        return *runner_;
    }

    const vcs_file_probe& git_repository_fixture::probe() const noexcept
    {
        return *probe_;
    }

    std::u8string git_repository_fixture::path_of(const std::u8string_view relative) const
    {
        return to_utf8(root_ / to_path(relative));
    }

    std::u8string git_repository_fixture::make_directory(const std::u8string_view relative)
    {
        const std::filesystem::path path { root_ / to_path(relative) };
        std::error_code error {};
        std::filesystem::create_directories(path, error);
        if (static_cast<bool>(error))
            failures_.push_back(u8"디렉터리를 만들지 못했습니다: " + to_utf8(path));
        return to_utf8(path);
    }

    std::u8string git_repository_fixture::make_repository(const std::u8string_view relative)
    {
        const std::u8string path { make_directory(relative) };
        git(path, { u8"init", u8"-b", u8"main" });
        return path;
    }

    std::u8string git_repository_fixture::make_bare_repository(const std::u8string_view relative)
    {
        const std::u8string path { make_directory(relative) };
        git(path, { u8"init", u8"--bare", u8"-b", u8"main" });
        return path;
    }

    void git_repository_fixture::write_file(const std::u8string_view directory, const std::u8string_view relative, const std::string_view content)
    {
        const std::filesystem::path path { to_path(directory) / to_path(relative) };
        std::error_code error {};
        std::filesystem::create_directories(path.parent_path(), error);

        std::ofstream stream { path, std::ios::binary | std::ios::trunc };
        if (stream.is_open() == false)
        {
            failures_.push_back(u8"파일을 만들지 못했습니다: " + to_utf8(path));
            return;
        }
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        stream.close();
        if (stream.good() == false)
            failures_.push_back(u8"파일을 기록하지 못했습니다: " + to_utf8(path));
    }

    void git_repository_fixture::git(const std::u8string_view working_directory, std::vector<std::u8string> arguments)
    {
        static_cast<void>(execute(working_directory, std::move(arguments), true));
    }

    int git_repository_fixture::git_allowing_failure(const std::u8string_view working_directory, std::vector<std::u8string> arguments)
    {
        return execute(working_directory, std::move(arguments), false);
    }

    const std::vector<std::u8string>& git_repository_fixture::failures() const noexcept
    {
        return failures_;
    }

    int git_repository_fixture::execute(const std::u8string_view working_directory, std::vector<std::u8string> arguments, const bool record_failure)
    {
        if (available() == false)
            return -1;

        std::u8string description { arguments.empty() ? std::u8string {} : arguments.front() };
        process_request request {
            make_vcs_process_request(repository_kind::git, tool_.executable, working_directory, with_fixed_configuration(std::move(arguments)), vcs_command_class::update),
        };
        add_isolation_overrides(request.environment_overrides, to_utf8(root_));

        const vcs_command_result result { run_vcs_command(*runner_, request, {}) };
        if (result.succeeded())
            return 0;

        if (record_failure)
        {
            std::u8string message { u8"git " + description + u8" 실패: " };
            message.append(result.standard_error_text());
            failures_.push_back(std::move(message));
        }
        return result.process.exit_code.value_or(-1);
    }
} // namespace gitman::testing
