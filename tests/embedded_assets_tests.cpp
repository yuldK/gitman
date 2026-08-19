#include "platform/win32/embedded_assets.h"
#include "platform/win32/resources/resource_ids.h"

#include "gitman/generated/codicons.h"

#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

TEST_CASE("Executable resources contain the Codicons font and licenses", "[assets]")
{
    std::u8string error;
    REQUIRE(gitman::win32::verify_embedded_resources(error));

    const auto typeface = gitman::win32::load_codicon_typeface();
    REQUIRE(typeface != nullptr);
    constexpr std::array required_glyphs {
        gitman::codicons::icon_source_control,
        gitman::codicons::icon_chrome_minimize,
        gitman::codicons::icon_chrome_restore,
        gitman::codicons::icon_chrome_maximize,
        gitman::codicons::icon_chrome_close,
    };
    for (const char32_t codepoint : required_glyphs)
        REQUIRE(typeface->unicharToGlyph(static_cast<SkUnichar>(codepoint)) != 0);

    const auto notice = gitman::win32::find_embedded_resource(IDR_THIRD_PARTY_NOTICES);
    REQUIRE(notice.data != nullptr);
    const std::string_view notice_text(static_cast<const char*>(notice.data), notice.size);
    // 고지는 실행 파일에 실제로 포함되는 구성 요소만 담는다 (ADR-006). Catch2는
    // test 실행 파일에만 링크되므로 대상이 아니다.
    REQUIRE(notice_text.find("Component: Skia") != std::string_view::npos);
    REQUIRE(notice_text.find("Component: nlohmann/json") != std::string_view::npos);
    REQUIRE(notice_text.find("Component: SPIRV-Cross") != std::string_view::npos);
    REQUIRE(notice_text.find("Codicons") != std::string_view::npos);
}
