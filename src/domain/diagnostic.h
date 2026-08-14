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
