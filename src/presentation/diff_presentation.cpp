#include "presentation/diff_presentation.h"

#include <cstddef>
#include <utility>

namespace gitman {
    diff_line_class classify_diff_line(const std::u8string_view line) noexcept
    {
        if (line.empty())
            return diff_line_class::context;

        // `+++`/`---` 파일 헤더는 추가·삭제 줄이 아니라 구조 줄이다.
        if (line.starts_with(u8"+++") || line.starts_with(u8"---"))
            return diff_line_class::heading;
        if (line.starts_with(u8"@@") || line.starts_with(u8"diff ") || line.starts_with(u8"index ") || line.starts_with(u8"Index:") || line.starts_with(u8"====")
            || line.starts_with(u8"Binary files ") || line.starts_with(u8"Cannot display:") || line.starts_with(u8"new file mode") || line.starts_with(u8"deleted file mode")
            || line.starts_with(u8"rename from") || line.starts_with(u8"rename to") || line.starts_with(u8"similarity index") || line.starts_with(u8"Property changes on:"))
            return diff_line_class::heading;

        if (line.front() == u8'+')
            return diff_line_class::added;
        if (line.front() == u8'-')
            return diff_line_class::removed;
        return diff_line_class::context;
    }

    std::vector<two_way_diff_row> build_two_way_diff(const std::vector<std::u8string>& unified_lines)
    {
        std::vector<two_way_diff_row> rows {};
        rows.reserve(unified_lines.size());
        std::vector<std::u8string> removed {};
        std::vector<std::u8string> added {};

        // 렌더러가 탭을 그리지 못하므로 고정 4칸 공백으로 펼친다. 열 정렬까지는
        // 흉내 내지 않는다.
        const auto expand_tabs = [](std::u8string line) -> std::u8string {
            std::u8string expanded {};
            expanded.reserve(line.size());
            for (const char8_t value : line)
                if (value == u8'\t')
                    expanded.append(u8"    ");
                else
                    expanded.push_back(value);
            return expanded;
        };

        const auto strip_marker = [&expand_tabs](const std::u8string& line) -> std::u8string {
            // `+`/`-`/문맥의 접두 한 문자를 떼어 낸다. 빈 문맥 줄은 그대로다.
            return expand_tabs(line.empty() ? line : line.substr(1));
        };

        const auto flush_pending = [&rows, &removed, &added]() {
            const std::size_t paired { removed.size() < added.size() ? added.size() : removed.size() };
            for (std::size_t index = 0; index < paired; ++index)
            {
                two_way_diff_row row {};
                row.changed = true;
                if (index < removed.size())
                {
                    row.left = std::move(removed[index]);
                    row.has_left = true;
                }
                if (index < added.size())
                {
                    row.right = std::move(added[index]);
                    row.has_right = true;
                }
                rows.push_back(std::move(row));
            }
            removed.clear();
            added.clear();
        };

        for (const std::u8string& line : unified_lines)
        {
            const diff_line_class kind { classify_diff_line(line) };
            if (kind == diff_line_class::removed)
            {
                removed.push_back(strip_marker(line));
                continue;
            }
            if (kind == diff_line_class::added)
            {
                added.push_back(strip_marker(line));
                continue;
            }

            flush_pending();
            if (kind == diff_line_class::heading)
            {
                two_way_diff_row row {};
                row.heading = true;
                row.left = expand_tabs(line);
                rows.push_back(std::move(row));
                continue;
            }

            two_way_diff_row row {};
            row.left = strip_marker(line);
            row.right = row.left;
            row.has_left = true;
            row.has_right = true;
            rows.push_back(std::move(row));
        }
        flush_pending();
        return rows;
    }
} // namespace gitman
