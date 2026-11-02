#pragma once

#include "application/process_request.h"
#include "domain/function_button.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gitman {
    // 기능 버튼 출력 수집 실행의 스트림당 캡처 상한이다 (4.1). VCS 정책과 따로 둔다.
    // 0은 요청 거부라 무제한을 뜻할 수 없다 (`process_request.cpp`).
    inline constexpr std::size_t function_capture_bytes_per_stream { 8u * 1024u * 1024u };

    // 치환과 검사(허용 목록, cmd 방어, 절대 경로)를 마친 실행 값이다. logic이 클릭 시점에
    // 만든다. 여기서는 다시 치환하거나 허용 목록을 보지 않는다.
    struct function_command
    {
        std::u8string program {};
        std::vector<std::u8string> arguments {};
        std::u8string working_directory {};
    };

    // `timeout_seconds`를 요청 timeout으로 옮긴다. 0은 무제한이라 값이 없다 (D18).
    // 0 ms 요청은 거부되기 때문이다. 파서가 0~86400만 받으므로 음수는 오지 않지만 무제한으로 본다.
    [[nodiscard]] std::optional<std::chrono::milliseconds> function_timeout(std::int32_t timeout_seconds) noexcept;

    // 내장 변수를 `GITMAN_*` 고정 이름으로 내보낸다 (3.3, D14). `svnUrl`은 내보내지 않는다.
    // 값이 없는 변수는 부모 환경에서 지워, gitman이 물려받은 같은 이름의 낡은 값이 자식에게
    // 가지 않게 한다. `capture`에만 `GIT_TERMINAL_PROMPT=0`을 더한다 (D9).
    [[nodiscard]] std::vector<process_environment_override> function_environment_overrides(std::span<const variable_binding> builtins, function_run_mode mode);

    // 출력 수집 실행 요청이다. VCS 정책(`make_vcs_process_request`)의 인자와 환경을 쓰지 않는다 (4.1).
    [[nodiscard]] process_request make_function_process_request(function_command command, std::int32_t timeout_seconds, std::span<const variable_binding> builtins);
    // 분리 실행 요청이다. timeout이 없고 `GIT_TERMINAL_PROMPT`를 싣지 않는다.
    [[nodiscard]] process_launch_request make_function_launch_request(function_command command, std::span<const variable_binding> builtins);
} // namespace gitman
