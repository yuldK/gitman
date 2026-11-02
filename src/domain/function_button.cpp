#include "domain/function_button.h"

#include "domain/path_syntax.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"

#include <algorithm>
#include <array>

namespace gitman {
    namespace {
        constexpr std::array document_variable_names {
            std::u8string_view { u8"workspaceFolder" },
            std::u8string_view { u8"workspaceFile" },
            std::u8string_view { u8"projectRoot" },
            std::u8string_view { u8"projectId" },
            std::u8string_view { u8"projectName" },
        };

        constexpr std::array runtime_variable_names {
            std::u8string_view { u8"vcs" },
            std::u8string_view { u8"repositoryRoot" },
            std::u8string_view { u8"branch" },
            std::u8string_view { u8"svnUrl" },
            std::u8string_view { u8"revision" },
        };

        constexpr std::u8string_view environment_prefix { u8"env:" };

        constexpr bool is_ascii_digit(const char8_t value) noexcept
        {
            return value >= u8'0' && value <= u8'9';
        }

        constexpr bool is_ascii_letter(const char8_t value) noexcept
        {
            return (value >= u8'a' && value <= u8'z') || (value >= u8'A' && value <= u8'Z');
        }

        template <std::size_t count>
        bool contains_name(const std::array<std::u8string_view, count>& names, const std::u8string_view value) noexcept
        {
            return std::find(names.begin(), names.end(), value) != names.end();
        }

        // 경로 출처 변수는 `/`를 받지 못하는 소비자가 있어 `\`로 통일한다 (3.3).
        std::u8string with_backslashes(const std::u8string_view path)
        {
            std::u8string result { path };
            std::replace(result.begin(), result.end(), u8'/', u8'\\');
            return result;
        }

        std::optional<std::u8string> non_empty(std::u8string value)
        {
            if (value.empty())
                return std::nullopt;
            return value;
        }

        // 문서가 드라이브 루트에 있으면 부모가 `E:`로 남는다. 그대로 두면 드라이브 기준
        // 상대 경로라 cwd 절대 경로 검사에 걸리고 스크립트가 엉뚱한 폴더로 가므로 `\`를 붙인다.
        // `windows_parent_directory`는 배너 메뉴도 쓰므로 여기서만 보정한다.
        std::u8string workspace_folder(const std::u8string_view document_path)
        {
            std::u8string folder { with_backslashes(windows_parent_directory(document_path)) };
            if (folder.size() == 2
                && is_ascii_letter(folder[0])
                && folder[1] == u8':')
                folder.push_back(u8'\\');
            return folder;
        }

