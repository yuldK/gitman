#include "infrastructure/json_command_catalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace gitman {
    namespace {
        using json = nlohmann::ordered_json;

        [[nodiscard]] std::u8string as_u8string(const std::string_view value)
        {
            return std::u8string { reinterpret_cast<const char8_t*>(value.data()), value.size() };
        }

        [[nodiscard]] std::string as_string(const std::u8string_view value)
        {
            return std::string { reinterpret_cast<const char*>(value.data()), value.size() };
        }

        [[nodiscard]] std::u8string index_pointer(const std::u8string_view prefix, const std::size_t index)
        {
            std::u8string pointer { prefix };
            pointer.push_back(u8'/');
            const std::string digits { std::to_string(index) };
            pointer.append(digits.begin(), digits.end());
            return pointer;
        }

        [[nodiscard]] std::u8string field_pointer(const std::u8string_view prefix, const std::u8string_view field)
        {
            std::u8string pointer { prefix };
            pointer.push_back(u8'/');
            pointer.append(field);
            return pointer;
        }

        class diagnostic_sink
        {
        public:
            diagnostic_sink(std::vector<diagnostic>& diagnostics, const std::u8string_view document_path, const diagnostic_severity severity) noexcept
                : diagnostics_ { &diagnostics }
                , document_path_ { document_path }
                , severity_ { severity }
            {
            }

            void add(const diagnostic_code code, const std::u8string_view message, std::u8string pointer) const
            {
                diagnostic value {};
                value.code = code;
                value.severity = severity_;
                value.message = message;
                value.source.document_path = document_path_;
                value.source.json_pointer = std::move(pointer);
                diagnostics_->push_back(std::move(value));
            }

        private:
            std::vector<diagnostic>* diagnostics_ { nullptr };
            std::u8string_view document_path_ {};
            diagnostic_severity severity_ { diagnostic_severity::warning };
        };

        // 문자열 하나를 읽는다. 값이 없으면 대상을 건드리지 않고, 타입이 다르면
        // 진단 하나를 남기고 무시한다.
        void read_string(const json& source, const char* const key, const std::u8string_view pointer, const diagnostic_sink& sink, std::u8string& target)
        {
            const auto field { source.find(key) };
            if (field == source.end() || field->is_null())
                return;
            if (field->is_string() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, u8"문자열이어야 합니다.", field_pointer(pointer, as_u8string(key)));
                return;
            }
            target = as_u8string(field->get_ref<const std::string&>());
        }

        void read_boolean(const json& source, const char* const key, const std::u8string_view pointer, const diagnostic_sink& sink, bool& target)
        {
            const auto field { source.find(key) };
            if (field == source.end() || field->is_null())
                return;
            if (field->is_boolean() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, u8"true 또는 false여야 합니다.", field_pointer(pointer, as_u8string(key)));
                return;
            }
            target = field->get<bool>();
        }

        void read_seconds(const json& source, const char* const key, const std::u8string_view pointer, const diagnostic_sink& sink, std::int32_t& target)
        {
            const auto field { source.find(key) };
            if (field == source.end() || field->is_null())
                return;
            if (field->is_number_integer() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, u8"제한 시간은 정수(초)여야 합니다.", field_pointer(pointer, as_u8string(key)));
                return;
            }
            const std::int64_t value { field->get<std::int64_t>() };
            if (value < 0 || value > 24 * 60 * 60)
            {
                sink.add(diagnostic_code::invalid_command_definition, u8"제한 시간은 0초 이상 24시간 이하여야 합니다.", field_pointer(pointer, as_u8string(key)));
                return;
            }
            target = static_cast<std::int32_t>(value);
        }

        void read_string_array(const json& source, const char* const key, const std::u8string_view pointer, const diagnostic_sink& sink, std::vector<std::u8string>& target)
        {
            const auto field { source.find(key) };
            if (field == source.end() || field->is_null())
                return;
            if (field->is_array() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, u8"배열이어야 합니다.", field_pointer(pointer, as_u8string(key)));
                return;
            }

            std::vector<std::u8string> values {};
            for (const json& entry : *field)
            {
                if (entry.is_string() == false)
                {
                    sink.add(diagnostic_code::invalid_command_definition, u8"배열의 항목은 문자열이어야 합니다.", field_pointer(pointer, as_u8string(key)));
                    continue;
                }
                values.push_back(as_u8string(entry.get_ref<const std::string&>()));
            }
            target = std::move(values);
        }

        [[nodiscard]] std::optional<custom_command> parse_custom_command(const json& source, const std::u8string_view pointer, const diagnostic_sink& sink)
        {
            if (source.is_object() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, u8"명령은 object여야 합니다.", std::u8string { pointer });
                return std::nullopt;
            }

            custom_command command {};
            read_string(source, "id", pointer, sink, command.id);
            read_string(source, "label", pointer, sink, command.label);
            read_string(source, "script", pointer, sink, command.script);
            read_string_array(source, "arguments", pointer, sink, command.arguments);
            read_string(source, "icon", pointer, sink, command.icon);
            read_boolean(source, "show_on_card", pointer, sink, command.show_on_card);
            read_string(source, "path_pattern", pointer, sink, command.path_pattern);
            read_string_array(source, "projects", pointer, sink, command.projects);
            read_seconds(source, "timeout_seconds", pointer, sink, command.timeout_seconds);
            read_boolean(source, "refresh_after", pointer, sink, command.refresh_after);

            if (const auto field { source.find("vcs") }; field != source.end() && field->is_null() == false)
            {
                const std::optional<command_target_vcs> value { field->is_string() ? parse_command_target_vcs(as_u8string(field->get_ref<const std::string&>())) : std::nullopt };
                if (value.has_value())
                    command.vcs = *value;
                else
                    sink.add(diagnostic_code::invalid_command_definition, u8"vcs는 any·git·subversion 중 하나여야 합니다.", field_pointer(pointer, u8"vcs"));
            }

            if (const auto field { source.find("working_directory") }; field != source.end() && field->is_null() == false)
            {
                const std::optional<command_working_directory> value { field->is_string() ? parse_command_working_directory(as_u8string(field->get_ref<const std::string&>())) : std::nullopt };
                if (value.has_value())
                    command.working_directory = *value;
                else
                    sink.add(diagnostic_code::invalid_command_definition, u8"working_directory는 repository·document·script 중 하나여야 합니다.", field_pointer(pointer, u8"working_directory"));
            }

            // 성립하지 않는 정의는 목록에서 뺀다. 반쯤 채워진 명령이 메뉴에 뜨면
            // 눌러 봐야 실패를 알게 된다.
            if (const std::u8string_view error { custom_command_error(command) }; error.empty() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, error, std::u8string { pointer });
                return std::nullopt;
            }
            return command;
        }

        [[nodiscard]] std::optional<svn_hook> parse_svn_hook(const json& source, const std::u8string_view pointer, const diagnostic_sink& sink)
        {
            if (source.is_object() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, u8"훅은 object여야 합니다.", std::u8string { pointer });
                return std::nullopt;
            }

            svn_hook hook {};
            read_string(source, "script", pointer, sink, hook.script);
            read_string_array(source, "arguments", pointer, sink, hook.arguments);
            read_boolean(source, "enabled", pointer, sink, hook.enabled);
            read_string(source, "path_pattern", pointer, sink, hook.path_pattern);
            read_seconds(source, "timeout_seconds", pointer, sink, hook.timeout_seconds);

            if (const auto field { source.find("phase") }; field != source.end() && field->is_null() == false)
            {
                const std::optional<svn_hook_phase> value { field->is_string() ? parse_svn_hook_phase(as_u8string(field->get_ref<const std::string&>())) : std::nullopt };
                if (value.has_value())
                    hook.phase = *value;
                else
                    sink.add(diagnostic_code::invalid_command_definition, u8"phase는 start_update·pre_update·post_update 중 하나여야 합니다.", field_pointer(pointer, u8"phase"));
            }

            if (const std::u8string_view error { svn_hook_error(hook) }; error.empty() == false)
            {
                sink.add(diagnostic_code::invalid_command_definition, error, std::u8string { pointer });
                return std::nullopt;
            }
            return hook;
        }

        [[nodiscard]] json custom_command_json(const custom_command& command)
        {
            json entry { json::object() };
            entry["id"] = as_string(command.id);
            entry["label"] = as_string(command.label);
            entry["script"] = as_string(command.script);
            if (command.arguments.empty() == false)
            {
                json arguments { json::array() };
                for (const std::u8string& argument : command.arguments)
                    arguments.push_back(as_string(argument));
                entry["arguments"] = std::move(arguments);
            }
            if (command.working_directory != command_working_directory::repository)
                entry["working_directory"] = as_string(command_working_directory_name(command.working_directory));
            if (command.icon.empty() == false)
                entry["icon"] = as_string(command.icon);
            if (command.show_on_card)
                entry["show_on_card"] = true;
            if (command.vcs != command_target_vcs::any)
                entry["vcs"] = as_string(command_target_vcs_name(command.vcs));
            if (command.path_pattern.empty() == false)
                entry["path_pattern"] = as_string(command.path_pattern);
            if (command.projects.empty() == false)
            {
                json projects { json::array() };
                for (const std::u8string& project : command.projects)
                    projects.push_back(as_string(project));
                entry["projects"] = std::move(projects);
            }
            if (command.timeout_seconds > 0)
                entry["timeout_seconds"] = command.timeout_seconds;
            // 기본값이 참이라 거짓일 때만 쓴다.
            if (command.refresh_after == false)
                entry["refresh_after"] = false;
            return entry;
        }

        [[nodiscard]] json svn_hook_settings_json(const svn_hook_settings& settings)
        {
            json target { json::object() };
            target["use_tortoise_svn"] = settings.use_tortoise_svn;

            json hooks { json::array() };
            for (const svn_hook& hook : settings.hooks)
            {
                json entry { json::object() };
                entry["phase"] = as_string(svn_hook_phase_name(hook.phase));
                entry["script"] = as_string(hook.script);
                if (hook.arguments.empty() == false)
                {
                    json arguments { json::array() };
                    for (const std::u8string& argument : hook.arguments)
                        arguments.push_back(as_string(argument));
                    entry["arguments"] = std::move(arguments);
                }
                if (hook.enabled == false)
                    entry["enabled"] = false;
                if (hook.path_pattern.empty() == false)
                    entry["path_pattern"] = as_string(hook.path_pattern);
                if (hook.timeout_seconds > 0)
                    entry["timeout_seconds"] = hook.timeout_seconds;
                hooks.push_back(std::move(entry));
            }
            target["hooks"] = std::move(hooks);
            return target;
        }
    } // namespace

    command_catalog_parse_result parse_command_catalog(
        const nlohmann::ordered_json& source, const std::u8string_view document_path, const std::u8string_view pointer_prefix, const diagnostic_severity severity)
    {
        command_catalog_parse_result result {};
        const diagnostic_sink sink { result.diagnostics, document_path, severity };

        if (source.is_object() == false)
        {
            sink.add(diagnostic_code::invalid_command_definition, u8"commands는 object여야 합니다.", std::u8string { pointer_prefix });
            return result;
        }

        if (const auto custom { source.find("custom") }; custom != source.end() && custom->is_null() == false)
        {
            const std::u8string custom_pointer { field_pointer(pointer_prefix, u8"custom") };
            if (custom->is_array() == false)
                sink.add(diagnostic_code::invalid_command_definition, u8"custom은 배열이어야 합니다.", custom_pointer);
            else
            {
                // 배열이 있으면 빈 배열이라도 "정의"다. 전역 목록을 갈아 끼운다.
                std::vector<custom_command> commands {};
                for (std::size_t index = 0; index < custom->size(); ++index)
                {
                    std::optional<custom_command> command { parse_custom_command((*custom)[index], index_pointer(custom_pointer, index), sink) };
                    if (command.has_value() == false)
                        continue;
                    // id가 겹치면 메뉴 key와 element id가 충돌한다. 먼저 온 것을 남긴다.
                    const bool duplicate { std::ranges::any_of(commands, [&command](const custom_command& existing) { return existing.id == command->id; }) };
                    if (duplicate)
                    {
                        sink.add(diagnostic_code::invalid_command_definition, u8"명령 id가 중복됩니다.", index_pointer(custom_pointer, index));
                        continue;
                    }
                    commands.push_back(std::move(*command));
                }
                result.overrides.custom = { std::move(commands) };
            }
        }

        if (const auto hooks { source.find("svn_hooks") }; hooks != source.end() && hooks->is_null() == false)
        {
            const std::u8string hooks_pointer { field_pointer(pointer_prefix, u8"svn_hooks") };
            if (hooks->is_object() == false)
                sink.add(diagnostic_code::invalid_command_definition, u8"svn_hooks는 object여야 합니다.", hooks_pointer);
            else
            {
                svn_hook_settings settings {};
                read_boolean(*hooks, "use_tortoise_svn", hooks_pointer, sink, settings.use_tortoise_svn);
                if (const auto list { hooks->find("hooks") }; list != hooks->end() && list->is_null() == false)
                {
                    const std::u8string list_pointer { field_pointer(hooks_pointer, u8"hooks") };
                    if (list->is_array() == false)
                        sink.add(diagnostic_code::invalid_command_definition, u8"hooks는 배열이어야 합니다.", list_pointer);
                    else
                        for (std::size_t index = 0; index < list->size(); ++index)
                        {
                            std::optional<svn_hook> hook { parse_svn_hook((*list)[index], index_pointer(list_pointer, index), sink) };
                            if (hook.has_value())
                                settings.hooks.push_back(std::move(*hook));
                        }
                }
                result.overrides.svn_hooks = { std::move(settings) };
            }
        }
        return result;
    }

    bool write_command_catalog_overrides(nlohmann::ordered_json& target, const command_catalog_overrides& overrides)
    {
        if (target.is_object() == false)
            target = json::object();

        // 정의되지 않은 항목은 지운다. 남겨 두면 다음에 열 때 override로 되살아난다
        // (json_project_store의 write_settings와 같은 규칙).
        if (overrides.custom.has_value())
        {
            json commands { json::array() };
            for (const custom_command& command : *overrides.custom)
                commands.push_back(custom_command_json(command));
            target["custom"] = std::move(commands);
        }
        else
            target.erase("custom");

        if (overrides.svn_hooks.has_value())
            target["svn_hooks"] = svn_hook_settings_json(*overrides.svn_hooks);
        else
            target.erase("svn_hooks");

        return target.empty() == false;
    }

    void write_command_catalog(nlohmann::ordered_json& target, const command_catalog& catalog)
    {
        if (target.is_object() == false)
            target = json::object();

        json commands { json::array() };
        for (const custom_command& command : catalog.custom)
            commands.push_back(custom_command_json(command));
        target["custom"] = std::move(commands);
        target["svn_hooks"] = svn_hook_settings_json(catalog.svn_hooks);
    }
} // namespace gitman
