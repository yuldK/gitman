#include "presentation/ui_theme.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

namespace {
    // 불투명(알파 255)인지다. 팔레트 색은 그리는 쪽이 알파를 낮춰 쓰므로 표의
    // 값 자체는 항상 불투명해야 한다.
    bool opaque(const gitman::ui_color value) noexcept
    {
        return (value >> 24U) == 0xFFU;
    }
} // namespace

TEST_CASE("UI color themes provide semantic caption colors", "[theme]")
{
    const auto dark { gitman::color_palette_for(gitman::color_theme::dark) };
    const auto high_contrast { gitman::color_palette_for(gitman::color_theme::high_contrast) };

    REQUIRE(dark.caption.background != dark.window_background);
    REQUIRE(dark.caption.button_hover_background != dark.caption.close_button_hover_background);
    REQUIRE(high_contrast.caption.button_hover_background == high_contrast.caption.close_button_hover_background);
    REQUIRE(high_contrast.caption.button_hover_foreground != high_contrast.caption.button_hover_background);
}

TEST_CASE("The built-in accent catalog is well formed", "[theme][accent]")
{
    const auto catalog { gitman::accent_catalog() };
    REQUIRE(catalog.empty() == false);

    std::set<std::u8string> identifiers {};
    for (const gitman::accent_definition& accent : catalog)
    {
        REQUIRE(accent.id.empty() == false);
        REQUIRE(accent.label.empty() == false);
        // id는 유일해야 고른 값을 되찾을 수 있다.
        REQUIRE(identifiers.insert(std::u8string { accent.id }).second);

        REQUIRE(opaque(accent.swatch));
        for (const gitman::accent_color_set& colors : { accent.dark, accent.light })
        {
            REQUIRE(opaque(colors.accent));
            REQUIRE(opaque(colors.hover));
            REQUIRE(opaque(colors.soft));
            REQUIRE(opaque(colors.emphasis_foreground));
            // 역할이 전부 같은 값이면 hover·강조가 눈에 띄지 않는다.
            REQUIRE(colors.accent != colors.hover);
        }
    }

    // 기본 색은 물러설 곳이라 반드시 있어야 한다.
    REQUIRE(gitman::accent_exists(gitman::default_accent_id));
    REQUIRE(gitman::accent_exists(u8"blue"));
    REQUIRE(gitman::accent_exists(u8"없는색") == false);
}

TEST_CASE("Unknown accent ids fall back to the default", "[theme][accent]")
{
    REQUIRE(gitman::accent_for(u8"blue").id == u8"blue");
    REQUIRE(gitman::accent_for(u8"없는색").id == gitman::default_accent_id);
    REQUIRE(gitman::accent_for(u8"").id == gitman::default_accent_id);
}

TEST_CASE("Palettes compose the chosen accent, and high contrast ignores it", "[theme][accent]")
{
    const gitman::accent_definition& blue { gitman::accent_for(u8"blue") };
    const auto dark { gitman::color_palette_for(gitman::color_theme::dark, blue) };

    REQUIRE(dark.accent == blue.dark.accent);
    REQUIRE(dark.accent_hover == blue.dark.hover);
    REQUIRE(dark.accent_soft == blue.dark.soft);
    REQUIRE(dark.accent_emphasis_foreground == blue.dark.emphasis_foreground);
    // 중립 색은 키 컬러와 무관하게 같다.
    REQUIRE(dark.window_background == gitman::color_palette_for(gitman::color_theme::dark).window_background);
    REQUIRE(dark.accent != gitman::color_palette_for(gitman::color_theme::dark).accent);

    // 고대비는 가독성이 우선이라 키 컬러를 쓰지 않는다.
    const auto high_contrast { gitman::color_palette_for(gitman::color_theme::high_contrast, blue) };
    REQUIRE(high_contrast.accent == high_contrast.primary_foreground);
    REQUIRE(high_contrast.accent_soft == high_contrast.primary_foreground);
    REQUIRE(high_contrast.accent_emphasis_foreground == high_contrast.primary_foreground);
}
