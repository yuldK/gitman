#include "application/switch_validation_service.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {
    gitman::switch_candidate remote_candidate(const std::u8string_view remote, const std::u8string_view branch, const std::u8string_view local = {})
    {
        gitman::switch_candidate candidate {};
        candidate.kind = gitman::switch_candidate_kind::git_remote_branch;
        candidate.display_name = std::u8string { remote } + u8"/" + std::u8string { branch };
        candidate.target = u8"refs/remotes/" + std::u8string { remote } + u8"/" + std::u8string { branch };
        candidate.remote_name = remote;
        candidate.local_branch = local;
        candidate.requires_tracking_branch = local.empty();
        return candidate;
    }

    gitman::switch_candidate local_candidate(const std::u8string_view branch)
    {
        gitman::switch_candidate candidate {};
        candidate.kind = gitman::switch_candidate_kind::git_local_branch;
        candidate.display_name = branch;
        candidate.target = u8"refs/heads/" + std::u8string { branch };
        candidate.local_branch = branch;
        return candidate;
    }

    gitman::switch_candidate url_candidate(const std::u8string_view url)
    {
        gitman::switch_candidate candidate {};
        candidate.kind = gitman::switch_candidate_kind::subversion_url;
        candidate.display_name = url;
        candidate.target = url;
        return candidate;
    }

    // `origin/main`은 local `main`이 추적하고, `fork/main`은 같은 이름의 local branch가
    // 다른 upstream을 가진 경우다. `origin/new-branch`에는 대응하는 local branch가 없다.
    std::vector<gitman::switch_candidate> git_candidates()
    {
        return {
            remote_candidate(u8"origin", u8"main", u8"main"),
            remote_candidate(u8"origin", u8"new-branch"),
            remote_candidate(u8"fork", u8"main", u8"main"),
            local_candidate(u8"only-local"),
        };
    }

    gitman::git_switch_context git_context(const std::u8string_view current = u8"main")
    {
        gitman::git_switch_context context {};
        context.snapshot.availability = gitman::repository_availability::ready;
        context.snapshot.kind = gitman::repository_kind::git;
        context.snapshot.current_reference = current;
        context.snapshot.working_tree.state = gitman::working_tree_state::clean;
        context.local_branches = { { u8"main", u8"refs/remotes/origin/main" }, { u8"only-local", {} } };
        context.checked_out_branches = { std::u8string { current } };
        return context;
    }

    gitman::repository_snapshot svn_snapshot()
    {
        gitman::repository_snapshot snapshot {};
        snapshot.availability = gitman::repository_availability::ready;
        snapshot.kind = gitman::repository_kind::subversion;
        snapshot.working_tree.state = gitman::working_tree_state::clean;
        snapshot.svn_repository_root = u8"https://host/svn/repo";
        snapshot.svn_repository_uuid = u8"11111111-2222-3333-4444-555555555555";
        return snapshot;
    }

    constexpr std::u8string_view trunk_url { u8"https://host/svn/repo/trunk" };
    constexpr std::u8string_view branch_url { u8"https://host/svn/repo/branches/x" };

    std::vector<std::u8string> allowed_urls()
    {
        return { std::u8string { trunk_url }, std::u8string { branch_url }, u8"잘못된 값" };
    }
} // namespace

TEST_CASE("Git switch rejects targets that are not on the list", "[application][switch][validation]")
{
    const gitman::git_switch_context context { git_context() };

    // 대상이 비어 있거나 다른 저장소 종류이면 목록을 볼 필요도 없다.
    REQUIRE(gitman::validate_git_switch(git_candidates(), {}, context).rejection == gitman::switch_rejection::target_not_found);
    REQUIRE(gitman::validate_git_switch(git_candidates(), url_candidate(u8"svn://host/x"), context).rejection == gitman::switch_rejection::target_not_found);
    REQUIRE(gitman::validate_git_switch(git_candidates(), remote_candidate(u8"origin", u8"없는-branch"), context).rejection == gitman::switch_rejection::target_not_found);
    REQUIRE(gitman::validate_git_switch(git_candidates(), local_candidate(u8"없는-branch"), context).rejection == gitman::switch_rejection::target_not_found);
    // 목록이 비어 있으면 어떤 대상도 승인하지 않는다.
    REQUIRE(gitman::validate_git_switch({}, local_candidate(u8"only-local"), context).rejection == gitman::switch_rejection::target_not_found);
}

