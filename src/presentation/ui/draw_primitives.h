#pragma once

#include "presentation/ui/ui_element.h"

#include <string>
#include <string_view>
#include <vector>

class SkFont;
class SkPaint;
class SkTypeface;

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

    // 고른 글꼴에 없는 글자를 대신 그릴 typeface를 찾는다
    // (text-input-and-font-fallback-design E3.3·E3.4). `Cascadia Code`에는 한글이
    // 없고 어느 코드 글꼴에도 이모지가 없다. 대체가 없으면 그 자리는 빈 네모가 된다.
    //
    // 해석은 platform이 하고 presentation은 이 interface만 안다. 업무 상태가 아니라
    // OS 자원 cache라 ADR-004의 단일 소유 규칙 밖이다 (`win32_fonts`의 registry와
    // 같은 성격이다).
    struct font_fallback
    {
        font_fallback() = default;
        font_fallback(const font_fallback&) = delete;
        font_fallback(font_fallback&&) = delete;
        font_fallback& operator=(const font_fallback&) = delete;
        font_fallback& operator=(font_fallback&&) = delete;
        virtual ~font_fallback() = default;

        // 찾지 못하면 nullptr다. 그리기와 입력 thread가 함께 부르므로 구현은
        // thread-safe해야 한다.
        [[nodiscard]] virtual SkTypeface* for_codepoint(char32_t codepoint) const = 0;
    };

    // platform이 시작할 때 한 번 등록하고 끝날 때 nullptr로 되돌린다. 등록하지 않으면
    // 대체 없이 기본 typeface로만 그린다 — test가 이 경로다.
    void set_font_fallback(const font_fallback* fallback) noexcept;

    // 이어 그릴 한 조각이다. `typeface`가 nullptr이면 기본 글꼴 그대로다.
    struct text_run
    {
        std::u8string_view text {};
        SkTypeface* typeface { nullptr };
    };

    // 기본 typeface에 **없는 글자만** 대체 typeface로 갈라낸다 (E3.3).
    //
    // 규칙은 하나다: 기본 글꼴이 그릴 수 있으면 기본 글꼴이다. 그래서 한글 뒤의
    // 영문이 다시 원래 글꼴로 돌아오고, 코드 글꼴에서 공백이 고정폭을 유지한다.
    //
    // 측정과 그리기가 **이 함수 하나를** 함께 써야 caret 자리와 글자가 어긋나지
    // 않는다. 경계를 test가 직접 확인할 수 있도록 노출한다.
    [[nodiscard]] std::vector<text_run> split_text_runs(std::u8string_view text, const SkFont& font);

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
