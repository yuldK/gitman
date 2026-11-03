#include "domain/cmd_command_line.h"

#include <array>
#include <utility>

namespace gitman {
    namespace {
        // 따옴표 밖이면 cmd가 연산자로 읽는 문자다. 이 문자를 따옴표 안에 넣는 것이 보정이다.
        constexpr std::u8string_view cmd_operator_characters { u8"&|<>^()" };
        // 감싸야 하는 문자다. 공백·탭, 배치 인자(`%1`)를 가르는 `, ; =`는 보통 조립이고, 연산자와 `"`는 보정이다.
        constexpr std::u8string_view cmd_quoted_characters { u8" \t\v\f,;=&|<>^()\"" };
        // 보통 조립이 감싸는 문자다. 이 문자 때문에 어차피 감싸는 토큰은 연산자가 함께 있어도 결과가 보통 조립과 같다.
        constexpr std::u8string_view cmd_ordinary_quoted_characters { u8" \t\v\f,;=" };
        // 배치 줄에서 `%NAME%`을 펼친 뒤 cmd가 해석하는 문자다 (D26). `!`는 지연 확장이 켜진 cmd(`/v:on`,
        // `setlocal EnableDelayedExpansion`, 레지스트리)가 `!NAME!`을 gitman 환경의 값으로 바꾼다. `, ; =`는 명령과
        // 인자를 가른다. `if %GITMAN_BRANCH%==main …`에 `x==x,md,INJECTED`가 오면 `==`가 비교를 일찍 끝내고 뒤의
        // `md INJECTED`가 실행된다(git이 받는 브랜치 이름, 2026-11-03 실측). 공백·탭은 넣지 않는다. 브랜치 이름에는 올
        // 수 없고, 저장소 루트(`OneDrive - 회사명`)마다 경고하면 경고가 뜻을 잃으며, `=` 없이 공백만으로는 `if` 비교가
        // 문법 오류로 끝난다.
        constexpr std::u8string_view cmd_environment_metacharacters { u8"&|<>^()%!\",;=\r\n" };

