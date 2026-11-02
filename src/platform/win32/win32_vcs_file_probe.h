#pragma once

#include "application/vcs_file_probe.h"
#include "infrastructure/vcs_tool_discovery.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gitman::win32 {
    [[nodiscard]] vcs_path_kind probe_vcs_path(std::u8string_view absolute_path) noexcept;

    // 파일 앞부분을 상한까지 읽는다 (`vcs_file_probe::read_prefix`의 구현).
    [[nodiscard]] vcs_file_content read_vcs_file_prefix(std::u8string_view absolute_path, std::size_t maximum_bytes) noexcept;

    [[nodiscard]] std::unique_ptr<vcs_file_probe> make_vcs_file_probe();

    // 정의되지 않았으면 값이 없다. 빈 값으로 정의된 변수는 빈 문자열이다. 기능 버튼의
    // `${env:NAME}`이 미정의를 거부하려고 둘을 가른다 (function-buttons-design 3.3).
    [[nodiscard]] std::optional<std::u8string> find_environment_variable(std::u8string_view name) noexcept;
    // 정의되지 않은 변수도 빈 문자열이다.
    [[nodiscard]] std::u8string read_environment_variable(std::u8string_view name) noexcept;

    // 호스트의 `PATH`와 Program Files 위치를 읽어 탐색 입력을 만든다. 탐색 규칙 자체는
    // 순수 함수라 이 함수 없이도 test할 수 있다.
    [[nodiscard]] vcs_tool_environment current_vcs_tool_environment() noexcept;
} // namespace gitman::win32
