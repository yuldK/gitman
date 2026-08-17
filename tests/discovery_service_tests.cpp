#include "application/discovery_service.h"
#include "application/process_cancellation.h"
#include "helpers/discovery_test_doubles.h"
#include "helpers/vcs_test_doubles.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {
    // 탐색 단위 test가 실제 디스크를 조회하지 않도록 lexical 규칙만 흉내 낸다.
    // `json_project_store_tests.cpp`의 대역과 같은 계약이다.
    class fake_project_path_resolver final : public gitman::project_path_resolver
    {
    public:
        [[nodiscard]] gitman::project_path_resolution resolve(const std::u8string_view original_path, const std::u8string_view) noexcept override
        {
            try
            {
                return { std::u8string { original_path }, gitman::configured_path_state::available, std::nullopt };
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] bool normalized_equal(const std::u8string_view left, const std::u8string_view right) noexcept override
        {
            return left == right;
        }
    };

    constexpr std::u8string_view scan_root { u8"C:\\scan" };

    gitman::directory_entry make_entry(const std::u8string_view name, const bool is_directory = true, const bool is_reparse_point = false)
    {
        return { std::u8string { name }, is_directory, is_reparse_point };
    }

    void register_git_child(gitman::testing::fake_vcs_file_probe& probe, const std::u8string_view name)
    {
        probe.add_directory(std::u8string { scan_root } + u8"\\" + std::u8string { name } + u8"\\.git");
    }

    void register_svn_child(gitman::testing::fake_vcs_file_probe& probe, const std::u8string_view name)
    {
        probe.add_directory(std::u8string { scan_root } + u8"\\" + std::u8string { name } + u8"\\.svn");
    }

    const gitman::discovery_candidate* find_candidate(const gitman::discovery_result& result, const std::u8string_view name)
    {
        for (const gitman::discovery_candidate& candidate : result.candidates)
            if (candidate.directory_name == name)
                return &candidate;
        return nullptr;
    }
} // namespace

TEST_CASE("Marker collection maps probe results onto the marker set", "[discovery][service]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\scan\\child\\.git");
    probe.add_directory(u8"C:\\scan\\child\\.svn");
    probe.add_file(u8"C:\\scan\\child\\HEAD");
    probe.add_directory(u8"C:\\scan\\child\\objects");
    probe.add_directory(u8"C:\\scan\\child\\refs");

    const gitman::repository_marker_set markers { gitman::collect_repository_markers(probe, u8"C:\\scan\\child") };
    REQUIRE_FALSE(markers.probe_failed);
    REQUIRE_FALSE(markers.has_git_directory);
    REQUIRE(markers.has_git_file);
    REQUIRE(markers.has_svn_directory);
    REQUIRE(markers.has_head_file);
    REQUIRE(markers.has_objects_directory);
    REQUIRE(markers.has_refs_directory);

    // `.git`이 디렉터리인 경우와 `.svn`이 파일인 경우의 구분도 확인한다.
    gitman::testing::fake_vcs_file_probe other {};
    other.add_directory(u8"C:\\scan\\other\\.git");
    other.add_file(u8"C:\\scan\\other\\.svn");
    const gitman::repository_marker_set other_markers { gitman::collect_repository_markers(other, u8"C:\\scan\\other") };
    REQUIRE(other_markers.has_git_directory);
    REQUIRE_FALSE(other_markers.has_git_file);
    REQUIRE_FALSE(other_markers.has_svn_directory);
}

TEST_CASE("Discovery rejects a relative scan root without touching the filesystem", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::testing::fake_vcs_file_probe probe {};
    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };

    const gitman::discovery_result result { service.discover_children(u8"relative\\path", {}, {}) };
    REQUIRE_FALSE(result.completed);
    REQUIRE(result.candidates.empty());
    REQUIRE(result.diagnostics.size() == 1u);
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::discovery_root_unavailable);
    REQUIRE(result.diagnostics.front().severity == gitman::diagnostic_severity::error);
    REQUIRE(enumerator.enumeration_count() == 0u);
}

TEST_CASE("Discovery reports an unavailable scan root with the native error", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::testing::fake_vcs_file_probe probe {};
    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };

    // 등록하지 않은 경로는 ERROR_PATH_NOT_FOUND(3)로 실패한다.
    const gitman::discovery_result result { service.discover_children(scan_root, {}, {}) };
    REQUIRE_FALSE(result.completed);
    REQUIRE(result.candidates.empty());
    REQUIRE(result.diagnostics.size() == 1u);
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::discovery_root_unavailable);
    REQUIRE(result.diagnostics.front().native_error.has_value());
    REQUIRE(*result.diagnostics.front().native_error == 3u);
}

