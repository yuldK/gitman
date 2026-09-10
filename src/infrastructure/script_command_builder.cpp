#include "infrastructure/script_command_builder.h"

#include "infrastructure/vcs_execution_policy.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        bool same_environment_name(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
            {
                char8_t a { left[index] };
                char8_t b { right[index] };
                if (a >= u8'a' && a <= u8'z')
                    a = static_cast<char8_t>(a - u8'a' + u8'A');
                if (b >= u8'a' && b <= u8'z')
                    b = static_cast<char8_t>(b - u8'a' + u8'A');
                if (a != b)
                    return false;
            }
            return true;
        }

        // 마지막 구성 요소다. 로그에 전체 해석기 경로를 늘어놓지 않는다.
        std::u8string_view file_name_of(const std::u8string_view path) noexcept
        {
            std::size_t begin { 0 };
            for (std::size_t index = 0; index < path.size(); ++index)
                if (path[index] == u8'/' || path[index] == u8'\\')
                    begin = index + 1;
            return path.substr(begin);
        }

        void append_quoted(std::u8string& target, const std::u8string_view value)
        {
            const bool needs_quotes { value.empty() || value.find(u8' ') != std::u8string_view::npos || value.find(u8'"') != std::u8string_view::npos };
            if (needs_quotes == false)
            {
                target.append(value);
                return;
            }
            target.push_back(u8'"');
            for (const char8_t character : value)
            {
                if (character == u8'"')
                    target.push_back(u8'\\');
                target.push_back(character);
            }
            target.push_back(u8'"');
        }
    } // namespace

    void script_environment_builder::set(const std::u8string_view name, const std::u8string_view value)
    {
        if (name.empty() || value.empty())
            return;

        const auto existing { std::ranges::find_if(values_, [name](const process_environment_override& entry) { return same_environment_name(entry.name, name); }) };
        if (existing != values_.end())
        {
            existing->value = std::u8string { value };
            return;
        }
        values_.push_back({ std::u8string { name }, std::u8string { value } });
    }

    process_request make_script_process_request(const resolved_script_interpreter& interpreter, const script_command& command)
    {
        if (interpreter.usable() == false)
            return {};

        const vcs_command_limits limits { vcs_limits_for(vcs_command_class::user_script) };

        process_request request {};
        request.executable = interpreter.executable;
        request.working_directory = command.working_directory;
        request.arguments = interpreter.leading_arguments;
        // `.exe`·`.com`은 스크립트 자체가 실행 파일이라 다시 인자로 넣지 않는다.
        if (interpreter.kind != script_interpreter_kind::executable)
            request.arguments.push_back(command.script);
        request.arguments.reserve(request.arguments.size() + command.arguments.size());
        for (const std::u8string& argument : command.arguments)
            request.arguments.push_back(argument);
        request.environment_overrides = command.environment;
        request.timeout = command.timeout_seconds > 0 ? std::optional<std::chrono::milliseconds> { std::chrono::milliseconds { command.timeout_seconds * 1000 } } : limits.timeout;
        request.maximum_captured_bytes_per_stream = limits.maximum_captured_bytes_per_stream;
        request.maximum_record_bytes = default_process_record_byte_limit;
        // Windows 스크립트의 출력은 보통 콘솔 코드 페이지다 (한국어 Windows에서
        // CP949). UTF-8로 읽히는 레코드는 그대로 두므로 UTF-8을 내는 스크립트도 상한다.
        request.text_encoding = process_text_encoding::active_code_page_fallback;
        return request;
    }

    std::u8string describe_process_command(const process_request& request)
    {
        std::u8string text {};
        append_quoted(text, file_name_of(request.executable));
        for (const std::u8string& argument : request.arguments)
        {
            text.push_back(u8' ');
            append_quoted(text, argument);
        }
        return text;
    }
} // namespace gitman
