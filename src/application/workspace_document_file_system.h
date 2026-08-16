#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gitman {
    enum class workspace_file_read_state
    {
        available,
        not_found,
        failed,
    };

    struct workspace_file_read_result
    {
        workspace_file_read_state state { workspace_file_read_state::failed };
        std::u8string bytes {};
        std::optional<std::uint32_t> native_error {};
    };

    enum class workspace_file_commit_failure
    {
        none,
        write,
        flush,
        replace,
        // replace 도중 원본이 backup 위치로 이동된 뒤 복원까지 실패했다. 원본 내용은
        // backup 경로에만 남아 있으므로 호출자가 복구 안내를 구분해서 표시해야 한다.
        restore,
    };

    struct workspace_file_commit_result
    {
        workspace_file_commit_failure failure { workspace_file_commit_failure::none };
        std::optional<std::uint32_t> native_error {};

        [[nodiscard]] bool succeeded() const noexcept
        {
            return failure == workspace_file_commit_failure::none;
        }
    };

    class workspace_document_file_system
    {
    public:
        workspace_document_file_system() = default;
        workspace_document_file_system(const workspace_document_file_system&) = delete;
        workspace_document_file_system(workspace_document_file_system&&) = delete;
        workspace_document_file_system& operator=(const workspace_document_file_system&) = delete;
        workspace_document_file_system& operator=(workspace_document_file_system&&) = delete;
        virtual ~workspace_document_file_system() = default;

        [[nodiscard]] virtual workspace_file_read_result read(std::u8string_view path) noexcept = 0;
        [[nodiscard]] virtual workspace_file_commit_result atomic_commit(std::u8string_view document_path, std::u8string_view backup_path, std::u8string_view bytes, bool replace_existing) noexcept
            = 0;
    };
} // namespace gitman
