#pragma once

#include "application/discovery_service.h"
#include "application/process_cancellation.h"
#include "application/project_registration_service.h"
#include "application/project_store.h"

#include <optional>
#include <string_view>
#include <vector>

namespace gitman {
    struct version_list_generation_result
    {
        bool succeeded { false };
        // 성공한 경우에만 값이 있다. 저장된 문서와 다음 저장의 기준 revision이며,
        // logic thread가 활성 문서를 이것으로 바꾼다.
        std::optional<workspace_document> document {};
        std::optional<workspace_revision_token> revision {};
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool has_errors() const noexcept;
    };

    // 폴더의 깊이 1 하위 디렉터리에서 저장소를 찾아 새 `.version-list` 문서를
    // 만든다 (REQ-004의 탐색과 등록을 신규 문서로 조합). worker thread에서 동기
    // 호출되며, 출력 경로에 이미 문서가 있으면 덮어쓰지 않는다.
    class version_list_generation_service
    {
    public:
        // 주입받은 계약들은 service보다 오래 살아 있어야 한다.
        version_list_generation_service(discovery_service& discovery, project_registration_service& registration, project_store& store) noexcept;

        [[nodiscard]] version_list_generation_result generate(std::u8string_view scan_root, std::u8string_view document_path, const process_cancellation_token& token) noexcept;

    private:
        discovery_service* discovery_ { nullptr };
        project_registration_service* registration_ { nullptr };
        project_store* store_ { nullptr };
    };
} // namespace gitman
