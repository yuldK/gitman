#pragma once

#include "application/vcs_file_probe.h"
#include "domain/diagnostic.h"
#include "domain/script_interpreter.h"
#include "infrastructure/vcs_tool_discovery.h"

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 스크립트 하나를 실행하기 위해 정해진 해석기다
    // (custom-commands-and-svn-hooks-design X3).
    struct resolved_script_interpreter
    {
        script_interpreter_kind kind { script_interpreter_kind::unsupported };
        // 실행할 절대 경로다. 비어 있으면 찾지 못했다.
        std::u8string executable {};
        // 스크립트 경로 앞에 붙는 인자다.
        std::vector<std::u8string> leading_arguments {};
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool usable() const noexcept
        {
            return executable.empty() == false;
        }
    };

    // 확장자로 해석기를 정하고 그 실행 파일을 찾는다. 찾는 순서는
    //  1. 셸(`cmd.exe`·`powershell.exe`)이면 `%SystemRoot%\System32` 먼저 — PATH 앞에
    //     놓인 동명 파일이 셸을 가로채는 것을 막는다.
    //  2. `PATH`의 각 디렉터리.
    // `.exe`·`.com`은 스크립트 경로 자체가 실행 파일이라 탐색하지 않는다.
    //
    // filesystem만 보고 프로세스는 만들지 않는다 (`--version` 확인 없음). 해석기의
    // 판번을 요구하지 않기 때문이며, 실행에 실패하면 그때 진단이 남는다.
    [[nodiscard]] resolved_script_interpreter resolve_script_interpreter(std::u8string_view script_path, const vcs_tool_environment& environment, const vcs_file_probe& probe);
} // namespace gitman
