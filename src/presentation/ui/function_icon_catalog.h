#pragma once

#include <string_view>

namespace gitman::ui {
    // 아이콘 이름 → codicon 글리프 표다 (function-buttons-design D16, `status_glyphs.cpp` 선례). 손으로 쓴
    // 이름 20개 안팎(기능 버튼의 `icon`)과 메뉴 항목이 쓰는 이름(T8의 6개, `stop-circle` 등)을 한 표에 둔다.
    // 둘을 나누면 같은 기능이 카드 버튼과 메뉴에서 다른 아이콘으로 보인다.

    // 표에 있는 이름의 글리프다. 비었거나 모르는 이름이면 0이다 (메뉴의 기존 항목은 아이콘 없음).
    [[nodiscard]] char32_t icon_glyph_by_name(std::u8string_view name) noexcept;

    // 기능 버튼의 글리프다. D16의 20개만 찾고, 그 밖의 이름(메뉴 전용 이름 포함)은 기본 `play`이며 경고하지 않는다
    // (3.2 `icon`).
    [[nodiscard]] char32_t function_icon_glyph(std::u8string_view name) noexcept;
}
