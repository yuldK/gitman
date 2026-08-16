#include "infrastructure/workspace_document_paths.h"

#include <array>
#include <charconv>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gitman {
    namespace {
        void append_ascii_number(std::u8string& target, const std::size_t value)
        {
            std::array<char, std::numeric_limits<std::size_t>::digits10 + 2> buffer {};
            const auto result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), value) };
            for (const char* current = buffer.data(); current != result.ptr; ++current)
                target.push_back(static_cast<char8_t>(*current));
        }

        std::u8string project_path_pointer(const std::size_t project_index)
        {
            std::u8string pointer { u8"/projects/" };
            append_ascii_number(pointer, project_index);
            pointer.append(u8"/path");
            return pointer;
        }

        void add_path_diagnostic(workspace_document_parse_result& result, const diagnostic_code code, const diagnostic_severity severity, const std::u8string_view message,
            const std::u8string_view document_path, const std::size_t project_index, const std::u8string_view project_id, const std::optional<std::uint32_t> native_error = std::nullopt)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = message;
            value.source.document_path = document_path;
            value.source.json_pointer = project_path_pointer(project_index);
            value.source.project_index = project_index;
            value.source.project_id = project_id;
            value.native_error = native_error;
            result.diagnostics.push_back(std::move(value));
        }

        bool contains_normalized_path(project_path_resolver& path_resolver, const std::vector<std::u8string>& paths, const std::u8string_view candidate) noexcept
        {
            for (const std::u8string& path : paths)
                if (path_resolver.normalized_equal(path, candidate))
                    return true;
            return false;
        }

        void add_state_diagnostic(workspace_document_parse_result& result, const project_definition& project, const std::size_t source_index, const std::u8string_view document_path,
            const project_path_resolution& resolution)
        {
            switch (resolution.state)
            {
            case configured_path_state::available:
                return;
            case configured_path_state::missing:
                add_path_diagnostic(
                    result, diagnostic_code::path_missing, diagnostic_severity::warning, u8"project 경로가 존재하지 않습니다.", document_path, source_index, project.id.value, resolution.native_error);
                return;
            case configured_path_state::inaccessible:
                add_path_diagnostic(result, diagnostic_code::path_inaccessible, diagnostic_severity::warning, u8"project 경로에 접근할 수 없습니다.", document_path, source_index, project.id.value,
                    resolution.native_error);
                return;
            case configured_path_state::not_directory:
                add_path_diagnostic(result, diagnostic_code::path_not_directory, diagnostic_severity::warning, u8"project 경로가 디렉터리가 아닙니다.", document_path, source_index, project.id.value,
                    resolution.native_error);
                return;
            case configured_path_state::unchecked:
            case configured_path_state::invalid:
                return;
            }
        }
    } // namespace

    void resolve_workspace_document_paths(workspace_document_parse_result& result, project_path_resolver& path_resolver)
    {
        if (result.document.has_value() == false)
            return;

        workspace_document& document { *result.document };
        const bool has_source_indices { result.shadow.project_source_indices.size() == document.projects.size() };
        std::vector<project_definition> retained_projects {};
        std::vector<std::size_t> retained_source_indices {};
        std::vector<std::u8string> normalized_paths {};
        retained_projects.reserve(document.projects.size());
        retained_source_indices.reserve(document.projects.size());
        normalized_paths.reserve(document.projects.size());

        for (std::size_t project_position = 0; project_position < document.projects.size(); ++project_position)
        {
            project_definition project { std::move(document.projects[project_position]) };
            const std::size_t source_index { has_source_indices ? result.shadow.project_source_indices[project_position] : project_position };
            project_path_resolution resolution { path_resolver.resolve(project.path.original, document.document_path) };
            project.path.normalized = std::move(resolution.normalized);
            project.path.state = resolution.state;

            if (resolution.state == configured_path_state::invalid)
            {
                add_path_diagnostic(result, diagnostic_code::invalid_project_path, diagnostic_severity::error, u8"project path 또는 document 기준 경로가 유효하지 않습니다.", document.document_path,
                    source_index, project.id.value, resolution.native_error);
                continue;
            }
            if (contains_normalized_path(path_resolver, normalized_paths, project.path.normalized))
            {
                add_path_diagnostic(
                    result, diagnostic_code::duplicate_project_path, diagnostic_severity::error, u8"중복된 project 경로입니다.", document.document_path, source_index, project.id.value);
                continue;
            }

            normalized_paths.push_back(project.path.normalized);
            add_state_diagnostic(result, project, source_index, document.document_path, resolution);
            retained_source_indices.push_back(source_index);
            retained_projects.push_back(std::move(project));
        }

        document.projects = std::move(retained_projects);
        result.shadow.project_source_indices = std::move(retained_source_indices);
    }
} // namespace gitman
