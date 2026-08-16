#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "domain/vcs_tool.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/git_repository_provider.h"

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

    std::u8string layout_output()
    {
        return join_lines({ u8"C:/작업 공간/repo/.git", u8"false", u8"true", u8"C:/작업 공간/repo" });
    }

    std::u8string clean_status_output()
    {
        return join_lines({ u8"# branch.oid 0123456789abcdef0123456789abcdef01234567", u8"# branch.head main", u8"# branch.upstream origin/main", u8"# branch.ab +0 -2" });
    }

    // 로컬 조회 한 번의 응답이다. update는 사전 검사와 사후 재조회에서 두 번 사용한다.
    void push_local_query(gitman::testing::fake_process_runner& runner, const std::u8string_view status)
    {
        runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { status }, {} });
    }

    std::size_t count_commands(const gitman::testing::fake_process_runner& runner, const std::u8string_view command)
    {
        std::size_t count { 0 };
        for (const gitman::process_request& request : runner.requests())
            if (request.arguments.size() > command_name_index && request.arguments[command_name_index] == command)
                ++count;
        return count;
    }

    gitman::repository_snapshot ready_snapshot()
    {
        gitman::repository_snapshot snapshot {};
        snapshot.availability = gitman::repository_availability::ready;
        snapshot.current_reference = u8"main";
        snapshot.local_revision = u8"abcdef";
        snapshot.working_tree.state = gitman::working_tree_state::clean;
        snapshot.comparison = gitman::comparison_source::local;
        snapshot.comparison_target = u8"origin/main";
        return snapshot;
    }

    bool has_diagnostic(const gitman::repository_change_result& result, const gitman::diagnostic_code code) noexcept
    {
        return std::ranges::any_of(result.diagnostics, [code](const gitman::diagnostic& value) { return value.code == code; });
    }
} // namespace

TEST_CASE("Update preflight refuses every unsafe repository state", "[infrastructure][git][update]")
{
    REQUIRE(gitman::evaluate_git_update_preflight(ready_snapshot()) == gitman::update_block_reason::none);

    struct expectation
    {
        std::u8string_view name {};
        gitman::repository_snapshot snapshot {};
        gitman::update_block_reason reason {};
    };

    std::vector<expectation> expectations {};
    {
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.availability = gitman::repository_availability::not_a_repository;
        expectations.push_back({ u8"저장소 아님", snapshot, gitman::update_block_reason::repository_unavailable });
    }
    {
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.working_tree.state = gitman::working_tree_state::conflicted;
        expectations.push_back({ u8"충돌", snapshot, gitman::update_block_reason::working_tree_conflicted });
    }
    {
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.working_tree.operation_in_progress = true;
        expectations.push_back({ u8"진행 중 작업", snapshot, gitman::update_block_reason::operation_in_progress });
    }
    {
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.working_tree.has_index_lock = true;
        expectations.push_back({ u8"index.lock", snapshot, gitman::update_block_reason::index_locked });
    }
    {
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.working_tree.is_detached = true;
        expectations.push_back({ u8"detached", snapshot, gitman::update_block_reason::detached_head });
    }
    {
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.working_tree.state = gitman::working_tree_state::modified;
        expectations.push_back({ u8"dirty", snapshot, gitman::update_block_reason::working_tree_dirty });
    }
    {
        // 모르는 상태에서 변경 명령을 실행하는 편이 더 위험하다.
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.working_tree.state = gitman::working_tree_state::unknown;
        expectations.push_back({ u8"미상", snapshot, gitman::update_block_reason::working_tree_dirty });
    }
    {
        gitman::repository_snapshot snapshot { ready_snapshot() };
        snapshot.sync_state = gitman::remote_sync_state::diverged;
        expectations.push_back({ u8"diverged", snapshot, gitman::update_block_reason::diverged });
    }

    for (const expectation& value : expectations)
    {
        INFO(reinterpret_cast<const char*>(value.name.data()));
        REQUIRE(gitman::evaluate_git_update_preflight(value.snapshot) == value.reason);
    }
}

