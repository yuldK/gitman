#include "infrastructure/json_workspace_document.h"

#include "domain/path_syntax.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

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
            return field == "schema_version" || field == "settings" || field == "window" || field == "appearance" || field == "fonts" || field == "projects" || field == "function_buttons";
        }

        // 기능 버튼 절(function-buttons-design 3)이다. 진단은 모두 warning이고 위치를
        // 문구에 담는다. 띠에서는 알림 하나로 묶이고 이 문구가 tooltip 상세가 된다 (3.8).
        // 모델은 문서 순서를 지키려고 키 순서를 보존하는 ordered_json으로 읽는다.
        using ordered_json = nlohmann::ordered_json;

        std::u8string append_pointer_token(std::u8string pointer, const std::string_view token)
        {
            pointer.push_back(u8'/');
            pointer.append(escape_json_pointer_token(token));
            return pointer;
        }

        std::u8string append_pointer_index(std::u8string pointer, const std::size_t index)
        {
            pointer.push_back(u8'/');
            append_ascii_number(pointer, index);
            return pointer;
        }

        void add_function_diagnostic(workspace_document_parse_result& result, const diagnostic_code code, const std::u8string_view location, const std::u8string_view reason
            , const std::u8string_view document_path, std::u8string json_pointer
        )
        {
            std::u8string message { location };
            message.append(u8": ");
            message.append(reason);
            add_diagnostic(result, code, diagnostic_severity::warning, message, document_path, std::move(json_pointer));
        }

        std::u8string dump_source(const ordered_json& value)
        {
            return as_utf8(value.dump());
        }

        std::size_t code_point_count(const std::u8string_view text) noexcept
        {
            return static_cast<std::size_t>(std::ranges::count_if(text
                , [](const char8_t value)
                {
                    return (value & 0xc0) != 0x80;
                }
            ));
        }

        // 버튼 진단의 위치 글이다. 사용자가 알아보도록 label이 있으면 붙인다 (3.8).
        std::u8string function_button_location(const std::size_t index, const ordered_json& source)
        {
            std::u8string location { u8"function_buttons.buttons[" };
            append_ascii_number(location, index);
            location.push_back(u8']');
            if (source.is_object() == false)
                return location;

            const auto label { source.find("label") };
            if (label != source.end() && label->is_string() && label->get_ref<const std::string&>().empty() == false)
            {
                location.append(u8" (\"");
                location.append(as_utf8(label->get_ref<const std::string&>()));
                location.append(u8"\")");
            }
            return location;
        }

        // 변수 하나를 읽는다. 쓸 수 없는 변수도 원문 보존을 위해 모델에 남기고 경고만 한다.
        // 쓸 수 있는지는 is_usable_function_variable이 같은 규칙으로 다시 판정한다 (3.4).
        function_variable parse_function_variable(const std::string& name, const ordered_json& value, const std::u8string_view location, std::u8string pointer
            , workspace_document_parse_result& result, const std::u8string_view document_path
        )
        {
            function_variable variable {};
            variable.name = as_utf8(name);
            if (value.is_string())
                variable.value = as_utf8(value.get_ref<const std::string&>());
            else
                variable.value = function_source_text { dump_source(value) };

            std::u8string variable_location { location };
            variable_location.push_back(u8'.');
            variable_location.append(variable.name);
            if (value.is_string() == false)
            {
                add_function_diagnostic(result, diagnostic_code::invalid_function_variable, variable_location
                    , u8"변수 값은 문자열이어야 합니다. 이 변수를 쓰지 않습니다."
                    , document_path, std::move(pointer)
                );
            }
            else if (is_function_variable_name(variable.name) == false)
            {
                add_function_diagnostic(result, diagnostic_code::invalid_function_variable, variable_location
                    , u8"변수 이름은 영문자·숫자·_로 쓰고 숫자로 시작할 수 없습니다. 이 변수를 쓰지 않습니다."
                    , document_path, std::move(pointer)
                );
            }
            else if (is_reserved_variable_name(variable.name))
            {
                add_function_diagnostic(result, diagnostic_code::invalid_function_variable, variable_location
                    , u8"내장 변수 이름과 env는 사용자 변수 이름으로 쓸 수 없습니다. 이 변수를 쓰지 않습니다."
                    , document_path, std::move(pointer)
                );
            }
            return variable;
        }

        std::vector<function_variable> parse_function_variable_object(const ordered_json& source, const std::u8string_view location, const std::u8string_view pointer
            , workspace_document_parse_result& result, const std::u8string_view document_path
        )
        {
            std::vector<function_variable> variables {};
            for (auto field = source.begin(); field != source.end(); ++field)
                variables.push_back(parse_function_variable(field.key(), field.value(), location, append_pointer_token(std::u8string { pointer }, field.key()), result, document_path));
            return variables;
        }

        bool has_project(const std::span<const project_definition> projects, const std::u8string_view id) noexcept
        {
            return std::ranges::any_of(projects
                , [id](const project_definition& project)
                {
                    return project.id.value == id;
                }
            );
        }

        // 정적 검사(3.7)다. 문법과 이름 존재만 본다. `${env:}`는 클릭 시점에 본다.
        // 문제가 없으면 빈 글이다.
        std::u8string check_variable_references(const std::u8string_view field, const std::u8string_view text, const std::unordered_set<std::u8string>& user_variable_names)
        {
            const variable_scan scan { scan_variable_references(text) };
            if (scan.error.has_value())
            {
                std::u8string reason { field };
                if (scan.error->kind == expansion_error_kind::unterminated)
                    reason.append(u8"의 변수 문법이 잘못되었습니다: ${가 닫히지 않았습니다.");
                else
                    reason.append(u8"의 변수 문법이 잘못되었습니다: 변수 이름이 비었습니다.");
                return reason;
            }

            for (const variable_reference& reference : scan.references)
            {
                if (reference.environment || is_builtin_variable_name(reference.name) || user_variable_names.contains(reference.name))
                    continue;
                std::u8string reason { field };
                reason.append(u8"에서 정의되지 않은 변수를 씁니다: ${");
                reason.append(reference.name);
                reason.push_back(u8'}');
                return reason;
            }
            return {};
        }

        // 변수가 없는 program은 읽을 때 허용 목록을 본다 (4.3). 변수가 든 경로는 값이 클릭
        // 시점에 정해지므로 그때 본다. `$${`는 글자로 풀린 뒤의 경로를 본다. 문법 오류는
        // 앞선 정적 검사가 이미 걸렀다.
        std::u8string check_static_program(const std::u8string_view program)
        {
            if (scan_variable_references(program).references.empty() == false)
                return {};
            const variable_expansion literal { expand_variables(program, variable_lookup {}) };
            if (literal.succeeded() == false)
                return {};
            const std::optional<function_program_rejection> rejection { check_function_program(literal.text) };
            if (rejection.has_value() == false)
                return {};
            return function_program_rejection_message(*rejection);
        }

        bool read_string_array(const ordered_json& source, std::vector<std::u8string>& target)
        {
            if (source.is_array() == false)
                return false;
            for (const ordered_json& element : source)
            {
                if (element.is_string() == false)
                    return false;
                target.push_back(as_utf8(element.get_ref<const std::string&>()));
            }
            return true;
        }

        bool is_known_function_button_field(const std::string_view field) noexcept
        {
            constexpr std::array known_fields {
                std::string_view { "id" },
                std::string_view { "label" },
                std::string_view { "icon" },
                std::string_view { "program" },
                std::string_view { "args" },
                std::string_view { "cwd" },
                std::string_view { "mode" },
                std::string_view { "vcs" },
                std::string_view { "project_ids" },
                std::string_view { "confirm" },
                std::string_view { "timeout_seconds" },
                std::string_view { "placement" },
            };
            return std::ranges::find(known_fields, field) != known_fields.end();
        }

        // 버튼 항목 하나를 검증한다. 실패하면 그 사유를 돌려주고, 부르는 쪽은 항목을 원문째
        // 같은 자리에 둔다 (3.9). 항목마다 경고는 첫 사유 하나다.
        std::u8string read_function_button(const ordered_json& source, const std::unordered_set<std::u8string>& user_variable_names, function_button& button)
        {
            if (source.is_object() == false)
                return u8"기능 버튼 항목은 object여야 합니다.";

            const auto id { source.find("id") };
            if (id == source.end())
                return u8"id가 필요합니다.";
            if (id->is_string() == false || is_function_button_id(as_utf8(id->get_ref<const std::string&>())) == false)
                return u8"id는 영문 소문자·숫자·_·-로 64자까지 쓴 문자열이어야 합니다.";
            button.id = as_utf8(id->get_ref<const std::string&>());

            const auto label { source.find("label") };
            if (label == source.end())
                return u8"label이 필요합니다.";
            if (label->is_string() == false || label->get_ref<const std::string&>().empty())
                return u8"label은 비지 않은 문자열이어야 합니다.";
            button.label = as_utf8(label->get_ref<const std::string&>());

            if (const auto icon { source.find("icon") }; icon != source.end())
            {
                if (icon->is_string() == false)
                    return u8"icon은 문자열이어야 합니다.";
                button.icon = { as_utf8(icon->get_ref<const std::string&>()) };
            }

            const auto program { source.find("program") };
            if (program == source.end())
                return u8"program이 필요합니다.";
            if (program->is_string() == false || program->get_ref<const std::string&>().empty())
                return u8"program은 비지 않은 문자열이어야 합니다.";
            button.program = as_utf8(program->get_ref<const std::string&>());

            if (const auto args { source.find("args") }; args != source.end() && read_string_array(*args, button.args) == false)
                return u8"args는 문자열 배열이어야 합니다.";

            if (const auto cwd { source.find("cwd") }; cwd != source.end())
            {
                if (cwd->is_string() == false)
                    return u8"cwd는 문자열이어야 합니다.";
                button.cwd = { as_utf8(cwd->get_ref<const std::string&>()) };
            }

            if (const auto mode { source.find("mode") }; mode != source.end())
            {
                const std::optional<function_run_mode> parsed { mode->is_string() ? parse_function_run_mode(as_utf8(mode->get_ref<const std::string&>())) : std::nullopt };
                if (parsed.has_value() == false)
                    return u8"mode는 capture 또는 detached여야 합니다.";
                button.mode = *parsed;
            }

            if (const auto vcs { source.find("vcs") }; vcs != source.end())
            {
                const std::optional<function_vcs_scope> parsed { vcs->is_string() ? parse_function_vcs_scope(as_utf8(vcs->get_ref<const std::string&>())) : std::nullopt };
                if (parsed.has_value() == false)
                    return u8"vcs는 git 또는 svn이어야 합니다.";
                button.vcs = *parsed;
            }

            if (const auto project_ids { source.find("project_ids") }; project_ids != source.end())
            {
                std::vector<std::u8string> ids {};
                if (read_string_array(*project_ids, ids) == false)
                    return u8"project_ids는 문자열 배열이어야 합니다.";
                button.project_ids = { std::move(ids) };
            }

            if (const auto confirm { source.find("confirm") }; confirm != source.end())
            {
                if (confirm->is_boolean() == false)
                    return u8"confirm은 true 또는 false여야 합니다.";
                button.confirm = confirm->get<bool>();
            }

            if (const auto timeout { source.find("timeout_seconds") }; timeout != source.end())
            {
                bool in_range { false };
                if (timeout->is_number_unsigned())
                    in_range = timeout->get<std::uint64_t>() <= static_cast<std::uint64_t>(maximum_function_timeout_seconds);
                else if (timeout->is_number_integer())
                    in_range = timeout->get<std::int64_t>() >= 0 && timeout->get<std::int64_t>() <= maximum_function_timeout_seconds;
                if (in_range == false)
                    return u8"timeout_seconds는 0~86400 사이의 정수여야 합니다.";
                button.timeout_seconds = static_cast<std::int32_t>(timeout->get<std::int64_t>());
            }

            // 배치는 어떤 값이든 버튼을 건너뛰지 않는다 (3.2). 문자열이 아니면 원문 JSON을
            // 알 수 없는 값으로 들고, 쓰기 함수(T10)가 template의 원문 값을 되살린다.
            if (const auto placement { source.find("placement") }; placement != source.end())
                button.placement = { placement->is_string() ? as_utf8(placement->get_ref<const std::string&>()) : dump_source(*placement) };

            if (std::u8string reason { check_variable_references(u8"program", button.program, user_variable_names) }; reason.empty() == false)
                return reason;
            if (std::u8string reason { check_static_program(button.program) }; reason.empty() == false)
                return reason;
            for (std::size_t index = 0; index < button.args.size(); ++index)
            {
                std::u8string field { u8"args[" };
                append_ascii_number(field, index);
                field.push_back(u8']');
                if (std::u8string reason { check_variable_references(field, button.args[index], user_variable_names) }; reason.empty() == false)
                    return reason;
            }
            if (button.cwd.has_value())
            {
                if (std::u8string reason { check_variable_references(u8"cwd", *button.cwd, user_variable_names) }; reason.empty() == false)
                    return reason;
            }
            return {};
        }

        // 통과한 항목이 그대로 쓰이되 알려 둘 것을 경고한다.
        void warn_function_button_details(const ordered_json& source, const function_button& button, const std::u8string_view location, const std::u8string_view pointer
            , const std::span<const project_definition> projects, workspace_document_parse_result& result, const std::u8string_view document_path
        )
        {
            if (code_point_count(button.label) > maximum_function_label_length)
            {
                add_function_diagnostic(result, diagnostic_code::function_button_warning, location
                    , u8"label이 40자를 넘습니다. 넘치는 글은 말줄임됩니다."
                    , document_path, append_pointer_token(std::u8string { pointer }, "label")
                );
            }

            if (button.project_ids.has_value())
            {
                for (std::size_t index = 0; index < button.project_ids->size(); ++index)
                {
                    const std::u8string& id { (*button.project_ids)[index] };
                    if (has_project(projects, id))
                        continue;
                    // 검증에 실패한 project도 이 목록에 없으므로 "문서에 없다"고 하지 않는다.
                    std::u8string reason { u8"카드 목록에 없는 project id입니다: " };
                    reason.append(id);
                    add_function_diagnostic(result, diagnostic_code::function_button_warning, location, reason
                        , document_path, append_pointer_index(append_pointer_token(std::u8string { pointer }, "project_ids"), index)
                    );
                }
            }

            // 값은 모델에 그대로 둔다. 쓰기 함수(T10)가 모델 값을 다시 쓰므로 여기서 기본값으로
            // 바꾸면 무관한 저장에도 파일 값이 바뀐다. detached 실행(T6)이 이 값을 쓰지 않는다.
            if (button.mode == function_run_mode::detached && source.contains("timeout_seconds"))
            {
                add_function_diagnostic(result, diagnostic_code::function_button_warning, location
                    , u8"detached 실행에는 timeout_seconds를 쓰지 않습니다."
                    , document_path, append_pointer_token(std::u8string { pointer }, "timeout_seconds")
                );
            }

            for (auto field = source.begin(); field != source.end(); ++field)
            {
                if (is_known_function_button_field(field.key()))
                    continue;
                add_function_diagnostic(result, diagnostic_code::unknown_field, location
                    , u8"알 수 없는 필드를 보존합니다."
                    , document_path, append_pointer_token(std::u8string { pointer }, field.key())
                );
            }
        }

        bool applies_to_project(const function_button& button, const project_definition& project) noexcept
        {
            if (button.project_ids.has_value() && std::ranges::find(*button.project_ids, project.id.value) == button.project_ids->end())
                return false;
            // `vcs_hint: auto`는 조회 전까지 종류를 모르므로 적용되는 것으로 센다 (B4).
            switch (button.vcs)
            {
            case function_vcs_scope::any:
                return true;
            case function_vcs_scope::git:
                return project.hint != vcs_hint::subversion;
            case function_vcs_scope::subversion:
                return project.hint != vcs_hint::git;
            }
            return true;
        }

        // 문서 전체를 봐야 하는 경고다. 4번째 card와 알 수 없는 배치는 bar로 보이고 (3.2),
        // 카드당 적용 기능이 20개를 넘으면 그 카드에 21번째로 적용되는 항목에 경고 하나를 단다 (B6).
        void warn_function_button_layout(const std::span<const std::u8string> locations, const std::span<const function_button_entry> buttons
            , const std::span<const project_definition> projects, workspace_document_parse_result& result, const std::u8string_view document_path
        )
        {
            const std::vector<std::optional<function_placement>> visible { resolve_visible_placements(buttons) };
            for (std::size_t index = 0; index < buttons.size(); ++index)
            {
                const function_button* const button { std::get_if<function_button>(&buttons[index]) };
                if (button == nullptr)
                    continue;

                const std::optional<function_placement> declared { declared_function_placement(*button) };
                std::u8string_view reason {};
                if (declared.has_value() == false)
                    reason = u8"placement는 card·bar·menu 중 하나여야 합니다. 기능 줄(bar)에 둡니다.";
                else if (*declared == function_placement::card && visible[index] != function_placement::card)
                    reason = u8"card 배치는 문서 전체에서 3개까지입니다. 기능 줄(bar)에 둡니다.";
                if (reason.empty())
                    continue;
                add_function_diagnostic(result, diagnostic_code::function_button_warning, locations[index], reason
                    , document_path, append_pointer_token(append_pointer_index(u8"/function_buttons/buttons", index), "placement")
                );
            }

            for (const project_definition& project : projects)
            {
                std::size_t applied { 0 };
                for (std::size_t index = 0; index < buttons.size(); ++index)
                {
                    const function_button* const button { std::get_if<function_button>(&buttons[index]) };
                    if (button == nullptr || applies_to_project(*button, project) == false)
                        continue;
                    if (++applied != maximum_functions_per_card + 1)
                        continue;

                    std::u8string reason { project.id.value };
                    reason.append(u8" 카드에 적용되는 기능이 20개를 넘습니다.");
                    add_function_diagnostic(result, diagnostic_code::function_button_warning, locations[index], reason
                        , document_path, append_pointer_index(u8"/function_buttons/buttons", index)
                    );

                    break;
                }
            }
        }

        // 신뢰 해시 입력이다 (3.10). 표시 필드를 뺀 원문을 다시 직렬화한다. 키를 정렬하는
        // json으로 만들어, gitman 저장이 키 순서를 바꿔 써도 같은 정의면 같은 입력이 된다.
        std::u8string function_digest_input(const json& section)
        {
            json digest { section };
            if (const auto buttons { digest.find("buttons") }; buttons != digest.end() && buttons->is_array())
            {
                for (json& item : *buttons)
                {
                    if (item.is_object() == false)
                        continue;
                    item.erase("label");
                    item.erase("icon");
                    item.erase("placement");
                }
            }
            return as_utf8(digest.dump());
        }

        // `function_buttons`를 읽는다 (3.8, 3.9). 잘못된 항목은 그 항목만 경고하고 실행과
        // 화면에서 빠지지만 원문은 모델의 같은 자리에 남는다. project의 is_valid는 건드리지
        // 않는다. 절 자체가 object가 아니면 경고 하나를 내고 기능 버튼 없이 연다.
        std::optional<function_buttons_definition> parse_function_buttons(const json& root, const std::string_view source_bytes, const std::span<const project_definition> projects
            , workspace_document_parse_result& result, const std::u8string_view document_path
        )
        {
            const auto section { root.find("function_buttons") };
            if (section == root.end() || section->is_null())
                return std::nullopt;
            if (section->is_object() == false)
            {
                add_function_diagnostic(result, diagnostic_code::invalid_function_buttons, u8"function_buttons"
                    , u8"object여야 합니다. 기능 버튼 없이 엽니다."
                    , document_path, u8"/function_buttons"
                );
                return std::nullopt;
            }

            // 같은 원문을 키 순서를 지키며 다시 읽는다. 앞의 파싱이 성공했으므로 실패하지 않는다.
            const ordered_json ordered_root { ordered_json::parse(source_bytes.begin(), source_bytes.end(), nullptr, false) };
            const ordered_json& source { ordered_root.at("function_buttons") };

            function_buttons_definition definition {};
            definition.source_digest_input = function_digest_input(*section);

            for (auto field = source.begin(); field != source.end(); ++field)
            {
                const std::string& key { field.key() };
                if (key == "variables" || key == "project_variables" || key == "buttons")
                    continue;
                std::u8string location { u8"function_buttons." };
                location.append(as_utf8(key));
                add_function_diagnostic(result, diagnostic_code::unknown_field, location, u8"알 수 없는 필드를 보존합니다.", document_path, append_pointer_token(u8"/function_buttons", key));
            }

            if (const auto variables { source.find("variables") }; variables != source.end() && variables->is_null() == false)
            {
                if (variables->is_object())
                    definition.variables = parse_function_variable_object(*variables, u8"function_buttons.variables", u8"/function_buttons/variables", result, document_path);
                else
                {
                    add_function_diagnostic(result, diagnostic_code::invalid_function_buttons, u8"function_buttons.variables"
                        , u8"object여야 합니다. 문서 변수 없이 읽습니다."
                        , document_path, u8"/function_buttons/variables"
                    );
                }
            }

            if (const auto overrides { source.find("project_variables") }; overrides != source.end() && overrides->is_null() == false)
            {
                if (overrides->is_object())
                {
                    for (auto field = overrides->begin(); field != overrides->end(); ++field)
                    {
                        function_project_variables entry {};
                        entry.project = as_utf8(field.key());
                        std::u8string location { u8"function_buttons.project_variables." };
                        location.append(entry.project);
                        std::u8string pointer { append_pointer_token(u8"/function_buttons/project_variables", field.key()) };
                        if (field.value().is_object() == false)
                        {
                            entry.variables = function_source_text { dump_source(field.value()) };
                            add_function_diagnostic(result, diagnostic_code::invalid_function_variable, location
                                , u8"object여야 합니다. 이 저장소의 덮어쓰기를 쓰지 않습니다."
                                , document_path, std::move(pointer)
                            );
                        }
                        else
                        {
                            if (has_project(projects, entry.project) == false)
                                add_function_diagnostic(result, diagnostic_code::function_button_warning, location, u8"카드 목록에 없는 project id입니다.", document_path, pointer);
                            entry.variables = parse_function_variable_object(field.value(), location, pointer, result, document_path);
                        }
                        definition.project_variables.push_back(std::move(entry));
                    }
                }
                else
                {
                    add_function_diagnostic(result, diagnostic_code::invalid_function_buttons, u8"function_buttons.project_variables"
                        , u8"object여야 합니다. 저장소별 덮어쓰기 없이 읽습니다."
                        , document_path, u8"/function_buttons/project_variables"
                    );
                }
            }

            // 정적 검사가 받는 사용자 변수 이름이다. 문서 변수와, 카드로 읽은 어느 한 project의
            // 덮어쓰기 중 쓸 수 있는 것이다 (3.7). 카드 목록에 없는 project의 덮어쓰기는 어느
            // 카드에서도 풀리지 않으므로 세지 않는다. 쓸 수 없는 변수를 참조하는 버튼은 여기서 빠진다.
            std::unordered_set<std::u8string> user_variable_names {};
            for (const function_variable& variable : definition.variables)
            {
                if (is_usable_function_variable(variable))
                    user_variable_names.insert(variable.name);
            }
            for (const function_project_variables& entry : definition.project_variables)
            {
                const auto* const variables { std::get_if<std::vector<function_variable>>(&entry.variables) };
                if (variables == nullptr || has_project(projects, entry.project) == false)
                    continue;
                for (const function_variable& variable : *variables)
                {
                    if (is_usable_function_variable(variable))
                        user_variable_names.insert(variable.name);
                }
            }

            const auto buttons { source.find("buttons") };
            if (buttons == source.end() || buttons->is_null())
                return definition;
            if (buttons->is_array() == false)
            {
                add_function_diagnostic(result, diagnostic_code::invalid_function_buttons, u8"function_buttons.buttons"
                    , u8"배열이어야 합니다. 기능 버튼 없이 읽습니다."
                    , document_path, u8"/function_buttons/buttons"
                );
                return definition;
            }

            // 문서 전체를 보는 배치·개수 경고도 같은 위치 글을 쓰므로 항목마다 한 번 만들어 둔다.
            std::vector<std::u8string> locations {};
            locations.reserve(buttons->size());
            std::unordered_set<std::u8string> accepted_ids {};
            for (std::size_t index = 0; index < buttons->size(); ++index)
            {
                const ordered_json& item { (*buttons)[index] };
                const std::u8string& location { locations.emplace_back(function_button_location(index, item)) };
                const std::u8string pointer { append_pointer_index(u8"/function_buttons/buttons", index) };

                function_button button {};
                std::u8string reason { read_function_button(item, user_variable_names, button) };
                // 중복 id는 앞의 항목을 살리고 뒤의 항목을 건너뛴다 (REQ-001 선례).
                if (reason.empty() && accepted_ids.contains(button.id))
                    reason = u8"같은 id의 기능 버튼이 앞에 있습니다.";
                if (reason.empty() == false)
                {
                    reason.append(u8" 이 기능 버튼을 쓰지 않습니다.");
                    add_function_diagnostic(result, diagnostic_code::invalid_function_button, location, reason, document_path, pointer);
                    definition.buttons.push_back(function_source_text { dump_source(item) });
                    continue;
                }

                accepted_ids.insert(button.id);
                warn_function_button_details(item, button, location, pointer, projects, result, document_path);
                definition.buttons.push_back(std::move(button));
            }
            warn_function_button_layout(locations, definition.buttons, projects, result, document_path);
            return definition;
        }

        // 외양은 표시 설정이라 어떤 오류도 문서 열기를 막지 않는다. 읽을 수 없는
        // 항목은 경고 하나를 남기고 앱 설정을 따른다
        // (settings-tabs-and-appearance-scope-design S2.2).
        // 글꼴 이름 하나를 읽는다. 값이 없으면 "앱 설정 따름"이라 대상을 건드리지
        // 않고, 빈 문자열은 "내장 글꼴을 쓴다"는 정의라 그대로 받는다.
        void read_font_family(const json& source, const char* const key, const std::u8string_view pointer, workspace_document_parse_result& result, const std::u8string_view document_path,
            std::optional<std::u8string>& target)
        {
            const auto field { source.find(key) };
            if (field == source.end() || field->is_null())
                return;
            if (field->is_string() == false)
            {
                add_diagnostic(
                    result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"fonts의 값은 문자열이어야 합니다. 앱 설정을 따릅니다.", document_path, std::u8string { pointer });
                return;
            }
            target = { as_utf8(field->get_ref<const std::string&>()) };
        }

        // 글꼴도 표시 설정이라 어떤 오류도 문서 열기를 막지 않는다 (2026-08-23
        // 지시). 빈 문자열은 "이 문서는 내장 글꼴을 쓴다"는 **정의**다 — 앱 설정을
        // 따르는 것과 구별해야 하므로 값이 비었다고 버리지 않는다.
        font_overrides parse_fonts(const json& root, workspace_document_parse_result& result, const std::u8string_view document_path)
        {
            font_overrides fonts {};
            const auto source { root.find("fonts") };
            if (source == root.end() || source->is_null())
                return fonts;

            if (source->is_object() == false)
            {
                add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"fonts는 object여야 합니다. 앱 설정의 글꼴을 사용합니다.", document_path, u8"/fonts");
                return fonts;
            }

            read_font_family(*source, "ui", u8"/fonts/ui", result, document_path, fonts.ui_family);
            read_font_family(*source, "code", u8"/fonts/code", result, document_path, fonts.code_family);
            return fonts;
        }

        appearance_overrides parse_appearance(const json& root, workspace_document_parse_result& result, const std::u8string_view document_path)
        {
            appearance_overrides appearance {};
            const auto source { root.find("appearance") };
            if (source == root.end() || source->is_null())
                return appearance;

            if (source->is_object() == false)
            {
                add_diagnostic(
                    result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"appearance는 object여야 합니다. 앱 설정의 외양을 사용합니다.", document_path, u8"/appearance");
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
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"settings의 제한 시간은 정수(초)여야 합니다. 기본값을 사용합니다.", document_path,
                        settings_field_pointer("query_timeout_seconds"));
                }
                else if (const std::int64_t seconds { timeout->get<std::int64_t>() }; seconds < minimum_query_timeout_seconds || seconds > maximum_query_timeout_seconds)
                {
                    add_diagnostic(result, diagnostic_code::invalid_project_field, diagnostic_severity::warning, u8"settings의 제한 시간은 10~3600초여야 합니다. 기본값을 사용합니다.", document_path,
                        settings_field_pointer("query_timeout_seconds"));
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
        document.fonts = parse_fonts(root, result, document_path);
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
        // project id 경고를 위해 project 다음에 읽는다.
        document.function_buttons = parse_function_buttons(root, source_bytes, document.projects, result, document_path);
        result.document = std::move(document);
        return result;
    }
} // namespace gitman
