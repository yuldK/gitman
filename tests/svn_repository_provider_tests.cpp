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

    // `svn info --xml` 성공 출력이다. 공식 스키마의 요소 이름을 근거로 작성했다.
    constexpr std::u8string_view info_xml_output {
        u8"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        u8"<info><entry kind=\"dir\" path=\".\" revision=\"4168\">\n"
        u8"<url>https://svn.example.com/repo/trunk</url>\n"
        u8"<relative-url>^/trunk</relative-url>\n"
        u8"<repository><root>https://svn.example.com/repo</root>\n"
        u8"<uuid>8f3a1c2e-0000-0000-0000-000000000000</uuid></repository>\n"
        u8"<wc-info><wcroot-abspath>C:\\작업 공간\\작업 복사본</wcroot-abspath></wc-info>\n"
        u8"<commit revision=\"4168\"></commit>\n"
        u8"</entry></info>\n",
    };

    // 로컬 조회 성공 경로의 응답이다. `info --xml` 하나, `status`, 선택적 `svnversion`
    // 순이다. `svnversion`은 update·switch 직전 검증에서만 실행된다.
    void push_local_responses(gitman::testing::fake_process_runner& runner, const std::u8string_view status, const std::u8string_view version)
    {
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
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

    bool has_diagnostic(const std::vector<gitman::diagnostic>& diagnostics, const gitman::diagnostic_code code) noexcept
    {
        return std::ranges::any_of(diagnostics, [code](const gitman::diagnostic& value) { return value.code == code; });
    }

    bool has_diagnostic(const gitman::repository_query_result& result, const gitman::diagnostic_code code) noexcept
    {
        return has_diagnostic(result.diagnostics, code);
    }

    // `repository_change_result`에는 `has_errors()`가 없다. 조회 결과와 달리 실행 결과는
    // `executed`와 `succeeded`로 성패를 나타내므로 여기서는 진단을 직접 본다.
    bool has_diagnostic(const gitman::repository_change_result& result, const gitman::diagnostic_code code) noexcept
    {
        return has_diagnostic(result.diagnostics, code);
    }

    bool has_error_diagnostic(const gitman::repository_change_result& result) noexcept
    {
        return std::ranges::any_of(result.diagnostics, [](const gitman::diagnostic& value) { return value.severity == gitman::diagnostic_severity::error; });
    }

    constexpr std::u8string_view switch_target_url { u8"https://svn.example.com/repo/branches/x" };

    gitman::project_definition switch_project()
    {
        gitman::project_definition project { make_project() };
        // F6부터 이 필드는 문서 호환을 위해 보존만 하고 후보·검증에는 쓰지 않는다.
        project.svn_switch_targets = { u8"읽되 무시할 값" };
        return project;
    }

    gitman::switch_candidate url_target(const std::u8string_view url = switch_target_url)
    {
        gitman::switch_candidate candidate {};
        candidate.kind = gitman::switch_candidate_kind::subversion_url;
        candidate.display_name = url;
        candidate.target = url;
        return candidate;
    }

    // 현재 URL 조회의 응답이다. 검증은 이 값으로 `already_on_target`을 판정한다.
    void push_current_url(gitman::testing::fake_process_runner& runner, const std::u8string_view url = repository_url)
    {
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { url } + u8"\n", {} });
    }

    // 대상 URL의 저장소 root와 UUID다. 기본값은 현재 작업 복사본과 같은 저장소다.
    void push_identity(
        gitman::testing::fake_process_runner& runner, const std::u8string_view root = u8"https://svn.example.com/repo", const std::u8string_view uuid = u8"8f3a1c2e-0000-0000-0000-000000000000")
    {
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { root } + u8"\n", {} });
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { uuid } + u8"\n", {} });
    }

    std::size_t count_svn_commands(const gitman::testing::fake_process_runner& runner, const std::u8string_view command)
    {
        std::size_t count { 0 };
        for (const gitman::process_request& request : runner.requests())
            if (request.arguments.size() > 1 && request.arguments[1] == command)
                ++count;
        return count;
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
    push_local_responses(runner, u8"M       trunk/a.txt\n?       trunk/새 파일.txt\n", u8"");
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
    // `svnversion` 검사는 update·switch 직전 검증에서만 실행되므로 일반 조회에서는
    // 미상으로 남는다. 거짓과 미상을 구분한다.
    REQUIRE_FALSE(result.snapshot.has_mixed_revision.has_value());
    REQUIRE_FALSE(result.snapshot.has_switched_subtree.has_value());
    REQUIRE(result.snapshot.local_checked_at.has_value());
    REQUIRE_FALSE(result.snapshot.remote_checked_at.has_value());
    REQUIRE(result.diagnostics.empty());

    // `info --xml` 하나와 `status`뿐이다. 항목별 `--show-item` 프로세스는 없다.
    REQUIRE(runner.request_count() == 2);
    REQUIRE(runner.request(0).arguments == std::vector<std::u8string> { u8"--non-interactive", u8"info", u8"--xml" });
    REQUIRE(runner.request(1).arguments == std::vector<std::u8string> { u8"--non-interactive", u8"status", u8"--ignore-externals" });
    // 로컬 조회는 네트워크를 쓰지 않는다.
    REQUIRE(runner.requests_for_executable_suffix(u8"svn.exe") == 2);
    for (const gitman::process_request& request : runner.requests())
        for (const std::u8string& argument : request.arguments)
            REQUIRE(argument.starts_with(u8"https://") == false);
}

