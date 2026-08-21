#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gitman {
    enum class diagnostic_severity
    {
        information,
        warning,
        error,
    };

    enum class diagnostic_code
    {
        unknown,
        malformed_document,
        invalid_document_root,
        missing_schema_version,
        invalid_schema_version,
        unsupported_legacy_schema,
        unsupported_future_schema,
        missing_projects,
        invalid_projects,
        missing_project_field,
        invalid_project_field,
        invalid_project_id,
        duplicate_project_id,
        invalid_project_path,
        duplicate_project_path,
        invalid_vcs_hint,
        invalid_window_placement,
        unknown_field,
        path_missing,
        path_inaccessible,
        path_not_directory,
        document_not_found,
        document_read_failed,
        document_write_failed,
        document_flush_failed,
        document_replace_failed,
        concurrent_modification,
        backup_invalid,
        recovery_available,
        repository_unavailable,
        operation_failed,
        invalid_process_request,
        process_start_failed,
        process_pipe_failed,
        process_timed_out,
        process_cancelled,
        process_output_truncated,
        vcs_tool_not_found,
        vcs_tool_too_old,
        vcs_tool_version_unreadable,
        vcs_tool_path_invalid,
        vcs_command_failed,
        vcs_output_unparsable,
        authentication_required,
        remote_unreachable,
        repository_not_found,
        update_blocked,
        switch_target_rejected,
        discovery_root_unavailable,
        discovery_child_skipped,
        discovery_cancelled,
        registration_candidate_rejected,
        generation_request_invalid,
        generation_output_exists,
        generation_no_repositories,
        generation_failed,
        app_settings_read_failed,
        app_settings_invalid,
        app_settings_write_failed,
    };

    struct diagnostic_source
    {
        std::u8string document_path {};
        std::u8string json_pointer {};
        std::optional<std::size_t> project_index {};
        std::optional<std::u8string> project_id {};
    };

    struct diagnostic
    {
        diagnostic_code code { diagnostic_code::unknown };
        diagnostic_severity severity { diagnostic_severity::error };
        std::u8string message {};
        diagnostic_source source {};
        std::optional<std::uint32_t> native_error {};
    };

    [[nodiscard]] std::u8string_view diagnostic_severity_name(diagnostic_severity severity) noexcept;
    [[nodiscard]] std::u8string_view diagnostic_code_name(diagnostic_code code) noexcept;
} // namespace gitman
