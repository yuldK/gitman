#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_tool.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/svn_repository_provider.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {
    constexpr std::u8string_view working_copy_path { u8"C:\\작업 공간\\작업 복사본" };
    constexpr std::u8string_view svn_executable { u8"C:\\Program Files\\TortoiseSVN\\bin\\svn.exe" };
    constexpr std::u8string_view svnversion_executable { u8"C:\\Program Files\\TortoiseSVN\\bin\\svnversion.exe" };
    constexpr std::u8string_view repository_url { u8"https://svn.example.com/repo/trunk" };

    gitman::vcs_tool_info available_tool(const bool with_svnversion = true)
    {
        gitman::vcs_tool_info tool {};
        tool.kind = gitman::repository_kind::subversion;
        tool.availability = gitman::vcs_tool_availability::available;
        tool.executable = svn_executable;
        if (with_svnversion)
            tool.auxiliary_executable = svnversion_executable;
        tool.version = { 1, 14, 5 };
        return tool;
    }

    gitman::vcs_tool_info missing_tool()
    {
        gitman::vcs_tool_info tool {};
        tool.kind = gitman::repository_kind::subversion;
        tool.availability = gitman::vcs_tool_availability::not_found;
        return tool;
    }

    gitman::project_definition make_project(const std::u8string_view path = working_copy_path)
    {
        gitman::project_definition project {};
        project.id.value = u8"svn-1";
        project.path.original = path;
        project.path.normalized = path;
        return project;
    }

    void register_working_directory(gitman::testing::fake_vcs_file_probe& probe, const std::u8string_view path = working_copy_path)
    {
        probe.add_directory(path);
    }

    // 로컬 조회 성공 경로의 응답이다. `info` 5개, `status`, `svnversion` 순이다.
    void push_local_responses(gitman::testing::fake_process_runner& runner, const std::u8string_view status, const std::u8string_view version)
    {
        runner.push_response({ gitman::process_completion::exited, 0, u8"C:\\작업 공간\\작업 복사본\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, u8"^/trunk\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, u8"4168\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, u8"https://svn.example.com/repo\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, u8"8f3a1c2e-0000-0000-0000-000000000000\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { status }, {} });
        if (version.empty() == false)
            runner.push_response({ gitman::process_completion::exited, 0, std::u8string { version }, {} });
    }

    gitman::repository_snapshot ready_local(const std::u8string_view revision = u8"4168")
    {
        gitman::repository_snapshot snapshot {};
        snapshot.kind = gitman::repository_kind::subversion;
        snapshot.availability = gitman::repository_availability::ready;
        snapshot.current_reference = u8"^/trunk";
        snapshot.local_revision = revision;
        snapshot.working_tree.state = gitman::working_tree_state::clean;
        return snapshot;
    }

    bool has_diagnostic(const gitman::repository_query_result& result, const gitman::diagnostic_code code) noexcept
    {
        return std::ranges::any_of(result.diagnostics, [code](const gitman::diagnostic& value) { return value.code == code; });
    }
} // namespace

TEST_CASE("SVN queries do nothing when the tool is missing", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { missing_tool(), runner, probe };

    REQUIRE(provider.kind() == gitman::repository_kind::subversion);
    REQUIRE_FALSE(provider.available());

    const gitman::repository_query_result local { provider.query_local(make_project(), {}) };
    REQUIRE(local.snapshot.availability == gitman::repository_availability::tool_unavailable);
    // SVN이 없는 환경에서도 앱은 계속 동작한다. 카드는 목록에 남고 동작만 비활성화된다.
    REQUIRE_FALSE(local.has_errors());

    const gitman::repository_query_result remote { provider.query_remote(make_project(), ready_local(), {}) };
    REQUIRE(remote.snapshot.availability == gitman::repository_availability::tool_unavailable);
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("SVN queries reject paths before starting a process", "[infrastructure][svn][provider]")
{
    SECTION("상대 경로")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_directory(u8"작업 복사본");
        gitman::svn_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_local(make_project(u8"작업 복사본"), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::path_unavailable);
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("사라진 경로")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        gitman::svn_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_local(make_project(), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::path_unavailable);
        REQUIRE(has_diagnostic(result, gitman::diagnostic_code::path_missing));
        REQUIRE(runner.request_count() == 0);
    }
}

TEST_CASE("Paths that are not working copies stop at the first query", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    // 번역된 메시지에도 `E<숫자>` 코드가 그대로 붙는다.
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"svn: E155007: '/wc'은(는) 작업 복사본이 아닙니다\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::not_a_repository);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::repository_not_found));
    REQUIRE(runner.request_count() == 1);
}

TEST_CASE("A working copy becomes a common snapshot", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, u8"M       trunk/a.txt\n?       trunk/새 파일.txt\n", u8"4168\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.kind == gitman::repository_kind::subversion);
    REQUIRE(result.snapshot.project.value == u8"svn-1");
    REQUIRE(result.snapshot.repository_root == working_copy_path);
    // 저장소 상대 URL이 카드가 보여 줄 현재 위치다.
    REQUIRE(result.snapshot.current_reference == u8"^/trunk");
    REQUIRE(result.snapshot.local_revision == u8"4168");
    REQUIRE(result.snapshot.svn_repository_root == u8"https://svn.example.com/repo");
    REQUIRE(result.snapshot.svn_repository_uuid == u8"8f3a1c2e-0000-0000-0000-000000000000");
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::modified);
    REQUIRE(result.snapshot.working_tree.modified_count == 1);
    REQUIRE(result.snapshot.working_tree.untracked_count == 1);
    REQUIRE(result.snapshot.has_mixed_revision.has_value());
    REQUIRE_FALSE(*result.snapshot.has_mixed_revision);
    REQUIRE(result.snapshot.has_switched_subtree.has_value());
    REQUIRE_FALSE(*result.snapshot.has_switched_subtree);
    REQUIRE(result.snapshot.local_checked_at.has_value());
    REQUIRE_FALSE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(result.diagnostics.empty());

    REQUIRE(runner.request_count() == 7);
    REQUIRE(runner.request(0).arguments.back() == u8"wc-root");
    REQUIRE(runner.request(1).arguments.back() == u8"relative-url");
    REQUIRE(runner.request(2).arguments.back() == u8"revision");
    REQUIRE(runner.request(3).arguments.back() == u8"repos-root-url");
    REQUIRE(runner.request(4).arguments.back() == u8"repos-uuid");
    REQUIRE(runner.request(5).arguments.back() == u8"status");
    // `svnversion`은 다른 실행 파일이고 인자가 없다.
    REQUIRE(runner.request(6).executable == svnversion_executable);
    REQUIRE(runner.request(6).arguments.empty());
    // 로컬 조회는 네트워크를 쓰지 않는다.
    REQUIRE(runner.requests_for_executable_suffix(u8"svn.exe") == 6);
    for (const gitman::process_request& request : runner.requests())
        for (const std::u8string& argument : request.arguments)
            REQUIRE(argument.starts_with(u8"https://") == false);
}

