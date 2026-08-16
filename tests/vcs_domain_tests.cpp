#include "application/repository_provider.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"
#include "domain/vcs_tool.h"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }
} // namespace

TEST_CASE("VCS tool information starts unknown and unusable", "[domain][vcs][tool]")
{
    const gitman::vcs_tool_info tool {};
    REQUIRE(tool.kind == gitman::repository_kind::unknown);
    REQUIRE(tool.availability == gitman::vcs_tool_availability::unknown);
    REQUIRE(tool.executable.empty());
    REQUIRE(tool.auxiliary_executable.empty());
    REQUIRE(tool.reported_version.empty());
    REQUIRE(tool.version == gitman::vcs_tool_version {});
    REQUIRE_FALSE(tool.manually_configured);
    REQUIRE(tool.diagnostics.empty());
    REQUIRE_FALSE(tool.usable());

    // 경로 없이 `available`만 세워진 값은 사용할 수 없다고 본다.
    gitman::vcs_tool_info without_path {};
    without_path.availability = gitman::vcs_tool_availability::available;
    REQUIRE_FALSE(without_path.usable());
}

TEST_CASE("Tool versions compare component by component", "[domain][vcs][tool]")
{
    REQUIRE(gitman::vcs_tool_version { 2u, 43u, 0u } == gitman::vcs_tool_version { 2u, 43u, 0u });
    REQUIRE(gitman::vcs_tool_version { 2u, 43u, 0u } < gitman::vcs_tool_version { 2u, 43u, 1u });
    REQUIRE(gitman::vcs_tool_version { 2u, 43u, 9u } < gitman::vcs_tool_version { 2u, 44u, 0u });
    REQUIRE(gitman::vcs_tool_version { 1u, 99u, 99u } < gitman::vcs_tool_version { 2u, 0u, 0u });
    REQUIRE(gitman::vcs_tool_version { 3u, 0u, 0u } > gitman::vcs_tool_version { 2u, 52u, 0u });
}

TEST_CASE("Tool sets treat a completely missing VCS environment as a first class state", "[domain][vcs][tool]")
{
    gitman::vcs_tool_set tools {};
    REQUIRE(tools.none_available());
    REQUIRE_FALSE(tools.any_available());
    REQUIRE_FALSE(tools.available(gitman::repository_kind::git));
    REQUIRE_FALSE(tools.available(gitman::repository_kind::subversion));
    REQUIRE_FALSE(tools.available(gitman::repository_kind::unknown));

    tools.git.availability = gitman::vcs_tool_availability::available;
    tools.git.executable = u8"C:\\tools\\git.exe";
    REQUIRE(tools.any_available());
    REQUIRE_FALSE(tools.none_available());
    REQUIRE(tools.available(gitman::repository_kind::git));
    REQUIRE_FALSE(tools.available(gitman::repository_kind::subversion));
    // 종류를 지정하지 않은 조회는 "하나라도 있는가"로 답한다.
    REQUIRE(tools.available(gitman::repository_kind::unknown));

    REQUIRE(tools.tool(gitman::repository_kind::git).executable == u8"C:\\tools\\git.exe");
    REQUIRE(tools.tool(gitman::repository_kind::subversion).executable.empty());
}

TEST_CASE("Tool availability names are stable", "[domain][vcs][tool]")
{
    REQUIRE(u8_equal(gitman::vcs_tool_availability_name(gitman::vcs_tool_availability::unknown), u8"unknown"));
    REQUIRE(u8_equal(gitman::vcs_tool_availability_name(gitman::vcs_tool_availability::not_found), u8"not_found"));
    REQUIRE(u8_equal(gitman::vcs_tool_availability_name(gitman::vcs_tool_availability::path_invalid), u8"path_invalid"));
    REQUIRE(u8_equal(gitman::vcs_tool_availability_name(gitman::vcs_tool_availability::version_unreadable), u8"version_unreadable"));
    REQUIRE(u8_equal(gitman::vcs_tool_availability_name(gitman::vcs_tool_availability::too_old), u8"too_old"));
    REQUIRE(u8_equal(gitman::vcs_tool_availability_name(gitman::vcs_tool_availability::available), u8"available"));
    REQUIRE(u8_equal(gitman::vcs_tool_availability_name(static_cast<gitman::vcs_tool_availability>(-1)), u8"unknown"));
}

