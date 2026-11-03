#pragma once

#include "application/function_run_services.h"
#include "application/process_request.h"
#include "domain/function_button.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace gitman {
    // 기능 버튼 출력 수집 실행의 스트림당 캡처 상한이다 (4.1). VCS 정책과 따로 둔다.
    // 0은 요청 거부라 무제한을 뜻할 수 없다 (`process_request.cpp`).
    inline constexpr std::size_t function_capture_bytes_per_stream { 8u * 1024u * 1024u };

    // `timeout_seconds`를 요청 timeout으로 옮긴다. 0은 무제한이라 값이 없다 (D18).
    // 0 ms 요청은 거부되기 때문이다. 파서가 0~86400만 받으므로 음수는 오지 않지만 무제한으로 본다.
    [[nodiscard]] std::optional<std::chrono::milliseconds> function_timeout(std::int32_t timeout_seconds) noexcept;

    // 내장 변수를 `GITMAN_*` 고정 이름으로 내보낸다 (3.3, D14). `svnUrl`은 내보내지 않는다.
    // 값이 없는 변수는 부모 환경에서 지워, gitman이 물려받은 같은 이름의 낡은 값이 자식에게
    // 가지 않게 한다. `capture`에만 `GIT_TERMINAL_PROMPT=0`을 더한다 (D9).
    [[nodiscard]] std::vector<process_environment_override> function_environment_overrides(std::span<const variable_binding> builtins, function_run_mode mode);

    // 출력 수집 실행 요청이다. VCS 정책(`make_vcs_process_request`)의 인자와 환경을 쓰지 않는다 (4.1).
    // 두 요청 모두 `program`이 `cmd.exe`이면 `command_line_syntax::cmd`로 조립한다 (4.3, D27).
    [[nodiscard]] process_request make_function_process_request(function_command command, std::int32_t timeout_seconds, std::span<const variable_binding> builtins);
    // 분리 실행 요청이다. timeout이 없고 `GIT_TERMINAL_PROMPT`를 싣지 않는다.
    [[nodiscard]] process_launch_request make_function_launch_request(function_command command, std::span<const variable_binding> builtins);
    // 모드에 맞는 요청이다 (`function_command_factory`의 구현). 분리 실행은 `timeout_seconds`를
    // 보지 않는다. 문서의 `detached` 버튼에 남은 값은 파서가 경고만 하고 모델에 둔다 (3.2).
    [[nodiscard]] std::variant<process_request, process_launch_request> make_function_command(function_command command, function_run_mode mode, std::int32_t timeout_seconds, std::span<const variable_binding> builtins);
    // 운영 조립이 logic에 넘기는 묶음이다. 인용은 `build_process_command_line`(cmd면 cmd 전용 조립), 가리기는
    // `mask_secrets`, 요청은 `make_function_command`이고 환경 변수 읽기만 부르는 쪽이 준다.
    [[nodiscard]] function_run_services make_function_run_services(function_environment_lookup environment);
} // namespace gitman
