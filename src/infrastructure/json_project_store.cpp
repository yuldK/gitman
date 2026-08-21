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

        void write_optional_project_fields(json& value, const project_definition& project)
        {
            const bool had_display_name { value.contains("display_name") };
            const bool had_vcs_hint { value.contains("vcs_hint") };
            const bool had_enabled { value.contains("enabled") };
            const bool had_preferred_remote { value.contains("preferred_remote") };
            const bool had_svn_switch_targets { value.contains("svn_switch_targets") };

            if (had_display_name || project.display_name != default_project_display_name(project.path.original))
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

        // 기존 `settings` object를 template으로 삼아 알 수 없는 키를 보존한다. 문서에
        // 없었고 값도 기본값이면 필드 자체를 만들지 않아 기존 문서 형태를 바꾸지 않는다.
        void write_settings(json& root, const workspace_settings& settings)
        {
            const auto existing { root.find("settings") };
            const bool had_settings { existing != root.end() && existing->is_object() };
            if (had_settings == false && settings.is_default())
            {
                if (existing != root.end())
                    root.erase("settings");
                return;
            }

            json value { had_settings ? *existing : json::object() };
            const bool had_git { value.contains("git_executable") };
            const bool had_svn { value.contains("svn_executable") };
            const bool had_relative_paths { value.contains("show_relative_paths") };
            const bool had_submodules { value.contains("update_submodules") };
            const bool had_ignore_local { value.contains("ignore_local_changes") };

            if (had_git || settings.git_executable.empty() == false)
                value["git_executable"] = as_string(settings.git_executable);
            if (had_svn || settings.svn_executable.empty() == false)
                value["svn_executable"] = as_string(settings.svn_executable);
            if (had_relative_paths || settings.show_relative_paths)
                value["show_relative_paths"] = settings.show_relative_paths;
            if (had_submodules || settings.update_submodules)
                value["update_submodules"] = settings.update_submodules;
            if (had_ignore_local || settings.ignore_local_changes)
                value["ignore_local_changes"] = settings.ignore_local_changes;

            // 제한 시간은 값이 없으면 기본값이라는 뜻이므로 필드를 지운다. 남겨 두면
            // 다음 열기에서 이전 값이 되살아난다.
            if (settings.query_timeout_seconds.has_value())
                value["query_timeout_seconds"] = *settings.query_timeout_seconds;
            else
                value.erase("query_timeout_seconds");

            root["settings"] = std::move(value);
        }

        // 창 배치는 표시 상태라 문서에 없던 값을 새로 만들지 않고, 문서에 있던 값을
        // 지우지도 않는다. 배치를 아는 경우에만 기존 object를 template으로 갱신한다.
        void write_window(json& root, const std::optional<window_placement>& placement)
        {
            if (placement.has_value() == false)
                return;

            const auto existing { root.find("window") };
            json value { existing != root.end() && existing->is_object() ? *existing : json::object() };
            value["x"] = placement->x;
            value["y"] = placement->y;
            value["width"] = placement->width;
            value["height"] = placement->height;
            value["maximized"] = placement->maximized;
            root["window"] = std::move(value);
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

        serialized_workspace_document serialize_workspace_document(const workspace_document& document, const std::u8string_view document_path, const std::u8string_view shadow_source_json,
            const std::span<const std::size_t> project_source_indices, project_path_resolver& path_resolver)
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
            write_settings(root, document.settings);
            write_window(root, document.window);
            root["projects"] = std::move(projects);

            serialized_workspace_document result {};
            result.bytes = format_json_bytes(root.dump(4, ' ', false, json::error_handler_t::strict));
            result.validation = parse_workspace_document_json(result.bytes, document_path);
            resolve_workspace_document_paths(result.validation, path_resolver);
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

        workspace_document_parse_result parse_and_resolve(const std::u8string_view bytes, const std::u8string_view document_path, project_path_resolver& path_resolver)
        {
            workspace_document_parse_result result { parse_workspace_document_json(bytes, document_path) };
            resolve_workspace_document_paths(result, path_resolver);
            return result;
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

    json_project_store::json_project_store(workspace_document_file_system& file_system, project_path_resolver& path_resolver) noexcept
        : file_system_ { file_system }
        , path_resolver_ { path_resolver }
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
            return result;
        }
        if (source.state == workspace_file_read_state::failed)
        {
            result.revision = make_revision_token(revision_file_state::unavailable, std::u8string { document_path }, {}, {}, {});
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::document_read_failed, diagnostic_severity::error, u8"작업공간 문서를 읽지 못했습니다.", document_path, source.native_error));
            return result;
        }

        workspace_document_parse_result parsed { parse_and_resolve(source.bytes, document_path, path_resolver_) };
        result.revision = make_revision_token(revision_file_state::present, std::u8string { document_path }, source.bytes, parsed.shadow.source_json, parsed.shadow.project_source_indices);
        result.document = std::move(parsed.document);
        result.diagnostics = std::move(parsed.diagnostics);
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
            serialize_workspace_document(document, document_path, revision->shadow_source_json, revision->project_source_indices, path_resolver_),
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

        const bool replace_existing { revision->file_state == revision_file_state::present };
        const workspace_file_commit_result commit { file_system_.atomic_commit(document_path, serialized.bytes, replace_existing) };
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
