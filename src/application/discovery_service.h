#pragma once

#include "application/directory_enumerator.h"
#include "application/process_cancellation.h"
#include "application/project_path_resolver.h"
#include "application/vcs_file_probe.h"
#include "domain/discovery.h"
#include "domain/project.h"

#include <string_view>

namespace gitman {
    // 자식 디렉터리 하나의 표식을 `vcs_file_probe`로 확인한다. 현재 probe 계약은 존재
    // 여부만 알려 주고 접근 실패를 구분하지 않으므로 `probe_failed`를 켤 수 없다.
    // 판정 규칙의 `inaccessible` 분기는 계약이 확장될 때를 위해 유지한다.
    [[nodiscard]] repository_marker_set collect_repository_markers(const vcs_file_probe& probe, std::u8string_view absolute_directory);

    // 깊이 1 자식 탐색이다 (REQ-004). 프로세스를 만들지 않고 표식 파일만 확인하며,
    // 발견 결과를 저장하지 않고 후보 목록으로만 반환한다. 등록은
    // `project_registration_service`가 담당한다.
    class discovery_service
    {
    public:
        // 주입받은 계약들은 service보다 오래 살아 있어야 한다.
        discovery_service(const directory_enumerator& enumerator, const vcs_file_probe& probe, project_path_resolver& resolver) noexcept;

        // `scan_root`는 절대 경로여야 한다. 자식 하나의 실패는 그 후보의 제외 사유로만
        // 남고 전체 탐색을 중단하지 않는다. 취소는 자식 경계에서 확인한다.
        [[nodiscard]] discovery_result discover_children(std::u8string_view scan_root, const workspace_document& document, const process_cancellation_token& token) noexcept;

    private:
        const directory_enumerator* enumerator_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
        project_path_resolver* resolver_ { nullptr };
    };
} // namespace gitman
