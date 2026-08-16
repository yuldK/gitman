#include "application/process_request.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "helpers/git_repository_fixture.h"
#include "infrastructure/git_repository_provider.h"
#include "infrastructure/vcs_command_runner.h"
#include "infrastructure/vcs_execution_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {
    gitman::project_definition make_project(const std::u8string_view path)
    {
        gitman::project_definition project {};
        project.id.value = u8"integration";
        project.path.original = path;
        project.path.normalized = path;
        project.display_name = u8"integration";
        return project;
    }

    // `repository_change_result`에는 `has_errors()`가 없다. 실행 결과는 `executed`와
    // `succeeded`로 성패를 나타내므로 진단은 여기서 직접 본다.
    bool has_error_diagnostic(const std::vector<gitman::diagnostic>& diagnostics) noexcept
    {
        for (const gitman::diagnostic& value : diagnostics)
            if (value.severity == gitman::diagnostic_severity::error)
                return true;
        return false;
    }

    gitman::repository_query_result query(gitman::testing::git_repository_fixture& fixture, const std::u8string_view path)
    {
        gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };
        return provider.query_local(make_project(path), {});
    }

    // 커밋 하나가 있는 저장소를 만든다.
    std::u8string make_committed_repository(gitman::testing::git_repository_fixture& fixture, const std::u8string_view name)
    {
        const std::u8string repository { fixture.make_repository(name) };
        fixture.write_file(repository, u8"a.txt", "one\n");
        fixture.git(repository, { u8"add", u8"-A" });
        fixture.git(repository, { u8"commit", u8"-m", u8"init" });
        return repository;
    }

    // 커밋 세 개를 담은 bare 원격을 만든다. 네트워크를 쓰지 않는 로컬 경로 원격이다.
    std::u8string make_published_remote(gitman::testing::git_repository_fixture& fixture)
    {
        const std::u8string remote { fixture.make_bare_repository(u8"remote") };
        const std::u8string source { make_committed_repository(fixture, u8"source") };
        fixture.git(source, { u8"remote", u8"add", u8"origin", remote });
        fixture.git(source, { u8"commit", u8"--allow-empty", u8"-m", u8"second" });
        fixture.git(source, { u8"commit", u8"--allow-empty", u8"-m", u8"third" });
        fixture.git(source, { u8"push", u8"-u", u8"origin", u8"main" });
        return remote;
    }

    std::u8string clone_of(gitman::testing::git_repository_fixture& fixture, const std::u8string_view remote, const std::u8string_view name)
    {
        const std::u8string workspace { fixture.make_directory(u8"clones") };
        fixture.git(workspace, { u8"clone", std::u8string { remote }, std::u8string { name } });
        return fixture.path_of(std::u8string { u8"clones\\" } + std::u8string { name });
    }

    // submodule까지 함께 받는 clone이다. Git은 기본적으로 submodule을 `file` 경로에서
    // 받아 오지 않으므로, production 명령이 원본을 새로 clone할 필요가 없도록 준비 단계에서
    // 미리 초기화해 둔다. 준비 명령만 그 전송을 허용한다.
    std::u8string clone_with_submodules_of(gitman::testing::git_repository_fixture& fixture, const std::u8string_view remote, const std::u8string_view name)
    {
        const std::u8string workspace { fixture.make_directory(u8"clones") };
        fixture.git(workspace, { u8"clone", u8"--recurse-submodules", std::u8string { remote }, std::u8string { name } });
        return fixture.path_of(std::u8string { u8"clones\\" } + std::u8string { name });
    }

    // 로컬 조회 뒤 원격 조회까지 수행한다. 실제 카드가 새로 고침에서 하는 순서다.
    gitman::repository_query_result refresh(gitman::testing::git_repository_fixture& fixture, const std::u8string_view path)
    {
        gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };
        const gitman::project_definition project { make_project(path) };
        const gitman::repository_query_result local { provider.query_local(project, {}) };
        return provider.query_remote(project, local.snapshot, {});
    }
} // namespace

#define REQUIRE_GIT_AVAILABLE(fixture)                                                                                                                                                                 \
    if ((fixture).available() == false)                                                                                                                                                                \
    SKIP("호스트에 사용할 수 있는 Git이 없어 통합 test를 건너뜁니다")

