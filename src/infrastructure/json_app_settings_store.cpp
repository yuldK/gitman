#include "infrastructure/json_app_settings_store.h"

#include <nlohmann/json.hpp>

#include <cstdint>
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
