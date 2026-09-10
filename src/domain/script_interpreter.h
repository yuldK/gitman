#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 스크립트 파일의 확장자가 정하는 해석기다
    // (custom-commands-and-svn-hooks-design X2).
    //
    // `win32_process_runner`는 `CreateProcessW`에 `lpApplicationName`을 넘기므로
    // `.py`·`.bat` 같은 파일을 직접 실행할 수 없다. 부르는 쪽이 해석기 실행 파일을
    // 정하고 스크립트를 인자로 넘겨야 한다. 이 표가 그 규칙이며, OS를 모르고 파일도
    // 읽지 않아 확장자 판정만 따로 test할 수 있다.
    enum class script_interpreter_kind
    {
        // 아는 해석기가 없다. 실행을 거부한다.
        unsupported,
        // 스크립트가 아니라 실행 파일 자체다 (`.exe`·`.com`). 그대로 실행한다.
        executable,
        python,
        node,
        power_shell,
        command_shell,
    };

    struct script_interpreter_spec
    {
        script_interpreter_kind kind { script_interpreter_kind::unsupported };
        // 찾을 실행 파일 이름이다. `executable`이면 비어 있다.
        std::u8string_view executable_name {};
        // 첫 이름을 찾지 못했을 때 쓰는 대체다 (`pwsh.exe` 다음 `powershell.exe`).
        std::u8string_view alternate_executable_name {};
        // 스크립트 경로 **앞에** 붙는 인자다. 스크립트의 인자는 그 뒤에 온다.
        std::vector<std::u8string> leading_arguments {};
        // 셸은 `PATH`보다 `%SystemRoot%\System32`를 먼저 본다. 앞에 놓인 동명 파일이
        // 셸을 가로채는 것을 막는다.
        bool prefer_system_directory { false };
    };

    // 경로의 확장자로 해석기를 정한다. 확장자 비교는 ASCII 대소문자를 무시한다.
    // 확장자가 없거나 모르는 것이면 `unsupported`다.
    [[nodiscard]] script_interpreter_spec script_interpreter_for(std::u8string_view script_path);

    // 로그와 진단에 쓰는 이름이다.
    [[nodiscard]] std::u8string_view script_interpreter_kind_name(script_interpreter_kind kind) noexcept;
} // namespace gitman
