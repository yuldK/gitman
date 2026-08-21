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

        // `svn info --xml`은 우리가 만든 요청에서만 오므로 문서 전체를 해석하는 XML
        // 파서 없이 요소를 문자열로 찾는 좁은 해석이면 충분하다.
        std::u8string xml_unescape(const std::u8string_view text)
        {
            std::u8string value {};
            value.reserve(text.size());
            for (std::size_t index = 0; index < text.size();)
            {
                if (text[index] != u8'&')
                {
                    value.push_back(text[index]);
                    ++index;
                    continue;
                }
                struct xml_entity
                {
                    std::u8string_view name {};
                    char8_t replacement {};
                };
                constexpr xml_entity entities[] {
                    { u8"&amp;", u8'&' }, { u8"&lt;", u8'<' }, { u8"&gt;", u8'>' }, { u8"&quot;", u8'"' }, { u8"&apos;", u8'\'' },
                };
                const std::u8string_view rest { text.substr(index) };
                bool replaced { false };
                for (const xml_entity& candidate : entities)
                {
                    if (rest.starts_with(candidate.name))
                    {
                        value.push_back(candidate.replacement);
                        index += candidate.name.size();
                        replaced = true;
                        break;
                    }
                }
                if (replaced == false)
                {
                    value.push_back(text[index]);
                    ++index;
                }
            }
            return value;
        }

        std::u8string first_element_text(const std::u8string_view xml, const std::u8string_view open_tag, const std::u8string_view close_tag)
        {
            const std::size_t open { xml.find(open_tag) };
            if (open == std::u8string_view::npos)
                return {};
            const std::size_t begin { open + open_tag.size() };
            const std::size_t end { xml.find(close_tag, begin) };
            if (end == std::u8string_view::npos)
                return {};
            return xml_unescape(trim_ascii_whitespace(xml.substr(begin, end - begin)));
        }

        // 첫 `open_tag` 요소의 `revision` attribute다. 다른 요소의 같은 attribute와
        // 섞이지 않도록 여는 tag 안에서만 찾는다.
        std::u8string first_revision_attribute(const std::u8string_view xml, const std::u8string_view open_tag)
        {
            const std::size_t element { xml.find(open_tag) };
            if (element == std::u8string_view::npos)
                return {};
            const std::size_t tag_end { xml.find(u8'>', element) };
            const std::u8string_view attributes { xml.substr(element, tag_end == std::u8string_view::npos ? xml.size() - element : tag_end - element) };
            constexpr std::u8string_view name { u8"revision=\"" };
            const std::size_t value_start { attributes.find(name) };
            if (value_start == std::u8string_view::npos)
                return {};
            const std::size_t begin { value_start + name.size() };
            const std::size_t end { attributes.find(u8'"', begin) };
            if (end == std::u8string_view::npos)
                return {};
            return std::u8string { attributes.substr(begin, end - begin) };
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

    svn_info_fields parse_svn_info_xml(const std::vector<std::u8string>& lines)
    {
        std::u8string xml {};
        for (const std::u8string& line : lines)
        {
            xml.append(line);
            xml.push_back(u8'\n');
        }
        svn_info_fields fields {};
        fields.url = first_element_text(xml, u8"<url>", u8"</url>");
        fields.relative_url = first_element_text(xml, u8"<relative-url>", u8"</relative-url>");
        fields.repository_root = first_element_text(xml, u8"<root>", u8"</root>");
        fields.repository_uuid = first_element_text(xml, u8"<uuid>", u8"</uuid>");
        fields.working_copy_root = first_element_text(xml, u8"<wcroot-abspath>", u8"</wcroot-abspath>");
        // entry attribute는 WC 리비전, commit attribute는 이 노드의 마지막 커밋
        // 리비전이다.
        fields.revision = first_revision_attribute(xml, u8"<entry");
        fields.last_changed_revision = first_revision_attribute(xml, u8"<commit");
        fields.parsed = fields.url.empty() == false && fields.revision.empty() == false;
        return fields;
    }

    std::vector<std::u8string> parse_svn_directory_list(const std::vector<std::u8string>& lines)
    {
        std::vector<std::u8string> directories {};
        for (const std::u8string& line : lines)
        {
            std::u8string_view value { line };
            if (value.ends_with(u8'\r'))
                value.remove_suffix(1);
            if (value.size() <= 1 || value.ends_with(u8'/') == false)
                continue;
            directories.emplace_back(value.substr(0, value.size() - 1));
        }
        return directories;
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

    bool svn_change_output_reports_conflict(const std::vector<std::u8string>& lines) noexcept
    {
        for (const std::u8string& line : lines)
        {
            if (line.size() < 6 || line[4] != u8' ')
                continue;
            for (std::size_t index = 0; index < 4; ++index)
            {
                if (line[index] == u8'C')
                    return true;
            }
        }
        return false;
    }

    std::vector<local_change_entry> collect_svn_local_changes(const svn_status_summary& status)
    {
        std::vector<local_change_entry> entries {};
        entries.reserve(status.entries.size());
        for (const svn_status_entry& entry : status.entries)
        {
            // 무시(`I`)와 외부 항목(`X`)은 변경 목록에 넣지 않는다.
            if (entry.item_state == u8'I' || entry.item_state == u8'X')
                continue;

            local_change_entry change {};
            change.path = entry.path;
            if (entry.item_state == u8'C' || entry.property_state == u8'C' || entry.tree_conflict)
                change.kind = local_change_kind::conflicted;
            else if (entry.item_state == u8'?')
                change.kind = local_change_kind::untracked;
            else if (entry.item_state == u8'A')
                change.kind = local_change_kind::added;
            else if (entry.item_state == u8'D' || entry.item_state == u8'!')
                change.kind = local_change_kind::deleted;
            else if (entry.item_state == u8'R')
                change.kind = local_change_kind::renamed;
            else if (entry.item_state == u8'M' || entry.item_state == u8'~' || entry.property_state == u8'M')
                change.kind = local_change_kind::modified;
            else
                change.kind = local_change_kind::other;
            entries.push_back(std::move(change));
        }
        return entries;
    }
} // namespace gitman
