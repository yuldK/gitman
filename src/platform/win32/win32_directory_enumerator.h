#pragma once

#include "application/directory_enumerator.h"

#include <memory>
#include <string_view>

namespace gitman::win32 {
    [[nodiscard]] directory_listing enumerate_directory(std::u8string_view absolute_directory) noexcept;

    [[nodiscard]] std::unique_ptr<directory_enumerator> make_directory_enumerator();
} // namespace gitman::win32
