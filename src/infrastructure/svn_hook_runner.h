#pragma once

#include "application/process_cancellation.h"
#include "application/process_runner.h"
#include "application/temp_file_writer.h"
#include "application/vcs_file_probe.h"
#include "domain/command_definition.h"
#include "domain/diagnostic.h"
#include "domain/project.h"
#include "infrastructure/tortoise_svn_hooks.h"
#include "infrastructure/vcs_tool_discovery.h"

#include <string>
#include <vector>

namespace gitman {
    // 훅 하나를 실행할 때 스크립트에 알려 줄 맥락이다
    // (custom-commands-and-svn-hooks-design H4·H6).
    struct svn_hook_context
    {
        project_definition project {};
        // 작업 복사본 루트다. 훅의 작업 폴더이자 경로 필터의 대상이다.
        std::u8string working_copy_path {};
        svn_hook_phase phase { svn_hook_phase::pre_update };
        // switch 작업이다. TortoiseSVN은 switch에도 update 훅을 쓰므로 단계는 같고
        // 이 값으로만 갈린다.
        bool switching { false };
        // switch일 때의 대상 URL이다.
        std::u8string target_url {};
        std::u8string revision {};
        std::u8string reference {};
        // post 단계에서 본 명령이 실패했으면 그 요약이다. 성공이면 비어 있다.
        std::u8string error_text {};
    };

    struct svn_hook_run_result
    {
        // pre 계열 훅이 실패했다. 본 작업을 실행하면 안 된다 (2026-09-10 결정).
        bool blocked { false };
        // 실행한 훅 수다. 0이면 걸린 훅이 없었다.
        std::size_t executed { 0 };
        std::vector<diagnostic> diagnostics {};
    };

    // update·switch 앞뒤에서 훅을 순차 실행한다 (H1·H2).
    //
    // 순서는 TortoiseSVN 훅이 먼저, 그다음 문서·전역이 정의한 gitman 훅이 목록
    // 순서대로다. 하나가 실패하면 그 뒤는 실행하지 않는다.
    //
    // `temp_files`가 없으면 TortoiseSVN 훅만 건너뛴다. 그쪽은 인자를 임시 파일로
    // 받는 규약이라 파일을 만들 수 없으면 규약을 지킬 수 없고, 규약을 어긴 채
    // 실행하면 남의 스크립트가 엉뚱한 인자를 받는다.
    class svn_hook_runner
    {
    public:
        // `document_path`는 훅 스크립트의 상대 경로를 푸는 기준이다. 작업 하나
        // 동안 바뀌지 않으므로 맥락이 아니라 runner가 든다.
        svn_hook_runner(svn_hook_settings settings, std::vector<tortoise_svn_hook> tortoise_hooks, std::u8string document_path, process_runner& runner, const vcs_file_probe& probe,
            vcs_tool_environment environment, temp_file_writer* temp_files, process_output_sink* log) noexcept;

        // 이 작업 복사본에 걸린 훅이 하나도 없다. provider가 호출 자체를 건너뛴다.
        [[nodiscard]] bool empty(std::u8string_view working_copy_path) const;

        [[nodiscard]] svn_hook_run_result run(const svn_hook_context& context, const process_cancellation_token& token);

    private:
        // 훅 하나를 실행한다. 성공하면 참이다.
        [[nodiscard]] bool run_one(const svn_hook_context& context, std::u8string_view label, std::u8string_view program, std::vector<std::u8string> arguments, std::int32_t timeout_seconds,
            bool observe_exit_code, const process_cancellation_token& token, svn_hook_run_result& result);
        [[nodiscard]] bool run_tortoise_hook(const svn_hook_context& context, const process_cancellation_token& token, svn_hook_run_result& result);

        svn_hook_settings settings_ {};
        std::vector<tortoise_svn_hook> tortoise_hooks_ {};
        std::u8string document_path_ {};
        process_runner* runner_ { nullptr };
        const vcs_file_probe* probe_ { nullptr };
        vcs_tool_environment environment_ {};
        temp_file_writer* temp_files_ { nullptr };
        process_output_sink* log_ { nullptr };
    };
} // namespace gitman
