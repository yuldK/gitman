#include "application/switch_validation_service.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "domain/vcs_tool.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/git_repository_provider.h"
#include "infrastructure/git_status_parser.h"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>

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

    gitman::vcs_tool_info missing_tool()
    {
        gitman::vcs_tool_info tool {};
        tool.kind = gitman::repository_kind::git;
        tool.availability = gitman::vcs_tool_availability::not_found;
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

    void register_working_directory(gitman::testing::fake_vcs_file_probe& probe)
    {
        probe.add_directory(repository_path);
    }

    std::u8string join_lines(const std::vector<std::u8string_view>& values)
    {
        std::u8string text {};
        for (const std::u8string_view value : values)
        {
            text.append(value);
            text.push_back(u8'\n');
        }
        return text;
    }

    std::u8string reference_line(const std::u8string_view name, const std::u8string_view upstream, const bool head = false, const std::u8string_view symbolic_target = {})
    {
        std::u8string line { name };
        line.append(u8"\t0123456789abcdef0123456789abcdef01234567\t");
        line.append(upstream);
        line.push_back(u8'\t');
        line.append(head ? u8"*" : u8" ");
        line.push_back(u8'\t');
        line.append(symbolic_target);
        return line;
    }

    std::vector<gitman::git_reference_entry> sample_references()
    {
        return gitman::parse_git_reference_list({
            reference_line(u8"refs/heads/main", u8"refs/remotes/origin/main", true),
            reference_line(u8"refs/heads/only-local", {}),
            reference_line(u8"refs/heads/forked", u8"refs/remotes/fork/forked"),
            reference_line(u8"refs/remotes/fork/forked", {}),
            reference_line(u8"refs/remotes/origin/HEAD", {}, false, u8"refs/remotes/origin/main"),
            reference_line(u8"refs/remotes/origin/forked", {}),
            reference_line(u8"refs/remotes/origin/main", {}),
            reference_line(u8"refs/remotes/origin/new-branch", {}),
        });
    }

    std::u8string sample_reference_output()
    {
        return join_lines({
            reference_line(u8"refs/heads/main", u8"refs/remotes/origin/main", true),
            reference_line(u8"refs/heads/only-local", {}),
            reference_line(u8"refs/remotes/origin/HEAD", {}, false, u8"refs/remotes/origin/main"),
            reference_line(u8"refs/remotes/origin/main", {}),
            reference_line(u8"refs/remotes/origin/new-branch", {}),
        });
    }

    std::u8string layout_output()
    {
        return join_lines({ u8"C:/작업 공간/repo/.git", u8"false", u8"true", u8"C:/작업 공간/repo" });
    }

    std::u8string clean_status_output()
    {
        return join_lines({ u8"# branch.oid 0123456789abcdef0123456789abcdef01234567", u8"# branch.head main", u8"# branch.upstream origin/main", u8"# branch.ab +0 -0" });
    }

    std::u8string worktree_output()
    {
        return join_lines({ u8"worktree C:/작업 공간/repo", u8"HEAD 0123456789abcdef0123456789abcdef01234567", u8"branch refs/heads/main" });
    }

    void push_local_query(gitman::testing::fake_process_runner& runner, const std::u8string_view status = {})
    {
        runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
        runner.push_response({ gitman::process_completion::exited, 0, status.empty() ? clean_status_output() : std::u8string { status }, {} });
    }

    // 전환 직전의 재검증에 필요한 세 조회다.
    void push_validation_queries(gitman::testing::fake_process_runner& runner, const std::u8string_view worktrees = {})
    {
        runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, sample_reference_output(), {} });
        runner.push_response({ gitman::process_completion::exited, 0, worktrees.empty() ? worktree_output() : std::u8string { worktrees }, {} });
    }

    std::u8string command_name(const gitman::process_request& request)
    {
        return request.arguments.size() > command_name_index ? request.arguments[command_name_index] : std::u8string {};
    }

    std::u8string argument(const gitman::process_request& request, const std::size_t index)
    {
        return index < request.arguments.size() ? request.arguments[index] : std::u8string {};
    }

    std::size_t count_commands(const gitman::testing::fake_process_runner& runner, const std::u8string_view command)
    {
        std::size_t count { 0 };
        for (const gitman::process_request& request : runner.requests())
            if (command_name(request) == command)
                ++count;
        return count;
    }

    const gitman::switch_candidate* find_candidate(const gitman::switch_candidate_result& result, const std::u8string_view display_name) noexcept
    {
        for (const gitman::switch_candidate& candidate : result.candidates)
            if (candidate.display_name == display_name)
                return &candidate;
        return nullptr;
    }

    const gitman::switch_candidate* find_candidate(const std::vector<gitman::switch_candidate>& candidates, const std::u8string_view display_name) noexcept
    {
        for (const gitman::switch_candidate& candidate : candidates)
            if (candidate.display_name == display_name)
                return &candidate;
        return nullptr;
    }

    gitman::switch_candidate remote_target(const std::u8string_view remote, const std::u8string_view branch)
    {
        gitman::switch_candidate candidate {};
        candidate.kind = gitman::switch_candidate_kind::git_remote_branch;
        candidate.display_name = std::u8string { remote } + u8"/" + std::u8string { branch };
        candidate.target = u8"refs/remotes/" + std::u8string { remote } + u8"/" + std::u8string { branch };
        candidate.remote_name = remote;
        return candidate;
    }

    gitman::switch_candidate local_target(const std::u8string_view branch)
    {
        gitman::switch_candidate candidate {};
        candidate.kind = gitman::switch_candidate_kind::git_local_branch;
        candidate.display_name = branch;
        candidate.target = u8"refs/heads/" + std::u8string { branch };
        candidate.local_branch = branch;
        return candidate;
    }

    bool has_diagnostic(const std::vector<gitman::diagnostic>& diagnostics, const gitman::diagnostic_code code) noexcept
    {
        return std::ranges::any_of(diagnostics, [code](const gitman::diagnostic& value) { return value.code == code; });
    }
} // namespace

