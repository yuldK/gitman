#pragma once

#include "presentation/caption_ui.h"
#include "presentation/renderer_policy.h"
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
        caption_button_hover hovered_caption_button { caption_button_hover::none };
    };

    void draw_skia_smoke_view(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const smoke_view_state& state);
} // namespace gitman
