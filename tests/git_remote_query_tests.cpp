#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_tool.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/git_repository_provider.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {
    constexpr std::u8string_view repository_path { u8"C:\\작업 공간\\repo" };
    constexpr std::size_t command_name_index { 7 };

    gitman::vcs_tool_info available_tool()
    {
        gitman::vcs_tool_info tool {};
        tool.kind = gitman::repository_kind::git;
        tool.availability = gitman::vcs_tool_availability::available;
        tool.executable = u8"C:\\Program Files\\Git\\cmd\\git.exe";
        tool.version = { 2, 52, 0 };
        return tool;
    }

    gitman::project_definition make_project()
    {
        gitman::project_definition project {};
        project.id.value = u8"repo-1";
        project.path.original = repository_path;
        project.path.normalized = repository_path;
        return project;
    }

    // 로컬 조회를 마친 상태다. `comparison_source::local`은 이미 받아 둔 remote tracking
    // ref와의 비교이며 원격 조회가 이 값을 덮어쓴다.
    gitman::repository_snapshot ready_local(const std::u8string_view branch = u8"main", const std::u8string_view upstream = u8"origin/main")
    {
        gitman::repository_snapshot snapshot {};
        snapshot.kind = gitman::repository_kind::git;
        snapshot.availability = gitman::repository_availability::ready;
        snapshot.current_reference = branch;
        snapshot.local_revision = u8"0123456789abcdef0123456789abcdef01234567";
        snapshot.working_tree.state = gitman::working_tree_state::clean;
        if (upstream.empty() == false)
        {
            snapshot.comparison = gitman::comparison_source::local;
            snapshot.comparison_target = upstream;
        }
        return snapshot;
    }

    std::vector<std::u8string> names(const std::vector<std::u8string_view>& values)
    {
        std::vector<std::u8string> result {};
        result.reserve(values.size());
        for (const std::u8string_view value : values)
            result.emplace_back(value);
        return result;
    }

    void register_working_directory(gitman::testing::fake_vcs_file_probe& probe)
    {
        probe.add_directory(repository_path);
    }

    // 원격 조회가 성공하는 최소 응답이다. remote 목록, fetch, ref 확인, 차이 계산 순이다.
    void push_successful_remote_responses(gitman::testing::fake_process_runner& runner, const std::u8string_view remotes, const std::u8string_view counts)
    {
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { remotes }, {} });
        runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
        runner.push_response({ gitman::process_completion::exited, 0, u8"abcdef1234567890\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { counts }, {} });
    }

    bool has_diagnostic(const gitman::repository_query_result& result, const gitman::diagnostic_code code) noexcept
    {
        return std::ranges::any_of(result.diagnostics, [code](const gitman::diagnostic& value) { return value.code == code; });
    }
} // namespace

