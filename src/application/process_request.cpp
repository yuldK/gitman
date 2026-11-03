#include "application/process_request.h"

#include <utility>

namespace gitman {
    namespace {
        constexpr char8_t ascii_lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value + (u8'a' - u8'A'));
            return value;
        }

        bool equal_ignoring_ascii_case(const std::u8string_view left, const std::u8string_view right) noexcept
        {
            if (left.size() != right.size())
                return false;
            for (std::size_t index = 0; index < left.size(); ++index)
                if (ascii_lower(left[index]) != ascii_lower(right[index]))
                    return false;
            return true;
        }

        bool contains_null(const std::u8string_view value) noexcept
        {
            return value.find(char8_t {}) != std::u8string_view::npos;
        }

        std::u8string to_u8string(const std::size_t value)
        {
            if (value == 0)
                return std::u8string { u8"0" };

            std::u8string digits {};
            for (std::size_t remaining = value; remaining > 0; remaining /= 10)
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + (remaining % 10)));
            return digits;
        }

        diagnostic make_invalid_request(std::u8string message)
        {
            diagnostic value {};
            value.code = diagnostic_code::invalid_process_request;
            value.severity = diagnostic_severity::error;
            value.message = std::move(message);
            return value;
        }

        void validate_executable(const std::u8string& executable, std::vector<diagnostic>& diagnostics)
        {
            if (executable.empty())
            {
                diagnostics.push_back(make_invalid_request(u8"실행 파일 경로가 비어 있습니다."));
                return;
            }
            if (contains_null(executable))
            {
                diagnostics.push_back(make_invalid_request(u8"실행 파일 경로에 NUL 문자를 포함할 수 없습니다."));
                return;
            }
            // PATH 탐색과 확장자 추론을 하지 않으므로 상대 경로는 호출자의 현재 디렉터리에
            // 따라 다른 프로그램을 실행할 수 있다. 따라서 절대 경로만 허용한다.
            if (is_absolute_windows_path(executable) == false)
                diagnostics.push_back(make_invalid_request(u8"실행 파일은 절대 경로여야 합니다: " + executable));
        }

        void validate_working_directory(const std::u8string& working_directory, std::vector<diagnostic>& diagnostics)
        {
            if (working_directory.empty())
            {
                diagnostics.push_back(make_invalid_request(u8"작업 디렉터리 경로가 비어 있습니다."));
                return;
            }
            if (contains_null(working_directory))
            {
                diagnostics.push_back(make_invalid_request(u8"작업 디렉터리 경로에 NUL 문자를 포함할 수 없습니다."));
                return;
            }
            if (is_absolute_windows_path(working_directory) == false)
                diagnostics.push_back(make_invalid_request(u8"작업 디렉터리는 절대 경로여야 합니다: " + working_directory));
        }

        void validate_arguments(const std::vector<std::u8string>& arguments, std::vector<diagnostic>& diagnostics)
        {
            for (std::size_t index = 0; index < arguments.size(); ++index)
                if (contains_null(arguments[index]))
                    diagnostics.push_back(make_invalid_request(u8"인자 " + to_u8string(index) + u8"에 NUL 문자를 포함할 수 없습니다."));
        }

        // cmd 명령줄로 아예 전달할 수 없는 문자(줄바꿈)가 든 인자는 실행하지 않는다. 기능 버튼은 logic이 먼저
        // 거르고(4.3), 이 검사는 다른 길로 온 요청을 막는 마지막 자리다. 미리 지은 명령줄은 cmd 요청에만 있다.
        void validate_cmd_arguments(const std::vector<std::u8string>& arguments, const command_line_syntax syntax, const std::u8string& command_line, std::vector<diagnostic>& diagnostics)
        {
            if (syntax != command_line_syntax::cmd)
            {
                if (command_line.empty() == false)
                    diagnostics.push_back(make_invalid_request(u8"미리 지은 명령줄은 cmd.exe 요청에만 쓸 수 있습니다."));
                return;
            }
            for (std::size_t index = 0; index < arguments.size(); ++index)
            {
                if (arguments[index].find_first_of(cmd_uncorrectable_characters) != std::u8string::npos)
                    diagnostics.push_back(make_invalid_request(u8"인자 " + to_u8string(index) + u8"에 cmd.exe 명령줄로 넘길 수 없는 문자(줄바꿈)가 있습니다."));
            }
            if (command_line.find_first_of(cmd_uncorrectable_characters) != std::u8string::npos || contains_null(command_line))
                diagnostics.push_back(make_invalid_request(u8"cmd.exe 명령줄에 줄바꿈이나 NUL 문자가 있습니다."));
        }

        void validate_environment(const std::vector<process_environment_override>& environment_overrides, std::vector<diagnostic>& diagnostics)
        {
            for (std::size_t index = 0; index < environment_overrides.size(); ++index)
            {
                const process_environment_override& entry { environment_overrides[index] };
                const std::u8string position { u8"환경 변수 " + to_u8string(index) };
                if (entry.name.empty())
                {
                    diagnostics.push_back(make_invalid_request(position + u8"의 이름이 비어 있습니다."));
                    continue;
                }
                if (entry.name.find(u8'=') != std::u8string::npos || contains_null(entry.name))
                {
                    diagnostics.push_back(make_invalid_request(position + u8"의 이름에는 '='와 NUL 문자를 포함할 수 없습니다: " + entry.name));
                    continue;
                }
                if (entry.value.has_value() && contains_null(*entry.value))
                {
                    diagnostics.push_back(make_invalid_request(position + u8"의 값에 NUL 문자를 포함할 수 없습니다: " + entry.name));
                    continue;
                }

                // Windows 환경 변수 이름은 대소문자를 구분하지 않으므로 중복 override는
                // 적용 순서에 따라 결과가 달라진다. 모호함을 남기지 않고 오류로 본다.
                for (std::size_t earlier = 0; earlier < index; ++earlier)
                    if (equal_ignoring_ascii_case(environment_overrides[earlier].name, entry.name))
                    {
                        diagnostics.push_back(make_invalid_request(position + u8"의 이름이 앞선 항목과 중복됩니다: " + entry.name));
                        break;
                    }
            }
        }

        void validate_limits(const process_request& request, std::vector<diagnostic>& diagnostics)
        {
            if (request.timeout.has_value() && request.timeout->count() <= 0)
                diagnostics.push_back(make_invalid_request(u8"timeout은 0보다 커야 합니다."));
            if (request.maximum_captured_bytes_per_stream == 0)
                diagnostics.push_back(make_invalid_request(u8"스트림별 캡처 상한은 0보다 커야 합니다."));
            if (request.maximum_record_bytes < minimum_process_record_byte_limit)
                diagnostics.push_back(make_invalid_request(u8"레코드 크기 상한은 " + to_u8string(minimum_process_record_byte_limit) + u8" byte 이상이어야 합니다."));
        }
    } // namespace

    std::vector<diagnostic> validate_process_request(const process_request& request)
    {
        std::vector<diagnostic> diagnostics {};
        validate_executable(request.executable, diagnostics);
        validate_working_directory(request.working_directory, diagnostics);
        validate_arguments(request.arguments, diagnostics);
        validate_cmd_arguments(request.arguments, request.syntax, request.command_line, diagnostics);
        validate_environment(request.environment_overrides, diagnostics);
        validate_limits(request, diagnostics);
        return diagnostics;
    }

    std::vector<diagnostic> validate_process_launch_request(const process_launch_request& request)
    {
        std::vector<diagnostic> diagnostics {};
        validate_executable(request.executable, diagnostics);
        validate_working_directory(request.working_directory, diagnostics);
        validate_arguments(request.arguments, diagnostics);
        validate_cmd_arguments(request.arguments, request.syntax, request.command_line, diagnostics);
        validate_environment(request.environment_overrides, diagnostics);
        return diagnostics;
    }

    std::u8string_view process_text_encoding_name(const process_text_encoding encoding) noexcept
    {
        switch (encoding)
        {
        case process_text_encoding::utf8:
            return u8"utf8";
        case process_text_encoding::active_code_page_fallback:
            return u8"active_code_page_fallback";
        }
        return u8"unknown";
    }
} // namespace gitman
