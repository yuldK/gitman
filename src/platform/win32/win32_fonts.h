#pragma once

#include "include/core/SkRefCnt.h"

#include <string>
#include <string_view>
#include <vector>

class SkTypeface;

namespace gitman::win32 {
    // 시스템에 설치된 글꼴 가족 이름을 이름 순으로 돌려준다
    // (fonts-and-in-app-dialogs-design F2.1). GDI font manager를 쓰며, 만들지
    // 못하면 빈 목록이다. OS 호출이라 UI thread에서 한 번만 부른다.
    [[nodiscard]] std::vector<std::u8string> installed_font_families();

    // 설정이 고른 가족을 반영한다. 빈 이름은 내장 글꼴을 뜻하고, 목록에 없는 이름도
    // 조용히 내장 글꼴로 되돌아간다. 값이 그대로면 아무 일도 하지 않으므로 매 frame
    // 불러도 된다 (UI thread).
    void set_configured_fonts(std::u8string_view ui_family, std::u8string_view code_family);

    // 현재 설정된 글꼴이다. 그리기 thread와 입력 thread가 같은 값을 봐야 caret 자리와
    // 글자 그림이 어긋나지 않으므로 registry를 공유한다 (F2.2). 업무 상태가 아니라
    // OS 자원 cache라 ADR-004의 단일 소유 규칙 밖이다.
    [[nodiscard]] sk_sp<SkTypeface> configured_ui_typeface();
    [[nodiscard]] sk_sp<SkTypeface> configured_code_typeface();

    // 미리 보기용이다. 빈 이름이면 내장 글꼴이고, 찾지 못하면 nullptr다.
    [[nodiscard]] sk_sp<SkTypeface> family_typeface(std::u8string_view family);
} // namespace gitman::win32
