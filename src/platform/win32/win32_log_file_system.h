#pragma once

#include "application/log_file_system.h"

namespace gitman::win32 {
    // 로그 적재의 Win32 구현이다 (app-shell-design A4.4). 로그 writer thread에서만
    // 호출한다.
    class log_file_system final : public gitman::log_file_system
    {
    public:
        [[nodiscard]] bool create_directories(std::u8string_view path) noexcept override;
        [[nodiscard]] bool file_exists(std::u8string_view path) noexcept override;
        [[nodiscard]] bool append_file(std::u8string_view path, std::u8string_view bytes) noexcept override;
    };
} // namespace gitman::win32