        constexpr char8_t ascii_lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value + (u8'a' - u8'A'));
            return value;
        }

        constexpr bool is_ascii_letter(const char8_t value) noexcept
        {
            return ascii_lower(value) >= u8'a' && ascii_lower(value) <= u8'z';
        }

        constexpr bool is_ascii_alphanumeric(const char8_t value) noexcept
        {
            return is_ascii_letter(value) || (value >= u8'0' && value <= u8'9');
        }

        constexpr bool is_cmd_command_letter(const char8_t value) noexcept
        {
            const char8_t letter { ascii_lower(value) };
            return letter == u8'c' || letter == u8'k' || letter == u8'r';
        }

        bool equal_ignoring_case(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (ascii_lower(left[index]) != ascii_lower(right[index]))
                    return false;
            }
            return true;
        }

        // `/d`, `/v:on`, `/t:0a` 꼴이다. `/dc`처럼 글자를 붙이면 cmd는 `/c`를 찾지 못하고 대화형으로 연다.
        // `/c:x`·`/k:1`·`/r:x`는 스위치로 보지 않는다. cmd는 그 `/c`·`/k`를 명령 스위치로 읽어 뒤에 넣는 `/c`
        // 앞에서 명령을 시작하므로 아무것도 실행하지 않거나(`/c:x`) 대화형으로 남는다(`/k:1`) (2026-11-03 실측).
        bool is_cmd_switch(const std::u8string_view argument) noexcept
        {
            if (argument.size() < 2 || argument[0] != u8'/' || is_ascii_letter(argument[1]) == false)
                return false;
            if (argument.size() == 2)
                return true;
            if (is_cmd_command_letter(argument[1]) || argument[2] != u8':' || argument.size() == 3)
                return false;
            for (const char8_t value : argument.substr(3))
            {
                if (is_ascii_alphanumeric(value) == false)
                    return false;
            }
            return true;
        }

        // 명령을 받는 스위치다. `/r`은 `/c`와 같다.
        bool is_cmd_command_switch(const std::u8string_view argument) noexcept
        {
            return argument.size() == 2 && argument[0] == u8'/' && is_cmd_command_letter(argument[1]);
        }

        bool is_cmd_s_switch(const std::u8string_view argument) noexcept
        {
            return argument.size() == 2 && argument[0] == u8'/' && ascii_lower(argument[1]) == u8's';
        }

        // `/v:on`이면 참, `/v:off`면 거짓이다. `/v` 스위치가 아니면 값이 없다.
        std::optional<bool> delayed_expansion_switch(const std::u8string_view argument) noexcept
        {
            if (argument.size() < 3 || ascii_lower(argument[1]) != u8'v' || argument[2] != u8':')
                return std::nullopt;
            return equal_ignoring_case(argument.substr(3), u8"off") == false;
        }

        // 따옴표로 감싼 글 하나다. 안의 `"`는 `""`로 겹치고, `"` 앞과 닫는 따옴표 앞의 `\`는 두 배로 늘린다.
        // `""`는 cmd의 따옴표 짝을 맞추고 UCRT와 `CommandLineToArgvW`가 글자 `"` 하나로 읽는다 (2026-11-03 실측).
        void append_quoted_run(std::u8string& target, const std::u8string_view run)
        {
            target.push_back(u8'"');
            std::size_t backslashes { 0 };
            for (const char8_t value : run)
            {
                if (value == u8'\\')
                {
                    ++backslashes;
                    continue;
                }
                target.append(value == u8'"' ? backslashes * 2 : backslashes, u8'\\');
                backslashes = 0;
                if (value == u8'"')
                    target.append(u8"\"\"");
                else
                    target.push_back(value);
            }
            target.append(backslashes * 2, u8'\\');
            target.push_back(u8'"');
        }

        // 토큰 하나를 짓는다. `%`가 있으면 `%`로 나눈 조각을 따로 감싸고 그 사이를 따옴표 밖의 `^%`로 쓴다. cmd는
        // 따옴표 안의 `%NAME%`도 펼치고 `^%`·`%%`로 막을 수 없지만(R1 실측), 따옴표 밖의 `^%`는 펼칠 이름을 `^`로
        // 끝나게 하고 다음 이름을 `"`로 시작하게 해 어떤 변수와도 맞지 않게 하며, `^`는 cmd가 지운다.
        // `force_quotes`는 표시용 글이 실제 값과 같은 모양(감쌌는지)으로 보이게 한다.
        std::u8string render_token(const std::u8string_view text, const bool force_quotes)
        {
            std::u8string target {};
            if (text.find(u8'%') != std::u8string_view::npos)
            {
                std::size_t start { 0 };
                while (true)
                {
                    const std::size_t found { text.find(u8'%', start) };
                    append_quoted_run(target, text.substr(start, found == std::u8string_view::npos ? std::u8string_view::npos : found - start));
                    if (found == std::u8string_view::npos)
                        break;
                    target.append(u8"^%");
                    start = found + 1;
                }
                return target;
            }
            if (force_quotes || text.empty() || text.find_first_of(cmd_quoted_characters) != std::u8string_view::npos)
            {
                append_quoted_run(target, text);
                return target;
            }
            target.append(text);
            return target;
        }

        // 작성자의 따옴표 안에 든 값 구간이다. 감싸지 않고 `"`만 `""`로, `%`만 `"^%"`(따옴표를 닫고 `^%`를 쓰고 다시
        // 연다)로 고친다. 따옴표 안의 연산자는 cmd가 해석하지 않는다. `quote_follows_text`면 곧바로 `"`가 오므로 끝의 `\`도
        // 두 배로 늘린다. 늘리지 않으면 `"${projectRoot}"`의 `D:\`를 받는 쪽이 `D:"`로 읽는다(R4 검토 실측).
        std::u8string render_inside_quotes(const std::u8string_view text, const bool quote_follows_text)
        {
            std::u8string target {};
            std::size_t backslashes { 0 };
            for (const char8_t value : text)
            {
                if (value == u8'\\')
                {
                    ++backslashes;
                    continue;
                }
                const bool quote_follows { value == u8'"' || value == u8'%' };
                target.append(quote_follows ? backslashes * 2 : backslashes, u8'\\');
                backslashes = 0;
                if (value == u8'"')
                    target.append(u8"\"\"");
                else if (value == u8'%')
                    target.append(u8"\"^%\"");
                else
                    target.push_back(value);
            }
            target.append(quote_follows_text ? backslashes * 2 : backslashes, u8'\\');
            return target;
        }

        bool needs_quotes(const std::u8string_view text) noexcept
        {
            return text.empty() || text.find_first_of(cmd_quoted_characters) != std::u8string_view::npos || text.find(u8'%') != std::u8string_view::npos;
        }

        // `cmd_correction_kind`의 개수다. `sentence`가 마지막 값이다.
        constexpr std::size_t cmd_correction_kind_count { static_cast<std::size_t>(cmd_correction_kind::sentence) + 1 };
        using correction_kinds = std::array<bool, cmd_correction_kind_count>;

        // 공백·탭·`, ; =` 때문이나 비어서 어차피 감싸는 토큰은 연산자를 따옴표 안에 넣은 결과가 보통 조립과 같아 보정으로
        // 세지 않는다(`C:\Program Files (x86)\run.bat`). `%`와 `"`는 감싸기만으로 막지 못하므로 센다.
        void add_token_kinds(const std::u8string_view text, correction_kinds& kinds) noexcept
        {
            const bool quoted_anyway { text.empty() || text.find_first_of(cmd_ordinary_quoted_characters) != std::u8string_view::npos };
            if (quoted_anyway == false && text.find_first_of(cmd_operator_characters) != std::u8string_view::npos)
                kinds[static_cast<std::size_t>(cmd_correction_kind::metacharacter)] = true;
            if (text.find(u8'%') != std::u8string_view::npos)
                kinds[static_cast<std::size_t>(cmd_correction_kind::percent)] = true;
            if (text.find(u8'"') != std::u8string_view::npos)
                kinds[static_cast<std::size_t>(cmd_correction_kind::quote)] = true;
        }

        // 보정 종류를 늘 같은 순서(명령 문장, 연산자, `%`, `"`)로 늘어놓는다. 승인 기억과 test가 순서에 기대지 않게 한다.
        std::vector<cmd_correction_kind> ordered_kinds(const correction_kinds& kinds)
        {
            std::vector<cmd_correction_kind> ordered {};
            for (const cmd_correction_kind kind : { cmd_correction_kind::sentence, cmd_correction_kind::metacharacter, cmd_correction_kind::percent, cmd_correction_kind::quote })
            {
                if (kinds[static_cast<std::size_t>(kind)])
                    ordered.push_back(kind);
            }
            return ordered;
        }

        // 드라이브 경로(`C:\`, `C:/`)나 UNC(`\\`)로 시작한다.
        bool starts_with_absolute_path(const std::u8string_view text) noexcept
        {
            if (text.size() >= 3 && is_ascii_letter(text[0]) && text[1] == u8':' && (text[2] == u8'\\' || text[2] == u8'/'))
                return true;
            return text.starts_with(u8"\\\\") || text.starts_with(u8"//");
        }

        bool equal_extension(const std::u8string_view text) noexcept
        {
            return equal_ignoring_case(text, u8".exe") || equal_ignoring_case(text, u8".com") || equal_ignoring_case(text, u8".bat") || equal_ignoring_case(text, u8".cmd");
        }

        // 실행 확장자 뒤에 공백·탭이 온다(`C:\tools\x.exe echo-args one`). 경로 뒤에 인자를 이어 쓴 명령 문장이다.
        bool has_arguments_after_executable(const std::u8string_view text) noexcept
        {
            constexpr std::size_t extension_length { 4 };
            for (std::size_t index = 0; index + extension_length < text.size(); ++index)
            {
                const char8_t next { text[index + extension_length] };
                if (text[index] == u8'.'
                    && (next == u8' ' || next == u8'\t')
                    && equal_extension(text.substr(index, extension_length)))
                    return true;
            }
            return false;
        }

        // 명령의 첫 인자가 명령 문장인지다. 글자 그대로 쓴 부분에 공백·탭이 없으면 토큰이다. 절대 경로로 시작하면
        // 경로로 보되, 글자 그대로 쓴 부분에서 실행 확장자 뒤에 공백이 오면 경로 뒤에 인자를 이어 쓴 문장이다.
        // - `C:\Program Files\x.exe`는 경로, `C:\tools\x.exe echo-args one`은 문장이다.
        // - 변수 값이 절대 경로이고 바로 뒤 글자 그대로의 부분이 `\`·`/`로 시작하면 경로를 이어 쓴 것이다
        //   (`${workspaceFolder}\Build Tools\build.bat`). 공백으로 시작하면 문장이다(`${tool} --flag`).
        // 경로와 문장을 글자만으로 완전히 가를 수는 없으므로(공백이 든 상대 경로) 사용자가 확인 dialog에서 본다.
        bool is_sentence(const cmd_argument& argument) noexcept
        {
            bool whitespace { false };
            for (const cmd_text_piece& piece : argument.pieces)
            {
                if (piece.literal && piece.text.find_first_of(u8" \t") != std::u8string::npos)
                    whitespace = true;
            }
            if (whitespace == false)
                return false;

            const cmd_text_piece& first { argument.pieces.front() };
            bool path { first.literal && starts_with_absolute_path(first.text) };
            if (first.literal == false && starts_with_absolute_path(first.text) && argument.pieces.size() > 1)
            {
                const cmd_text_piece& next { argument.pieces[1] };
                path = next.literal && (next.text.starts_with(u8'\\') || next.text.starts_with(u8'/'));
            }
            if (path == false)
                return true;
            for (const cmd_text_piece& piece : argument.pieces)
            {
                if (piece.literal && has_arguments_after_executable(piece.text))
                    return true;
            }
            return false;
        }

        std::u8string_view shown(const cmd_text_piece& piece) noexcept
        {
            return piece.display.has_value() ? std::u8string_view { *piece.display } : std::u8string_view { piece.text };
        }

        // 작성자의 따옴표 안에서 `index` 조각 바로 뒤에 `"`가 오는지다. 값 조각의 `%`도 `"^%"`로 써서 `"`로 시작한다.
        bool quote_follows(const std::span<const cmd_text_piece> pieces, const std::size_t index) noexcept
        {
            for (std::size_t next = index + 1; next < pieces.size(); ++next)
            {
                const std::u8string& text { pieces[next].text };
                if (text.empty())
                    continue;
                if (pieces[next].literal)
                    return text.front() == u8'"';
                return text.front() == u8'"' || text.front() == u8'%';
            }
            return false;
        }

        // cmd가 `/c "…"` 안의 명령을 읽는 따옴표 상태다. 따옴표 밖의 `^`는 다음 글자를 글자로 만들어 따옴표를 세지
        // 않는다. cmd는 명령 전체를 한 번에 읽으므로 조각과 인자를 넘어 이어 간다. 작성자가 글자 그대로 쓴 `^`로 끝나거나
        // 따옴표를 연 채로 끝난 문장 뒤에서는 gitman이 감싼 따옴표가 거꾸로 읽힌다(R4 검토 실측).
        struct cmd_quote_state
        {
            bool inside_quotes { false };
            bool escaped { false };

            void advance(const std::u8string_view text) noexcept
            {
                for (const char8_t value : text)
                {
                    if (escaped)
                        escaped = false;
                    else if (value == u8'^' && inside_quotes == false)
                        escaped = true;
                    else if (value == u8'"')
                        inside_quotes = inside_quotes == false;
                }
            }
        };

        // 명령 부분의 인자 하나를 지은 결과다.
        struct rendered_argument
        {
            std::u8string text {};
            std::u8string display {};
            correction_kinds kinds {};
            // 값에 `!`가 있다(명령 문장의 글자 그대로 부분은 빼고).
            bool bang { false };
            // 명령 문장에 글자 그대로 쓴 `%`·`!`다.
            std::optional<char8_t> sentence_expansion {};
            // 앞의 글자 그대로 부분 때문에 따옴표로 보호하지 못한 값이 있다. 값은 열린 따옴표(`"`)나 끝의 `^`다.
            std::optional<char8_t> unprotected {};
        };

        rendered_argument render_argument(const cmd_argument& argument, const bool sentence, cmd_quote_state& state)
        {
            rendered_argument result {};
            if (sentence == false)
            {
                const std::u8string text { argument.text() };
                result.bang = text.find(u8'!') != std::u8string::npos;
                result.text = render_token(text, false);
                result.display = render_token(argument.display_text(), needs_quotes(text));
                // 앞 문장이 따옴표를 연 채로 끝났으면 감싼 따옴표가 그 따옴표를 닫고 값이 따옴표 밖에 남는다. 보정했다고
                // 말하지 않고 경고한다.
                if (state.inside_quotes && result.text != text)
                    result.unprotected = u8'"';
                else
                    add_token_kinds(text, result.kinds);
                state.advance(result.text);
                return result;
            }

            result.kinds[static_cast<std::size_t>(cmd_correction_kind::sentence)] = true;
            for (std::size_t index = 0; index < argument.pieces.size(); ++index)
            {
                const cmd_text_piece& piece { argument.pieces[index] };
                if (piece.literal)
                {
                    result.text += piece.text;
                    result.display += piece.text;
                    const std::size_t expansion { piece.text.find_first_of(u8"%!") };
                    if (expansion != std::u8string::npos && result.sentence_expansion.has_value() == false)
                        result.sentence_expansion = piece.text[expansion];
                    state.advance(piece.text);
                    continue;
                }

                result.bang = result.bang || piece.text.find(u8'!') != std::u8string::npos;
                if (state.inside_quotes)
                {
                    if (piece.text.find(u8'%') != std::u8string::npos)
                        result.kinds[static_cast<std::size_t>(cmd_correction_kind::percent)] = true;
                    if (piece.text.find(u8'"') != std::u8string::npos)
                        result.kinds[static_cast<std::size_t>(cmd_correction_kind::quote)] = true;
                    const bool closing { quote_follows(argument.pieces, index) };
                    const std::u8string rendered { render_inside_quotes(piece.text, closing) };
                    result.text += rendered;
                    result.display += render_inside_quotes(shown(piece), closing);
                    state.advance(rendered);
                    continue;
                }
                // 값이 비면 아무것도 쓰지 않는다. 문장 가운데의 빈 `""`는 인자 하나를 새로 만든다.
                if (piece.text.empty())
                {
                    result.display += shown(piece);
                    continue;
                }
                const std::u8string rendered { render_token(piece.text, false) };
                // 바로 앞이 글자 그대로 쓴 `^`면 cmd가 여는 따옴표를 글자로 읽어 값이 따옴표 밖에 남는다.
                if (state.escaped)
                {
                    if (result.unprotected.has_value() == false)
                        result.unprotected = u8'^';
                }
                else
                {
                    add_token_kinds(piece.text, result.kinds);
                }
                result.text += rendered;
                result.display += render_token(shown(piece), needs_quotes(piece.text));
                state.advance(rendered);
            }
            return result;
        }

        // 배치가 읽을 환경 변수 값의 메타 문자를 경고로 더한다 (D26을 D30이 경고로 바꿈).
        void append_exported_warnings(std::vector<cmd_warning>& warnings, const std::span<const cmd_exported_value> exported)
        {
            for (const cmd_exported_value& value : exported)
            {
                const std::size_t found { value.value.find_first_of(cmd_environment_metacharacters) };
                if (found != std::u8string::npos)
                    warnings.push_back({ cmd_warning_kind::exported_environment, value.name, value.value, std::nullopt, value.value[found] });
            }
        }

        void append_character(std::u8string& target, const char8_t character)
        {
            if (character == u8'\r' || character == u8'\n')
                target.append(u8"줄바꿈");
            else
                target.push_back(character);
        }

        std::u8string argument_name(const std::optional<std::size_t> argument)
        {
            std::u8string digits {};
            std::size_t remaining { argument.value_or(0) };
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                remaining /= 10;
            } while (remaining > 0);
            return u8"args[" + digits + u8"]";
        }
    }

    std::u8string cmd_argument::text() const
    {
        std::u8string value {};
        for (const cmd_text_piece& piece : pieces)
            value += piece.text;
        return value;
    }

    std::u8string cmd_argument::display_text() const
    {
        std::u8string value {};
        for (const cmd_text_piece& piece : pieces)
            value += shown(piece);
        return value;
    }

    bool cmd_command_line::needs_approval() const noexcept
    {
        return corrections.empty() == false || warnings.empty() == false;
    }

    cmd_command_line assemble_cmd_command_line(const cmd_text_piece& executable, const std::span<const cmd_argument> arguments, const std::span<const cmd_exported_value> exported)
    {
        cmd_command_line result {};
        for (std::size_t index = 0; index < arguments.size() && result.rejection.has_value() == false; ++index)
        {
            const std::u8string text { arguments[index].text() };
            const std::size_t found { text.find_first_of(u8"\r\n") };
            if (found != std::u8string::npos)
                result.rejection = cmd_rejection { index, text[found] };
        }

        // cmd는 따옴표로 감싼 argv[0] 안의 `/`도 스위치로 읽는다. `"C:/Windows/System32/cmd.exe"`면 `/cmd.exe`를
        // `/c`로 읽어 내장 명령 `md`가 나머지를 받는다 (2026-11-03 실측). 그래서 argv[0]의 `/`를 `\`로 쓴다. 실행
        // 대상은 `lpApplicationName`이 정하므로 argv[0]의 구분자는 실행할 파일을 바꾸지 않는다. argv[0]의 `% ! & ( )`는
        // cmd가 해석하지 않는다(실측).
        const auto append_program = [](std::u8string& target, const std::u8string_view program)
        {
            target.push_back(u8'"');
            for (const char8_t value : program)
                target.push_back(value == u8'/' ? u8'\\' : value);
            target.push_back(u8'"');
        };
        std::u8string switches {};
        std::u8string switches_display {};
        std::size_t index { 0 };
        bool has_s_switch { false };
        std::optional<bool> delayed_expansion {};
        std::u8string command_switch {};
        while (index < arguments.size() && is_cmd_switch(arguments[index].text()))
        {
            const std::u8string argument { arguments[index].text() };
            ++index;
            if (is_cmd_command_switch(argument))
            {
                command_switch = argument;
                break;
            }
            has_s_switch = has_s_switch || is_cmd_s_switch(argument);
            if (const std::optional<bool> enabled { delayed_expansion_switch(argument) }; enabled.has_value())
                delayed_expansion = enabled;
            switches.push_back(u8' ');
            switches.append(argument);
            switches_display.push_back(u8' ');
            switches_display.append(arguments[index - 1].display_text());
        }

        append_program(result.text, executable.text);
        append_program(result.display_text, shown(executable));
        if (command_switch.empty() && index == arguments.size())
        {
            result.text += switches;
            result.display_text += switches_display;
            append_exported_warnings(result.warnings, exported);
            return result;
        }
        if (command_switch.empty())
            command_switch = u8"/c";

        std::u8string command {};
        std::u8string command_display {};
        std::optional<std::size_t> bang_argument {};
        std::vector<cmd_correction> argument_corrections {};
        cmd_quote_state state {};
        for (std::size_t current = index; current < arguments.size(); ++current)
        {
            const cmd_argument& argument { arguments[current] };
            const bool sentence { current == index && argument.pieces.empty() == false && is_sentence(argument) };
            if (current != index)
            {
                command.push_back(u8' ');
                command_display.push_back(u8' ');
                state.advance(u8" ");
            }
            const rendered_argument rendered { render_argument(argument, sentence, state) };
            command += rendered.text;
            command_display += rendered.display;
            if (rendered.bang && bang_argument.has_value() == false)
                bang_argument = current;
            if (rendered.sentence_expansion.has_value())
                result.warnings.push_back({ cmd_warning_kind::sentence_expansion, {}, {}, current, *rendered.sentence_expansion });
            if (rendered.unprotected.has_value())
                result.warnings.push_back({ cmd_warning_kind::unprotected_value, {}, {}, current, *rendered.unprotected });

            std::vector<cmd_correction_kind> kinds { ordered_kinds(rendered.kinds) };
            if (kinds.empty() == false)
                argument_corrections.push_back({ current, argument.display_text(), rendered.display, std::move(kinds) });
        }

        // 지연 확장은 스위치로만 끌 수 있다. `/v:off`는 레지스트리 설정보다 앞선다. 작성자가 `/v:on`을 적었으면 그
        // 선택을 바꾸지 않고 무력화하지 못한 `!`로 경고한다.
        std::u8string inserted {};
        if (bang_argument.has_value())
        {
            if (delayed_expansion.has_value() == false)
            {
                inserted = u8" /v:off";
                result.corrections.push_back({ std::nullopt, {}, u8"/v:off", { cmd_correction_kind::delayed_expansion } });
            }
            else if (*delayed_expansion)
            {
                result.warnings.insert(result.warnings.begin(), { cmd_warning_kind::delayed_expansion_enabled, {}, {}, bang_argument, u8'!' });
            }
        }
        for (cmd_correction& correction : argument_corrections)
            result.corrections.push_back(std::move(correction));

        // `/s`가 있으면 cmd는 `/c` 뒤 첫 따옴표와 마지막 따옴표만 벗기고 그 사이를 그대로 명령으로 쓴다. `/s`가
        // 없으면 따옴표가 셋 이상이거나 특수 문자가 있을 때 앞뒤를 벗겨 `"a b.bat" "c d"`가 깨진다. `/c` 뒤에 쓴 `/s`는
        // cmd가 명령으로 읽으므로 반드시 앞에 둔다.
        const std::u8string tail { (has_s_switch ? std::u8string {} : std::u8string { u8" /s" }) + u8" " + command_switch + u8" \"" };
        result.text += switches + inserted + tail + command + u8"\"";
        result.display_text += switches_display + inserted + tail + command_display + u8"\"";
        append_exported_warnings(result.warnings, exported);
        return result;
    }

    std::u8string cmd_correction_reason(const std::span<const cmd_correction_kind> kinds)
    {
        std::u8string reason {};
        for (const cmd_correction_kind kind : kinds)
        {
            if (reason.empty() == false)
                reason += u8", ";
            switch (kind)
            {
            case cmd_correction_kind::metacharacter:
                reason += u8"& | < > ^ ( )를 따옴표 안에 넣어 글자로 넘깁니다";
                break;
            case cmd_correction_kind::percent:
                reason += u8"%를 따옴표 밖 ^%로 써서 환경 변수로 펼치지 않습니다";
                break;
            case cmd_correction_kind::quote:
                reason += u8"\"를 \"\"로 겹쳐 따옴표 짝을 맞춥니다";
                break;
            case cmd_correction_kind::delayed_expansion:
                reason += u8"/v:off를 넣어 cmd 명령줄의 !를 글자로 넘깁니다(배치가 지연 확장을 켜면 펼쳐집니다)";
                break;
            case cmd_correction_kind::sentence:
                reason += u8"공백이 든 글을 명령 문장으로 넘깁니다(글자 그대로 쓴 부분은 cmd가 해석합니다)";
                break;
            }
        }
        return reason;
    }

    std::u8string cmd_warning_message(const cmd_warning& warning)
    {
        std::u8string message {};
        switch (warning.kind)
        {
        case cmd_warning_kind::exported_environment:
            message = warning.environment + u8" 값(" + warning.value + u8")에 cmd가 다시 해석하는 문자(";
            append_character(message, warning.character);
            message += u8")가 있습니다. 배치 안에서는 \"%" + warning.environment + u8"%\"처럼 따옴표 안에서 읽어야 합니다.";
            break;
        case cmd_warning_kind::delayed_expansion_enabled:
            message = argument_name(warning.argument) + u8"의 !는 /v:on이라 무력화하지 못했습니다. cmd가 !NAME!을 환경 변수 값으로 바꿀 수 있습니다.";
            break;
        case cmd_warning_kind::sentence_expansion:
            message = argument_name(warning.argument) + u8"의 글자 그대로 쓴 ";
            append_character(message, warning.character);
            // `!`는 지연 확장이 켜졌을 때만 펼쳐진다. 기본 cmd는 `!GMX!`를 그대로 둔다(R4 검토 실측).
            if (warning.character == u8'!')
                message += u8"는 명령 문장이라 지연 확장이 켜져 있으면 cmd가 환경 변수로 펼칩니다.";
            else
                message += u8"는 명령 문장이라 cmd가 환경 변수로 펼칩니다.";
            break;
        case cmd_warning_kind::unprotected_value:
            message = argument_name(warning.argument);
            if (warning.character == u8'^')
                message += u8"의 값은 바로 앞에 글자 그대로 쓴 ^ 때문에 따옴표로 보호되지 않습니다.";
            else
                message += u8"의 값은 앞의 명령 문장이 따옴표를 연 채로 끝나 따옴표로 보호되지 않습니다.";
            message += u8" 값의 & | < > %를 cmd가 해석할 수 있습니다.";
            break;
        }
        return message;
    }
}