TEST_CASE("Mixed revisions and switched subtrees are reported", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, u8"    S   trunk/전환된 폴더\n", u8"4123:4168MS\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.has_mixed_revision.has_value());
    REQUIRE(*result.snapshot.has_mixed_revision);
    REQUIRE(result.snapshot.has_switched_subtree.has_value());
    REQUIRE(*result.snapshot.has_switched_subtree);
}

TEST_CASE("Missing svnversion gives up only the two judgements", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, u8"    S   trunk/전환된 폴더\n", u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(false), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.local_revision == u8"4168");
    // 거짓과 미상을 구분한다.
    REQUIRE_FALSE(result.snapshot.has_mixed_revision.has_value());
    // switched는 `status`의 5번 칸으로 보조 판정한다.
    REQUIRE(result.snapshot.has_switched_subtree.has_value());
    REQUIRE(*result.snapshot.has_switched_subtree);
    REQUIRE(runner.request_count() == 6);
}

TEST_CASE("Unreadable svnversion output is a warning, not a failure", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"Unversioned directory\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
    REQUIRE_FALSE(result.snapshot.has_mixed_revision.has_value());
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::vcs_output_unparsable));
    REQUIRE_FALSE(result.has_errors());
}

TEST_CASE("SVN status failures do not become a working tree state", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    for (std::size_t index = 0; index < 5; ++index)
        runner.push_response({ gitman::process_completion::exited, 0, u8"값\n", {} });
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"svn: E155004: 작업 복사본이 잠겨 있습니다\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::unknown);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::unknown);
    REQUIRE(result.has_errors());
    REQUIRE(runner.request_count() == 6);
}

