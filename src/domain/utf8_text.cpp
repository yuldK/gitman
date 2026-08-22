#include "domain/utf8_text.h"

#include <cstdint>

namespace gitman {
    namespace {
        constexpr char32_t maximum_codepoint { 0x10FFFFu };
        constexpr char32_t surrogate_first { 0xD800u };
        constexpr char32_t surrogate_last { 0xDFFFu };

        [[nodiscard]] bool is_continuation(const char8_t byte) noexcept
        {
            return (static_cast<std::uint8_t>(byte) & 0xC0u) == 0x80u;
        }

        [[nodiscard]] bool is_surrogate(const char32_t codepoint) noexcept
        {
            return codepoint >= surrogate_first && codepoint <= surrogate_last;
        }

        // 한 자리를 읽는다. 길이가 0이면 그 자리가 잘못된 것이다. 과잉 인코딩과
        // surrogate까지 걸러야 Skia가 보는 것과 판정이 같아진다.
        [[nodiscard]] utf8_decoded decode_strict(const std::u8string_view text, const std::size_t offset) noexcept
        {
            const std::size_t available { text.size() - offset };
            const auto lead { static_cast<std::uint8_t>(text[offset]) };

            std::size_t length { 0 };
            char32_t codepoint { 0 };
            char32_t minimum { 0 };
            if (lead < 0x80u)
                return { static_cast<char32_t>(lead), 1 };
            if ((lead & 0xE0u) == 0xC0u)
            {
                length = 2;
                codepoint = lead & 0x1Fu;
                minimum = 0x80u;
            }
            else if ((lead & 0xF0u) == 0xE0u)
            {
                length = 3;
                codepoint = lead & 0x0Fu;
                minimum = 0x800u;
            }
            else if ((lead & 0xF8u) == 0xF0u)
            {
                length = 4;
                codepoint = lead & 0x07u;
                minimum = 0x10000u;
            }
            else
                return { utf8_replacement_character, 0 };

            if (available < length)
                return { utf8_replacement_character, 0 };
            for (std::size_t index = 1; index < length; ++index)
            {
                if (is_continuation(text[offset + index]) == false)
                    return { utf8_replacement_character, 0 };
                codepoint = (codepoint << 6) | (static_cast<std::uint8_t>(text[offset + index]) & 0x3Fu);
            }

            if (codepoint < minimum || codepoint > maximum_codepoint || is_surrogate(codepoint))
                return { utf8_replacement_character, 0 };
            return { codepoint, length };
        }
    } // namespace

    bool utf8_is_valid(const std::u8string_view text) noexcept
    {
        std::size_t offset { 0 };
        while (offset < text.size())
        {
            const utf8_decoded decoded { decode_strict(text, offset) };
            if (decoded.length == 0)
                return false;
            offset += decoded.length;
        }
        return true;
    }

    std::u8string utf8_replace_invalid(const std::u8string_view text)
    {
        std::u8string result {};
        result.reserve(text.size());

        std::size_t offset { 0 };
        while (offset < text.size())
        {
            const utf8_decoded decoded { decode_strict(text, offset) };
            if (decoded.length == 0)
            {
                // 잘못된 바이트 하나마다 대체 글자 하나다. 뒤따르는 바이트가 온전한
                // 시작이면 그 자리부터 다시 제대로 읽힌다.
                result.append(utf8_encode(utf8_replacement_character));
                offset += 1;
                continue;
            }
            result.append(text.substr(offset, decoded.length));
            offset += decoded.length;
        }
        return result;
    }

    std::u8string utf8_encode(const char32_t codepoint)
    {
        std::u8string result {};
        if (codepoint > maximum_codepoint || is_surrogate(codepoint))
            return result;

        if (codepoint < 0x80u)
        {
            result.push_back(static_cast<char8_t>(codepoint));
            return result;
        }
        if (codepoint < 0x800u)
        {
            result.push_back(static_cast<char8_t>(0xC0u | (codepoint >> 6)));
            result.push_back(static_cast<char8_t>(0x80u | (codepoint & 0x3Fu)));
            return result;
        }
        if (codepoint < 0x10000u)
        {
            result.push_back(static_cast<char8_t>(0xE0u | (codepoint >> 12)));
            result.push_back(static_cast<char8_t>(0x80u | ((codepoint >> 6) & 0x3Fu)));
            result.push_back(static_cast<char8_t>(0x80u | (codepoint & 0x3Fu)));
            return result;
        }

        result.push_back(static_cast<char8_t>(0xF0u | (codepoint >> 18)));
        result.push_back(static_cast<char8_t>(0x80u | ((codepoint >> 12) & 0x3Fu)));
        result.push_back(static_cast<char8_t>(0x80u | ((codepoint >> 6) & 0x3Fu)));
        result.push_back(static_cast<char8_t>(0x80u | (codepoint & 0x3Fu)));
        return result;
    }

    utf8_decoded utf8_decode(const std::u8string_view text, const std::size_t offset) noexcept
    {
        if (offset >= text.size())
            return {};
        const utf8_decoded decoded { decode_strict(text, offset) };
        return decoded.length == 0 ? utf8_decoded {} : decoded;
    }
} // namespace gitman
