#include "domain/script_interpreter.h"

#include <cstddef>
#include <initializer_list>

namespace gitman {
    namespace {
        // 경로의 확장자다. 마지막 구분자 뒤의 마지막 점부터이며, 점으로 시작하는
        // 이름(`.gitignore`)은 확장자가 없는 것으로 본다.
        std::u8string_view extension_of(const std::u8string_view path) noexcept
        {
            std::size_t name_begin { 0 };
            for (std::size_t index = 0; index < path.size(); ++index)
                if (path[index] == u8'/' || path[index] == u8'\\')
                    name_begin = index + 1;

            const std::u8string_view name { path.substr(name_begin) };
            const std::size_t dot { name.rfind(u8'.') };
            if (dot == std::u8string_view::npos || dot == 0)
                return {};
            return name.substr(dot);
        }

        bool same_extension(const std::u8string_view value, const std::u8string_view expected) noexcept
        {
            if (value.size() != expected.size())
                return false;
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                char8_t left { value[index] };
                if (left >= u8'A' && left <= u8'Z')
                    left = static_cast<char8_t>(left - u8'A' + u8'a');
                if (left != expected[index])
                    return false;
            }
            return true;
        }

        std::vector<std::u8string> arguments_of(const std::initializer_list<std::u8string_view> values)
        {
            std::vector<std::u8string> result {};
            result.reserve(values.size());
            for (const std::u8string_view value : values)
                result.emplace_back(value);
            return result;
        }
    } // namespace

    script_interpreter_spec script_interpreter_for(const std::u8string_view script_path)
    {
        const std::u8string_view extension { extension_of(script_path) };
        if (extension.empty())
            return {};

        script_interpreter_spec spec {};
        if (same_extension(extension, u8".exe") || same_extension(extension, u8".com"))
        {
            // 스크립트가 아니라 실행 파일이다. 해석기 없이 그대로 실행한다.
            spec.kind = script_interpreter_kind::executable;
            return spec;
        }
        if (same_extension(extension, u8".py") || same_extension(extension, u8".pyw"))
        {
            spec.kind = script_interpreter_kind::python;
            spec.executable_name = u8"python.exe";
            return spec;
        }
        if (same_extension(extension, u8".js") || same_extension(extension, u8".mjs") || same_extension(extension, u8".cjs"))
        {
            spec.kind = script_interpreter_kind::node;
            spec.executable_name = u8"node.exe";
            return spec;
        }
        if (same_extension(extension, u8".ps1"))
        {
            spec.kind = script_interpreter_kind::power_shell;
            // PowerShell 7이 있으면 그쪽을 쓴다. 없으면 Windows에 늘 있는 5.1이다.
            spec.executable_name = u8"pwsh.exe";
            spec.alternate_executable_name = u8"powershell.exe";
            // `-NoProfile`은 사용자 프로필이 출력을 더럽히거나 느리게 하는 것을 막고,
            // `-NonInteractive`는 stdin이 NUL인 환경에서 프롬프트를 기다리지 않게
            // 한다 (ADR-003 비대화형 규칙). `-File`은 그 뒤를 스크립트로 못 박아
            // 명령줄이 식으로 해석되지 않게 한다.
            spec.leading_arguments = arguments_of({ u8"-NoProfile", u8"-NonInteractive", u8"-ExecutionPolicy", u8"Bypass", u8"-File" });
            spec.prefer_system_directory = true;
            return spec;
        }
        if (same_extension(extension, u8".bat") || same_extension(extension, u8".cmd"))
        {
            spec.kind = script_interpreter_kind::command_shell;
            spec.executable_name = u8"cmd.exe";
            // `/d`는 AutoRun 레지스트리 명령을 건너뛴다. 다른 프로그램이 심어 둔
            // AutoRun이 훅·명령 앞에 끼어드는 것을 막는다.
            spec.leading_arguments = arguments_of({ u8"/d", u8"/c" });
            spec.prefer_system_directory = true;
            return spec;
        }
        return {};
    }

    std::u8string_view script_interpreter_kind_name(const script_interpreter_kind kind) noexcept
    {
        switch (kind)
        {
        case script_interpreter_kind::executable:
            return u8"executable";
        case script_interpreter_kind::python:
            return u8"python";
        case script_interpreter_kind::node:
            return u8"node";
        case script_interpreter_kind::power_shell:
            return u8"powershell";
        case script_interpreter_kind::command_shell:
            return u8"cmd";
        case script_interpreter_kind::unsupported:
        default:
            return u8"unsupported";
        }
    }
} // namespace gitman
