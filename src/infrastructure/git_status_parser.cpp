#include "infrastructure/git_status_parser.h"

#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        constexpr std::u8string_view branch_header_prefix { u8"# " };
        constexpr std::u8string_view initial_commit_marker { u8"(initial)" };
        constexpr std::u8string_view detached_head_marker { u8"(detached)" };
        // porcelain v2 레코드의 경로 앞에 오는 고정 필드 수다.
        constexpr std::size_t ordinary_field_count { 8 };
        constexpr std::size_t rename_field_count { 9 };
        constexpr std::size_t unmerged_field_count { 10 };
        constexpr std::size_t untracked_field_count { 1 };

        bool parse_boolean(const std::u8string_view value) noexcept
        {
            return value == u8"true";
        }

        bool parse_unsigned(const std::u8string_view value, std::uint64_t& result) noexcept
        {
            if (value.empty())
                return false;

            std::uint64_t parsed { 0 };
            for (const char8_t character : value)
            {
                if (character < u8'0' || character > u8'9')
                    return false;
                parsed = parsed * 10u + static_cast<std::uint64_t>(character - u8'0');
            }
            result = parsed;
            return true;
        }

        // 앞에서 `count`개의 공백 구분 필드를 떼어 내고 나머지를 `rest`에 남긴다. 경로에는
        // 공백이 들어갈 수 있으므로 나머지는 자르지 않는다.
        bool split_leading_fields(const std::u8string_view line, const std::size_t count, std::vector<std::u8string_view>& fields, std::u8string_view& rest)
        {
            fields.clear();
            std::u8string_view remaining { line };
            for (std::size_t index = 0; index < count; ++index)
            {
                const std::size_t separator { remaining.find(u8' ') };
                if (separator == std::u8string_view::npos)
                    return false;
                fields.push_back(remaining.substr(0, separator));
                remaining.remove_prefix(separator + 1);
            }
            rest = remaining;
            return rest.empty() == false;
        }

        void parse_branch_header(const std::u8string_view line, git_status_summary& summary)
        {
            const std::u8string_view body { line.substr(branch_header_prefix.size()) };
            const std::size_t separator { body.find(u8' ') };
            if (separator == std::u8string_view::npos)
                return;

            const std::u8string_view key { body.substr(0, separator) };
            const std::u8string_view value { body.substr(separator + 1) };
            if (key == u8"branch.oid")
            {
                summary.has_branch_header = true;
                // 커밋이 하나도 없는 저장소다. 리비전을 표시할 값이 아직 없다.
                summary.unborn = value == initial_commit_marker;
                if (summary.unborn == false)
                    summary.oid = value;
                return;
            }
            if (key == u8"branch.head")
            {
                summary.has_branch_header = true;
                summary.head = value;
                summary.detached = value == detached_head_marker;
                return;
            }
            if (key == u8"branch.upstream")
            {
                summary.has_branch_header = true;
                summary.upstream = value;
                return;
            }
            if (key != u8"branch.ab")
                return;

            // `+<ahead> -<behind>` 형식이다.
            const std::size_t counts_separator { value.find(u8' ') };
            if (counts_separator == std::u8string_view::npos)
                return;

            const std::u8string_view ahead { value.substr(0, counts_separator) };
            const std::u8string_view behind { value.substr(counts_separator + 1) };
            if (ahead.starts_with(u8'+') == false || behind.starts_with(u8'-') == false)
                return;

            std::uint64_t ahead_count { 0 };
            std::uint64_t behind_count { 0 };
            if (parse_unsigned(ahead.substr(1), ahead_count) == false || parse_unsigned(behind.substr(1), behind_count) == false)
                return;

            summary.has_branch_header = true;
            summary.has_ahead_behind = true;
            summary.ahead = ahead_count;
            summary.behind = behind_count;
        }

        bool fill_change_states(const std::u8string_view states, git_status_entry& entry) noexcept
        {
            if (states.size() != 2)
                return false;
            entry.index_state = states[0];
            entry.work_tree_state = states[1];
            return true;
        }

        bool parse_tracked_entry(const std::u8string_view line, const git_status_entry_kind kind, const std::size_t field_count, git_status_entry& entry)
        {
            std::vector<std::u8string_view> fields {};
            std::u8string_view rest {};
            if (split_leading_fields(line, field_count, fields, rest) == false)
                return false;
            if (fill_change_states(fields[1], entry) == false)
                return false;

            entry.kind = kind;
            if (kind != git_status_entry_kind::renamed_or_copied)
            {
                entry.path = unquote_git_path(rest);
                return true;
            }

            // rename과 copy 레코드는 `<path><TAB><original path>`다. 경로에 TAB이 들어가면
            // Git이 그 경로를 인용하고 TAB을 `\t`로 바꾸므로 첫 TAB이 곧 구분자다.
            const std::size_t separator { rest.find(u8'\t') };
            if (separator == std::u8string_view::npos)
            {
                entry.path = unquote_git_path(rest);
                return true;
            }
            entry.path = unquote_git_path(rest.substr(0, separator));
            entry.original_path = unquote_git_path(rest.substr(separator + 1));
            return true;
        }

        bool parse_untracked_entry(const std::u8string_view line, const git_status_entry_kind kind, git_status_entry& entry)
        {
            std::vector<std::u8string_view> fields {};
            std::u8string_view rest {};
            if (split_leading_fields(line, untracked_field_count, fields, rest) == false)
                return false;

            entry.kind = kind;
            entry.path = unquote_git_path(rest);
            return true;
        }

        bool parse_status_entry(const std::u8string_view line, git_status_entry& entry)
        {
            switch (line.front())
            {
            case u8'1':
                return parse_tracked_entry(line, git_status_entry_kind::ordinary, ordinary_field_count, entry);
            case u8'2':
                return parse_tracked_entry(line, git_status_entry_kind::renamed_or_copied, rename_field_count, entry);
            case u8'u':
                return parse_tracked_entry(line, git_status_entry_kind::unmerged, unmerged_field_count, entry);
            case u8'?':
                return parse_untracked_entry(line, git_status_entry_kind::untracked, entry);
            case u8'!':
                return parse_untracked_entry(line, git_status_entry_kind::ignored, entry);
            default:
                return false;
            }
        }

        char8_t unescape_control_character(const char8_t escape) noexcept
        {
            switch (escape)
            {
            case u8'a':
                return 0x07;
            case u8'b':
                return 0x08;
            case u8'f':
                return 0x0C;
            case u8'n':
                return 0x0A;
            case u8'r':
                return 0x0D;
            case u8't':
                return 0x09;
            case u8'v':
                return 0x0B;
            default:
                return escape;
            }
        }

        bool is_octal_digit(const char8_t value) noexcept
        {
            return value >= u8'0' && value <= u8'7';
        }
    } // namespace

    std::u8string unquote_git_path(const std::u8string_view value)
    {
        if (value.size() < 2 || value.front() != u8'"' || value.back() != u8'"')
            return std::u8string { value };

        const std::u8string_view body { value.substr(1, value.size() - 2) };
        std::u8string result {};
        result.reserve(body.size());

        std::size_t index { 0 };
        while (index < body.size())
        {
            const char8_t character { body[index] };
            if (character != u8'\\')
            {
                result.push_back(character);
                ++index;
                continue;
            }

            ++index;
            if (index >= body.size())
            {
                // 닫는 따옴표 앞에서 끊긴 이스케이프다. 원문을 그대로 남긴다.
                result.push_back(u8'\\');
                break;
            }

            const char8_t escape { body[index] };
            if (is_octal_digit(escape) == false)
            {
                // `\"`와 `\\`는 문자 자체이며 나머지는 제어 문자다.
                result.push_back(unescape_control_character(escape));
                ++index;
                continue;
            }

            // `\ooo`는 byte 하나다. 자릿수가 모자라면 있는 만큼만 읽는다.
            std::uint32_t code { 0 };
            std::size_t digits { 0 };
            while (digits < 3 && index < body.size() && is_octal_digit(body[index]))
            {
                code = code * 8u + static_cast<std::uint32_t>(body[index] - u8'0');
                ++index;
                ++digits;
            }
            result.push_back(static_cast<char8_t>(code & 0xFFu));
        }
        return result;
    }

    git_repository_layout parse_git_repository_layout(const std::vector<std::u8string>& lines)
    {
        git_repository_layout layout {};
        // bare 저장소는 `--show-toplevel`에서 실패해 세 줄만 남는다. 앞의 세 값만으로도
        // 배치를 판정할 수 있으므로 이를 성공한 해석으로 다룬다.
        if (lines.size() < 3)
            return layout;

        layout.git_directory = lines[0];
        layout.bare = parse_boolean(lines[1]);
        layout.inside_work_tree = parse_boolean(lines[2]);
        if (lines.size() >= 4)
            layout.work_tree_root = lines[3];
        layout.parsed = layout.git_directory.empty() == false;
        return layout;
    }

    git_status_summary parse_git_status_porcelain_v2(const std::vector<std::u8string>& lines)
    {
        git_status_summary summary {};
        for (const std::u8string& line : lines)
        {
            if (line.empty())
                continue;
            if (std::u8string_view { line }.starts_with(branch_header_prefix))
            {
                // 알 수 없는 헤더는 무시한다. Git이 헤더를 추가해도 기존 판정이 깨지지
                // 않아야 한다.
                parse_branch_header(line, summary);
                continue;
            }

            git_status_entry entry {};
            if (parse_status_entry(line, entry) == false)
            {
                ++summary.unparsable_records;
                continue;
            }
            summary.entries.push_back(std::move(entry));
        }
        return summary;
    }

    working_tree_summary summarize_git_working_tree(const git_status_summary& status)
    {
        working_tree_summary summary {};
        summary.is_detached = status.detached;
        for (const git_status_entry& entry : status.entries)
        {
            switch (entry.kind)
            {
            case git_status_entry_kind::ordinary:
            case git_status_entry_kind::renamed_or_copied:
                if (entry.index_state != u8'.' || entry.work_tree_state != u8'.')
                    ++summary.modified_count;
                break;
            case git_status_entry_kind::unmerged:
                ++summary.conflicted_count;
                break;
            case git_status_entry_kind::untracked:
                ++summary.untracked_count;
                break;
            case git_status_entry_kind::ignored:
                break;
            }
        }

        if (status.unparsable_records > 0 || status.has_branch_header == false)
        {
            // 출력을 다 읽지 못했으므로 깨끗하다고 단정하지 않는다.
            summary.state = working_tree_state::unknown;
            return summary;
        }
        if (summary.conflicted_count > 0)
            summary.state = working_tree_state::conflicted;
        else if (summary.modified_count > 0 || summary.untracked_count > 0)
            // untracked만 있는 경우도 `modified`로 본다. 개수를 따로 두므로 카드는 둘을
            // 구분해 표시할 수 있다.
            summary.state = working_tree_state::modified;
        else
            summary.state = working_tree_state::clean;
        return summary;
    }
} // namespace gitman
