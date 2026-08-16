#include "domain/vcs_tool.h"

#include <array>
#include <charconv>
#include <limits>

namespace gitman {
    namespace {
        void append_ascii_number(std::u8string& target, const std::uint32_t value)
        {
            std::array<char, std::numeric_limits<std::uint32_t>::digits10 + 2> buffer {};
            const auto result { std::to_chars(buffer.data(), buffer.data() + buffer.size(), value) };
            for (const char* current = buffer.data(); current != result.ptr; ++current)
                target.push_back(static_cast<char8_t>(*current));
        }
    } // namespace

    bool vcs_tool_info::usable() const noexcept
    {
        return availability == vcs_tool_availability::available && executable.empty() == false;
    }

    const vcs_tool_info& vcs_tool_set::tool(const repository_kind kind) const noexcept
    {
        return kind == repository_kind::subversion ? subversion : git;
    }

    bool vcs_tool_set::available(const repository_kind kind) const noexcept
    {
        if (kind == repository_kind::unknown)
            return any_available();
        return tool(kind).usable();
    }

    bool vcs_tool_set::any_available() const noexcept
    {
        return git.usable() || subversion.usable();
    }

    bool vcs_tool_set::none_available() const noexcept
    {
        return any_available() == false;
    }

    vcs_tool_version minimum_supported_version(const repository_kind kind) noexcept
    {
        if (kind == repository_kind::subversion)
            return { minimum_subversion_version[0], minimum_subversion_version[1], minimum_subversion_version[2] };
        return { minimum_git_version[0], minimum_git_version[1], minimum_git_version[2] };
    }

    std::u8string_view vcs_tool_availability_name(const vcs_tool_availability availability) noexcept
    {
        switch (availability)
        {
        case vcs_tool_availability::unknown:
            return u8"unknown";
        case vcs_tool_availability::not_found:
            return u8"not_found";
        case vcs_tool_availability::path_invalid:
            return u8"path_invalid";
        case vcs_tool_availability::version_unreadable:
            return u8"version_unreadable";
        case vcs_tool_availability::too_old:
            return u8"too_old";
        case vcs_tool_availability::available:
            return u8"available";
        }
        return u8"unknown";
    }

    std::u8string format_vcs_tool_version(const vcs_tool_version& version)
    {
        std::u8string result {};
        append_ascii_number(result, version.major);
        result.push_back(u8'.');
        append_ascii_number(result, version.minor);
        result.push_back(u8'.');
        append_ascii_number(result, version.patch);
        return result;
    }
} // namespace gitman
