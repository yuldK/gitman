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

        // 이름 뒤 주격 조사다. 끝 글자에 받침이 있으면 `이`, 없으면 `가`다 — 기능 이름은 사용자가 지으므로 받침을 미리
        // 알 수 없다 (S6 앱 점검의 `기능 "핑"가`). 숫자는 한국어로 읽은 소리로 고르고, 소리를 정할 수 없는 끝 글자(영문,
        // 기호)와 빈 이름은 예전 글 그대로 `가`다.
        std::u8string_view subject_particle(const std::u8string_view label) noexcept
        {
            constexpr std::u8string_view with_final { u8"이" };
            constexpr std::u8string_view without_final { u8"가" };
            if (label.empty())
                return without_final;
            const char8_t last { label.back() };
            if (last >= u8'0' && last <= u8'9')
            {
                // 0 영, 1 일, 3 삼, 6 육, 7 칠, 8 팔은 받침이 있다.
                constexpr std::u8string_view digits_with_final { u8"013678" };
                return digits_with_final.find(last) != std::u8string_view::npos ? with_final : without_final;
            }
            // 한글 음절(U+AC00~U+D7A3)은 UTF-8로 세 바이트다.
            if (label.size() < 3)
                return without_final;
            const std::u8string_view tail { label.substr(label.size() - 3) };
            const auto lead { static_cast<char32_t>(tail[0]) };
            const auto second { static_cast<char32_t>(tail[1]) };
            const auto third { static_cast<char32_t>(tail[2]) };
            if ((lead & 0xF0u) != 0xE0u
                || (second & 0xC0u) != 0x80u
                || (third & 0xC0u) != 0x80u)
                return without_final;
            const char32_t code { ((lead & 0x0Fu) << 12) | ((second & 0x3Fu) << 6) | (third & 0x3Fu) };
            if (code < 0xAC00u || code > 0xD7A3u)
                return without_final;
            // 음절은 (초성 × 21 + 중성) × 28 + 종성 순서라 28로 나눈 나머지가 종성이다. 0이면 받침이 없다.
            return (code - 0xAC00u) % 28u != 0u ? with_final : without_final;
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

        // `실행 중 1개, 실패 2개`다. 0인 상태는 빼고, 둘 다 0이면 비어 있다.
        std::u8string more_status_text(const function_more_status& status)
        {
            std::u8string text {};
            if (status.running > 0)
                text += u8"실행 중 " + decimal_text(static_cast<std::int64_t>(status.running)) + u8"개";
            if (status.failed > 0)
            {
                if (text.empty() == false)
                    text += u8", ";
                text += u8"실패 " + decimal_text(static_cast<std::int64_t>(status.failed)) + u8"개";
            }
            return text;
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

    function_more_status function_more_status_of(const std::vector<card_function_view>& functions, const std::vector<bool>& on_card)
    {
        function_more_status status {};
        for (std::size_t index = 0; index < functions.size(); ++index)
        {
            if (index < on_card.size() && on_card[index])
                continue;
            if (functions[index].running)
                ++status.running;
            else if (functions[index].failed)
                ++status.failed;
        }
        return status;
    }

    std::u8string function_more_access_name(const std::u8string_view card_name, const std::size_t count, const function_more_status& status)
    {
        std::u8string name { more_access_name(u8"기능 더 보기", card_name, count) };
        const std::u8string state { more_status_text(status) };
        if (state.empty() == false)
            name += u8", " + state;
        return name;
    }

    std::u8string function_more_tooltip(const function_more_status& status)
    {
        std::u8string text { u8"기능 더 보기 (Alt+↓)" };
        const std::u8string state { more_status_text(status) };
        if (state.empty() == false)
        {
            text += state_separator;
            text += state;
        }
        return text;
    }

    std::u8string function_bar_more_access_name(const std::u8string_view card_name, const std::size_t count)
    {
        return more_access_name(u8"기능 더 보기 (기능 줄)", card_name, count);
    }

    function_more_marker function_more_marker_state(const function_more_status& status) noexcept
    {
        if (status.failed > 0)
            return function_more_marker::failed;
        return status.running > 0 ? function_more_marker::running : function_more_marker::none;
    }

    function_more_marker function_more_marker_state(const std::vector<card_function_view>& functions, const std::vector<bool>& on_card)
    {
        return function_more_marker_state(function_more_status_of(functions, on_card));
    }

    std::u8string function_running_reason(const std::u8string_view label)
    {
        std::u8string text { u8"기능 \"" };
        text += label;
        text += u8"\"";
        text += subject_particle(label);
        text += u8" 실행 중입니다";
        return text;
    }

    std::u8string function_running_entry_text(const function_running_entry& entry)
    {
        std::u8string text { entry.label };
        text += u8" (";
        text += entry.card_name;
        text += u8")";
        return text;
    }

    std::u8string function_running_summary(const std::vector<function_running_entry>& entries)
    {
        if (entries.empty())
            return u8"실행 중인 기능이 없습니다";
        std::u8string text { function_running_entry_text(entries.front()) };
        if (entries.size() > 1)
        {
            text += u8" 외 ";
            text += decimal_text(static_cast<std::int64_t>(entries.size() - 1));
            text += u8"개";
        }
        return text;
    }
}
