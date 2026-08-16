#include "infrastructure/vcs_execution_policy.h"

#include <utility>

namespace gitman {
    namespace {
        constexpr std::size_t kibibyte { 1024u };
        constexpr std::size_t mebibyte { 1024u * 1024u };

        process_environment_override set_variable(const std::u8string_view name, const std::u8string_view value)
        {
            return {
                std::u8string { name },
                std::u8string { value },
            };
        }

        process_environment_override remove_variable(const std::u8string_view name)
        {
            return {
                std::u8string { name },
                std::nullopt,
            };
        }
    } // namespace

    vcs_command_limits vcs_limits_for(const vcs_command_class command_class) noexcept
    {
        switch (command_class)
        {
        case vcs_command_class::tool_probe:
            return { std::chrono::milliseconds { 5000 }, 64u * kibibyte };
        case vcs_command_class::local_query:
            return { std::chrono::milliseconds { 30000 }, 8u * mebibyte };
        case vcs_command_class::remote_query:
            return { std::chrono::milliseconds { 120000 }, 8u * mebibyte };
        case vcs_command_class::update:
            return { std::chrono::milliseconds { 600000 }, 32u * mebibyte };
        case vcs_command_class::switch_target:
            return { std::chrono::milliseconds { 300000 }, 8u * mebibyte };
        }
        return { std::chrono::milliseconds { 30000 }, 8u * mebibyte };
    }

    std::vector<process_environment_override> git_environment_overrides()
    {
        return {
            // 터미널 자격 증명 프롬프트를 금지한다.
            set_variable(u8"GIT_TERMINAL_PROMPT", u8"0"),
            // Git Credential Manager의 GUI 프롬프트를 금지한다.
            set_variable(u8"GCM_INTERACTIVE", u8"Never"),
            // SSH 암호와 호스트 키 확인 프롬프트 대신 즉시 실패하게 한다.
            set_variable(u8"GIT_SSH_COMMAND", u8"ssh -oBatchMode=yes"),
            // 조회 명령이 index를 갱신하려고 잠그지 않게 한다.
            set_variable(u8"GIT_OPTIONAL_LOCKS", u8"0"),
            // askpass 프로그램과 GUI 경로를 차단한다.
            remove_variable(u8"GIT_ASKPASS"),
            remove_variable(u8"SSH_ASKPASS"),
            remove_variable(u8"DISPLAY"),
        };
    }

    std::vector<std::u8string> git_common_arguments()
    {
        return {
            // 비ASCII 경로를 8진 이스케이프 없이 UTF-8 원문으로 출력한다.
            std::u8string { u8"-c" },
            std::u8string { u8"core.quotepath=false" },
            // background `gc`가 출력 pipe를 상속한 채 남지 않게 한다.
            std::u8string { u8"-c" },
            std::u8string { u8"gc.auto=0" },
            // ANSI escape가 로그와 파서에 섞이지 않게 한다.
            std::u8string { u8"-c" },
            std::u8string { u8"color.ui=false" },
            std::u8string { u8"--no-pager" },
        };
    }

    std::vector<process_environment_override> svn_environment_overrides()
    {
        // SVN은 `--non-interactive` 인자로 비대화형을 강제하므로 별도 환경 변수가
        // 필요하지 않다. 로캘도 강제하지 않는다.
        return {};
    }

    std::vector<std::u8string> svn_common_arguments()
    {
        return {
            std::u8string { u8"--non-interactive" },
        };
    }

    std::vector<process_environment_override> vcs_environment_overrides(const repository_kind kind)
    {
        return kind == repository_kind::subversion ? svn_environment_overrides() : git_environment_overrides();
    }

    std::vector<std::u8string> vcs_common_arguments(const repository_kind kind)
    {
        return kind == repository_kind::subversion ? svn_common_arguments() : git_common_arguments();
    }

    process_request make_vcs_process_request(
        const repository_kind kind, const std::u8string_view executable, const std::u8string_view working_directory, std::vector<std::u8string> arguments, const vcs_command_class command_class)
    {
        const vcs_command_limits limits { vcs_limits_for(command_class) };

        process_request request {};
        request.executable = executable;
        request.working_directory = working_directory;
        request.arguments = vcs_common_arguments(kind);
        request.arguments.reserve(request.arguments.size() + arguments.size());
        for (std::u8string& argument : arguments)
            request.arguments.push_back(std::move(argument));
        request.environment_overrides = vcs_environment_overrides(kind);
        request.timeout = limits.timeout;
        request.maximum_captured_bytes_per_stream = limits.maximum_captured_bytes_per_stream;
        // 로캘을 강제하지 않기로 했으므로 Git과 SVN 모두 시스템 언어 메시지를 낼 수
        // 있다. Windows에서 그 인코딩은 UTF-8일 수도 활성 code page일 수도 있어
        // fallback 모드를 쓴다. 유효한 UTF-8 레코드는 변환하지 않으므로 기계 판독
        // 출력에는 영향이 없다.
        request.text_encoding = process_text_encoding::active_code_page_fallback;
        return request;
    }
} // namespace gitman
