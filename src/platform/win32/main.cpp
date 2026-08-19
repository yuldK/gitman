#include "application/application_options.h"
#include "platform/win32/utf8.h"
#include "platform/win32/win32_application.h"
#include "platform/win32/win32_file_association.h"

#include <shellapi.h>
#include <windows.h>

#include <objbase.h>

#include <string>
#include <vector>

namespace {
    void show_argument_error(const std::u8string& error)
    {
        const auto wide_error { gitman::win32::utf8_to_utf16(error) };
        if (wide_error.value.has_value())
            MessageBoxW(nullptr, wide_error.value->c_str(), L"Gitman 명령줄 오류", MB_OK | MB_ICONERROR);
    }

    // 명령행 연결 등록·제거다 (REQ-016). 창 없이 수행하고 종료 코드로 성패를
    // 알린다. 스크립트 사용을 위해 성공은 조용히 끝나고 실패만 dialog로 알린다.
    [[nodiscard]] int run_file_association_command(const bool register_association)
    {
        gitman::win32::file_association_outcome outcome {};
        if (register_association)
            outcome = gitman::win32::register_file_association(gitman::win32::current_executable_path());
        else
            outcome = gitman::win32::unregister_file_association();
        if (outcome.succeeded)
            return 0;
        for (const gitman::diagnostic& value : outcome.diagnostics)
            show_argument_error(value.message);
        return 1;
    }
} // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ LPWSTR, _In_ int)
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const HRESULT com_result { CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED) };
    const bool should_uninitialize_com { SUCCEEDED(com_result) };

    int argument_count { 0 };
    LPWSTR* wide_arguments { CommandLineToArgvW(GetCommandLineW(), &argument_count) };
    if (wide_arguments == nullptr)
    {
        if (should_uninitialize_com)
            CoUninitialize();
        return 1;
    }

    std::vector<std::u8string> arguments {};
    arguments.reserve(static_cast<std::size_t>(argument_count));
    for (int index = 0; index < argument_count; ++index)
    {
        auto conversion { gitman::win32::utf16_to_utf8(wide_arguments[index]) };
        if (conversion.value.has_value() == false)
        {
            LocalFree(wide_arguments);
            if (should_uninitialize_com)
                CoUninitialize();
            return 1;
        }
        arguments.push_back(std::move(*conversion.value));
    }
    LocalFree(wide_arguments);

    const gitman::application_options_result parsed_options {
        gitman::parse_application_options(arguments),
    };

    if (parsed_options.options.has_value() == false)
    {
        show_argument_error(parsed_options.error);
        if (should_uninitialize_com)
            CoUninitialize();
        return 2;
    }

    // 연결 등록·제거 모드는 창을 만들지 않는다.
    if (parsed_options.options->register_file_association || parsed_options.options->unregister_file_association)
    {
        const int association_result { run_file_association_command(parsed_options.options->register_file_association) };
        if (should_uninitialize_com)
            CoUninitialize();
        return association_result;
    }

    const int result { gitman::win32::run_application(instance, *parsed_options.options) };
    if (should_uninitialize_com)
        CoUninitialize();
    return result;
}