TEST_CASE("A real clean repository with an upstream is read", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { fixture.make_bare_repository(u8"remote") };
    const std::u8string repository { make_committed_repository(fixture, u8"clean") };
    fixture.git(repository, { u8"remote", u8"add", u8"origin", remote });
    fixture.git(repository, { u8"push", u8"-u", u8"origin", u8"main" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.kind == gitman::repository_kind::git);
    REQUIRE(result.snapshot.current_reference == u8"main");
    REQUIRE(result.snapshot.local_revision.size() == 40);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
    REQUIRE(result.snapshot.working_tree.is_safe_for_change());
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::local);
    REQUIRE(result.snapshot.comparison_target == u8"origin/main");
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::up_to_date);
    REQUIRE(result.diagnostics.empty());

    // 등록 경로가 곧 저장소 루트다. 구분자만 Git이 쓰는 `/`로 보고된다.
    REQUIRE(result.snapshot.repository_root.empty() == false);
}

TEST_CASE("A real repository ahead of its upstream is read without network access", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { fixture.make_bare_repository(u8"remote") };
    const std::u8string repository { make_committed_repository(fixture, u8"ahead") };
    fixture.git(repository, { u8"remote", u8"add", u8"origin", remote });
    fixture.git(repository, { u8"push", u8"-u", u8"origin", u8"main" });
    fixture.git(repository, { u8"commit", u8"--allow-empty", u8"-m", u8"local work" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::ahead);
    REQUIRE(result.snapshot.ahead_count == 1);
    REQUIRE(result.snapshot.behind_count == 0);
}

TEST_CASE("A real dirty repository reports its counts", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"dirty") };
    fixture.write_file(repository, u8"한글 파일.txt", "content\n");
    fixture.git(repository, { u8"add", u8"-A" });
    fixture.git(repository, { u8"commit", u8"-m", u8"second" });
    fixture.write_file(repository, u8"a.txt", "one\ntwo\n");
    fixture.write_file(repository, u8"새 파일 😀.txt", "new\n");
    fixture.git(repository, { u8"mv", u8"한글 파일.txt", u8"옮긴 이름.txt" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::modified);
    // 수정한 추적 파일과 이름을 바꾼 항목이다.
    REQUIRE(result.snapshot.working_tree.modified_count == 2);
    REQUIRE(result.snapshot.working_tree.untracked_count == 1);
    REQUIRE(result.snapshot.working_tree.conflicted_count == 0);
    REQUIRE_FALSE(result.snapshot.working_tree.is_safe_for_change());
}

TEST_CASE("A real conflicted repository reports the interrupted merge", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"conflicted") };
    fixture.git(repository, { u8"checkout", u8"-b", u8"other" });
    fixture.write_file(repository, u8"a.txt", "other\n");
    fixture.git(repository, { u8"commit", u8"-am", u8"other" });
    fixture.git(repository, { u8"checkout", u8"main" });
    fixture.write_file(repository, u8"a.txt", "main\n");
    fixture.git(repository, { u8"commit", u8"-am", u8"main" });
    REQUIRE(fixture.failures().empty());
    // 충돌하는 merge다. 실패가 정상이며 저장소에 `MERGE_HEAD`가 남는다.
    REQUIRE(fixture.git_allowing_failure(repository, { u8"merge", u8"other" }) != 0);

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::conflicted);
    REQUIRE(result.snapshot.working_tree.conflicted_count == 1);
    REQUIRE(result.snapshot.working_tree.operation_in_progress);
    REQUIRE_FALSE(result.snapshot.working_tree.is_safe_for_change());
}

TEST_CASE("A real interrupted rebase is detected from the git directory", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"rebase") };
    fixture.git(repository, { u8"checkout", u8"-b", u8"topic" });
    fixture.write_file(repository, u8"a.txt", "topic\n");
    fixture.git(repository, { u8"commit", u8"-am", u8"topic" });
    fixture.git(repository, { u8"checkout", u8"main" });
    fixture.write_file(repository, u8"a.txt", "main\n");
    fixture.git(repository, { u8"commit", u8"-am", u8"main" });
    fixture.git(repository, { u8"checkout", u8"topic" });
    REQUIRE(fixture.failures().empty());
    REQUIRE(fixture.git_allowing_failure(repository, { u8"rebase", u8"main" }) != 0);

    const gitman::repository_query_result result { query(fixture, repository) };

    // rebase는 표식이 디렉터리이며 porcelain 출력에는 나오지 않는다.
    REQUIRE(result.snapshot.working_tree.operation_in_progress);
    REQUIRE_FALSE(result.snapshot.working_tree.is_safe_for_change());
}