TEST_CASE("Mixed revisions and switched subtrees are reported before an update", "[infrastructure][svn][provider]")
{
    // `svnversion` 검사는 판정이 실제로 필요한 update·switch 직전 검증에서만 실행된다.
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, u8"    S   trunk/전환된 폴더\n", u8"4123:4168MS\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.blocked_by != gitman::update_block_reason::none);
    REQUIRE(result.snapshot.has_mixed_revision.has_value());
    REQUIRE(*result.snapshot.has_mixed_revision);
    REQUIRE(result.snapshot.has_switched_subtree.has_value());
    REQUIRE(*result.snapshot.has_switched_subtree);
    // 조회 3(info·status·svnversion)에서 preflight가 막아 update 명령이 없다.
    REQUIRE(runner.request_count() == 3);
}

TEST_CASE("Missing svnversion gives up only the two judgements", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, u8"    S   trunk/전환된 폴더\n", u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(false), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    // 거짓과 미상을 구분한다. `svnversion` 없이도 조회는 계속된다.
    REQUIRE_FALSE(result.snapshot.has_mixed_revision.has_value());
    // switched는 `status`의 5번 칸으로 보조 판정하며 update를 막는다.
    REQUIRE(result.snapshot.has_switched_subtree.has_value());
    REQUIRE(*result.snapshot.has_switched_subtree);
    REQUIRE_FALSE(result.executed);
    REQUIRE(result.blocked_by == gitman::update_block_reason::switched_subtree);
    REQUIRE(runner.request_count() == 2);
}

TEST_CASE("Unreadable svnversion output is a warning, not a failure", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"Unversioned directory\n");
    runner.push_response({ gitman::process_completion::exited, 0, u8"Updating '.':\nAt revision 4180.\n", {} });
    push_local_responses(runner, {}, u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    // 판정 실패는 경고로 남고 update 진행을 막지 않는다.
    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
    REQUIRE_FALSE(result.snapshot.has_mixed_revision.has_value());
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::vcs_output_unparsable));
    REQUIRE_FALSE(has_error_diagnostic(result));
}

