#include "application/vcs_tool_registry.h"
#include "domain/project.h"
#include "domain/vcs_tool.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/vcs_tool_discovery.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {
    constexpr std::u8string_view git_path { u8"C:\\tools\\git\\cmd\\git.exe" };
    constexpr std::u8string_view svn_path { u8"C:\\tools\\svn\\bin\\svn.exe" };

    gitman::vcs_tool_environment make_environment()
    {
        gitman::vcs_tool_environment environment {};
        environment.path_environment = u8"C:\\tools\\git\\cmd;C:\\tools\\svn\\bin";
        environment.program_files_directories.push_back(u8"C:\\Program Files");
        return environment;
    }

    gitman::testing::fake_process_runner::response version_response(const std::u8string_view banner)
    {
        gitman::testing::fake_process_runner::response value {};
        value.standard_output = banner;
        return value;
    }

    bool contains_diagnostic(const std::vector<gitman::diagnostic>& diagnostics, const gitman::diagnostic_code code)
    {
        for (const gitman::diagnostic& value : diagnostics)
            if (value.code == code)
                return true;
        return false;
    }

    bool all_warnings(const std::vector<gitman::diagnostic>& diagnostics)
    {
        for (const gitman::diagnostic& value : diagnostics)
            if (value.severity != gitman::diagnostic_severity::warning)
                return false;
        return diagnostics.empty() == false;
    }
} // namespace

TEST_CASE("Search path splitting drops unusable and duplicate entries", "[infrastructure][vcs][discovery]")
{
    const std::vector<std::u8string> directories {
        gitman::split_search_path(u8"C:\\a;;\"C:\\Program Files\\b\";relative\\dir;C:\\A\\;D:/c;  C:\\d  "),
    };

    REQUIRE(directories.size() == 4);
    REQUIRE(directories[0] == u8"C:\\a");
    // 따옴표는 벗기고 공백이 포함된 경로는 그대로 남긴다.
    REQUIRE(directories[1] == u8"C:\\Program Files\\b");
    // `relative\dir`는 상대 경로라 버리고, `C:\A\`는 `C:\a`의 대소문자 무시 중복이라 버린다.
    REQUIRE(directories[2] == u8"D:/c");
    REQUIRE(directories[3] == u8"C:\\d");

    REQUIRE(gitman::split_search_path(u8"").empty());
    REQUIRE(gitman::split_search_path(u8";;;").empty());
    REQUIRE(gitman::split_search_path(u8"relative;..\\also-relative").empty());
}

TEST_CASE("Executable directories are derived without touching the file system", "[infrastructure][vcs][discovery]")
{
    REQUIRE(gitman::vcs_executable_directory(u8"C:\\Program Files\\Git\\cmd\\git.exe") == u8"C:\\Program Files\\Git\\cmd");
    REQUIRE(gitman::vcs_executable_directory(u8"C:/tools/svn/bin/svn.exe") == u8"C:/tools/svn/bin");
    // 드라이브 루트는 마지막 구분자를 남겨야 경로가 유효하다.
    REQUIRE(gitman::vcs_executable_directory(u8"C:\\git.exe") == u8"C:\\");

    REQUIRE(gitman::vcs_tool_executable_name(gitman::repository_kind::git) == u8"git.exe");
    REQUIRE(gitman::vcs_tool_executable_name(gitman::repository_kind::subversion) == u8"svn.exe");
    REQUIRE(gitman::vcs_auxiliary_executable_name(gitman::repository_kind::git).empty());
    REQUIRE(gitman::vcs_auxiliary_executable_name(gitman::repository_kind::subversion) == u8"svnversion.exe");
}

TEST_CASE("Candidate generation follows PATH then the known install locations", "[infrastructure][vcs][discovery]")
{
    const gitman::vcs_tool_environment environment { make_environment() };
    const auto candidates { gitman::vcs_tool_candidates(gitman::repository_kind::git, u8"", environment) };

    REQUIRE(candidates.size() == 4);
    REQUIRE(candidates[0].executable == u8"C:\\tools\\git\\cmd\\git.exe");
    REQUIRE(candidates[1].executable == u8"C:\\tools\\svn\\bin\\git.exe");
    REQUIRE(candidates[2].executable == u8"C:\\Program Files\\Git\\cmd\\git.exe");
    REQUIRE(candidates[3].executable == u8"C:\\Program Files\\Git\\bin\\git.exe");
    for (const auto& candidate : candidates)
        REQUIRE_FALSE(candidate.manually_configured);

    const auto subversion { gitman::vcs_tool_candidates(gitman::repository_kind::subversion, u8"", environment) };
    REQUIRE(subversion.size() == 5);
    REQUIRE(subversion.back().executable == u8"C:\\Program Files\\Subversion\\bin\\svn.exe");
}

