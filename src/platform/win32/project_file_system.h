#pragma once

#include "application/project_path_resolver.h"
#include "domain/project.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gitman::win32 {
    // 기존 호출자와 test가 win32 이름을 계속 쓸 수 있도록 application 계약의 값 type을
    // 그대로 노출한다.
    using project_path_resolution = gitman::project_path_resolution;

    [[nodiscard]] project_path_resolution resolve_project_path(std::u8string_view original_path, std::u8string_view document_path) noexcept;
    [[nodiscard]] bool normalized_project_paths_equal(std::u8string_view left, std::u8string_view right) noexcept;

    // 속성 조회 실패 오류를 경로 상태로 변환한다. 로컬 NTFS는 속성 조회를 부모
    // 디렉터리 메타데이터로 처리해 deny ACE로도 접근 거부를 만들 수 없으므로,
    // `inaccessible` 분기는 실제 파일 시스템 대신 이 매핑으로 검증한다.
    [[nodiscard]] configured_path_state project_path_state_from_error(std::uint32_t native_error) noexcept;

    // Windows 규칙으로 동작하는 `project_path_resolver` 구현을 만든다.
    [[nodiscard]] std::unique_ptr<project_path_resolver> make_project_path_resolver();
} // namespace gitman::win32
