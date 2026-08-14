#include "domain/repository_snapshot.h"

namespace gitman {
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
