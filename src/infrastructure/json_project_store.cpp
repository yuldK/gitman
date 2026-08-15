#include "infrastructure/json_project_store.h"

#include "infrastructure/json_workspace_document.h"
#include "infrastructure/workspace_document_paths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gitman {
    namespace {
        using json = nlohmann::ordered_json;

        struct serialized_workspace_document
        {
            std::u8string bytes {};
            workspace_document_parse_result validation {};
        };

        std::string_view as_bytes(const std::u8string_view value) noexcept
        {
            return {
                reinterpret_cast<const char*>(value.data()),
                value.size(),
            };
        }

        std::string as_string(const std::u8string_view value)
        {
            const std::string_view bytes { as_bytes(value) };
            return std::string { bytes };
        }

        std::u8string default_display_name(const std::u8string_view path)
        {
            std::size_t end { path.size() };
            while (end > 0 && (path[end - 1] == u8'/' || path[end - 1] == u8'\\'))
                --end;
            if (end == 0)
                return std::u8string { path };

            const std::size_t separator { path.find_last_of(u8"/\\", end - 1) };
            if (separator == std::u8string_view::npos)
                return std::u8string { path.substr(0, end) };
            return std::u8string { path.substr(separator + 1, end - separator - 1) };
        }

        void write_optional_project_fields(json& value, const project_definition& project)
        {
            const bool had_display_name { value.contains("display_name") };
            const bool had_vcs_hint { value.contains("vcs_hint") };
            const bool had_enabled { value.contains("enabled") };
            const bool had_preferred_remote { value.contains("preferred_remote") };
            const bool had_svn_switch_targets { value.contains("svn_switch_targets") };

            if (had_display_name || project.display_name != default_display_name(project.path.original))
                value["display_name"] = as_string(project.display_name);
            else
                value.erase("display_name");

            if (had_vcs_hint || project.hint != vcs_hint::automatic)
                value["vcs_hint"] = as_string(vcs_hint_name(project.hint));
            else
                value.erase("vcs_hint");

            if (had_enabled || project.enabled == false)
                value["enabled"] = project.enabled;
            else
                value.erase("enabled");

            if (project.preferred_remote.has_value())
                value["preferred_remote"] = as_string(*project.preferred_remote);
            else if (had_preferred_remote)
                value["preferred_remote"] = nullptr;
            else
                value.erase("preferred_remote");

            if (had_svn_switch_targets || project.svn_switch_targets.empty() == false)
            {
                json targets { json::array() };
                for (const std::u8string& target : project.svn_switch_targets)
                    targets.push_back(as_string(target));
                value["svn_switch_targets"] = std::move(targets);
            }
            else
                value.erase("svn_switch_targets");
        }

        json project_json(json value, const project_definition& project)
        {
            if (value.is_object() == false)
                value = json::object();

            value["id"] = as_string(project.id.value);
            value["path"] = as_string(project.path.original);
            write_optional_project_fields(value, project);
            return value;
        }

        std::unordered_map<std::string, json> project_templates(const json& root, const std::span<const std::size_t> source_indices)
        {
            std::unordered_map<std::string, json> templates {};
            const auto projects { root.find("projects") };
            if (projects == root.end() || projects->is_array() == false)
                return templates;

            for (const std::size_t source_index : source_indices)
            {
                if (source_index >= projects->size())
                    continue;
                const json& source { (*projects)[source_index] };
                if (source.is_object() == false)
                    continue;
                const auto id { source.find("id") };
                if (id == source.end() || id->is_string() == false)
                    continue;
                templates.try_emplace(id->get_ref<const std::string&>(), source);
            }
            return templates;
        }

        json shadow_root(const std::u8string_view shadow_source_json)
        {
            if (shadow_source_json.empty())
                return json::object();

            const std::string_view source_bytes { as_bytes(shadow_source_json) };
            json root { json::parse(source_bytes.begin(), source_bytes.end(), nullptr, false) };
            if (root.is_discarded() || root.is_object() == false)
                return json::object();
            return root;
        }

        std::u8string format_json_bytes(const std::string& dumped)
        {
            std::u8string result {};
            result.reserve(dumped.size() + (dumped.size() / 16) + 2);
            for (const char value : dumped)
            {
                if (value == '\n')
                    result.push_back(u8'\r');
                result.push_back(static_cast<char8_t>(value));
            }
            result.append(u8"\r\n");
            return result;
        }

        serialized_workspace_document serialize_workspace_document(
            const workspace_document& document, const std::u8string_view document_path, const std::u8string_view shadow_source_json, const std::span<const std::size_t> project_source_indices)
        {
            json root { shadow_root(shadow_source_json) };
            std::unordered_map<std::string, json> templates { project_templates(root, project_source_indices) };
            json projects { json::array() };
            for (const project_definition& project : document.projects)
            {
                const std::string id { as_string(project.id.value) };
                const auto existing { templates.find(id) };
                projects.push_back(project_json(existing == templates.end() ? json::object() : existing->second, project));
            }

            root["schema_version"] = document.schema_version;
            root["projects"] = std::move(projects);

            serialized_workspace_document result {};
            result.bytes = format_json_bytes(root.dump(4, ' ', false, json::error_handler_t::strict));
            result.validation = parse_workspace_document_json(result.bytes, document_path);
            resolve_workspace_document_paths(result.validation);
            return result;
        }

        diagnostic make_diagnostic(const diagnostic_code code, const diagnostic_severity severity, const std::u8string_view message, const std::u8string_view document_path,
            const std::optional<std::uint32_t> native_error = std::nullopt)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = message;
            value.source.document_path = document_path;
            value.native_error = native_error;
            return value;
        }

        void append_diagnostics(std::vector<diagnostic>& target, std::vector<diagnostic> source)
        {
            target.insert(target.end(), std::make_move_iterator(source.begin()), std::make_move_iterator(source.end()));
        }

        workspace_document_parse_result parse_and_resolve(const std::u8string_view bytes, const std::u8string_view document_path)
        {
            workspace_document_parse_result result { parse_workspace_document_json(bytes, document_path) };
            resolve_workspace_document_paths(result);
            return result;
        }

        bool is_valid_recovery_document(const workspace_document_parse_result& result) noexcept
        {
            return result.document.has_value() && result.has_errors() == false;
        }

        diagnostic_code commit_diagnostic_code(const workspace_file_commit_failure failure) noexcept
        {
            switch (failure)
            {
            case workspace_file_commit_failure::write:
                return diagnostic_code::document_write_failed;
            case workspace_file_commit_failure::flush:
                return diagnostic_code::document_flush_failed;
            case workspace_file_commit_failure::replace:
                return diagnostic_code::document_replace_failed;
            case workspace_file_commit_failure::none:
                return diagnostic_code::unknown;
            }
            return diagnostic_code::unknown;
        }

        std::u8string_view commit_failure_message(const workspace_file_commit_failure failure) noexcept
        {
            switch (failure)
            {
            case workspace_file_commit_failure::write:
                return u8"작업공간 문서 임시 파일을 쓰지 못했습니다.";
            case workspace_file_commit_failure::flush:
                return u8"작업공간 문서 임시 파일을 디스크에 반영하지 못했습니다.";
            case workspace_file_commit_failure::replace:
                return u8"작업공간 문서 원본을 교체하지 못했습니다.";
            case workspace_file_commit_failure::none:
                return u8"작업공간 문서 저장에 실패했습니다.";
            }
            return u8"작업공간 문서 저장에 실패했습니다.";
        }
    } // namespace

    std::u8string workspace_document_backup_path(const std::u8string_view document_path)
    {
        std::u8string result { document_path };
        result.append(u8".bak");
        return result;
    }

    json_project_store::json_project_store(workspace_document_file_system& file_system) noexcept
        : file_system_ { file_system }
    {}

    project_store_load_result json_project_store::load(const std::u8string_view document_path) noexcept
    {
        try
        {
            return load_impl(document_path);
        }
        catch (...)
        {
            project_store_load_result result {};
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::document_read_failed, diagnostic_severity::error, u8"작업공간 문서를 읽는 중 예기치 않은 오류가 발생했습니다.", document_path));
            return result;
        }
    }

    project_store_load_result json_project_store::load_impl(const std::u8string_view document_path)
    {
        project_store_load_result result {};
        workspace_file_read_result source { file_system_.read(document_path) };
        if (source.state == workspace_file_read_state::not_found)
        {
            result.revision = make_revision_token(revision_file_state::missing, std::u8string { document_path }, {}, {}, {});
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::document_not_found, diagnostic_severity::error, u8"작업공간 문서를 찾을 수 없습니다.", document_path, source.native_error));
            append_recovery_diagnostic(document_path, result);
            return result;
        }
        if (source.state == workspace_file_read_state::failed)
        {
            result.revision = make_revision_token(revision_file_state::unavailable, std::u8string { document_path }, {}, {}, {});
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::document_read_failed, diagnostic_severity::error, u8"작업공간 문서를 읽지 못했습니다.", document_path, source.native_error));
            append_recovery_diagnostic(document_path, result);
            return result;
        }

        workspace_document_parse_result parsed { parse_and_resolve(source.bytes, document_path) };
        result.revision = make_revision_token(revision_file_state::present, std::u8string { document_path }, source.bytes, parsed.shadow.source_json, parsed.shadow.project_source_indices);
        result.document = std::move(parsed.document);
        result.diagnostics = std::move(parsed.diagnostics);
        if (result.document.has_value() == false)
            append_recovery_diagnostic(document_path, result);
        return result;
    }

    void json_project_store::append_recovery_diagnostic(const std::u8string_view document_path, project_store_load_result& result)
    {
        const std::u8string backup_path { workspace_document_backup_path(document_path) };
        workspace_file_read_result backup { file_system_.read(backup_path) };
        if (backup.state == workspace_file_read_state::not_found)
            return;
        if (backup.state == workspace_file_read_state::failed)
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::backup_invalid, diagnostic_severity::warning, u8"backup 작업공간 문서를 읽을 수 없습니다.", backup_path, backup.native_error));
            return;
        }

        const workspace_document_parse_result parsed { parse_and_resolve(backup.bytes, backup_path) };
        if (is_valid_recovery_document(parsed))
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::recovery_available, diagnostic_severity::warning, u8"유효한 backup이 있습니다. 명시적으로 열고 저장해야 복구됩니다.", backup_path));
            return;
        }
        result.diagnostics.push_back(make_diagnostic(diagnostic_code::backup_invalid, diagnostic_severity::warning, u8"backup 작업공간 문서가 유효하지 않습니다.", backup_path));
    }

    project_store_load_result json_project_store::load_backup(const std::u8string_view document_path) noexcept
    {
        try
        {
            return load_backup_impl(document_path);
        }
        catch (...)
        {
            project_store_load_result result {};
            result.source = workspace_document_source::backup;
            const std::u8string backup_path { workspace_document_backup_path(document_path) };
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::backup_invalid, diagnostic_severity::error, u8"backup 작업공간 문서를 읽는 중 예기치 않은 오류가 발생했습니다.", backup_path));
            return result;
        }
    }

    project_store_load_result json_project_store::load_backup_impl(const std::u8string_view document_path)
    {
        project_store_load_result result {};
        result.source = workspace_document_source::backup;

        revision_file_state primary_state { revision_file_state::unavailable };
        std::u8string primary_bytes {};
        const workspace_file_read_result primary { file_system_.read(document_path) };
        if (primary.state == workspace_file_read_state::available)
        {
            primary_state = revision_file_state::present;
            primary_bytes = primary.bytes;
        }
        else if (primary.state == workspace_file_read_state::not_found)
            primary_state = revision_file_state::missing;
        else
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::document_read_failed, diagnostic_severity::error, u8"복구 저장에 필요한 primary revision을 읽지 못했습니다.", document_path, primary.native_error));
        }

        const std::u8string backup_path { workspace_document_backup_path(document_path) };
        workspace_file_read_result backup { file_system_.read(backup_path) };
        if (backup.state != workspace_file_read_state::available)
        {
            result.revision = make_revision_token(primary_state, std::u8string { document_path }, std::move(primary_bytes), {}, {});
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::backup_invalid, diagnostic_severity::error,
                backup.state == workspace_file_read_state::not_found ? u8"backup 작업공간 문서를 찾을 수 없습니다." : u8"backup 작업공간 문서를 읽지 못했습니다.", backup_path, backup.native_error));
            return result;
        }

        workspace_document_parse_result parsed { parse_and_resolve(backup.bytes, backup_path) };
        if (is_valid_recovery_document(parsed) == false)
        {
            append_diagnostics(result.diagnostics, std::move(parsed.diagnostics));
            result.revision = make_revision_token(primary_state, std::u8string { document_path }, std::move(primary_bytes), {}, {});
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::backup_invalid, diagnostic_severity::error, u8"backup 작업공간 문서가 유효하지 않아 복구할 수 없습니다.", backup_path));
            return result;
        }

        parsed.document->document_path = document_path;
        result.revision = make_revision_token(primary_state, std::u8string { document_path }, std::move(primary_bytes), backup.bytes, parsed.shadow.project_source_indices);
        result.document = std::move(parsed.document);
        append_diagnostics(result.diagnostics, std::move(parsed.diagnostics));
        return result;
    }

    project_store_save_result json_project_store::save(const std::u8string_view document_path, const workspace_document& document, const workspace_revision_token& expected_revision) noexcept
    {
        try
        {
            return save_impl(document_path, document, expected_revision);
        }
        catch (...)
        {
            project_store_save_result result {};
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::document_write_failed, diagnostic_severity::error, u8"작업공간 문서를 저장하는 중 예기치 않은 오류가 발생했습니다.", document_path));
            return result;
        }
    }

    project_store_save_result json_project_store::save_impl(const std::u8string_view document_path, const workspace_document& document, const workspace_revision_token& expected_revision)
    {
        project_store_save_result result {};
        const std::optional<revision_view> revision { inspect_revision(expected_revision) };
        if (revision.has_value() == false || revision->document_path != document_path)
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::concurrent_modification, diagnostic_severity::error, u8"이 작업공간 문서에 대응하는 revision token이 없어 저장하지 않았습니다.", document_path));
            return result;
        }

        serialized_workspace_document serialized {
            serialize_workspace_document(document, document_path, revision->shadow_source_json, revision->project_source_indices),
        };
        append_diagnostics(result.diagnostics, std::move(serialized.validation.diagnostics));
        if (serialized.validation.document.has_value() == false || result.has_errors())
            return result;

        workspace_revision_token saved_revision {
            make_revision_token(revision_file_state::present, std::u8string { document_path }, serialized.bytes, serialized.bytes, std::move(serialized.validation.shadow.project_source_indices)),
        };

        workspace_file_read_result current { file_system_.read(document_path) };
        if (current.state == workspace_file_read_state::failed)
        {
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::document_read_failed, diagnostic_severity::error, u8"저장 직전 primary revision을 읽지 못해 원본을 보존했습니다.", document_path, current.native_error));
            return result;
        }

        bool revision_matches { false };
        switch (revision->file_state)
        {
        case revision_file_state::missing:
            revision_matches = current.state == workspace_file_read_state::not_found;
            break;
        case revision_file_state::present:
            revision_matches = current.state == workspace_file_read_state::available && current.bytes == revision->expected_source_bytes;
            break;
        case revision_file_state::unavailable:
            revision_matches = false;
            break;
        }
        if (revision_matches == false)
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::concurrent_modification, diagnostic_severity::error, u8"load 이후 작업공간 문서가 변경되어 저장하지 않았습니다.", document_path));
            return result;
        }

        const std::u8string backup_path { workspace_document_backup_path(document_path) };
        const bool replace_existing { revision->file_state == revision_file_state::present };
        const workspace_file_commit_result commit { file_system_.atomic_commit(document_path, backup_path, serialized.bytes, replace_existing) };
        if (commit.succeeded() == false)
        {
            result.diagnostics.push_back(
                make_diagnostic(commit_diagnostic_code(commit.failure), diagnostic_severity::error, commit_failure_message(commit.failure), document_path, commit.native_error));
            return result;
        }

        result.revision = std::move(saved_revision);
        return result;
    }
} // namespace gitman
