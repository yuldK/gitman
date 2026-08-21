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

    // 명령줄 인자·drag & drop·파일 선택기로 받은 문서 경로를 절대 경로로 만든다
    // (app-shell-design A2.2). 구분자를 `\`로 통일하고 현재 작업 디렉터리 기준으로
    // 편다. 상대 경로 그대로 문서를 열면 문서 기준 상대 저장소 경로가 엉뚱한 곳을
    // 가리키기 때문이다. 펼 수 없는 형식은 원본을 그대로 돌려주고, 존재 여부는
    // 확인하지 않는다(열기 실패 경로가 사유를 보고한다).
    [[nodiscard]] std::u8string absolute_workspace_document_path(std::u8string_view path) noexcept;

    [[nodiscard]] project_path_resolution resolve_project_path(std::u8string_view original_path, std::u8string_view document_path) noexcept;
    [[nodiscard]] bool normalized_project_paths_equal(std::u8string_view left, std::u8string_view right) noexcept;

    // 속성 조회 실패 오류를 경로 상태로 변환한다. 로컬 NTFS는 속성 조회를 부모
    // 디렉터리 메타데이터로 처리해 deny ACE로도 접근 거부를 만들 수 없으므로,
    // `inaccessible` 분기는 실제 파일 시스템 대신 이 매핑으로 검증한다.
    [[nodiscard]] configured_path_state project_path_state_from_error(std::uint32_t native_error) noexcept;

    // Windows 규칙으로 동작하는 `project_path_resolver` 구현을 만든다.
    [[nodiscard]] std::unique_ptr<project_path_resolver> make_project_path_resolver();
} // namespace gitman::win32
