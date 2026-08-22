#include "platform/win32/win32_fonts.h"

#include "domain/utf8_text.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkFont.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// 설치된 글꼴 이름은 글꼴 선택 dialog에서 그대로 Skia로 그려진다. UTF-8이 아닌 이름이
// 하나라도 섞이면 `SkFont::countText`가 -1을 돌려주고 그 값이 `AutoSTArray::reset`의
// `SkASSERT(count >= 0)`에 걸려 **프로세스를 죽인다**
// (text-input-and-font-fallback-design E0.1). GDI font manager가 정확히 그랬다 —
// 한국어 Windows에서 `맑은 고딕`을 CP949 바이트로 돌려주었다.
TEST_CASE("Installed font family names are valid UTF-8", "[win32][fonts]")
{
    const std::vector<std::u8string> families { gitman::win32::installed_font_families() };
    REQUIRE_FALSE(families.empty());

    const SkFont font {};
    for (const std::u8string& family : families)
    {
        INFO("family byte size: " << family.size());
        REQUIRE(gitman::utf8_is_valid(family));
        // 이름 끝의 NUL은 빈 글리프 하나로 그려진다.
        REQUIRE(family.find(char8_t { 0 }) == std::u8string::npos);
        // 실제로 재 본다. 고쳐지기 전에는 여기에서 abort했다.
        static_cast<void>(gitman::ui::measure_text(family, font));
    }
}
