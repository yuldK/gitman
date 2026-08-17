#pragma once

#include "presentation/caption_ui.h"
#include "presentation/layout_model.h"
#include "presentation/renderer_policy.h"
#include "presentation/ui_theme.h"
#include "presentation/view_snapshot.h"

class SkCanvas;
class SkTypeface;

namespace gitman {
    struct smoke_view_state
    {
        int width { 0 };
        int height { 0 };
        float dpi_scale { 1.0F };
        renderer_backend backend { renderer_backend::cpu };
        bool used_fallback { false };
        color_theme theme { color_theme::dark };
        bool maximized { false };
        caption_button_hover hovered_caption_button { caption_button_hover::none };

        // 값이 있으면 smoke 화면 대신 단계 6의 카드 목록 화면을 그린다. 두 포인터는
        // 렌더 호출 동안만 유효하면 된다.
        const view_snapshot* application_view { nullptr };
        const layout_snapshot* application_layout { nullptr };
    };

    void draw_skia_smoke_view(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const smoke_view_state& state);
} // namespace gitman