TEST_CASE("Git switch never picks a remote by itself", "[application][switch][validation]")
{
    gitman::switch_candidate target { remote_candidate(u8"origin", u8"main", u8"main") };
    target.remote_name.clear();

    // 같은 branch 이름이 여러 remote에 있을 수 있다. remote를 지정하지 않은 대상은
    // 목록에 있는 것처럼 보여도 자동으로 고르지 않는다.
    const gitman::switch_validation_result result { gitman::validate_git_switch(git_candidates(), target, git_context()) };
    REQUIRE(result.rejection == gitman::switch_rejection::ambiguous_remote);
    REQUIRE_FALSE(result.approved);
}

TEST_CASE("Git switch refuses a repository it cannot read", "[application][switch][validation]")
{
    gitman::git_switch_context context { git_context() };
    context.snapshot.availability = gitman::repository_availability::not_a_repository;
    REQUIRE(gitman::validate_git_switch(git_candidates(), local_candidate(u8"only-local"), context).rejection == gitman::switch_rejection::repository_unavailable);

    context.snapshot.availability = gitman::repository_availability::unsupported_layout;
    REQUIRE(gitman::validate_git_switch(git_candidates(), local_candidate(u8"only-local"), context).rejection == gitman::switch_rejection::repository_unavailable);
}

TEST_CASE("Git switch recognises the target the working tree is already on", "[application][switch][validation]")
{
    const std::vector<gitman::switch_candidate> candidates { git_candidates() };
    const gitman::git_switch_context context { git_context(u8"main") };

    // remote 후보를 골라도 그 upstream을 추적하는 local branch에 이미 있으면 달라지는
    // 것이 없다.
    REQUIRE(gitman::validate_git_switch(candidates, candidates[0], context).rejection == gitman::switch_rejection::already_on_target);
    REQUIRE(gitman::validate_git_switch(candidates, local_candidate(u8"only-local"), git_context(u8"only-local")).rejection == gitman::switch_rejection::already_on_target);

    // detached HEAD에서는 어떤 branch도 현재가 아니다.
    gitman::git_switch_context detached { context };
    detached.snapshot.current_reference = u8"(detached) 0123456";
    detached.snapshot.working_tree.is_detached = true;
    detached.checked_out_branches.clear();
    REQUIRE(gitman::validate_git_switch(candidates, candidates[0], detached).approved);
}

TEST_CASE("Git switch refuses a branch another worktree holds", "[application][switch][validation]")
{
    const std::vector<gitman::switch_candidate> candidates { git_candidates() };
    gitman::git_switch_context context { git_context(u8"only-local") };
    context.checked_out_branches = { u8"only-local", u8"main" };

    REQUIRE(gitman::validate_git_switch(candidates, candidates[0], context).rejection == gitman::switch_rejection::target_in_use);

    // 그 worktree가 branch를 놓아 주면 바로 승인된다.
    context.checked_out_branches = { u8"only-local" };
    REQUIRE(gitman::validate_git_switch(candidates, candidates[0], context).approved);
}

TEST_CASE("Git switch refuses every unsafe working tree", "[application][switch][validation]")
{
    const std::vector<gitman::switch_candidate> candidates { git_candidates() };
    const gitman::git_switch_context base { git_context(u8"only-local") };

    struct expectation
    {
        std::u8string_view name {};
        gitman::working_tree_summary working_tree {};
    };

    std::vector<expectation> expectations {};
    {
        gitman::working_tree_summary working_tree { base.snapshot.working_tree };
        working_tree.state = gitman::working_tree_state::modified;
        expectations.push_back({ u8"dirty", working_tree });
    }
    {
        gitman::working_tree_summary working_tree { base.snapshot.working_tree };
        working_tree.state = gitman::working_tree_state::conflicted;
        expectations.push_back({ u8"충돌", working_tree });
    }
    {
        // 모르는 상태에서 전환하는 편이 더 위험하다.
        gitman::working_tree_summary working_tree { base.snapshot.working_tree };
        working_tree.state = gitman::working_tree_state::unknown;
        expectations.push_back({ u8"미상", working_tree });
    }
    {
        gitman::working_tree_summary working_tree { base.snapshot.working_tree };
        working_tree.operation_in_progress = true;
        expectations.push_back({ u8"진행 중 작업", working_tree });
    }
    {
        gitman::working_tree_summary working_tree { base.snapshot.working_tree };
        working_tree.has_index_lock = true;
        expectations.push_back({ u8"index.lock", working_tree });
    }

    for (const expectation& value : expectations)
    {
        INFO(reinterpret_cast<const char*>(value.name.data()));
        gitman::git_switch_context context { base };
        context.snapshot.working_tree = value.working_tree;
        REQUIRE(gitman::validate_git_switch(candidates, candidates[0], context).rejection == gitman::switch_rejection::working_tree_unsafe);
    }
}

