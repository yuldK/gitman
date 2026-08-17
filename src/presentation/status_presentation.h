#pragma once

#include "domain/repository_snapshot.h"

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
    };

    // plan 3.2의 표를 그대로 구현한다. 수치가 있는 상태는 툴팁에 개수를 담는다.
    [[nodiscard]] status_glyph sync_state_glyph(remote_sync_state state, std::uint64_t ahead_count, std::uint64_t behind_count);

    // 조회할 수 없는 저장소의 표시다. `ready`는 sync 상태 아이콘이 대신하므로 여기서
    // 다루지 않는다.
    [[nodiscard]] status_glyph availability_glyph(repository_availability availability);

    // 카드에 표시할 작업 트리 요약이다. 깨끗하면 빈 문자열이다.
    [[nodiscard]] std::u8string working_tree_summary_text(const working_tree_summary& summary);

    [[nodiscard]] std::u8string_view card_view_state_name(card_view_state state) noexcept;
} // namespace gitman
