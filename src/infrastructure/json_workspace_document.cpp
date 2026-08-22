#include "infrastructure/json_workspace_document.h"

#include "domain/path_syntax.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace gitman {
    namespace {
        using json = nlohmann::json;

        std::string_view as_bytes(const std::u8string_view value) noexcept
        {
            return {
                reinterpret_cast<const char*>(value.data()),
                value.size(),
            };
        }

        std::u8string as_utf8(const std::string_view value)
        {
            return {
                reinterpret_cast<const char8_t*>(value.data()),
                value.size(),
            };
        }

        void append_ascii_number(std::u8string& target, const std::size_t value)
        {
            std::array<char, std::numeric_limits<std::size_t>::digits10 + 2> buffer {};
            const auto result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), value) };
            for (const char* current = buffer.data(); current != result.ptr; ++current)
                target.push_back(static_cast<char8_t>(*current));
        }

        std::u8string escape_json_pointer_token(const std::string_view token)
        {
            std::u8string escaped {};
            escaped.reserve(token.size());
            for (const char value : token)
            {
                if (value == '~')
                {
                    escaped.append(u8"~0");
                    continue;
                }
                if (value == '/')
                {
                    escaped.append(u8"~1");
                    continue;
                }
                escaped.push_back(static_cast<char8_t>(value));
            }
            return escaped;
        }

        std::u8string project_pointer(const std::size_t project_index)
        {
            std::u8string pointer { u8"/projects/" };
            append_ascii_number(pointer, project_index);
            return pointer;
        }

        std::u8string project_field_pointer(const std::size_t project_index, const std::string_view field)
        {
            std::u8string pointer { project_pointer(project_index) };
            pointer.push_back(u8'/');
            pointer.append(escape_json_pointer_token(field));
            return pointer;
        }

        std::u8string project_array_element_pointer(const std::size_t project_index, const std::string_view field, const std::size_t element_index)
        {
            std::u8string pointer { project_field_pointer(project_index, field) };
            pointer.push_back(u8'/');
            append_ascii_number(pointer, element_index);
            return pointer;
        }

        void add_diagnostic(workspace_document_parse_result& result, const diagnostic_code code, const diagnostic_severity severity, const std::u8string_view message,
            const std::u8string_view document_path, std::u8string json_pointer, const std::optional<std::size_t> project_index = std::nullopt, std::optional<std::u8string> project_id = std::nullopt)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = message;
            value.source.document_path = document_path;
            value.source.json_pointer = std::move(json_pointer);
            value.source.project_index = project_index;
            value.source.project_id = std::move(project_id);
            result.diagnostics.push_back(std::move(value));
        }

        bool is_known_top_level_field(const std::string_view field) noexcept
        {
            return field == "schema_version" || field == "settings" || field == "window" || field == "appearance" || field == "projects";
        }

        // 외양은 표시 설정이라 어떤 오류도 문서 열기를 막지 않는다. 읽을 수 없는
        // 항목은 경고 하나를 남기고 앱 설정을 따른다
        // (settings-tabs-and-appearance-scope-design S2.2).
        appearance_overrides parse_appearance(const json& root, workspace_document_parse_result& result, const std::u8string_view document_path)
        {
            appearance_overrides appearance {};
            const auto source { root.find("appearance") };
            if (source == root.end() || source->is_null())
                return appearance;

            if (source->is_object() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"appearance는 object여야 합니다. 앱 설정의 외양을 사용합니다.", document_path,
                    u8"/appearance");
                return appearance;
            }

            if (const auto theme { source->find("theme") }; theme != source->end() && theme->is_null() == false)
            {
                theme_preference value {};
                if (theme->is_string() && parse_theme_preference(as_utf8(theme->get_ref<const std::string&>()), value))
                    appearance.theme = { value };
                else
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"appearance의 theme은 system·light·dark 중 하나여야 합니다. 앱 설정을 따릅니다.",
                        document_path, u8"/appearance/theme");
            }

            if (const auto accent { source->find("accent") }; accent != source->end() && accent->is_null() == false)
            {
                if (accent->is_string() == false)
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"appearance의 accent는 문자열이어야 합니다. 앱 설정을 따릅니다.", document_path,
                        u8"/appearance/accent");
                else if (std::u8string id { as_utf8(accent->get_ref<const std::string&>()) }; id.empty() == false)
                    appearance.accent_id = { std::move(id) };
            }
            return appearance;
        }

        std::u8string window_field_pointer(const std::string_view field)
        {
            std::u8string pointer { u8"/window/" };
            pointer.append(escape_json_pointer_token(field));
            return pointer;
        }

        // `window`는 표시 상태이므로 어떤 오류도 문서 열기를 막지 않는다. 읽을 수
        // 없으면 경고만 남기고 배치 없이 연다.
        std::optional<window_placement> parse_window(const json& root, workspace_document_parse_result& result, const std::u8string_view document_path)
        {
            const auto source { root.find("window") };
            if (source == root.end() || source->is_null())
                return std::nullopt;

            if (source->is_object() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_window_placement, diagnostic_severity::warning, u8"window는 object여야 합니다. 창 배치를 무시합니다.", document_path, u8"/window");
                return std::nullopt;
            }

            constexpr std::array coordinate_fields {
                std::string_view { "x" },
                std::string_view { "y" },
                std::string_view { "width" },
                std::string_view { "height" },
            };
            std::array<std::int32_t, 4> coordinates {};
            for (std::size_t index = 0; index < coordinate_fields.size(); ++index)
            {
                const auto value { source->find(coordinate_fields[index]) };
                if (value == source->end() || value->is_number_integer() == false)
                {
                    add_diagnostic(result, diagnostic_code::invalid_window_placement, diagnostic_severity::warning, u8"window의 좌표와 크기는 정수여야 합니다. 창 배치를 무시합니다.", document_path,
                        window_field_pointer(coordinate_fields[index]));
                    return std::nullopt;
                }

                const std::int64_t number { value->get<std::int64_t>() };
                if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max())
                {
                    add_diagnostic(result, diagnostic_code::invalid_window_placement, diagnostic_severity::warning, u8"window의 좌표와 크기가 표현 범위를 벗어났습니다. 창 배치를 무시합니다.",
                        document_path, window_field_pointer(coordinate_fields[index]));
                    return std::nullopt;
                }
                coordinates[index] = static_cast<std::int32_t>(number);
            }

            window_placement placement {};
            placement.x = coordinates[0];
            placement.y = coordinates[1];
            placement.width = coordinates[2];
            placement.height = coordinates[3];
            if (placement.valid() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_window_placement, diagnostic_severity::warning, u8"window의 크기는 양수여야 합니다. 창 배치를 무시합니다.", document_path,
                    window_field_pointer("width"));
                return std::nullopt;
            }

            const auto maximized { source->find("maximized") };
            if (maximized != source->end() && maximized->is_null() == false)
            {
                if (maximized->is_boolean() == false)
                {
                    add_diagnostic(result, diagnostic_code::invalid_window_placement, diagnostic_severity::warning, u8"window의 maximized는 boolean이어야 합니다. 최대화 상태를 무시합니다.",
                        document_path, window_field_pointer("maximized"));
                }
                else
                    placement.maximized = maximized->get<bool>();
            }
            return placement;
        }

        bool is_known_settings_field(const std::string_view field) noexcept
        {
            return field == "git_executable" || field == "svn_executable" || field == "show_relative_paths" || field == "update_submodules" || field == "ignore_local_changes"
                || field == "write_log_files" || field == "query_timeout_seconds";
        }

        std::u8string settings_field_pointer(const std::string_view field)
        {
            std::u8string pointer { u8"/settings/" };
            pointer.append(escape_json_pointer_token(field));
            return pointer;
        }

        // `settings`는 optional이며 문서에 있는 키만 override로 읽는다. 없는 키는
        // 앱 설정을 따른다 (global-settings-and-ui-fixes-design G3.1). 스키마 버전을
        // 올리지 않아 이 필드를 모르는 기존 문서도 그대로 열리고, 기존 문서의 키는
        // 그대로 override가 되어 동작이 바뀌지 않는다.
        workspace_settings_overrides parse_settings(const json& root, workspace_document_parse_result& result, const std::u8string_view document_path)
        {
            workspace_settings_overrides settings {};
            const auto source { root.find("settings") };
            if (source == root.end() || source->is_null())
                return settings;

            if (source->is_object() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"settings는 object여야 합니다.", document_path, u8"/settings");
                return settings;
            }

            constexpr std::array executable_fields {
                std::string_view { "git_executable" },
                std::string_view { "svn_executable" },
            };
            for (const std::string_view field : executable_fields)
            {
                const auto value { source->find(field) };
                if (value == source->end() || value->is_null())
                    continue;
                if (value->is_string() == false)
                {
                    add_diagnostic(
                        result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"settings의 실행 파일 경로는 문자열이어야 합니다.", document_path, settings_field_pointer(field));
                    continue;
                }

                std::u8string executable { as_utf8(value->get_ref<const std::string&>()) };
                // 빈 값은 "지정하지 않음"이며 자동 탐색으로 간다. 값이 있으면 절대
                // 경로여야 한다. 상대 경로는 실행 시점의 현재 디렉터리에 따라 다른
                // 프로그램을 가리킬 수 있다.
                if (executable.empty() == false && is_absolute_windows_path(executable) == false)
                {
                    add_diagnostic(result, diagnostic_code::vcs_tool_path_invalid, diagnostic_severity::error, u8"settings의 실행 파일 경로는 절대 경로여야 합니다.", document_path,
                        settings_field_pointer(field));
                    continue;
                }
                if (field == "git_executable")
                    settings.git_executable = { std::move(executable) };
                else
                    settings.svn_executable = { std::move(executable) };
            }

            // 상태 확인 제한 시간이다 (field-feedback-design 1.3). 값이 나빠도 문서
            // 열기를 막지 않는다 — 경고만 남기고 앱 설정을 따른다.
            const auto timeout { source->find("query_timeout_seconds") };
            if (timeout != source->end() && timeout->is_null() == false)
            {
                if (timeout->is_number_integer() == false)
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"settings의 제한 시간은 정수(초)여야 합니다. 기본값을 사용합니다.",
                        document_path, settings_field_pointer("query_timeout_seconds"));
                }
                else if (const std::int64_t seconds { timeout->get<std::int64_t>() }; seconds < minimum_query_timeout_seconds || seconds > maximum_query_timeout_seconds)
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"settings의 제한 시간은 10~3600초여야 합니다. 기본값을 사용합니다.",
                        document_path, settings_field_pointer("query_timeout_seconds"));
                }
                else
                    settings.query_timeout_seconds = { static_cast<std::int32_t>(seconds) };
            }

            const auto relative_paths { source->find("show_relative_paths") };
            if (relative_paths != source->end() && relative_paths->is_null() == false)
            {
                if (relative_paths->is_boolean())
                    settings.show_relative_paths = { relative_paths->get<bool>() };
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"settings의 show_relative_paths는 boolean이어야 합니다.", document_path,
                        settings_field_pointer("show_relative_paths"));
                }
            }

            const auto submodules { source->find("update_submodules") };
            if (submodules != source->end() && submodules->is_null() == false)
            {
                if (submodules->is_boolean())
                    settings.update_submodules = { submodules->get<bool>() };
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"settings의 update_submodules는 boolean이어야 합니다.", document_path,
                        settings_field_pointer("update_submodules"));
                }
            }

            const auto ignore_local { source->find("ignore_local_changes") };
            if (ignore_local != source->end() && ignore_local->is_null() == false)
            {
                if (ignore_local->is_boolean())
                    settings.ignore_local_changes = { ignore_local->get<bool>() };
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"settings의 ignore_local_changes는 boolean이어야 합니다.", document_path,
                        settings_field_pointer("ignore_local_changes"));
                }
            }

            const auto write_logs { source->find("write_log_files") };
            if (write_logs != source->end() && write_logs->is_null() == false)
            {
                if (write_logs->is_boolean())
                    settings.write_log_files = { write_logs->get<bool>() };
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"settings의 write_log_files는 boolean이어야 합니다.", document_path,
                        settings_field_pointer("write_log_files"));
                }
            }

            for (auto field = source->begin(); field != source->end(); ++field)
            {
                if (is_known_settings_field(field.key()))
                    continue;
                add_diagnostic(result, diagnostic_code::unknown_field, diagnostic_severity::warning, u8"알 수 없는 settings 필드를 보존합니다.", document_path, settings_field_pointer(field.key()));
            }
            return settings;
        }

        bool is_known_project_field(const std::string_view field) noexcept
        {
            constexpr std::array known_fields {
                std::string_view { "id" },
                std::string_view { "path" },
                std::string_view { "display_name" },
                std::string_view { "vcs_hint" },
                std::string_view { "enabled" },
                std::string_view { "preferred_remote" },
                std::string_view { "svn_switch_targets" },
            };
            return std::ranges::find(known_fields, field) != known_fields.end();
        }

        std::optional<std::int32_t> parse_schema_version(const json& root, workspace_document_parse_result& result, const std::u8string_view document_path)
        {
            const auto version { root.find("schema_version") };
            if (version == root.end())
            {
                add_diagnostic(result, diagnostic_code::missing_schema_version, diagnostic_severity::error, u8"schema_version 필드가 필요합니다.", document_path, u8"/schema_version");
                return std::nullopt;
            }
            if (version->is_number_integer() == false && version->is_number_unsigned() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_schema_version, diagnostic_severity::error, u8"schema_version은 정수여야 합니다.", document_path, u8"/schema_version");
                return std::nullopt;
            }

            if (version->is_number_unsigned())
            {
                const std::uint64_t unsigned_version { version->get<std::uint64_t>() };
                if (unsigned_version < static_cast<std::uint64_t>(current_workspace_schema_version))
                {
                    add_diagnostic(result, diagnostic_code::unsupported_legacy_schema, diagnostic_severity::error, u8"지원하지 않는 이전 schema version입니다.", document_path, u8"/schema_version");
                    return std::nullopt;
                }
                if (unsigned_version > static_cast<std::uint64_t>(current_workspace_schema_version))
                {
                    add_diagnostic(
                        result, diagnostic_code::unsupported_future_schema, diagnostic_severity::error, u8"현재 Gitman보다 새로운 schema version입니다.", document_path, u8"/schema_version");
                    return std::nullopt;
                }
                return static_cast<std::int32_t>(unsigned_version);
            }

            const std::int64_t signed_version { version->get<std::int64_t>() };
            if (signed_version < current_workspace_schema_version)
            {
                add_diagnostic(result, diagnostic_code::unsupported_legacy_schema, diagnostic_severity::error, u8"지원하지 않는 이전 schema version입니다.", document_path, u8"/schema_version");
                return std::nullopt;
            }
            if (signed_version > current_workspace_schema_version)
            {
                add_diagnostic(result, diagnostic_code::unsupported_future_schema, diagnostic_severity::error, u8"현재 Gitman보다 새로운 schema version입니다.", document_path, u8"/schema_version");
                return std::nullopt;
            }
            return static_cast<std::int32_t>(signed_version);
        }

        std::optional<project_definition> parse_project(
            const json& source, const std::size_t project_index, const std::u8string_view document_path, workspace_document_parse_result& result, std::unordered_set<std::u8string>& project_ids)
        {
            if (source.is_object() == false)
            {
                add_diagnostic(
                    result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"project 항목은 object여야 합니다.", document_path, project_pointer(project_index), project_index);
                return std::nullopt;
            }

            project_definition project {};
            bool is_valid { true };
            std::optional<std::u8string> diagnostic_project_id {};

            const auto id { source.find("id") };
            if (id == source.end())
            {
                add_diagnostic(
                    result, diagnostic_code::missing_project_field, diagnostic_severity::error, u8"project id가 필요합니다.", document_path, project_field_pointer(project_index, "id"), project_index);
                is_valid = false;
            }
            else if (id->is_string() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"project id는 문자열이어야 합니다.", document_path,
                    project_field_pointer(project_index, "id"), project_index);
                is_valid = false;
            }
            else
            {
                project.id.value = as_utf8(id->get_ref<const std::string&>());
                if (project.id.value.empty())
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_id, diagnostic_severity::error, u8"project id는 비어 있을 수 없습니다.", document_path,
                        project_field_pointer(project_index, "id"), project_index);
                    is_valid = false;
                }
                else
                    diagnostic_project_id = project.id.value;
            }

            const auto path { source.find("path") };
            if (path == source.end())
            {
                add_diagnostic(result, diagnostic_code::missing_project_field, diagnostic_severity::error, u8"project path가 필요합니다.", document_path, project_field_pointer(project_index, "path"),
                    project_index, diagnostic_project_id);
                is_valid = false;
            }
            else if (path->is_string() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"project path는 문자열이어야 합니다.", document_path,
                    project_field_pointer(project_index, "path"), project_index, diagnostic_project_id);
                is_valid = false;
            }
            else
            {
                project.path.original = as_utf8(path->get_ref<const std::string&>());
                if (project.path.original.empty())
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_path, diagnostic_severity::error, u8"project path는 비어 있을 수 없습니다.", document_path,
                        project_field_pointer(project_index, "path"), project_index, diagnostic_project_id);
                    is_valid = false;
                }
            }

            const auto display_name { source.find("display_name") };
            if (display_name == source.end())
                project.display_name = default_project_display_name(project.path.original);
            else if (display_name->is_string())
                project.display_name = as_utf8(display_name->get_ref<const std::string&>());
            else
            {
                add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"display_name은 문자열이어야 합니다.", document_path,
                    project_field_pointer(project_index, "display_name"), project_index, diagnostic_project_id);
                is_valid = false;
            }

            const auto hint { source.find("vcs_hint") };
            if (hint != source.end())
            {
                if (hint->is_string())
                {
                    const auto parsed_hint { parse_vcs_hint(as_utf8(hint->get_ref<const std::string&>())) };
                    if (parsed_hint.has_value())
                        project.hint = *parsed_hint;
                    else
                    {
                        add_diagnostic(result, diagnostic_code::invalid_vcs_hint, diagnostic_severity::error, u8"vcs_hint는 auto, git, svn 중 하나여야 합니다.", document_path,
                            project_field_pointer(project_index, "vcs_hint"), project_index, diagnostic_project_id);
                        is_valid = false;
                    }
                }
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"vcs_hint는 문자열이어야 합니다.", document_path,
                        project_field_pointer(project_index, "vcs_hint"), project_index, diagnostic_project_id);
                    is_valid = false;
                }
            }

            const auto enabled { source.find("enabled") };
            if (enabled != source.end())
            {
                if (enabled->is_boolean())
                    project.enabled = enabled->get<bool>();
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"enabled는 boolean이어야 합니다.", document_path,
                        project_field_pointer(project_index, "enabled"), project_index, diagnostic_project_id);
                    is_valid = false;
                }
            }

            const auto preferred_remote { source.find("preferred_remote") };
            if (preferred_remote != source.end() && preferred_remote->is_null() == false)
            {
                if (preferred_remote->is_string())
                    project.preferred_remote = as_utf8(preferred_remote->get_ref<const std::string&>());
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"preferred_remote는 문자열 또는 null이어야 합니다.", document_path,
                        project_field_pointer(project_index, "preferred_remote"), project_index, diagnostic_project_id);
                    is_valid = false;
                }
            }

            const auto svn_targets { source.find("svn_switch_targets") };
            if (svn_targets != source.end())
            {
                if (svn_targets->is_array())
                {
                    for (std::size_t target_index = 0; target_index < svn_targets->size(); ++target_index)
                    {
                        const json& target { (*svn_targets)[target_index] };
                        if (target.is_string())
                            project.svn_switch_targets.push_back(as_utf8(target.get_ref<const std::string&>()));
                        else
                        {
                            add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"svn_switch_targets의 모든 값은 문자열이어야 합니다.", document_path,
                                project_array_element_pointer(project_index, "svn_switch_targets", target_index), project_index, diagnostic_project_id);
                            is_valid = false;
                        }
                    }
                }
                else
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::error, u8"svn_switch_targets는 배열이어야 합니다.", document_path,
                        project_field_pointer(project_index, "svn_switch_targets"), project_index, diagnostic_project_id);
                    is_valid = false;
                }
            }

            for (auto field = source.begin(); field != source.end(); ++field)
            {
                if (is_known_project_field(field.key()))
                    continue;
                add_diagnostic(result, diagnostic_code::unknown_field, diagnostic_severity::warning, u8"알 수 없는 project 필드를 보존합니다.", document_path,
                    project_field_pointer(project_index, field.key()), project_index, diagnostic_project_id);
            }

            if (is_valid == false)
                return std::nullopt;
            if (project_ids.insert(project.id.value).second == false)
            {
                add_diagnostic(result, diagnostic_code::duplicate_project_id, diagnostic_severity::error, u8"중복된 project id입니다.", document_path, project_field_pointer(project_index, "id"),
                    project_index, project.id.value);
                return std::nullopt;
            }
            return project;
        }
    } // namespace

    bool workspace_document_parse_result::has_errors() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::error; });
    }

    bool workspace_document_parse_result::has_warnings() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::warning; });
    }

    std::u8string default_project_display_name(const std::u8string_view path)
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

    workspace_document_parse_result parse_workspace_document_json(const std::u8string_view source_json, const std::u8string_view document_path)
    {
        workspace_document_parse_result result {};
        result.shadow.source_json = source_json;

        const std::string_view source_bytes { as_bytes(source_json) };
        const json root { json::parse(source_bytes.begin(), source_bytes.end(), nullptr, false) };
        if (root.is_discarded())
        {
            add_diagnostic(result, diagnostic_code::malformed_document, diagnostic_severity::error, u8"작업공간 문서가 유효한 JSON이 아닙니다.", document_path, {});
            return result;
        }
        if (root.is_object() == false)
        {
            add_diagnostic(result, diagnostic_code::invalid_document_root, diagnostic_severity::error, u8"작업공간 문서의 root는 object여야 합니다.", document_path, {});
            return result;
        }

        for (auto field = root.begin(); field != root.end(); ++field)
        {
            if (is_known_top_level_field(field.key()))
                continue;
            std::u8string pointer { u8"/" };
            pointer.append(escape_json_pointer_token(field.key()));
            add_diagnostic(result, diagnostic_code::unknown_field, diagnostic_severity::warning, u8"알 수 없는 top-level 필드를 보존합니다.", document_path, std::move(pointer));
        }

        const std::optional<std::int32_t> schema_version { parse_schema_version(root, result, document_path) };
        if (schema_version.has_value() == false)
            return result;

        const auto projects { root.find("projects") };
        if (projects == root.end())
        {
            add_diagnostic(result, diagnostic_code::missing_projects, diagnostic_severity::error, u8"projects 배열이 필요합니다.", document_path, u8"/projects");
            return result;
        }
        if (projects->is_array() == false)
        {
            add_diagnostic(result, diagnostic_code::invalid_projects, diagnostic_severity::error, u8"projects는 배열이어야 합니다.", document_path, u8"/projects");
            return result;
        }

        workspace_document document {};
        document.schema_version = *schema_version;
        document.document_path = document_path;
        document.settings = parse_settings(root, result, document_path);
        document.window = parse_window(root, result, document_path);
        document.appearance = parse_appearance(root, result, document_path);
        std::unordered_set<std::u8string> project_ids {};
        for (std::size_t project_index = 0; project_index < projects->size(); ++project_index)
        {
            std::optional<project_definition> project { parse_project((*projects)[project_index], project_index, document_path, result, project_ids) };
            if (project.has_value())
            {
                document.projects.push_back(std::move(*project));
                result.shadow.project_source_indices.push_back(project_index);
            }
        }
        result.document = std::move(document);
        return result;
    }
} // namespace gitman
