#include "platform/win32/win32_vcs_file_probe.h"

#include "platform/win32/utf8.h"

#include <windows.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace gitman::win32 {
    namespace {
        bool is_separator(const wchar_t value) noexcept
        {
            return value == L'\\' || value == L'/';
        }

        bool is_ascii_letter(const wchar_t value) noexcept
        {
            return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
        }

        bool starts_with_extended_prefix(const std::wstring_view value) noexcept
        {
            return value.size() >= 4 && value[0] == L'\\' && value[1] == L'\\' && (value[2] == L'?' || value[2] == L'.') && value[3] == L'\\';
        }

        // MAX_PATH를 넘는 경로에서도 조회가 실패하지 않도록 확장 접두어를 붙인다.
        std::wstring path_for_file_api(std::wstring path)
        {
            for (wchar_t& value : path)
                if (value == L'/')
                    value = L'\\';

            if (starts_with_extended_prefix(path))
                return path;
            if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\')
            {
                std::wstring extended { L"\\\\?\\UNC\\" };
                extended.append(std::wstring_view { path }.substr(2));
                return extended;
            }
            if (path.size() >= 3 && is_ascii_letter(path[0]) && path[1] == L':' && is_separator(path[2]))
            {
                std::wstring extended { L"\\\\?\\" };
                extended.append(path);
                return extended;
            }
            return path;
        }

        void append_program_files_directory(std::vector<std::u8string>& directories, const std::u8string_view name)
        {
            std::u8string value { read_environment_variable(name) };
            if (value.empty())
                return;
            for (const std::u8string& existing : directories)
                if (existing == value)
                    return;
            directories.push_back(std::move(value));
        }
    } // namespace

    vcs_path_kind probe_vcs_path(const std::u8string_view absolute_path) noexcept
    {
        try
        {
            if (absolute_path.empty())
                return vcs_path_kind::missing;

            const auto wide { utf8_to_utf16(absolute_path) };
            if (wide.value.has_value() == false)
                return vcs_path_kind::missing;

            const std::wstring api_path { path_for_file_api(std::move(*wide.value)) };
            const DWORD attributes { GetFileAttributesW(api_path.c_str()) };
            if (attributes == INVALID_FILE_ATTRIBUTES)
                return vcs_path_kind::missing;
            return (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0 ? vcs_path_kind::directory : vcs_path_kind::file;
        }
        catch (...)
        {
            return vcs_path_kind::missing;
        }
    }

    std::u8string read_environment_variable(const std::u8string_view name) noexcept
    {
        try
        {
            const auto wide_name { utf8_to_utf16(name) };
            if (wide_name.value.has_value() == false)
                return {};

            SetLastError(ERROR_SUCCESS);
            DWORD capacity { GetEnvironmentVariableW(wide_name.value->c_str(), nullptr, 0) };
            if (capacity == 0)
                return {};

            while (true)
            {
                std::wstring buffer(static_cast<std::size_t>(capacity), L'\0');
                const DWORD length { GetEnvironmentVariableW(wide_name.value->c_str(), buffer.data(), capacity) };
                if (length == 0)
                    return {};
                if (length < capacity)
                {
                    buffer.resize(static_cast<std::size_t>(length));
                    const auto value { utf16_to_utf8(buffer) };
                    return value.value.has_value() ? std::move(*value.value) : std::u8string {};
                }
                capacity = length;
            }
        }
        catch (...)
        {
            return {};
        }
    }

    namespace {
        // 이름 있는 REG_SZ 값 하나를 읽는다. 32비트로 빌드해도 64비트 뷰를 보도록
        // `KEY_WOW64_64KEY`를 준다 — TortoiseSVN은 64비트로 설치된다.
        std::u8string read_registry_string(const HKEY root, const wchar_t* const subkey, const wchar_t* const name) noexcept
        {
            try
            {
                HKEY key { nullptr };
                if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS)
                    return {};

                DWORD type { 0 };
                DWORD size { 0 };
                if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS || type != REG_SZ || size == 0)
                {
                    RegCloseKey(key);
                    return {};
                }

                std::wstring value(size / sizeof(wchar_t) + 1, L'\0');
                DWORD read_size { static_cast<DWORD>(value.size() * sizeof(wchar_t)) };
                const LSTATUS status { RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &read_size) };
                RegCloseKey(key);
                if (status != ERROR_SUCCESS)
                    return {};

                // 값의 크기는 종결 NUL을 포함할 수도, 안 할 수도 있다.
                value.resize(read_size / sizeof(wchar_t));
                while (value.empty() == false && value.back() == L'\0')
                    value.pop_back();

                const auto converted { utf16_to_utf8(value) };
                return converted.value.has_value() ? std::move(*converted.value) : std::u8string {};
            }
            catch (...)
            {
                return {};
            }
        }

        // `%SystemRoot%\System32`다. 환경 변수 대신 API를 쓰는 것은 `SystemRoot`를
        // 사용자가 바꿔 놓을 수 있고, WOW64에서는 `GetSystemDirectoryW`가 32비트
        // 프로세스에 맞는 자리를 알려 주기 때문이다.
        std::u8string read_system_directory() noexcept
        {
            try
            {
                const UINT length { GetSystemDirectoryW(nullptr, 0) };
                if (length == 0)
                    return {};

                std::wstring buffer(static_cast<std::size_t>(length), L'\0');
                const UINT written { GetSystemDirectoryW(buffer.data(), length) };
                if (written == 0 || written >= length)
                    return {};

                buffer.resize(static_cast<std::size_t>(written));
                const auto value { utf16_to_utf8(buffer) };
                return value.value.has_value() ? std::move(*value.value) : std::u8string {};
            }
            catch (...)
            {
                return {};
            }
        }
    } // namespace

    vcs_tool_environment current_vcs_tool_environment() noexcept
    {
        try
        {
            vcs_tool_environment environment {};
            environment.path_environment = read_environment_variable(u8"PATH");
            environment.system_directory = read_system_directory();
            // TortoiseSVN은 클라이언트 훅을 레지스트리에 둔다 (REQ-021). 설치 여부는
            // HKLM의 설치 폴더로, 훅 목록은 HKCU의 값 하나로 읽는다. 해석은
            // infrastructure의 순수 함수가 한다.
            environment.tortoise_svn_directory = read_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\TortoiseSVN", L"Directory");
            environment.tortoise_svn_hooks = read_registry_string(HKEY_CURRENT_USER, L"Software\\TortoiseSVN", L"hooks");
            append_program_files_directory(environment.program_files_directories, u8"ProgramFiles");
            append_program_files_directory(environment.program_files_directories, u8"ProgramFiles(x86)");
            append_program_files_directory(environment.program_files_directories, u8"ProgramW6432");
            return environment;
        }
        catch (...)
        {
            return {};
        }
    }

    vcs_file_content read_vcs_file_prefix(const std::u8string_view absolute_path, const std::size_t maximum_bytes) noexcept
    {
        try
        {
            vcs_file_content content {};
            content.kind = probe_vcs_path(absolute_path);
            if (content.kind != vcs_path_kind::file)
                return content;

            const auto wide { utf8_to_utf16(absolute_path) };
            if (wide.value.has_value() == false)
            {
                content.kind = vcs_path_kind::missing;
                return content;
            }

            const std::wstring api_path { path_for_file_api(std::move(*wide.value)) };
            const HANDLE file { CreateFileW(api_path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
            if (file == INVALID_HANDLE_VALUE)
            {
                content.kind = vcs_path_kind::missing;
                return content;
            }

            // 상한보다 한 byte 더 읽어 잘렸는지 판정한다.
            content.bytes.resize(maximum_bytes + 1);
            DWORD read { 0 };
            const BOOL succeeded { ReadFile(file, content.bytes.data(), static_cast<DWORD>(content.bytes.size()), &read, nullptr) };
            CloseHandle(file);
            if (succeeded == FALSE)
            {
                content.kind = vcs_path_kind::missing;
                content.bytes.clear();
                return content;
            }

            content.bytes.resize(static_cast<std::size_t>(read));
            if (content.bytes.size() > maximum_bytes)
            {
                content.bytes.resize(maximum_bytes);
                content.truncated = true;
            }
            return content;
        }
        catch (...)
        {
            return {};
        }
    }

    namespace {
        class win32_vcs_file_probe final : public vcs_file_probe
        {
        public:
            [[nodiscard]] vcs_path_kind probe(const std::u8string_view absolute_path) const noexcept override
            {
                return probe_vcs_path(absolute_path);
            }

            [[nodiscard]] vcs_file_content read_prefix(const std::u8string_view absolute_path, const std::size_t maximum_bytes) const noexcept override
            {
                return read_vcs_file_prefix(absolute_path, maximum_bytes);
            }
        };
    } // namespace

    std::unique_ptr<vcs_file_probe> make_vcs_file_probe()
    {
        return std::make_unique<win32_vcs_file_probe>();
    }
} // namespace gitman::win32