TEST_CASE("Update preflight allows the states that update exists for", "[infrastructure][git][update]")
{
    gitman::repository_snapshot behind { ready_snapshot() };
    behind.sync_state = gitman::remote_sync_state::behind;
    behind.behind_count = 3;
    REQUIRE(gitman::evaluate_git_update_preflight(behind) == gitman::update_block_reason::none);

    gitman::repository_snapshot up_to_date { ready_snapshot() };
    up_to_date.sync_state = gitman::remote_sync_state::up_to_date;
    REQUIRE(gitman::evaluate_git_update_preflight(up_to_date) == gitman::update_block_reason::none);

    // 원격을 아직 확인하지 않은 상태도 막지 않는다. `--ff-only`가 최후의 방어선이다.
    gitman::repository_snapshot unknown_remote { ready_snapshot() };
    unknown_remote.sync_state = gitman::remote_sync_state::unknown;
    REQUIRE(gitman::evaluate_git_update_preflight(unknown_remote) == gitman::update_block_reason::none);
}

TEST_CASE("Update preflight reports the reason to solve first", "[infrastructure][git][update]")
{
    gitman::repository_snapshot snapshot { ready_snapshot() };
    snapshot.working_tree.state = gitman::working_tree_state::conflicted;
    snapshot.working_tree.operation_in_progress = true;
    snapshot.working_tree.has_index_lock = true;
    snapshot.working_tree.is_detached = true;
    snapshot.sync_state = gitman::remote_sync_state::diverged;

    // 사유가 여럿이면 사용자가 먼저 해결해야 하는 것을 돌려준다.
    REQUIRE(gitman::evaluate_git_update_preflight(snapshot) == gitman::update_block_reason::working_tree_conflicted);

    snapshot.working_tree.state = gitman::working_tree_state::clean;
    REQUIRE(gitman::evaluate_git_update_preflight(snapshot) == gitman::update_block_reason::operation_in_progress);

    snapshot.working_tree.operation_in_progress = false;
    REQUIRE(gitman::evaluate_git_update_preflight(snapshot) == gitman::update_block_reason::index_locked);

    snapshot.working_tree.has_index_lock = false;
    REQUIRE(gitman::evaluate_git_update_preflight(snapshot) == gitman::update_block_reason::detached_head);

    snapshot.working_tree.is_detached = false;
    REQUIRE(gitman::evaluate_git_update_preflight(snapshot) == gitman::update_block_reason::diverged);
}

TEST_CASE("Submodule preflight blocks conflicts and mismatched commits", "[infrastructure][git][update]")
{
    gitman::submodule_status healthy {};
    healthy.relative_path = u8"vendor/lib";
    healthy.revision = u8"abc";

    gitman::submodule_status mismatched { healthy };
    mismatched.revision_mismatch = true;

    gitman::submodule_status conflicted { healthy };
    conflicted.conflicted = true;

    gitman::submodule_status uninitialized { healthy };
    uninitialized.initialized = false;

    REQUIRE(gitman::evaluate_git_submodule_preflight({}) == gitman::update_block_reason::none);
    REQUIRE(gitman::evaluate_git_submodule_preflight({ healthy }) == gitman::update_block_reason::none);
    // `--init`이 그대로 처리하므로 위험이 아니다.
    REQUIRE(gitman::evaluate_git_submodule_preflight({ uninitialized }) == gitman::update_block_reason::none);
    REQUIRE(gitman::evaluate_git_submodule_preflight({ mismatched }) == gitman::update_block_reason::submodule_unsafe);
    REQUIRE(gitman::evaluate_git_submodule_preflight({ conflicted }) == gitman::update_block_reason::submodule_unsafe);
    // 하나라도 위험하면 전체를 막는다.
    REQUIRE(gitman::evaluate_git_submodule_preflight({ healthy, uninitialized, conflicted }) == gitman::update_block_reason::submodule_unsafe);
}

