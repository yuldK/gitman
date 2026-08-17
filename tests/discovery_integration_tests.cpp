#include "application/discovery_service.h"
#include "application/process_runner.h"
#include "helpers/discovery_test_doubles.h"
#include "helpers/git_repository_fixture.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/win32_directory_enumerator.h"
#include "platform/win32/win32_process_runner.h"
#include "platform/win32/win32_vcs_file_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace {
    // junction은 표준 API로 만들 수 없어 `cmd.exe`의 `mklink /J`를 사용한다. 관리자
    // 권한이 필요한 심볼릭 링크와 달리 junction은 일반 권한으로 만들 수 있다.
    bool create_junction(const std::u8string_view working_directory, const std::u8string_view link, const std::u8string_view target)
    {
        const std::unique_ptr<gitman::process_runner> runner { gitman::win32::make_process_runner() };
        if (runner == nullptr)
            return false;

        std::u8string comspec { gitman::win32::read_environment_variable(u8"ComSpec") };
        if (comspec.empty())
            comspec = u8"C:\\Windows\\System32\\cmd.exe";

        gitman::process_request request {};
        request.executable = comspec;
        request.arguments = { u8"/c", u8"mklink", u8"/J", std::u8string { link }, std::u8string { target } };
        request.working_directory = working_directory;
        request.timeout = { std::chrono::milliseconds { 30000 } };
        request.maximum_captured_bytes_per_stream = 64u * 1024u;
        request.text_encoding = gitman::process_text_encoding::active_code_page_fallback;

        const gitman::process_result result { runner->run(request, nullptr, {}) };
        return result.completion == gitman::process_completion::exited && result.exit_code == 0;
    }

    const gitman::discovery_candidate* find_candidate(const gitman::discovery_result& result, const std::u8string_view name)
    {
        for (const gitman::discovery_candidate& candidate : result.candidates)
            if (candidate.directory_name == name)
                return &candidate;
        return nullptr;
    }

    gitman::discovery_result discover(const std::u8string_view scan_root, const gitman::workspace_document& document = {})
    {
        const std::unique_ptr<gitman::directory_enumerator> enumerator { gitman::win32::make_directory_enumerator() };
        const std::unique_ptr<gitman::vcs_file_probe> probe { gitman::win32::make_vcs_file_probe() };
        const std::unique_ptr<gitman::project_path_resolver> resolver { gitman::win32::make_project_path_resolver() };
        REQUIRE(enumerator != nullptr);
        REQUIRE(probe != nullptr);
        REQUIRE(resolver != nullptr);

        gitman::discovery_service service { *enumerator, *probe, *resolver };
        return service.discover_children(scan_root, document, {});
    }
} // namespace

#define REQUIRE_GIT_AVAILABLE(fixture)                                                                                                                                                                 \
    if ((fixture).available() == false)                                                                                                                                                                \
    SKIP("호스트에 사용할 수 있는 Git이 없어 통합 test를 건너뜁니다")

TEST_CASE("A real mixed layout is classified without any VCS executable", "[discovery][integration]")
{
    // 판정이 표식 기반이라 SVN 경로도 `svn.exe` 없이 실제 filesystem으로 검증된다.
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string scan { directory.make_directory(u8"scan") };
    (void)directory.make_directory(u8"scan\\svn-copy\\.svn");
    (void)directory.make_directory(u8"scan\\plain");
    (void)directory.make_directory(u8"scan\\target");
    (void)directory.make_file(u8"scan\\readme.txt");

    if (create_junction(scan, u8"linked-junction", directory.path_of(u8"scan\\target")) == false)
        SKIP("junction을 만들 수 없어 통합 test를 건너뜁니다");

    const gitman::discovery_result result { discover(scan) };
    REQUIRE(result.completed);
    REQUIRE_FALSE(result.root_is_repository);
    REQUIRE(result.candidates.size() == 4u);

    const gitman::discovery_candidate* const svn_copy { find_candidate(result, u8"svn-copy") };
    REQUIRE(svn_copy != nullptr);
    REQUIRE(svn_copy->kind == gitman::repository_kind::subversion);
    REQUIRE(svn_copy->selectable());
    REQUIRE(svn_copy->normalized_path.empty() == false);

    const gitman::discovery_candidate* const junction { find_candidate(result, u8"linked-junction") };
    REQUIRE(junction != nullptr);
    REQUIRE(junction->exclusion == gitman::discovery_exclusion::reparse_point);
    REQUIRE(junction->kind == gitman::repository_kind::unknown);

    REQUIRE(find_candidate(result, u8"plain")->exclusion == gitman::discovery_exclusion::not_a_repository);
    REQUIRE(find_candidate(result, u8"target")->exclusion == gitman::discovery_exclusion::not_a_repository);
    REQUIRE(find_candidate(result, u8"readme.txt") == nullptr);
}

