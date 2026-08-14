#include "platform/win32/caption_layout.h"
#include "presentation/caption_ui.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Caption button bounds scale with DPI", "[caption]")
{
    const auto layout = gitman::win32::make_caption_layout(1000, 96);
    REQUIRE(layout.height == gitman::default_caption_ui_metrics.height);
    REQUIRE(layout.button_width == gitman::default_caption_ui_metrics.button_width);
    REQUIRE(layout.close_left == 954);
    REQUIRE(layout.maximize_left == 908);
    REQUIRE(layout.minimize_left == 862);

    const auto scaled = gitman::win32::make_caption_layout(1500, 144);
    REQUIRE(scaled.height == 60);
    REQUIRE(scaled.button_width == 69);
}

TEST_CASE("Caption hit testing distinguishes drag and system buttons", "[caption]")
{
    const auto layout = gitman::win32::make_caption_layout(1000, 96);
    REQUIRE(gitman::win32::hit_test_caption(layout, 100, 20) == gitman::win32::caption_hit::drag);
    REQUIRE(gitman::win32::hit_test_caption(layout, 880, 20) == gitman::win32::caption_hit::minimize);
    REQUIRE(gitman::win32::hit_test_caption(layout, 930, 20) == gitman::win32::caption_hit::maximize);
    REQUIRE(gitman::win32::hit_test_caption(layout, 980, 20) == gitman::win32::caption_hit::close);
    REQUIRE(gitman::win32::hit_test_caption(layout, 100, 60) == gitman::win32::caption_hit::client);
}
