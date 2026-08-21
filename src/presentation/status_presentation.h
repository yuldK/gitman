#pragma once

#include "domain/repository_snapshot.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace gitman {
    // 카드 하나의 겉모습 상태다 (plan 3.8). 렌더러는 이 값으로 색과 진행 표시를
    // 고른다.
    enum class card_view_state
    {
        loading,
        ready,
        running,
        warning,
        failed,
        disabled,
    };

    // 상태 아이콘 하나의 표시 정보다. 아이콘 이름은 Codicons의 공식 이름이며 실제
    // 글리프 해석은 렌더러의 codicon registry가 담당한다. 아이콘만으로 의미를
    // 전달하지 않도록 한국어 툴팁을 항상 함께 둔다 (REQ-005).
    struct status_glyph
    {
        std::u8string_view codicon {};
        std::u8string tooltip {};
        // 확인되지 않은 상태(`?`)다. 렌더러가 상태 강조색 대신 비활성 계열로
        // 그려 정상(키 컬러)처럼 보이지 않게 한다. codicon 이름 비교 대신 이
        // flag를 쓰는 이유는 표현 규칙이 글리프 교체와 무관하게 유지되기 위해서다.
        bool undetermined { false };
    };

    // plan 3.2의 표를 그대로 구현한다. 수치가 있는 상태는 툴팁에 개수를 담는다.
    [[nodiscard]] status_glyph sync_state_glyph(remote_sync_state state, std::uint64_t ahead_count, std::uint64_t behind_count);

    // 조회할 수 없는 저장소의 표시다. `ready`는 sync 상태 아이콘이 대신하므로 여기서
    // 다루지 않는다.
    [[nodiscard]] status_glyph availability_glyph(repository_availability availability);

    // 카드에 표시할 작업 트리 요약이다. 깨끗하면 빈 문자열이다.
    [[nodiscard]] std::u8string working_tree_summary_text(const working_tree_summary& summary);

    inline constexpr std::size_t displayed_git_revision_length { 7 };

    // 저장된 revision은 그대로 두고 카드에 그릴 때만 Git object id를 줄인다. 해시
    // 형식이나 길이를 검증하지 않으며 SVN의 숫자 revision은 그대로 표시한다.
    [[nodiscard]] std::u8string_view revision_display_text(repository_kind kind, std::u8string_view revision) noexcept;

    [[nodiscard]] std::u8string_view card_view_state_name(card_view_state state) noexcept;
} // namespace gitman
