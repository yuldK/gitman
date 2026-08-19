#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "helpers/git_repository_fixture.h"
#include "infrastructure/git_repository_provider.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <string>
#include <string_view>

namespace {
    // 실제 GitHub에 접속하는 실측 test다 (stage-8-plan 5.6). 네트워크와 외부 서비스
    // 상태에 의존하므로 기본으로는 skip하고, 환경 변수를 켠 실측 실행에서만 돈다.
    [[nodiscard]] bool network_tests_enabled()
    {
        wchar_t value[8] {};
        const DWORD length { GetEnvironmentVariableW(L"GITMAN_NETWORK_TESTS", value, 8) };
        return length > 0 && std::wstring_view { value, length } == L"1";
    }

    gitman::project_definition make_project(const std::u8string_view path)
    {
        gitman::project_definition project {};
        project.id.value = u8"network";
        project.path.original = path;
        project.path.normalized = path;
        project.display_name = u8"network";
        return project;
    }
} // namespace

#define REQUIRE_NETWORK_FIXTURE(fixture)                                                                                                                                                               \
    if (network_tests_enabled() == false)                                                                                                                                                              \
        SKIP("GITMAN_NETWORK_TESTS=1이 아니어서 실제 네트워크 실측을 건너뜁니다");                                                                                                                     \
    if ((fixture).available() == false)                                                                                                                                                                \
    SKIP("호스트에 사용할 수 있는 Git이 없어 실측을 건너뜁니다")

TEST_CASE("A public GitHub clone refreshes, updates and lists switch candidates", "[network][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_NETWORK_FIXTURE(fixture);

    // 작고 오래 유지된 공개 저장소다 (기본 branch master, 원격 branch 3개).
    const std::u8string workspace { fixture.make_directory(u8"clones") };
    fixture.git(workspace, { u8"clone", u8"https://github.com/octocat/Hello-World.git", u8"hello" });
    REQUIRE(fixture.failures().empty());
    const std::u8string repository { fixture.path_of(u8"clones\\hello") };
    const gitman::project_definition project { make_project(repository) };
    gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };

    // remote-first refresh: 갓 clone한 저장소는 up_to_date다 (REQ-002).
    const gitman::repository_query_result local { provider.query_local(project, {}) };
    REQUIRE(local.snapshot.availability == gitman::repository_availability::ready);
    const gitman::repository_query_result remote { provider.query_remote(project, local.snapshot, {}) };
    REQUIRE(remote.snapshot.sync_state == gitman::remote_sync_state::up_to_date);
    REQUIRE(remote.snapshot.comparison == gitman::comparison_source::remote);
    REQUIRE(remote.snapshot.comparison_target == u8"origin/master");
    REQUIRE(remote.snapshot.remote_checked_at.has_value());

    // update --ff-only: 최신 상태에서도 비대화형으로 실행되고 성공한다 (REQ-006).
    const gitman::repository_change_result updated { provider.update(project, {}, {}) };
    REQUIRE(updated.executed);
    REQUIRE(updated.succeeded);

    // switch 후보: 원격 branch group이 실제 원격에서 조회된다 (REQ-007).
    const gitman::switch_candidate_result candidates { provider.query_switch_candidates(project, {}) };
    REQUIRE(candidates.stale == false);
    bool found_remote_branch { false };
    for (const gitman::switch_candidate& candidate : candidates.candidates)
        if (candidate.kind == gitman::switch_candidate_kind::git_remote_branch)
            found_remote_branch = true;
    REQUIRE(found_remote_branch);
}

TEST_CASE("A remote that requires credentials fails immediately without prompting", "[network][git]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_NETWORK_FIXTURE(fixture);

    // 존재하지 않는(또는 비공개) 저장소 URL은 GitHub가 자격 증명을 요구한다.
    // 비대화형 계약이 프롬프트 없이 즉시 실패해야 한다 (plan 3.4, ADR-003).
    //
    // production은 사용자의 credential helper를 그대로 쓴다 (ADR-003). 호스트에
    // GitHub 자격 증명이 저장되어 있으면 helper가 응답해 404(저장소 없음)로
    // 끝나므로, URL에 다른 username을 박아 helper 조회를 빗나가게 해 "자격 증명
    // 없음" 경로를 강제한다 (2026-08-19 이 호스트 실측: helper 미스 시 1.2초 만에
    // could not read Password로 실패).
    const std::u8string repository { fixture.make_repository(u8"private-probe") };
    fixture.write_file(repository, u8"a.txt", "one\n");
    fixture.git(repository, { u8"add", u8"-A" });
    fixture.git(repository, { u8"commit", u8"-m", u8"init" });
    fixture.git(repository, { u8"remote", u8"add", u8"origin", u8"https://gitman-credential-probe@github.com/gitman-nonexistent-check/private-probe.git" });
    REQUIRE(fixture.failures().empty());

    const gitman::project_definition project { make_project(repository) };
    gitman::git_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };
    const gitman::repository_query_result local { provider.query_local(project, {}) };
    const gitman::repository_query_result remote { provider.query_remote(project, local.snapshot, {}) };

    // 자격 증명 요구는 로캘 독립 신호로 분류된다 (단계 4). 무한 대기 없이 test
    // timeout 안에 끝나는 것 자체가 비대화형 보장의 실측이다.
    std::u8string diagnostics_text {};
    for (const gitman::diagnostic& value : remote.diagnostics)
    {
        diagnostics_text += value.message;
        diagnostics_text += u8" | ";
    }
    CAPTURE(std::string { diagnostics_text.begin(), diagnostics_text.end() });
    REQUIRE(remote.snapshot.sync_state == gitman::remote_sync_state::authentication_required);
}
