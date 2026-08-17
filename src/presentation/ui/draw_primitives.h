#pragma once

#include "presentation/ui/ui_element.h"

#include <string_view>

class SkFont;
class SkPaint;

namespace gitman::ui {
    // element 구현들이 공유하는 Skia 그리기 조각이다. canvas 상태를 저장·복원하지
    // 않으므로 호출자가 paint를 소유한다.
    [[nodiscard]] SkPaint solid_paint(ui_color color);

    void draw_text(SkCanvas& canvas, std::u8string_view text, float x, float y, const SkFont& font, const SkPaint& paint);

    // target 중앙에 글리프 하나를 그린다. 글리프가 없으면 아무것도 그리지 않는다.
    void draw_centered_glyph(SkCanvas& canvas, char32_t codepoint, const rect_f& target, const SkFont& font, const SkPaint& paint);

    // target_height 안에서 세로 중앙 정렬된 텍스트 baseline을 돌려준다.
    [[nodiscard]] float centered_text_baseline(const SkFont& font, float target_height);

    // status_presentation의 Codicon 이름을 embedded 글리프로 옮긴다. 모르는 이름은
    // question으로 표시해 침묵하지 않는다.
    [[nodiscard]] char32_t codicon_for_name(std::u8string_view name) noexcept;
} // namespace gitman::ui
