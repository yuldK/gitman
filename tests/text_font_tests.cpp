#include "presentation/ui/ui_common.h"

#include "luil/text/fonts.h"
#include "luil/theme/ui_theme.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {
    namespace ui = gitman::ui;

    // 시스템 글꼴 폴더의 파일을 읽은 face다. 파일로 읽은 face는 luil이 같은 이름의 시스템
    // 가족을 찾지 않으므로, 굵기 요청은 덧칠로 메워진다 (luil font-style-design 규칙 1·3).
    sk_sp<SkTypeface> file_face(const char* const name)
    {
        return luil::load_typeface_file(std::filesystem::path { "C:/Windows/Fonts" } / name);
    }

    // 1×1 그림 위의 그리기 문맥이다. 글꼴 도우미는 문맥의 typeface와 배율만 읽는다.
    struct font_fixture
    {
        SkBitmap bitmap {};
        sk_sp<SkTypeface> ui_face { file_face("segoeui.ttf") };
        sk_sp<SkTypeface> code_face { file_face("consola.ttf") };
        sk_sp<SkTypeface> icon_face { file_face("segmdl2.ttf") };
        luil::ui_color_palette palette { luil::color_palette_for(luil::color_theme::dark) };

        font_fixture()
        {
            bitmap.allocN32Pixels(1, 1);
        }
    };
} // namespace

TEST_CASE("Font helpers size text by the display scale and pick the face by role", "[ui][fonts]")
{
    font_fixture fixture {};
    REQUIRE(fixture.ui_face != nullptr);
    REQUIRE(fixture.code_face != nullptr);
    REQUIRE(fixture.icon_face != nullptr);
    SkCanvas canvas { fixture.bitmap };
    const luil::draw_context context {
        .canvas = canvas,
        .codicon_typeface = fixture.icon_face.get(),
        .ui_typeface = fixture.ui_face.get(),
        .code_typeface = fixture.code_face.get(),
        .palette = fixture.palette,
        .scale = 1.5f,
    };

    // Windows의 글자 크기는 논리 크기 × 화면 배율이다. 손으로 짓던 `size * scale`과 같다.
    const SkFont body { ui::ui_font(context, 11.0f) };
    REQUIRE(body.getTypeface() == fixture.ui_face.get());
    REQUIRE(body.getSize() == 11.0f * 1.5f);
    REQUIRE_FALSE(body.isEmbolden());

    const SkFont code { ui::code_font(context, 10.5f) };
    REQUIRE(code.getTypeface() == fixture.code_face.get());
    REQUIRE(code.getSize() == 10.5f * 1.5f);

    const SkFont icon { ui::icon_font(context, 14.0f) };
    REQUIRE(icon.getTypeface() == fixture.icon_face.get());
    REQUIRE(icon.getSize() == 14.0f * 1.5f);

    // 굵은 글은 크기가 같고 굵기만 다르다. face를 찾지 못한 파일 face는 덧칠로 메운다.
    const SkFont bold { ui::ui_font(context, 11.0f, luil::font_weight::bold) };
    REQUIRE(bold.getSize() == body.getSize());
    REQUIRE((bold.isEmbolden() || bold.getTypeface() != fixture.ui_face.get()));
}

TEST_CASE("The code font falls back to the UI font when no code face is set", "[ui][fonts]")
{
    font_fixture fixture {};
    REQUIRE(fixture.ui_face != nullptr);
    SkCanvas canvas { fixture.bitmap };
    const luil::draw_context context { .canvas = canvas, .ui_typeface = fixture.ui_face.get(), .palette = fixture.palette };

    REQUIRE(ui::code_font(context, 11.0f).getTypeface() == fixture.ui_face.get());
}