TEST_CASE("Switch candidates list remote branches before local ones", "[infrastructure][git][switch]")
{
    const std::vector<std::u8string> remotes { u8"origin", u8"fork" };
    const std::vector<gitman::switch_candidate> candidates { gitman::build_git_switch_candidates(sample_references(), remotes, u8"origin") };

    REQUIRE(candidates.size() == 5);
    for (std::size_t index { 0 }; index < 4; ++index)
    {
        INFO(index);
        REQUIRE(candidates[index].kind == gitman::switch_candidate_kind::git_remote_branch);
    }
    REQUIRE(candidates[4].kind == gitman::switch_candidate_kind::git_local_branch);
    REQUIRE(candidates[4].display_name == u8"only-local");

    // 심볼릭 항목은 후보가 아니다.
    REQUIRE(std::ranges::none_of(candidates, [](const gitman::switch_candidate& candidate) { return candidate.target == u8"refs/remotes/origin/HEAD"; }));

    // 같은 branch 이름이 여러 remote에 있어도 합치지 않는다.
    REQUIRE(find_candidate(candidates, u8"origin/forked") != nullptr);
    REQUIRE(find_candidate(candidates, u8"fork/forked") != nullptr);
}

TEST_CASE("Switch candidates carry the matching local branch", "[infrastructure][git][switch]")
{
    const std::vector<gitman::switch_candidate> candidates { gitman::build_git_switch_candidates(sample_references(), { u8"origin", u8"fork" }, u8"origin") };

    const gitman::switch_candidate* const main_branch { find_candidate(candidates, u8"origin/main") };
    REQUIRE(main_branch != nullptr);
    REQUIRE(main_branch->local_branch == u8"main");
    REQUIRE_FALSE(main_branch->requires_tracking_branch);

    const gitman::switch_candidate* const new_branch { find_candidate(candidates, u8"origin/new-branch") };
    REQUIRE(new_branch != nullptr);
    REQUIRE(new_branch->local_branch.empty());
    // 대응하는 local branch가 없으면 만들어야 전환할 수 있다.
    REQUIRE(new_branch->requires_tracking_branch);
    // 후보 조회는 확인 여부를 채우지 않는다. dialog가 사용자에게 물어본 뒤에 켠다.
    REQUIRE_FALSE(new_branch->tracking_branch_confirmed);
}

