#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_tool.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/git_repository_provider.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    constexpr std::u8string_view repository_path { u8"C:\\작업 공간\\repo" };
    constexpr std::u8string_view git_directory { u8"C:/작업 공간/repo/.git" };
    // 공통 인자 뒤에서 명령 이름이 시작하는 자리다.
    constexpr std::size_t command_name_index { 7 };

    gitman::vcs_tool_info available_tool()
    {
        gitman::vcs_tool_info tool {};
        tool.kind = gitman::repository_kind::git;
        tool.availability = gitman::vcs_tool_availability::available;
        tool.executable = u8"C:\\Program Files\\Git\\cmd\\git.exe";
        tool.reported_version = u8"git version 2.52.0.windows.1";
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

    gitman::project_definition make_project(const std::u8string_view path = repository_path)
    {
        gitman::project_definition project {};
        project.id.value = u8"repo-1";
        project.path.original = path;
        project.path.normalized = path;
        project.display_name = u8"repo";
        return project;
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
        return join_lines({ git_directory, u8"false", u8"true", u8"C:/작업 공간/repo" });
    }

    std::u8string clean_status_output()
    {
        return join_lines({ u8"# branch.oid 0123456789abcdef0123456789abcdef01234567", u8"# branch.head main", u8"# branch.upstream origin/main", u8"# branch.ab +0 -0" });
    }

    bool has_diagnostic(const gitman::repository_query_result& result, const gitman::diagnostic_code code) noexcept
    {
        return std::ranges::any_of(result.diagnostics, [code](const gitman::diagnostic& value) { return value.code == code; });
    }

    // provider는 명령을 만들기 전에 등록 경로가 디렉터리인지 확인한다. 도우미는 표식
    // 파일뿐 아니라 작업 디렉터리도 등록해야 한다.
    void register_working_directory(gitman::testing::fake_vcs_file_probe& probe, const std::u8string_view path = repository_path)
    {
        probe.add_directory(path);
    }
} // namespace

TEST_CASE("Local queries do nothing when Git is missing", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { missing_tool(), runner, probe };

    REQUIRE_FALSE(provider.available());
    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::tool_unavailable);
    REQUIRE(result.snapshot.kind == gitman::repository_kind::git);
    // 도구 부재는 앱을 멈추는 오류가 아니다. 카드는 목록에 남고 동작만 비활성화된다.
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("Local queries reject paths before starting a process", "[infrastructure][git][provider]")
{
    SECTION("상대 경로")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_directory(u8"repo");
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_local(make_project(u8"repo"), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::path_unavailable);
        REQUIRE(result.has_errors());
        REQUIRE(runner.request_count() == 0);
    }

    SECTION("사라진 경로")
    {
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_local(make_project(), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::path_unavailable);
        REQUIRE(has_diagnostic(result, gitman::diagnostic_code::path_missing));
        REQUIRE(runner.request_count() == 0);
    }
}

TEST_CASE("Local queries build only the two local commands", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, clean_status_output(), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(runner.request_count() == 2);
    REQUIRE(runner.request(0).arguments.at(command_name_index) == u8"rev-parse");
    REQUIRE(runner.request(1).arguments.at(command_name_index) == u8"status");
    REQUIRE(runner.request(0).working_directory == repository_path);
    REQUIRE(runner.request(1).working_directory == repository_path);
    REQUIRE(runner.requests_for_executable_suffix(u8"git.exe") == 2);
    // 로컬 조회는 네트워크를 쓰는 명령을 만들지 않는다.
    for (const gitman::process_request& request : runner.requests())
        for (const std::u8string& argument : request.arguments)
            REQUIRE(argument != u8"fetch");
}

