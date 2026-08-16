#pragma once

#include "domain/diagnostic.h"
#include "domain/project.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    struct workspace_document_shadow
    {
        std::u8string source_json {};
        std::vector<std::size_t> project_source_indices {};
    };

    struct workspace_document_parse_result
    {
        std::optional<workspace_document> document {};
        std::vector<diagnostic> diagnostics {};
        workspace_document_shadow shadow {};

        [[nodiscard]] bool has_errors() const noexcept;
        [[nodiscard]] bool has_warnings() const noexcept;
    };

    [[nodiscard]] workspace_document_parse_result parse_workspace_document_json(std::u8string_view source_json, std::u8string_view document_path);

    // `display_name` 생략 시 사용하는 경로 마지막 구성 요소다. parser와 저장 경로가
    // 같은 규칙을 공유해야 저장 시 기본값 생략 판정이 어긋나지 않는다.
    [[nodiscard]] std::u8string default_project_display_name(std::u8string_view path);
} // namespace gitman
