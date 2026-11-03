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

    // `cmd.exe` 전용 명령줄이다 (function-buttons-design 4.3, D27, D30). 규칙은 domain의 `assemble_cmd_command_line`이다.
    // 인자의 출처를 모르므로 모두 값으로 보고(명령 문장으로 넘기지 않는다) 보정 목록과 경고는 버린다. 기능 버튼은 logic이
    // 사용자가 승인한 최종 명령줄을 요청의 `command_line`에 싣고 runner·launcher가 그 글을 그대로 쓴다.
    [[nodiscard]] std::u8string build_cmd_command_line(std::u8string_view executable, std::span<const std::u8string> arguments);
    // 요청의 `syntax`에 맞는 명령줄이다. runner·launcher가 요청에 미리 지은 `command_line`이 없을 때 쓴다. 기능 버튼의
    // 표시용 명령줄(3.6)은 argv면 이 함수, cmd면 같은 domain 조립의 표시용 글이다.
    [[nodiscard]] std::u8string build_process_command_line(std::u8string_view executable, std::span<const std::u8string> arguments, command_line_syntax syntax);
} // namespace gitman
