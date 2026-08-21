#include "domain/project.h"

namespace gitman {
    bool workspace_settings::is_default() const noexcept
    {
        return git_executable.empty() && svn_executable.empty() && show_relative_paths == false && update_submodules == false && ignore_local_changes == false && write_log_files
            && query_timeout_seconds.has_value() == false;
    }

    bool workspace_settings_overrides::empty() const noexcept
    {
        return git_executable.has_value() == false && svn_executable.has_value() == false && show_relative_paths.has_value() == false && update_submodules.has_value() == false
            && ignore_local_changes.has_value() == false && write_log_files.has_value() == false && query_timeout_seconds.has_value() == false;
    }

    workspace_settings apply_overrides(const workspace_settings& base, const workspace_settings_overrides& overrides)
    {
        workspace_settings result { base };
        if (overrides.git_executable.has_value())
            result.git_executable = *overrides.git_executable;
        if (overrides.svn_executable.has_value())
            result.svn_executable = *overrides.svn_executable;
        if (overrides.show_relative_paths.has_value())
            result.show_relative_paths = *overrides.show_relative_paths;
        if (overrides.update_submodules.has_value())
            result.update_submodules = *overrides.update_submodules;
        if (overrides.ignore_local_changes.has_value())
            result.ignore_local_changes = *overrides.ignore_local_changes;
        if (overrides.write_log_files.has_value())
            result.write_log_files = *overrides.write_log_files;
        if (overrides.query_timeout_seconds.has_value())
            result.query_timeout_seconds = { *overrides.query_timeout_seconds };
        return result;
    }

    bool window_placement::valid() const noexcept
    {
        return width > 0 && height > 0;
    }

    std::optional<vcs_hint> parse_vcs_hint(const std::u8string_view value) noexcept
    {
        if (value == u8"auto")
            return vcs_hint::automatic;
        if (value == u8"git")
            return vcs_hint::git;
        if (value == u8"svn")
            return vcs_hint::subversion;
        return std::nullopt;
    }

    std::u8string_view vcs_hint_name(const vcs_hint hint) noexcept
    {
        switch (hint)
        {
        case vcs_hint::automatic:
            return u8"auto";
        case vcs_hint::git:
            return u8"git";
        case vcs_hint::subversion:
            return u8"svn";
        }
        return u8"unknown";
    }

    std::u8string_view configured_path_state_name(const configured_path_state state) noexcept
    {
        switch (state)
        {
        case configured_path_state::unchecked:
            return u8"unchecked";
        case configured_path_state::available:
            return u8"available";
        case configured_path_state::missing:
            return u8"missing";
        case configured_path_state::inaccessible:
            return u8"inaccessible";
        case configured_path_state::not_directory:
            return u8"not_directory";
        case configured_path_state::invalid:
            return u8"invalid";
        }
        return u8"invalid";
    }
} // namespace gitman
