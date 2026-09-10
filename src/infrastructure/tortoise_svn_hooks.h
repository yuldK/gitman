#pragma once

#include "domain/command_definition.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // TortoiseSVN이 등록해 둔 클라이언트 훅 하나다
    // (custom-commands-and-svn-hooks-design H3).
    //
    // TortoiseSVN은 훅을 `HKCU\Software\TortoiseSVN`의 `hooks` (REG_SZ) **값 하나**에
    // 담는다. 개행으로 나뉜 여섯 줄이 훅 하나이며 옛 판은 다섯 줄이다. 레지스트리를
    // 읽는 것은 platform/win32의 몫이고, 여기서는 그 문자열을 값으로 받아 해석한다 —
    // 그래야 레지스트리 없이 규칙을 test할 수 있다.
    struct tortoise_svn_hook
    {
        svn_hook_phase phase { svn_hook_phase::pre_update };
        // 훅을 걸 작업 복사본 경로다. `*`는 전체를 뜻한다.
        std::u8string working_copy_path {};
        // 사용자가 적은 명령줄이다. TortoiseSVN이 뒤에 인자를 덧붙인다.
        std::u8string command_line {};
        // 끝날 때까지 기다린다. 거짓이면 TortoiseSVN도 종료 코드를 보지 않으므로
        // gitman은 기다리되 결과를 판정에 쓰지 않는다 (H5).
        bool wait { true };
        // 콘솔 창을 보인다. gitman의 runner는 창을 만들지 않아 지킬 수 없다 (H5).
        bool show_console { false };
        // 건너뛸 수 없는 훅이다. gitman에는 "훅 없이 다시 시도" 경로가 없어 판정에
        // 쓰지 않고 읽어만 둔다.
        bool enforce { false };

        [[nodiscard]] bool operator==(const tortoise_svn_hook&) const noexcept = default;
    };

    // 값 하나를 훅 목록으로 해석한다. update 계열이 아닌 종류(commit·lock 등)는
    // gitman이 그 작업을 하지 않으므로 버린다. 모르는 종류 이름이 나오면 그 record를
    // 건너뛰고 다음 줄부터 다시 맞춰 본다.
    [[nodiscard]] std::vector<tortoise_svn_hook> parse_tortoise_svn_hooks(std::u8string_view registry_value);

    // 작업 복사본에 걸린 훅 하나를 고른다. 경로에서 부모로 올라가며 정확히 일치하는
    // 것을 먼저 찾고, 없으면 `*`를 쓴다. 한 종류당 하나만 실행하는 TortoiseSVN의
    // `FindItem` 규칙과 같다 (H3).
    [[nodiscard]] const tortoise_svn_hook* select_tortoise_svn_hook(const std::vector<tortoise_svn_hook>& hooks, svn_hook_phase phase, std::u8string_view working_copy_path);

    // 저장된 명령줄을 프로그램과 인자로 쪼갠다. 첫 토큰이 프로그램이며 따옴표로
    // 감싼 경로를 허용한다. TortoiseSVN은 이 문자열을 `CreateProcess`의
    // `lpCommandLine`으로 그대로 넘기지만, gitman의 runner는 실행 파일과 인자 배열을
    // 요구하므로 여기서 나눈다.
    struct tortoise_command_line
    {
        std::u8string program {};
        std::vector<std::u8string> arguments {};

        [[nodiscard]] bool operator==(const tortoise_command_line&) const noexcept = default;
    };

    [[nodiscard]] tortoise_command_line split_tortoise_command_line(std::u8string_view command_line);

    // 종류 이름과 단계의 짝이다. update 계열만 다룬다.
    [[nodiscard]] std::optional<svn_hook_phase> parse_tortoise_hook_type(std::u8string_view value) noexcept;
    // 아는 종류 이름인지다 (버리는 종류 포함). record 경계를 다시 맞출 때 쓴다.
    [[nodiscard]] bool is_tortoise_hook_type(std::u8string_view value) noexcept;
} // namespace gitman
