#include "presentation/ui_theme.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("UI color themes provide semantic caption colors", "[theme]")
{
    const auto& dark { gitman::color_palette_for(gitman::color_theme::dark) };
    const auto& high_contrast { gitman::color_palette_for(gitman::color_theme::high_contrast) };

    REQUIRE(dark.caption.background != dark.window_background);
    REQUIRE(dark.caption.button_hover_background != dark.caption.close_button_hover_background);
    REQUIRE(high_contrast.caption.button_hover_background == high_contrast.caption.close_button_hover_background);
    REQUIRE(high_contrast.caption.button_hover_foreground != high_contrast.caption.button_hover_background);
}
