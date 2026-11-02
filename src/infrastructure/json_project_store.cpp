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
#include <unordered_set>
#include <utility>
#include <variant>
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

        // 기존 `settings` object를 template으로 삼아 알 수 없는 키를 보존한다. 문서가
        // 덮어쓴(정의된) 키만 남기고, 정의를 거둔 키는 지운다 — 남겨 두면 다음
        // 열기에서 override로 되살아난다 (global-settings-and-ui-fixes-design G3.1).
        void write_settings(json& root, const workspace_settings_overrides& settings)
        {
            const auto existing { root.find("settings") };
            const bool had_settings { existing != root.end() && existing->is_object() };
            if (had_settings == false && settings.empty())
            {
                if (existing != root.end())
                    root.erase("settings");
                return;
            }

            json value { had_settings ? *existing : json::object() };
            const auto set_or_erase = [&value](const char* const key, const auto& field, const auto& to_json) {
                if (field.has_value())
                    value[key] = to_json(*field);
                else
                    value.erase(key);
            };
            const auto as_is = [](const auto& raw) { return raw; };
            set_or_erase("git_executable", settings.git_executable, [](const std::u8string& raw) { return as_string(raw); });
            set_or_erase("svn_executable", settings.svn_executable, [](const std::u8string& raw) { return as_string(raw); });
            set_or_erase("show_relative_paths", settings.show_relative_paths, as_is);
            set_or_erase("update_submodules", settings.update_submodules, as_is);
            set_or_erase("ignore_local_changes", settings.ignore_local_changes, as_is);
            set_or_erase("write_log_files", settings.write_log_files, as_is);
            set_or_erase("query_timeout_seconds", settings.query_timeout_seconds, as_is);

            // 알 수 없는 키만 남았어도 보존하고, 아무것도 남지 않으면 object 자체를
            // 만들지 않는다.
            if (value.empty())
                root.erase("settings");
            else
                root["settings"] = std::move(value);
        }

        // 외양도 `settings`와 같은 규칙이다 — 정의된 키만 남기고, 정의를 거둔 키는
        // 지우며, 알 수 없는 키는 보존한다
        // (settings-tabs-and-appearance-scope-design S2.2).
        void write_appearance(json& root, const appearance_overrides& appearance)
        {
            const auto existing { root.find("appearance") };
            const bool had_appearance { existing != root.end() && existing->is_object() };
            if (had_appearance == false && appearance.empty())
            {
                if (existing != root.end())
                    root.erase("appearance");
                return;
            }

            json value { had_appearance ? *existing : json::object() };
            if (appearance.theme.has_value())
                value["theme"] = as_string(theme_preference_name(*appearance.theme));
            else
                value.erase("theme");
            if (appearance.accent_id.has_value())
                value["accent"] = as_string(*appearance.accent_id);
            else
                value.erase("accent");

            if (value.empty())
                root.erase("appearance");
            else
                root["appearance"] = std::move(value);
        }

        // 글꼴도 `appearance`와 같은 규칙이다 (2026-08-23 지시). **빈 문자열도
        // 정의다** — "이 문서는 내장 글꼴을 쓴다"는 뜻이라 키를 남긴다.
        void write_fonts(json& root, const font_overrides& fonts)
        {
            const auto existing { root.find("fonts") };
            const bool had_fonts { existing != root.end() && existing->is_object() };
            if (had_fonts == false && fonts.empty())
            {
                if (existing != root.end())
                    root.erase("fonts");
                return;
            }

            json value { had_fonts ? *existing : json::object() };
            if (fonts.ui_family.has_value())
                value["ui"] = as_string(*fonts.ui_family);
            else
                value.erase("ui");
            if (fonts.code_family.has_value())
                value["code"] = as_string(*fonts.code_family);
            else
                value.erase("code");

            if (value.empty())
                root.erase("fonts");
            else
                root["fonts"] = std::move(value);
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

        // 원문 JSON 글로 든 값(검증에 실패한 항목, 문자열이 아닌 변수 값)을 다시 값으로 되돌린다. 파서가 dump한 글이라
        // 늘 파싱되지만, 그렇지 않으면 글자 그대로 문자열로 둔다 — 원문이 사라지는 것보다 낫다.
        json parse_source_text(const std::u8string_view text)
        {
            const std::string_view bytes { as_bytes(text) };
            json value = json::parse(bytes.begin(), bytes.end(), nullptr, false);
            if (value.is_discarded())
                return json(as_string(text));
            return value;
        }

        json function_variables_json(const std::vector<function_variable>& variables)
        {
            // 쓸 수 없는 변수(이름 규칙 위반, 예약 이름, 문자열이 아닌 값)도 원문째 같은 자리에 쓴다 (3.9).
            json value = json::object();
            for (const function_variable& variable : variables)
            {
                const std::string name { as_string(variable.name) };
                if (const std::u8string* const text { std::get_if<std::u8string>(&variable.value) }; text != nullptr)
                    value[name] = as_string(*text);
                else
                    value[name] = parse_source_text(std::get<function_source_text>(variable.value).json);
            }
            return value;
        }

        json function_project_variables_json(const std::vector<function_project_variables>& entries)
        {
            json value = json::object();
            for (const function_project_variables& entry : entries)
            {
                const std::string project { as_string(entry.project) };
                if (const auto* const variables { std::get_if<std::vector<function_variable>>(&entry.variables) }; variables != nullptr)
                    value[project] = function_variables_json(*variables);
                else
                    value[project] = parse_source_text(std::get<function_source_text>(entry.variables).json);
            }
            return value;
        }

        // 통과한 버튼의 template이다 (3.9). 버튼 `id`가 열쇠이고 읽을 때의 원문 객체다 — 알 수 없는 키와 알 수 없는
        // `placement` 값을 여기서 되살린다. 같은 id의 객체가 여럿이면(중복 id는 뒤 항목이 실패 항목이 된다) 모델의
        // 실패 항목 원문과 같은 객체를 건너뛰어, 실패한 뒤 항목이 앞 항목의 template이 되지 않게 한다.
        std::unordered_map<std::string, json> function_button_templates(const json& buttons, const std::vector<function_button_entry>& entries)
        {
            std::unordered_map<std::string, json> templates {};
            if (buttons.is_array() == false)
                return templates;

            std::unordered_set<std::string> failed {};
            for (const function_button_entry& entry : entries)
            {
                if (const function_source_text* const source { std::get_if<function_source_text>(&entry) }; source != nullptr)
                    failed.insert(as_string(source->json));
            }

            std::unordered_map<std::string, json> fallbacks {};
            for (const json& item : buttons)
            {
                if (item.is_object() == false)
                    continue;
                const auto id { item.find("id") };
                if (id == item.end() || id->is_string() == false)
                    continue;
                const std::string& key { id->get_ref<const std::string&>() };
                fallbacks.try_emplace(key, item);
                if (failed.contains(item.dump()) == false)
                    templates.try_emplace(key, item);
            }
            // 같은 글의 객체가 통과 항목과 실패 항목에 모두 있으면 둘 다 건너뛰었다. 그때는 그 id의 첫 객체를 쓴다.
            for (auto& [key, item] : fallbacks)
                templates.try_emplace(key, std::move(item));
            return templates;
        }

        // 통과한 버튼 하나다. 알려진 필드는 모델 값으로 쓰고, 생략한 문서에 기본값 키를 새로 만들지 않는다 — 다시 읽은
        // 신뢰 해시 입력(3.10)이 쓰기 전과 같아야 재배치가 신뢰를 다시 묻지 않는다 (T7에서 넘어온 일).
        json function_button_json(json value, const function_button& button)
        {
            if (value.is_object() == false)
                value = json::object();
            const bool had_args { value.contains("args") };
            const bool had_mode { value.contains("mode") };
            const bool had_confirm { value.contains("confirm") };
            const bool had_timeout { value.contains("timeout_seconds") };

            value["id"] = as_string(button.id);
            value["label"] = as_string(button.label);
            if (button.icon.has_value())
                value["icon"] = as_string(*button.icon);
            else
                value.erase("icon");
            value["program"] = as_string(button.program);
            if (had_args || button.args.empty() == false)
            {
                json args = json::array();
                for (const std::u8string& argument : button.args)
                    args.push_back(as_string(argument));
                value["args"] = std::move(args);
            }
            if (button.cwd.has_value())
                value["cwd"] = as_string(*button.cwd);
            else
                value.erase("cwd");
            if (had_mode || button.mode != function_run_mode::capture)
                value["mode"] = as_string(function_run_mode_name(button.mode));
            if (button.vcs != function_vcs_scope::any)
                value["vcs"] = as_string(function_vcs_scope_name(button.vcs));
            else
                value.erase("vcs");
            if (button.project_ids.has_value())
            {
                json ids = json::array();
                for (const std::u8string& id : *button.project_ids)
                    ids.push_back(as_string(id));
                value["project_ids"] = std::move(ids);
            }
            else
                value.erase("project_ids");
            if (had_confirm || button.confirm)
                value["confirm"] = button.confirm;
            if (had_timeout || button.timeout_seconds != default_function_timeout_seconds)
                value["timeout_seconds"] = button.timeout_seconds;

            // 배치는 아는 값이면 모델 값이다. 알 수 없는 값(문자열이 아닌 원문 포함)은 template의 원문 값을 그대로 둔다 —
            // 다른 일로 저장해도 파일의 값이 바뀌지 않는다. 사용자가 배치 메뉴로 고르면 모델이 아는 값이 되어 그때 쓴다 (3.2).
            if (button.placement.has_value() == false)
                value.erase("placement");
            else if (parse_function_placement(*button.placement).has_value() || value.contains("placement") == false)
                value["placement"] = as_string(*button.placement);
            return value;
        }

        // `function_buttons`를 모델에서 다시 쓴다 (function-buttons-design 3.9, D12-A). 모델이 이해하지 못한 것은
        // 원문째 되살린다: 실패 항목은 같은 자리에, 버튼 안과 절 바로 아래의 알 수 없는 키는 template에서, 모델에 자리가
        // 없는 원문(object가 아닌 `variables`·`project_variables`, 배열이 아닌 `buttons`, `null`)은 그 키를 건드리지 않는다.
        // 모델에 절이 없으면(키가 없었거나 object가 아니었다) 원문 그대로다.
        void write_function_buttons(json& root, const std::optional<function_buttons_definition>& definition)
        {
            if (definition.has_value() == false)
                return;

            const auto existing { root.find("function_buttons") };
            json value = existing != root.end() && existing->is_object() ? *existing : json::object();

            const auto variables { value.find("variables") };
            if (variables == value.end() ? definition->variables.empty() == false : variables->is_object())
                value["variables"] = function_variables_json(definition->variables);

            const auto project_variables { value.find("project_variables") };
            if (project_variables == value.end() ? definition->project_variables.empty() == false : project_variables->is_object())
                value["project_variables"] = function_project_variables_json(definition->project_variables);

            const auto buttons { value.find("buttons") };
            if (buttons == value.end() ? definition->buttons.empty() == false : buttons->is_array())
            {
                std::unordered_map<std::string, json> templates { function_button_templates(buttons == value.end() ? json::array() : *buttons, definition->buttons) };
                json items = json::array();
                for (const function_button_entry& entry : definition->buttons)
                {
                    if (const function_button* const button { std::get_if<function_button>(&entry) }; button != nullptr)
                    {
                        const auto found { templates.find(as_string(button->id)) };
                        items.push_back(function_button_json(found == templates.end() ? json::object() : found->second, *button));
                    }
                    else
                        items.push_back(parse_source_text(std::get<function_source_text>(entry).json));
                }
                value["buttons"] = std::move(items);
            }

            root["function_buttons"] = std::move(value);
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
            write_appearance(root, document.appearance);
            write_fonts(root, document.fonts);
            write_window(root, document.window);
            write_function_buttons(root, document.function_buttons);
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
