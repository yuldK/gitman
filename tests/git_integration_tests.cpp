#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "helpers/git_repository_fixture.h"
#include "infrastructure/git_repository_provider.h"

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
