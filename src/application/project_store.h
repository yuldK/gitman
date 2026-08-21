#pragma once

#include "domain/diagnostic.h"
#include "domain/project.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    class workspace_revision_token
    {
    public:
        workspace_revision_token() noexcept = default;
        workspace_revision_token(const workspace_revision_token&) noexcept = default;
        workspace_revision_token(workspace_revision_token&&) noexcept = default;
        workspace_revision_token& operator=(const workspace_revision_token&) noexcept = default;
        workspace_revision_token& operator=(workspace_revision_token&&) noexcept = default;
        ~workspace_revision_token() = default;

        [[nodiscard]] bool valid() const noexcept;

    private:
        struct state;

        explicit workspace_revision_token(std::shared_ptr<const state> value) noexcept;

        std::shared_ptr<const state> state_ {};

        friend class project_store;
    };

    struct project_store_load_result
    {
        std::optional<workspace_document> document {};
        std::vector<diagnostic> diagnostics {};
        workspace_revision_token revision {};

        [[nodiscard]] bool has_errors() const noexcept;
        [[nodiscard]] bool has_warnings() const noexcept;
    };

    struct project_store_save_result
    {
        std::optional<workspace_revision_token> revision {};
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool succeeded() const noexcept;
        [[nodiscard]] bool has_errors() const noexcept;
        [[nodiscard]] bool has_warnings() const noexcept;
    };

    class project_store
    {
    public:
        project_store() = default;
        project_store(const project_store&) = delete;
        project_store(project_store&&) = delete;
        project_store& operator=(const project_store&) = delete;
        project_store& operator=(project_store&&) = delete;
        virtual ~project_store() = default;

        [[nodiscard]] virtual project_store_load_result load(std::u8string_view document_path) noexcept = 0;
        [[nodiscard]] virtual project_store_save_result save(std::u8string_view document_path, const workspace_document& document, const workspace_revision_token& expected_revision) noexcept = 0;

    protected:
        enum class revision_file_state
        {
            unavailable,
            missing,
            present,
        };

        struct revision_view
        {
            revision_file_state file_state { revision_file_state::unavailable };
            std::u8string_view document_path {};
            std::u8string_view expected_source_bytes {};
            std::u8string_view shadow_source_json {};
            std::span<const std::size_t> project_source_indices {};
        };

        [[nodiscard]] static workspace_revision_token make_revision_token(
            revision_file_state file_state, std::u8string document_path, std::u8string expected_source_bytes, std::u8string shadow_source_json, std::vector<std::size_t> project_source_indices);
        [[nodiscard]] static std::optional<revision_view> inspect_revision(const workspace_revision_token& token) noexcept;
    };
} // namespace gitman
