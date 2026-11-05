#pragma once

#include "application/cmd_target_inspector.h"

#include <memory>
#include <optional>
#include <span>
#include <string>

namespace gitman::win32 {
    // 비어 있으면 현재 실행 파일이다. 별도 설치 파일 없이 같은 exe의 숨은 조회 모드를 실행한다.
    [[nodiscard]] std::unique_ptr<cmd_target_inspector> make_cmd_target_inspector(
        std::u8string executable = {}, std::chrono::milliseconds timeout = cmd_target_inspection_timeout
    );

    // 보조 조회 모드일 때만 종료 코드를 돌려준다. 일반 앱 인자는 해석하지 않으며 창·설정·렌더러를 만들지 않는다.
    [[nodiscard]] std::optional<int> run_cmd_target_probe(std::span<const std::u8string> arguments);
}
