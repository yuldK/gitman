#pragma once

#include "application/process_request.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace gitman::win32 {
    struct environment_block
    {
        // 값이 없고 실패도 아니면 부모 환경을 그대로 상속한다.
        std::optional<std::wstring> block {};
        // 실패했을 때의 Win32 오류 코드다. 성공이면 0(`ERROR_SUCCESS`)이다.
        std::uint32_t native_error { 0 };
        bool failed { false };
    };

    // 부모 환경에 override를 얹은 `CreateProcessW`용 UTF-16 환경 block을 만든다.
    // override가 없으면 block 없이 돌려줘 부모 환경을 그대로 상속시킨다. 출력 수집
    // 실행(runner)과 분리 실행(launcher)이 같은 env 내보내기를 갖도록 함께 쓴다.
    [[nodiscard]] environment_block make_environment_block(const std::vector<process_environment_override>& overrides);
} // namespace gitman::win32
