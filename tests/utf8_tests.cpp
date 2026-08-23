#include "domain/utf8_text.h"
#include "platform/win32/utf8.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <optional>
#include <string>

TEST_CASE("UTF-8 paths round-trip through UTF-16 without loss", "[utf8]")
{
    const std::u8string original = u8"E:\\작업 폴더\\확장 문자 😀\\repo";
    const auto wide = gitman::win32::utf8_to_utf16(original);
    REQUIRE(wide.value.has_value());

    const auto round_trip = gitman::win32::utf16_to_utf8(*wide.value);
    REQUIRE(round_trip.value.has_value());
    REQUIRE(*round_trip.value == original);
}

TEST_CASE("Invalid UTF input returns a structured error", "[utf8]")
{
    const std::u8string invalid_utf8 {
        static_cast<char8_t>(0xC3),
        static_cast<char8_t>(0x28),
    };
    const auto wide = gitman::win32::utf8_to_utf16(invalid_utf8);
    REQUIRE_FALSE(wide.value.has_value());
    REQUIRE(wide.error.has_value());
    REQUIRE(wide.error->kind == gitman::win32::utf_conversion_error_kind::invalid_input);

    const std::wstring invalid_utf16 { static_cast<wchar_t>(0xD800) };
    const auto narrow = gitman::win32::utf16_to_utf8(invalid_utf16);
    REQUIRE_FALSE(narrow.value.has_value());
    REQUIRE(narrow.error.has_value());
    REQUIRE(narrow.error->kind == gitman::win32::utf_conversion_error_kind::invalid_input);
}

namespace {
    // 잘못된 UTF-8을 만들려면 바이트를 직접 써야 한다. `u8'¸'` 같은 문자 상수는
    // MSVC가 U+00B8을 UTF-8로 인코딩해 두 바이트가 되어 버린다.
    std::u8string bytes(const std::initializer_list<int> values)
    {
        std::u8string result {};
        for (const int value : values)
            result.push_back(static_cast<char8_t>(value));
        return result;
    }
} // namespace

TEST_CASE("UTF-8 validation rejects what Skia rejects", "[utf8][domain]")
{
    REQUIRE(gitman::utf8_is_valid(u8"맑은 고딕 Cascadia 😀"));
    REQUIRE(gitman::utf8_is_valid({}));

    // 한국어 Windows의 GDI font manager가 돌려주던 `맑은 고딕`의 CP949 바이트다.
    // 이 한 줄이 Skia에서 프로세스를 죽였다 (설계 E0.1).
    REQUIRE_FALSE(gitman::utf8_is_valid(bytes({ 0xB8, 0xBC, 0xC0, 0xBA, 0x20, 0xB0, 0xED, 0xB5, 0xF1 })));

    // 잘린 이어짐, 이어짐 바이트로 시작, 과잉 인코딩, surrogate, 범위 초과.
    REQUIRE_FALSE(gitman::utf8_is_valid(bytes({ 0xED, 0x95 })));
    REQUIRE_FALSE(gitman::utf8_is_valid(bytes({ 0x95, 0x9C })));
    REQUIRE_FALSE(gitman::utf8_is_valid(bytes({ 0xC0, 0xAF })));
    REQUIRE_FALSE(gitman::utf8_is_valid(bytes({ 0xED, 0xA0, 0x80 })));
    REQUIRE_FALSE(gitman::utf8_is_valid(bytes({ 0xF5, 0x80, 0x80, 0x80 })));
}

TEST_CASE("UTF-8 repair keeps the readable parts", "[utf8][domain]")
{
    const std::u8string repaired { gitman::utf8_replace_invalid(bytes({ 0x41, 0xB8, 0xC0, 0x42 })) };

    std::u8string expected { u8"A" };
    expected += gitman::utf8_encode(gitman::utf8_replacement_character);
    expected += gitman::utf8_encode(gitman::utf8_replacement_character);
    expected += u8"B";

    REQUIRE(gitman::utf8_is_valid(repaired));
    REQUIRE(repaired == expected);
}

TEST_CASE("UTF-8 encoding covers the planes above BMP", "[utf8][domain]")
{
    REQUIRE(gitman::utf8_encode(U'A') == u8"A");
    REQUIRE(gitman::utf8_encode(U'한') == u8"한");
    // BMP 밖 문자다 (U+1F600). 세 바이트로는 담기지 않는다.
    REQUIRE(gitman::utf8_encode(U'😀') == u8"😀");
    REQUIRE(gitman::utf8_encode(U'😀').size() == 4u);

    // 잘못된 UTF-8을 만들지 않는 것이 계약이다.
    REQUIRE(gitman::utf8_encode(static_cast<char32_t>(0xD800u)).empty());
    REQUIRE(gitman::utf8_encode(static_cast<char32_t>(0x110000u)).empty());
}

TEST_CASE("UTF-8 decoding always advances", "[utf8][domain]")
{
    const std::u8string text { u8"가😀" };
    const gitman::utf8_decoded first { gitman::utf8_decode(text, 0) };
    REQUIRE(first.codepoint == U'가');
    REQUIRE(first.length == 3u);

    const gitman::utf8_decoded second { gitman::utf8_decode(text, first.length) };
    REQUIRE(second.codepoint == U'😀');
    REQUIRE(second.length == 4u);

    // 잘못된 자리에서 길이가 0이면 호출자가 제자리에 갇힌다.
    const gitman::utf8_decoded broken { gitman::utf8_decode(bytes({ 0xB8 }), 0) };
    REQUIRE(broken.codepoint == gitman::utf8_replacement_character);
    REQUIRE(broken.length == 1u);
}

TEST_CASE("Surrogate pairs from WM_CHAR merge into one codepoint", "[utf8][win32]")
{
    std::optional<char16_t> pending {};

    // BMP 안의 글자는 그대로 지나간다.
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, u'한') == U'한');
    REQUIRE(pending.has_value() == false);

    // U+1F600은 D83D DE00 두 번에 나눠 온다. 앞쪽만으로는 글자가 아니다.
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, static_cast<char16_t>(0xD83Du)).has_value() == false);
    REQUIRE(pending.has_value());
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, static_cast<char16_t>(0xDE00u)) == U'\U0001F600');
    REQUIRE(pending.has_value() == false);
}

TEST_CASE("Unpaired surrogates are dropped instead of becoming broken text", "[utf8][win32]")
{
    std::optional<char16_t> pending {};

    // 앞쪽 없이 온 뒤쪽은 버린다.
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, static_cast<char16_t>(0xDE00u)).has_value() == false);

    // 앞쪽을 기다리다가 평범한 글자가 오면 기다리던 조각을 버리고 그 글자를 낸다.
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, static_cast<char16_t>(0xD83Du)).has_value() == false);
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, u'A') == U'A');
    REQUIRE(pending.has_value() == false);

    // 앞쪽이 연달아 와도 마지막 것만 기다린다.
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, static_cast<char16_t>(0xD800u)).has_value() == false);
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, static_cast<char16_t>(0xD83Du)).has_value() == false);
    REQUIRE(gitman::win32::merge_utf16_code_unit(pending, static_cast<char16_t>(0xDE00u)) == U'\U0001F600');
}
