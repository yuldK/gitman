#include "application/switch_validation_service.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        constexpr std::u8string_view local_branch_prefix { u8"refs/heads/" };
        constexpr std::u8string_view supported_svn_schemes[] { u8"http", u8"https", u8"svn", u8"svn+ssh", u8"file" };

        switch_validation_result reject(const switch_rejection rejection)
        {
            switch_validation_result result {};
            result.rejection = rejection;
            result.message = std::u8string { switch_rejection_message(rejection) };
            return result;
        }

        switch_validation_result reject(const switch_rejection rejection, std::u8string message)
        {
            switch_validation_result result {};
            result.rejection = rejection;
            result.message = std::move(message);
            return result;
        }

        switch_validation_result approve()
        {
            switch_validation_result result {};
            result.approved = true;
            result.message = std::u8string { switch_rejection_message(switch_rejection::none) };
            return result;
        }

        bool contains(const std::vector<std::u8string>& values, const std::u8string_view value) noexcept
        {
            return std::ranges::find(values, value) != values.end();
        }

        // 후보를 고르는 값은 종류, 완전한 ref와 remote 이름뿐이다. 표시 이름과 tracking
        // 정보는 조회 시점의 값이라 검증에서 다시 만든 목록의 값을 써야 한다.
        const switch_candidate* find_candidate(const std::vector<switch_candidate>& candidates, const switch_candidate& target) noexcept
        {
            for (const switch_candidate& candidate : candidates)
                if (candidate.kind == target.kind && candidate.target == target.target && candidate.remote_name == target.remote_name)
                    return &candidate;
            return nullptr;
        }

        // 전환하면 checkout될 local branch 이름이다. remote 후보는 대응하는 local branch가
        // 없으면 비어 있고, 그 경우에만 tracking branch를 새로 만든다.
        std::u8string_view effective_local_branch(const switch_candidate& candidate) noexcept
        {
            if (candidate.kind == switch_candidate_kind::git_local_branch && candidate.target.starts_with(local_branch_prefix))
                return std::u8string_view { candidate.target }.substr(local_branch_prefix.size());
            return candidate.local_branch;
        }

        const git_local_branch_state* find_local_branch(const std::vector<git_local_branch_state>& branches, const std::u8string_view name) noexcept
        {
            for (const git_local_branch_state& branch : branches)
                if (branch.name == name)
                    return &branch;
            return nullptr;
        }
    } // namespace

    switch_validation_result validate_git_switch(const std::vector<switch_candidate>& candidates, const switch_candidate& target, const git_switch_context& context)
    {
        if (target.kind == switch_candidate_kind::subversion_url || target.target.empty())
            return reject(switch_rejection::target_not_found);
        if (target.kind == switch_candidate_kind::git_remote_branch && target.remote_name.empty())
            // 같은 branch 이름이 여러 remote에 있을 수 있다. remote를 지정하지 않은
            // 대상은 자동으로 고르지 않는다.
            return reject(switch_rejection::ambiguous_remote);
        if (context.snapshot.availability != repository_availability::ready)
            return reject(switch_rejection::repository_unavailable);

        const switch_candidate* const current { find_candidate(candidates, target) };
        if (current == nullptr)
            return reject(switch_rejection::target_not_found);

        const std::u8string_view branch { effective_local_branch(*current) };
        const git_local_branch_state* const local { branch.empty() ? nullptr : find_local_branch(context.local_branches, branch) };
        // upstream이 없는 local branch는 충돌이 아니다. 이 경우 전환은 upstream을 건드리지
        // 않고 그 branch로 옮기기만 한다.
        const bool upstream_matches { current->kind != switch_candidate_kind::git_remote_branch || local == nullptr || local->upstream.empty() || local->upstream == current->target };
        const bool on_branch { context.snapshot.working_tree.is_detached == false && branch.empty() == false && context.snapshot.current_reference == branch };

        if (on_branch && upstream_matches)
            return reject(switch_rejection::already_on_target);
        if (on_branch == false && branch.empty() == false && contains(context.checked_out_branches, branch))
            // 현재 worktree가 쓰는 branch는 위에서 걸러졌다. 여기 남는 것은 다른
            // worktree가 잡고 있는 branch뿐이며 Git 자체도 전환을 거부한다.
            return reject(switch_rejection::target_in_use);
        if (context.snapshot.working_tree.is_safe_for_change() == false)
            return reject(switch_rejection::working_tree_unsafe);
        if (upstream_matches == false)
            return reject(switch_rejection::tracking_branch_conflict);

        if (current->kind == switch_candidate_kind::git_remote_branch && branch.empty() && target.tracking_branch_confirmed == false)
        {
            // 오류가 아니라 확인 요구다. 사용자가 확인하면 같은 후보에
            // `tracking_branch_confirmed`를 켜서 다시 호출한다.
            switch_validation_result result { reject(switch_rejection::tracking_branch_confirmation_required) };
            result.requires_tracking_branch_confirmation = true;
            return result;
        }
        return approve();
    }

    bool is_supported_svn_url(const std::u8string_view value) noexcept
    {
        const std::size_t separator { value.find(u8"://") };
        if (separator == std::u8string_view::npos || separator == 0)
            return false;
        if (value.size() <= separator + 3)
            return false;
        for (const char8_t character : value)
        {
            // 공백과 제어 문자가 든 값은 URL로 다루지 않는다. 인자로 만들기 전에 거른다.
            if (character <= u8' ')
                return false;
        }

        const std::u8string_view scheme { value.substr(0, separator) };
        return std::ranges::find(supported_svn_schemes, scheme) != std::ranges::end(supported_svn_schemes);
    }

    switch_validation_result validate_svn_switch_target(const switch_candidate& target, const repository_snapshot& snapshot, const std::u8string_view current_url)
    {
        if (target.kind != switch_candidate_kind::subversion_url || target.target.empty())
            return reject(switch_rejection::target_not_found);
        if (is_supported_svn_url(target.target) == false)
        {
            std::u8string message { u8"지원하지 않는 SVN URL 형식입니다: " };
            message.append(target.target);
            return reject(switch_rejection::target_not_allowed, std::move(message));
        }
        if (snapshot.availability != repository_availability::ready)
            return reject(switch_rejection::repository_unavailable);
        if (current_url.empty() == false && current_url == target.target)
            return reject(switch_rejection::already_on_target);
        if (snapshot.working_tree.is_safe_for_change() == false)
            return reject(switch_rejection::working_tree_unsafe);
        return approve();
    }

    switch_validation_result validate_svn_repository_identity(const repository_snapshot& snapshot, const std::u8string_view target_repository_root, const std::u8string_view target_repository_uuid)
    {
        if (target_repository_root.empty() || target_repository_uuid.empty())
            return reject(switch_rejection::target_unreachable);
        if (snapshot.svn_repository_root.empty() || snapshot.svn_repository_uuid.empty())
            return reject(switch_rejection::repository_mismatch, std::u8string { u8"현재 작업 복사본의 저장소 root 또는 UUID를 확인하지 못해 전환 대상과 대조할 수 없습니다." });
        if (snapshot.svn_repository_root != target_repository_root || snapshot.svn_repository_uuid != target_repository_uuid)
            return reject(switch_rejection::repository_mismatch);
        return approve();
    }
} // namespace gitman