TEST_CASE("Remote target selection follows the remote first order", "[infrastructure][git][remote]")
{
    SECTION("upstream이 있으면 그대로 쓴다")
    {
        const gitman::git_remote_target target { gitman::select_git_remote_target(names({ u8"origin", u8"fork" }), u8"main", u8"origin/main", u8"fork", false) };
        REQUIRE(target.resolved);
        REQUIRE(target.reason == gitman::git_remote_target_reason::upstream);
        REQUIRE(target.remote == u8"origin");
        REQUIRE(target.branch == u8"main");
        REQUIRE(target.tracking_reference == u8"refs/remotes/origin/main");
        REQUIRE(target.display_name == u8"origin/main");
    }

    SECTION("branch 이름의 슬래시를 자르지 않는다")
    {
        const gitman::git_remote_target target { gitman::select_git_remote_target(names({ u8"origin" }), u8"feature/a/b", u8"origin/feature/a/b", u8"", false) };
        REQUIRE(target.branch == u8"feature/a/b");
        REQUIRE(target.tracking_reference == u8"refs/remotes/origin/feature/a/b");
    }

    SECTION("가장 긴 remote 접두사를 고른다")
    {
        // `up`과 `up/stream`이 모두 있으면 짧은 쪽을 고를 때 branch 이름이 어긋난다.
        const gitman::git_remote_target target { gitman::select_git_remote_target(names({ u8"up", u8"up/stream" }), u8"main", u8"up/stream/main", u8"", false) };
        REQUIRE(target.remote == u8"up/stream");
        REQUIRE(target.branch == u8"main");
    }

    SECTION("local branch를 가리키는 upstream은 원격 비교에 쓰지 않는다")
    {
        // `branch.<name>.remote = .`이면 upstream이 local branch다.
        const gitman::git_remote_target target { gitman::select_git_remote_target(names({ u8"origin" }), u8"topic", u8"main", u8"", false) };
        REQUIRE(target.resolved);
        REQUIRE(target.reason == gitman::git_remote_target_reason::origin);
        REQUIRE(target.branch == u8"topic");
    }

    SECTION("preferred_remote가 origin보다 앞선다")
    {
        const gitman::git_remote_target target { gitman::select_git_remote_target(names({ u8"origin", u8"fork" }), u8"main", u8"", u8"fork", false) };
        REQUIRE(target.reason == gitman::git_remote_target_reason::preferred_remote);
        REQUIRE(target.remote == u8"fork");
        REQUIRE_FALSE(target.preferred_remote_missing);
    }

    SECTION("지정한 remote가 없으면 다음 규칙으로 가되 사실을 남긴다")
    {
        const gitman::git_remote_target target { gitman::select_git_remote_target(names({ u8"origin", u8"fork" }), u8"main", u8"", u8"absent", false) };
        REQUIRE(target.resolved);
        REQUIRE(target.reason == gitman::git_remote_target_reason::origin);
        REQUIRE(target.preferred_remote_missing);
    }

    SECTION("origin과 유일한 remote")
    {
        REQUIRE(gitman::select_git_remote_target(names({ u8"upstream", u8"origin" }), u8"main", u8"", u8"", false).reason == gitman::git_remote_target_reason::origin);

        const gitman::git_remote_target only { gitman::select_git_remote_target(names({ u8"fork" }), u8"main", u8"", u8"", false) };
        REQUIRE(only.reason == gitman::git_remote_target_reason::only_remote);
        REQUIRE(only.display_name == u8"fork/main");
    }
}

TEST_CASE("Ambiguous remotes are never chosen automatically", "[infrastructure][git][remote]")
{
    const gitman::git_remote_target target { gitman::select_git_remote_target(names({ u8"alpha", u8"beta" }), u8"main", u8"", u8"", false) };

    // 자동으로 고르면 사용자가 의도하지 않은 원격과 비교하게 된다.
    REQUIRE_FALSE(target.resolved);
    REQUIRE(target.reason == gitman::git_remote_target_reason::ambiguous_remote);
    REQUIRE(target.tracking_reference.empty());
    REQUIRE(target.remote.empty());
}

TEST_CASE("Targets without a comparable branch are refused", "[infrastructure][git][remote]")
{
    REQUIRE(gitman::select_git_remote_target({}, u8"main", u8"", u8"", false).reason == gitman::git_remote_target_reason::no_remote);
    REQUIRE(gitman::select_git_remote_target(names({ u8"origin" }), u8"(detached) abc1234", u8"", u8"", true).reason == gitman::git_remote_target_reason::detached_head);
    REQUIRE(gitman::select_git_remote_target(names({ u8"origin" }), u8"", u8"", u8"", false).reason == gitman::git_remote_target_reason::no_branch);

    for (const gitman::git_remote_target_reason reason : { gitman::git_remote_target_reason::no_remote, gitman::git_remote_target_reason::detached_head, gitman::git_remote_target_reason::no_branch })
        REQUIRE(reason != gitman::git_remote_target_reason::upstream);
}

