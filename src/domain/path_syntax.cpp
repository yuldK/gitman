#include "domain/path_syntax.h"

#include <cstddef>
#include <vector>

namespace gitman {
    namespace {
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

        // drive(`C:`)나 UNC share(`\\server\share`)까지의 길이다. 이 앞부분이 서로 다르면
        // 상대 경로로 이을 수 없다.
        std::size_t root_length(const std::u8string_view path) noexcept
        {
            if (path.size() >= 2 && is_path_separator(path[0]) && is_path_separator(path[1]))
            {
                // `\\server`와 `\share`를 차례로 지난다.
                std::size_t index { 2 };
                for (int component = 0; component < 2 && index < path.size(); ++component)
                {
                    while (index < path.size() && is_path_separator(path[index]) == false)
                        ++index;
                    if (component == 0)
                        while (index < path.size() && is_path_separator(path[index]))
                            ++index;
                }
                return index;
            }
            if (path.size() >= 2 && is_ascii_letter(path[0]) && path[1] == u8':')
                return 2;
            return 0;
        }

        bool roots_equal(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                const char8_t left_value { is_path_separator(left[index]) ? u8'/' : ascii_lower(left[index]) };
                const char8_t right_value { is_path_separator(right[index]) ? u8'/' : ascii_lower(right[index]) };
                if (left_value != right_value)
                    return false;
            }
            return true;
        }

        std::vector<std::u8string_view> split_components(const std::u8string_view path)
        {
            std::vector<std::u8string_view> components {};
            std::size_t index { 0 };
            while (index < path.size())
            {
                while (index < path.size() && is_path_separator(path[index]))
                    ++index;
                const std::size_t begin { index };
                while (index < path.size() && is_path_separator(path[index]) == false)
                    ++index;
                if (index == begin)
                    continue;

                const std::u8string_view component { path.substr(begin, index - begin) };
                // `.`은 위치를 바꾸지 않는다. `..`은 어휘적으로만 접어 올린다.
                if (component == u8".")
                    continue;
                if (component == u8".." && components.empty() == false && components.back() != u8"..")
                {
                    components.pop_back();
                    continue;
                }
                components.push_back(component);
            }
            return components;
        }

        bool components_equal(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
                if (ascii_lower(left[index]) != ascii_lower(right[index]))
                    return false;
            return true;
        }
    } // namespace

    bool is_absolute_windows_path(const std::u8string_view path) noexcept
    {
        // UNC(`\\server\share`)와 device(`\\?\C:\`) 경로는 separator 두 개 뒤에 이름이 있어야 한다.
        if (path.size() >= 2 && is_path_separator(path[0]) && is_path_separator(path[1]))
            return path.size() > 2 && is_path_separator(path[2]) == false;
        // `C:relative`는 drive 기준 상대 경로이고 `\absolute`는 현재 drive에 의존한다.
        if (path.size() < 3)
            return false;
        return is_ascii_letter(path[0]) && path[1] == u8':' && is_path_separator(path[2]);
    }

    std::u8string_view windows_parent_directory(const std::u8string_view path) noexcept
    {
        std::size_t end { path.size() };
        // 끝의 구분자는 이름의 일부가 아니다. 이름을 지운 뒤 남는 구분자도 없앤다.
        while (end > 0 && is_path_separator(path[end - 1]))
            --end;
        while (end > 0 && is_path_separator(path[end - 1]) == false)
            --end;
        while (end > 0 && is_path_separator(path[end - 1]))
            --end;
        if (end == 0)
            return {};
        return path.substr(0, end);
    }

    std::u8string relative_windows_path(const std::u8string_view path, const std::u8string_view base_directory)
    {
        if (is_absolute_windows_path(path) == false || is_absolute_windows_path(base_directory) == false)
            return std::u8string { path };

        const std::size_t path_root { root_length(path) };
        const std::size_t base_root { root_length(base_directory) };
        if (roots_equal(path.substr(0, path_root), base_directory.substr(0, base_root)) == false)
            return std::u8string { path };

        const std::vector<std::u8string_view> path_components { split_components(path.substr(path_root)) };
        const std::vector<std::u8string_view> base_components { split_components(base_directory.substr(base_root)) };

        std::size_t common { 0 };
        while (common < path_components.size() && common < base_components.size() && components_equal(path_components[common], base_components[common]))
            ++common;

        std::u8string result {};
        for (std::size_t index = common; index < base_components.size(); ++index)
        {
            if (result.empty() == false)
                result.push_back(u8'\\');
            result.append(u8"..");
        }
        for (std::size_t index = common; index < path_components.size(); ++index)
        {
            if (result.empty() == false)
                result.push_back(u8'\\');
            result.append(path_components[index]);
        }
        if (result.empty())
            return std::u8string { u8"." };
        return result;
    }

    std::u8string to_display_path(const std::u8string_view path)
    {
        std::u8string result { path };
        for (char8_t& value : result)
            if (value == u8'\\')
                value = u8'/';
        return result;
    }
} // namespace gitman
