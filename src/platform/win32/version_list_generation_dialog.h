#pragma once

#include "application/app_messages.h"
#include "presentation/ui_theme.h"

#include <windows.h>

#include <optional>

namespace gitman::win32 {
    // `.version-list` 생성 입력을 받는 모달 팝업이다. UI thread 전용이며, 사용자가
    // 취소하면 값이 없다. 반환된 intent의 경로 검증은 dialog가 어휘 규칙과 존재
    // 여부까지 확인하지만, 최종 검증과 원자적 생성은 worker의 생성 service가 한다.
    // `theme`과 `accent`는 본 창과 같은 팔레트를 쓰기 위한 값이다
    // (theme-and-banner-menu-design T3.2). 라이트 테마에서는 시스템 caption도
    // 밝게 칠한다.
    [[nodiscard]] std::optional<generate_document_intent> show_version_list_generation_dialog(HWND owner, color_theme theme, const accent_definition& accent);
} // namespace gitman::win32