TEST_CASE("SVN status failures do not become a working tree state", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"svn: E155004: 작업 복사본이 잠겨 있습니다\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::unknown);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::unknown);
    REQUIRE(result.has_errors());
    REQUIRE(runner.request_count() == 2);
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

TEST_CASE("SVN update preflight refuses unsafe working copies", "[infrastructure][svn][provider]")
{
    gitman::repository_snapshot clean { ready_local() };
    REQUIRE(gitman::evaluate_svn_update_preflight(clean) == gitman::update_block_reason::none);

    gitman::repository_snapshot unavailable { clean };
    unavailable.availability = gitman::repository_availability::not_a_repository;
    REQUIRE(gitman::evaluate_svn_update_preflight(unavailable) == gitman::update_block_reason::repository_unavailable);

    gitman::repository_snapshot conflicted { clean };
    conflicted.working_tree.state = gitman::working_tree_state::conflicted;
    REQUIRE(gitman::evaluate_svn_update_preflight(conflicted) == gitman::update_block_reason::working_tree_conflicted);

    gitman::repository_snapshot dirty { clean };
    dirty.working_tree.state = gitman::working_tree_state::modified;
    dirty.working_tree.modified_count = 1;
    REQUIRE(gitman::evaluate_svn_update_preflight(dirty) == gitman::update_block_reason::working_tree_dirty);

    // 미추적(미버전) 파일만 있는 작업 복사본은 막지 않는다 (field-feedback-design
    // 2.2). `svn update`는 미버전 파일을 건드리지 않는다.
    gitman::repository_snapshot untracked_only { clean };
    untracked_only.working_tree.state = gitman::working_tree_state::modified;
    untracked_only.working_tree.untracked_count = 2;
    REQUIRE(gitman::evaluate_svn_update_preflight(untracked_only) == gitman::update_block_reason::none);

    gitman::repository_snapshot unknown { clean };
    unknown.working_tree.state = gitman::working_tree_state::unknown;
    REQUIRE(gitman::evaluate_svn_update_preflight(unknown) == gitman::update_block_reason::working_tree_dirty);

    gitman::repository_snapshot switched { clean };
    switched.has_switched_subtree = true;
    REQUIRE(gitman::evaluate_svn_update_preflight(switched) == gitman::update_block_reason::switched_subtree);

    gitman::repository_snapshot mixed { clean };
    mixed.has_mixed_revision = true;
    REQUIRE(gitman::evaluate_svn_update_preflight(mixed) == gitman::update_block_reason::mixed_revision);
}

TEST_CASE("SVN judgements that could not be made do not block updates", "[infrastructure][svn][provider]")
{
    gitman::repository_snapshot snapshot { ready_local() };
    REQUIRE_FALSE(snapshot.has_switched_subtree.has_value());
    REQUIRE_FALSE(snapshot.has_mixed_revision.has_value());

    // `svnversion`이 없어 판정할 수 없다는 이유로 update를 영영 막지 않는다. 조회가 이미
    // 그 사실을 warning으로 남긴다.
    REQUIRE(gitman::evaluate_svn_update_preflight(snapshot) == gitman::update_block_reason::none);

    // 거짓으로 판정된 경우도 통과다.
    snapshot.has_switched_subtree = false;
    snapshot.has_mixed_revision = false;
    REQUIRE(gitman::evaluate_svn_update_preflight(snapshot) == gitman::update_block_reason::none);
}

TEST_CASE("A blocked SVN update builds no change command", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, u8"M       trunk/a.txt\n", u8"4168\n");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.blocked_by == gitman::update_block_reason::working_tree_dirty);
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::update_blocked));
    // 조회 3개뿐이며 update 명령이 없다. `svnversion` 요청에는 인자가 아예 없으므로
    // 마지막 인자만 보지 않고 목록 전체를 확인한다.
    REQUIRE(runner.request_count() == 3);
    for (const gitman::process_request& request : runner.requests())
        REQUIRE(std::ranges::find(request.arguments, std::u8string { u8"update" }) == request.arguments.end());
}

TEST_CASE("A successful SVN update re-reads the working copy", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"4168\n");
    runner.push_response({ gitman::process_completion::exited, 0, u8"Updating '.':\nAt revision 4180.\n", {} });
    push_local_responses(runner, {}, u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    REQUIRE(result.blocked_by == gitman::update_block_reason::none);
    // 사전 조회 3(info·status·svnversion) + update 1 + 사후 조회 2(info·status)다.
    REQUIRE(runner.request_count() == 6);
    REQUIRE(runner.request(3).arguments == std::vector<std::u8string> { u8"--non-interactive", u8"update" });
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
}

