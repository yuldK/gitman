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

        bool ends_with_extension(const std::u8string_view text, const std::u8string_view extension) noexcept
        {
            return text.size() >= extension.size() && equal_ignoring_case(text.substr(text.size() - extension.size()), extension);
        }

        // 배치 파일(`.bat`·`.cmd`)을 가리키는 글이다.
        bool is_batch_path(const std::u8string_view text) noexcept
        {
            return ends_with_extension(text, u8".bat") || ends_with_extension(text, u8".cmd");
        }

        // 명령 문장의 첫 토큰이다. cmd가 명령 앞에서 건너뛰는 `@`·`(`·구분자(`, ; =`)는 벗긴다. 따옴표로 시작하면 닫는
        // 따옴표까지의 안쪽, 아니면 공백이나 cmd 연산자 앞까지이고 `^`는 cmd처럼 지우고 다음 글자를 그대로 둔다(`^x.bat`,
        // `run^ x.bat`). 그 뒤의 글자는 이 토큰과 상관없다 (S3 검토).
        std::u8string first_sentence_token(std::u8string_view text)
        {
            const std::size_t start { text.find_first_not_of(u8" \t@(,;=") };
            if (start == std::u8string_view::npos)
                return {};
            text.remove_prefix(start);
            if (text.starts_with(u8'"'))
            {
                text.remove_prefix(1);
                return std::u8string { text.substr(0, text.find(u8'"')) };
            }
            constexpr std::u8string_view ends { u8" \t&|<>()" };
            std::u8string token {};
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == u8'^' && index + 1 < text.size())
                {
                    ++index;
                    token.push_back(text[index]);
                    continue;
                }
                if (ends.find(text[index]) != std::u8string_view::npos)
                    break;
                token.push_back(text[index]);
            }
            return token;
        }

        // 스위치 줄 다음의 명령 첫 인자 순번이다. 스위치뿐이면 값이 없다. `assemble_cmd_command_line`과 같은 판정이다.
        std::optional<std::size_t> command_argument_index(const std::span<const cmd_argument> arguments)
        {
            std::size_t index { 0 };
            while (index < arguments.size() && is_cmd_switch(arguments[index].text()))
            {
                const bool command { is_cmd_command_switch(arguments[index].text()) };
                ++index;
                if (command)
                    break;
            }
            if (index == arguments.size())
                return std::nullopt;
            return index;
        }

        constexpr char ascii_lower_byte(const char value) noexcept
        {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
        }

        bool equal_ignoring_case_bytes(const std::string_view left, const std::string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                if (ascii_lower_byte(left[index]) != ascii_lower_byte(right[index]))
                    return false;
            }
            return true;
        }

        bool contains_ignoring_case_bytes(const std::string_view text, const std::string_view word) noexcept
        {
            for (std::size_t index = 0; index + word.size() <= text.size(); ++index)
            {
                if (equal_ignoring_case_bytes(text.substr(index, word.size()), word))
                    return true;
            }
            return false;
        }

        // cmd가 낱말을 가르는 글자(공백, 탭, `, ; =`, VT, FF, 0xFF)와 연산자·따옴표·`@`다. 배치 글의 낱말 경계다. 공백만
        // 보면 `setlocal EnableDelayedExpansion&echo`, `(setlocal …)`, `call,x`처럼 cmd가 그대로 실행하는 줄을 놓친다
        // (S3 검토, 실제 cmd.exe로 확인).
        bool is_batch_word_boundary(const char value) noexcept
        {
            constexpr std::string_view boundaries { " \t,;=\x0B\x0C\xFF&|<>()\"@" };
            return boundaries.find(value) != std::string_view::npos;
        }

        // 줄에 낱말 `word`가 있다. 대소문자를 가리지 않는다. 앞뒤가 줄 끝이나 cmd의 낱말 경계이고, 뒤는 `call:label`·
        // `call/?`처럼 `:`·`/`도 받는다.
        bool has_batch_word(const std::string_view line, const std::string_view word) noexcept
        {
            for (std::size_t index = 0; index + word.size() <= line.size(); ++index)
            {
                if (equal_ignoring_case_bytes(line.substr(index, word.size()), word) == false)
                    continue;
                const std::size_t next { index + word.size() };
                const bool start { index == 0 || is_batch_word_boundary(line[index - 1]) };
                const bool end { next == line.size() || is_batch_word_boundary(line[next]) || line[next] == ':' || line[next] == '/' };
                if (start && end)
                    return true;
            }
            return false;
        }

        // 줄이 자식 cmd의 지연 확장을 켠다(`cmd /v:on /c …`, `cmd /v /c …`). `/v`만으로는 `findstr /v`와 같으므로 같은
        // 줄에 `cmd`가 있을 때만 본다. `/v:off`는 끈다.
        bool has_delayed_expansion_switch(const std::string_view line) noexcept
        {
            if (contains_ignoring_case_bytes(line, "/v:on"))
                return true;
            if (contains_ignoring_case_bytes(line, "cmd") == false)
                return false;
            for (std::size_t index = 0; index + 2 <= line.size(); ++index)
            {
                if (equal_ignoring_case_bytes(line.substr(index, 2), "/v") == false)
                    continue;
                if (index + 2 == line.size() || is_batch_word_boundary(line[index + 2]))
                    return true;
            }
            return false;
        }

        // cmd가 따옴표 밖에서 지우는 `^`를 지운 사본이다. `set^local`, `ca^ll`도 그 낱말이다.
        std::string without_carets(const std::string_view line)
        {
            std::string result {};
            result.reserve(line.size());
            for (const char value : line)
            {
                if (value != '^')
                    result.push_back(value);
            }
            return result;
        }

        // `%…%` 구간을 지운 사본이다. 정의되지 않은 변수는 빈 글로 펼쳐지므로 `%nothing%call`, `set%x%local`도 그
        // 낱말일 수 있다. 짝이 없는 `%`부터는 그대로 둔다. `%~1 call %~2`처럼 구간이 낱말을 삼킬 수 있어 원래 줄과 함께 본다.
        std::string without_percent_spans(const std::string_view line)
        {
            std::string result {};
            result.reserve(line.size());
            std::size_t index { 0 };
            while (index < line.size())
            {
                if (line[index] != '%')
                {
                    result.push_back(line[index]);
                    ++index;
                    continue;
                }
                const std::size_t close { line.find('%', index + 1) };
                if (close == std::string_view::npos)
                {
                    result.append(line.substr(index));
                    break;
                }
                index = close + 1;
            }
            return result;
        }

        // `rem`·`@rem`·`::`로 시작하는 주석 줄이다.
        bool is_batch_comment(std::string_view line) noexcept
        {
            const std::size_t start { line.find_first_not_of(" \t") };
            if (start == std::string_view::npos)
                return false;
            line.remove_prefix(start);
            if (line.starts_with("::"))
                return true;
            if (line.starts_with('@'))
                line.remove_prefix(1);
            return line.size() >= 3
                && equal_ignoring_case_bytes(line.substr(0, 3), "rem")
                && (line.size() == 3 || line[3] == ' ' || line[3] == '\t');
        }

        // 배치 한 줄의 글자 검사다. 주석 줄은 건너뛴다. `EnableDelayedExpansion`은 낱말 경계 없이 찾는다 — 다른 뜻으로 쓰일
        // 글이 아니고, 경계를 따지면 `setlocal EnableDelayedExpansion>nul` 같은 꼴을 놓친다.
        void scan_batch_line(const std::string_view line, cmd_batch_findings& findings)
        {
            if (is_batch_comment(line))
                return;
            if (contains_ignoring_case_bytes(line, "enabledelayedexpansion") || has_delayed_expansion_switch(line))
                findings.delayed_expansion = true;
            if (has_batch_word(line, "call"))
                findings.call = true;
        }

        std::u8string backslashed(const std::u8string_view path)
        {
            std::u8string result { path };
            for (char8_t& value : result)
            {
                if (value == u8'/')
                    value = u8'\\';
            }
            return result;
        }

        // Win32가 `.`·`..`를 풀지 않는 장치 경로(`\\?\`, `\\.\`)다.
        bool is_device_path(const std::u8string_view path) noexcept
        {
            return path.starts_with(u8"\\\\?\\") || path.starts_with(u8"\\\\.\\");
        }

        // `\`로 고친 절대 경로의 뿌리 길이다. `X:\`는 3, `\\server\share`는 share 뒤의 `\`까지(없으면 끝까지)다. 뿌리가
        // 아니거나 장치 경로면 0이다.
        std::size_t absolute_root_length(const std::u8string_view path) noexcept
        {
            if (path.size() >= 3 && is_ascii_letter(path[0]) && path[1] == u8':' && path[2] == u8'\\')
                return 3;
            if (path.starts_with(u8"\\\\") == false || is_device_path(path))
                return 0;
            const std::size_t server_end { path.find(u8'\\', 2) };
            if (server_end == std::u8string_view::npos || server_end == 2)
                return 0;
            const std::size_t share_end { path.find(u8'\\', server_end + 1) };
            if (share_end == server_end + 1)
                return 0;
            return share_end == std::u8string_view::npos ? path.size() : share_end + 1;
        }

        // 뿌리 뒤의 빈 구간과 `.`을 빼고 `..`는 앞 구간을 지운다. 뿌리 위로는 올라가지 않는다(Win32의 경로 정규화와 같다).
        // probe가 붙이는 `\\?\`는 이 정규화를 끄므로 이어 붙인 경로를 그대로 넘기면 있는 파일도 없다고 나온다 (S3 검토).
        std::u8string collapse_path(const std::u8string_view root, std::u8string_view rest)
        {
            std::vector<std::u8string_view> segments {};
            while (rest.empty() == false)
            {
                const std::size_t end { rest.find(u8'\\') };
                const std::u8string_view segment { rest.substr(0, end) };
                rest = end == std::u8string_view::npos ? std::u8string_view {} : rest.substr(end + 1);
                if (segment.empty() || segment == u8".")
                    continue;
                if (segment == u8"..")
                {
                    if (segments.empty() == false)
                        segments.pop_back();
                    continue;
                }
                segments.push_back(segment);
            }
            std::u8string result { root };
            if (result.ends_with(u8'\\') == false)
                result.push_back(u8'\\');
            for (std::size_t index = 0; index < segments.size(); ++index)
            {
                if (index > 0)
                    result.push_back(u8'\\');
                result.append(segments[index]);
            }
            return result;
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

    cmd_command_line assemble_cmd_command_line(const cmd_text_piece& executable, const std::span<const cmd_argument> arguments, const std::span<const cmd_exported_value> exported, const cmd_target_facts* const facts)
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
            // 공백이 든 상대 경로는 글자만으로는 문장이다. worker가 작업 폴더에서 그 파일을 찾았으면 경로 하나다 (S3).
            const bool existing_path {
                facts != nullptr
                && facts->path_exists
                && facts->query.relative_path.has_value()
                && facts->query.argument == current
            };
            const bool sentence { current == index && argument.pieces.empty() == false && existing_path == false && is_sentence(argument) };
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
        // 부르는 배치 파일 안의 일은 명령줄로 고칠 수 없다. worker가 읽은 결과를 경고로 보인다 (S3).
        if (facts != nullptr && facts->batch.has_value())
        {
            const std::optional<std::size_t> argument { facts->query.argument };
            if (facts->batch->delayed_expansion)
                result.warnings.push_back({ cmd_warning_kind::batch_delayed_expansion, {}, {}, argument, u8'!' });
            if (facts->batch->call)
                result.warnings.push_back({ cmd_warning_kind::batch_call, {}, {}, argument, u8'^' });
            if (facts->batch->unchecked)
                result.warnings.push_back({ cmd_warning_kind::batch_unchecked, {}, {}, argument, u8'!' });
        }
        append_exported_warnings(result.warnings, exported);
        return result;
    }

    std::optional<cmd_target_query> find_cmd_target_query(const std::span<const cmd_argument> arguments)
    {
        const std::optional<std::size_t> index { command_argument_index(arguments) };
        if (index.has_value() == false || arguments[*index].pieces.empty())
            return std::nullopt;

        const cmd_argument& argument { arguments[*index] };
        const std::u8string text { argument.text() };
        cmd_target_query query {};
        query.argument = *index;
        query.sentence = is_sentence(argument);
        // 공백이 든 상대 경로다. 경로 꼴(`\`·`/`가 있거나 실행 확장자로 끝남)일 때만 묻는다. `echo hi`는 경로일 리 없다.
        if (query.sentence
            && starts_with_absolute_path(text) == false
            && text.find(u8'"') == std::u8string::npos
            && (text.find_first_of(u8"\\/") != std::u8string::npos || (text.size() >= 4 && equal_extension(std::u8string_view { text }.substr(text.size() - 4)))))
            query.relative_path = text;
        if ((query.sentence == false || query.relative_path.has_value()) && is_batch_path(text))
            query.batch_if_path = text;
        if (query.sentence)
        {
            std::u8string token { first_sentence_token(text) };
            if (is_batch_path(token))
                query.batch_if_sentence = std::move(token);
        }
        if (query.relative_path.has_value() == false && query.batch_if_path.empty() && query.batch_if_sentence.empty())
            return std::nullopt;
        return query;
    }

    std::u8string cmd_target_batch_path(const cmd_target_query& query, const bool path_exists)
    {
        const bool sentence { query.sentence && (query.relative_path.has_value() == false || path_exists == false) };
        return sentence ? query.batch_if_sentence : query.batch_if_path;
    }

    std::u8string cmd_target_absolute_path(const std::u8string_view working_directory, const std::u8string_view path)
    {
        const std::u8string target { backslashed(path) };
        if (is_device_path(target))
            return target;
        const std::size_t target_root { absolute_root_length(target) };
        if (target_root > 0)
            return collapse_path(std::u8string_view { target }.substr(0, target_root), std::u8string_view { target }.substr(target_root));

        const std::u8string base { backslashed(working_directory) };
        const std::size_t base_root { absolute_root_length(base) };
        if (base_root == 0)
            return {};
        const std::u8string_view root { std::u8string_view { base }.substr(0, base_root) };
        // 드라이브 상대 경로(`C:x.bat`)는 그 드라이브의 현재 폴더 기준이다. cmd는 작업 폴더에서 시작하므로 같은 드라이브면
        // 작업 폴더이고, 다른 드라이브의 현재 폴더는 알 수 없다.
        if (target.size() >= 2 && is_ascii_letter(target[0]) && target[1] == u8':')
        {
            if (base_root != 3 || ascii_lower(base[0]) != ascii_lower(target[0]))
                return {};
            return collapse_path(root, std::u8string { std::u8string_view { base }.substr(base_root) } + u8"\\" + target.substr(2));
        }
        // 루트 상대 경로(`\tools\x.bat`)는 작업 폴더의 드라이브나 share 뿌리 기준이다.
        if (target.starts_with(u8'\\'))
            return collapse_path(root, std::u8string_view { target }.substr(1));
        return collapse_path(root, std::u8string { std::u8string_view { base }.substr(base_root) } + u8"\\" + target);
    }

    bool cmd_target_is_local(const cmd_target_query& query)
    {
        const auto local = [&](const std::u8string_view path)
        {
            const std::u8string absolute { cmd_target_absolute_path(query.working_directory, path) };
            return absolute.size() >= 3 && is_ascii_letter(absolute[0]) && absolute[1] == u8':' && absolute[2] == u8'\\';
        };
        return (query.relative_path.has_value() == false || local(*query.relative_path))
            && (query.batch_if_path.empty() || local(query.batch_if_path))
            && (query.batch_if_sentence.empty() || local(query.batch_if_sentence));
    }

    cmd_target_facts cmd_target_unread_facts(const cmd_target_query& query)
    {
        cmd_target_facts facts {};
        facts.query = query;
        // 경로로 볼지 문장으로 볼지도 모르므로 어느 쪽이든 배치를 부를 수 있으면 확인하지 못했다고 경고한다.
        if (query.batch_if_path.empty() == false || query.batch_if_sentence.empty() == false)
            facts.batch = cmd_batch_findings { .unchecked = true };
        return facts;
    }

    cmd_batch_findings scan_cmd_batch_text(std::string_view text)
    {
        cmd_batch_findings findings {};
        // UTF-8 BOM은 cmd에게 첫 줄의 글자다. 지우지 않으면 첫 줄이 `call`로 시작해도 낱말 경계가 맞지 않는다.
        if (text.starts_with("\xEF\xBB\xBF"))
            text.remove_prefix(3);
        std::size_t start { 0 };
        while (start <= text.size())
        {
            const std::size_t end { text.find('\n', start) };
            std::string_view line { text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start) };
            if (line.ends_with('\r'))
                line.remove_suffix(1);
            // cmd가 지우는 `^`와 빈 변수로 가른 낱말(`set^local`, `%x%call`)도 본다. 원래 줄도 함께 보는 것은 `%…%` 구간이
            // 낱말을 삼킬 수 있어서다.
            const std::string uncareted { without_carets(line) };
            scan_batch_line(line, findings);
            scan_batch_line(uncareted, findings);
            scan_batch_line(without_percent_spans(uncareted), findings);
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
        return findings;
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
        case cmd_warning_kind::batch_delayed_expansion:
            message = argument_name(warning.argument) + u8"이 부르는 배치 파일이 지연 확장을 켭니다(EnableDelayedExpansion, cmd /v:on). 넘긴 값의 !NAME!을 배치 안에서 cmd가 환경 변수로 펼칠 수 있습니다(/v:off로 막지 못합니다).";
            break;
        case cmd_warning_kind::batch_call:
            message = argument_name(warning.argument) + u8"이 부르는 배치 파일이 call을 씁니다. call로 다시 넘긴 값은 ^가 두 배가 되고 %가 한 번 더 펼쳐져 인자 경계가 바뀔 수 있습니다.";
            break;
        case cmd_warning_kind::batch_unchecked:
            message = argument_name(warning.argument) + u8"이 부르는 배치 파일을 읽지 못하거나 커서(256KiB 넘음) 지연 확장과 call을 확인하지 못했습니다.";
            break;
        }
        return message;
    }
}