TEST_CASE("A real detached HEAD is reported with its short revision", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"detached") };
    fixture.git(repository, { u8"checkout", u8"--detach" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.working_tree.is_detached);
    REQUIRE(result.snapshot.current_reference.starts_with(u8"(detached) "));
    REQUIRE(result.snapshot.current_reference.size() == std::u8string_view { u8"(detached) " }.size() + 7);
    REQUIRE(result.snapshot.local_revision.starts_with(result.snapshot.current_reference.substr(std::u8string_view { u8"(detached) " }.size())));
}

TEST_CASE("A real repository without commits is still ready", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { fixture.make_repository(u8"unborn") };
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.current_reference == u8"main");
    REQUIRE(result.snapshot.local_revision.empty());
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
}

TEST_CASE("A real bare repository is an unsupported layout", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { fixture.make_bare_repository(u8"bare") };
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::unsupported_layout);
    REQUIRE_FALSE(result.has_errors());
}

TEST_CASE("A real linked worktree is read like any other work tree", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"main-tree") };
    const std::u8string worktree { fixture.path_of(u8"linked-tree") };
    fixture.git(repository, { u8"worktree", u8"add", u8"-b", u8"feature", worktree });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, worktree) };

    // git dir이 `<main>/.git/worktrees/<name>`이라 표식 확인도 그 디렉터리를 본다.
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.current_reference == u8"feature");
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
}

TEST_CASE("A real repository under a unicode path is read", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"한글 공간 😀/저장소") };
    fixture.write_file(repository, u8"a.txt", "changed\n");
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::modified);
    REQUIRE(result.snapshot.working_tree.modified_count == 1);
}

TEST_CASE("Real paths that are not repositories are separated from missing paths", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string plain { fixture.make_directory(u8"plain") };
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result not_a_repository { query(fixture, plain) };
    REQUIRE(not_a_repository.snapshot.availability == gitman::repository_availability::not_a_repository);

    const gitman::repository_query_result missing { query(fixture, fixture.path_of(u8"absent")) };
    REQUIRE(missing.snapshot.availability == gitman::repository_availability::path_unavailable);
}

