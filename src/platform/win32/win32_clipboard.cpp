#include "platform/win32/win32_clipboard.h"

#include "platform/win32/utf8.h"

#include <cstring>

namespace gitman::win32 {
    bool copy_text_to_clipboard(const HWND owner, const std::u8string_view text) noexcept
    {
        try
        {
            const utf_conversion_result<std::wstring> converted { utf8_to_utf16(text) };
            if (converted.value.has_value() == false)
                return false;

            const std::wstring& wide { *converted.value };
            const SIZE_T bytes { (wide.size() + 1) * sizeof(wchar_t) };
            const HGLOBAL storage { GlobalAlloc(GMEM_MOVEABLE, bytes) };
            if (storage == nullptr)
                return false;

            void* const memory { GlobalLock(storage) };
            if (memory == nullptr)
            {
                GlobalFree(storage);
                return false;
            }
            std::memcpy(memory, wide.c_str(), bytes);
            GlobalUnlock(storage);

            if (OpenClipboard(owner) == FALSE)
            {
                GlobalFree(storage);
                return false;
            }
            EmptyClipboard();
            // 성공하면 소유권이 시스템으로 넘어가므로 해제하지 않는다.
            const bool stored { SetClipboardData(CF_UNICODETEXT, storage) != nullptr };
            if (stored == false)
                GlobalFree(storage);
            CloseClipboard();
            return stored;
        }
        catch (...)
        {
            return false;
        }
    }

    std::u8string read_text_from_clipboard(const HWND owner) noexcept
    {
        try
        {
            if (IsClipboardFormatAvailable(CF_UNICODETEXT) == FALSE || OpenClipboard(owner) == FALSE)
                return {};

            std::u8string result {};
            if (const HANDLE handle { GetClipboardData(CF_UNICODETEXT) }; handle != nullptr)
                if (const void* const memory { GlobalLock(handle) }; memory != nullptr)
                {
                    const utf_conversion_result<std::u8string> converted { utf16_to_utf8(static_cast<const wchar_t*>(memory)) };
                    if (converted.value.has_value())
                        result = *converted.value;
                    GlobalUnlock(handle);
                }
            CloseClipboard();

            // 한 줄 칸이라 첫 줄만 쓴다. 탭도 줄 구분과 같이 취급한다.
            if (const std::size_t line_end { result.find_first_of(u8"\r\n\t") }; line_end != std::u8string::npos)
                result.erase(line_end);
            return result;
        }
        catch (...)
        {
            return {};
        }
    }
} // namespace gitman::win32
