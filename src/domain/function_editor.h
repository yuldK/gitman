#pragma once

#include "domain/function_button.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 편집 dialog(function-buttons-design 2.7)의 인자 한 줄을 나눈 결과다.
    struct function_argument_split
    {
        std::vector<std::u8string> arguments {};
        // 닫히지 않은 큰따옴표가 있다. 칸 아래에 알리고 Apply를 막는다. 그때 `arguments`는 쓰지 않는다.
        bool unterminated_quote { false };

        [[nodiscard]] bool operator==(const function_argument_split&) const = default;
    };

    // 인자 한 줄을 `CommandLineToArgvW`의 인자 규칙으로 나눈다 (2.7). 공백·탭이 구분이고, 큰따옴표가 공백을 묶으며,
    // 큰따옴표 앞의 역슬래시는 2n개면 n개 + 따옴표 열고 닫기, 2n+1개면 n개 + 글자 `"`다. 따옴표 앞이 아닌 역슬래시는
    // 글자 그대로라 `C:\Program Files\x`를 따옴표로 감싸면 그대로 남는다. `""`는 빈 인자다. **먼저 나누고** 나눈 토큰
    // 안에서만 `${}`를 푼다 — 이 함수는 변수를 보지 않고, 저장은 배열이다.
    //  - 2.7의 세 규칙(따옴표, 역슬래시, 공백 경로) 밖의 한 가지는 따르지 않는다: 따옴표 안의 `""`를 이 함수는 따옴표를 닫고
    //    다시 여는 것으로 읽는다(`"a""b"` → `ab`). `CommandLineToArgvW`는 글자 `"` 하나를 넣고 따옴표를 닫는다(`a"b`, 실측).
    //    그 규칙이면 `"a""b"`의 마지막 따옴표가 열린 채 끝나 이 함수가 막는 "닫히지 않은 따옴표"와 부딪힌다. 편집기가
    //    잇는 글(`join_function_arguments`)은 `\"`만 쓰므로 왕복에는 영향이 없고, 손으로 `""`를 친 줄만 다르다.
    [[nodiscard]] function_argument_split split_function_arguments(std::u8string_view line);

    // 인자 배열을 칸에 보일 한 줄로 다시 잇는다. `split_function_arguments`가 같은 배열로 되돌리는 인용이다: 비었거나
    // 공백·탭·큰따옴표가 든 인자만 따옴표로 감싸고, 따옴표 앞과 끝의 역슬래시를 두 배로 한다.
    [[nodiscard]] std::u8string join_function_arguments(std::span<const std::u8string> arguments);

    // 편집 dialog의 `[+]`·복제가 새로 만드는 버튼 id다 (2.7에는 id 칸이 없다). `base`(비면 `function`) 뒤에 `-N`을 붙여
    // `taken`과 겹치지 않는 첫 이름이다. `taken`에는 초안의 id뿐 아니라 dialog를 열 때 문서에 있던 id와 실행 중인 기능의
    // id도 든다 — 지운 기능의 id를 새 버튼이 받으면 쓰기 함수가 지운 항목의 원문을 template으로 쓰고, 실행 중 거절 검사가
    // 새 버튼을 지운 기능으로 본다. `sources`는 검증에 실패해 원문만 든 항목이다 — 그 원문에 같은 id 글이 보이면
    // 피한다 (쓰기 함수의 template이 원문 항목의 알 수 없는 키를 새 버튼으로 옮기지 않게, 3.9).
    [[nodiscard]] std::u8string make_function_button_id(std::u8string_view base, std::span<const std::u8string> taken, std::span<const function_source_text> sources);
}
