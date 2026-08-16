#include "platform/win32/workspace_document_file_system.h"

#include "platform/win32/utf8.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace gitman::win32 {
    namespace {
        class unique_handle
        {
        public:
            unique_handle() noexcept = default;

            explicit unique_handle(const HANDLE value) noexcept
                : value_ { value }
            {}

            unique_handle(const unique_handle&) = delete;
            unique_handle& operator=(const unique_handle&) = delete;

            unique_handle(unique_handle&& other) noexcept
                : value_ { other.release() }
            {}

            unique_handle& operator=(unique_handle&& other) noexcept
            {
                if (this != &other)
                {
                    reset();
                    value_ = other.release();
                }
                return *this;
            }

            ~unique_handle()
            {
                reset();
            }

            [[nodiscard]] HANDLE get() const noexcept
            {
                return value_;
            }

            [[nodiscard]] bool valid() const noexcept
            {
                return value_ != INVALID_HANDLE_VALUE && value_ != nullptr;
            }

            [[nodiscard]] HANDLE release() noexcept
            {
                const HANDLE value { value_ };
                value_ = INVALID_HANDLE_VALUE;
                return value;
            }

            void reset(const HANDLE value = INVALID_HANDLE_VALUE) noexcept
            {
                if (valid())
                    CloseHandle(value_);
                value_ = value;
            }

        private:
            HANDLE value_ { INVALID_HANDLE_VALUE };
        };

        struct prepared_path
        {
            std::optional<std::wstring> value {};
            std::uint32_t native_error { ERROR_INVALID_NAME };
        };

        struct temporary_file
        {
            unique_handle handle {};
            std::wstring path {};
            std::uint32_t native_error { ERROR_CANNOT_MAKE };
        };

        std::atomic_uint64_t temporary_file_sequence { 0 };

        std::uint32_t last_error_or(const std::uint32_t fallback) noexcept
        {
            const DWORD error { GetLastError() };
            return error == ERROR_SUCCESS ? fallback : static_cast<std::uint32_t>(error);
        }

        bool starts_with(const std::wstring_view value, const std::wstring_view prefix) noexcept
        {
            return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
        }

        bool is_ascii_drive_path(const std::wstring_view value) noexcept
        {
            if (value.size() < 3 || value[1] != L':' || value[2] != L'\\')
                return false;
            return (value[0] >= L'A' && value[0] <= L'Z') || (value[0] >= L'a' && value[0] <= L'z');
        }

        prepared_path full_path(std::wstring source)
        {
            for (wchar_t& value : source)
                if (value == L'/')
                    value = L'\\';

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
                    return { std::move(output), ERROR_SUCCESS };
                }
                capacity = length;
            }
        }

        prepared_path prepare_file_path(const std::u8string_view path)
        {
            const auto converted { utf8_to_utf16(path) };
            if (converted.value.has_value() == false)
                return { std::nullopt, static_cast<std::uint32_t>(converted.error->native_error) };
            if (converted.value->empty() || converted.value->find(L'\0') != std::wstring::npos)
                return { std::nullopt, ERROR_INVALID_NAME };

            prepared_path absolute { full_path(std::move(*converted.value)) };
            if (absolute.value.has_value() == false)
                return absolute;

            std::wstring& value { *absolute.value };
            if (starts_with(value, L"\\\\.\\"))
                return { std::nullopt, ERROR_INVALID_NAME };
            if (starts_with(value, L"\\\\?\\UNC\\"))
                return absolute;
            if (starts_with(value, L"\\\\?\\"))
                return is_ascii_drive_path(std::wstring_view { value }.substr(4)) ? std::move(absolute) : prepared_path { std::nullopt, ERROR_INVALID_NAME };
            if (starts_with(value, L"\\\\"))
            {
                value.erase(0, 2);
                value.insert(0, L"\\\\?\\UNC\\");
                return absolute;
            }
            if (is_ascii_drive_path(value))
            {
                value.insert(0, L"\\\\?\\");
                return absolute;
            }
            return { std::nullopt, ERROR_INVALID_NAME };
        }

        bool is_not_found_error(const std::uint32_t error) noexcept
        {
            switch (error)
            {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
            case ERROR_INVALID_DRIVE:
            case ERROR_BAD_NETPATH:
            case ERROR_BAD_NET_NAME:
                return true;
            default:
                return false;
            }
        }

        workspace_file_read_result read_impl(const std::u8string_view path)
        {
            prepared_path prepared { prepare_file_path(path) };
            if (prepared.value.has_value() == false)
                return { workspace_file_read_state::failed, {}, prepared.native_error };

            SetLastError(ERROR_SUCCESS);
            unique_handle file {
                CreateFileW(prepared.value->c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr),
            };
            if (file.valid() == false)
            {
                const std::uint32_t error { last_error_or(ERROR_READ_FAULT) };
                return {
                    is_not_found_error(error) ? workspace_file_read_state::not_found : workspace_file_read_state::failed,
                    {},
                    error,
                };
            }

            LARGE_INTEGER size {};
            SetLastError(ERROR_SUCCESS);
            if (GetFileSizeEx(file.get(), &size) == FALSE)
                return { workspace_file_read_state::failed, {}, last_error_or(ERROR_READ_FAULT) };
            if (size.QuadPart < 0 || static_cast<unsigned long long>(size.QuadPart) > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max()))
                return { workspace_file_read_state::failed, {}, ERROR_FILE_TOO_LARGE };

            std::u8string bytes(static_cast<std::size_t>(size.QuadPart), u8'\0');
            std::size_t offset { 0 };
            while (offset < bytes.size())
            {
                const std::size_t remaining { bytes.size() - offset };
                const DWORD requested { static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max())) };
                DWORD read_count { 0 };
                SetLastError(ERROR_SUCCESS);
                if (ReadFile(file.get(), bytes.data() + offset, requested, &read_count, nullptr) == FALSE)
                    return { workspace_file_read_state::failed, {}, last_error_or(ERROR_READ_FAULT) };
                if (read_count == 0)
                    return { workspace_file_read_state::failed, {}, ERROR_HANDLE_EOF };
                offset += static_cast<std::size_t>(read_count);
            }
            return { workspace_file_read_state::available, std::move(bytes), std::nullopt };
        }

        std::wstring temporary_file_name(const std::wstring_view document_path, const std::uint64_t sequence)
        {
            const std::size_t separator { document_path.find_last_of(L'\\') };
            std::wstring value { document_path.substr(0, separator + 1) };
            value.append(L".gitman-");
            value.append(std::to_wstring(GetCurrentProcessId()));
            value.push_back(L'-');
            value.append(std::to_wstring(sequence));
            value.append(L".tmp");
            return value;
        }

        temporary_file create_temporary_file(const std::wstring_view document_path)
        {
            for (std::size_t attempt = 0; attempt < 100; ++attempt)
            {
                const std::uint64_t sequence { temporary_file_sequence.fetch_add(1, std::memory_order_relaxed) };
                std::wstring path { temporary_file_name(document_path, sequence) };
                SetLastError(ERROR_SUCCESS);
                unique_handle handle { CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr) };
                if (handle.valid())
                    return { std::move(handle), std::move(path), ERROR_SUCCESS };

                const std::uint32_t error { last_error_or(ERROR_CANNOT_MAKE) };
                if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
                    return { {}, {}, error };
            }
            return { {}, {}, ERROR_FILE_EXISTS };
        }

        workspace_file_commit_result write_bytes(const HANDLE file, const std::u8string_view bytes)
        {
            std::size_t offset { 0 };
            while (offset < bytes.size())
            {
                const std::size_t remaining { bytes.size() - offset };
                const DWORD requested { static_cast<DWORD>(std::min<std::size_t>(remaining, std::numeric_limits<DWORD>::max())) };
                DWORD written { 0 };
                SetLastError(ERROR_SUCCESS);
                if (WriteFile(file, bytes.data() + offset, requested, &written, nullptr) == FALSE)
                    return { workspace_file_commit_failure::write, last_error_or(ERROR_WRITE_FAULT) };
                if (written == 0)
                    return { workspace_file_commit_failure::write, ERROR_WRITE_FAULT };
                offset += static_cast<std::size_t>(written);
            }
            return {};
        }

        void delete_temporary_file(const std::wstring& path) noexcept
        {
            if (path.empty() == false)
                DeleteFileW(path.c_str());
        }

        workspace_file_commit_result atomic_commit_impl(const std::u8string_view document_path, const std::u8string_view backup_path, const std::u8string_view bytes, const bool replace_existing)
        {
            prepared_path prepared_document { prepare_file_path(document_path) };
            if (prepared_document.value.has_value() == false)
                return { workspace_file_commit_failure::write, prepared_document.native_error };

            prepared_path prepared_backup {};
            if (replace_existing)
            {
                prepared_backup = prepare_file_path(backup_path);
                if (prepared_backup.value.has_value() == false)
                    return { workspace_file_commit_failure::write, prepared_backup.native_error };
            }

            temporary_file temporary { create_temporary_file(*prepared_document.value) };
            if (temporary.handle.valid() == false)
                return { workspace_file_commit_failure::write, temporary.native_error };

            const workspace_file_commit_result write_result { write_bytes(temporary.handle.get(), bytes) };
            if (write_result.succeeded() == false)
            {
                temporary.handle.reset();
                delete_temporary_file(temporary.path);
                return write_result;
            }

            SetLastError(ERROR_SUCCESS);
            if (FlushFileBuffers(temporary.handle.get()) == FALSE)
            {
                const std::uint32_t error { last_error_or(ERROR_WRITE_FAULT) };
                temporary.handle.reset();
                delete_temporary_file(temporary.path);
                return { workspace_file_commit_failure::flush, error };
            }
            temporary.handle.reset();

            SetLastError(ERROR_SUCCESS);
            if (replace_existing)
            {
                if (ReplaceFileW(prepared_document.value->c_str(), temporary.path.c_str(), prepared_backup.value->c_str(), 0, nullptr, nullptr) != FALSE)
                    return {};

                const std::uint32_t replace_error { last_error_or(ERROR_WRITE_FAULT) };
                if (replace_error == ERROR_UNABLE_TO_MOVE_REPLACEMENT_2)
                {
                    // 원본이 이미 backup 위치로 이동된 상태다. 복원까지 실패하면 원본이
                    // backup 경로에만 남으므로 호출자가 구분할 수 있게 별도로 보고한다.
                    SetLastError(ERROR_SUCCESS);
                    if (MoveFileExW(prepared_backup.value->c_str(), prepared_document.value->c_str(), MOVEFILE_WRITE_THROUGH) == FALSE)
                    {
                        delete_temporary_file(temporary.path);
                        return { workspace_file_commit_failure::restore, last_error_or(replace_error) };
                    }
                }
                delete_temporary_file(temporary.path);
                return { workspace_file_commit_failure::replace, replace_error };
            }

            if (MoveFileExW(temporary.path.c_str(), prepared_document.value->c_str(), MOVEFILE_WRITE_THROUGH) != FALSE)
                return {};

            const std::uint32_t replace_error { last_error_or(ERROR_WRITE_FAULT) };
            delete_temporary_file(temporary.path);
            return { workspace_file_commit_failure::replace, replace_error };
        }
    } // namespace

    workspace_file_read_result workspace_document_file_system::read(const std::u8string_view path) noexcept
    {
        try
        {
            return read_impl(path);
        }
        catch (...)
        {
            return { workspace_file_read_state::failed, {}, ERROR_NOT_ENOUGH_MEMORY };
        }
    }

    workspace_file_commit_result workspace_document_file_system::atomic_commit(
        const std::u8string_view document_path, const std::u8string_view backup_path, const std::u8string_view bytes, const bool replace_existing) noexcept
    {
        try
        {
            return atomic_commit_impl(document_path, backup_path, bytes, replace_existing);
        }
        catch (...)
        {
            return { workspace_file_commit_failure::write, ERROR_NOT_ENOUGH_MEMORY };
        }
    }
} // namespace gitman::win32