TEST_CASE("A failed SVN update is reported with the state after it", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"4168\n");
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"svn: E155004: 작업 복사본이 잠겨 있습니다\n" });
    push_local_responses(runner, u8"C       trunk/충돌.txt\n", u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    // 실행의 성공 여부와 조회 결과는 분리해 보고한다.
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::conflicted);
    REQUIRE(has_error_diagnostic(result));
}

TEST_CASE("SVN updates do nothing when the tool is missing", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { missing_tool(), runner, probe };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.blocked_by == gitman::update_block_reason::tool_unavailable);
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("SVN switch browser initialization reads the root and current URL", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };
    const gitman::project_definition project { make_project() };

    const gitman::switch_candidate_result candidates { provider.query_switch_candidates(project, {}) };
    REQUIRE(candidates.candidates.empty());
    REQUIRE(candidates.svn_browser.has_value());
    REQUIRE(candidates.svn_browser->repository_root_url == u8"https://svn.example.com/repo");
    REQUIRE(candidates.svn_browser->current_url == u8"https://svn.example.com/repo/trunk");
    // 루트와 현재 URL은 `info --xml` 하나로 받는다.
    REQUIRE(runner.request_count() == 1u);
    REQUIRE(runner.request(0).arguments == std::vector<std::u8string> { u8"--non-interactive", u8"info", u8"--xml" });

    const gitman::repository_change_result switched { provider.switch_to(project, {}, {}) };
    REQUIRE_FALSE(switched.executed);
    REQUIRE(switched.rejected_by == gitman::switch_rejection::target_not_found);
    REQUIRE(runner.request_count() == 1u);
}

TEST_CASE("SVN switch browser ignores the preserved document target list", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    gitman::project_definition project { make_project() };
    project.svn_switch_targets = { u8"https://svn.example.com/repo/trunk", u8"https://svn.example.com/repo/branches/x", u8"https://svn.example.com/repo/trunk", u8"잘못된 값" };

    const gitman::switch_candidate_result result { provider.query_switch_candidates(project, {}) };

    REQUIRE(runner.request_count() == 1u);
    REQUIRE(result.candidates.empty());
    REQUIRE(result.svn_browser.has_value());
    REQUIRE_FALSE(result.stale);
    REQUIRE_FALSE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::invalid_project_field));
}

TEST_CASE("SVN switch candidates need the tool as well", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { missing_tool(), runner, probe };

    gitman::project_definition project { make_project() };
    project.svn_switch_targets = { u8"https://svn.example.com/repo/trunk" };

    const gitman::switch_candidate_result result { provider.query_switch_candidates(project, {}) };
    REQUIRE(result.candidates.empty());
    REQUIRE_FALSE(result.svn_browser.has_value());
    REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::vcs_tool_not_found));
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("Rejected SVN switches never reach the network", "[infrastructure][svn][provider]")
{
    struct expectation
    {
        std::u8string_view name {};
        std::u8string status {};
        gitman::switch_candidate target {};
        gitman::switch_rejection rejection {};
    };

    const expectation expectations[] {
        { u8"지원하지 않는 URL", {}, url_target(u8"잘못된 값"), gitman::switch_rejection::target_not_allowed },
        { u8"이미 대상", {}, url_target(repository_url), gitman::switch_rejection::already_on_target },
        { u8"dirty 작업 복사본", u8"M       trunk/a.txt\n", url_target(), gitman::switch_rejection::working_tree_unsafe },
    };

    for (const expectation& value : expectations)
    {
        INFO(reinterpret_cast<const char*>(value.name.data()));
        gitman::testing::fake_process_runner runner {};
        push_local_responses(runner, value.status, u8"4168\n");
        push_current_url(runner);
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::svn_repository_provider provider { available_tool(), runner, probe };

        const gitman::repository_change_result result { provider.switch_to(switch_project(), value.target, {}) };

        REQUIRE_FALSE(result.executed);
        REQUIRE(result.rejected_by == value.rejection);
        // 조회 3 + 현재 URL 1에서 끝난다. 대상 URL을 확인하는 원격 조회가 없다.
        REQUIRE(runner.request_count() == 4);
        REQUIRE(count_svn_commands(runner, u8"switch") == 0);
    }
}

