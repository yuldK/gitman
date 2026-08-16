#pragma once

#include "application/process_request.h"
#include "domain/repository_snapshot.h"

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 명령 부류별 제한이다. 단계 3은 기본값을 강제하지 않고 호출자가 명시하게
    // 남겼으므로 실제 값은 여기서 정한다. 후속 단계가 설정으로 바꿀 수 있도록 한
    // 곳에 모아 둔다.
    enum class vcs_command_class
    {
        // `--version` 확인이다. 응답 없는 실행 파일이 시작을 막지 않게 짧게 둔다.
        tool_probe,
        // 네트워크를 쓰지 않는 조회다.
        local_query,
        // fetch 등 네트워크를 쓰는 조회다.
        remote_query,
        // pull, submodule update, svn update다.
        update,
        switch_target,
    };

    struct vcs_command_limits
    {
        std::chrono::milliseconds timeout {};
        std::size_t maximum_captured_bytes_per_stream {};
    };

    [[nodiscard]] vcs_command_limits vcs_limits_for(vcs_command_class command_class) noexcept;

    // Git을 비대화형으로 강제하는 환경 override다. 값이 없는 항목은 부모 환경에서
    // 삭제해 askpass GUI 경로까지 막는다. 로캘은 강제하지 않는다. 사용자 결정에 따라
    // 시스템 언어 메시지를 그대로 보여 주고, 오류 분류는 `vcs_error_classifier`가
    // 로캘 독립 신호로만 수행한다.
    [[nodiscard]] std::vector<process_environment_override> git_environment_overrides();

    // 모든 Git 명령 앞에 붙는 공통 인자다. `-c gc.auto=0`은 조회 명령이 background
    // 유지보수 프로세스를 띄워 출력 pipe를 물고 늘어지는 것을 원천 차단한다.
    [[nodiscard]] std::vector<std::u8string> git_common_arguments();

    [[nodiscard]] std::vector<process_environment_override> svn_environment_overrides();
    [[nodiscard]] std::vector<std::u8string> svn_common_arguments();

    [[nodiscard]] std::vector<process_environment_override> vcs_environment_overrides(repository_kind kind);
    [[nodiscard]] std::vector<std::u8string> vcs_common_arguments(repository_kind kind);

    // 공통 정책을 적용한 요청을 만든다. `arguments`는 공통 인자 뒤에 붙는다.
    [[nodiscard]] process_request make_vcs_process_request(
        repository_kind kind, std::u8string_view executable, std::u8string_view working_directory, std::vector<std::u8string> arguments, vcs_command_class command_class);
} // namespace gitman
