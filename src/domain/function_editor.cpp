#include "domain/function_editor.h"

#include <algorithm>

namespace gitman {
    namespace {
        [[nodiscard]] constexpr bool is_argument_space(const char8_t value) noexcept
        {
            return value == u8' ' || value == u8'\t';
        }
    }

    function_argument_split split_function_arguments(const std::u8string_view line)
    {
        function_argument_split result {};
        std::size_t index { 0 };
        while (index < line.size())
        {
            while (index < line.size() && is_argument_space(line[index]))
                ++index;
            if (index >= line.size())
                break;

            // 인자 하나다. 따옴표 안의 공백은 인자를 끊지 않는다.
            std::u8string argument {};
            bool quoted { false };
            while (index < line.size() && (quoted || is_argument_space(line[index]) == false))
            {
                const char8_t value { line[index] };
                if (value == u8'\\')
                {
                    std::size_t backslashes { 0 };
                    while (index < line.size() && line[index] == u8'\\')
                    {
                        ++backslashes;
                        ++index;
                    }
                    if (index < line.size() && line[index] == u8'"')
                    {
                        // 따옴표 앞의 역슬래시만 특별하다: 2n개면 n개 + 따옴표, 2n+1개면 n개 + 글자 `"`.
                        argument.append(backslashes / 2, u8'\\');
                        if (backslashes % 2 == 1)
                        {
                            argument.push_back(u8'"');
                            ++index;
                        }
                    }
                    else
                        argument.append(backslashes, u8'\\');
                    continue;
                }
                if (value == u8'"')
                {
                    quoted = quoted == false;
                    ++index;
                    continue;
                }
                argument.push_back(value);
                ++index;
            }
            if (quoted)
            {
                result.unterminated_quote = true;
                return result;
            }
            result.arguments.push_back(std::move(argument));
        }
        return result;
    }

    std::u8string join_function_arguments(const std::span<const std::u8string> arguments)
    {
        std::u8string line {};
        for (const std::u8string& argument : arguments)
        {
            if (line.empty() == false)
                line.push_back(u8' ');
            const bool needs_quotes {
                argument.empty()
                || std::ranges::any_of(argument
                    , [](const char8_t value)
                    {
                        return is_argument_space(value) || value == u8'"';
                    }
                )
            };
            if (needs_quotes == false)
            {
                // 따옴표가 없으면 역슬래시는 글자 그대로 읽힌다.
                line.append(argument);
                continue;
            }

            line.push_back(u8'"');
            std::size_t backslashes { 0 };
            for (const char8_t value : argument)
            {
                if (value == u8'\\')
                {
                    ++backslashes;
                    continue;
                }
                if (value == u8'"')
                {
                    line.append(backslashes * 2 + 1, u8'\\');
                    line.push_back(u8'"');
                }
                else
                {
                    line.append(backslashes, u8'\\');
                    line.push_back(value);
                }
                backslashes = 0;
            }
            // 닫는 따옴표 앞의 역슬래시는 두 배로 해야 따옴표를 글자로 바꾸지 않는다.
            line.append(backslashes * 2, u8'\\');
            line.push_back(u8'"');
        }
        return line;
    }

    std::u8string make_function_button_id(const std::u8string_view base, const std::span<const std::u8string> taken, const std::span<const function_source_text> sources)
    {
        const std::u8string_view stem { base.empty() ? std::u8string_view { u8"function" } : base };
        for (std::size_t number = 1;; ++number)
        {
            std::u8string candidate { stem };
            candidate.push_back(u8'-');
            candidate.append(function_decimal_text(number));
            if (is_function_button_id(candidate) == false)
            {
                // 이름이 길어 64자를 넘으면 기본 이름으로 다시 센다.
                return make_function_button_id(u8"function", taken, sources);
            }
            if (std::ranges::find(taken, candidate) != taken.end())
                continue;
            std::u8string quoted { u8"\"" };
            quoted.append(candidate);
            quoted.push_back(u8'"');
            const bool in_source {
                std::ranges::any_of(sources
                    , [&quoted](const function_source_text& source)
                    {
                        return source.json.find(quoted) != std::u8string::npos;
                    }
                )
            };
            if (in_source == false)
                return candidate;
        }
    }
}
