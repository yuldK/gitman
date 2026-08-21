#include "domain/log_file_naming.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string_view>
#include <utility>

namespace gitman {
    namespace {
        constexpr std::u8string_view fallback_folder_name { u8"repository" };

        constexpr bool is_path_separator(const char8_t value) noexcept
        {
            return value == u8'\\' || value == u8'/';
        }

        constexpr bool is_ascii_letter(const char8_t value) noexcept
        {
            return (value >= u8'a' && value <= u8'z') || (value >= u8'A' && value <= u8'Z');
        }

        constexpr char8_t ascii_lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        std::u8string ascii_lowered(const std::u8string_view value)
        {
            std::u8string result {};
            result.reserve(value.size());
            for (const char8_t character : value)
                result.push_back(ascii_lower(character));
            return result;
        }

        // 경로를 뿌리 표식과 구성 요소로 나눈다. 뿌리는 드라이브(`C:` → `c-drive`)
        // 또는 UNC share(`\\server\share` → `server-share`)다.
        struct path_parts
        {
            std::u8string root {};
            std::vector<std::u8string> segments {};
        };

        std::vector<std::u8string> split_segments(const std::u8string_view path)
        {
            std::vector<std::u8string> segments {};
            std::size_t index { 0 };
            while (index < path.size())
            {
                while (index < path.size() && is_path_separator(path[index]))
                    ++index;
                const std::size_t begin { index };
                while (index < path.size() && is_path_separator(path[index]) == false)
                    ++index;
                if (index > begin)
                    segments.emplace_back(path.substr(begin, index - begin));
            }
            return segments;
        }

        path_parts split_path(const std::u8string_view path)
        {
            path_parts parts {};
            if (path.size() >= 2 && is_path_separator(path[0]) && is_path_separator(path[1]))
            {
                std::vector<std::u8string> segments { split_segments(path.substr(2)) };
                if (segments.empty() == false)
                {
                    parts.root = segments.front();
                    segments.erase(segments.begin());
                }
                if (segments.empty() == false)
                {
                    parts.root += u8"-";
                    parts.root += segments.front();
                    segments.erase(segments.begin());
                }
                parts.segments = std::move(segments);
                return parts;
            }

            if (path.size() >= 2 && is_ascii_letter(path[0]) && path[1] == u8':')
            {
                parts.root.push_back(ascii_lower(path[0]));
                parts.root += u8"-drive";
                parts.segments = split_segments(path.substr(2));
                return parts;
            }

            parts.segments = split_segments(path);
            return parts;
        }

        // 마지막 `depth`개 구성 요소를 `-`로 잇고, 뿌리를 포함하면 `_`로 앞에 붙인다.
        std::u8string assemble_name(const path_parts& parts, const std::size_t depth, const bool with_root)
        {
            std::u8string joined {};
            const std::size_t count { depth > parts.segments.size() ? parts.segments.size() : depth };
            for (std::size_t index = parts.segments.size() - count; index < parts.segments.size(); ++index)
            {
                if (joined.empty() == false)
                    joined += u8"-";
                joined += parts.segments[index];
            }

            if (with_root == false || parts.root.empty())
                return joined;
            if (joined.empty())
                return parts.root;
            return parts.root + u8"_" + joined;
        }

        std::u8string decimal_text(const std::size_t value)
        {
            std::u8string digits {};
            std::size_t remaining { value };
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                remaining /= 10;
            } while (remaining > 0);
            return digits;
        }

        std::u8string hash_suffix(const std::u8string_view value)
        {
            std::uint32_t hash { 2166136261U };
            for (const char8_t character : value)
            {
                hash ^= static_cast<std::uint32_t>(character);
                hash *= 16777619U;
            }

            constexpr std::u8string_view digits { u8"0123456789abcdef" };
            std::u8string result {};
            for (int shift = 28; shift >= 0; shift -= 4)
                result.push_back(digits[(hash >> static_cast<std::uint32_t>(shift)) & 0xFU]);
            return result;
        }

