#include "infrastructure/command_line_builder.h"

#include "domain/cmd_command_line.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace gitman {
    namespace {
        constexpr std::u8string_view characters_requiring_quotes { u8" \t\n\v\"" };

        bool needs_quotes(const std::u8string_view argument) noexcept
        {
            // 빈 인자는 인용하지 않으면 사라지므로 항상 `""`로 만든다.
            return argument.empty() || argument.find_first_of(characters_requiring_quotes) != std::u8string_view::npos;
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
        // 출처를 모르는 인자라 모두 값 조각이다. 명령 문장(글자 그대로의 인자)은 기능 버튼의 logic이 짓고
        // 요청의 `command_line`으로 넘긴다 (function-buttons-design 4.3, D30).
        cmd_text_piece program {};
        program.text = executable;
        std::vector<cmd_argument> pieces {};
        pieces.reserve(arguments.size());
        for (const std::u8string& argument : arguments)
            pieces.push_back({ { cmd_text_piece { argument, false, std::nullopt } } });
        return assemble_cmd_command_line(program, pieces).text;
    }

    std::u8string build_process_command_line(const std::u8string_view executable, const std::span<const std::u8string> arguments, const command_line_syntax syntax)
    {
        if (syntax == command_line_syntax::cmd)
            return build_cmd_command_line(executable, arguments);
        return build_windows_command_line(executable, arguments);
    }
} // namespace gitman
