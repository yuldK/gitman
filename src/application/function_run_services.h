#pragma once

#include "application/process_request.h"
#include "domain/function_button.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace gitman {
    // 치환과 검사(허용 목록, cmd 방어, 절대 경로)를 마친 실행 값이다. logic이 클릭 시점에
    // 만든다. 요청 조립 쪽은 다시 치환하거나 허용 목록을 보지 않는다.
    struct function_command
    {
        std::u8string program {};
        std::vector<std::u8string> arguments {};
        std::u8string working_directory {};
        // `program`이 `cmd.exe`일 때 logic이 지어 사용자가 승인한 최종 명령줄이다 (4.3, D30). 요청의 `command_line`이
        // 된다. cmd가 아니면 비어 있다.
        std::u8string command_line {};
    };

    // `${env:NAME}`의 값이다. 정의되지 않았으면 값이 없다. 클릭 시점의 gitman 프로세스
    // 환경을 읽는다 (function-buttons-design 3.3).
    using function_environment_lookup = std::function<std::optional<std::u8string>(std::u8string_view)>;

    // 실행 모드에 맞는 요청을 만든다. 출력 수집은 `process_request`, 분리 실행은
    // `process_launch_request`이고 분리 실행은 `timeout_seconds`를 쓰지 않는다 (3.2).
    using function_command_factory = std::function<std::variant<process_request, process_launch_request>(function_command, function_run_mode, std::int32_t, std::span<const variable_binding>)>;

    // logic이 기능 실행을 조립할 때 부르는 아래 계층의 함수 묶음이다 (4.5). 명령줄 인용·
    // 비밀 가리기·요청 조립은 infrastructure에, 환경 변수 읽기는 platform에 있어 logic이
    // 직접 부르지 않는다. 조립하는 쪽(driver, test)이 채워 넘긴다. 넷 모두 비어 있으면 안 된다.
    struct function_run_services
    {
        command_line_joiner join {};
        secret_text_masker mask {};
        function_environment_lookup environment {};
        function_command_factory make_command {};
    };
}