TEST_CASE("Clean repositories become a ready snapshot", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, clean_status_output(), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.project.value == u8"repo-1");
    REQUIRE(result.snapshot.repository_root == u8"C:/작업 공간/repo");
    REQUIRE(result.snapshot.current_reference == u8"main");
    REQUIRE(result.snapshot.local_revision == u8"0123456789abcdef0123456789abcdef01234567");
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
    REQUIRE(result.snapshot.working_tree.is_safe_for_change());
    REQUIRE(result.snapshot.local_checked_at.has_value());
    // 로컬 조회는 원격을 확인하지 않으므로 원격 확인 시각을 만들지 않는다.
    REQUIRE_FALSE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("Cached upstream comparison is reported as a local comparison", "[infrastructure][git][provider]")
{
    const std::u8string status { join_lines({ u8"# branch.oid abc", u8"# branch.head main", u8"# branch.upstream origin/main", u8"# branch.ab +2 -3" }) };

    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, status, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    // 이미 받아 둔 remote tracking ref와의 비교다. 원격을 실제로 확인하는 remote-first
    // 판정은 `S4-D3`이 덮어쓴다.
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::local);
    REQUIRE(result.snapshot.comparison_target == u8"origin/main");
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::diverged);
    REQUIRE(result.snapshot.ahead_count == 2);
    REQUIRE(result.snapshot.behind_count == 3);
}

TEST_CASE("Ahead and behind counts map to sync states", "[infrastructure][git][provider]")
{
    struct expectation
    {
        std::u8string_view ahead_behind {};
        gitman::remote_sync_state state {};
    };

    const expectation expectations[] {
        { u8"# branch.ab +0 -0", gitman::remote_sync_state::up_to_date },
        { u8"# branch.ab +3 -0", gitman::remote_sync_state::ahead },
        { u8"# branch.ab +0 -4", gitman::remote_sync_state::behind },
        { u8"# branch.ab +1 -1", gitman::remote_sync_state::diverged },
    };

    for (const expectation& value : expectations)
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
        runner.push_response({ gitman::process_completion::exited, 0, join_lines({ u8"# branch.oid abc", u8"# branch.head main", u8"# branch.upstream origin/main", value.ahead_behind }), {} });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        REQUIRE(provider.query_local(make_project(), {}).snapshot.sync_state == value.state);
    }
}

TEST_CASE("Branches without upstream keep the remote state unknown", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, join_lines({ u8"# branch.oid abc", u8"# branch.head feature" }), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    // `local_only`와 `remote_target_missing`은 `git remote` 결과가 있어야 구분된다.
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::none);
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::unknown);
}

TEST_CASE("Working tree markers are added to the snapshot", "[infrastructure][git][provider]")
{
    const std::u8string status { join_lines({ u8"# branch.oid abcdef0123456789", u8"# branch.head (detached)", u8"1 .M N... 100644 100644 100644 5626abf 5626abf a.txt" }) };

    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, status, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    probe.add_file(u8"C:/작업 공간/repo/.git/MERGE_HEAD");
    probe.add_file(u8"C:/작업 공간/repo/.git/index.lock");
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.current_reference == u8"(detached) abcdef0");
    REQUIRE(result.snapshot.working_tree.is_detached);
    REQUIRE(result.snapshot.working_tree.operation_in_progress);
    REQUIRE(result.snapshot.working_tree.has_index_lock);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::modified);
    REQUIRE_FALSE(result.snapshot.working_tree.is_safe_for_change());
}

TEST_CASE("Index locks are reported apart from interrupted operations", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, clean_status_output(), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    probe.add_file(u8"C:/작업 공간/repo/.git/index.lock");
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    // 중단된 작업이 아니라 지금 다른 Git 프로세스가 도는 상태다. 사용자가 할 일이 다르다.
    REQUIRE(result.snapshot.working_tree.has_index_lock);
    REQUIRE_FALSE(result.snapshot.working_tree.operation_in_progress);
    REQUIRE_FALSE(result.snapshot.working_tree.is_safe_for_change());
}

TEST_CASE("Repositories without commits keep an empty revision", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, join_lines({ u8"# branch.oid (initial)", u8"# branch.head main" }), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.current_reference == u8"main");
    REQUIRE(result.snapshot.local_revision.empty());
}

TEST_CASE("Directories that are not repositories are classified without message text", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    // 한국어 환경의 Git 메시지다. 분류는 메시지 본문이 아니라 "정상 종료했는데 출력이
    // 없다"는 구조적 신호로 해야 한다.
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"치명적: Git 저장소가 아닙니다\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::not_a_repository);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::repository_not_found));
    // 저장소가 아니면 상태 조회를 시도하지 않는다.
    REQUIRE(runner.request_count() == 1);
}

