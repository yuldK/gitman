#pragma once

#include "domain/diagnostic.h"
#include "domain/repository_snapshot.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // ADR-003이 정한 최소 지원 버전이다.
    inline constexpr std::uint32_t minimum_git_version[3] { 2u, 43u, 0u };
    inline constexpr std::uint32_t minimum_subversion_version[3] { 1u, 14u, 5u };

    enum class vcs_tool_availability
    {
        // 아직 조사하지 않았다.
        unknown,
        // 자동 탐색으로도 실행 파일을 찾지 못했다.
        not_found,
        // `settings`에 지정한 경로가 절대 경로가 아니거나 실행할 수 없다. 이 경우
        // 자동 탐색으로 물러서지 않는다. 사용자가 의도해서 지정한 값이기 때문이다.
        path_invalid,
        // 실행은 됐지만 `--version` 출력을 해석하지 못했다.
        version_unreadable,
        too_old,
        available,
    };

    struct vcs_tool_version
    {
        std::uint32_t major { 0 };
        std::uint32_t minor { 0 };
        std::uint32_t patch { 0 };

        [[nodiscard]] bool operator==(const vcs_tool_version&) const noexcept = default;
        [[nodiscard]] auto operator<=>(const vcs_tool_version&) const noexcept = default;
    };

    struct vcs_tool_info
    {
        repository_kind kind { repository_kind::unknown };
        vcs_tool_availability availability { vcs_tool_availability::unknown };
        // 발견한 절대 경로다. `not_found`면 비어 있다.
        std::u8string executable {};
        // `svnversion`처럼 주 실행 파일과 함께 배포되는 보조 도구다. 없어도 조회를
        // 막지 않고 해당 판정만 포기한다.
        std::u8string auxiliary_executable {};
        // `--version`의 첫 줄 원문이다. 진단과 사용자 표시에 사용한다.
        std::u8string reported_version {};
        vcs_tool_version version {};
        // `settings`로 사용자가 직접 지정한 경로인지 여부다.
        bool manually_configured { false };
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool usable() const noexcept;
    };

    // Git과 SVN 중 어느 것도 없는 환경을 1급 상태로 다룬다. 이때도 앱은 프로젝트
    // 목록을 열고 카드를 보여 주며, 조회와 변경 동작만 비활성화된다.
    struct vcs_tool_set
    {
        vcs_tool_info git {};
        vcs_tool_info subversion {};

        [[nodiscard]] const vcs_tool_info& tool(repository_kind kind) const noexcept;
        [[nodiscard]] bool available(repository_kind kind) const noexcept;
        [[nodiscard]] bool any_available() const noexcept;
        [[nodiscard]] bool none_available() const noexcept;
    };

    [[nodiscard]] vcs_tool_version minimum_supported_version(repository_kind kind) noexcept;
    [[nodiscard]] std::u8string_view vcs_tool_availability_name(vcs_tool_availability availability) noexcept;
    [[nodiscard]] std::u8string format_vcs_tool_version(const vcs_tool_version& version);
} // namespace gitman