TEST_CASE("Remote queries build the four commands in order", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    push_successful_remote_responses(runner, u8"origin\n", u8"0\t0\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };

    REQUIRE(runner.request_count() == 4);
    REQUIRE(runner.request(0).arguments.at(command_name_index) == u8"remote");
    REQUIRE(runner.request(1).arguments.at(command_name_index) == u8"fetch");
    REQUIRE(runner.request(2).arguments.at(command_name_index) == u8"rev-parse");
    REQUIRE(runner.request(3).arguments.at(command_name_index) == u8"rev-list");
    // upstream이 가리키는 remote를 fetch한다.
    REQUIRE(runner.request(1).arguments.back() == u8"origin");
    REQUIRE(runner.request(2).arguments.back() == u8"refs/remotes/origin/main");

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::up_to_date);
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::remote);
    REQUIRE(result.snapshot.comparison_target == u8"origin/main");
    REQUIRE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("Remote comparison maps counts to sync states", "[infrastructure][git][remote]")
{
    struct expectation
    {
        std::u8string_view counts {};
        gitman::remote_sync_state state {};
        std::uint64_t ahead {};
        std::uint64_t behind {};
    };

    const expectation expectations[] {
        { u8"0\t0\n", gitman::remote_sync_state::up_to_date, 0, 0 },
        { u8"2\t0\n", gitman::remote_sync_state::ahead, 2, 0 },
        { u8"0\t5\n", gitman::remote_sync_state::behind, 0, 5 },
        { u8"2\t3\n", gitman::remote_sync_state::diverged, 2, 3 },
    };

    for (const expectation& value : expectations)
    {
        gitman::testing::fake_process_runner runner {};
        push_successful_remote_responses(runner, u8"origin\n", value.counts);
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };
        REQUIRE(result.snapshot.sync_state == value.state);
        REQUIRE(result.snapshot.ahead_count == value.ahead);
        REQUIRE(result.snapshot.behind_count == value.behind);
    }
}

TEST_CASE("Remote queries stop before the network when there is nothing to compare", "[infrastructure][git][remote]")
{
    SECTION("도구가 없다")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::vcs_tool_info tool {};
        tool.kind = gitman::repository_kind::git;
        tool.availability = gitman::vcs_tool_availability::not_found;
        gitman::git_repository_provider provider { tool, runner, probe };

        const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::tool_unavailable);
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("로컬 조회가 저장소로 인정하지 않았다")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        gitman::repository_snapshot local { ready_local() };
        local.availability = gitman::repository_availability::not_a_repository;
        const gitman::repository_query_result result { provider.query_remote(make_project(), local, {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::not_a_repository);
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("경로가 사라졌다")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::path_unavailable);
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("detached HEAD에는 비교할 branch가 없다")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        gitman::repository_snapshot local { ready_local(u8"(detached) abc1234", u8"") };
        local.working_tree.is_detached = true;
        const gitman::repository_query_result result { provider.query_remote(make_project(), local, {}) };
        REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::remote_target_missing);
        REQUIRE(result.snapshot.comparison == gitman::comparison_source::none);
        REQUIRE(runner.request_count() == 0);
    }
}

TEST_CASE("Repositories without remotes are local only", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(u8"main", u8""), {}) };

    // 비교 대상이 없는 상태와 원격 확인에 실패한 상태는 사용자가 할 일이 다르다.
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::local_only);
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::none);
    REQUIRE(result.snapshot.remote_checked_at.has_value() == false);
    // remote가 없으면 fetch하지 않는다.
    REQUIRE(runner.request_count() == 1);
}

TEST_CASE("Ambiguous remotes are reported without fetching", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, u8"alpha\nbeta\n", {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(u8"main", u8""), {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::remote_target_missing);
    REQUIRE(runner.request_count() == 1);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.diagnostics.size() == 1);
}

TEST_CASE("Missing preferred remotes are reported but do not stop the query", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    push_successful_remote_responses(runner, u8"origin\nfork\n", u8"1\t0\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::project_definition project { make_project() };
    project.preferred_remote = std::u8string { u8"absent" };
    const gitman::repository_query_result result { provider.query_remote(project, ready_local(u8"main", u8""), {}) };

    // 지정한 값이 조용히 무시되면 다른 remote와 비교되고 있다는 사실을 알 수 없다.
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::invalid_project_field));
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::ahead);
    REQUIRE(result.snapshot.comparison_target == u8"origin/main");
}

