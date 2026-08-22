#pragma once

#include "presentation/ui/ui_element.h"

#include <string>
#include <string_view>

class SkFont;
class SkPaint;

namespace gitman::ui {
    // element 구현들이 공유하는 Skia 그리기 조각이다. canvas 상태를 저장·복원하지
    // 않으므로 호출자가 paint를 소유한다.
    [[nodiscard]] SkPaint solid_paint(ui_color color);

    // Skia로 가는 모든 글은 이 관문을 지난다
    // (text-input-and-font-fallback-design E3.2). 잘못된 UTF-8을 Skia에 넘기면
    // `SkFont::countText`가 -1을 돌려주고 그 값이 `AutoSTArray::reset`의
    // `SkASSERT(count >= 0)`에 걸려 **프로세스가 죽는다**. 밖에서 오는 글(프로세스
    // 출력·경로·설정 파일·클립보드)은 언제든 깨질 수 있으므로 근본 원인을 고쳐도
    // 관문은 남긴다.
    //
    // 온전한 글이면 `text`를 그대로 돌려주고 `storage`를 건드리지 않는다 — 위반이
    // 없으면 할당도 복사도 없다.
    [[nodiscard]] std::u8string_view drawable_text(std::u8string_view text, std::u8string& storage);

    void draw_text(SkCanvas& canvas, std::u8string_view text, float x, float y, const SkFont& font, const SkPaint& paint);

    // 같은 색의 투명도만 바꾼 값이다. chip 배경처럼 강조색을 옅게 깔 때 쓴다.
    [[nodiscard]] ui_color with_alpha(ui_color color, float alpha) noexcept;

    [[nodiscard]] float measure_text(std::u8string_view text, const SkFont& font);

    // `max_width` 안에 들어가도록 뒤를 잘라 `…`를 붙인 문자열이다. `…`조차 들어가지
    // 않으면 빈 값이다. 자르는 위치는 UTF-8 문자 경계다.
    [[nodiscard]] std::u8string elide_text(std::u8string_view text, float max_width, const SkFont& font);

    // `max_width` 안에서 잘라 그린다. 좁은 창에서 글자가 옆 UI를 침범하지 않게 하는
    // 공통 경로다. 반환값은 실제로 그린 폭이다.
    float draw_text_within(SkCanvas& canvas, std::u8string_view text, float x, float y, float max_width, const SkFont& font, const SkPaint& paint);

    // `area` 위쪽 경계에서 아래로 옅어지는 그림자다. 스크롤된 내용이 상단 막대
    // 아래로 지나간다는 것을 보여 준다. 셰이더 없이 알파를 낮춘 띠를 쌓는다.
    void draw_downward_shadow(SkCanvas& canvas, const rect_f& area, ui_color color, float strength);

    // target 중앙에 글리프 하나를 그린다. 글리프가 없으면 아무것도 그리지 않는다.
    void draw_centered_glyph(SkCanvas& canvas, char32_t codepoint, const rect_f& target, const SkFont& font, const SkPaint& paint);

    // target_height 안에서 세로 중앙 정렬된 텍스트 baseline을 돌려준다.
    [[nodiscard]] float centered_text_baseline(const SkFont& font, float target_height);

    // status_presentation의 Codicon 이름을 embedded 글리프로 옮긴다. 모르는 이름은
    // question으로 표시해 침묵하지 않는다.
    [[nodiscard]] char32_t codicon_for_name(std::u8string_view name) noexcept;
} // namespace gitman::ui
