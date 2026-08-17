#include "domain/discovery.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string_view>
#include <vector>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    gitman::discovery_candidate make_candidate(const std::u8string_view name, const std::u8string_view absolute_path = u8"")
    {
        gitman::discovery_candidate candidate {};
        candidate.directory_name = name;
        candidate.absolute_path = absolute_path.empty() ? std::u8string { u8"C:\\scan\\" } + std::u8string { name } : std::u8string { absolute_path };
        return candidate;
    }
} // namespace

TEST_CASE("Discovery candidates start unclassified and selectable state follows exclusion", "[discovery][domain]")
{
    const gitman::discovery_candidate candidate {};
    REQUIRE(candidate.directory_name.empty());
    REQUIRE(candidate.absolute_path.empty());
    REQUIRE(candidate.normalized_path.empty());
    REQUIRE(candidate.kind == gitman::repository_kind::unknown);
    REQUIRE_FALSE(candidate.via_git_file);
    REQUIRE(candidate.exclusion == gitman::discovery_exclusion::none);

    gitman::discovery_candidate excluded {};
    for (const gitman::discovery_exclusion exclusion : {
             gitman::discovery_exclusion::not_a_repository,
             gitman::discovery_exclusion::bare_repository,
             gitman::discovery_exclusion::conflicting_metadata,
             gitman::discovery_exclusion::reparse_point,
             gitman::discovery_exclusion::already_registered,
             gitman::discovery_exclusion::inaccessible,
         })
    {
        excluded.exclusion = exclusion;
        REQUIRE_FALSE(excluded.selectable());
    }
    excluded.exclusion = gitman::discovery_exclusion::none;
    REQUIRE(excluded.selectable());
}