TEST_CASE("Git switch reports a tracking branch that follows another remote", "[application][switch][validation]")
{
    const std::vector<gitman::switch_candidate> candidates { git_candidates() };
    const gitman::git_switch_context context { git_context(u8"only-local") };

    // local `main`은 `origin/main`을 추적한다. `fork/main`을 골라도 그 branch로 옮기는
    // 것뿐이라 사용자가 기대한 원격과 달라진다.
    REQUIRE(gitman::validate_git_switch(candidates, candidates[2], context).rejection == gitman::switch_rejection::tracking_branch_conflict);

    // upstream이 아예 없는 local branch는 충돌이 아니다. 전환이 upstream을 건드리지 않는다.
    const std::vector<gitman::switch_candidate> untracked { remote_candidate(u8"origin", u8"feature", u8"feature") };
    gitman::git_switch_context untracked_context { git_context(u8"main") };
    untracked_context.local_branches = { { u8"feature", {} } };
    REQUIRE(gitman::validate_git_switch(untracked, untracked[0], untracked_context).approved);
}

TEST_CASE("Git switch creates a tracking branch only after confirmation", "[application][switch][validation]")
{
    const std::vector<gitman::switch_candidate> candidates { git_candidates() };
    const gitman::git_switch_context context { git_context(u8"only-local") };

    const gitman::switch_validation_result unconfirmed { gitman::validate_git_switch(candidates, candidates[1], context) };
    REQUIRE_FALSE(unconfirmed.approved);
    REQUIRE(unconfirmed.rejection == gitman::switch_rejection::tracking_branch_confirmation_required);
    // 오류가 아니라 확인 요구다. 카드는 이 표시를 보고 사용자에게 물어본다.
    REQUIRE(unconfirmed.requires_tracking_branch_confirmation);
    REQUIRE(unconfirmed.message.empty() == false);

    gitman::switch_candidate confirmed { candidates[1] };
    confirmed.tracking_branch_confirmed = true;
    const gitman::switch_validation_result approved { gitman::validate_git_switch(candidates, confirmed, context) };
    REQUIRE(approved.approved);
    REQUIRE(approved.rejection == gitman::switch_rejection::none);
    REQUIRE_FALSE(approved.requires_tracking_branch_confirmation);
}

TEST_CASE("Git switch reports the reason to solve first", "[application][switch][validation]")
{
    const std::vector<gitman::switch_candidate> candidates { git_candidates() };
    gitman::switch_candidate target { candidates[1] };
    target.tracking_branch_confirmed = true;

    gitman::git_switch_context context { git_context(u8"only-local") };
    context.snapshot.availability = gitman::repository_availability::unknown;
    context.snapshot.working_tree.state = gitman::working_tree_state::modified;

    // 사용자가 이미 확인했더라도 실제 차단 사유가 있으면 그것을 먼저 알린다.
    REQUIRE(gitman::validate_git_switch(candidates, target, context).rejection == gitman::switch_rejection::repository_unavailable);

    context.snapshot.availability = gitman::repository_availability::ready;
    REQUIRE(gitman::validate_git_switch(candidates, target, context).rejection == gitman::switch_rejection::working_tree_unsafe);

    context.snapshot.working_tree.state = gitman::working_tree_state::clean;
    REQUIRE(gitman::validate_git_switch(candidates, target, context).approved);
}

TEST_CASE("Only known Subversion URL forms are accepted", "[application][switch][validation]")
{
    REQUIRE(gitman::is_supported_svn_url(u8"https://host/svn/repo"));
    REQUIRE(gitman::is_supported_svn_url(u8"http://host/svn/repo"));
    REQUIRE(gitman::is_supported_svn_url(u8"svn://host/repo"));
    REQUIRE(gitman::is_supported_svn_url(u8"svn+ssh://host/repo"));
    REQUIRE(gitman::is_supported_svn_url(u8"file:///C:/repo"));
    REQUIRE(gitman::is_supported_svn_url(u8"https://host/저장소/가지"));

    REQUIRE_FALSE(gitman::is_supported_svn_url({}));
    REQUIRE_FALSE(gitman::is_supported_svn_url(u8"ftp://host/repo"));
    REQUIRE_FALSE(gitman::is_supported_svn_url(u8"C:\\repo"));
    REQUIRE_FALSE(gitman::is_supported_svn_url(u8"https://"));
    REQUIRE_FALSE(gitman::is_supported_svn_url(u8"://host"));
    REQUIRE_FALSE(gitman::is_supported_svn_url(u8"host/repo"));
    // 공백과 제어 문자가 든 값은 인자로 만들기 전에 거른다.
    REQUIRE_FALSE(gitman::is_supported_svn_url(u8"https://host/a b"));
    REQUIRE_FALSE(gitman::is_supported_svn_url(u8"https://host/a\tb"));
}

