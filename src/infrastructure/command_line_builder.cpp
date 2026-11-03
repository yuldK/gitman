#include "infrastructure/command_line_builder.h"

#include <cstddef>

namespace gitman {
    namespace {
        constexpr std::u8string_view characters_requiring_quotes { u8" \t\n\v\"" };
        // cmd가 따옴표 밖에서 해석하는 문자, 배치 인자(`%1`)를 가르는 문자, 감싸야 펼친 값이 따옴표 안에 머무는
        // `%`·`!`다. 따옴표 안이면 cmd도 배치의 `%*`도 이 문자를 값으로 둔다 (2026-11-03 실측, R1).
        constexpr std::u8string_view cmd_characters_requiring_quotes { u8" \t\v\f&|<>^(),;=%!" };

        bool needs_quotes(const std::u8string_view argument) noexcept
        {
            // 빈 인자는 인용하지 않으면 사라지므로 항상 `""`로 만든다.
            return argument.empty() || argument.find_first_of(characters_requiring_quotes) != std::u8string_view::npos;
        }

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

        void append_cmd_argument(std::u8string& target, const std::u8string_view argument)
        {
            if (argument.empty() == false && argument.find_first_of(cmd_characters_requiring_quotes) == std::u8string_view::npos)
            {
                target.append(argument);
                return;
            }

            // 값에 `"`가 없으므로(부르는 쪽이 거부한다) 끝의 `\`만 두 배로 늘리면 `CommandLineToArgvW`가 원래 값을 읽는다.
            // cmd는 `\`를 특별히 보지 않는다.
            std::size_t trailing_backslashes { 0 };
            while (trailing_backslashes < argument.size() && argument[argument.size() - 1 - trailing_backslashes] == u8'\\')
                ++trailing_backslashes;
            target.push_back(u8'"');
            target.append(argument);
            target.append(trailing_backslashes, u8'\\');
            target.push_back(u8'"');
        }
    } // namespace

    void append_windows_command_line_argument(std::u8string& target, const std::u8string_view argument)
    {
        if (needs_quotes(argument) == false)
        {
            target.append(argument);
            return;
        }

        target.push_back(u8'"');
        std::size_t index { 0 };
        while (index < argument.size())
        {
            std::size_t backslashes { 0 };
            while (index < argument.size() && argument[index] == u8'\\')
            {
                ++backslashes;
                ++index;
            }

            if (index == argument.size())
            {
                // 닫는 따옴표 바로 앞의 backslash는 두 배로 늘려야 문자로 남는다.
                target.append(backslashes * 2, u8'\\');
                break;
            }
            if (argument[index] == u8'"')
            {
                // 따옴표를 이스케이프하는 backslash도 두 배로 늘린 뒤 `\"`를 만든다.
                target.append(backslashes * 2 + 1, u8'\\');
                target.push_back(u8'"');
            }
            else
            {
                target.append(backslashes, u8'\\');
                target.push_back(argument[index]);
            }
            ++index;
        }
        target.push_back(u8'"');
    }

    std::u8string build_windows_command_line(const std::u8string_view executable, const std::span<const std::u8string> arguments)
    {
        // argv[0]에는 backslash 이스케이프 규칙이 적용되지 않으므로 그대로 감싼다.
        // 실제 실행 대상은 `lpApplicationName`이 결정하므로 argv[0]은 표시용 값이다.
        std::u8string command_line {};
        command_line.push_back(u8'"');
        command_line.append(executable);
        command_line.push_back(u8'"');

        for (const std::u8string& argument : arguments)
        {
            command_line.push_back(u8' ');
            append_windows_command_line_argument(command_line, argument);
        }
        return command_line;
    }

    std::u8string build_cmd_command_line(const std::u8string_view executable, const std::span<const std::u8string> arguments)
    {
        // cmd는 따옴표로 감싼 argv[0] 안의 `/`도 스위치로 읽는다. `"C:/Windows/System32/cmd.exe"`면 `/cmd.exe`를
        // `/c`로 읽어 내장 명령 `md`가 나머지를 받는다 (2026-11-03 실측). 그래서 argv[0]의 `/`를 `\`로 쓴다. 실행
        // 대상은 `lpApplicationName`이 정하므로 argv[0]의 구분자는 실행할 파일을 바꾸지 않는다.
        std::u8string command_line {};
        command_line.push_back(u8'"');
        for (const char8_t value : executable)
            command_line.push_back(value == u8'/' ? u8'\\' : value);
        command_line.push_back(u8'"');

        std::size_t index { 0 };
        bool has_s_switch { false };
        std::u8string_view command_switch {};
        while (index < arguments.size() && is_cmd_switch(arguments[index]))
        {
            const std::u8string_view argument { arguments[index] };
            ++index;
            if (is_cmd_command_switch(argument))
            {
                command_switch = argument;
                break;
            }
            has_s_switch = has_s_switch || is_cmd_s_switch(argument);
            command_line.push_back(u8' ');
            command_line.append(argument);
        }
        if (command_switch.empty() && index == arguments.size())
            return command_line;
        if (command_switch.empty())
            command_switch = u8"/c";

        // `/s`가 있으면 cmd는 `/c` 뒤 첫 따옴표와 마지막 따옴표만 벗기고 그 사이를 그대로 명령으로 쓴다. `/s`가
        // 없으면 따옴표가 셋 이상이거나 특수 문자가 있을 때 앞뒤를 벗겨 `"a b.bat" "c d"`가 깨진다.
        if (has_s_switch == false)
            command_line.append(u8" /s");
        command_line.push_back(u8' ');
        command_line.append(command_switch);
        command_line.append(u8" \"");
        const std::size_t first { index };
        for (; index < arguments.size(); ++index)
        {
            if (index != first)
                command_line.push_back(u8' ');
            append_cmd_argument(command_line, arguments[index]);
        }
        command_line.push_back(u8'"');
        return command_line;
    }

    std::u8string build_process_command_line(const std::u8string_view executable, const std::span<const std::u8string> arguments, const command_line_syntax syntax)
    {
        if (syntax == command_line_syntax::cmd)
            return build_cmd_command_line(executable, arguments);
        return build_windows_command_line(executable, arguments);
    }
} // namespace gitman