TEST_CASE("Discovery results start incomplete and empty", "[discovery][domain]")
{
    const gitman::discovery_result result {};
    REQUIRE_FALSE(result.completed);
    REQUIRE_FALSE(result.root_is_repository);
    REQUIRE(result.candidates.empty());
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("Marker classification recognises the repository kinds", "[discovery][domain]")
{
    gitman::repository_marker_set markers {};

    SECTION("git 디렉터리는 일반 Git 후보다")
    {
        markers.has_git_directory = true;
        const auto classification { gitman::classify_discovery_markers(markers) };
        REQUIRE(classification == gitman::discovery_classification { gitman::repository_kind::git, false, gitman::discovery_exclusion::none });
    }

    SECTION("git 파일은 worktree 및 submodule 표시를 남긴 Git 후보다")
    {
        markers.has_git_file = true;
        const auto classification { gitman::classify_discovery_markers(markers) };
        REQUIRE(classification == gitman::discovery_classification { gitman::repository_kind::git, true, gitman::discovery_exclusion::none });
    }

    SECTION("svn 디렉터리는 SVN 후보다")
    {
        markers.has_svn_directory = true;
        const auto classification { gitman::classify_discovery_markers(markers) };
        REQUIRE(classification == gitman::discovery_classification { gitman::repository_kind::subversion, false, gitman::discovery_exclusion::none });
    }
}

TEST_CASE("Marker classification excludes conflicting metadata before choosing a kind", "[discovery][domain]")
{
    gitman::repository_marker_set markers {};
    markers.has_svn_directory = true;

    SECTION("git 디렉터리와 svn의 동시 발견")
    {
        markers.has_git_directory = true;
    }

    SECTION("git 파일과 svn의 동시 발견")
    {
        markers.has_git_file = true;
    }

    const auto classification { gitman::classify_discovery_markers(markers) };
    REQUIRE(classification.kind == gitman::repository_kind::unknown);
    REQUIRE_FALSE(classification.via_git_file);
    REQUIRE(classification.exclusion == gitman::discovery_exclusion::conflicting_metadata);
}

TEST_CASE("Marker classification requires all three bare markers", "[discovery][domain]")
{
    SECTION("세 표식이 모두 있으면 bare Git 저장소로 제외한다")
    {
        gitman::repository_marker_set markers {};
        markers.has_head_file = true;
        markers.has_objects_directory = true;
        markers.has_refs_directory = true;
        const auto classification { gitman::classify_discovery_markers(markers) };
        REQUIRE(classification == gitman::discovery_classification { gitman::repository_kind::git, false, gitman::discovery_exclusion::bare_repository });
    }

    SECTION("표식이 하나라도 빠지면 비저장소다")
    {
        // HEAD 파일 하나(다른 도구의 산출물)나 refs 디렉터리 하나로 bare를 단정하면
        // 오탐이 생긴다. 세 표식의 진부분집합 전체를 확인한다.
        for (int mask = 0; mask < 7; ++mask)
        {
            gitman::repository_marker_set markers {};
            markers.has_head_file = (mask & 1) != 0;
            markers.has_objects_directory = (mask & 2) != 0;
            markers.has_refs_directory = (mask & 4) != 0;
            const auto classification { gitman::classify_discovery_markers(markers) };
            REQUIRE(classification.kind == gitman::repository_kind::unknown);
            REQUIRE(classification.exclusion == gitman::discovery_exclusion::not_a_repository);
        }
    }
}

TEST_CASE("Marker classification prefers work tree markers over bare markers", "[discovery][domain]")
{
    // 작업 복사본 표식과 bare 표식이 함께 있으면 작업 복사본으로 판정한다. bare
    // 휴리스틱은 `.git`도 `.svn`도 없을 때만 적용된다.
    gitman::repository_marker_set markers {};
    markers.has_head_file = true;
    markers.has_objects_directory = true;
    markers.has_refs_directory = true;

    SECTION("git 디렉터리가 우선한다")
    {
        markers.has_git_directory = true;
        const auto classification { gitman::classify_discovery_markers(markers) };
        REQUIRE(classification == gitman::discovery_classification { gitman::repository_kind::git, false, gitman::discovery_exclusion::none });
    }

    SECTION("git 파일이 우선한다")
    {
        markers.has_git_file = true;
        const auto classification { gitman::classify_discovery_markers(markers) };
        REQUIRE(classification == gitman::discovery_classification { gitman::repository_kind::git, true, gitman::discovery_exclusion::none });
    }

    SECTION("svn 디렉터리가 우선한다")
    {
        markers.has_svn_directory = true;
        const auto classification { gitman::classify_discovery_markers(markers) };
        REQUIRE(classification == gitman::discovery_classification { gitman::repository_kind::subversion, false, gitman::discovery_exclusion::none });
    }
}

TEST_CASE("Marker classification treats a failed probe as inaccessible regardless of markers", "[discovery][domain]")
{
    gitman::repository_marker_set markers {};
    markers.probe_failed = true;
    markers.has_git_directory = true;
    markers.has_svn_directory = true;

    const auto classification { gitman::classify_discovery_markers(markers) };
    REQUIRE(classification.kind == gitman::repository_kind::unknown);
    REQUIRE(classification.exclusion == gitman::discovery_exclusion::inaccessible);
}

TEST_CASE("Candidate ordering ignores ASCII case and stays deterministic", "[discovery][domain]")
{
    std::vector<gitman::discovery_candidate> candidates {
        make_candidate(u8"zeta"),
        make_candidate(u8"Alpha"),
        make_candidate(u8"beta"),
        make_candidate(u8"alpine"),
    };
    std::sort(candidates.begin(), candidates.end(), gitman::discovery_candidate_before);

    REQUIRE(u8_equal(candidates[0].directory_name, u8"Alpha"));
    REQUIRE(u8_equal(candidates[1].directory_name, u8"alpine"));
    REQUIRE(u8_equal(candidates[2].directory_name, u8"beta"));
    REQUIRE(u8_equal(candidates[3].directory_name, u8"zeta"));
}

TEST_CASE("Candidate ordering breaks case only ties by code unit", "[discovery][domain]")
{
    // NTFS는 대소문자만 다른 이름을 같은 디렉터리에 두지 않지만, 정렬은 입력 순서와
    // 무관하게 결정적이어야 한다.
    const gitman::discovery_candidate upper { make_candidate(u8"Alpha") };
    const gitman::discovery_candidate lower { make_candidate(u8"alpha") };
    REQUIRE(gitman::discovery_candidate_before(upper, lower));
    REQUIRE_FALSE(gitman::discovery_candidate_before(lower, upper));

    // 이름까지 같으면 절대 경로로 구분해 어느 쪽으로도 참이 두 번 나오지 않는다.
    const gitman::discovery_candidate first { make_candidate(u8"same", u8"C:\\left\\same") };
    const gitman::discovery_candidate second { make_candidate(u8"same", u8"C:\\right\\same") };
    REQUIRE(gitman::discovery_candidate_before(first, second));
    REQUIRE_FALSE(gitman::discovery_candidate_before(second, first));
    REQUIRE_FALSE(gitman::discovery_candidate_before(first, first));
}

TEST_CASE("Candidate ordering handles non ASCII names by code unit", "[discovery][domain]")
{
    // ASCII 밖 이름에는 대소문자 접기를 적용하지 않는다. UTF-8 code unit 순서는
    // code point 순서와 같아 로캘 없이 결정적이다.
    std::vector<gitman::discovery_candidate> candidates {
        make_candidate(u8"한글"),
        make_candidate(u8"tools"),
        make_candidate(u8"가나다"),
    };
    std::sort(candidates.begin(), candidates.end(), gitman::discovery_candidate_before);

    REQUIRE(u8_equal(candidates[0].directory_name, u8"tools"));
    REQUIRE(u8_equal(candidates[1].directory_name, u8"가나다"));
    REQUIRE(u8_equal(candidates[2].directory_name, u8"한글"));
}

TEST_CASE("Discovery exclusion names are stable", "[discovery][domain]")
{
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(gitman::discovery_exclusion::none), u8"none"));
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(gitman::discovery_exclusion::not_a_repository), u8"not_a_repository"));
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(gitman::discovery_exclusion::bare_repository), u8"bare_repository"));
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(gitman::discovery_exclusion::conflicting_metadata), u8"conflicting_metadata"));
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(gitman::discovery_exclusion::reparse_point), u8"reparse_point"));
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(gitman::discovery_exclusion::already_registered), u8"already_registered"));
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(gitman::discovery_exclusion::inaccessible), u8"inaccessible"));
    REQUIRE(u8_equal(gitman::discovery_exclusion_name(static_cast<gitman::discovery_exclusion>(-1)), u8"unknown"));
}
