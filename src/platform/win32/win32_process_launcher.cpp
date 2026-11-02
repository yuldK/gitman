#include "platform/win32/win32_process_launcher.h"

#include "infrastructure/command_line_builder.h"
#include "platform/win32/utf8.h"
#include "platform/win32/win32_environment_block.h"

#include <windows.h>

#include <string>
#include <utility>

namespace gitman::win32 {
    namespace {
        std::uint32_t last_error_or(const std::uint32_t fallback) noexcept
        {
            const DWORD error { GetLastError() };
            return error == ERROR_SUCCESS ? fallback : static_cast<std::uint32_t>(error);
        }

        process_launch_result make_start_failure(const std::u8string_view message, const std::uint32_t native_error)
        {
            process_launch_result result {};
            result.status = process_launch_status::start_failed;
            result.native_error = native_error;

            diagnostic value {};
            value.code = diagnostic_code::process_start_failed;
            value.severity = diagnostic_severity::error;
            value.message = std::u8string { message };
            value.native_error = native_error;
            result.diagnostics.push_back(std::move(value));
            return result;
        }

        // 예외로 빠진 경로도 `start_failed`에 `process_start_failed` 진단을 붙인다는 계약을
        // 지킨다. 진단 문자열마저 할당하지 못하면 구조화된 결과만 돌려준다.
        process_launch_result make_unexpected_failure_result() noexcept
        {
            process_launch_result result {};
            result.status = process_launch_status::start_failed;
            result.native_error = ERROR_NOT_ENOUGH_MEMORY;
            try
            {
                diagnostic value {};
                value.code = diagnostic_code::process_start_failed;
                value.severity = diagnostic_severity::error;
                value.message = u8"프로세스 실행 중 예기치 않은 오류가 발생했습니다.";
                value.native_error = result.native_error;
                result.diagnostics.push_back(std::move(value));
            }
            catch (...)
            {
                // 진단 문자열조차 만들 수 없는 상황이다.
            }
            return result;
        }

        void normalize_separators(std::wstring& path) noexcept
        {
            for (wchar_t& value : path)
            {
                if (value == L'/')
                    value = L'\\';
            }
        }

        process_launch_result launch_impl(const process_launch_request& request)
        {
            std::vector<diagnostic> validation { validate_process_launch_request(request) };
            if (validation.empty() == false)
            {
                process_launch_result result {};
                result.status = process_launch_status::invalid_request;
                result.diagnostics = std::move(validation);
                return result;
            }

            const auto executable { utf8_to_utf16(request.executable) };
            const auto command_line { utf8_to_utf16(build_windows_command_line(request.executable, request.arguments)) };
            const auto working_directory { utf8_to_utf16(request.working_directory) };
            if (executable.value.has_value() == false
                || command_line.value.has_value() == false
                || working_directory.value.has_value() == false)
                return make_start_failure(u8"실행 요청의 UTF-8 값을 Windows 문자열로 변환하지 못했습니다.", ERROR_NO_UNICODE_TRANSLATION);

            const environment_block environment { make_environment_block(request.environment_overrides) };
            if (environment.failed)
                return make_start_failure(u8"환경 변수 override를 Windows 문자열로 변환하지 못했습니다.", environment.native_error);

            std::wstring application_path { *executable.value };
            std::wstring current_directory { *working_directory.value };
            normalize_separators(application_path);
            normalize_separators(current_directory);
            // `CreateProcessW`는 명령줄 buffer를 수정할 수 있으므로 쓰기 가능한 사본을 넘긴다.
            std::wstring mutable_command_line { *command_line.value };

            void* environment_pointer { nullptr };
            if (environment.block.has_value())
                environment_pointer = const_cast<wchar_t*>(environment.block->c_str());

            STARTUPINFOW startup {};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION created {};
            SetLastError(ERROR_SUCCESS);
            // `lpApplicationName`에 절대 경로를 줘 PATH 탐색과 확장자 추론을 모두 끈다 (4.2).
            // 핸들을 상속하지 않고 job에 넣지 않는다. `CREATE_NO_WINDOW`가 없으므로 GUI
            // 부모(gitman)에서 띄운 콘솔 프로그램은 자기 콘솔을 갖는다 (4.1).
            const BOOL launched {
                CreateProcessW(application_path.c_str(), mutable_command_line.data(), nullptr, nullptr, FALSE
                    , CREATE_UNICODE_ENVIRONMENT, environment_pointer, current_directory.c_str(), &startup, &created
                )
            };
            if (launched == FALSE)
                return make_start_failure(u8"프로그램을 시작하지 못했습니다.", last_error_or(ERROR_FILE_NOT_FOUND));

            // 기다리지 않으므로 handle을 바로 닫는다. 자식은 그대로 돈다.
            CloseHandle(created.hThread);
            CloseHandle(created.hProcess);

            process_launch_result result {};
            result.status = process_launch_status::started;
            return result;
        }

        class win32_process_launcher final : public process_launcher
        {
        public:
            [[nodiscard]] process_launch_result launch(const process_launch_request& request) noexcept override
            {
                try
                {
                    return launch_impl(request);
                }
                catch (...)
                {
                    return make_unexpected_failure_result();
                }
            }
        };
    } // namespace

    std::unique_ptr<process_launcher> make_process_launcher()
    {
        return std::make_unique<win32_process_launcher>();
    }
} // namespace gitman::win32
