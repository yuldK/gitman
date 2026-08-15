#include "application/project_store.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace gitman {
    struct workspace_revision_token::state
    {
        std::uint8_t file_state {};
        std::u8string document_path {};
        std::u8string expected_source_bytes {};
        std::u8string shadow_source_json {};
        std::vector<std::size_t> project_source_indices {};
    };

    workspace_revision_token::workspace_revision_token(std::shared_ptr<const state> value) noexcept
        : state_ { std::move(value) }
    {}

    bool workspace_revision_token::valid() const noexcept
    {
        return state_ != nullptr;
    }

    bool project_store_load_result::has_errors() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::error; });
    }

    bool project_store_load_result::has_warnings() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::warning; });
    }

    bool project_store_save_result::succeeded() const noexcept
    {
        return revision.has_value() && has_errors() == false;
    }

    bool project_store_save_result::has_errors() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::error; });
    }

    bool project_store_save_result::has_warnings() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::warning; });
    }

    workspace_revision_token project_store::make_revision_token(
        const revision_file_state file_state, std::u8string document_path, std::u8string expected_source_bytes, std::u8string shadow_source_json, std::vector<std::size_t> project_source_indices)
    {
        auto value { std::make_shared<workspace_revision_token::state>() };
        value->file_state = static_cast<std::uint8_t>(file_state);
        value->document_path = std::move(document_path);
        value->expected_source_bytes = std::move(expected_source_bytes);
        value->shadow_source_json = std::move(shadow_source_json);
        value->project_source_indices = std::move(project_source_indices);
        return workspace_revision_token { std::move(value) };
    }

    std::optional<project_store::revision_view> project_store::inspect_revision(const workspace_revision_token& token) noexcept
    {
        if (token.state_ == nullptr)
            return std::nullopt;

        return revision_view {
            static_cast<revision_file_state>(token.state_->file_state),
            token.state_->document_path,
            token.state_->expected_source_bytes,
            token.state_->shadow_source_json,
            token.state_->project_source_indices,
        };
    }
} // namespace gitman
