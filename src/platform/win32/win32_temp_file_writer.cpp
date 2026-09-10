#include "platform/win32/win32_temp_file_writer.h"

#include "platform/win32/utf8.h"

#include <windows.h>

#include <atomic>
#include <string>

namespace gitman::win32 {
    namespace {
        // `%TEMP%\gitman-hooks`다. 훅 인자 파일만 들어가며, 남아도 다음 실행에 지장이
        // 없도록 이름이 겹치지 않게 만든다.
        [[nodiscard]] std::wstring hook_temp_directory()
        {
            std::wstring buffer(MAX_PATH + 1, L'\0');
            const DWORD length { GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data()) };
            if (length == 0 || length > buffer.size())
                return {};
            buffer.resize(static_cast<std::size_t>(length));
            if (buffer.empty() == false && buffer.back() != L'\\')
                buffer.push_back(L'\\');
            buffer.append(L"gitman-hooks");
            if (CreateDirectoryW(buffer.c_str(), nullptr) == FALSE && GetLastError() != ERROR_ALREADY_EXISTS)
                return {};
            return buffer;
        }

        class win32_temp_file_writer final : public temp_file_writer
        {
        public:
            [[nodiscard]] std::u8string write(const std::u8string_view name_hint, const std::u8string_view content) noexcept override
            {
                try
                {
                    const std::wstring directory { hook_temp_directory() };
                    if (directory.empty())
                        return {};

                    const auto hint { utf8_to_utf16(name_hint) };
                    std::wstring path { directory };
                    path.push_back(L'\\');
                    path.append(std::to_wstring(GetCurrentProcessId()));
                    path.push_back(L'-');
                    path.append(std::to_wstring(counter_.fetch_add(1) + 1));
                    path.push_back(L'-');
                    path.append(hint.value.has_value() ? *hint.value : std::wstring { L"file" });
                    path.append(L".txt");

                    const HANDLE file { CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr) };
                    if (file == INVALID_HANDLE_VALUE)
                        return {};

                    // 내용은 UTF-8 그대로다. TortoiseSVN의 인자 파일 규약이 UTF-8이며,
                    // BOM은 붙이지 않는다 — 스크립트가 첫 줄을 경로로 읽는다.
                    bool written { true };
                    if (content.empty() == false)
                    {
                        DWORD count { 0 };
                        written = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &count, nullptr) != FALSE && count == content.size();
                    }
                    CloseHandle(file);
                    if (written == false)
                    {
                        DeleteFileW(path.c_str());
                        return {};
                    }

                    const auto result { utf16_to_utf8(path) };
                    return result.value.has_value() ? std::move(*result.value) : std::u8string {};
                }
                catch (...)
                {
                    return {};
                }
            }

            void remove(const std::u8string_view path) noexcept override
            {
                try
                {
                    const auto wide { utf8_to_utf16(path) };
                    if (wide.value.has_value())
                        static_cast<void>(DeleteFileW(wide.value->c_str()));
                }
                catch (...)
                {}
            }

        private:
            // worker 여럿이 동시에 훅을 돌릴 수 있다. 이름이 겹치면 서로의 인자를
            // 덮어쓰므로 번호를 원자적으로 늘린다.
            std::atomic<unsigned long long> counter_ { 0 };
        };
    } // namespace

    std::unique_ptr<temp_file_writer> make_temp_file_writer()
    {
        return std::make_unique<win32_temp_file_writer>();
    }
} // namespace gitman::win32