TEST_CASE("Bare repositories and git directories are unsupported layouts", "[infrastructure][git][provider]")
{
    SECTION("bare 저장소")
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ gitman::process_completion::exited, 128, join_lines({ u8"C:/bare", u8"true", u8"false" }), u8"fatal: this operation must be run in a work tree\n" });
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_directory(u8"C:\\bare");
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_local(make_project(u8"C:\\bare"), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::unsupported_layout);
        REQUIRE(result.snapshot.repository_root == u8"C:/bare");
        // 지원하지 않는 배치는 오류가 아니라 카드에 표시할 사유다.
        REQUIRE_FALSE(result.has_errors());
        REQUIRE(runner.request_count() == 1);
    }

    SECTION("git dir 안의 경로")
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ gitman::process_completion::exited, 128, join_lines({ git_directory, u8"false", u8"false" }), {} });
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_directory(u8"C:\\작업 공간\\repo\\.git");
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_local(make_project(u8"C:\\작업 공간\\repo\\.git"), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::unsupported_layout);
        REQUIRE(runner.request_count() == 1);
    }
}

TEST_CASE("Process failures are promoted instead of guessing a repository state", "[infrastructure][git][provider]")
{
    struct expectation
    {
        gitman::process_completion completion {};
        gitman::diagnostic_code code {};
    };

    const expectation expectations[] {
        { gitman::process_completion::timed_out, gitman::diagnostic_code::process_timed_out },
        { gitman::process_completion::start_failed, gitman::diagnostic_code::process_start_failed },
        { gitman::process_completion::internal_error, gitman::diagnostic_code::process_start_failed },
    };

    for (const expectation& value : expectations)
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ value.completion, 0, {}, {} });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::git_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_query_result result { provider.query_local(make_project(), {}) };
        REQUIRE(result.snapshot.availability == gitman::repository_availability::unknown);
        REQUIRE(has_diagnostic(result, value.code));
        REQUIRE(result.has_errors());
    }
}

TEST_CASE("Cancelled local queries do not report a missing repository", "[infrastructure][git][provider]")
{
    gitman::process_cancellation_source source {};
    source.request_cancellation();

    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), source.token()) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::unknown);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::process_cancelled));
}

TEST_CASE("Status failures keep the known repository root", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 128, {}, u8"fatal: unable to read index\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    // 저장소인 것은 확인했지만 상태를 읽지 못했다. 상태를 단정하지 않는다.
    REQUIRE(result.snapshot.availability == gitman::repository_availability::unknown);
    REQUIRE(result.snapshot.repository_root == u8"C:/작업 공간/repo");
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::unknown);
    REQUIRE(result.has_errors());
    REQUIRE(runner.request_count() == 2);
}

TEST_CASE("Unreadable status records are reported as a warning", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, join_lines({ u8"# branch.oid abc", u8"# branch.head main", u8"9 broken record" }), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::unknown);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::vcs_output_unparsable));
    REQUIRE_FALSE(result.has_errors());
}