TEST_CASE("Switch candidates do not repeat a branch that a remote already reaches", "[infrastructure][git][switch]")
{
    const std::vector<gitman::switch_candidate> candidates { gitman::build_git_switch_candidates(sample_references(), { u8"origin", u8"fork" }, u8"origin") };

    // `main`은 `origin/main` 후보로, `forked`는 `fork/forked` 후보로 도달한다.
    REQUIRE(find_candidate(candidates, u8"main") == nullptr);
    REQUIRE(find_candidate(candidates, u8"forked") == nullptr);
    // remote에 같은 이름이 없는 branch는 그대로 남는다.
    REQUIRE(find_candidate(candidates, u8"only-local") != nullptr);

    // upstream이 다른 remote를 가리키면 그 remote 후보로 도달할 수 없다. 이 경우 local
    // 후보를 지우면 그 branch로 전환할 방법이 사라진다.
    const std::vector<std::u8string> conflicting_lines {
        reference_line(u8"refs/heads/main", u8"refs/remotes/fork/main", true),
        reference_line(u8"refs/remotes/origin/main", {}),
    };
    const std::vector<gitman::git_reference_entry> conflicting { gitman::parse_git_reference_list(conflicting_lines) };
    const std::vector<gitman::switch_candidate> with_conflict { gitman::build_git_switch_candidates(conflicting, { u8"origin", u8"fork" }, u8"origin") };
    REQUIRE(with_conflict.size() == 2);
    REQUIRE(find_candidate(with_conflict, u8"main") != nullptr);
}

TEST_CASE("Switch candidates mark remotes that were not refreshed", "[infrastructure][git][switch]")
{
    const std::vector<gitman::switch_candidate> candidates { gitman::build_git_switch_candidates(sample_references(), { u8"origin", u8"fork" }, u8"origin") };

    REQUIRE_FALSE(find_candidate(candidates, u8"origin/main")->stale);
    REQUIRE(find_candidate(candidates, u8"fork/forked")->stale);
    // local branch는 원격을 확인할 필요가 없으므로 stale이 될 수 없다.
    REQUIRE_FALSE(find_candidate(candidates, u8"only-local")->stale);

    const std::vector<gitman::switch_candidate> none_refreshed { gitman::build_git_switch_candidates(sample_references(), { u8"origin", u8"fork" }, {}) };
    REQUIRE(find_candidate(none_refreshed, u8"origin/main")->stale);
}

TEST_CASE("Switch candidates split remote names that contain a slash", "[infrastructure][git][switch]")
{
    const std::vector<std::u8string> lines { reference_line(u8"refs/remotes/team/fork/feature/a", {}) };
    const std::vector<gitman::git_reference_entry> references { gitman::parse_git_reference_list(lines) };

    const std::vector<gitman::switch_candidate> candidates { gitman::build_git_switch_candidates(references, { u8"team/fork" }, u8"team/fork") };
    REQUIRE(candidates.size() == 1);
    REQUIRE(candidates[0].remote_name == u8"team/fork");
    REQUIRE(candidates[0].display_name == u8"team/fork/feature/a");
    REQUIRE(candidates[0].target == u8"refs/remotes/team/fork/feature/a");

    // 설정에 없는 remote가 남긴 tracking ref는 첫 조각으로 나눠 후보에는 남긴다.
    const std::vector<gitman::switch_candidate> orphan { gitman::build_git_switch_candidates(references, {}, {}) };
    REQUIRE(orphan.size() == 1);
    REQUIRE(orphan[0].remote_name == u8"team");
    REQUIRE(orphan[0].stale);
}

