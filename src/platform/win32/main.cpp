#include "application/application_options.h"
#include "platform/win32/utf8.h"
#include "platform/win32/win32_application.h"

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

    const int result { gitman::win32::run_application(instance, *parsed_options.options) };
    if (should_uninitialize_com)
        CoUninitialize();
    return result;
}
