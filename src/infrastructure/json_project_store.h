#pragma once

#include "application/project_path_resolver.h"
#include "application/project_store.h"
#include "application/workspace_document_file_system.h"

#include <string>
#include <string_view>

namespace gitman {
    class json_project_store final : public project_store
    {
    public:
        json_project_store(workspace_document_file_system& file_system, project_path_resolver& path_resolver) noexcept;

        [[nodiscard]] project_store_load_result load(std::u8string_view document_path) noexcept override;
        [[nodiscard]] project_store_save_result save(std::u8string_view document_path, const workspace_document& document, const workspace_revision_token& expected_revision) noexcept override;

    private:
        [[nodiscard]] project_store_load_result load_impl(std::u8string_view document_path);
        [[nodiscard]] project_store_save_result save_impl(std::u8string_view document_path, const workspace_document& document, const workspace_revision_token& expected_revision);

        workspace_document_file_system& file_system_;
        project_path_resolver& path_resolver_;
    };
} // namespace gitman
