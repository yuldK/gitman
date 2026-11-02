#include "domain/function_button.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "infrastructure/function_execution_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
    gitman::project_definition make_project()
    {
        gitman::project_definition project {};
        project.id = { u8"frontend" };
        project.path.original = u8"frontend";
        project.path.normalized = u8"E:/repos/frontend";
        project.display_name = u8"Frontend Web";
        return project;
    }

    gitman::repository_snapshot make_git_snapshot()
    {
        gitman::repository_snapshot snapshot {};
        snapshot.kind = gitman::repository_kind::git;
        snapshot.repository_root = u8"E:/repos/frontend";
        snapshot.current_reference = u8"feature/login";
        snapshot.local_revision = u8"a1b2c3d";
        return snapshot;
    }

    gitman::repository_snapshot make_svn_snapshot()
    {
        gitman::repository_snapshot snapshot {};
        snapshot.kind = gitman::repository_kind::subversion;
        snapshot.repository_root = u8"E:/repos/frontend";
        snapshot.current_reference = u8"^/trunk";
        snapshot.local_revision = u8"1234";
        snapshot.svn_repository_root = u8"https://svn.example.com/repo";
        return snapshot;
    }

    gitman::function_command make_command()
    {
        gitman::function_command command {};
        command.program = u8"C:\\Program Files\\CMake\\bin\\cmake.exe";
        command.arguments = { u8"--build", u8"E:\\repos\\frontend\\build", u8"--config", u8"Debug" };
        command.working_directory = u8"E:\\repos\\frontend";
        return command;
    }

    // 값이 없으면 override가 없고, 있으면 그 override의 값(없으면 지우기)이다.
    std::optional<std::optional<std::u8string>> override_of(const std::vector<gitman::process_environment_override>& overrides, const std::u8string_view name)
    {
        const auto found {
            std::ranges::find_if(overrides
                , [name](const gitman::process_environment_override& entry)
                {
                    return entry.name == name;
                }
            )
        };
        if (found == overrides.end())
            return std::nullopt;
        return found->value;
    }
} // namespace

TEST_CASE("Capture requests use the function policy instead of the VCS policy", "[function_buttons][t4]")
{
    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"E:/repos/team.version-list", make_project(), nullptr) };
    const gitman::process_request request { gitman::make_function_process_request(make_command(), 1800, builtins) };

    const gitman::function_command command { make_command() };
    REQUIRE(request.executable == command.program);
    REQUIRE(request.arguments == command.arguments);
    REQUIRE(request.working_directory == command.working_directory);
    REQUIRE(request.timeout == std::optional<std::chrono::milliseconds> { std::chrono::milliseconds { 1'800'000 } });
    REQUIRE(request.maximum_captured_bytes_per_stream == 8u * 1024u * 1024u);
    REQUIRE(request.maximum_record_bytes == gitman::default_process_record_byte_limit);
    REQUIRE(request.text_encoding == gitman::process_text_encoding::active_code_page_fallback);
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("A zero timeout becomes an unlimited request", "[function_buttons][t4]")
{
    REQUIRE_FALSE(gitman::function_timeout(0).has_value());
    REQUIRE(gitman::function_timeout(gitman::default_function_timeout_seconds) == std::optional<std::chrono::milliseconds> { std::chrono::milliseconds { 3'600'000 } });
    REQUIRE(gitman::function_timeout(gitman::maximum_function_timeout_seconds) == std::optional<std::chrono::milliseconds> { std::chrono::milliseconds { 86'400'000 } });

    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"E:/repos/team.version-list", make_project(), nullptr) };
    const gitman::process_request request { gitman::make_function_process_request(make_command(), 0, builtins) };
    REQUIRE_FALSE(request.timeout.has_value());
    // 0 ms 요청은 거부되므로 무제한이 검증을 통과하는지도 본다.
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Capture exports GITMAN variables and disables the git terminal prompt", "[function_buttons][t4]")
{
    const gitman::repository_snapshot snapshot { make_git_snapshot() };
    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"E:/repos/team.version-list", make_project(), &snapshot) };
    const gitman::process_request request { gitman::make_function_process_request(make_command(), 60, builtins) };
    const std::vector<gitman::process_environment_override>& overrides { request.environment_overrides };

    REQUIRE(override_of(overrides, u8"GITMAN_WORKSPACE_FOLDER") == std::optional<std::optional<std::u8string>> { u8"E:\\repos" });
    REQUIRE(override_of(overrides, u8"GITMAN_WORKSPACE_FILE") == std::optional<std::optional<std::u8string>> { u8"E:\\repos\\team.version-list" });
    REQUIRE(override_of(overrides, u8"GITMAN_PROJECT_ROOT") == std::optional<std::optional<std::u8string>> { u8"E:\\repos\\frontend" });
    REQUIRE(override_of(overrides, u8"GITMAN_PROJECT_ID") == std::optional<std::optional<std::u8string>> { u8"frontend" });
    REQUIRE(override_of(overrides, u8"GITMAN_PROJECT_NAME") == std::optional<std::optional<std::u8string>> { u8"Frontend Web" });
    REQUIRE(override_of(overrides, u8"GITMAN_VCS") == std::optional<std::optional<std::u8string>> { u8"git" });
    REQUIRE(override_of(overrides, u8"GITMAN_REPOSITORY_ROOT") == std::optional<std::optional<std::u8string>> { u8"E:\\repos\\frontend" });
    REQUIRE(override_of(overrides, u8"GITMAN_BRANCH") == std::optional<std::optional<std::u8string>> { u8"feature/login" });
    REQUIRE(override_of(overrides, u8"GITMAN_REVISION") == std::optional<std::optional<std::u8string>> { u8"a1b2c3d" });
    REQUIRE(override_of(overrides, u8"GIT_TERMINAL_PROMPT") == std::optional<std::optional<std::u8string>> { u8"0" });
    REQUIRE(overrides.size() == 10u);
}