TEST_CASE("Subversion switch uses only the document allow list", "[application][switch][validation]")
{
    const gitman::repository_snapshot snapshot { svn_snapshot() };

    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), url_candidate(branch_url), snapshot, trunk_url).approved);

    // 저장소 layout을 자동으로 가정하지 않는다. 문서에 없는 URL은 형식이 옳아도 거부한다.
    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), url_candidate(u8"https://host/svn/repo/branches/y"), snapshot, trunk_url).rejection == gitman::switch_rejection::target_not_allowed);
    REQUIRE(gitman::validate_svn_switch_target({}, url_candidate(branch_url), snapshot, trunk_url).rejection == gitman::switch_rejection::target_not_allowed);

    // 목록에 있어도 URL로 다룰 수 없으면 거부한다. 메시지에 값이 들어가 어느 항목인지 알 수 있다.
    const gitman::switch_validation_result malformed { gitman::validate_svn_switch_target(allowed_urls(), url_candidate(u8"잘못된 값"), snapshot, trunk_url) };
    REQUIRE(malformed.rejection == gitman::switch_rejection::target_not_allowed);
    REQUIRE(malformed.message.find(u8"잘못된 값") != std::u8string::npos);

    // Git 후보를 SVN 검증에 넘기면 목록을 볼 필요도 없다.
    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), local_candidate(u8"main"), snapshot, trunk_url).rejection == gitman::switch_rejection::target_not_found);
    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), {}, snapshot, trunk_url).rejection == gitman::switch_rejection::target_not_found);
}

TEST_CASE("Subversion switch checks the working copy before the network", "[application][switch][validation]")
{
    // 이미 그 URL에 있으면 전환할 이유가 없다.
    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), url_candidate(branch_url), svn_snapshot(), branch_url).rejection == gitman::switch_rejection::already_on_target);

    gitman::repository_snapshot dirty { svn_snapshot() };
    dirty.working_tree.state = gitman::working_tree_state::modified;
    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), url_candidate(branch_url), dirty, trunk_url).rejection == gitman::switch_rejection::working_tree_unsafe);

    gitman::repository_snapshot conflicted { svn_snapshot() };
    conflicted.working_tree.state = gitman::working_tree_state::conflicted;
    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), url_candidate(branch_url), conflicted, trunk_url).rejection == gitman::switch_rejection::working_tree_unsafe);

    gitman::repository_snapshot unavailable { svn_snapshot() };
    unavailable.availability = gitman::repository_availability::not_a_repository;
    REQUIRE(gitman::validate_svn_switch_target(allowed_urls(), url_candidate(branch_url), unavailable, trunk_url).rejection == gitman::switch_rejection::repository_unavailable);
}

TEST_CASE("Subversion switch compares both repository identities", "[application][switch][validation]")
{
    const gitman::repository_snapshot snapshot { svn_snapshot() };
    constexpr std::u8string_view root { u8"https://host/svn/repo" };
    constexpr std::u8string_view uuid { u8"11111111-2222-3333-4444-555555555555" };

    REQUIRE(gitman::validate_svn_repository_identity(snapshot, root, uuid).approved);

    // 대상 값을 읽지 못한 것은 접근 불가다. 이 판정을 저장소 불일치로 보고하면 사용자가
    // 할 일이 달라진다.
    REQUIRE(gitman::validate_svn_repository_identity(snapshot, {}, uuid).rejection == gitman::switch_rejection::target_unreachable);
    REQUIRE(gitman::validate_svn_repository_identity(snapshot, root, {}).rejection == gitman::switch_rejection::target_unreachable);

    REQUIRE(gitman::validate_svn_repository_identity(snapshot, u8"https://host/svn/other", uuid).rejection == gitman::switch_rejection::repository_mismatch);
    REQUIRE(gitman::validate_svn_repository_identity(snapshot, root, u8"99999999-2222-3333-4444-555555555555").rejection == gitman::switch_rejection::repository_mismatch);

    // 현재 작업 복사본의 값을 모르면 대조할 수 없다. 전환은 되돌리기 어려우므로 확인하지
    // 못한 것을 안전으로 보지 않는다.
    gitman::repository_snapshot unknown_root { snapshot };
    unknown_root.svn_repository_root.clear();
    REQUIRE(gitman::validate_svn_repository_identity(unknown_root, root, uuid).rejection == gitman::switch_rejection::repository_mismatch);

    gitman::repository_snapshot unknown_uuid { snapshot };
    unknown_uuid.svn_repository_uuid.clear();
    REQUIRE(gitman::validate_svn_repository_identity(unknown_uuid, root, uuid).rejection == gitman::switch_rejection::repository_mismatch);
}
