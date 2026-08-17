#pragma once

#include "presentation/renderer_policy.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gitman {
    struct application_options
    {
        renderer_mode renderer { renderer_mode::automatic };
        std::optional<std::u8string> workspace_document_path {};
        bool smoke_test { false };
        bool simulate_direct3d_failure { false };
    };

    struct application_options_result
    {
        std::optional<application_options> options {};
        std::u8string error {};
    };

    // 명령줄, 파일 선택기, shell drag & drop이 같은 확장자 규칙을 쓴다.
    [[nodiscard]] bool has_workspace_document_extension(std::u8string_view path) noexcept;
    [[nodiscard]] application_options_result parse_application_options(std::span<const std::u8string> arguments);
} // namespace gitman
