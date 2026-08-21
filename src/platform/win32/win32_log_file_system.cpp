#include "platform/win32/win32_log_file_system.h"

#include "platform/win32/utf8.h"

#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>

namespace gitman::win32 {
    namespace {
        std::optional<std::wstring> to_wide_path(const std::u8string_view path)
        {
            if (path.empty())
                return std::nullopt;

            auto converted { utf8_to_utf16(path) };
            if (converted.value.has_value() == false)
                return std::nullopt;

            std::wstring wide { std::move(*converted.value) };
            for (wchar_t& value : wide)
                if (value == L'/')
                    value = L'\\';
            return wide;
        }

        bool directory_exists(const std::wstring& path) noexcept
        {
            const DWORD attributes { GetFileAttributesW(path.c_str()) };
            return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        }

        // 중간 폴더까지 차례로 만든다. 뿌리(`C:\`, `\\server\share`)는 건너뛴다.
        bool create_directory_chain(const std::wstring& path)
        {
            std::size_t index { 0 };
            if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
            {
                // `\\server\share`까지는 만들 수 없다.
                index = 2;
                for (int component = 0; component < 2 && index < path.size(); ++component)
                {
                    while (index < path.size() && path[index] != L'\\')
                        ++index;
                    if (index < path.size())
                        ++index;
                }
            }
            else if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\')
                index = 3;

            while (index <= path.size())
            {
                const std::size_t separator { path.find(L'\\', index) };
                const std::wstring current { path.substr(0, separator == std::wstring::npos ? path.size() : separator) };
                if (current.empty() == false && directory_exists(current) == false)
                {
                    if (CreateDirectoryW(current.c_str(), nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
                        return false;
                }
                if (separator == std::wstring::npos)
                    return true;
                index = separator + 1;
            }
            return true;
        }
    } // namespace

    bool log_file_system::create_directories(const std::u8string_view path) noexcept
    {
        try
        {
            const std::optional<std::wstring> wide { to_wide_path(path) };
            if (wide.has_value() == false)
                return false;
            return create_directory_chain(*wide);
        }
        catch (...)
        {
            return false;
        }
    }

    bool log_file_system::file_exists(const std::u8string_view path) noexcept
    {
        try
        {
            const std::optional<std::wstring> wide { to_wide_path(path) };
            if (wide.has_value() == false)
                return false;
            return GetFileAttributesW(wide->c_str()) != INVALID_FILE_ATTRIBUTES;
        }
        catch (...)
        {
            return false;
        }
    }

    bool log_file_system::append_file(const std::u8string_view path, const std::u8string_view bytes) noexcept
    {
        try
        {
            const std::optional<std::wstring> wide { to_wide_path(path) };
            if (wide.has_value() == false)
                return false;

            // FILE_APPEND_DATA는 다른 프로세스가 같은 파일을 열어도 끝에 이어 쓴다.
            const HANDLE file {
                CreateFileW(wide->c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr),
            };

            if (file == INVALID_HANDLE_VALUE)
                return false;

            bool succeeded { true };
            std::size_t offset { 0 };
            while (offset < bytes.size())
            {
                const std::size_t remaining { bytes.size() - offset };
                const DWORD requested { static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max())) };
                DWORD written { 0 };
                if (WriteFile(file, bytes.data() + offset, requested, &written, nullptr) == FALSE || written == 0)
                {
                    succeeded = false;
                    break;
                }
                offset += static_cast<std::size_t>(written);
            }
            CloseHandle(file);
            return succeeded;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace gitman::win32
