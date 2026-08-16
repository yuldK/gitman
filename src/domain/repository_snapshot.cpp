#include "domain/repository_snapshot.h"

namespace gitman {
    bool working_tree_summary::is_safe_for_change() const noexcept
    {
        // 보호 정책의 기본값이다. 상태를 아직 조회하지 못한 경우도 안전하다고 보지
        // 않는다. 모르는 상태에서 변경 명령을 실행하는 편이 더 위험하기 때문이다.
        if (state != working_tree_state::clean)
            return false;
        return operation_in_progress == false && has_index_lock == false;
    }

    std::u8string_view repository_kind_name(const repository_kind kind) noexcept
    {
        switch (kind)
        {
        case repository_kind::unknown:
            return u8"unknown";
        case repository_kind::git:
            return u8"git";
        case repository_kind::subversion:
            return u8"svn";
        }
        return u8"unknown";
    }

    std::u8string_view repository_availability_name(const repository_availability availability) noexcept
    {
        switch (availability)
        {
        case repository_availability::unknown:
            return u8"unknown";
        case repository_availability::ready:
            return u8"ready";
        case repository_availability::tool_unavailable:
            return u8"tool_unavailable";
        case repository_availability::not_a_repository:
            return u8"not_a_repository";
        case repository_availability::unsupported_layout:
            return u8"unsupported_layout";
        case repository_availability::path_unavailable:
            return u8"path_unavailable";
        }
        return u8"unknown";
    }

    std::u8string_view comparison_source_name(const comparison_source source) noexcept
    {
        switch (source)
        {
        case comparison_source::none:
            return u8"none";
        case comparison_source::remote:
            return u8"remote";
        case comparison_source::local:
            return u8"local";
        }
        return u8"none";
    }

    std::u8string_view remote_sync_state_name(const remote_sync_state state) noexcept
    {
        switch (state)
        {
        case remote_sync_state::unknown:
            return u8"unknown";
        case remote_sync_state::up_to_date:
            return u8"up_to_date";
        case remote_sync_state::behind:
            return u8"behind";
        case remote_sync_state::ahead:
            return u8"ahead";
        case remote_sync_state::diverged:
            return u8"diverged";
        case remote_sync_state::local_only:
            return u8"local_only";
        case remote_sync_state::remote_target_missing:
            return u8"remote_target_missing";
        case remote_sync_state::authentication_required:
            return u8"authentication_required";
        case remote_sync_state::offline:
            return u8"offline";
        case remote_sync_state::error:
            return u8"error";
        }
        return u8"unknown";
    }

    std::u8string_view working_tree_state_name(const working_tree_state state) noexcept
    {
        switch (state)
        {
        case working_tree_state::unknown:
            return u8"unknown";
        case working_tree_state::clean:
            return u8"clean";
        case working_tree_state::modified:
            return u8"modified";
        case working_tree_state::conflicted:
            return u8"conflicted";
        }
        return u8"unknown";
    }
} // namespace gitman