TEST_CASE("Discovery classifies children and sorts candidates deterministically", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.entries.push_back(make_entry(u8"zulu"));
    listing.entries.push_back(make_entry(u8"Alpha"));
    listing.entries.push_back(make_entry(u8"svn-copy"));
    listing.entries.push_back(make_entry(u8"bare-store"));
    listing.entries.push_back(make_entry(u8"plain"));
    listing.entries.push_back(make_entry(u8"linked"));
    listing.entries.push_back(make_entry(u8"note.txt", false));
    enumerator.set_listing(scan_root, std::move(listing));

    gitman::testing::fake_vcs_file_probe probe {};
    register_git_child(probe, u8"zulu");
    register_git_child(probe, u8"Alpha");
    register_svn_child(probe, u8"svn-copy");
    probe.add_file(u8"C:\\scan\\bare-store\\HEAD");
    probe.add_directory(u8"C:\\scan\\bare-store\\objects");
    probe.add_directory(u8"C:\\scan\\bare-store\\refs");
    probe.add_file(u8"C:\\scan\\linked\\.git");

    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };
    const gitman::discovery_result result { service.discover_children(scan_root, {}, {}) };

    REQUIRE(result.completed);
    REQUIRE_FALSE(result.root_is_repository);

    // 파일 항목은 후보가 아니다. 디렉터리 6개만 이름 순서로 남는다.
    REQUIRE(result.candidates.size() == 6u);
    REQUIRE(result.candidates[0].directory_name == u8"Alpha");
    REQUIRE(result.candidates[1].directory_name == u8"bare-store");
    REQUIRE(result.candidates[2].directory_name == u8"linked");
    REQUIRE(result.candidates[3].directory_name == u8"plain");
    REQUIRE(result.candidates[4].directory_name == u8"svn-copy");
    REQUIRE(result.candidates[5].directory_name == u8"zulu");

    const gitman::discovery_candidate* const git_candidate { find_candidate(result, u8"zulu") };
    REQUIRE(git_candidate != nullptr);
    REQUIRE(git_candidate->kind == gitman::repository_kind::git);
    REQUIRE(git_candidate->selectable());
    REQUIRE(git_candidate->absolute_path == u8"C:\\scan\\zulu");
    REQUIRE(git_candidate->normalized_path == u8"C:\\scan\\zulu");

    const gitman::discovery_candidate* const linked { find_candidate(result, u8"linked") };
    REQUIRE(linked != nullptr);
    REQUIRE(linked->kind == gitman::repository_kind::git);
    REQUIRE(linked->via_git_file);
    REQUIRE(linked->selectable());

    const gitman::discovery_candidate* const svn_candidate { find_candidate(result, u8"svn-copy") };
    REQUIRE(svn_candidate != nullptr);
    REQUIRE(svn_candidate->kind == gitman::repository_kind::subversion);
    REQUIRE(svn_candidate->selectable());

    const gitman::discovery_candidate* const bare { find_candidate(result, u8"bare-store") };
    REQUIRE(bare != nullptr);
    REQUIRE(bare->kind == gitman::repository_kind::git);
    REQUIRE(bare->exclusion == gitman::discovery_exclusion::bare_repository);

    const gitman::discovery_candidate* const plain { find_candidate(result, u8"plain") };
    REQUIRE(plain != nullptr);
    REQUIRE(plain->exclusion == gitman::discovery_exclusion::not_a_repository);
}

TEST_CASE("Discovery excludes reparse points without probing them", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.entries.push_back(make_entry(u8"junction", true, true));
    enumerator.set_listing(scan_root, std::move(listing));

    // 링크 대상이 실제 Git 저장소처럼 보여도 판정하지 않는다.
    gitman::testing::fake_vcs_file_probe probe {};
    register_git_child(probe, u8"junction");

    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };
    const gitman::discovery_result result { service.discover_children(scan_root, {}, {}) };

    REQUIRE(result.completed);
    REQUIRE(result.candidates.size() == 1u);
    REQUIRE(result.candidates.front().exclusion == gitman::discovery_exclusion::reparse_point);
    REQUIRE(result.candidates.front().kind == gitman::repository_kind::unknown);
}

TEST_CASE("Discovery marks candidates that are already registered", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.entries.push_back(make_entry(u8"active"));
    listing.entries.push_back(make_entry(u8"disabled"));
    listing.entries.push_back(make_entry(u8"fresh"));
    enumerator.set_listing(scan_root, std::move(listing));

    gitman::testing::fake_vcs_file_probe probe {};
    register_git_child(probe, u8"active");
    register_git_child(probe, u8"disabled");
    register_git_child(probe, u8"fresh");

    gitman::workspace_document document {};
    gitman::project_definition active {};
    active.id.value = u8"active";
    active.path.normalized = u8"C:\\scan\\active";
    document.projects.push_back(active);

    // 비활성 프로젝트도 문서에 있는 항목이므로 중복이다 (계획 4.5).
    gitman::project_definition disabled {};
    disabled.id.value = u8"disabled";
    disabled.enabled = false;
    disabled.path.normalized = u8"C:\\scan\\disabled";
    document.projects.push_back(disabled);

    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };
    const gitman::discovery_result result { service.discover_children(scan_root, document, {}) };

    REQUIRE(result.completed);
    REQUIRE(find_candidate(result, u8"active")->exclusion == gitman::discovery_exclusion::already_registered);
    REQUIRE(find_candidate(result, u8"disabled")->exclusion == gitman::discovery_exclusion::already_registered);
    REQUIRE(find_candidate(result, u8"fresh")->selectable());
}