TEST_CASE("Working tree safety refuses unknown and interrupted states", "[domain][repository]")
{
    gitman::working_tree_summary summary {};
    // 조회하지 못한 상태를 안전하다고 보지 않는다. 모르는 상태에서 변경 명령을
    // 실행하는 편이 더 위험하다.
    REQUIRE(summary.state == gitman::working_tree_state::unknown);
    REQUIRE_FALSE(summary.is_safe_for_change());

    summary.state = gitman::working_tree_state::clean;
    REQUIRE(summary.is_safe_for_change());

    summary.state = gitman::working_tree_state::modified;
    REQUIRE_FALSE(summary.is_safe_for_change());

    summary.state = gitman::working_tree_state::conflicted;
    REQUIRE_FALSE(summary.is_safe_for_change());

    summary.state = gitman::working_tree_state::clean;
    summary.operation_in_progress = true;
    REQUIRE_FALSE(summary.is_safe_for_change());

    summary.operation_in_progress = false;
    summary.has_index_lock = true;
    REQUIRE_FALSE(summary.is_safe_for_change());

    // detached HEAD는 작업 트리 안전성과 별개 축이며 update 차단은 별도 사유로 다룬다.
    summary.has_index_lock = false;
    summary.is_detached = true;
    REQUIRE(summary.is_safe_for_change());
}

TEST_CASE("Repository snapshots default to an unknown availability", "[domain][repository]")
{
    const gitman::repository_snapshot snapshot {};
    REQUIRE(snapshot.availability == gitman::repository_availability::unknown);
    REQUIRE(snapshot.submodules.empty());
    REQUIRE(snapshot.svn_repository_root.empty());
    REQUIRE(snapshot.svn_repository_uuid.empty());
    // `svnversion`이 없는 환경에서는 판정할 수 없으므로 거짓과 미상을 구분한다.
    REQUIRE_FALSE(snapshot.has_switched_subtree.has_value());
    REQUIRE_FALSE(snapshot.has_mixed_revision.has_value());

    REQUIRE(u8_equal(gitman::repository_availability_name(gitman::repository_availability::unknown), u8"unknown"));
    REQUIRE(u8_equal(gitman::repository_availability_name(gitman::repository_availability::ready), u8"ready"));
    REQUIRE(u8_equal(gitman::repository_availability_name(gitman::repository_availability::tool_unavailable), u8"tool_unavailable"));
    REQUIRE(u8_equal(gitman::repository_availability_name(gitman::repository_availability::not_a_repository), u8"not_a_repository"));
    REQUIRE(u8_equal(gitman::repository_availability_name(gitman::repository_availability::path_unavailable), u8"path_unavailable"));
    REQUIRE(u8_equal(gitman::repository_availability_name(static_cast<gitman::repository_availability>(-1)), u8"unknown"));

    const gitman::submodule_status submodule {};
    REQUIRE(submodule.relative_path.empty());
    REQUIRE(submodule.initialized);
    REQUIRE_FALSE(submodule.revision_mismatch);
    REQUIRE_FALSE(submodule.conflicted);
}

TEST_CASE("Switch candidates default to a local branch with no tracking work", "[domain][vcs][operation]")
{
    const gitman::switch_candidate candidate {};
    REQUIRE(candidate.kind == gitman::switch_candidate_kind::git_local_branch);
    REQUIRE(candidate.display_name.empty());
    REQUIRE(candidate.target.empty());
    REQUIRE(candidate.remote_name.empty());
    REQUIRE(candidate.local_branch.empty());
    REQUIRE_FALSE(candidate.requires_tracking_branch);
    REQUIRE_FALSE(candidate.stale);

    REQUIRE(u8_equal(gitman::switch_candidate_kind_name(gitman::switch_candidate_kind::git_remote_branch), u8"git_remote_branch"));
    REQUIRE(u8_equal(gitman::switch_candidate_kind_name(gitman::switch_candidate_kind::git_local_branch), u8"git_local_branch"));
    REQUIRE(u8_equal(gitman::switch_candidate_kind_name(gitman::switch_candidate_kind::subversion_url), u8"subversion_url"));
    REQUIRE(u8_equal(gitman::switch_candidate_kind_name(static_cast<gitman::switch_candidate_kind>(-1)), u8"git_local_branch"));
}

TEST_CASE("Switch validation defaults to rejection", "[domain][vcs][operation]")
{
    // 기본값이 승인이면 검증을 잊은 코드가 통과한다. 기본값을 거부로 둔다.
    const gitman::switch_validation_result result {};
    REQUIRE_FALSE(result.approved);
    REQUIRE(result.rejection == gitman::switch_rejection::none);
    REQUIRE_FALSE(result.requires_tracking_branch_confirmation);
    REQUIRE(result.diagnostics.empty());
}

