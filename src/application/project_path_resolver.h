#pragma once

#include "domain/project.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gitman {
    struct project_path_resolution
    {
        std::u8string normalized {};
        configured_path_state state { configured_path_state::invalid };
        std::optional<std::uint32_t> native_error {};
    };

    // 경로 정규화, 상태 판정과 중복 비교의 경계다. Windows 규칙 구현은 Win32 adapter에
    // 두고 infrastructure는 이 계약만 사용해, 단위 test가 실제 디스크를 조회하지 않고
    // 결정적으로 동작할 수 있게 한다.
    class project_path_resolver
    {
    public:
        project_path_resolver() = default;
        project_path_resolver(const project_path_resolver&) = delete;
        project_path_resolver(project_path_resolver&&) = delete;
        project_path_resolver& operator=(const project_path_resolver&) = delete;
        project_path_resolver& operator=(project_path_resolver&&) = delete;
        virtual ~project_path_resolver() = default;

        [[nodiscard]] virtual project_path_resolution resolve(std::u8string_view original_path, std::u8string_view document_path) noexcept = 0;
        [[nodiscard]] virtual bool normalized_equal(std::u8string_view left, std::u8string_view right) noexcept = 0;
    };
} // namespace gitman
