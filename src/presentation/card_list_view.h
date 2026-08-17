#pragma once

#include "presentation/layout_model.h"
#include "presentation/ui_theme.h"
#include "presentation/view_snapshot.h"

class SkCanvas;
class SkTypeface;

namespace gitman {
    // 단계 6의 카드 목록 화면이다. view snapshot과 layout snapshot만 읽어 그린다
    // (ADR-004). caption은 기존 caption_ui가 그리며 이 함수는 그 아래 영역을 담당한다.
    void draw_card_list(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const view_snapshot& view, const layout_snapshot& layout, color_theme theme);
} // namespace gitman