TEST_CASE("Detached launches export GITMAN variables without the prompt override", "[function_buttons][t4]")
{
    const gitman::repository_snapshot snapshot { make_git_snapshot() };
    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"E:/repos/team.version-list", make_project(), &snapshot) };
    const gitman::process_launch_request request { gitman::make_function_launch_request(make_command(), builtins) };

    const gitman::function_command command { make_command() };
    REQUIRE(request.executable == command.program);
    REQUIRE(request.arguments == command.arguments);
    REQUIRE(request.working_directory == command.working_directory);
    REQUIRE(override_of(request.environment_overrides, u8"GITMAN_PROJECT_ID") == std::optional<std::optional<std::u8string>> { u8"frontend" });
    REQUIRE(override_of(request.environment_overrides, u8"GITMAN_BRANCH") == std::optional<std::optional<std::u8string>> { u8"feature/login" });
    REQUIRE_FALSE(override_of(request.environment_overrides, u8"GIT_TERMINAL_PROMPT").has_value());
    REQUIRE(gitman::validate_process_launch_request(request).empty());
}

TEST_CASE("Runtime GITMAN variables are removed before a local query", "[function_buttons][t4]")
{
    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"E:/repos/team.version-list", make_project(), nullptr) };
    const std::vector<gitman::process_environment_override> overrides { gitman::function_environment_overrides(builtins, gitman::function_run_mode::capture) };

    REQUIRE(override_of(overrides, u8"GITMAN_PROJECT_ID") == std::optional<std::optional<std::u8string>> { u8"frontend" });
    for (const std::u8string_view name : { u8"GITMAN_VCS", u8"GITMAN_REPOSITORY_ROOT", u8"GITMAN_BRANCH", u8"GITMAN_REVISION" })
    {
        const std::optional<std::optional<std::u8string>> entry { override_of(overrides, name) };
        REQUIRE(entry.has_value());
        REQUIRE_FALSE(entry->has_value());
    }
}

TEST_CASE("The svn URL is never exported", "[function_buttons][t4]")
{
    const gitman::repository_snapshot snapshot { make_svn_snapshot() };
    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"E:/repos/team.version-list", make_project(), &snapshot) };
    const std::vector<gitman::process_environment_override> overrides { gitman::function_environment_overrides(builtins, gitman::function_run_mode::detached) };

    REQUIRE(override_of(overrides, u8"GITMAN_VCS") == std::optional<std::optional<std::u8string>> { u8"svn" });
    // svn에는 브랜치 이름이 없다 (3.3). 값이 없으니 지운다.
    const std::optional<std::optional<std::u8string>> branch { override_of(overrides, u8"GITMAN_BRANCH") };
    REQUIRE(branch.has_value());
    REQUIRE_FALSE(branch->has_value());
    for (const gitman::process_environment_override& entry : overrides)
        REQUIRE(entry.name.find(u8"SVN") == std::u8string::npos);
}
