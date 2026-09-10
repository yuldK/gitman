#pragma once

#include "application/process_request.h"
#include "domain/command_definition.h"
#include "infrastructure/script_interpreter_discovery.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 스크립트가 인자 파싱 없이 맥락을 읽을 수 있게 넣어 주는 환경 변수다
    // (custom-commands-and-svn-hooks-design X5, H6).
    inline constexpr std::u8string_view script_environment_project_id { u8"GITMAN_PROJECT_ID" };
    inline constexpr std::u8string_view script_environment_project_name { u8"GITMAN_PROJECT_NAME" };
    inline constexpr std::u8string_view script_environment_repository_path { u8"GITMAN_REPOSITORY_PATH" };
    inline constexpr std::u8string_view script_environment_document_path { u8"GITMAN_DOCUMENT_PATH" };
    inline constexpr std::u8string_view script_environment_vcs { u8"GITMAN_VCS" };
    inline constexpr std::u8string_view script_environment_reference { u8"GITMAN_REFERENCE" };
    inline constexpr std::u8string_view script_environment_revision { u8"GITMAN_REVISION" };
    inline constexpr std::u8string_view script_environment_hook_phase { u8"GITMAN_HOOK_PHASE" };
    inline constexpr std::u8string_view script_environment_hook_operation { u8"GITMAN_HOOK_OPERATION" };
    inline constexpr std::u8string_view script_environment_hook_target { u8"GITMAN_HOOK_TARGET" };
    inline constexpr std::u8string_view script_environment_hook_error { u8"GITMAN_HOOK_ERROR" };

    // 환경 변수를 모은다. `process_request`가 대소문자 무시 중복을 오류로 보므로
    // 여기서 한 번 정리한다. 빈 값은 넣지 않는다 — "없음"과 "빈 문자열"을 구분할
    // 필요가 없고, 빈 변수를 만들면 스크립트가 `if defined`로 판단하지 못한다.
    class script_environment_builder
    {
    public:
        void set(std::u8string_view name, std::u8string_view value);
        [[nodiscard]] const std::vector<process_environment_override>& values() const noexcept
        {
            return values_;
        }

    private:
        std::vector<process_environment_override> values_ {};
    };

    // 스크립트 하나를 실행하는 요청의 재료다.
    struct script_command
    {
        // 절대 경로다. 상대 경로는 부르는 쪽이 문서 폴더 기준으로 이미 풀어 둔다.
        std::u8string script {};
        // 스크립트 **뒤에** 붙는 인자다.
        std::vector<std::u8string> arguments {};
        std::u8string working_directory {};
        std::vector<process_environment_override> environment {};
        // 0이면 제한 없음이다 (`vcs_command_class::user_script`의 기본과 같다).
        std::int32_t timeout_seconds { 0 };
    };

    // `make_vcs_process_request`의 형제다. git·svn 공통 인자를 붙이지 않는 것이
    // 유일한 차이이며, 나머지 정책(캡처 상한, 인코딩)은 같은 곳에서 가져온다.
    //
    // 해석기가 `usable()`이 아니면 빈 요청을 돌려준다 — 부르는 쪽이 진단을 보고
    // 실행하지 않는다.
    [[nodiscard]] process_request make_script_process_request(const resolved_script_interpreter& interpreter, const script_command& command);

    // 문서가 있는 폴더를 기준으로 상대 경로를 푼다. 이미 절대 경로면 그대로다.
    // 문서가 없거나(전역 설정만 있는 상태) 기준이 없으면 원본을 돌려주고, 절대
    // 경로가 아닌 채로 남으면 프로세스 요청 검증이 걸러 사유를 남긴다.
    [[nodiscard]] std::u8string resolve_script_path(std::u8string_view script, std::u8string_view document_path);

    // 스크립트를 어느 폴더에서 실행할지 정한다. 고른 자리가 비어 있으면 저장소
    // 경로로, 그것도 없으면 문서 폴더로 물러선다 — 작업 폴더가 비면 프로세스를
    // 만들 수 없다.
    [[nodiscard]] std::u8string resolve_script_working_directory(
        command_working_directory kind, std::u8string_view repository_path, std::u8string_view document_path, std::u8string_view script_path);

    // 카드 로그에 남길 사람이 읽는 명령줄이다 (X6). 무엇이 실행되는지 사후에
    // 추적할 수 있어야 해서 실행 파일과 인자를 모두 담는다. 공백이나 따옴표가 있는
    // 조각은 따옴표로 감싼다. 민감 정보 마스킹은 로그 파이프라인이 따로 한다.
    [[nodiscard]] std::u8string describe_process_command(const process_request& request);
} // namespace gitman