TEST_CASE("Fetch failures are classified by locale independent signals", "[infrastructure][git][remote]")
{
    struct expectation
    {
        std::u8string_view standard_error {};
        gitman::remote_sync_state state {};
        gitman::diagnostic_code code {};
    };

    // libcurl과 OpenSSH의 문장은 번역 catalog가 없어 어떤 로캘에서도 그대로 남는다.
    // 같은 실패의 영어 출력과 한국어 출력이 같은 분류를 내야 한다.
    using state = gitman::remote_sync_state;
    using code = gitman::diagnostic_code;
    const expectation expectations[] {
        { u8"fatal: unable to access: Could not resolve host: host\n", state::offline, code::remote_unreachable },
        { u8"치명적: 접근할 수 없습니다: Failed to connect to host port 443\n", state::offline, code::remote_unreachable },
        { u8"git@host: Permission denied (publickey).\n", state::authentication_required, code::authentication_required },
        { u8"치명적: 접근 거부: Permission denied (publickey).\n", state::authentication_required, code::authentication_required },
        // HTTP 상태 번호는 오탐을 줄이려고 같은 텍스트의 `http`와 함께일 때만 신호가 된다.
        { u8"fatal: unable to access 'https://h/r.git/': The requested URL returned error: 403\n", state::authentication_required, code::authentication_required },
        { u8"fatal: 알 수 없는 이유로 실패했습니다\n", state::error, code::vcs_command_failed },
    };

    for (const expectation& value : expectations)
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
        runner.push_response({ gitman::process_completion::exited, 128, {}, std::u8string { value.standard_error } });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };
        REQUIRE(result.snapshot.sync_state == value.state);
        REQUIRE(has_diagnostic(result, value.code));
        // fetch가 실패하면 뒤의 명령은 실행하지 않는다.
        REQUIRE(runner.request_count() == 2);
    }
}

TEST_CASE("Failed remote checks keep what was known before", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: unable to access: Failed to connect to host port 443\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::repository_snapshot local { ready_local() };
    local.ahead_count = 4;
    local.working_tree.state = gitman::working_tree_state::modified;
    local.remote_checked_at = std::chrono::system_clock::now() - std::chrono::hours { 3 };
    const auto previous_check { *local.remote_checked_at };

    const gitman::repository_query_result result { provider.query_remote(make_project(), local, {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::offline);
    // "지금은 연결이 안 되지만 마지막으로 알기로는 4개 앞서 있었다"를 보여 줄 수 있어야 한다.
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::local);
    REQUIRE(result.snapshot.ahead_count == 4);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::modified);
    REQUIRE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(*result.snapshot.remote_checked_at == previous_check);
}

TEST_CASE("Process failures during the remote query are promoted", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::timed_out, 0, {}, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::offline);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::process_timed_out));
}

TEST_CASE("Cancelled remote queries do not become a comparison", "[infrastructure][git][remote]")
{
    gitman::process_cancellation_source source {};
    source.request_cancellation();

    gitman::testing::fake_process_runner runner {};
    push_successful_remote_responses(runner, u8"origin\n", u8"0\t0\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), source.token()) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::unknown);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::process_cancelled));
    REQUIRE(result.snapshot.remote_checked_at.has_value() == false);
}

TEST_CASE("Branches missing on the remote are reported after fetching", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
    // `rev-parse --verify --quiet`는 없는 ref를 출력 없이 종료 코드로만 알린다.
    runner.push_response({ gitman::process_completion::exited, 1, {}, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::repository_snapshot local { ready_local(u8"topic", u8"") };
    local.ahead_count = 2;
    const gitman::repository_query_result result { provider.query_remote(make_project(), local, {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::remote_target_missing);
    // 원격에는 닿았으므로 확인 시각은 남는다.
    REQUIRE(result.snapshot.remote_checked_at.has_value());
    // 유효하지 않은 비교를 남기면 카드가 잘못된 수를 계속 보여 준다.
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::none);
    REQUIRE(result.snapshot.ahead_count == 0);
    REQUIRE(runner.request_count() == 3);
    REQUIRE(runner.request(2).arguments.back() == u8"refs/remotes/origin/topic");
}

TEST_CASE("Repositories without commits are not compared", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"abcdef1234567890\n", {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::repository_snapshot local { ready_local() };
    local.local_revision.clear();
    const gitman::repository_query_result result { provider.query_remote(make_project(), local, {}) };

    // `HEAD`가 없어 대칭 차이를 계산할 수 없다. 값을 추측하지 않는다.
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::unknown);
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::remote);
    REQUIRE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(runner.request_count() == 3);
}

TEST_CASE("Unreadable difference output is not guessed", "[infrastructure][git][remote]")
{
    gitman::testing::fake_process_runner runner {};
    push_successful_remote_responses(runner, u8"origin\n", u8"이상한 출력\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_remote(make_project(), ready_local(), {}) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::error);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::vcs_output_unparsable));
    REQUIRE(result.snapshot.comparison != gitman::comparison_source::remote);
}
