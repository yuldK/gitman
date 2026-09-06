#pragma once

#include "luil/win32/renderer_policy.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace gitman {
    // 렌더러 정책은 luil이 소유한다 (설계 L4). 값 타입만 담은 헤더라 application
    // 계층이 include해도 되며, 기존 호출자가 `gitman::renderer_mode`를 그대로 쓸 수
    // 있도록 이름만 들여온다.
    using luil::renderer_backend;
    using luil::renderer_mode;
    using luil::renderer_selection;
    using luil::renderer_selection_status;

    using luil::parse_renderer_mode;
    using luil::renderer_backend_name;
    using luil::renderer_mode_name;
    using luil::select_renderer_backend;

    struct application_options
    {
        renderer_mode renderer { renderer_mode::automatic };
        std::optional<std::u8string> workspace_document_path {};
        bool smoke_test { false };
        bool simulate_direct3d_failure { false };
        // `.version-list` file association의 등록·제거만 수행하고 종료한다
        // (REQ-016, 단계 8). 창을 만들지 않는다.
        bool register_file_association { false };
        bool unregister_file_association { false };
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