TEST_CASE("A configured executable suppresses automatic discovery entirely", "[infrastructure][vcs][discovery]")
{
    const gitman::vcs_tool_environment environment { make_environment() };
    const auto candidates { gitman::vcs_tool_candidates(gitman::repository_kind::git, u8"D:\\custom\\git.exe", environment) };

    // 지정 경로가 있으면 다른 후보를 만들지 않는다. 자동 탐색으로 조용히 물러서면
    // 사용자가 지정한 값이 무시된 사실을 알 수 없다.
    REQUIRE(candidates.size() == 1);
    REQUIRE(candidates[0].executable == u8"D:\\custom\\git.exe");
    REQUIRE(candidates[0].manually_configured);
}

TEST_CASE("Resolution reports an available tool with its version and auxiliary program", "[infrastructure][vcs][discovery]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(git_path);
    probe.add_file(svn_path);
    probe.add_file(u8"C:\\tools\\svn\\bin\\svnversion.exe");

    gitman::testing::fake_process_runner runner {};
    runner.push_response(version_response(u8"git version 2.52.0.windows.1\r\n"));
    runner.push_response(version_response(u8"svn, version 1.14.5 (r1922182)\r\n"));

    const gitman::vcs_tool_set tools { gitman::resolve_vcs_tools({}, make_environment(), runner, probe, {}) };

    REQUIRE(tools.git.usable());
    REQUIRE(tools.git.availability == gitman::vcs_tool_availability::available);
    REQUIRE(tools.git.executable == git_path);
    REQUIRE(tools.git.version == gitman::vcs_tool_version { 2u, 52u, 0u });
    REQUIRE(tools.git.reported_version == u8"git version 2.52.0.windows.1");
    REQUIRE_FALSE(tools.git.manually_configured);
    // Git에는 보조 실행 파일이 없다.
    REQUIRE(tools.git.auxiliary_executable.empty());

    REQUIRE(tools.subversion.usable());
    REQUIRE(tools.subversion.auxiliary_executable == u8"C:\\tools\\svn\\bin\\svnversion.exe");
    REQUIRE(tools.any_available());
    REQUIRE_FALSE(tools.none_available());

    // `--version`은 도구마다 한 번씩만 실행한다.
    REQUIRE(runner.request_count() == 2);
    REQUIRE(runner.request(0).arguments.back() == u8"--version");
    // 작업 디렉터리는 실행 파일이 있는 디렉터리여야 단계 3 검증을 통과한다.
    REQUIRE(runner.request(0).working_directory == u8"C:\\tools\\git\\cmd");
    REQUIRE(gitman::validate_process_request(runner.request(0)).empty());
    REQUIRE(gitman::validate_process_request(runner.request(1)).empty());
}

TEST_CASE("A missing auxiliary program does not block the main tool", "[infrastructure][vcs][discovery]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(svn_path);

    gitman::testing::fake_process_runner runner {};
    runner.push_response(version_response(u8"svn, version 1.14.5 (r1922182)"));

    const gitman::vcs_tool_info tool { gitman::resolve_vcs_tool(gitman::repository_kind::subversion, u8"", make_environment(), runner, probe, {}) };
    REQUIRE(tool.usable());
    REQUIRE(tool.auxiliary_executable.empty());
}