TEST_CASE("SVN directory queries keep directories and use the requested URL", "[infrastructure][svn][provider][browser]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, u8"branches/\nREADME.txt\ntrunk/\n", {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::svn_directory_query_result result {
        provider.query_directory(make_project(), u8"https://svn.example.com/repo", u8"https://svn.example.com/repo", {}),
    };
    REQUIRE(result.succeeded());
    REQUIRE(result.directories == std::vector<std::u8string> { u8"branches", u8"trunk" });
    REQUIRE(runner.request_count() == 1u);
    REQUIRE(runner.request(0).arguments == std::vector<std::u8string> { u8"--non-interactive", u8"ls", u8"https://svn.example.com/repo" });
}

TEST_CASE("SVN directory authentication failure is explicit and noninteractive", "[infrastructure][svn][provider][browser]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"svn: E170001: 인증이 필요합니다\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::svn_directory_query_result result {
        provider.query_directory(make_project(), u8"https://svn.example.com/repo", u8"https://svn.example.com/repo/private", {}),
    };
    REQUIRE_FALSE(result.succeeded());
    REQUIRE(result.error == gitman::svn_browser_query_error::authentication_required);
    REQUIRE(runner.request_count() == 1u);
    REQUIRE(runner.request(0).arguments.front() == u8"--non-interactive");
    for (const std::u8string& argument : runner.request(0).arguments)
    {
        REQUIRE(argument != u8"--username");
        REQUIRE(argument != u8"--password");
    }
}

TEST_CASE("SVN directory queries reject URLs outside the browser root", "[infrastructure][svn][provider][browser]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::svn_directory_query_result result {
        provider.query_directory(make_project(), u8"https://svn.example.com/repo", u8"https://svn.example.com/other", {}),
    };
    REQUIRE_FALSE(result.succeeded());
    REQUIRE(runner.request_count() == 0u);
}

TEST_CASE("An SVN switch refuses a different repository", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"4168\n");
    push_current_url(runner);
    push_identity(runner, u8"https://svn.example.com/other");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(switch_project(), url_target(), {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.rejected_by == gitman::switch_rejection::repository_mismatch);
    REQUIRE(runner.request_count() == 6);
    REQUIRE(count_svn_commands(runner, u8"switch") == 0);
    REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::switch_target_rejected));
}

TEST_CASE("An SVN switch stops when the target cannot be reached", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"4168\n");
    push_current_url(runner);
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"svn: E170013: URL의 저장소에 연결할 수 없습니다\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(switch_project(), url_target(), {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.rejected_by == gitman::switch_rejection::target_unreachable);
    REQUIRE(runner.request_count() == 5);
    REQUIRE(count_svn_commands(runner, u8"switch") == 0);
}

TEST_CASE("A successful SVN switch re-reads the working copy", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"4168\n");
    push_current_url(runner);
    push_identity(runner);
    runner.push_response({ gitman::process_completion::exited, 0, u8"Updating '.':\nAt revision 4180.\n", {} });
    push_local_responses(runner, {}, u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(switch_project(), url_target(), {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    REQUIRE(result.rejected_by == gitman::switch_rejection::none);
    // 사전 조회 3 + 현재 URL 1 + 대상 확인 2 + switch 1 + 사후 조회 2다.
    REQUIRE(runner.request_count() == 9);
    REQUIRE(runner.request(6).arguments == std::vector<std::u8string> { u8"--non-interactive", u8"switch", std::u8string { switch_target_url } });
    REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
}

TEST_CASE("A failed SVN switch is reported with the state after it", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"4168\n");
    push_current_url(runner);
    push_identity(runner);
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"svn: E155004: 작업 복사본이 잠겨 있습니다\n" });
    push_local_responses(runner, u8"C       trunk/충돌.txt\n", u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(switch_project(), url_target(), {}) };

    // 실행 자체는 했으므로 거부가 아니다. 성공 여부와 조회 결과는 분리해 보고한다.
    REQUIRE(result.executed);
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(result.rejected_by == gitman::switch_rejection::none);
    REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::conflicted);
    REQUIRE(has_error_diagnostic(result));
}

