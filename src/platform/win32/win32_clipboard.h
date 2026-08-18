#pragma once

#include <windows.h>

#include <string_view>

namespace gitman::win32 {
    // UTF-8 텍스트를 Win32 클립보드에 넣는다. UI thread 전용이며 실패해도 앱 상태를
    // 바꾸지 않으므로 성공 여부만 돌려준다.
    [[nodiscard]] bool copy_text_to_clipboard(HWND owner, std::u8string_view text) noexcept;
} // namespace gitman::win32
