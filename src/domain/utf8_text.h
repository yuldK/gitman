#pragma once

#include <string>
#include <string_view>

namespace gitman {
    // UTF-8 바이트열을 다루는 순수 도구다 (text-input-and-font-fallback-design E3.2).
    // Skia는 잘못된 UTF-8을 받으면 `SkFont::countText`가 -1을 돌려주고 그 값이
    // `AutoSTArray::reset`의 `SkASSERT(count >= 0)`에 걸려 **프로세스를 죽인다**.
    // 밖에서 오는 글(프로세스 출력·경로·설정 파일·클립보드)은 언제든 깨질 수 있으므로
    // 그리기 앞에 이 관문을 둔다.

    // 잘못된 UTF-8 대신 그리는 글자다. 무엇이 깨졌는지 눈에 보인다.
    inline constexpr char32_t utf8_replacement_character { 0xFFFDu };

    // 전부 올바른 UTF-8인지다. 과잉 인코딩(overlong), surrogate 코드포인트,
    // `U+10FFFF` 초과, 잘린 이어짐 바이트를 모두 잘못된 것으로 본다.
    [[nodiscard]] bool utf8_is_valid(std::u8string_view text) noexcept;

    // 잘못된 바이트를 `U+FFFD` 하나씩으로 바꾼 사본이다. 온전한 글에는 쓰지 않는다
    // (호출 전에 `utf8_is_valid`로 거른다).
    [[nodiscard]] std::u8string utf8_replace_invalid(std::u8string_view text);

    // 코드포인트 하나를 UTF-8 조각으로 만든다. surrogate와 `U+10FFFF` 초과는 빈
    // 값이다 — 잘못된 UTF-8을 **만들지 않는 것**이 이 함수의 계약이다.
    [[nodiscard]] std::u8string utf8_encode(char32_t codepoint);

    // `offset`이 가리키는 자리의 코드포인트와 그 길이다. 잘못된 자리면 `U+FFFD`와
    // 길이 1을 돌려준다 — 호출자가 무한히 제자리에 머물지 않는다.
    struct utf8_decoded
    {
        char32_t codepoint { utf8_replacement_character };
        std::size_t length { 1 };
    };

    [[nodiscard]] utf8_decoded utf8_decode(std::u8string_view text, std::size_t offset) noexcept;
} // namespace gitman