TEST_CASE("Every switch rejection has a stable name and a Korean message", "[domain][vcs][operation]")
{
    constexpr gitman::switch_rejection rejections[] {
        gitman::switch_rejection::none,
        gitman::switch_rejection::target_not_found,
        gitman::switch_rejection::already_on_target,
        gitman::switch_rejection::target_in_use,
        gitman::switch_rejection::working_tree_unsafe,
        gitman::switch_rejection::tracking_branch_confirmation_required,
        gitman::switch_rejection::tracking_branch_conflict,
        gitman::switch_rejection::ambiguous_remote,
        gitman::switch_rejection::target_not_allowed,
        gitman::switch_rejection::target_unreachable,
        gitman::switch_rejection::repository_mismatch,
        gitman::switch_rejection::tool_unavailable,
    };
    for (const gitman::switch_rejection rejection : rejections)
    {
        REQUIRE(gitman::switch_rejection_name(rejection).empty() == false);
        REQUIRE(gitman::switch_rejection_message(rejection).empty() == false);
    }

    REQUIRE(u8_equal(gitman::switch_rejection_name(gitman::switch_rejection::ambiguous_remote), u8"ambiguous_remote"));
    REQUIRE(u8_equal(gitman::switch_rejection_name(static_cast<gitman::switch_rejection>(-1)), u8"none"));
    REQUIRE(gitman::switch_rejection_message(static_cast<gitman::switch_rejection>(-1)).empty() == false);
}

TEST_CASE("Every update block reason has a stable name and a Korean message", "[domain][vcs][operation]")
{
    constexpr gitman::update_block_reason reasons[] {
        gitman::update_block_reason::none,
        gitman::update_block_reason::tool_unavailable,
        gitman::update_block_reason::repository_unavailable,
        gitman::update_block_reason::working_tree_conflicted,
        gitman::update_block_reason::working_tree_dirty,
        gitman::update_block_reason::operation_in_progress,
        gitman::update_block_reason::index_locked,
        gitman::update_block_reason::detached_head,
        gitman::update_block_reason::diverged,
        gitman::update_block_reason::no_remote_target,
        gitman::update_block_reason::submodule_unsafe,
        gitman::update_block_reason::switched_subtree,
        gitman::update_block_reason::mixed_revision,
    };
    for (const gitman::update_block_reason reason : reasons)
    {
        REQUIRE(gitman::update_block_reason_name(reason).empty() == false);
        REQUIRE(gitman::update_block_reason_message(reason).empty() == false);
    }

    REQUIRE(u8_equal(gitman::update_block_reason_name(gitman::update_block_reason::working_tree_dirty), u8"working_tree_dirty"));
    REQUIRE(u8_equal(gitman::update_block_reason_name(static_cast<gitman::update_block_reason>(-1)), u8"none"));
    REQUIRE(gitman::update_block_reason_message(static_cast<gitman::update_block_reason>(-1)).empty() == false);

    // ADR-003에 따라 submodule 갱신은 기본적으로 꺼져 있다.
    const gitman::update_options options {};
    REQUIRE_FALSE(options.update_submodules);
}

TEST_CASE("Provider results start as not executed and not succeeded", "[application][vcs][provider]")
{
    // `executed`가 거짓이면 어떤 process request도 만들지 않았다는 뜻이다. REQ-007의
    // 수용 기준을 계약 수준에서 관측할 수 있게 하는 값이다.
    const gitman::repository_change_result change {};
    REQUIRE_FALSE(change.executed);
    REQUIRE_FALSE(change.succeeded);
    REQUIRE(change.blocked_by == gitman::update_block_reason::none);
    REQUIRE(change.rejected_by == gitman::switch_rejection::none);
    REQUIRE(change.diagnostics.empty());

    const gitman::switch_candidate_result candidates {};
    REQUIRE(candidates.candidates.empty());
    REQUIRE_FALSE(candidates.stale);

    gitman::repository_query_result query {};
    REQUIRE_FALSE(query.has_errors());
    query.diagnostics.push_back({ gitman::diagnostic_code::vcs_tool_not_found, gitman::diagnostic_severity::warning, u8"경고", {}, {} });
    // 도구 부재는 warning이므로 조회 결과를 오류로 만들지 않는다.
    REQUIRE_FALSE(query.has_errors());
    query.diagnostics.push_back({ gitman::diagnostic_code::vcs_command_failed, gitman::diagnostic_severity::error, u8"오류", {}, {} });
    REQUIRE(query.has_errors());
}
