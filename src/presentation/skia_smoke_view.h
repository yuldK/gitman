#pragma once

#include "presentation/renderer_policy.h"
#include "presentation/ui/ui_element.h"
#include "presentation/ui/ui_tree.h"
#include "presentation/ui_theme.h"

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

        // input thread가 게시한 상호작용 상태다. caption의 비클라이언트 hover는
        // UI thread가 게시 전에 합쳐 둔다.
        ui::interaction_snapshot interaction {};

        // 값이 있으면 smoke 화면 대신 단계 6의 카드 목록 tree를 그린다. 포인터는
        // 렌더 호출 동안만 유효하면 된다.
        const ui::ui_tree* application_tree { nullptr };
    };

    void draw_skia_smoke_view(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const smoke_view_state& state);
} // namespace gitman
