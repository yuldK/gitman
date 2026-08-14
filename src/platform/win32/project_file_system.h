#pragma once

#include "domain/project.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gitman::win32 {
    struct project_path_resolution
    {
        std::u8string normalized {};
        configured_path_state state { configured_path_state::invalid };
        std::optional<std::uint32_t> native_error {};
    };

    [[nodiscard]] project_path_resolution resolve_project_path(std::u8string_view original_path, std::u8string_view document_path) noexcept;
    [[nodiscard]] bool normalized_project_paths_equal(std::u8string_view left, std::u8string_view right) noexcept;
} // namespace gitman::win32
