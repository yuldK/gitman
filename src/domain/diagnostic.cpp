#include "domain/diagnostic.h"

namespace gitman {
    std::u8string_view diagnostic_severity_name(const diagnostic_severity severity) noexcept
    {
        switch (severity)
        {
        case diagnostic_severity::information:
            return u8"information";
        case diagnostic_severity::warning:
            return u8"warning";
        case diagnostic_severity::error:
            return u8"error";
        }
        return u8"unknown";
    }

    std::u8string_view diagnostic_code_name(const diagnostic_code code) noexcept
    {
        switch (code)
        {
        case diagnostic_code::unknown:
            return u8"unknown";
        case diagnostic_code::malformed_document:
            return u8"malformed_document";
        case diagnostic_code::invalid_document_root:
            return u8"invalid_document_root";
        case diagnostic_code::missing_schema_version:
            return u8"missing_schema_version";
        case diagnostic_code::invalid_schema_version:
            return u8"invalid_schema_version";
        case diagnostic_code::unsupported_legacy_schema:
            return u8"unsupported_legacy_schema";
        case diagnostic_code::unsupported_future_schema:
            return u8"unsupported_future_schema";
        case diagnostic_code::missing_projects:
            return u8"missing_projects";
        case diagnostic_code::invalid_projects:
            return u8"invalid_projects";
        case diagnostic_code::missing_project_field:
            return u8"missing_project_field";
        case diagnostic_code::invalid_project_field:
            return u8"invalid_project_field";
        case diagnostic_code::invalid_project_id:
            return u8"invalid_project_id";
        case diagnostic_code::duplicate_project_id:
            return u8"duplicate_project_id";
        case diagnostic_code::invalid_project_path:
            return u8"invalid_project_path";
        case diagnostic_code::duplicate_project_path:
            return u8"duplicate_project_path";
        case diagnostic_code::invalid_vcs_hint:
            return u8"invalid_vcs_hint";
        case diagnostic_code::unknown_field:
            return u8"unknown_field";
        case diagnostic_code::path_missing:
            return u8"path_missing";
        case diagnostic_code::path_inaccessible:
            return u8"path_inaccessible";
        case diagnostic_code::path_not_directory:
            return u8"path_not_directory";
        case diagnostic_code::document_not_found:
            return u8"document_not_found";
        case diagnostic_code::document_read_failed:
            return u8"document_read_failed";
        case diagnostic_code::document_write_failed:
            return u8"document_write_failed";
        case diagnostic_code::document_flush_failed:
            return u8"document_flush_failed";
        case diagnostic_code::document_replace_failed:
            return u8"document_replace_failed";
        case diagnostic_code::concurrent_modification:
            return u8"concurrent_modification";
        case diagnostic_code::backup_invalid:
            return u8"backup_invalid";
        case diagnostic_code::recovery_available:
            return u8"recovery_available";
        case diagnostic_code::repository_unavailable:
            return u8"repository_unavailable";
        case diagnostic_code::operation_failed:
            return u8"operation_failed";
        case diagnostic_code::invalid_process_request:
            return u8"invalid_process_request";
        case diagnostic_code::process_start_failed:
            return u8"process_start_failed";
        case diagnostic_code::process_pipe_failed:
            return u8"process_pipe_failed";
        case diagnostic_code::process_timed_out:
            return u8"process_timed_out";
        case diagnostic_code::process_cancelled:
            return u8"process_cancelled";
        case diagnostic_code::process_output_truncated:
            return u8"process_output_truncated";
        case diagnostic_code::vcs_tool_not_found:
            return u8"vcs_tool_not_found";
        case diagnostic_code::vcs_tool_too_old:
            return u8"vcs_tool_too_old";
        case diagnostic_code::vcs_tool_version_unreadable:
            return u8"vcs_tool_version_unreadable";
        case diagnostic_code::vcs_tool_path_invalid:
            return u8"vcs_tool_path_invalid";
        case diagnostic_code::vcs_command_failed:
            return u8"vcs_command_failed";
        case diagnostic_code::vcs_output_unparsable:
            return u8"vcs_output_unparsable";
        case diagnostic_code::authentication_required:
            return u8"authentication_required";
        case diagnostic_code::remote_unreachable:
            return u8"remote_unreachable";
        case diagnostic_code::repository_not_found:
            return u8"repository_not_found";
        case diagnostic_code::update_blocked:
            return u8"update_blocked";
        case diagnostic_code::switch_target_rejected:
            return u8"switch_target_rejected";
        case diagnostic_code::discovery_root_unavailable:
            return u8"discovery_root_unavailable";
        case diagnostic_code::discovery_child_skipped:
            return u8"discovery_child_skipped";
        case diagnostic_code::discovery_cancelled:
            return u8"discovery_cancelled";
        }
        return u8"unknown";
    }
} // namespace gitman
