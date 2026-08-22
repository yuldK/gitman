#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace gitman::win32 {
    // UTF-8 텍스트를 Win32 클립보드에 넣는다. UI thread 전용이며 실패해도 앱 상태를
    // 바꾸지 않으므로 성공 여부만 돌려준다.
    [[nodiscard]] bool copy_text_to_clipboard(HWND owner, std::u8string_view text) noexcept;

    // 클립보드의 UTF-8 텍스트다. 텍스트가 없거나 읽지 못하면 빈 값이다. 한 줄 입력
    // 칸에 넣을 값이라 줄바꿈에서 잘라 첫 줄만 돌려준다
    // (switch-dialog-query-bar-design R2.5).
    [[nodiscard]] std::u8string read_text_from_clipboard(HWND owner) noexcept;
} // namespace gitman::win32