        constexpr char8_t ascii_lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value + (u8'a' - u8'A'));
            return value;
        }

        bool equal_ignoring_ascii_case(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            return left.size() == right.size()
                && std::equal(left.begin(), left.end(), right.begin()
                    , [](const char8_t first, const char8_t second)
                    {
                        return ascii_lower(first) == ascii_lower(second);
                    }
                );
        }

        // `\`와 `/`를 모두 구분자로 본다. `is_absolute_windows_path`가 `C:/`도 받기 때문이다.
        std::u8string_view last_path_component(const std::u8string_view path) noexcept
        {
            const std::size_t separator { path.find_last_of(u8"\\/") };
            if (separator == std::u8string_view::npos)
                return path;
            return path.substr(separator + 1);
        }

        // cmd가 `/c` 뒤의 명령줄을 다시 파싱할 때 뜻을 갖는 문자다 (4.3).
        constexpr std::u8string_view cmd_metacharacters { u8"%&|^<>\"\r\n" };

        // 원문을 글자 그대로의 조각과 참조로 나눈 결과다. `$${`는 이 단계에서 `${`로 바뀐다.
        struct template_token
        {
            bool reference { false };
            // 글자 그대로의 조각이다. 참조면 비어 있다.
            std::u8string literal {};
            std::u8string name {};
            bool environment { false };
            std::size_t offset { 0 };
        };

        struct tokenized_template
        {
            std::vector<template_token> tokens {};
            std::optional<expansion_error> error {};
        };

        tokenized_template tokenize(const std::u8string_view text)
        {
            tokenized_template result {};
            std::u8string literal {};
            const auto flush_literal { [&result, &literal]
                {
                    if (literal.empty())
                        return;
                    result.tokens.push_back({ false, std::move(literal), {}, false, 0 });
                    literal.clear();
                }
            };

            std::size_t index { 0 };
            while (index < text.size())
            {
                const std::u8string_view rest { text.substr(index) };
                if (rest.starts_with(u8"$${"))
                {
                    literal.append(u8"${");
                    index += 3;
                    continue;
                }
                if (rest.starts_with(u8"${") == false)
                {
                    literal.push_back(text[index]);
                    ++index;
                    continue;
                }

                const std::size_t close { text.find(u8'}', index + 2) };
                if (close == std::u8string_view::npos)
                {
                    result.error = { expansion_error_kind::unterminated, {}, index };
                    return result;
                }

                std::u8string_view name { text.substr(index + 2, close - index - 2) };
                const bool environment { name.starts_with(environment_prefix) };
                if (environment)
                    name.remove_prefix(environment_prefix.size());
                if (name.empty())
                {
                    result.error = { expansion_error_kind::empty_name, {}, index };
                    return result;
                }

                flush_literal();
                result.tokens.push_back({ true, {}, std::u8string { name }, environment, index });
                index = close + 1;
            }
            flush_literal();
            return result;
        }

        const variable_binding* find_builtin(const variable_lookup& lookup, const std::u8string_view name) noexcept
        {
            const auto found {
                std::find_if(lookup.builtins.begin(), lookup.builtins.end()
                    , [name](const variable_binding& binding)
                    {
                        return binding.name == name;
                    }
                )
            };
            return found == lookup.builtins.end() ? nullptr : &*found;
        }

        const user_variable* find_user(const variable_lookup& lookup, const std::u8string_view name) noexcept
        {
            const auto found {
                std::find_if(lookup.user.begin(), lookup.user.end()
                    , [name](const user_variable& variable)
                    {
                        return variable.name == name;
                    }
                )
            };
            return found == lookup.user.end() ? nullptr : &*found;
        }

        void append_segment(variable_expansion& expansion, const std::u8string_view text, const variable_origin origin, const std::u8string_view variable)
        {
            expansion.segments.push_back({ expansion.text.size(), text.size(), origin, std::u8string { variable } });
            expansion.text.append(text);
        }

        // 환경 변수와 내장 변수 참조 하나를 푼다. 사용자 변수는 부르는 쪽이 다룬다.
        // 다룰 수 없는 이름이면 false이고 `error`는 건드리지 않는다.
        bool append_environment_or_builtin(variable_expansion& expansion, const template_token& token, const variable_lookup& lookup, std::optional<expansion_error>& error)
        {
            if (token.environment)
            {
                std::optional<std::u8string> value {};
                if (lookup.environment)
                    value = lookup.environment(token.name);
                if (value.has_value() == false)
                {
                    error = { expansion_error_kind::environment_undefined, token.name, token.offset };
                    return true;
                }
                append_segment(expansion, *value, variable_origin::environment, token.name);
                return true;
            }

            const variable_binding* const builtin { find_builtin(lookup, token.name) };
            if (builtin == nullptr)
                return false;
            if (builtin->value.has_value() == false)
            {
                error = { expansion_error_kind::unresolved, token.name, token.offset };
                return true;
            }
            append_segment(expansion, *builtin->value, builtin->origin, token.name);
            return true;
        }

        // 사용자 변수 값을 한 단계 푼다 (3.4). 값의 글자는 문서 출처이고, 값 안의 사용자
        // 변수 참조는 미정의다. 오류 위치는 바깥 참조의 위치로 둔다.
        void append_user_variable(variable_expansion& expansion, const user_variable& variable, const std::size_t outer_offset, const variable_lookup& lookup, std::optional<expansion_error>& error)
        {
            tokenized_template inner { tokenize(variable.value) };
            if (inner.error.has_value())
            {
                error = { inner.error->kind, variable.name, outer_offset };
                return;
            }

            for (template_token& token : inner.tokens)
            {
                if (token.reference == false)
                {
                    append_segment(expansion, token.literal, variable_origin::document, variable.name);
                    continue;
                }

                token.offset = outer_offset;
                if (append_environment_or_builtin(expansion, token, lookup, error) == false)
                    error = { expansion_error_kind::undefined, token.name, outer_offset };
                if (error.has_value())
                    return;
            }
        }
    } // namespace

    std::u8string_view function_button::working_directory() const noexcept
    {
        if (cwd.has_value())
            return *cwd;
        return default_function_working_directory;
    }

    std::u8string_view function_button::icon_name() const noexcept
    {
        if (icon.has_value() == false || icon->empty())
            return default_function_icon;
        return *icon;
    }

    std::optional<function_placement> parse_function_placement(const std::u8string_view value) noexcept
    {
        if (value == u8"card")
            return function_placement::card;
        if (value == u8"bar")
            return function_placement::bar;
        if (value == u8"menu")
            return function_placement::menu;
        return std::nullopt;
    }

    std::u8string_view function_placement_name(const function_placement placement) noexcept
    {
        switch (placement)
        {
        case function_placement::card:
            return u8"card";
        case function_placement::bar:
            return u8"bar";
        case function_placement::menu:
            return u8"menu";
        }
        return u8"menu";
    }

    std::optional<function_run_mode> parse_function_run_mode(const std::u8string_view value) noexcept
    {
        if (value == u8"capture")
            return function_run_mode::capture;
        if (value == u8"detached")
            return function_run_mode::detached;
        return std::nullopt;
    }

    std::u8string_view function_run_mode_name(const function_run_mode mode) noexcept
    {
        switch (mode)
        {
        case function_run_mode::capture:
            return u8"capture";
        case function_run_mode::detached:
            return u8"detached";
        }
        return u8"capture";
    }

    std::optional<function_vcs_scope> parse_function_vcs_scope(const std::u8string_view value) noexcept
    {
        if (value == u8"git")
            return function_vcs_scope::git;
        if (value == u8"svn")
            return function_vcs_scope::subversion;
        return std::nullopt;
    }

    std::u8string_view function_vcs_scope_name(const function_vcs_scope scope) noexcept
    {
        switch (scope)
        {
        case function_vcs_scope::any:
            return u8"";
        case function_vcs_scope::git:
            return u8"git";
        case function_vcs_scope::subversion:
            return u8"svn";
        }
        return u8"";
    }

    std::optional<function_placement> declared_function_placement(const function_button& button) noexcept
    {
        if (button.placement.has_value() == false)
            return function_placement::menu;
        return parse_function_placement(*button.placement);
    }

    std::vector<std::optional<function_placement>> resolve_visible_placements(const std::span<const function_button_entry> buttons)
    {
        std::vector<std::optional<function_placement>> result {};
        result.reserve(buttons.size());
        std::size_t card_count { 0 };
        for (const function_button_entry& entry : buttons)
        {
            const function_button* const button { std::get_if<function_button>(&entry) };
            if (button == nullptr)
            {
                result.push_back(std::nullopt);
                continue;
            }

            function_placement placement { declared_function_placement(*button).value_or(function_placement::bar) };
            if (placement == function_placement::card)
            {
                if (card_count < maximum_card_function_buttons)
                    ++card_count;
                else
                    placement = function_placement::bar;
            }
            result.push_back(placement);
        }
        return result;
    }

    bool is_function_button_id(const std::u8string_view value) noexcept
    {
        if (value.empty() || value.size() > maximum_function_button_id_length)
            return false;
        return std::all_of(value.begin(), value.end()
            , [](const char8_t character)
            {
                return (character >= u8'a' && character <= u8'z') || is_ascii_digit(character) || character == u8'_' || character == u8'-';
            }
        );
    }

    bool is_function_variable_name(const std::u8string_view value) noexcept
    {
        if (value.empty() || is_ascii_digit(value.front()))
            return false;
        return std::all_of(value.begin(), value.end()
            , [](const char8_t character)
            {
                return is_ascii_letter(character) || is_ascii_digit(character) || character == u8'_';
            }
        );
    }

    bool is_reserved_variable_name(const std::u8string_view value) noexcept
    {
        return value == u8"env" || is_builtin_variable_name(value);
    }

    bool is_builtin_variable_name(const std::u8string_view value) noexcept
    {
        return contains_name(document_variable_names, value) || contains_name(runtime_variable_names, value);
    }

    bool is_runtime_variable_name(const std::u8string_view value) noexcept
    {
        return contains_name(runtime_variable_names, value);
    }

    bool is_usable_function_variable(const function_variable& variable) noexcept
    {
        return std::holds_alternative<std::u8string>(variable.value) && is_function_variable_name(variable.name) && is_reserved_variable_name(variable.name) == false;
    }

    std::vector<variable_binding> make_builtin_variables(const std::u8string_view document_path, const project_definition& project, const repository_snapshot* const snapshot)
    {
        std::vector<variable_binding> result {};
        result.reserve(document_variable_names.size() + runtime_variable_names.size());

        const std::u8string_view root { project.path.normalized.empty() ? project.path.original : project.path.normalized };
        const std::u8string_view name { project.display_name.empty() ? project.id.value : project.display_name };
        result.push_back({ u8"workspaceFolder", non_empty(workspace_folder(document_path)), variable_origin::document });
        result.push_back({ u8"workspaceFile", non_empty(with_backslashes(document_path)), variable_origin::document });
        result.push_back({ u8"projectRoot", non_empty(with_backslashes(root)), variable_origin::document });
        result.push_back({ u8"projectId", project.id.value, variable_origin::document });
        result.push_back({ u8"projectName", std::u8string { name }, variable_origin::document });

        std::optional<std::u8string> vcs {};
        std::optional<std::u8string> repository_root {};
        std::optional<std::u8string> branch {};
        std::optional<std::u8string> svn_url {};
        std::optional<std::u8string> revision {};
        if (snapshot != nullptr)
        {
            if (snapshot->kind == repository_kind::git)
                vcs = { u8"git" };
            else if (snapshot->kind == repository_kind::subversion)
                vcs = { u8"svn" };
            repository_root = non_empty(with_backslashes(snapshot->repository_root));
            revision = non_empty(snapshot->local_revision);

            // 표시용 reference는 detached면 접두어 + oid, svn이면 상대 URL이라 브랜치
            // 이름으로 쓸 수 없다 (3.3).
            if (snapshot->kind == repository_kind::git && snapshot->working_tree.is_detached == false)
                branch = non_empty(snapshot->current_reference);

            std::u8string_view svn_root { snapshot->svn_repository_root };
            while (svn_root.ends_with(u8'/'))
                svn_root.remove_suffix(1);
            const std::u8string_view reference { snapshot->current_reference };
            if (snapshot->kind == repository_kind::subversion && svn_root.empty() == false && reference.starts_with(u8"^/"))
                svn_url = std::u8string { svn_root } + std::u8string { reference.substr(1) };
        }
        result.push_back({ u8"vcs", std::move(vcs), variable_origin::runtime });
        result.push_back({ u8"repositoryRoot", std::move(repository_root), variable_origin::runtime });
        result.push_back({ u8"branch", std::move(branch), variable_origin::runtime });
        result.push_back({ u8"svnUrl", std::move(svn_url), variable_origin::runtime });
        result.push_back({ u8"revision", std::move(revision), variable_origin::runtime });
        return result;
    }

    std::vector<user_variable> make_user_variables(const function_buttons_definition& definition, const std::u8string_view project)
    {
        std::vector<user_variable> result {};
        const auto assign { [&result](const function_variable& variable)
            {
                if (is_usable_function_variable(variable) == false)
                    return;

                const std::u8string& value { std::get<std::u8string>(variable.value) };
                for (user_variable& existing : result)
                {
                    if (existing.name == variable.name)
                    {
                        existing.value = value;
                        return;
                    }
                }
                result.push_back({ variable.name, value });
            }
        };

        for (const function_variable& variable : definition.variables)
            assign(variable);
        for (const function_project_variables& overrides : definition.project_variables)
        {
            const auto* const variables { std::get_if<std::vector<function_variable>>(&overrides.variables) };
            if (overrides.project != project || variables == nullptr)
                continue;
            for (const function_variable& variable : *variables)
                assign(variable);
        }
        return result;
    }

    bool variable_expansion::succeeded() const noexcept
    {
        return error.has_value() == false;
    }

    variable_expansion expand_variables(const std::u8string_view template_text, const variable_lookup& lookup)
    {
        const tokenized_template tokenized { tokenize(template_text) };
        if (tokenized.error.has_value())
            return { {}, {}, tokenized.error };

        variable_expansion result {};
        std::optional<expansion_error> error {};
        for (const template_token& token : tokenized.tokens)
        {
            if (token.reference == false)
            {
                append_segment(result, token.literal, variable_origin::literal, {});
                continue;
            }

            if (append_environment_or_builtin(result, token, lookup, error) == false)
            {
                const user_variable* const user { find_user(lookup, token.name) };
                if (user == nullptr)
                    error = { expansion_error_kind::undefined, token.name, token.offset };
                else
                    append_user_variable(result, *user, token.offset, lookup, error);
            }
            if (error.has_value())
                return { {}, {}, std::move(error) };
        }
        return result;
    }

    variable_scan scan_variable_references(const std::u8string_view template_text)
    {
        tokenized_template tokenized { tokenize(template_text) };
        variable_scan result {};
        result.error = tokenized.error;
        for (template_token& token : tokenized.tokens)
        {
            if (token.reference)
                result.references.push_back({ std::move(token.name), token.environment, token.offset });
        }
        return result;
    }

    std::u8string display_text(const variable_expansion& expansion)
    {
        std::u8string result {};
        result.reserve(expansion.text.size());
        for (const expansion_segment& segment : expansion.segments)
        {
            // 실제 값 대신 이름을 남겨 비밀이 로그와 파일 로그에 남지 않게 한다 (3.6).
            if (segment.origin == variable_origin::environment)
            {
                result.append(u8"${env:");
                result.append(segment.variable);
                result.push_back(u8'}');
                continue;
            }
            result.append(std::u8string_view { expansion.text }.substr(segment.offset, segment.length));
        }
        return result;
    }

    std::u8string build_display_command_line(const variable_expansion& program, const std::span<const variable_expansion> arguments, const command_line_joiner& join, const secret_text_masker& mask)
    {
        std::vector<std::u8string> displayed_arguments {};
        displayed_arguments.reserve(arguments.size());
        for (const variable_expansion& argument : arguments)
            displayed_arguments.push_back(display_text(argument));

        const std::u8string joined { join(display_text(program), std::span<const std::u8string> { displayed_arguments }) };
        return mask(joined);
    }

    std::optional<function_program_rejection> check_function_program(const std::u8string_view program) noexcept
    {
        // PATH 탐색과 확장자 추론이 없으므로 상대 경로는 gitman의 현재 폴더에 따라
        // 다른 프로그램을 가리킬 수 있다 (stage-3-plan.md:56).
        if (is_absolute_windows_path(program) == false)
            return function_program_rejection::not_absolute;

        const std::u8string_view name { last_path_component(program) };
        if (name.ends_with(u8'.') || name.ends_with(u8' '))
            return function_program_rejection::trailing_dot_or_space;

        // 확장자만 있는 이름(`.exe`)은 파일 이름이 비어 있어 받지 않는다.
        constexpr std::size_t extension_length { 4 };
        if (name.size() <= extension_length)
            return function_program_rejection::extension_not_allowed;
        const std::u8string_view extension { name.substr(name.size() - extension_length) };
        if (equal_ignoring_ascii_case(extension, u8".exe") == false && equal_ignoring_ascii_case(extension, u8".com") == false)
            return function_program_rejection::extension_not_allowed;
        return std::nullopt;
    }

    std::u8string function_program_rejection_message(const function_program_rejection rejection)
    {
        switch (rejection)
        {
        case function_program_rejection::not_absolute:
            return u8"program은 절대 경로여야 합니다.";
        case function_program_rejection::trailing_dot_or_space:
            return u8"program의 파일 이름이 점이나 공백으로 끝납니다.";
        case function_program_rejection::extension_not_allowed:
            return u8"program이 허용 목록에 없습니다 (.exe 또는 .com). "
                u8"스크립트는 인터프리터를 program에 적습니다. 예: powershell.exe -NoProfile -File <ps1>, cmd.exe /d /c <bat>";
        }
        return u8"program이 허용 목록에 없습니다.";
    }

    bool is_cmd_program(const std::u8string_view program) noexcept
    {
        return equal_ignoring_ascii_case(last_path_component(program), u8"cmd.exe");
    }

    std::optional<cmd_injection_finding> find_cmd_injection(const variable_expansion& program, const std::span<const variable_expansion> arguments)
    {
        if (is_cmd_program(program.text) == false)
            return std::nullopt;

        const auto scan { [](const variable_expansion& expansion, const std::optional<std::size_t> argument) -> std::optional<cmd_injection_finding>
            {
                for (const expansion_segment& segment : expansion.segments)
                {
                    // 문서 출처와 글자 그대로의 구간은 문서 작성자가 쓴 것이고 신뢰 확인의 대상이다.
                    if (segment.origin != variable_origin::runtime && segment.origin != variable_origin::environment)
                        continue;

                    const std::u8string_view text { std::u8string_view { expansion.text }.substr(segment.offset, segment.length) };
                    const std::size_t found { text.find_first_of(cmd_metacharacters) };
                    if (found != std::u8string_view::npos)
                        return cmd_injection_finding { argument, segment.origin, segment.variable, text[found] };
                }
                return std::nullopt;
            }
        };

        if (std::optional<cmd_injection_finding> finding { scan(program, std::nullopt) }; finding.has_value())
            return finding;
        for (std::size_t index = 0; index < arguments.size(); ++index)
        {
            if (std::optional<cmd_injection_finding> finding { scan(arguments[index], index) }; finding.has_value())
                return finding;
        }
        return std::nullopt;
    }

    std::u8string cmd_injection_message(const cmd_injection_finding& finding)
    {
        std::u8string message { u8"cmd.exe에 넘기는 " };
        message.append(finding.origin == variable_origin::environment ? u8"${env:" : u8"${");
        message.append(finding.variable);
        message.append(u8"} 값에 cmd가 다시 해석하는 문자(");
        if (finding.character == u8'\r' || finding.character == u8'\n')
            message.append(u8"줄바꿈");
        else
            message.push_back(finding.character);
        message.append(u8")가 있습니다.");
        return message;
    }
} // namespace gitman
