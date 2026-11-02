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
        // 기능 버튼 진단이다 (function-buttons-design 3.8). 모두 warning이라 저장을 막지 않는다.
        // 절이나 그 하위 묶음의 모양이 잘못되었다.
        invalid_function_buttons,
        // 버튼 항목 하나가 검증에 실패해 실행과 화면에서 빠진다. 원문은 남는다.
        invalid_function_button,
        // 사용자 변수 하나를 쓸 수 없어 치환에서 빠진다. 원문은 남는다.
        invalid_function_variable,
        // 항목은 그대로 쓰되 알려 둘 것이다 (4번째 card, 알 수 없는 배치, 카드당 상한 등).
        function_button_warning,
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
