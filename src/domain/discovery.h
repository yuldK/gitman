#pragma once

#include "domain/diagnostic.h"
#include "domain/repository_snapshot.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    enum class discovery_exclusion
    {
        none,
        not_a_repository,
        bare_repository,
        conflicting_metadata,
        reparse_point,
        already_registered,
        inaccessible,
    };

    // 자식 디렉터리 하나에서 확인한 표식이다. filesystem 확인과 판정 규칙을 분리해
    // 판정 자체를 실제 디스크 없이 결정적으로 검증할 수 있게 한다.
    struct repository_marker_set
    {
        // 표식 확인 자체가 실패했다. 이 값이 켜지면 나머지 표식은 신뢰하지 않는다.
        bool probe_failed { false };
        bool has_git_directory { false };
        bool has_git_file { false };
        bool has_svn_directory { false };
        bool has_head_file { false };
        bool has_objects_directory { false };
        bool has_refs_directory { false };
    };

    struct discovery_classification
    {
        repository_kind kind { repository_kind::unknown };
        bool via_git_file { false };
        discovery_exclusion exclusion { discovery_exclusion::none };

        [[nodiscard]] bool operator==(const discovery_classification&) const noexcept = default;
    };

    // 계획 4.2의 표식 판정이다. 확인 실패 → 메타데이터 충돌 → `.git` 디렉터리 →
    // `.git` 파일 → `.svn` → bare 휴리스틱 → 비저장소 순서로 본다. bare는 세 표식
    // (`HEAD`, `objects`, `refs`)이 모두 있을 때만 인정해 오탐을 줄인다.
    [[nodiscard]] discovery_classification classify_discovery_markers(const repository_marker_set& markers) noexcept;

    struct discovery_candidate
    {
        std::u8string directory_name {};
        std::u8string absolute_path {};
        std::u8string normalized_path {};
        repository_kind kind { repository_kind::unknown };
        // `.git` 파일로 판정한 Git 후보다. linked worktree와 submodule이 여기에
        // 해당하며 둘의 구분은 단계 6~7의 UI 요구가 생길 때 추가한다.
        bool via_git_file { false };
        discovery_exclusion exclusion { discovery_exclusion::none };

        [[nodiscard]] bool selectable() const noexcept;
    };

    struct discovery_result
    {
        // 스캔 루트 실패나 취소 없이 끝까지 열거했다는 표시다. 개별 자식의 실패는
        // 해당 후보의 `inaccessible`로만 남고 이 값을 끄지 않는다.
        bool completed { false };
        bool root_is_repository { false };
        std::vector<discovery_candidate> candidates {};
        std::vector<diagnostic> diagnostics {};
    };

    // 후보 목록의 결정적 정렬 기준이다. filesystem 열거 순서에 의존하지 않도록
    // 이름의 ASCII 대소문자 무시 오름차순으로 두고, 대소문자만 다른 이름은 code unit
    // 순서로, 이름까지 같으면 절대 경로로 구분한다.
    [[nodiscard]] bool discovery_candidate_before(const discovery_candidate& left, const discovery_candidate& right) noexcept;

    [[nodiscard]] std::u8string_view discovery_exclusion_name(discovery_exclusion value) noexcept;
} // namespace gitman
