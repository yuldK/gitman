#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // diff viewer 한 줄의 색 분류다 (field-feedback-design 2.3). 렌더링과 test가
    // 같은 판정을 쓰도록 순수 함수로 둔다.
    enum class diff_line_class
    {
        context,
        added,
        removed,
        // `diff --git`, `Index:`, `@@` 같은 구조 줄이다. 흐리게 그린다.
        heading,
    };

    [[nodiscard]] diff_line_class classify_diff_line(std::u8string_view line) noexcept;

    // 2-way diff viewer의 한 행이다. unified diff를 좌(이전)·우(이후) 열로 정렬해
    // 만든다. `heading`이면 `left`에 담긴 구조 줄 하나를 전체 폭으로 그린다.
    struct two_way_diff_row
    {
        bool heading { false };
        // 좌우가 -/+ 쌍(또는 한쪽만 있는 변경)이다. 거짓이면 양쪽이 같은 문맥 줄이다.
        bool changed { false };
        std::u8string left {};
        std::u8string right {};
        bool has_left { false };
        bool has_right { false };

        [[nodiscard]] bool operator==(const two_way_diff_row&) const noexcept = default;
    };

    // unified diff 줄을 2-way 행으로 바꾼다. hunk 안에서 연속한 `-` 묶음과 `+`
    // 묶음을 순서대로 짝지어 좌우로 배치하고, 남는 쪽은 빈 칸으로 둔다. 표시 줄의
    // `-`/`+`/문맥 접두 문자는 떼어 낸다.
    [[nodiscard]] std::vector<two_way_diff_row> build_two_way_diff(const std::vector<std::u8string>& unified_lines);
} // namespace gitman
