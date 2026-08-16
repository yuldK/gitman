#pragma once

#include "domain/repository_snapshot.h"
#include "infrastructure/vcs_command_runner.h"

#include <string_view>

namespace gitman {
    enum class vcs_failure_kind
    {
        none,
        authentication_required,
        offline,
        repository_not_found,
        timed_out,
        cancelled,
        // 실행 파일을 시작하지 못했거나 결과를 신뢰할 수 없다.
        execution_failed,
        // 위 어느 신호에도 맞지 않는 실패다. 추측하지 않고 원문을 그대로 보고한다.
        command_failed,
    };

    // 로캘을 강제하지 않기로 했으므로 번역되는 메시지 본문으로 분류하지 않는다.
    // 언어와 무관하게 남는 신호만 사용한다.
    //
    // - SVN은 번역된 메시지에도 `svn: E170013:` 형태의 오류 코드를 붙인다.
    // - Git의 HTTP 전송 오류는 libcurl이 만들고 libcurl에는 번역 catalog가 없다.
    // - SSH 오류는 OpenSSH가 만들며 같은 이유로 번역되지 않는다.
    // - Git이 감싸는 문장이 번역되어도 HTTP 상태 번호는 남는다.
    [[nodiscard]] vcs_failure_kind classify_vcs_failure(repository_kind kind, const vcs_command_result& result) noexcept;

    // 명령 결과 없이 stderr 본문만으로 판정한다. fixture test와 재분류에 사용한다.
    [[nodiscard]] vcs_failure_kind classify_vcs_error_text(repository_kind kind, std::u8string_view standard_error) noexcept;

    [[nodiscard]] remote_sync_state remote_sync_state_for_failure(vcs_failure_kind failure) noexcept;
    [[nodiscard]] diagnostic_code diagnostic_code_for_failure(vcs_failure_kind failure) noexcept;
    [[nodiscard]] std::u8string_view vcs_failure_kind_name(vcs_failure_kind failure) noexcept;
    [[nodiscard]] std::u8string_view vcs_failure_message(vcs_failure_kind failure) noexcept;
} // namespace gitman
