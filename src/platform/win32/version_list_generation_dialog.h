#pragma once

#include "application/app_messages.h"

#include <windows.h>

#include <optional>

namespace gitman::win32 {
    // `.version-list` 생성 입력을 받는 모달 팝업이다. UI thread 전용이며, 사용자가
    // 취소하면 값이 없다. 반환된 intent의 경로 검증은 dialog가 어휘 규칙과 존재
    // 여부까지 확인하지만, 최종 검증과 원자적 생성은 worker의 생성 service가 한다.
    [[nodiscard]] std::optional<generate_document_intent> show_version_list_generation_dialog(HWND owner);
} // namespace gitman::win32