TEST_CASE("Operations that are not implemented yet build no request", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { available_tool(), runner, probe };
    const gitman::project_definition project { make_project() };

    gitman::repository_snapshot local {};
    local.availability = gitman::repository_availability::ready;
    local.current_reference = u8"main";

    // 원격 판정은 `S4-D3`, update는 `S4-D5`, switch와 후보 조회는 `S4-D6` 구간이다.
    const gitman::repository_query_result remote { provider.query_remote(project, local, {}) };
    REQUIRE(remote.snapshot.current_reference == u8"main");
    REQUIRE(remote.snapshot.availability == gitman::repository_availability::ready);

    const gitman::switch_candidate_result candidates { provider.query_switch_candidates(project, {}) };
    REQUIRE(candidates.candidates.empty());
    REQUIRE_FALSE(candidates.stale);

    const gitman::repository_change_result updated { provider.update(project, {}, {}) };
    REQUIRE_FALSE(updated.executed);
    REQUIRE_FALSE(updated.succeeded);

    const gitman::repository_change_result switched { provider.switch_to(project, {}, {}) };
    REQUIRE_FALSE(switched.executed);

    // REQ-007의 수용 기준이다. 실행하지 않은 동작은 process request를 만들지 않는다.
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("Refreshed tool information is picked up", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, layout_output(), {} });
    runner.push_response({ gitman::process_completion::exited, 0, clean_status_output(), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::git_repository_provider provider { missing_tool(), runner, probe };

    REQUIRE_FALSE(provider.available());
    // 도구를 설치한 뒤 앱을 다시 시작하지 않아도 카드가 다시 동작해야 한다.
    provider.set_tool(available_tool());

    REQUIRE(provider.available());
    REQUIRE(provider.tool().version == gitman::vcs_tool_version { 2, 52, 0 });
    REQUIRE(provider.query_local(make_project(), {}).snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(runner.request_count() == 2);
}

TEST_CASE("The Git provider reports its kind", "[infrastructure][git][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    const gitman::git_repository_provider provider { available_tool(), runner, probe };

    REQUIRE(provider.kind() == gitman::repository_kind::git);
}

TEST_CASE("In progress markers are detected from the git directory", "[infrastructure][git][provider]")
{
    SECTION("표식이 없으면 진행 중 작업이 없다")
    {
        gitman::testing::fake_vcs_file_probe probe {};
        REQUIRE_FALSE(gitman::detect_git_in_progress_markers(probe, git_directory).operation_in_progress());
    }

    SECTION("merge, cherry-pick, revert와 bisect 표식")
    {
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_file(u8"C:/작업 공간/repo/.git/MERGE_HEAD");
        probe.add_file(u8"C:/작업 공간/repo/.git/CHERRY_PICK_HEAD");
        probe.add_file(u8"C:/작업 공간/repo/.git/REVERT_HEAD");
        probe.add_file(u8"C:/작업 공간/repo/.git/BISECT_LOG");

        const gitman::git_in_progress_markers markers { gitman::detect_git_in_progress_markers(probe, git_directory) };
        REQUIRE(markers.merge);
        REQUIRE(markers.cherry_pick);
        REQUIRE(markers.revert);
        REQUIRE(markers.bisect);
        REQUIRE_FALSE(markers.rebase);
        REQUIRE(markers.operation_in_progress());
    }

    SECTION("rebase는 디렉터리 표식이다")
    {
        gitman::testing::fake_vcs_file_probe merge_backend {};
        merge_backend.add_directory(u8"C:/작업 공간/repo/.git/rebase-merge");
        REQUIRE(gitman::detect_git_in_progress_markers(merge_backend, git_directory).rebase);

        gitman::testing::fake_vcs_file_probe apply_backend {};
        apply_backend.add_directory(u8"C:/작업 공간/repo/.git/rebase-apply");
        REQUIRE(gitman::detect_git_in_progress_markers(apply_backend, git_directory).rebase);

        // 같은 이름의 파일은 rebase 표식이 아니다.
        gitman::testing::fake_vcs_file_probe as_file {};
        as_file.add_file(u8"C:/작업 공간/repo/.git/rebase-merge");
        REQUIRE_FALSE(gitman::detect_git_in_progress_markers(as_file, git_directory).rebase);
    }

    SECTION("linked worktree의 표식은 worktree 전용 디렉터리에 있다")
    {
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_file(u8"C:/repo/.git/worktrees/feature/MERGE_HEAD");

        REQUIRE(gitman::detect_git_in_progress_markers(probe, u8"C:/repo/.git/worktrees/feature").merge);
        // 주 저장소의 git dir에는 표식이 없다.
        REQUIRE_FALSE(gitman::detect_git_in_progress_markers(probe, u8"C:/repo/.git").merge);
    }

    SECTION("git dir을 모르면 조회하지 않는다")
    {
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_file(u8"MERGE_HEAD");
        REQUIRE_FALSE(gitman::detect_git_in_progress_markers(probe, u8"").operation_in_progress());
    }
}