TEST_CASE("Update does nothing when Git is missing", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::vcs_tool_info tool {};
    tool.kind = gitman::repository_kind::git;
    tool.availability = gitman::vcs_tool_availability::not_found;
    gitman::git_repository_provider provider { tool, runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.blocked_by == gitman::update_block_reason::tool_unavailable);
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("Blocked updates never build a change command", "[infrastructure][git][update]")
{
    struct expectation
    {
        std::u8string_view status {};
        gitman::update_block_reason reason {};
    };

    const std::u8string dirty { join_lines({ u8"# branch.oid abc", u8"# branch.head main", u8"1 .M N... 100644 100644 100644 a a a.txt" }) };
    const std::u8string conflicted { join_lines({ u8"# branch.oid abc", u8"# branch.head main", u8"u UU N... 100644 100644 100644 100644 a b c f.txt" }) };
    const std::u8string detached { join_lines({ u8"# branch.oid abcdef0123", u8"# branch.head (detached)" }) };
    const std::u8string diverged { join_lines({ u8"# branch.oid abc", u8"# branch.head main", u8"# branch.upstream origin/main", u8"# branch.ab +1 -1" }) };

    const expectation expectations[] {
        { dirty, gitman::update_block_reason::working_tree_dirty },
        { conflicted, gitman::update_block_reason::working_tree_conflicted },
        { detached, gitman::update_block_reason::detached_head },
        { diverged, gitman::update_block_reason::diverged },
    };

    for (const expectation& value : expectations)
    {
        gitman::testing::fake_process_runner runner {};
        push_local_query(runner, value.status);
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.blocked_by == value.reason);
        REQUIRE(has_diagnostic(result, gitman::diagnostic_code::update_blocked));
        // 사전 검사에 쓴 상태를 함께 돌려주어 카드가 사유를 보여 줄 수 있다.
        REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
        // 조회 두 개뿐이며 변경 명령이 없다.
        REQUIRE(runner.request_count() == 2);
        REQUIRE(count_commands(runner, u8"pull") == 0);
    }
}

TEST_CASE("Interrupted operations block updates before any command", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    // 중단된 merge와 다른 Git 프로세스는 porcelain 출력에 나오지 않는다.
    probe.add_file(u8"C:/작업 공간/repo/.git/MERGE_HEAD");
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.blocked_by == gitman::update_block_reason::operation_in_progress);
    REQUIRE(count_commands(runner, u8"pull") == 0);
}

TEST_CASE("Updates without a comparable remote are blocked", "[infrastructure][git][update]")
{
    SECTION("remote가 없다")
    {
        gitman::testing::fake_process_runner runner {};
        push_local_query(runner, join_lines({ u8"# branch.oid abc", u8"# branch.head main" }));
        runner.push_response({ gitman::process_completion::exited, 0, {}, {} });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.blocked_by == gitman::update_block_reason::no_remote_target);
        REQUIRE(count_commands(runner, u8"pull") == 0);
    }

    SECTION("대상이 모호하다")
    {
        gitman::testing::fake_process_runner runner {};
        push_local_query(runner, join_lines({ u8"# branch.oid abc", u8"# branch.head main" }));
        runner.push_response({ gitman::process_completion::exited, 0, u8"alpha\nbeta\n", {} });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };
        REQUIRE_FALSE(result.executed);
        REQUIRE(result.blocked_by == gitman::update_block_reason::no_remote_target);
        REQUIRE(count_commands(runner, u8"pull") == 0);
    }
}

TEST_CASE("A successful update pulls the named remote and branch", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"Fast-forward\n", {} });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    REQUIRE(result.blocked_by == gitman::update_block_reason::none);
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);

    // 조회 2 + remote + pull + 사후 조회 2다.
    REQUIRE(runner.request_count() == 6);
    REQUIRE(runner.request(2).arguments.at(command_name_index) == u8"remote");
    REQUIRE(runner.request(3).arguments.at(command_name_index) == u8"pull");
    REQUIRE(runner.request(3).arguments.back() == u8"main");
    // 사후 재조회를 수행한다.
    REQUIRE(count_commands(runner, u8"rev-parse") == 2);
    REQUIRE(count_commands(runner, u8"status") == 2);
    REQUIRE(count_commands(runner, u8"submodule") == 0);
}