TEST_CASE("Old and unreadable versions are reported without executing further commands", "[infrastructure][vcs][discovery]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(git_path);

    SECTION("too old")
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response(version_response(u8"git version 2.42.0.windows.1"));

        const gitman::vcs_tool_info tool { gitman::resolve_vcs_tool(gitman::repository_kind::git, u8"", make_environment(), runner, probe, {}) };
        REQUIRE(tool.availability == gitman::vcs_tool_availability::too_old);
        REQUIRE_FALSE(tool.usable());
        REQUIRE(tool.version == gitman::vcs_tool_version { 2u, 42u, 0u });
        REQUIRE(contains_diagnostic(tool.diagnostics, gitman::diagnostic_code::vcs_tool_too_old));
        REQUIRE(all_warnings(tool.diagnostics));
    }

    SECTION("unparsable banner")
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response(version_response(u8"git version unknown"));

        const gitman::vcs_tool_info tool { gitman::resolve_vcs_tool(gitman::repository_kind::git, u8"", make_environment(), runner, probe, {}) };
        REQUIRE(tool.availability == gitman::vcs_tool_availability::version_unreadable);
        REQUIRE(tool.reported_version == u8"git version unknown");
        REQUIRE(contains_diagnostic(tool.diagnostics, gitman::diagnostic_code::vcs_tool_version_unreadable));
    }

    SECTION("non-zero exit")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_process_runner::response failure {};
        failure.exit_code = 1;
        failure.standard_error = u8"boom";
        runner.push_response(failure);

        const gitman::vcs_tool_info tool { gitman::resolve_vcs_tool(gitman::repository_kind::git, u8"", make_environment(), runner, probe, {}) };
        REQUIRE(tool.availability == gitman::vcs_tool_availability::version_unreadable);
        REQUIRE_FALSE(tool.usable());
    }
}

TEST_CASE("A configured path that cannot be used never falls back to PATH", "[infrastructure][vcs][discovery]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    // PATH 위치에는 사용할 수 있는 Git이 있다. 지정 경로가 잘못되어도 이것으로
    // 물러서면 안 된다.
    probe.add_file(git_path);

    SECTION("relative configured path")
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response(version_response(u8"git version 2.52.0"));

        const gitman::vcs_tool_info tool { gitman::resolve_vcs_tool(gitman::repository_kind::git, u8"git.exe", make_environment(), runner, probe, {}) };
        REQUIRE(tool.availability == gitman::vcs_tool_availability::path_invalid);
        REQUIRE(tool.manually_configured);
        REQUIRE_FALSE(tool.usable());
        REQUIRE(contains_diagnostic(tool.diagnostics, gitman::diagnostic_code::vcs_tool_path_invalid));
        // 상대 경로는 실행 자체를 시도하지 않는다.
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("configured path does not exist")
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response(version_response(u8"git version 2.52.0"));

        const gitman::vcs_tool_info tool { gitman::resolve_vcs_tool(gitman::repository_kind::git, u8"D:\\missing\\git.exe", make_environment(), runner, probe, {}) };
        REQUIRE(tool.availability == gitman::vcs_tool_availability::path_invalid);
        REQUIRE(tool.executable == u8"D:\\missing\\git.exe");
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("configured path exists but fails to run")
    {
        gitman::testing::fake_vcs_file_probe configured_probe {};
        configured_probe.add_file(u8"D:\\custom\\git.exe");
        configured_probe.add_file(git_path);

        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_process_runner::response failure {};
        failure.completion = gitman::process_completion::start_failed;
        runner.push_response(failure);

        const gitman::vcs_tool_info tool { gitman::resolve_vcs_tool(gitman::repository_kind::git, u8"D:\\custom\\git.exe", make_environment(), runner, configured_probe, {}) };
        // 자동 탐색이었다면 `version_unreadable`이지만 지정 경로는 `path_invalid`다.
        REQUIRE(tool.availability == gitman::vcs_tool_availability::path_invalid);
        REQUIRE(runner.request_count() == 1);
        REQUIRE(runner.request(0).executable == u8"D:\\custom\\git.exe");
    }
}

TEST_CASE("A valid configured path is used and marked as manual", "[infrastructure][vcs][discovery]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"D:\\custom\\git.exe");

    gitman::testing::fake_process_runner runner {};
    runner.push_response(version_response(u8"git version 2.52.0"));

    gitman::workspace_settings settings {};
    settings.git_executable = u8"D:\\custom\\git.exe";

    const gitman::vcs_tool_set tools { gitman::resolve_vcs_tools(settings, make_environment(), runner, probe, {}) };
    REQUIRE(tools.git.usable());
    REQUIRE(tools.git.manually_configured);
    REQUIRE(tools.git.executable == u8"D:\\custom\\git.exe");
    REQUIRE(runner.requests_for_executable_suffix(u8"custom\\git.exe") == 1);
}

