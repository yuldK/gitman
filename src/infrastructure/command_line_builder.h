#pragma once

#include "domain/process_execution.h"

#include <span>
#include <string>
#include <string_view>

namespace gitman {
    // Win32는 인자 배열을 직접 받는 프로세스 시작 API가 없으므로 `CommandLineToArgvW`
    // 규칙에 맞는 명령줄 하나를 만든다. 셸을 거치지 않기 때문에 `&`, `|`, `^` 같은
    // 문자는 특별한 의미 없이 인자 값의 일부로 전달된다.
    [[nodiscard]] std::u8string build_windows_command_line(std::u8string_view executable, std::span<const std::u8string> arguments);
    // 하나의 인자를 위 규칙으로 인용해 덧붙인다. 명령줄 조립 test와 재사용을 위해 노출한다.
    void append_windows_command_line_argument(std::u8string& target, std::u8string_view argument);

    // `cmd.exe` 전용 명령줄이다 (function-buttons-design 4.3, D27). `cmd /c`는 받은 명령줄을 자기 규칙으로 다시
    // 해석하므로 인자 배열의 경계와 값을 지키려면 따로 지어야 한다.
    // - argv[0]은 `/`를 `\`로 바꿔 감싼다. cmd는 argv[0] 안의 `/c`도 스위치로 읽는다.
    // - 앞쪽의 스위치(`/d`, `/v:on` 꼴)는 그대로 둔다. `/c`·`/k`·`/r`이 스위치 줄을 끝내고, 스위치 꼴이 아닌 첫
    //   인자에서 `/c` 없이 명령이 시작되면 `/c`를 넣는다. 그 앞에 `/s`가 없으면 넣는다(`/c` 뒤의 `/s`는 명령이다).
    //   `/c:x`·`/k:1`·`/r:x`는 스위치가 아니라 명령의 시작이다.
    // - 명령 인자는 cmd 메타 문자(`& | < > ^ ( )`), 배치 인자를 가르는 문자(`, ; =`, 공백·탭), `%`·`!`가 있거나
    //   비었을 때만 따옴표로 감싸 공백으로 잇고, 전체를 `"…"` 한 묶음으로 둔다. `/s`라서 cmd는 맨 앞과 맨 끝
    //   따옴표만 벗긴다. 따옴표 안의 메타 문자는 cmd가 해석하지 않고, 배치의 `%*`로 다시 넘겨도 따옴표째 간다.
    // - 감싼 토큰 끝의 `\`는 두 배로 늘린다(`CommandLineToArgvW`). `cmd_uncorrectable_characters`는 보정하지
    //   않는다. 부르는 쪽이 거부한다(표시용 미리보기는 그대로 보인다).
    [[nodiscard]] std::u8string build_cmd_command_line(std::u8string_view executable, std::span<const std::u8string> arguments);
    // 요청의 `syntax`에 맞는 명령줄이다. runner·launcher와 표시용 명령줄(3.6)이 같은 함수를 쓴다.
    [[nodiscard]] std::u8string build_process_command_line(std::u8string_view executable, std::span<const std::u8string> arguments, command_line_syntax syntax);
} // namespace gitman
