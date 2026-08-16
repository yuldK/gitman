#include "platform/win32/win32_text_transcoder.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <memory>
#include <optional>
#include <string>

namespace {
    // 활성 code page는 호스트 설정에 따라 다르므로 단정을 code page별로 나눈다.
    constexpr unsigned int korean_code_page { 949 };

    std::u8string cp949_korean_bytes()
    {
        // CP949로 인코딩한 `한글`이다.
        return std::u8string { char8_t { 0xC7 }, char8_t { 0xD1 }, char8_t { 0xB1 }, char8_t { 0xDB } };
    }
} // namespace

TEST_CASE("The active code page transcoder handles empty and ASCII input", "[win32][transcoder]")
{
    const std::unique_ptr<gitman::text_transcoder> transcoder { gitman::win32::make_active_code_page_transcoder() };
    REQUIRE(transcoder != nullptr);

    const std::optional<std::u8string> empty { transcoder->to_utf8(u8"") };
    REQUIRE(empty.has_value());
    REQUIRE(empty->empty());

    // ASCII는 어떤 활성 code page에서도 같은 byte로 해석된다.
    const std::optional<std::u8string> ascii { transcoder->to_utf8(u8"fatal: not a git repository") };
    REQUIRE(ascii.has_value());
    REQUIRE(*ascii == u8"fatal: not a git repository");
}

TEST_CASE("The active code page transcoder rejects undecodable bytes", "[win32][transcoder]")
{
    const std::unique_ptr<gitman::text_transcoder> transcoder { gitman::win32::make_active_code_page_transcoder() };

    // `0xFF`는 CP949와 UTF-8 어디에서도 단독으로 유효하지 않다.
    const std::optional<std::u8string> undecodable { transcoder->to_utf8(std::u8string { char8_t { 0xFF } }) };
    REQUIRE_FALSE(undecodable.has_value());
}

TEST_CASE("The active code page transcoder recovers text on a Korean host", "[win32][transcoder]")
{
    if (GetACP() != korean_code_page)
    {
        WARN("활성 code page가 949가 아니어서 CP949 복원 단정을 건너뛴다.");
        return;
    }

    const std::unique_ptr<gitman::text_transcoder> transcoder { gitman::win32::make_active_code_page_transcoder() };
    const std::optional<std::u8string> recovered { transcoder->to_utf8(cp949_korean_bytes()) };
    REQUIRE(recovered.has_value());
    REQUIRE(*recovered == u8"한글");

    // 같은 문자열의 UTF-8 byte는 CP949 sequence로 성립하지 않는다. 엄격 변환이
    // 실패하므로 파이프라인은 U+FFFD 경로로 되돌아간다. 유효한 UTF-8을 먼저 확인해
    // 변환 자체를 건너뛰는 순서가 그래서 필요하다.
    const std::optional<std::u8string> misread { transcoder->to_utf8(u8"한글") };
    REQUIRE_FALSE(misread.has_value());

    // ASCII만 있는 UTF-8 문자열은 CP949에서도 같은 값으로 해석된다.
    const std::optional<std::u8string> ascii_only { transcoder->to_utf8(u8"branch main") };
    REQUIRE(ascii_only.has_value());
    REQUIRE(*ascii_only == u8"branch main");
}
