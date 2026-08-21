#pragma once

#include "application/workspace_document_file_system.h"

namespace gitman::win32 {
    class workspace_document_file_system final : public gitman::workspace_document_file_system
    {
    public:
        [[nodiscard]] workspace_file_read_result read(std::u8string_view path) noexcept override;
        [[nodiscard]] workspace_file_commit_result atomic_commit(std::u8string_view document_path, std::u8string_view bytes, bool replace_existing) noexcept override;
    };
} // namespace gitman::win32