TEST_CASE("Local branch collection ignores upstreams that are not remote", "[infrastructure][git][switch]")
{
    const std::vector<gitman::git_local_branch_state> branches { gitman::collect_git_local_branches(sample_references()) };

    REQUIRE(branches.size() == 3);
    REQUIRE(branches[0].name == u8"main");
    REQUIRE(branches[0].upstream == u8"refs/remotes/origin/main");
    REQUIRE(branches[1].name == u8"only-local");
    REQUIRE(branches[1].upstream.empty());

    // `branch.<name>.remote = .`처럼 local branch를 가리키는 upstream은 원격 판정에 쓸 수 없다.
    const std::vector<std::u8string> self_lines { reference_line(u8"refs/heads/topic", u8"refs/heads/base") };
    const std::vector<gitman::git_local_branch_state> self { gitman::collect_git_local_branches(gitman::parse_git_reference_list(self_lines)) };
    REQUIRE(self.size() == 1);
    REQUIRE(self[0].upstream.empty());
}

TEST_CASE("The refreshed remote follows the approved order", "[infrastructure][git][switch]")
{
    REQUIRE(gitman::select_git_candidate_fetch_remote({ u8"origin", u8"fork" }, u8"fork") == u8"fork");
    // 지정한 값이 저장소에 없으면 다음 규칙으로 넘어간다.
    REQUIRE(gitman::select_git_candidate_fetch_remote({ u8"origin", u8"fork" }, u8"없음") == u8"origin");
    REQUIRE(gitman::select_git_candidate_fetch_remote({ u8"upstream" }, {}) == u8"upstream");
    // 좁혀지지 않으면 자동으로 고르지 않는다. fetch하지 않고 목록을 stale로 알린다.
    REQUIRE(gitman::select_git_candidate_fetch_remote({ u8"upstream", u8"fork" }, {}).empty());
    REQUIRE(gitman::select_git_candidate_fetch_remote({}, {}).empty());
    REQUIRE(gitman::select_git_candidate_fetch_remote({}, u8"origin").empty());
}

TEST_CASE("Candidate queries stop before building a command when they cannot run", "[infrastructure][git][switch]")
{
    SECTION("도구 부재")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { missing_tool(), runner, probe };

        const gitman::switch_candidate_result result { provider.query_switch_candidates(make_project(), {}) };
        REQUIRE(result.candidates.empty());
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::vcs_tool_not_found));
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("경로 소멸")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::switch_candidate_result result { provider.query_switch_candidates(make_project(), {}) };
        REQUIRE(result.candidates.empty());
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::path_missing));
        REQUIRE(runner.request_count() == 0);
    }
}

TEST_CASE("A candidate query refreshes one remote and reads every reference", "[infrastructure][git][switch]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
    runner.push_response({ gitman::process_completion::exited, 0, sample_reference_output(), {} });
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::switch_candidate_result result { provider.query_switch_candidates(make_project(), {}) };

    REQUIRE(runner.request_count() == 3);
    REQUIRE(command_name(runner.request(0)) == u8"remote");
    REQUIRE(command_name(runner.request(1)) == u8"fetch");
    REQUIRE(argument(runner.request(1), command_name_index + 1) == u8"--prune");
    REQUIRE(command_name(runner.request(2)) == u8"for-each-ref");

    REQUIRE(result.candidates.size() == 3);
    REQUIRE_FALSE(result.stale);
    REQUIRE(find_candidate(result, u8"origin/main") != nullptr);
    REQUIRE(find_candidate(result, u8"origin/new-branch") != nullptr);
    REQUIRE(find_candidate(result, u8"only-local") != nullptr);
}