TEST_CASE("An existing exclusion is not replaced by the duplicate marker", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.entries.push_back(make_entry(u8"junction", true, true));
    enumerator.set_listing(scan_root, std::move(listing));

    gitman::workspace_document document {};
    gitman::project_definition registered {};
    registered.id.value = u8"junction";
    registered.path.normalized = u8"C:\\scan\\junction";
    document.projects.push_back(registered);

    gitman::testing::fake_vcs_file_probe probe {};
    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };
    const gitman::discovery_result result { service.discover_children(scan_root, document, {}) };

    // 먼저 판정된 사유가 남는다. 사용자는 링크라서 제외됐다는 실제 원인을 봐야 한다.
    REQUIRE(result.candidates.size() == 1u);
    REQUIRE(result.candidates.front().exclusion == gitman::discovery_exclusion::reparse_point);
}

TEST_CASE("Discovery reports whether the scan root itself is a repository", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    enumerator.set_listing(scan_root, std::move(listing));

    fake_project_path_resolver resolver {};

    SECTION("작업 복사본 루트")
    {
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_directory(u8"C:\\scan\\.git");
        gitman::discovery_service service { enumerator, probe, resolver };
        const gitman::discovery_result result { service.discover_children(scan_root, {}, {}) };
        REQUIRE(result.completed);
        REQUIRE(result.root_is_repository);
    }

    SECTION("bare 저장소 루트도 저장소다")
    {
        gitman::testing::fake_vcs_file_probe probe {};
        probe.add_file(u8"C:\\scan\\HEAD");
        probe.add_directory(u8"C:\\scan\\objects");
        probe.add_directory(u8"C:\\scan\\refs");
        gitman::discovery_service service { enumerator, probe, resolver };
        const gitman::discovery_result result { service.discover_children(scan_root, {}, {}) };
        REQUIRE(result.completed);
        REQUIRE(result.root_is_repository);
    }

    SECTION("일반 디렉터리 루트")
    {
        gitman::testing::fake_vcs_file_probe probe {};
        gitman::discovery_service service { enumerator, probe, resolver };
        const gitman::discovery_result result { service.discover_children(scan_root, {}, {}) };
        REQUIRE(result.completed);
        REQUIRE_FALSE(result.root_is_repository);
    }
}

TEST_CASE("A cancelled discovery does not enumerate at all", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.entries.push_back(make_entry(u8"repo"));
    enumerator.set_listing(scan_root, std::move(listing));

    gitman::testing::fake_vcs_file_probe probe {};
    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };

    gitman::process_cancellation_source source {};
    source.request_cancellation();
    const gitman::discovery_result result { service.discover_children(scan_root, {}, source.token()) };

    REQUIRE_FALSE(result.completed);
    REQUIRE(result.candidates.empty());
    REQUIRE(result.diagnostics.size() == 1u);
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::discovery_cancelled);
    REQUIRE(result.diagnostics.front().severity == gitman::diagnostic_severity::warning);
    REQUIRE(enumerator.enumeration_count() == 0u);
}

TEST_CASE("Unreadable entry names surface as a warning while discovery continues", "[discovery][service]")
{
    gitman::testing::fake_directory_enumerator enumerator {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.unreadable_name_count = 2u;
    listing.entries.push_back(make_entry(u8"repo"));
    enumerator.set_listing(scan_root, std::move(listing));

    gitman::testing::fake_vcs_file_probe probe {};
    register_git_child(probe, u8"repo");

    fake_project_path_resolver resolver {};
    gitman::discovery_service service { enumerator, probe, resolver };
    const gitman::discovery_result result { service.discover_children(scan_root, {}, {}) };

    // 이름을 읽지 못한 항목은 후보가 될 수 없지만 탐색 전체는 계속된다. 조용한
    // 누락 대신 warning으로 알린다.
    REQUIRE(result.completed);
    REQUIRE(result.candidates.size() == 1u);
    REQUIRE(result.diagnostics.size() == 1u);
    REQUIRE(result.diagnostics.front().code == gitman::diagnostic_code::discovery_child_skipped);
    REQUIRE(result.diagnostics.front().severity == gitman::diagnostic_severity::warning);
}

TEST_CASE("Discovery diagnostic code names are stable", "[discovery][service]")
{
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::discovery_root_unavailable) == std::u8string_view { u8"discovery_root_unavailable" });
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::discovery_child_skipped) == std::u8string_view { u8"discovery_child_skipped" });
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::discovery_cancelled) == std::u8string_view { u8"discovery_cancelled" });
}
