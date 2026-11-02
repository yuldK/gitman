#include "presentation/function_presentation.h"

#include "presentation/log_presentation.h"

#include <string>

namespace gitman {
    namespace {
        constexpr std::u8string_view state_separator { u8" — " };

        std::u8string decimal_text(const std::int64_t value)
        {
            const std::string digits { std::to_string(value) };
            return std::u8string { digits.begin(), digits.end() };
        }

        // 실패 꼬리다. 종료 코드가 없으면 실패라는 사실만 적는다.
        std::u8string failure_text(const card_function_view& function, const std::u8string_view prefix)
        {
            std::u8string text { prefix };
            if (function.exit_code.has_value())
            {
                text += u8": 종료 코드 ";
                text += function_exit_code_text(*function.exit_code);
            }
            return text;
        }

        std::u8string more_access_name(const std::u8string_view prefix, const std::u8string_view card_name, const std::size_t count)
        {
            std::u8string name { prefix };
            name += u8", ";
            name += card_name;
            name += u8", ";
            name += decimal_text(static_cast<std::int64_t>(count));
            name += u8"개";
            return name;
        }
    }

    std::u8string function_exit_code_text(const std::int32_t code)
    {
        if (code >= 0)
            return decimal_text(code);
        constexpr std::u8string_view digits { u8"0123456789ABCDEF" };
        const std::uint32_t bits { static_cast<std::uint32_t>(code) };
        std::u8string text { u8"0x" };
        for (int shift = 28; shift >= 0; shift -= 4)
            text.push_back(digits[(bits >> shift) & 0xFu]);
        return text;
    }

    std::u8string function_shortcut_text(const std::size_t number)
    {
        if (number == 0)
            return {};
        return u8"Alt+" + decimal_text(static_cast<std::int64_t>(number));
    }

    std::u8string function_menu_label(const card_function_view& function, const std::chrono::steady_clock::duration elapsed)
    {
        std::u8string label { function.label };
        if (function.running)
        {
            label += state_separator;
            label += u8"실행 중 ";
            label += format_elapsed_time(elapsed);
            return label;
        }
        if (function.enabled == false)
        {
            if (function.disabled_reason.empty() == false)
            {
                label += state_separator;
                label += function.disabled_reason;
            }
            return label;
        }
        if (function.failed)
        {
            label += state_separator;
            label += u8"실패";
            if (function.exit_code.has_value())
            {
                label += u8" (종료 코드 ";
                label += function_exit_code_text(*function.exit_code);
                label += u8")";
            }
        }
        return label;
    }

    std::u8string function_button_access_name(const card_function_view& function, const std::u8string_view card_name)
    {
        std::u8string name { function.label };
        name += u8", ";
        name += card_name;
        if (function.running)
            name += u8", 실행 중";
        else if (function.failed)
            name += u8", " + failure_text(function, u8"마지막 실패");
        return name;
    }

    std::u8string function_button_tooltip(const card_function_view& function)
    {
        std::u8string text { function.label };
        if (function.running)
        {
            text += state_separator;
            text += u8"실행 중입니다. 누르면 중지합니다";
            return text;
        }
        if (function.enabled == false)
        {
            if (function.disabled_reason.empty() == false)
            {
                text += state_separator;
                text += function.disabled_reason;
            }
            return text;
        }
        if (function.failed)
        {
            text += state_separator;
            text += failure_text(function, u8"마지막 실행이 실패했습니다");
            return text;
        }
        if (function.shortcut > 0)
            text += u8" (" + function_shortcut_text(function.shortcut) + u8")";
        return text;
    }

    std::u8string function_bar_text(const card_function_view& function)
    {
        std::u8string text { function.label };
        if (function.running)
            text += u8" · 실행 중";
        else if (function.failed)
            text += u8" · 실패";
        return text;
    }

    std::u8string function_bar_widest_text(const std::u8string_view label)
    {
        std::u8string text { label };
        text += u8" · 실행 중";
        return text;
    }

    std::u8string function_more_access_name(const std::u8string_view card_name, const std::size_t count)
    {
        return more_access_name(u8"기능 더 보기", card_name, count);
    }

    std::u8string function_bar_more_access_name(const std::u8string_view card_name, const std::size_t count)
    {
        return more_access_name(u8"기능 더 보기 (기능 줄)", card_name, count);
    }

    function_more_marker function_more_marker_state(const std::vector<card_function_view>& functions, const std::vector<bool>& on_card)
    {
        bool running { false };
        for (std::size_t index = 0; index < functions.size(); ++index)
        {
            if (index < on_card.size() && on_card[index])
                continue;
            if (functions[index].failed)
                return function_more_marker::failed;
            running = running || functions[index].running;
        }
        return running ? function_more_marker::running : function_more_marker::none;
    }

    std::u8string function_running_reason(const std::u8string_view label)
    {
        std::u8string text { u8"기능 \"" };
        text += label;
        text += u8"\"가 실행 중입니다";
        return text;
    }
}
