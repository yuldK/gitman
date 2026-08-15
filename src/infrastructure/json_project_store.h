#pragma once

#include "application/project_store.h"
#include "application/workspace_document_file_system.h"

#include <string>
#include <string_view>

namespace gitman {
    [[nodiscard]] std::u8string workspace_document_backup_path(std::u8string_view document_path);

    class json_project_store final : public project_store
    {
    public:
        explicit json_project_store(workspace_document_file_system& file_system) noexcept;

        [[nodiscard]] project_store_load_result load(std::u8string_view document_path) noexcept override;
        [[nodiscard]] project_store_load_result load_backup(std::u8string_view document_path) noexcept override;
        [[nodiscard]] project_store_save_result save(std::u8string_view document_path, const workspace_document& document, const workspace_revision_token& expected_revision) noexcept override;

    private:
        [[nodiscard]] project_store_load_result load_impl(std::u8string_view document_path);
        [[nodiscard]] project_store_load_result load_backup_impl(std::u8string_view document_path);
        [[nodiscard]] project_store_save_result save_impl(std::u8string_view document_path, const workspace_document& document, const workspace_revision_token& expected_revision);
        void append_recovery_diagnostic(std::u8string_view document_path, project_store_load_result& result);

        workspace_document_file_system& file_system_;
    };
} // namespace gitman
