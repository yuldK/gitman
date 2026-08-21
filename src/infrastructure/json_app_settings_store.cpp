#include "infrastructure/json_app_settings_store.h"

#include "domain/path_syntax.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gitman {
    namespace {
        using json = nlohmann::ordered_json;

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

        std::u8string as_u8string(const std::string_view value)
        {
            return std::u8string {
                reinterpret_cast<const char8_t*>(value.data()),
                value.size(),
            };
        }

        diagnostic make_diagnostic(const diagnostic_code code, const diagnostic_severity severity, const std::u8string_view message, const std::u8string_view path,
            const std::optional<std::uint32_t> native_error = std::nullopt)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = message;
            value.source.document_path = path;
            value.native_error = native_error;
            return value;
        }

        // 문서 저장과 같은 규칙이다: LF를 CRLF로 바꾸고 마지막 줄바꿈을 보장한다.
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

        // 문자열 필드 하나를 읽는다. 없거나 형식이 다르면 빈 값이다.
        std::u8string read_string(const json& value, const char* const key)
        {
            const auto found { value.find(key) };
            if (found == value.end() || found->is_string() == false)
                return {};
            return as_u8string(found->get_ref<const std::string&>());
        }

        // 문서의 `window`와 같은 규칙이다: 표시 상태라 어떤 오류도 앱 시작을 막지
        // 않고, 읽을 수 없으면 경고만 남기고 배치 없이 연다.
        std::optional<window_placement> parse_window(const json& root, app_settings_load_result& result, const std::u8string_view path)
        {
            const auto source { root.find("window") };
            if (source == root.end() || source->is_null())
                return std::nullopt;
            if (source->is_object() == false)
            {
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 window는 object여야 합니다. 창 배치를 무시합니다.", path));
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
                    result.diagnostics.push_back(
                        make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정 window의 좌표와 크기는 정수여야 합니다. 창 배치를 무시합니다.", path));
                    return std::nullopt;
                }
                const std::int64_t number { value->get<std::int64_t>() };
                if (number < std::numeric_limits<std::int32_t>::min() || number > std::numeric_limits<std::int32_t>::max())
                {
                    result.diagnostics.push_back(
                        make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정 window의 좌표와 크기가 표현 범위를 벗어났습니다. 창 배치를 무시합니다.", path));
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
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정 window의 크기는 양수여야 합니다. 창 배치를 무시합니다.", path));
                return std::nullopt;
            }

            if (const auto maximized { source->find("maximized") }; maximized != source->end() && maximized->is_boolean())
                placement.maximized = maximized->get<bool>();
            return placement;
        }

        // 전역 설정이다 (global-settings-and-ui-fixes-design G3). 문서 파서와 같은
        // 검증 규칙이되, 앱 설정은 어떤 오류도 시작을 막지 않으므로 전부 경고로
        // 남기고 그 필드만 기본값을 쓴다.
        workspace_settings parse_global_settings(const json& root, app_settings_load_result& result, const std::u8string_view path)
        {
            workspace_settings settings {};
            const auto source { root.find("settings") };
            if (source == root.end() || source->is_null())
                return settings;
            if (source->is_object() == false)
            {
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 settings는 object여야 합니다. 기본값을 사용합니다.", path));
                return settings;
            }

            const auto read_executable = [&source, &result, &path](const char* const key, std::u8string& target) {
                const auto value { source->find(key) };
                if (value == source->end() || value->is_null())
                    return;
                if (value->is_string() == false)
                {
                    result.diagnostics.push_back(
                        make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 실행 파일 경로는 문자열이어야 합니다. 기본값을 사용합니다.", path));
                    return;
                }
                std::u8string executable { as_u8string(value->get_ref<const std::string&>()) };
                // 빈 값은 "지정하지 않음"이며 자동 탐색으로 간다 (문서와 같은 규칙).
                if (executable.empty() == false && is_absolute_windows_path(executable) == false)
                {
                    result.diagnostics.push_back(
                        make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 실행 파일 경로는 절대 경로여야 합니다. 기본값을 사용합니다.", path));
                    return;
                }
                target = std::move(executable);
            };
            read_executable("git_executable", settings.git_executable);
            read_executable("svn_executable", settings.svn_executable);

            const auto read_boolean = [&source, &result, &path](const char* const key, bool& target) {
                const auto value { source->find(key) };
                if (value == source->end() || value->is_null())
                    return;
                if (value->is_boolean() == false)
                {
                    result.diagnostics.push_back(
                        make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 boolean 항목 형식이 잘못되어 기본값을 사용합니다.", path));
                    return;
                }
                target = value->get<bool>();
            };
            read_boolean("show_relative_paths", settings.show_relative_paths);
            read_boolean("update_submodules", settings.update_submodules);
            read_boolean("ignore_local_changes", settings.ignore_local_changes);
            read_boolean("write_log_files", settings.write_log_files);

            if (const auto timeout { source->find("query_timeout_seconds") }; timeout != source->end() && timeout->is_null() == false)
            {
                if (timeout->is_number_integer() == false)
                    result.diagnostics.push_back(
                        make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 제한 시간은 정수(초)여야 합니다. 기본값을 사용합니다.", path));
                else if (const std::int64_t seconds { timeout->get<std::int64_t>() }; seconds < minimum_query_timeout_seconds || seconds > maximum_query_timeout_seconds)
                    result.diagnostics.push_back(
                        make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 제한 시간은 10~3600초여야 합니다. 기본값을 사용합니다.", path));
                else
                    settings.query_timeout_seconds = { static_cast<std::int32_t>(seconds) };
            }
            return settings;
        }
    } // namespace

    std::u8string serialize_app_settings_json(const app_settings& settings, const std::u8string_view shadow_source_json)
    {
        json root { shadow_root(shadow_source_json) };
        json recent { json::array() };
        for (const recent_document& value : settings.recent_documents)
        {
            json entry { json::object() };
            entry["path"] = as_string(value.path);
            entry["display_name"] = as_string(value.display_name);
            entry["opened_at"] = as_string(value.opened_at);
            recent.push_back(std::move(entry));
        }

        root["schema_version"] = settings.schema_version;
        root["recent_documents"] = std::move(recent);

        // 전역 설정은 모든 키를 항상 기록한다. 값이 파일에 그대로 보여 사용자가
        // 직접 고치기도 쉽다. 기존 object의 알 수 없는 키는 template으로 보존한다.
        {
            const auto existing { root.find("settings") };
            json value { existing != root.end() && existing->is_object() ? *existing : json::object() };
            value["git_executable"] = as_string(settings.settings.git_executable);
            value["svn_executable"] = as_string(settings.settings.svn_executable);
            value["show_relative_paths"] = settings.settings.show_relative_paths;
            value["update_submodules"] = settings.settings.update_submodules;
            value["ignore_local_changes"] = settings.settings.ignore_local_changes;
            value["write_log_files"] = settings.settings.write_log_files;
            // 값이 없으면 기본값(600초)이라는 뜻이므로 키를 지운다.
            if (settings.settings.query_timeout_seconds.has_value())
                value["query_timeout_seconds"] = *settings.settings.query_timeout_seconds;
            else
                value.erase("query_timeout_seconds");
            root["settings"] = std::move(value);
        }

        // 값이 없으면 필드를 만들지 않고, 파일에 이미 있던 `window`는 지우지
        // 않는다 (문서 저장의 window 규칙과 동일).
        if (settings.window.has_value())
        {
            json window { json::object() };
            window["x"] = settings.window->x;
            window["y"] = settings.window->y;
            window["width"] = settings.window->width;
            window["height"] = settings.window->height;
            window["maximized"] = settings.window->maximized;
            root["window"] = std::move(window);
        }
        return format_json_bytes(root.dump(4, ' ', false, json::error_handler_t::strict));
    }

    app_settings_load_result parse_app_settings_json(const std::u8string_view source_json, const std::u8string_view path)
    {
        app_settings_load_result result {};
        const std::string_view source_bytes { as_bytes(source_json) };
        json root { json::parse(source_bytes.begin(), source_bytes.end(), nullptr, false) };
        if (root.is_discarded() || root.is_object() == false)
        {
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정 파일이 JSON object가 아니라 기본값을 사용합니다.", path));
            return result;
        }

        // 알 수 없는 키는 저장할 때 그대로 돌려 놓는다. 이후 버전이 추가한 항목을
        // 이전 버전이 지우지 않게 하는 규칙이다 (문서 저장과 동일).
        result.shadow_source_json = source_json;

        if (const auto version { root.find("schema_version") }; version != root.end() && version->is_number_integer())
            result.settings.schema_version = version->get<std::int32_t>();

        result.settings.window = parse_window(root, result, path);
        result.settings.settings = parse_global_settings(root, result, path);

        const auto recent { root.find("recent_documents") };
        if (recent == root.end())
            return result;
        if (recent->is_array() == false)
        {
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::app_settings_invalid, diagnostic_severity::warning, u8"앱 설정의 최근 문서 목록이 배열이 아니라 무시했습니다.", path));
            return result;
        }

        for (const json& value : *recent)
        {
            if (value.is_object() == false)
                continue;

            recent_document entry {};
            entry.path = read_string(value, "path");
            if (entry.path.empty())
                continue;
            entry.display_name = read_string(value, "display_name");
            if (entry.display_name.empty())
                entry.display_name = recent_document_display_name(entry.path);
            entry.opened_at = read_string(value, "opened_at");
            result.settings.recent_documents.push_back(std::move(entry));
            if (result.settings.recent_documents.size() >= recent_document_capacity)
                break;
        }
        return result;
    }

    json_app_settings_store::json_app_settings_store(workspace_document_file_system& file_system) noexcept
        : file_system_ { &file_system }
    {}

    app_settings_load_result json_app_settings_store::load(const std::u8string_view path) noexcept
    {
        try
        {
            const workspace_file_read_result source { file_system_->read(path) };
            if (source.state == workspace_file_read_state::not_found)
                return {};
            if (source.state == workspace_file_read_state::failed)
            {
                app_settings_load_result result {};
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::app_settings_read_failed, diagnostic_severity::warning, u8"앱 설정 파일을 읽지 못해 기본값을 사용합니다.", path, source.native_error));
                return result;
            }
            return parse_app_settings_json(source.bytes, path);
        }
        catch (...)
        {
            app_settings_load_result result {};
            try
            {
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::app_settings_read_failed, diagnostic_severity::warning, u8"앱 설정 파일을 읽는 중 예기치 않은 오류가 발생했습니다.", path));
            }
            catch (...)
            {}
            return result;
        }
    }

    app_settings_save_result json_app_settings_store::save(const std::u8string_view path, const app_settings& settings, const std::u8string_view shadow_source_json) noexcept
    {
        try
        {
            std::u8string bytes { serialize_app_settings_json(settings, shadow_source_json) };
            const workspace_file_read_result current { file_system_->read(path) };
            const bool replace_existing { current.state == workspace_file_read_state::available };
            const workspace_file_commit_result commit { file_system_->atomic_commit(path, bytes, replace_existing) };

            app_settings_save_result result {};
            if (commit.succeeded() == false)
            {
                result.diagnostics.push_back(make_diagnostic(diagnostic_code::app_settings_write_failed, diagnostic_severity::warning, u8"앱 설정을 저장하지 못했습니다.", path, commit.native_error));
                return result;
            }

            result.succeeded = true;
            result.shadow_source_json = std::move(bytes);
            return result;
        }
        catch (...)
        {
            app_settings_save_result result {};
            try
            {
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::app_settings_write_failed, diagnostic_severity::warning, u8"앱 설정을 저장하는 중 예기치 않은 오류가 발생했습니다.", path));
            }
            catch (...)
            {}
            return result;
        }
    }
} // namespace gitman
