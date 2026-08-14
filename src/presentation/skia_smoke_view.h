#pragma once

#include "presentation/renderer_policy.h"

class SkCanvas;
class SkTypeface;

namespace gitman {
    enum class caption_button_hover
    {
        none,
        minimize,
        maximize,
        close,
    };

    struct smoke_view_state
    {
        int width { 0 };
        int height { 0 };
        float dpi_scale { 1.0F };
        renderer_backend backend { renderer_backend::cpu };
        bool used_fallback { false };
        bool high_contrast { false };
        bool maximized { false };
        caption_button_hover hovered_caption_button { caption_button_hover::none };
    };

    void draw_skia_smoke_view(SkCanvas& canvas, SkTypeface* codicon_typeface, SkTypeface* ui_typeface, const smoke_view_state& state);
} // namespace gitman