TEST_CASE("A failed fast forward is reported without losing the new state", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: Not possible to fast-forward, aborting.\n" });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    // 실행 여부와 성공 여부는 다른 값이다.
    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::vcs_command_failed));
    // 실패해도 사후 재조회를 수행한다.
    REQUIRE(count_commands(runner, u8"rev-parse") == 2);
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
}

TEST_CASE("Authentication failures during update are classified", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"치명적: 접근 거부: Permission denied (publickey).\n" });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    // 로캘 독립 신호로 판정한다.
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::authentication_required));
}

TEST_CASE("Submodules are left alone unless the option is on", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"Fast-forward\n", {} });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    // ADR-003에 따라 기본값은 off다.
    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE(result.succeeded);
    REQUIRE(count_commands(runner, u8"submodule") == 0);
    REQUIRE(runner.request(3).arguments.at(command_name_index + 2) == u8"--recurse-submodules=no");
}

TEST_CASE("Enabling submodules adds a survey and an update", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8" abc vendor/lib (heads/main)\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"Fast-forward\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"Submodule path 'vendor/lib': checked out\n", {} });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::update_options options {};
    options.update_submodules = true;
    const gitman::repository_change_result result { provider.update(make_project(), options, {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    // 조회 2 + remote + submodule status + pull + submodule update + 사후 조회 2다.
    REQUIRE(runner.request_count() == 8);
    REQUIRE(runner.request(3).arguments.at(command_name_index) == u8"submodule");
    REQUIRE(runner.request(3).arguments.at(command_name_index + 1) == u8"status");
    REQUIRE(runner.request(4).arguments.at(command_name_index) == u8"pull");
    REQUIRE(runner.request(4).arguments.at(command_name_index + 2) == u8"--recurse-submodules=on-demand");
    REQUIRE(runner.request(5).arguments.at(command_name_index + 1) == u8"update");
    REQUIRE(result.snapshot.submodules.size() == 1);
    REQUIRE(result.snapshot.submodules.front().relative_path == u8"vendor/lib");
}

TEST_CASE("An unsafe submodule stops the parent pull", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    // `+`는 등록된 커밋과 다른 커밋이 checkout되어 있다는 뜻이다.
    runner.push_response({ gitman::process_completion::exited, 0, u8" abc vendor/ok (heads/main)\n+def vendor/moved (heads/main)\n", {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::update_options options {};
    options.update_submodules = true;
    const gitman::repository_change_result result { provider.update(make_project(), options, {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.blocked_by == gitman::update_block_reason::submodule_unsafe);
    // 부분적으로 갱신된 상태가 가장 되돌리기 어렵다.
    REQUIRE(count_commands(runner, u8"pull") == 0);
    REQUIRE(count_commands(runner, u8"submodule") == 1);
    REQUIRE(result.snapshot.submodules.size() == 2);
}

TEST_CASE("Submodules are only updated after the parent succeeds", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8" abc vendor/lib (heads/main)\n", {} });
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: Not possible to fast-forward, aborting.\n" });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::update_options options {};
    options.update_submodules = true;
    const gitman::repository_change_result result { provider.update(make_project(), options, {}) };

    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    // 실패한 pull 뒤에 submodule을 옮기면 되돌리기 어려운 조합이 남는다.
    REQUIRE(count_commands(runner, u8"submodule") == 1);
}

TEST_CASE("A failed submodule update makes the whole update fail", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8" abc vendor/lib (heads/main)\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"Fast-forward\n", {} });
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"fatal: clone of 'x' into submodule path failed\n" });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    gitman::update_options options {};
    options.update_submodules = true;
    const gitman::repository_change_result result { provider.update(make_project(), options, {}) };

    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
}

TEST_CASE("Timed out updates keep the block reason clear", "[infrastructure][git][update]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_query(runner, clean_status_output());
    runner.push_response({ gitman::process_completion::exited, 0, u8"origin\n", {} });
    runner.push_response({ gitman::process_completion::timed_out, 0, {}, {} });
    push_local_query(runner, clean_status_output());
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    // 실행은 했으므로 차단이 아니다. 실패 사유는 단계 3의 완료 사유를 그대로 쓴다.
    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.blocked_by == gitman::update_block_reason::none);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::process_timed_out));
}
