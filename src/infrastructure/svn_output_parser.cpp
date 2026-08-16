#include "infrastructure/svn_output_parser.h"

#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        // 상태 칸의 폭이다. 1번 항목부터 7번 tree conflict까지가 의미 있는 칸이며 그
        // 뒤의 공백을 모두 건너뛰면 경로가 시작된다. 경로 시작 위치를 상수로 고정하지
        // 않는 이유는 배포판마다 칸 하나가 더 붙을 수 있기 때문이다.
        constexpr std::size_t svn_status_column_count { 7 };
        constexpr std::size_t switched_column { 4 };
        constexpr std::size_t tree_conflict_column { 6 };

        std::u8string_view trim_ascii_whitespace(std::u8string_view value) noexcept
        {
            constexpr std::u8string_view whitespace { u8" \t\r" };
            while (value.empty() == false && whitespace.find(value.front()) != std::u8string_view::npos)
                value.remove_prefix(1);
            while (value.empty() == false && whitespace.find(value.back()) != std::u8string_view::npos)
                value.remove_suffix(1);
            return value;
        }

        bool is_digit(const char8_t value) noexcept
        {
            return value >= u8'0' && value <= u8'9';
        }

        // 앞에서부터 숫자를 읽는다. 자릿수가 없으면 실패다.
        bool read_unsigned(std::u8string_view& value, std::uint64_t& result) noexcept
        {
            std::size_t digits { 0 };
            std::uint64_t parsed { 0 };
            while (digits < value.size() && is_digit(value[digits]))
            {
                parsed = parsed * 10u + static_cast<std::uint64_t>(value[digits] - u8'0');
                ++digits;
            }
            if (digits == 0)
                return false;

            value.remove_prefix(digits);
            result = parsed;
            return true;
        }

        // 상태 칸이 모두 공백인 줄은 `> moved from ...` 같은 부가 설명이다. 항목이 아니다.
        bool is_status_detail_line(const std::u8string_view line) noexcept
        {
            for (std::size_t index = 0; index < svn_status_column_count && index < line.size(); ++index)
                if (line[index] != u8' ')
                    return false;
            return true;
        }

        bool is_change_state(const char8_t value) noexcept
        {
            // `I`(무시)와 `X`(외부 항목), `?`(미추적)는 변경으로 세지 않는다.
            return value == u8'M' || value == u8'A' || value == u8'D' || value == u8'R' || value == u8'!' || value == u8'~';
        }
    } // namespace

    bool svn_version_info::mixed_revision() const noexcept
    {
        return parsed && low_revision != high_revision;
    }

    std::u8string parse_svn_info_item(const std::vector<std::u8string>& lines)
    {
        for (const std::u8string& line : lines)
        {
            const std::u8string_view value { trim_ascii_whitespace(line) };
            if (value.empty() == false)
                return std::u8string { value };
        }
        return {};
    }

    svn_status_summary parse_svn_status(const std::vector<std::u8string>& lines)
    {
        svn_status_summary summary {};
        for (const std::u8string& line : lines)
        {
            const std::u8string_view text { line };
            if (trim_ascii_whitespace(text).empty())
                continue;
            if (text.size() <= svn_status_column_count || is_status_detail_line(text))
                continue;

            // 상태 칸 뒤의 공백을 모두 건너뛴 지점이 경로다. 경로에 공백이 있어도
            // 경계가 흔들리지 않는다.
            std::size_t path_start { svn_status_column_count };
            while (path_start < text.size() && text[path_start] == u8' ')
                ++path_start;
            if (path_start >= text.size())
            {
                ++summary.unparsable_records;
                continue;
            }

            svn_status_entry entry {};
            entry.item_state = text[0];
            entry.property_state = text[1];
            entry.switched = text[switched_column] == u8'S';
            entry.tree_conflict = text[tree_conflict_column] == u8'C';
            entry.path = text.substr(path_start);
            summary.entries.push_back(std::move(entry));
        }
        return summary;
    }

    svn_version_info parse_svnversion(const std::u8string_view line)
    {
        svn_version_info info {};
        std::u8string_view value { trim_ascii_whitespace(line) };
        // 작업 복사본이 아니면 `Unversioned directory`처럼 문장을 낸다. 해석하지 않는다.
        if (value.empty() || is_digit(value.front()) == false)
            return info;

        std::uint64_t first { 0 };
        if (read_unsigned(value, first) == false)
            return info;

        info.low_revision = first;
        info.high_revision = first;
        if (value.empty() == false && value.front() == u8':')
        {
            value.remove_prefix(1);
            std::uint64_t second { 0 };
            if (read_unsigned(value, second) == false)
                return {};
            info.high_revision = second;
        }

        for (const char8_t flag : value)
            if (flag == u8'M')
                info.modified = true;
            else if (flag == u8'S')
                info.switched = true;
            else if (flag == u8'P')
                info.partial = true;
            else
                // 모르는 접미사가 붙으면 값을 신뢰하지 않는다.
                return {};

        info.parsed = true;
        return info;
    }

    working_tree_summary summarize_svn_working_tree(const svn_status_summary& status)
    {
        working_tree_summary summary {};
        for (const svn_status_entry& entry : status.entries)
        {
            if (entry.item_state == u8'C' || entry.property_state == u8'C' || entry.tree_conflict)
            {
                ++summary.conflicted_count;
                continue;
            }
            if (entry.item_state == u8'?')
            {
                ++summary.untracked_count;
                continue;
            }
            if (is_change_state(entry.item_state) || entry.property_state == u8'M')
                ++summary.modified_count;
        }

        if (status.unparsable_records > 0)
        {
            // 출력을 다 읽지 못한 작업 복사본을 깨끗하다고 단정하지 않는다.
            summary.state = working_tree_state::unknown;
            return summary;
        }
        if (summary.conflicted_count > 0)
            summary.state = working_tree_state::conflicted;
        else if (summary.modified_count > 0 || summary.untracked_count > 0)
            summary.state = working_tree_state::modified;
        else
            summary.state = working_tree_state::clean;
        return summary;
    }

    bool has_svn_switched_entry(const svn_status_summary& status) noexcept
    {
        for (const svn_status_entry& entry : status.entries)
            if (entry.switched)
                return true;
        return false;
    }
} // namespace gitman