TEST_CASE("SVN switches do nothing when the tool is missing", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { missing_tool(), runner, probe };

    const gitman::repository_change_result result { provider.switch_to(switch_project(), url_target(), {}) };

    REQUIRE_FALSE(result.executed);
    REQUIRE(result.rejected_by == gitman::switch_rejection::tool_unavailable);
    REQUIRE(runner.request_count() == 0);
}

TEST_CASE("Refreshed SVN tool information is picked up", "[infrastructure][svn][provider]")
{
    gitman::testing::fake_process_runner runner {};
    push_local_responses(runner, {}, u8"");
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { missing_tool(), runner, probe };

    REQUIRE_FALSE(provider.available());
    provider.set_tool(available_tool());

    REQUIRE(provider.available());
    REQUIRE(provider.tool().version == gitman::vcs_tool_version { 1, 14, 5 });
    REQUIRE(provider.query_local(make_project(), {}).snapshot.availability == gitman::repository_availability::ready);
}

TEST_CASE("Ignoring local changes skips the status walk but keeps the revision scan", "[infrastructure][svn][provider]")
{
    SECTION("조회는 status 없이 작업 트리를 깨끗하다고 믿는다")
    {
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::svn_repository_provider provider { available_tool(), runner, probe, nullptr, {}, true };

        const gitman::repository_query_result result { provider.query_local(make_project(), {}) };

        REQUIRE(result.snapshot.availability == gitman::repository_availability::ready);
        REQUIRE(result.snapshot.working_tree.state == gitman::working_tree_state::clean);
        // `info --xml` 하나뿐이다. status 순회가 없다.
        REQUIRE(runner.request_count() == 1);
    }

    SECTION("update 직전 검증의 svnversion 검사는 그대로 수행한다")
    {
        // mixed revision·switched는 로컬 변경이 아니라 작업 복사본 구조라서 이 설정의
        // 대상이 아니다. F3의 전환·업데이트 차단 정책이 유지된다.
        gitman::testing::fake_process_runner runner {};
        runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
        runner.push_response({ gitman::process_completion::exited, 0, u8"4123:4168M\n", {} });
        gitman::testing::fake_vcs_file_probe probe {};
        register_working_directory(probe);
        gitman::svn_repository_provider provider { available_tool(), runner, probe, nullptr, {}, true };

        const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

        REQUIRE_FALSE(result.executed);
        REQUIRE(result.blocked_by == gitman::update_block_reason::mixed_revision);
        // `info --xml`과 `svnversion`뿐이다. status 순회가 없다.
        REQUIRE(runner.request_count() == 2);
    }
}

TEST_CASE("Ignore mode surfaces conflicts from the update output", "[infrastructure][svn][provider]")
{
    // 사전 status 검사가 없으므로 실행 출력의 충돌 행이 유일한 안내 근거다.
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"4168\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, u8"Updating '.':\nC    trunk/충돌.txt\nAt revision 4180.\n", {} });
    runner.push_response({ gitman::process_completion::exited, 0, std::u8string { info_xml_output }, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    register_working_directory(probe);
    gitman::svn_repository_provider provider { available_tool(), runner, probe, nullptr, {}, true };

    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE(result.executed);
    REQUIRE(result.succeeded);
    // 실행은 성공했지만 직접 확인 안내가 남는다.
    REQUIRE(has_diagnostic(result, gitman::diagnostic_code::operation_failed));
    // 사전 조회 2(info·svnversion) + update 1 + 사후 조회 1(info)이다.
    REQUIRE(runner.request_count() == 4);
}