TEST_CASE("A failed fetch still yields a candidate list", "[infrastructure][git][switch]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: unable to access 'https://host/repo.git/': Could not resolve host: host\n" });
    runner.push_response({ gitman::process_completion::exited, 0, sample_reference_output(), {} });
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::switch_candidate_result result { provider.query_switch_candidates(make_project(), {}) };

    // 원격을 새로 고치지 못한 것과 후보를 전혀 알 수 없는 것은 사용자가 할 일이 다르다.
    REQUIRE(result.candidates.size() == 3);
    REQUIRE(result.stale);
    REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::remote_unreachable));
    REQUIRE(std::ranges::none_of(result.diagnostics, [](const gitman::diagnostic& value) { return value.severity == gitman::diagnostic_severity::error; }));
}

TEST_CASE("A candidate query does not fetch when it cannot choose a remote", "[infrastructure][git][switch]")
{
    SECTION("remote 없음")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
        runner.push_response({ gitman::process_completion::exited, 0, join_lines({ reference_line(u8"refs/heads/main", {}, true), reference_line(u8"refs/heads/work", {}) }), {} });
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::switch_candidate_result result { provider.query_switch_candidates(make_project(), {}) };
        REQUIRE(runner.request_count() == 2);
        REQUIRE(count_commands(runner, u8"fetch") == 0);
        REQUIRE(result.candidates.size() == 2);
        // 확인할 원격이 없으므로 오래된 목록이 아니다.
        REQUIRE_FALSE(result.stale);
    }

    SECTION("자동으로 고를 수 없는 remote")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        runner.push_response({ gitman::process_completion::exited, 0, u8"upstream\nfork\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, sample_reference_output(), {} });
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::switch_candidate_result result { provider.query_switch_candidates(make_project(), {}) };
        REQUIRE(runner.request_count() == 2);
        REQUIRE(count_commands(runner, u8"fetch") == 0);
        REQUIRE(result.stale);
    }
}

TEST_CASE("A candidate query reports a reference failure instead of an empty list", "[infrastructure][git][switch]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: not a git repository\n" });
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::switch_candidate_result result { provider.query_switch_candidates(make_project(), {}) };
    REQUIRE(result.candidates.empty());
    REQUIRE(result.stale);
    REQUIRE(runner.request_count() == 1);
}

TEST_CASE("An unusable switch target never builds a command", "[infrastructure][git][switch]")
{
    SECTION("빈 대상")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.switch_to(make_project(), {}, {}) };
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.rejected_by == gitman::switch_rejection::target_not_found);
        // 조회조차 하지 않는다.
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("다른 저장소 종류의 대상")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        gitman::switch_candidate target {};
        target.kind = gitman::switch_candidate_kind::subversion_url;
        target.target = u8"https://host/svn/repo/trunk";

        const gitman::repository_change_result result { provider.switch_to(make_project(), target, {}) };
        REQUIRE(result.rejected_by == gitman::switch_rejection::target_not_found);
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("도구 부재")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { missing_tool(), runner, probe };

        const gitman::repository_change_result result { provider.switch_to(make_project(), local_target(u8"only-local"), {}) };
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.rejected_by == gitman::switch_rejection::tool_unavailable);
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("저장소 아님")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: 여기는 git 저장소가 아닙니다\n" });
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.switch_to(make_project(), local_target(u8"only-local"), {}) };
        REQUIRE(result.rejected_by == gitman::switch_rejection::repository_unavailable);
        REQUIRE(runner.request_count() == 1);
        REQUIRE(count_commands(runner, u8"switch") == 0);
    }
}

