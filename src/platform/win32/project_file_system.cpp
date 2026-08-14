#include "platform/win32/project_file_system.h"

#include "platform/win32/utf8.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gitman::win32 {
    namespace {
        enum class wide_path_kind
        {
            relative,
            drive_absolute,
            unc_absolute,
            invalid,
        };

        struct wide_path_result
        {
            std::optional<std::wstring> value {};
            std::uint32_t native_error { ERROR_INVALID_NAME };
        };

        bool is_separator(const wchar_t value) noexcept
        {
            return value == L'\\' || value == L'/';
        }

        bool is_ascii_letter(const wchar_t value) noexcept
        {
            return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
        }

        wchar_t ascii_uppercase(const wchar_t value) noexcept
        {
            if (value >= L'a' && value <= L'z')
                return static_cast<wchar_t>(value - L'a' + L'A');
            return value;
        }

        bool starts_with_ascii_case_insensitive(const std::wstring_view value, const std::wstring_view prefix) noexcept
        {
            if (value.size() < prefix.size())
                return false;
            for (std::size_t index = 0; index < prefix.size(); ++index)
                if (ascii_uppercase(value[index]) != ascii_uppercase(prefix[index]))
                    return false;
            return true;
        }

        bool remove_supported_extended_prefix(std::wstring& path)
        {
            if (starts_with_ascii_case_insensitive(path, L"\\\\?\\UNC\\"))
            {
                path.erase(0, 8);
                path.insert(0, L"\\\\");
                return true;
            }
            if (starts_with_ascii_case_insensitive(path, L"\\\\?\\"))
            {
                if (path.size() < 7 || is_ascii_letter(path[4]) == false || path[5] != L':' || path[6] != L'\\')
                    return false;
                path.erase(0, 4);
                return true;
            }
            return starts_with_ascii_case_insensitive(path, L"\\\\.\\") == false;
        }

        void normalize_separators(std::wstring& path)
        {
            for (wchar_t& value : path)
                if (value == L'/')
                    value = L'\\';

            std::wstring collapsed {};
            collapsed.reserve(path.size());
            std::size_t index { 0 };
            if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
            {
                collapsed.append(L"\\\\");
                index = 2;
                while (index < path.size() && path[index] == L'\\')
                    ++index;
            }

            for (; index < path.size(); ++index)
            {
                const wchar_t value { path[index] };
                if (value == L'\\' && collapsed.empty() == false && collapsed.back() == L'\\')
                    continue;
                collapsed.push_back(value);
            }
            path = std::move(collapsed);
        }

        wide_path_kind classify_path(const std::wstring_view path) noexcept
        {
            if (path.size() >= 3 && is_ascii_letter(path[0]) && path[1] == L':' && is_separator(path[2]))
                return wide_path_kind::drive_absolute;
            if (path.size() >= 2 && is_separator(path[0]) && is_separator(path[1]))
                return wide_path_kind::unc_absolute;
            if (path.empty() == false && is_separator(path[0]))
                return wide_path_kind::invalid;
            if (path.size() >= 2 && is_ascii_letter(path[0]) && path[1] == L':')
                return wide_path_kind::invalid;
            return path.empty() ? wide_path_kind::invalid : wide_path_kind::relative;
        }

        bool has_invalid_characters(const std::wstring_view path, const wide_path_kind kind) noexcept
        {
            for (std::size_t index = 0; index < path.size(); ++index)
            {
                const wchar_t value { path[index] };
                if (value < L' ' || value == L'"' || value == L'<' || value == L'>' || value == L'|' || value == L'?' || value == L'*')
                    return true;
                if (value == L':' && (kind != wide_path_kind::drive_absolute || index != 1))
                    return true;
            }
            return false;
        }

        bool has_valid_unc_root(const std::wstring_view path) noexcept
        {
            const std::size_t server_end { path.find(L'\\', 2) };
            if (server_end == std::wstring_view::npos || server_end == 2)
                return false;
            const std::size_t share_begin { server_end + 1 };
            if (share_begin >= path.size())
                return false;
            const std::size_t share_end { path.find(L'\\', share_begin) };
            return share_end == std::wstring_view::npos ? share_begin < path.size() : share_end > share_begin;
        }

        bool validate_path_syntax(const std::wstring_view path, const wide_path_kind kind) noexcept
        {
            if (kind == wide_path_kind::invalid || has_invalid_characters(path, kind))
                return false;
            if (kind == wide_path_kind::unc_absolute)
                return has_valid_unc_root(path);
            return true;
        }

        std::uint32_t last_error_or(const std::uint32_t fallback) noexcept
        {
            const DWORD error { GetLastError() };
            return error == ERROR_SUCCESS ? fallback : static_cast<std::uint32_t>(error);
        }

        wide_path_result full_path(const std::wstring& source)
        {
            SetLastError(ERROR_SUCCESS);
            DWORD capacity { GetFullPathNameW(source.c_str(), 0, nullptr, nullptr) };
            if (capacity == 0)
                return { std::nullopt, last_error_or(ERROR_INVALID_NAME) };

            while (true)
            {
                std::wstring output(static_cast<std::size_t>(capacity), L'\0');
                SetLastError(ERROR_SUCCESS);
                const DWORD length { GetFullPathNameW(source.c_str(), capacity, output.data(), nullptr) };
                if (length == 0)
                    return { std::nullopt, last_error_or(ERROR_INVALID_NAME) };
                if (length < capacity)
                {
                    output.resize(static_cast<std::size_t>(length));
                    normalize_separators(output);
                    return { std::move(output), ERROR_SUCCESS };
                }
                capacity = length;
            }
        }

        void remove_trailing_separators(std::wstring& path) noexcept
        {
            while (path.size() > 1 && path.back() == L'\\')
            {
                if (path.size() == 3 && is_ascii_letter(path[0]) && path[1] == L':')
                    return;
                path.pop_back();
            }
        }

        wide_path_result prepare_path(std::wstring path, const bool allow_relative)
        {
            if (path.empty())
                return { std::nullopt, ERROR_INVALID_NAME };
            normalize_separators(path);
            if (remove_supported_extended_prefix(path) == false)
                return { std::nullopt, ERROR_INVALID_NAME };
            normalize_separators(path);
            const wide_path_kind kind { classify_path(path) };
            if ((allow_relative == false && kind == wide_path_kind::relative) || validate_path_syntax(path, kind) == false)
                return { std::nullopt, ERROR_INVALID_NAME };
            return { std::move(path), ERROR_SUCCESS };
        }

        wide_path_result document_directory(const std::u8string_view document_path)
        {
            const auto wide_document { utf8_to_utf16(document_path) };
            if (wide_document.value.has_value() == false)
                return { std::nullopt, static_cast<std::uint32_t>(wide_document.error->native_error) };

            wide_path_result prepared { prepare_path(std::move(*wide_document.value), true) };
            if (prepared.value.has_value() == false)
                return prepared;

            wide_path_result absolute { full_path(*prepared.value) };
            if (absolute.value.has_value() == false)
                return absolute;

            std::wstring& value { *absolute.value };
            remove_trailing_separators(value);
            const std::size_t separator { value.find_last_of(L'\\') };
            if (separator == std::wstring::npos)
                return { std::nullopt, ERROR_BAD_PATHNAME };
            if (separator == 2 && value.size() >= 3 && is_ascii_letter(value[0]) && value[1] == L':')
                value.resize(3);
            else
                value.resize(separator);

            const wide_path_kind kind { classify_path(value) };
            if ((kind != wide_path_kind::drive_absolute && kind != wide_path_kind::unc_absolute) || validate_path_syntax(value, kind) == false)
                return { std::nullopt, ERROR_BAD_PATHNAME };
            return absolute;
        }

        std::wstring path_for_file_api(const std::wstring_view normalized)
        {
            if (classify_path(normalized) == wide_path_kind::unc_absolute)
            {
                std::wstring extended { L"\\\\?\\UNC\\" };
                extended.append(normalized.substr(2));
                return extended;
            }
            std::wstring extended { L"\\\\?\\" };
            extended.append(normalized);
            return extended;
        }

        configured_path_state state_from_error(const std::uint32_t error) noexcept
        {
            switch (error)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_DRIVE:
            case ERROR_BAD_NETPATH:
            case ERROR_BAD_NET_NAME:
            case ERROR_NOT_READY:
            case ERROR_DEV_NOT_EXIST:
                return configured_path_state::missing;
            case ERROR_INVALID_NAME:
            case ERROR_BAD_PATHNAME:
            case ERROR_FILENAME_EXCED_RANGE:
                return configured_path_state::invalid;
            default:
                return configured_path_state::inaccessible;
            }
        }

        project_path_resolution invalid_resolution(const std::uint32_t native_error) noexcept
        {
            return {
                {},
                configured_path_state::invalid,
                native_error,
            };
        }

        project_path_resolution resolve_project_path_impl(const std::u8string_view original_path, const std::u8string_view document_path)
        {
            const auto wide_original { utf8_to_utf16(original_path) };
            if (wide_original.value.has_value() == false)
                return invalid_resolution(static_cast<std::uint32_t>(wide_original.error->native_error));

            wide_path_result prepared { prepare_path(std::move(*wide_original.value), true) };
            if (prepared.value.has_value() == false)
                return invalid_resolution(prepared.native_error);

            const wide_path_kind original_kind { classify_path(*prepared.value) };
            std::wstring absolute_source {};
            if (original_kind == wide_path_kind::relative)
            {
                wide_path_result base { document_directory(document_path) };
                if (base.value.has_value() == false)
                    return invalid_resolution(base.native_error);
                absolute_source = std::move(*base.value);
                if (absolute_source.empty() == false && absolute_source.back() != L'\\')
                    absolute_source.push_back(L'\\');
                absolute_source.append(*prepared.value);
            }
            else
                absolute_source = std::move(*prepared.value);

            wide_path_result absolute { full_path(absolute_source) };
            if (absolute.value.has_value() == false)
                return invalid_resolution(absolute.native_error);
            remove_trailing_separators(*absolute.value);

            const wide_path_kind absolute_kind { classify_path(*absolute.value) };
            if ((absolute_kind != wide_path_kind::drive_absolute && absolute_kind != wide_path_kind::unc_absolute) || validate_path_syntax(*absolute.value, absolute_kind) == false)
                return invalid_resolution(ERROR_INVALID_NAME);

            const auto normalized_utf8 { utf16_to_utf8(*absolute.value) };
            if (normalized_utf8.value.has_value() == false)
                return invalid_resolution(static_cast<std::uint32_t>(normalized_utf8.error->native_error));

            const std::wstring api_path { path_for_file_api(*absolute.value) };
            SetLastError(ERROR_SUCCESS);
            const DWORD attributes { GetFileAttributesW(api_path.c_str()) };
            if (attributes == INVALID_FILE_ATTRIBUTES)
            {
                const std::uint32_t error { last_error_or(ERROR_PATH_NOT_FOUND) };
                return {
                    std::move(*normalized_utf8.value),
                    state_from_error(error),
                    error,
                };
            }

            return {
                std::move(*normalized_utf8.value),
                (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? configured_path_state::available : configured_path_state::not_directory,
                std::nullopt,
            };
        }
    } // namespace

    project_path_resolution resolve_project_path(const std::u8string_view original_path, const std::u8string_view document_path) noexcept
    {
        try
        {
            return resolve_project_path_impl(original_path, document_path);
        }
        catch (...)
        {
            return invalid_resolution(ERROR_NOT_ENOUGH_MEMORY);
        }
    }

    bool normalized_project_paths_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        try
        {
            const auto wide_left { utf8_to_utf16(left) };
            const auto wide_right { utf8_to_utf16(right) };
            if (wide_left.value.has_value() == false || wide_right.value.has_value() == false)
                return false;
            if (wide_left.value->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || wide_right.value->size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
                return false;
            return CompareStringOrdinal(wide_left.value->data(), static_cast<int>(wide_left.value->size()), wide_right.value->data(), static_cast<int>(wide_right.value->size()), TRUE) == CSTR_EQUAL;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace gitman::win32