TEST_CASE("A real remote comparison reports the synchronised state", "[integration][git][remote]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { make_published_remote(fixture) };
    const std::u8string clone { clone_of(fixture, remote, u8"synced") };
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { refresh(fixture, clone) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::up_to_date);
    // 이제는 cache된 ref가 아니라 실제로 확인한 원격이 근거다.
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::remote);
    REQUIRE(result.snapshot.comparison_target == u8"origin/main");
    REQUIRE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(result.snapshot.ahead_count == 0);
    REQUIRE(result.snapshot.behind_count == 0);
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("A real remote comparison separates ahead, behind and diverged", "[integration][git][remote]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { make_published_remote(fixture) };

    const std::u8string ahead { clone_of(fixture, remote, u8"ahead") };
    fixture.git(ahead, { u8"commit", u8"--allow-empty", u8"-m", u8"local work" });

    // 원격보다 한 커밋 뒤로 되돌려 놓는다. 원격을 건드리지 않고 behind를 만든다.
    const std::u8string behind { clone_of(fixture, remote, u8"behind") };
    fixture.git(behind, { u8"reset", u8"--hard", u8"HEAD~1" });

    const std::u8string diverged { clone_of(fixture, remote, u8"diverged") };
    fixture.git(diverged, { u8"reset", u8"--hard", u8"HEAD~1" });
    fixture.git(diverged, { u8"commit", u8"--allow-empty", u8"-m", u8"mine" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result ahead_result { refresh(fixture, ahead) };
    REQUIRE(ahead_result.snapshot.sync_state == gitman::remote_sync_state::ahead);
    REQUIRE(ahead_result.snapshot.ahead_count == 1);
    REQUIRE(ahead_result.snapshot.behind_count == 0);

    const gitman::repository_query_result behind_result { refresh(fixture, behind) };
    REQUIRE(behind_result.snapshot.sync_state == gitman::remote_sync_state::behind);
    REQUIRE(behind_result.snapshot.ahead_count == 0);
    REQUIRE(behind_result.snapshot.behind_count == 1);

    const gitman::repository_query_result diverged_result { refresh(fixture, diverged) };
    REQUIRE(diverged_result.snapshot.sync_state == gitman::remote_sync_state::diverged);
    REQUIRE(diverged_result.snapshot.ahead_count == 1);
    REQUIRE(diverged_result.snapshot.behind_count == 1);
}

TEST_CASE("A real repository without remotes is local only", "[integration][git][remote]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"lonely") };
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { refresh(fixture, repository) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::local_only);
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::none);
    REQUIRE_FALSE(result.snapshot.remote_checked_at.has_value());
}

TEST_CASE("A real branch missing on the remote is not compared locally", "[integration][git][remote]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { make_published_remote(fixture) };
    const std::u8string clone { clone_of(fixture, remote, u8"topic") };
    fixture.git(clone, { u8"checkout", u8"-b", u8"local-only-branch" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { refresh(fixture, clone) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::remote_target_missing);
    // fetch는 성공했으므로 원격 확인 시각은 남는다.
    REQUIRE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::none);
}

TEST_CASE("A real unreachable remote is reported as offline", "[integration][git][remote]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { make_published_remote(fixture) };
    const std::u8string clone { clone_of(fixture, remote, u8"unreachable") };
    // 닫힌 포트라 DNS 없이 즉시 연결에 실패한다. 실패 문장은 libcurl이 만든다.
    fixture.git(clone, { u8"remote", u8"set-url", u8"origin", u8"http://127.0.0.1:1/absent.git" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { refresh(fixture, clone) };

    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::offline);
    REQUIRE(result.has_errors());
    // 실패해도 직전에 알던 로컬 비교와 작업 트리 상태는 남는다.
    REQUIRE(result.snapshot.comparison == gitman::comparison_source::local);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
    REQUIRE_FALSE(result.snapshot.remote_checked_at.has_value());
}

TEST_CASE("A real update fast forwards a repository that is behind", "[integration][git][update]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { make_published_remote(fixture) };
    const std::u8string clone { clone_of(fixture, remote, u8"behind") };
    fixture.git(clone, { u8"reset", u8"--hard", u8"HEAD~1" });
    REQUIRE(fixture.failures().empty());

    gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };
    const gitman::repository_change_result result { provider.update(make_project(clone), {}, {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    REQUIRE(result.blocked_by == gitman::update_block_reason::none);
    // 사후 재조회가 갱신된 상태를 보고한다.
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::up_to_date);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
}

TEST_CASE("A real update refuses a merge when fast forward is impossible", "[integration][git][update]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { make_published_remote(fixture) };
    const std::u8string clone { clone_of(fixture, remote, u8"rewritten") };
    // 원격 이력을 다시 써서 clone의 커밋이 더 이상 조상이 아니게 만든다. 로컬이 들고
    // 있는 tracking ref는 아직 옛 값이라 사전 검사는 통과한다.
    const std::u8string source { fixture.path_of(u8"source") };
    fixture.git(source, { u8"reset", u8"--hard", u8"HEAD~1" });
    fixture.git(source, { u8"commit", u8"--allow-empty", u8"-m", u8"rewritten" });
    fixture.git(source, { u8"push", u8"--force", u8"origin", u8"main" });
    REQUIRE(fixture.failures().empty());

    gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };
    const gitman::repository_change_result result { provider.update(make_project(clone), {}, {}) };

    // 실행은 했지만 merge를 만들지 않고 실패한다. 이것이 `--ff-only`의 목적이다.
    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(has_error_diagnostic(result.diagnostics));
    // 실패해도 작업 트리는 그대로다.
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
}

TEST_CASE("A real update is blocked before touching an unsafe repository", "[integration][git][update]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { make_published_remote(fixture) };
    gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };

    const std::u8string dirty { clone_of(fixture, remote, u8"dirty") };
    fixture.write_file(dirty, u8"a.txt", "changed\n");

    const std::u8string diverged { clone_of(fixture, remote, u8"diverged") };
    fixture.git(diverged, { u8"reset", u8"--hard", u8"HEAD~1" });
    fixture.git(diverged, { u8"commit", u8"--allow-empty", u8"-m", u8"mine" });

    const std::u8string detached { clone_of(fixture, remote, u8"detached") };
    fixture.git(detached, { u8"checkout", u8"--detach" });

    const std::u8string lonely { make_committed_repository(fixture, u8"lonely") };
    REQUIRE(fixture.failures().empty());

    const gitman::repository_change_result dirty_result { provider.update(make_project(dirty), {}, {}) };
    REQUIRE_FALSE(dirty_result.executed);
    REQUIRE(dirty_result.blocked_by == gitman::update_block_reason::working_tree_dirty);

    const gitman::repository_change_result diverged_result { provider.update(make_project(diverged), {}, {}) };
    REQUIRE_FALSE(diverged_result.executed);
    REQUIRE(diverged_result.blocked_by == gitman::update_block_reason::diverged);

    const gitman::repository_change_result detached_result { provider.update(make_project(detached), {}, {}) };
    REQUIRE_FALSE(detached_result.executed);
    REQUIRE(detached_result.blocked_by == gitman::update_block_reason::detached_head);

    const gitman::repository_change_result lonely_result { provider.update(make_project(lonely), {}, {}) };
    REQUIRE_FALSE(lonely_result.executed);
    REQUIRE(lonely_result.blocked_by == gitman::update_block_reason::no_remote_target);
}

TEST_CASE("A real update leaves submodules alone unless asked", "[integration][git][update]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    // submodule 원본과 그것을 담은 상위 저장소를 만든다. 모두 로컬 경로다.
    const std::u8string module_remote { fixture.make_bare_repository(u8"module-remote") };
    const std::u8string module_source { make_committed_repository(fixture, u8"module-source") };
    fixture.git(module_source, { u8"remote", u8"add", u8"origin", module_remote });
    fixture.git(module_source, { u8"push", u8"-u", u8"origin", u8"main" });

    const std::u8string parent_remote { fixture.make_bare_repository(u8"parent-remote") };
    const std::u8string parent_source { make_committed_repository(fixture, u8"parent-source") };
    fixture.git(parent_source, { u8"submodule", u8"add", module_remote, u8"vendor/모듈" });
    fixture.git(parent_source, { u8"commit", u8"-m", u8"add submodule" });
    fixture.git(parent_source, { u8"remote", u8"add", u8"origin", parent_remote });
    fixture.git(parent_source, { u8"push", u8"-u", u8"origin", u8"main" });
    REQUIRE(fixture.failures().empty());

    const std::u8string clone { clone_with_submodules_of(fixture, parent_remote, u8"parent") };
    fixture.git(parent_source, { u8"commit", u8"--allow-empty", u8"-m", u8"more" });
    fixture.git(parent_source, { u8"push", u8"origin", u8"main" });
    REQUIRE(fixture.failures().empty());

    gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };

    // 기본값은 off다. submodule은 초기화되지 않은 채로 남는다.
    const gitman::repository_change_result without { provider.update(make_project(clone), {}, {}) };
    REQUIRE(without.executed);
    REQUIRE(without.succeeded);
    REQUIRE(without.snapshot.submodules.empty());

    gitman::update_options options {};
    options.update_submodules = true;
    const gitman::repository_change_result with { provider.update(make_project(clone), options, {}) };

    REQUIRE(with.executed);
    REQUIRE(with.succeeded);
    // 조사한 submodule이 결과에 담긴다.
    REQUIRE(with.snapshot.submodules.size() == 1);
    REQUIRE(with.snapshot.submodules.front().relative_path == u8"vendor/모듈");
    // 갱신 뒤에도 작업 트리는 깨끗해야 한다.
    REQUIRE(with.snapshot.working_tree.state == gitman::working_tree_state::clean);
}

TEST_CASE("Non ASCII text in real Git output reaches the caller unchanged", "[integration][git][encoding]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"encoding") };
    REQUIRE(fixture.failures().empty());

    // 없는 branch 이름을 그대로 되돌려 주는 명령이다. 로캘을 강제하지 않으므로 문장은
    // 호스트 설정을 따르지만, 되돌려 주는 이름은 우리가 넘긴 값이라 인코딩 왕복을
    // 그대로 확인할 수 있다.
    std::vector<std::u8string> arguments { u8"checkout", u8"없는 브랜치 😀" };
    const gitman::process_request request {
        gitman::make_vcs_process_request(gitman::repository_kind::git, fixture.tool().executable, repository, std::move(arguments), gitman::vcs_command_class::local_query),
    };
    const gitman::vcs_command_result result { gitman::run_vcs_command(fixture.runner(), request, {}) };

    REQUIRE_FALSE(result.succeeded());
    const std::u8string text { result.standard_error_text() };
    // `active_code_page_fallback`은 유효한 UTF-8 레코드를 건드리지 않는다.
    REQUIRE(text.find(u8"없는 브랜치 😀") != std::u8string::npos);
}

TEST_CASE("Local queries of a real repository never touch the network", "[integration][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { make_committed_repository(fixture, u8"offline") };
    // 도달할 수 없는 remote를 등록한다. 로컬 조회가 원격에 접근하면 여기서 지연되거나
    // 실패한다.
    fixture.git(repository, { u8"remote", u8"add", u8"origin", u8"https://gitman.invalid/repo.git" });
    REQUIRE(fixture.failures().empty());

    const gitman::repository_query_result result { query(fixture, repository) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
    // upstream이 없으므로 로컬 조회는 원격 상태를 단정하지 않는다.
    REQUIRE(result.snapshot.sync_state == gitman::remote_sync_state::unknown);
    REQUIRE(result.diagnostics.empty());
}