TEST_CASE("A switch runs the approved command sequence", "[infrastructure][git][switch]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    push_local_query(runner);
    push_validation_queries(runner);
    runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
    push_local_query(runner);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(make_project(), local_target(u8"only-local"), {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    REQUIRE(result.rejected_by == gitman::switch_rejection::none);

    // 재조회 2 → 재검증 3 → 전환 1 → 사후 재조회 2다.
    REQUIRE(runner.request_count() == 8);
    REQUIRE(command_name(runner.request(0)) == u8"rev-parse");
    REQUIRE(command_name(runner.request(1)) == u8"status");
    REQUIRE(command_name(runner.request(2)) == u8"remote");
    REQUIRE(command_name(runner.request(3)) == u8"for-each-ref");
    REQUIRE(command_name(runner.request(4)) == u8"worktree");
    REQUIRE(command_name(runner.request(5)) == u8"switch");
    REQUIRE(argument(runner.request(5), command_name_index + 3) == u8"only-local");
    REQUIRE(command_name(runner.request(6)) == u8"rev-parse");
    REQUIRE(command_name(runner.request(7)) == u8"status");

    // 전환은 이미 받아 둔 ref로만 한다. 실행 직전에는 원격을 건드리지 않는다.
    REQUIRE(count_commands(runner, u8"fetch") == 0);
}

TEST_CASE("A switch to a remote branch uses the existing local branch", "[infrastructure][git][switch]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    // 현재는 `only-local`에 있고 `origin/main`을 고른다.
    push_local_query(runner, join_lines({ u8"# branch.oid 0123456789abcdef0123456789abcdef01234567", u8"# branch.head only-local" }));
    push_validation_queries(runner, join_lines({ u8"worktree C:/작업 공간/repo", u8"branch refs/heads/only-local" }));
    runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
    push_local_query(runner);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(make_project(), remote_target(u8"origin", u8"main"), {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    // 대응하는 local branch가 있으므로 새로 만들지 않는다.
    REQUIRE(argument(runner.request(5), command_name_index + 2) == u8"--");
    REQUIRE(argument(runner.request(5), command_name_index + 3) == u8"main");
}

TEST_CASE("A tracking branch is created only after the user confirms", "[infrastructure][git][switch]")
{
    const gitman::switch_candidate target { remote_target(u8"origin", u8"new-branch") };

    SECTION("확인 전")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        push_local_query(runner);
        push_validation_queries(runner);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.switch_to(make_project(), target, {}) };

        REQUIRE_FALSE(result.executed);
        REQUIRE(result.rejected_by == gitman::switch_rejection::tracking_branch_confirmation_required);
        // 조회 5개에서 끝나고 branch를 만들지 않는다.
        REQUIRE(runner.request_count() == 5);
        REQUIRE(count_commands(runner, u8"switch") == 0);
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::switch_target_rejected));
    }

    SECTION("확인 후")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        push_local_query(runner);
        push_validation_queries(runner);
        runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
        push_local_query(runner);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        gitman::switch_candidate confirmed { target };
        confirmed.tracking_branch_confirmed = true;
        const gitman::repository_change_result result { provider.switch_to(make_project(), confirmed, {}) };

        REQUIRE(result.executed);
        REQUIRE(result.succeeded);
        REQUIRE(command_name(runner.request(5)) == u8"switch");
        REQUIRE(argument(runner.request(5), command_name_index + 2) == u8"--create");
        REQUIRE(argument(runner.request(5), command_name_index + 3) == u8"new-branch");
        REQUIRE(argument(runner.request(5), command_name_index + 4) == u8"--track");
        REQUIRE(argument(runner.request(5), command_name_index + 6) == u8"refs/remotes/origin/new-branch");
    }
}