        bool is_reserved_device_name(const std::u8string_view name) noexcept
        {
            constexpr std::array<std::u8string_view, 4> simple { u8"con", u8"prn", u8"aux", u8"nul" };
            const std::size_t dot { name.find(u8'.') };
            const std::u8string base { ascii_lowered(dot == std::u8string_view::npos ? name : name.substr(0, dot)) };
            for (const std::u8string_view reserved : simple)
                if (base == reserved)
                    return true;
            if (base.size() == 4 && (base[3] >= u8'1' && base[3] <= u8'9'))
            {
                const std::u8string_view prefix { base.data(), 3 };
                return prefix == u8"com" || prefix == u8"lpt";
            }
            return false;
        }

        // Windows 파일 이름 규칙에 맞춘다. 금지 문자는 `_`로 바꾸고 끝의 `.`과 공백을
        // 지우며, 예약 이름은 `_`를 붙이고, 너무 길면 앞부분 + 경로 해시로 자른다.
        std::u8string sanitize_folder_name(const std::u8string_view name)
        {
            std::u8string result {};
            result.reserve(name.size());
            for (const char8_t character : name)
            {
                const bool forbidden {
                    character < 0x20 || character == u8'<' || character == u8'>' || character == u8':' || character == u8'"' || character == u8'/' || character == u8'\\' || character == u8'|'
                        || character == u8'?' || character == u8'*',
                };
                result.push_back(forbidden ? u8'_' : character);
            }

            while (result.empty() == false && (result.back() == u8'.' || result.back() == u8' '))
                result.pop_back();
            if (result.empty())
                result = fallback_folder_name;
            if (is_reserved_device_name(result))
                result.push_back(u8'_');

            if (result.size() > log_folder_name_limit)
            {
                std::size_t cut { log_folder_name_limit };
                // UTF-8 이어지는 바이트(10xxxxxx) 한가운데를 자르지 않는다.
                while (cut > 0 && (result[cut] & 0xC0U) == 0x80U)
                    --cut;
                std::u8string trimmed { result.substr(0, cut) };
                trimmed += u8"-";
                trimmed += hash_suffix(result);
                result = std::move(trimmed);
            }
            return result;
        }

