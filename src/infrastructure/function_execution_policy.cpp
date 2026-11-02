#include "infrastructure/function_execution_policy.h"

#include "infrastructure/command_line_builder.h"
#include "infrastructure/secret_masking.h"

#include <algorithm>
#include <array>
#include <string_view>
#include <utility>

namespace gitman {
    namespace {
        struct exported_variable
        {
            std::u8string_view variable {};
            std::u8string_view environment {};
        };

        // 고정 집합이다. 사용자 변수를 내보내지 않으므로 대소문자만 다른 이름이 생기지 않는다 (3.3).
        constexpr std::array exported_variables {
            exported_variable { u8"workspaceFolder", u8"GITMAN_WORKSPACE_FOLDER" },
            exported_variable { u8"workspaceFile", u8"GITMAN_WORKSPACE_FILE" },
            exported_variable { u8"projectRoot", u8"GITMAN_PROJECT_ROOT" },
            exported_variable { u8"projectId", u8"GITMAN_PROJECT_ID" },
            exported_variable { u8"projectName", u8"GITMAN_PROJECT_NAME" },
            exported_variable { u8"vcs", u8"GITMAN_VCS" },
            exported_variable { u8"repositoryRoot", u8"GITMAN_REPOSITORY_ROOT" },
            exported_variable { u8"branch", u8"GITMAN_BRANCH" },
            exported_variable { u8"revision", u8"GITMAN_REVISION" },
        };

        std::optional<std::u8string> binding_value(const std::span<const variable_binding> builtins, const std::u8string_view name)
        {
            const auto found {
                std::ranges::find_if(builtins
                    , [name](const variable_binding& binding)
                    {
                        return binding.name == name;
                    }
                )
            };
            if (found == builtins.end())
                return std::nullopt;
            return found->value;
        }
    } // namespace

    std::optional<std::chrono::milliseconds> function_timeout(const std::int32_t timeout_seconds) noexcept
    {
        if (timeout_seconds <= 0)
            return std::nullopt;
        return std::chrono::milliseconds { std::chrono::seconds { timeout_seconds } };
    }

    std::vector<process_environment_override> function_environment_overrides(const std::span<const variable_binding> builtins, const function_run_mode mode)
    {
        std::vector<process_environment_override> overrides {};
        overrides.reserve(exported_variables.size() + 1);
        for (const exported_variable& exported : exported_variables)
            overrides.push_back({ std::u8string { exported.environment }, binding_value(builtins, exported.variable) });

        // 스크립트 안의 git이 터미널에서 자격 증명을 묻다 timeout까지 멈추지 않게 한다.
        // GCM 같은 GUI 창은 막지 못한다. 분리 실행은 사용자가 창에서 답할 수 있어 싣지 않는다.
        if (mode == function_run_mode::capture)
            overrides.push_back({ std::u8string { u8"GIT_TERMINAL_PROMPT" }, std::u8string { u8"0" } });
        return overrides;
    }

    process_request make_function_process_request(function_command command, const std::int32_t timeout_seconds, const std::span<const variable_binding> builtins)
    {
        process_request request {};
        request.executable = std::move(command.program);
        request.arguments = std::move(command.arguments);
        request.working_directory = std::move(command.working_directory);
        request.environment_overrides = function_environment_overrides(builtins, function_run_mode::capture);
        request.timeout = function_timeout(timeout_seconds);
        request.maximum_captured_bytes_per_stream = function_capture_bytes_per_stream;
        request.maximum_record_bytes = default_process_record_byte_limit;
        // 사용자 프로그램의 출력 인코딩은 알 수 없다. VCS 요청과 같이 UTF-8이 아닌 레코드만
        // 활성 code page로 바꾼다 (`vcs_execution_policy.cpp`와 같은 값).
        request.text_encoding = process_text_encoding::active_code_page_fallback;
        return request;
    }

    process_launch_request make_function_launch_request(function_command command, const std::span<const variable_binding> builtins)
    {
        process_launch_request request {};
        request.executable = std::move(command.program);
        request.arguments = std::move(command.arguments);
        request.working_directory = std::move(command.working_directory);
        request.environment_overrides = function_environment_overrides(builtins, function_run_mode::detached);
        return request;
    }

    std::variant<process_request, process_launch_request> make_function_command(function_command command, const function_run_mode mode, const std::int32_t timeout_seconds, const std::span<const variable_binding> builtins)
    {
        if (mode == function_run_mode::detached)
            return make_function_launch_request(std::move(command), builtins);
        return make_function_process_request(std::move(command), timeout_seconds, builtins);
    }

    function_run_services make_function_run_services(function_environment_lookup environment)
    {
        function_run_services services {};
        services.join = [](const std::u8string_view program, const std::span<const std::u8string> arguments)
        {
            return build_windows_command_line(program, arguments);
        };
        services.mask = [](const std::u8string_view text)
        {
            return mask_secrets(text);
        };
        services.environment = std::move(environment);
        services.make_command = &make_function_command;
        return services;
    }
} // namespace gitman