TEST_CASE("An environment without any VCS stays usable and reports warnings only", "[infrastructure][vcs][discovery][registry]")
{
    // Git과 SVN이 둘 다 없는 환경은 오류 상태가 아니다. 앱은 계속 동작하고 조회와
    // 변경 동작만 비활성화된다.
    gitman::testing::fake_vcs_file_probe probe {};
    gitman::testing::fake_process_runner runner {};

    const gitman::vcs_tool_set tools { gitman::resolve_vcs_tools({}, make_environment(), runner, probe, {}) };

    REQUIRE(tools.none_available());
    REQUIRE_FALSE(tools.any_available());
    REQUIRE(tools.git.availability == gitman::vcs_tool_availability::not_found);
    REQUIRE(tools.subversion.availability == gitman::vcs_tool_availability::not_found);
    REQUIRE(tools.git.executable.empty());
    // 실행 파일이 없으면 프로세스를 하나도 만들지 않는다.
    REQUIRE(runner.request_count() == 0);

    const gitman::vcs_tool_registry registry { tools };
    REQUIRE(registry.none_available());
    REQUIRE_FALSE(registry.available(gitman::repository_kind::git));
    REQUIRE_FALSE(registry.available(gitman::repository_kind::subversion));
    REQUIRE_FALSE(registry.available(gitman::repository_kind::unknown));

    const auto diagnostics { registry.unavailable_diagnostics() };
    REQUIRE(diagnostics.size() == 2);
    REQUIRE(all_warnings(diagnostics));
    REQUIRE(contains_diagnostic(diagnostics, gitman::diagnostic_code::vcs_tool_not_found));
}

TEST_CASE("One missing tool does not disable the other", "[infrastructure][vcs][discovery][registry]")
{
    // 현재 호스트와 같은 구성이다. Git은 있고 SVN은 없다.
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(git_path);

    gitman::testing::fake_process_runner runner {};
    runner.push_response(version_response(u8"git version 2.52.0.windows.1"));

    const gitman::vcs_tool_registry registry { gitman::resolve_vcs_tools({}, make_environment(), runner, probe, {}) };

    REQUIRE(registry.any_available());
    REQUIRE_FALSE(registry.none_available());
    REQUIRE(registry.available(gitman::repository_kind::git));
    REQUIRE_FALSE(registry.available(gitman::repository_kind::subversion));
    // 사용할 수 없는 도구의 진단만 모은다.
    REQUIRE(registry.unavailable_diagnostics().size() == 1);
    REQUIRE(registry.tool(gitman::repository_kind::git).usable());
    REQUIRE(registry.tools().subversion.availability == gitman::vcs_tool_availability::not_found);
}

TEST_CASE("Registries start empty and accept replacement results", "[application][vcs][registry]")
{
    gitman::vcs_tool_registry registry {};
    REQUIRE(registry.none_available());
    REQUIRE(registry.tool(gitman::repository_kind::git).availability == gitman::vcs_tool_availability::unknown);
    // 아직 조사하지 않은 상태에는 진단이 없다. 조사 전에 경고를 띄우지 않기 위해서다.
    REQUIRE(registry.unavailable_diagnostics().empty());

    gitman::vcs_tool_set tools {};
    tools.git.kind = gitman::repository_kind::git;
    tools.git.availability = gitman::vcs_tool_availability::available;
    tools.git.executable = git_path;
    registry.set_tools(tools);

    REQUIRE(registry.available(gitman::repository_kind::git));
    REQUIRE(registry.any_available());
    REQUIRE(registry.tools().git.executable == git_path);
}

TEST_CASE("Unavailable tool messages name the tool and the remedy", "[application][vcs][registry]")
{
    const std::u8string_view git_missing { gitman::vcs_tool_unavailable_message(gitman::repository_kind::git, gitman::vcs_tool_availability::not_found) };
    const std::u8string_view svn_missing { gitman::vcs_tool_unavailable_message(gitman::repository_kind::subversion, gitman::vcs_tool_availability::not_found) };
    REQUIRE(git_missing != svn_missing);
    REQUIRE(git_missing.find(u8"settings") != std::u8string_view::npos);

    REQUIRE(gitman::vcs_tool_unavailable_message(gitman::repository_kind::git, gitman::vcs_tool_availability::too_old).empty() == false);
    REQUIRE(gitman::vcs_tool_unavailable_message(gitman::repository_kind::git, gitman::vcs_tool_availability::path_invalid).empty() == false);
    REQUIRE(gitman::vcs_tool_unavailable_message(gitman::repository_kind::git, gitman::vcs_tool_availability::version_unreadable).empty() == false);
    REQUIRE(gitman::vcs_tool_unavailable_message(gitman::repository_kind::git, gitman::vcs_tool_availability::unknown).empty() == false);
    REQUIRE(gitman::vcs_tool_unavailable_message(gitman::repository_kind::git, static_cast<gitman::vcs_tool_availability>(-1)).empty() == false);
}
