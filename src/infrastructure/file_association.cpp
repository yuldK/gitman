#include "infrastructure/file_association.h"

#include "domain/project.h"

namespace gitman {
    std::u8string file_association_extension_subkey()
    {
        return std::u8string { workspace_document_extension };
    }

    std::vector<registry_default_value> make_file_association_values(const std::u8string_view executable_path)
    {
        const std::u8string prog_id { file_association_prog_id };
        const std::u8string quoted_executable { std::u8string { u8"\"" } + std::u8string { executable_path } + u8"\"" };

        std::vector<registry_default_value> values {};
        values.push_back({ file_association_extension_subkey(), prog_id });
        values.push_back({ prog_id, std::u8string { u8"Gitman 버전 목록 문서" } });
        // exe에 embed된 첫 아이콘이다.
        values.push_back({ prog_id + u8"\\DefaultIcon", quoted_executable + u8",0" });
        values.push_back({ prog_id + u8"\\shell\\open\\command", quoted_executable + u8" \"%1\"" });
        return values;
    }

    std::vector<std::u8string> file_association_prog_id_subkeys()
    {
        const std::u8string prog_id { file_association_prog_id };
        return {
            prog_id + u8"\\shell\\open\\command",
            prog_id + u8"\\shell\\open",
            prog_id + u8"\\shell",
            prog_id + u8"\\DefaultIcon",
            prog_id,
        };
    }

    bool owns_extension_link(const std::u8string_view current_prog_id) noexcept
    {
        return current_prog_id == file_association_prog_id;
    }
} // namespace gitman