        struct name_state
        {
            path_parts parts {};
            std::size_t depth { 1 };
            bool with_root { false };
            bool exhausted { false };
            std::u8string name {};
        };
    } // namespace

    std::u8string log_root_path(const std::u8string_view document_path)
    {
        // 이름 앞에 `.`을 붙여 탐색기에서 문서·저장소 폴더 사이에 섞이지 않고 맨
        // 앞에 모이도록 한다 (2026-08-22 사용자 지시).
        const std::size_t separator { document_path.find_last_of(u8"\\/") };
        std::u8string result { separator == std::u8string_view::npos ? std::u8string {} : std::u8string { document_path.substr(0, separator + 1) } };
        result.push_back(u8'.');
        result.append(separator == std::u8string_view::npos ? document_path : document_path.substr(separator + 1));
        result.append(u8".log");
        return result;
    }

    std::vector<std::u8string> log_folder_names(const std::span<const std::u8string> repository_paths)
    {
        std::vector<name_state> states {};
        states.reserve(repository_paths.size());
        for (const std::u8string& path : repository_paths)
        {
            name_state state {};
            state.parts = split_path(path);
            // 구성 요소가 없으면(뿌리만 있거나 빈 경로) 뿌리부터 쓴다.
            state.with_root = state.parts.segments.empty();
            state.name = assemble_name(state.parts, state.depth, state.with_root);
            if (state.name.empty())
                state.name = fallback_folder_name;
            states.push_back(std::move(state));
        }

        // 겹치는 것들만 상위 세그먼트를 하나씩 늘린다. 더 늘릴 수 없으면 뿌리를
        // 붙이고, 그래도 겹치면 뒤에 번호를 붙인다.
        for (std::size_t round = 0; round < repository_paths.size() + 2; ++round)
        {
            // 겹침 판정은 이번 회차의 이름 전체를 기준으로 한 번에 한다. 한 항목을
            // 늘리는 도중에 다시 판정하면 짝이 되는 항목이 그대로 남는다.
            std::vector<bool> collides(states.size(), false);
            for (std::size_t index = 0; index < states.size(); ++index)
                for (std::size_t other = index + 1; other < states.size(); ++other)
                    if (states[index].name == states[other].name)
                    {
                        collides[index] = true;
                        collides[other] = true;
                    }

            bool changed { false };
            for (std::size_t index = 0; index < states.size(); ++index)
            {
                if (states[index].exhausted || collides[index] == false)
                    continue;

                name_state& state { states[index] };
                if (state.depth < state.parts.segments.size())
                    ++state.depth;
                else if (state.with_root == false && state.parts.root.empty() == false)
                    state.with_root = true;
                else
                {
                    state.exhausted = true;
                    continue;
                }
                state.name = assemble_name(state.parts, state.depth, state.with_root);
                if (state.name.empty())
                    state.name = fallback_folder_name;
                changed = true;
            }
            if (changed == false)
                break;
        }

        std::vector<std::u8string> names {};
        names.reserve(states.size());
        for (const name_state& state : states)
            names.push_back(sanitize_folder_name(state.name));

        // 정리 후에도 같아진 이름은 뒤에 번호를 붙여 구분한다.
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            std::size_t suffix { 1 };
            while (true)
            {
                bool duplicate { false };
                for (std::size_t other = 0; other < index; ++other)
                    if (names[other] == names[index])
                        duplicate = true;
                if (duplicate == false)
                    break;
                ++suffix;
                names[index] = sanitize_folder_name(names[index] + u8"-" + decimal_text(suffix));
            }
        }
        return names;
    }

    std::u8string log_file_name(const std::chrono::system_clock::time_point time, const std::size_t attempt)
    {
        const std::time_t seconds { std::chrono::system_clock::to_time_t(time) };
        std::tm parts {};
        if (localtime_s(&parts, &seconds) != 0)
            return std::u8string { u8"log.log" };

        char text[32] {};
        const int written { std::snprintf(text, sizeof(text), "%04d%02d%02d-%02d%02d%02d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec) };
        if (written != 15)
            return std::u8string { u8"log.log" };

        std::u8string name { reinterpret_cast<const char8_t*>(text), 15 };
        if (attempt > 0)
        {
            name += u8"-";
            name += decimal_text(attempt + 1);
        }
        name += u8".log";
        return name;
    }

    std::u8string format_log_file_line(const operation_log_entry& entry)
    {
        const std::time_t seconds { std::chrono::system_clock::to_time_t(entry.time) };
        std::tm parts {};
        bool have_time { localtime_s(&parts, &seconds) == 0 };
        const auto milliseconds {
            std::chrono::duration_cast<std::chrono::milliseconds>(entry.time.time_since_epoch()) % std::chrono::seconds { 1 },
        };

        char stamp[32] {};
        if (have_time)
        {
            const int written {
                std::snprintf(stamp, sizeof(stamp), "%04d-%02d-%02d %02d:%02d:%02d.%03d", parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour, parts.tm_min, parts.tm_sec,
                    static_cast<int>(milliseconds.count())),
            };

            if (written <= 0)
                have_time = false;
        }

        std::u8string line {};
        if (have_time)
            line.append(reinterpret_cast<const char8_t*>(stamp));
        else
            line.append(u8"---------- --:--:--.---");
        line += u8" [";
        line += log_entry_kind_name(entry.kind);
        line += u8"/";
        line += diagnostic_severity_name(entry.severity);
        line += u8"] ";
        line += entry.text;
        return line;
    }
} // namespace gitman
