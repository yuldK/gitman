#pragma once

#include "domain/diagnostic.h"
#include "domain/project.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    struct workspace_document_shadow
    {
        std::u8string source_json {};
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
} // namespace gitman