TEST_CASE("Real git layouts are classified from their markers", "[discovery][integration]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string scan { fixture.make_directory(u8"scan") };
    const std::u8string repository { fixture.make_repository(u8"scan\\repo") };
    fixture.write_file(repository, u8"a.txt", "one\n");
    fixture.git(repository, { u8"add", u8"-A" });
    fixture.git(repository, { u8"commit", u8"-m", u8"init" });
    (void)fixture.make_bare_repository(u8"scan\\bare");
    (void)fixture.make_directory(u8"scan\\plain");

    // linked worktree는 `.git` 파일 표식이다. 원본 저장소는 스캔 범위 밖에 둔다.
    const std::u8string source { fixture.make_repository(u8"wt-source") };
    fixture.write_file(source, u8"b.txt", "two\n");
    fixture.git(source, { u8"add", u8"-A" });
    fixture.git(source, { u8"commit", u8"-m", u8"init" });
    fixture.git(source, { u8"worktree", u8"add", fixture.path_of(u8"scan\\linked-worktree") });
    REQUIRE(fixture.failures().empty());

    const gitman::discovery_result result { discover(scan) };
    REQUIRE(result.completed);
    REQUIRE_FALSE(result.root_is_repository);
    REQUIRE(result.candidates.size() == 4u);

    const gitman::discovery_candidate* const repo { find_candidate(result, u8"repo") };
    REQUIRE(repo != nullptr);
    REQUIRE(repo->kind == gitman::repository_kind::git);
    REQUIRE_FALSE(repo->via_git_file);
    REQUIRE(repo->selectable());

    const gitman::discovery_candidate* const worktree { find_candidate(result, u8"linked-worktree") };
    REQUIRE(worktree != nullptr);
    REQUIRE(worktree->kind == gitman::repository_kind::git);
    REQUIRE(worktree->via_git_file);
    REQUIRE(worktree->selectable());

    const gitman::discovery_candidate* const bare { find_candidate(result, u8"bare") };
    REQUIRE(bare != nullptr);
    REQUIRE(bare->kind == gitman::repository_kind::git);
    REQUIRE(bare->exclusion == gitman::discovery_exclusion::bare_repository);

    REQUIRE(find_candidate(result, u8"plain")->exclusion == gitman::discovery_exclusion::not_a_repository);
}

TEST_CASE("A registered real repository is marked as a duplicate candidate", "[discovery][integration]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string scan { fixture.make_directory(u8"scan") };
    const std::u8string repository { fixture.make_repository(u8"scan\\repo") };
    REQUIRE(fixture.failures().empty());

    // 실제 해석기로 문서 항목을 만들어 정규화 비교가 실제 규칙으로 동작하는 것을
    // 확인한다. 경로 표기가 달라도(구분자 `/`) 같은 항목으로 잡혀야 한다.
    const std::unique_ptr<gitman::project_path_resolver> resolver { gitman::win32::make_project_path_resolver() };
    REQUIRE(resolver != nullptr);
    std::u8string slashed { repository };
    for (char8_t& value : slashed)
        if (value == u8'\\')
            value = u8'/';

    gitman::workspace_document document {};
    gitman::project_definition registered {};
    registered.id.value = u8"repo";
    registered.path.original = slashed;
    registered.path.normalized = resolver->resolve(slashed, {}).normalized;
    document.projects.push_back(registered);

    const gitman::discovery_result result { discover(scan, document) };
    REQUIRE(result.completed);
    REQUIRE(result.candidates.size() == 1u);
    REQUIRE(result.candidates.front().exclusion == gitman::discovery_exclusion::already_registered);
}

TEST_CASE("The scan root inside a real repository is reported", "[discovery][integration]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string repository { fixture.make_repository(u8"root-repo") };
    (void)fixture.make_directory(u8"root-repo\\child");
    REQUIRE(fixture.failures().empty());

    const gitman::discovery_result result { discover(repository) };
    REQUIRE(result.completed);
    REQUIRE(result.root_is_repository);

    // 루트 자체는 후보가 아니고 `.git` 디렉터리는 자식으로 판정되지 않아야 한다.
    REQUIRE(find_candidate(result, u8".git") != nullptr);
    REQUIRE(find_candidate(result, u8".git")->exclusion != gitman::discovery_exclusion::none);
}

TEST_CASE("Discovery completes over hundreds of children", "[discovery][integration]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string scan { directory.make_directory(u8"scan") };
    for (std::size_t index = 0; index < 300; ++index)
    {
        const std::string digits { std::to_string(index) };
        std::u8string relative { u8"scan\\child-" };
        relative.append(digits.begin(), digits.end());
        (void)directory.make_directory(relative);
    }

    const gitman::discovery_result result { discover(scan) };
    REQUIRE(result.completed);
    REQUIRE(result.candidates.size() == 300u);
    for (const gitman::discovery_candidate& candidate : result.candidates)
        REQUIRE(candidate.exclusion == gitman::discovery_exclusion::not_a_repository);
}
