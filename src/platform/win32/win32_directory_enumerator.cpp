#include "platform/win32/win32_directory_enumerator.h"

#include "domain/path_syntax.h"
#include "platform/win32/utf8.h"

#include <windows.h>

#include <cstdint>
#include <string>
#include <utility>

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

        // MAX_PATH를 넘는 경로에서도 열거가 실패하지 않도록 확장 접두어를 붙인다.
        // `win32_vcs_file_probe`의 규칙과 같으며, 그쪽 helper는 파일 전용이라 공유하지
        // 않고 같은 규칙을 유지한다.
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

        std::wstring search_pattern(std::wstring directory)
        {
            std::wstring pattern { path_for_file_api(std::move(directory)) };
            if (pattern.empty() == false && pattern.back() != L'\\')
                pattern.push_back(L'\\');
            pattern.push_back(L'*');
            return pattern;
        }

        // 열거 중 예외가 나도 handle이 새지 않도록 RAII로 감싼다.
        class find_handle
        {
        public:
            explicit find_handle(const HANDLE value) noexcept
                : value_ { value }
            {}

            find_handle(const find_handle&) = delete;
            find_handle(find_handle&&) = delete;
            find_handle& operator=(const find_handle&) = delete;
            find_handle& operator=(find_handle&&) = delete;

            ~find_handle()
            {
                if (value_ != INVALID_HANDLE_VALUE)
                    FindClose(value_);
            }

            [[nodiscard]] HANDLE get() const noexcept
            {
                return value_;
            }

        private:
            HANDLE value_ {};
        };
    } // namespace

    directory_listing enumerate_directory(const std::u8string_view absolute_directory) noexcept
    {
        try
        {
            directory_listing listing {};

            // 계약 위반은 OS까지 가지 않고 여기서 끝낸다. Win32 오류 번호가 없는
            // 실패는 호출 형식의 문제라는 뜻이다.
            if (is_absolute_windows_path(absolute_directory) == false)
                return listing;

            const auto wide { utf8_to_utf16(absolute_directory) };
            if (wide.value.has_value() == false)
                return listing;

            const std::wstring pattern { search_pattern(std::move(*wide.value)) };
            WIN32_FIND_DATAW data {};
            const find_handle find { FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH) };
            if (find.get() == INVALID_HANDLE_VALUE)
            {
                const DWORD error { GetLastError() };

                // 디렉터리는 있지만 일치 항목이 없는 경우다. 열거 실패가 아니라 빈
                // 목록이다.
                if (error == ERROR_FILE_NOT_FOUND)
                {
                    listing.succeeded = true;
                    return listing;
                }
                listing.native_error = { static_cast<std::uint32_t>(error) };
                return listing;
            }

            do
            {
                const std::wstring_view name { data.cFileName };
                if (name == L"." || name == L"..")
                    continue;

                auto utf8_name { utf16_to_utf8(name) };
                if (utf8_name.value.has_value() == false)
                {
                    ++listing.unreadable_name_count;
                    continue;
                }

                directory_entry entry {};
                entry.name = std::move(*utf8_name.value);
                entry.is_directory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                entry.is_reparse_point = (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                listing.entries.push_back(std::move(entry));
            } while (FindNextFileW(find.get(), &data) != 0);

            const DWORD last { GetLastError() };
            if (last != ERROR_NO_MORE_FILES)
            {
                // 중간에 끊긴 목록을 성공으로 보고하면 누락된 자식이 조용히 사라진다.
                listing.native_error = { static_cast<std::uint32_t>(last) };
                return listing;
            }

            listing.succeeded = true;
            return listing;
        }
        catch (...)
        {
            return {};
        }
    }

    namespace {
        class win32_directory_enumerator final : public directory_enumerator
        {
        public:
            [[nodiscard]] directory_listing enumerate(const std::u8string_view absolute_directory) const noexcept override
            {
                return enumerate_directory(absolute_directory);
            }
        };
    } // namespace

    std::unique_ptr<directory_enumerator> make_directory_enumerator()
    {
        return std::make_unique<win32_directory_enumerator>();
    }
} // namespace gitman::win32