TEST_CASE("Rejected switches never build a switch command", "[infrastructure][git][switch]")
{
    struct expectation
    {
        std::u8string_view name {};
        std::u8string status {};
        std::u8string worktrees {};
        gitman::switch_candidate target {};
        gitman::switch_rejection rejection {};
    };

    const std::u8string on_main { join_lines({ u8"# branch.oid abcdef0123", u8"# branch.head main", u8"# branch.upstream origin/main", u8"# branch.ab +0 -0" }) };
    const std::u8string on_other { join_lines({ u8"# branch.oid abcdef0123", u8"# branch.head only-local" }) };
    const std::u8string dirty { join_lines({ u8"# branch.oid abcdef0123", u8"# branch.head only-local", u8"1 .M N... 100644 100644 100644 a a a.txt" }) };
    const std::u8string here { join_lines({ u8"worktree C:/작업 공간/repo", u8"branch refs/heads/only-local" }) };
    const std::u8string elsewhere { join_lines({ u8"worktree C:/작업 공간/repo", u8"branch refs/heads/only-local", u8"", u8"worktree C:/작업 공간/other", u8"branch refs/heads/main" }) };

    const expectation expectations[] {
        { u8"이미 대상", on_main, {}, remote_target(u8"origin", u8"main"), gitman::switch_rejection::already_on_target },
        { u8"목록에 없는 대상", on_other, here, remote_target(u8"origin", u8"없는-branch"), gitman::switch_rejection::target_not_found },
        { u8"다른 worktree 사용 중", on_other, elsewhere, remote_target(u8"origin", u8"main"), gitman::switch_rejection::target_in_use },
        { u8"dirty 작업 트리", dirty, here, remote_target(u8"origin", u8"main"), gitman::switch_rejection::working_tree_unsafe },
    };

    for (const expectation& value : expectations)
    {
        INFO(reinterpret_cast<const char*>(value.name.data()));
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        push_local_query(runner, value.status);
        push_validation_queries(runner, value.worktrees);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.switch_to(make_project(), value.target, {}) };

        REQUIRE_FALSE(result.executed);
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(result.rejected_by == value.rejection);
        // REQ-007의 수용 기준이다. 검증에 실패한 전환은 명령을 만들지 않는다.
        REQUIRE(count_commands(runner, u8"switch") == 0);
    }
}

TEST_CASE("A switch stops when it cannot see which branches are in use", "[infrastructure][git][switch]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    push_local_query(runner);
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, sample_reference_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"fatal: 실패\n" });
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(make_project(), local_target(u8"only-local"), {}) };

    // 어떤 branch가 다른 worktree에 잡혀 있는지 모르는 채로 전환하지 않는다.
    REQUIRE_FALSE(result.executed);
    REQUIRE(result.rejected_by == gitman::switch_rejection::repository_unavailable);
    REQUIRE(count_commands(runner, u8"switch") == 0);
}

TEST_CASE("A failed switch is still followed by a fresh query", "[infrastructure][git][switch]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    push_local_query(runner);
    push_validation_queries(runner);
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: 전환할 수 없습니다\n" });
    push_local_query(runner);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(make_project(), local_target(u8"only-local"), {}) };

    // 실행 자체는 했으므로 차단이 아니다. 성공 여부와 조회 결과는 분리해 보고한다.
    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.rejected_by == gitman::switch_rejection::none);
    REQUIRE(runner.request_count() == 8);
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(std::ranges::any_of(result.diagnostics, [](const gitman::diagnostic& value) { return value.severity == gitman::diagnostic_severity::error; }));
}

TEST_CASE("Switch failures are classified by locale independent signals", "[infrastructure][git][switch]")
{
    struct expectation
    {
        std::u8string_view name {};
        gitman::testing::fake_process_runner::response response {};
        gitman::diagnostic_code code {};
    };

    const expectation expectations[] {
        { u8"timeout", { gitman::process_completion::timed_out, 0, {}, {} }, gitman::diagnostic_code::process_timed_out },
        { u8"취소", { gitman::process_completion::cancelled, 0, {}, {} }, gitman::diagnostic_code::process_cancelled },
        { u8"그 밖의 실패", { gitman::process_completion::exited, 1, {}, u8"fatal: 알 수 없는 오류\n" }, gitman::diagnostic_code::vcs_command_failed },
    };

    for (const expectation& value : expectations)
    {
        INFO(reinterpret_cast<const char*>(value.name.data()));
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        push_local_query(runner);
        push_validation_queries(runner);
        runner.push_response(value.response);
        push_local_query(runner);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.switch_to(make_project(), local_target(u8"only-local"), {}) };

        REQUIRE(result.executed);
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_diagnostic(result.diagnostics, value.code));
    }
}