TEST_CASE("Process failures during SVN queries are promoted", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::timed_out, 0, {}, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    // 실행이 끝나지 않은 것과 작업 복사본이 아닌 것은 다르다.
    REQUIRE(result.snapshot.availability == gitman::repository_availability::unknown);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::process_timed_out));
}

TEST_CASE("SVN remote comparison reports behind by revision distance", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { repository_url } + u8"\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"4180\n", {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::behind);
    REQUIRE(result.snapshot.behind_count == 12);
    // SVN에는 Git의 `ahead`와 `diverged`가 없다.
    REQUIRE(result.snapshot.ahead_count == 0);
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::remote);
    REQUIRE(result.snapshot.comparison_target == repository_url);
    REQUIRE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(runner.request_count() == 2);
    REQUIRE(runner.request(1).arguments.back() == repository_url);
}

TEST_CASE("SVN remote comparison reports an up to date working copy", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { repository_url } + u8"\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"4168\n", {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::up_to_date);
    REQUIRE(result.snapshot.behind_count == 0);
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("SVN remote failures are classified by error code", "[infrastructure][svn][provider]")
{
    struct expectation
    {
        std::u8string_view standard_error {};
        gitman::remote_sync_state state {};
    };

    // 번역된 메시지에도 코드가 그대로 붙어 로캘에 의존하지 않는다.
    const expectation expectations[] {
        { u8"svn: E170013: 저장소에 연결할 수 없습니다\nsvn: E175002: 연결 실패\n", gitman::remote_sync_state::offline },
        { u8"svn: E215004: 자격 증명을 찾을 수 없습니다\n", gitman::remote_sync_state::authentication_required },
        { u8"svn: E155010: 대상을 찾을 수 없습니다\n", gitman::remote_sync_state::error },
    };

    for (const expectation& value : expectations)
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { repository_url } + u8"\n", {} });
        runner.push_response({ gitman::process_completion::exited, 1, {}, std::u8string { value.standard_error } });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::svn_repository_provider provider { available_tool(), runner, probe };

        gitman::repository_snapshot local { ready_local() };
        local.working_tree.state = gitman::working_tree_state::modified;
        const gitman::repository_query_result result { provider.query_remote(make_project(), local, {}) };

        REQUIRE(result.snapshot.sync_state == value.state);
        // 실패해도 직전 로컬 상태는 남는다.
        REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::modified);
        REQUIRE_FALSE(result.snapshot.remote_checked_at.has_value());
    }
}

TEST_CASE("Unreadable SVN revisions are not guessed", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { repository_url } + u8"\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"알 수 없음\n", {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::error);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::vcs_output_unparsable));
}

TEST_CASE("SVN remote queries need a finished local query", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    gitman::repository_snapshot local { ready_local() };
    local.availability = gitman::repository_availability::not_a_repository;
    const gitman::repository_query_result result { provider.query_remote(make_project(), local, {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::not_a_repository);
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("SVN operations that are not implemented yet build no request", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };
    const gitman::project_definition project { make_project() };

    // 허용 목록 기반 switch는 `S4-D6` 구간이다. `svn update`는 `S4-D5-CODE`가 구현했다.
    REQUIRE(provider.query_switch_candidates(project, {}).candidates.empty());
    REQUIRE_FALSE(provider.switch_to(project, {}, {}).executed);
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("Refreshed SVN tool information is picked up", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"4168\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { missing_tool(), runner, probe };

    REQUIRE_FALSE(provider.available());
    provider.set_tool(available_tool());

    REQUIRE(provider.available());
    REQUIRE(provider.tool().version == gitman::vcs_tool_version { 1, 14, 5 });
    REQUIRE(provider.query_local(make_project(), {}).snapshot.availability == gitman::repository_availability::ready);
}
