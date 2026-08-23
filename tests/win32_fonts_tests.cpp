#include "platform/win32/win32_fonts.h"

#include "domain/utf8_text.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {
    // 실제 시스템 대체를 그리기 계층에 붙인다. 앱에서는 `application_window`가 한다.
    class registry_fallback final : public gitman::ui::font_fallback
    {
    public:
        [[nodiscard]] SkTypeface* for_codepoint(const char32_t codepoint) const override
        {
            return gitman::win32::fallback_typeface(codepoint).get();
        }
    };

    class installed_fallback_scope
    {
    public:
        explicit installed_fallback_scope(const gitman::ui::font_fallback& fallback)
        {
            gitman::ui::set_font_fallback(&fallback);
        }

        installed_fallback_scope(const installed_fallback_scope&) = delete;
        installed_fallback_scope(installed_fallback_scope&&) = delete;
        installed_fallback_scope& operator=(const installed_fallback_scope&) = delete;
        installed_fallback_scope& operator=(installed_fallback_scope&&) = delete;

        ~installed_fallback_scope()
        {
            gitman::ui::set_font_fallback(nullptr);
        }
    };
} // namespace

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

// 대체가 실제로 해석되는지다 (E3.3). GDI font manager는 이 진입점이 언제나 nullptr라
// `Cascadia Code`에서 한글이 빈 네모가 되었다.
TEST_CASE("The system resolves a fallback typeface for Hangul and emoji", "[win32][fonts]")
{
    const sk_sp<SkTypeface> hangul { gitman::win32::fallback_typeface(U'한') };
    REQUIRE(hangul != nullptr);
    REQUIRE(hangul->unicharToGlyph(U'한') != 0);

    const sk_sp<SkTypeface> emoji { gitman::win32::fallback_typeface(U'\U0001F600') };
    REQUIRE(emoji != nullptr);
    REQUIRE(emoji->unicharToGlyph(static_cast<SkUnichar>(U'\U0001F600')) != 0);

    // 두 번째 조회는 cache에서 온다 — 같은 객체여야 한다.
    REQUIRE(gitman::win32::fallback_typeface(U'한').get() == hangul.get());
}

// 대체가 붙은 뒤 실제로 폭이 달라지는지다. 한글이 `.notdef`로 그려지고 있다면 폭이
// 그대로였을 것이다.
TEST_CASE("A code font without Hangul still measures Hangul through fallback", "[win32][fonts]")
{
    const sk_sp<SkTypeface> code { gitman::win32::family_typeface(u8"Cascadia Code") };
    if (code == nullptr)
        SKIP("Cascadia Code is not installed on this machine.");

    // 전제: 그 글꼴에는 한글 글리프가 없다.
    REQUIRE(code->unicharToGlyph(U'한') == 0);

    const registry_fallback fallback {};
    const installed_fallback_scope scope { fallback };

    const SkFont font { code, 12.0f };
    const std::vector<gitman::ui::text_run> runs { gitman::ui::split_text_runs(u8"code 한글", font) };
    REQUIRE(runs.size() == 2u);
    REQUIRE(runs[0].text == u8"code ");
    REQUIRE(runs[0].typeface == nullptr);
    REQUIRE(runs[1].text == u8"한글");
    REQUIRE(runs[1].typeface != nullptr);
    REQUIRE(runs[1].typeface->unicharToGlyph(U'한') != 0);
}
