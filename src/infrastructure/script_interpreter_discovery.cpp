#include "infrastructure/script_interpreter_discovery.h"

#include "domain/path_syntax.h"

#include <utility>

namespace gitman {
    namespace {
        diagnostic make_diagnostic(const diagnostic_code code, const diagnostic_severity severity, std::u8string message)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = std::move(message);
            return value;
        }

        std::u8string join_path(const std::u8string_view directory, const std::u8string_view name)
        {
            std::u8string result { directory };
            if (result.empty() == false && result.back() != u8'\\' && result.back() != u8'/')
                result.push_back(u8'\\');
            result.append(name);
            return result;
        }

        // 후보 디렉터리를 순서대로 훑어 처음 나오는 파일을 쓴다. 이름이 둘이면
        // (`pwsh.exe` → `powershell.exe`) **첫 이름을 모든 디렉터리에서 먼저** 찾는다.
        // 디렉터리를 우선하면 PATH 앞쪽의 옛 판이 새 판을 가릴 수 있다.
        std::u8string find_in_directories(const std::vector<std::u8string>& directories, const std::u8string_view name, const vcs_file_probe& probe)
        {
            if (name.empty())
                return {};
            for (const std::u8string& directory : directories)
            {
                std::u8string candidate { join_path(directory, name) };
                if (probe.probe(candidate) == vcs_path_kind::file)
                    return candidate;
            }
            return {};
        }
    } // namespace

    resolved_script_interpreter resolve_script_interpreter(const std::u8string_view script_path, const vcs_tool_environment& environment, const vcs_file_probe& probe)
    {
        const script_interpreter_spec spec { script_interpreter_for(script_path) };

        resolved_script_interpreter resolved {};
        resolved.kind = spec.kind;
        resolved.leading_arguments = spec.leading_arguments;

        if (spec.kind == script_interpreter_kind::unsupported)
        {
            resolved.diagnostics.push_back(make_diagnostic(diagnostic_code::script_interpreter_unavailable, diagnostic_severity::error,
                std::u8string { u8"실행할 수 있는 확장자가 아닙니다: " } + std::u8string { script_path }));
            return resolved;
        }

        if (spec.kind == script_interpreter_kind::executable)
        {
            // 실행 파일 자체다. 절대 경로 판정은 프로세스 요청 검증이 다시 하지만,
            // 여기서 걸러야 "해석기를 못 찾았다"가 아니라 정확한 사유가 남는다.
            if (is_absolute_windows_path(script_path) == false)
            {
                resolved.diagnostics.push_back(make_diagnostic(diagnostic_code::script_interpreter_unavailable, diagnostic_severity::error,
                    std::u8string { u8"실행 파일은 절대 경로여야 합니다: " } + std::u8string { script_path }));
                return resolved;
            }
            resolved.executable = script_path;
            return resolved;
        }

        std::vector<std::u8string> directories {};
        // 셸은 시스템 디렉터리를 먼저 본다. PATH 앞에 놓인 동명 파일이 `cmd.exe`나
        // `powershell.exe`를 가로채는 것을 막는다.
        if (spec.prefer_system_directory && environment.system_directory.empty() == false)
            directories.push_back(environment.system_directory);
        for (std::u8string& entry : split_search_path(environment.path_environment))
            directories.push_back(std::move(entry));

        resolved.executable = find_in_directories(directories, spec.executable_name, probe);
        if (resolved.executable.empty())
            resolved.executable = find_in_directories(directories, spec.alternate_executable_name, probe);

        if (resolved.executable.empty())
        {
            std::u8string message { u8"스크립트를 실행할 " };
            message.append(spec.executable_name);
            if (spec.alternate_executable_name.empty() == false)
            {
                message.append(u8"(또는 ");
                message.append(spec.alternate_executable_name);
                message.append(u8")");
            }
            message.append(u8"을 PATH에서 찾지 못했습니다.");
            resolved.diagnostics.push_back(make_diagnostic(diagnostic_code::script_interpreter_unavailable, diagnostic_severity::error, std::move(message)));
        }
        return resolved;
    }
} // namespace gitman
