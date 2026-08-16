#pragma once

#include "domain/repository_snapshot.h"
#include "domain/vcs_tool.h"

#include <optional>
#include <string_view>

namespace gitman {
    // `--version` 첫 줄에서 버전을 뽑는다. 형식은 다음과 같다.
    //
    //     git version 2.52.0.windows.1
    //     svn, version 1.14.5 (r1922182)
    //
    // 네 번째 이후 구성 요소와 플랫폼 접미사는 비교에 쓰지 않는다. Git for Windows의
    // `.windows.N`처럼 배포판마다 다른 값이라 최소 버전 판정 기준이 될 수 없다.
    [[nodiscard]] std::optional<vcs_tool_version> parse_vcs_tool_version(repository_kind kind, std::u8string_view first_line) noexcept;

    // 여러 줄을 받은 경우 첫 줄만 골라 파싱한다. `--version`이 여러 줄을 내는 SVN을
    // 위한 편의 함수다.
    [[nodiscard]] std::optional<vcs_tool_version> parse_vcs_tool_version_output(repository_kind kind, std::u8string_view output) noexcept;

    [[nodiscard]] bool meets_minimum_vcs_version(repository_kind kind, const vcs_tool_version& version) noexcept;
} // namespace gitman
