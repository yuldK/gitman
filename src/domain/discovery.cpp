#include "domain/discovery.h"

#include <compare>
#include <cstddef>

namespace gitman {
    namespace {
        char8_t ascii_lowercase(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        std::strong_ordering compare_ascii_case_insensitive(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            const std::size_t common { left.size() < right.size() ? left.size() : right.size() };
            for (std::size_t index = 0; index < common; ++index)
            {
                const char8_t left_value { ascii_lowercase(left[index]) };
                const char8_t right_value { ascii_lowercase(right[index]) };
                if (left_value != right_value)
                    return left_value <=> right_value;
            }
            return left.size() <=> right.size();
        }
    } // namespace

    discovery_classification classify_discovery_markers(const repository_marker_set& markers) noexcept
    {
        if (markers.probe_failed)
            return { repository_kind::unknown, false, discovery_exclusion::inaccessible };

        const bool has_git_marker { markers.has_git_directory || markers.has_git_file };
        if (has_git_marker && markers.has_svn_directory)
            return { repository_kind::unknown, false, discovery_exclusion::conflicting_metadata };
        if (markers.has_git_directory)
            return { repository_kind::git, false, discovery_exclusion::none };
        if (markers.has_git_file)
            return { repository_kind::git, true, discovery_exclusion::none };
        if (markers.has_svn_directory)
            return { repository_kind::subversion, false, discovery_exclusion::none };

        // bare 저장소는 kind를 git으로 남긴다. 등록은 제외하지만 목록에는 "Git bare
        // 저장소라서 제외됐다"는 정보가 필요하다.
        if (markers.has_head_file && markers.has_objects_directory && markers.has_refs_directory)
            return { repository_kind::git, false, discovery_exclusion::bare_repository };
        return { repository_kind::unknown, false, discovery_exclusion::not_a_repository };
    }

    bool discovery_candidate::selectable() const noexcept
    {
        return exclusion == discovery_exclusion::none;
    }

    bool discovery_candidate_before(const discovery_candidate& left, const discovery_candidate& right) noexcept
    {
        const std::strong_ordering order { compare_ascii_case_insensitive(left.directory_name, right.directory_name) };
        if (order != std::strong_ordering::equal)
            return order == std::strong_ordering::less;
        if (left.directory_name != right.directory_name)
            return left.directory_name < right.directory_name;
        return left.absolute_path < right.absolute_path;
    }

    std::u8string_view discovery_exclusion_name(const discovery_exclusion value) noexcept
    {
        switch (value)
        {
        case discovery_exclusion::none:
            return u8"none";
        case discovery_exclusion::not_a_repository:
            return u8"not_a_repository";
        case discovery_exclusion::bare_repository:
            return u8"bare_repository";
        case discovery_exclusion::conflicting_metadata:
            return u8"conflicting_metadata";
        case discovery_exclusion::reparse_point:
            return u8"reparse_point";
        case discovery_exclusion::already_registered:
            return u8"already_registered";
        case discovery_exclusion::inaccessible:
            return u8"inaccessible";
        }
        return u8"unknown";
    }
} // namespace gitman
