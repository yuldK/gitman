#include "platform/win32/utf8.h"

#include <catch2/catch_test_macros.hpp>

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
