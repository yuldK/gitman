// 프로세스 실행 계층 test가 사용하는 자식 프로그램이다.
//
// 표준 `wmain` argv 규칙을 쓰는 콘솔 실행 파일이어야 한다. `cmd.exe`는
// `CommandLineToArgvW` 규칙이 아닌 자체 따옴표 처리를 하므로 test 자식으로 쓰면
// 인용 검증이 잘못 실패한다. 출력은 CRT 변환을 피하려고 raw byte로 쓴다.
#include <windows.h>

#include <cstddef>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {
    constexpr int usage_exit_code { 90 };
    constexpr int unknown_command_exit_code { 99 };

    void write_bytes(const DWORD stream, const std::string_view bytes)
    {
        const HANDLE handle { GetStdHandle(stream) };
        std::size_t offset { 0 };
        while (offset < bytes.size())
        {
            DWORD written { 0 };
            if (WriteFile(handle, bytes.data() + offset, static_cast<DWORD>(bytes.size() - offset), &written, nullptr) == FALSE || written == 0)
                return;
            offset += static_cast<std::size_t>(written);
        }
    }

    std::string to_utf8(const std::wstring_view value)
    {
        if (value.empty())
            return {};

        const int size { WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr) };
        if (size <= 0)
            return {};

        std::string utf8(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), utf8.data(), size, nullptr, nullptr);
        return utf8;
    }

    int hex_digit_value(const wchar_t digit) noexcept
    {
        if (digit >= L'0' && digit <= L'9')
            return digit - L'0';
        if (digit >= L'a' && digit <= L'f')
            return 10 + (digit - L'a');
        if (digit >= L'A' && digit <= L'F')
            return 10 + (digit - L'A');
        return -1;
    }

    int run_echo_args(const int argc, wchar_t** const argv)
    {
        // 인용 왕복을 확인하려고 대괄호로 감싸 argv를 한 줄씩 출력한다.
        for (int index = 2; index < argc; ++index)
            write_bytes(STD_OUTPUT_HANDLE, "[" + to_utf8(argv[index]) + "]\n");
        return 0;
    }

    int run_echo_cwd()
    {
        const DWORD length { GetCurrentDirectoryW(0, nullptr) };
        if (length == 0)
            return 91;

        std::wstring directory(static_cast<std::size_t>(length), L'\0');
        const DWORD written { GetCurrentDirectoryW(length, directory.data()) };
        if (written == 0 || written >= length)
            return 91;

        directory.resize(static_cast<std::size_t>(written));
        write_bytes(STD_OUTPUT_HANDLE, to_utf8(directory) + "\n");
        return 0;
    }

    int run_echo_env(const int argc, wchar_t** const argv)
    {
        if (argc < 3)
            return usage_exit_code;

        std::wstring value(32u * 1024u, L'\0');
        const DWORD length { GetEnvironmentVariableW(argv[2], value.data(), static_cast<DWORD>(value.size())) };
        if (length == 0)
        {
            write_bytes(STD_OUTPUT_HANDLE, "<unset>\n");
            return 0;
        }

        value.resize(static_cast<std::size_t>(length));
        write_bytes(STD_OUTPUT_HANDLE, to_utf8(value) + "\n");
        return 0;
    }

    int run_emit(const int argc, wchar_t** const argv)
    {
        if (argc < 3)
            return usage_exit_code;

        const long total { std::wcstol(argv[2], nullptr, 10) };
        const DWORD stream { argc >= 4 && std::wstring_view { argv[3] } == L"stderr" ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE };
        std::string line {};
        for (std::size_t index = 0; index < 60; ++index)
            line.push_back(static_cast<char>('a' + (index % 26)));
        line.push_back('\n');

        long written { 0 };
        while (written < total)
        {
            write_bytes(stream, line);
            written += static_cast<long>(line.size());
        }
        return 0;
    }

    int run_interleave(const int argc, wchar_t** const argv)
    {
        if (argc < 3)
            return usage_exit_code;

        const long count { std::wcstol(argv[2], nullptr, 10) };
        for (long index = 0; index < count; ++index)
        {
            write_bytes(STD_OUTPUT_HANDLE, "out " + std::to_string(index) + "\n");
            write_bytes(STD_ERROR_HANDLE, "err " + std::to_string(index) + "\n");
        }
        return 0;
    }

    int run_emit_bytes(const int argc, wchar_t** const argv)
    {
        if (argc < 3)
            return usage_exit_code;

        const std::wstring_view text { argv[2] };
        if (text.size() % 2 != 0)
            return usage_exit_code;

        std::string bytes {};
        for (std::size_t index = 0; index < text.size(); index += 2)
        {
            const int high { hex_digit_value(text[index]) };
            const int low { hex_digit_value(text[index + 1]) };
            if (high < 0 || low < 0)
                return usage_exit_code;
            bytes.push_back(static_cast<char>((high << 4) | low));
        }
        write_bytes(STD_OUTPUT_HANDLE, bytes);
        return 0;
    }

    int run_emit_mixed()
    {
        // 한글 CRLF 줄, 잘못된 byte, 진행 표시 두 개와 줄 끝 없는 tail을 낸다.
        write_bytes(STD_OUTPUT_HANDLE, "\xed\x95\x9c\xea\xb8\x80 line\r\n");
        write_bytes(STD_OUTPUT_HANDLE, "broken \xff\xfe end\n");
        write_bytes(STD_OUTPUT_HANDLE, "progress 10%\rprogress 100%\r");
        write_bytes(STD_OUTPUT_HANDLE, "no newline tail");
        return 0;
    }

    int run_emit_split()
    {
        // 3 byte 문자가 두 번의 읽기에 걸치도록 나눠 보낸다.
        write_bytes(STD_OUTPUT_HANDLE, std::string_view { "\xed\x95", 2 });
        Sleep(150);
        write_bytes(STD_OUTPUT_HANDLE, std::string_view { "\x9c\n", 2 });
        return 0;
    }

    int run_sleep(const int argc, wchar_t** const argv)
    {
        if (argc < 3)
            return usage_exit_code;

        // 대기 전에 한 줄을 내보내 timeout 이전 출력이 전달되는지 확인할 수 있게 한다.
        write_bytes(STD_OUTPUT_HANDLE, "sleeping\n");
        Sleep(static_cast<DWORD>(std::wcstol(argv[2], nullptr, 10)));
        write_bytes(STD_OUTPUT_HANDLE, "woke\n");
        return 0;
    }

    int run_write_marker(const int argc, wchar_t** const argv)
    {
        if (argc < 4)
            return usage_exit_code;

        Sleep(static_cast<DWORD>(std::wcstol(argv[2], nullptr, 10)));
        const HANDLE file { CreateFileW(argv[3], GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr) };
        if (file == INVALID_HANDLE_VALUE)
            return 93;
        CloseHandle(file);
        return 0;
    }

    // 손자 프로세스를 만들고 오래 대기한다. job이 트리를 함께 종료하는지 확인할 때
    // 손자가 남아 있으면 지정한 시간 뒤 marker 파일을 만든다.
    int run_spawn_child(const int argc, wchar_t** const argv)
    {
        if (argc < 4)
            return usage_exit_code;

        std::wstring module_path(MAX_PATH, L'\0');
        const DWORD length { GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size())) };
        if (length == 0 || length >= module_path.size())
            return 94;
        module_path.resize(static_cast<std::size_t>(length));

        std::wstring command_line { L"\"" };
        command_line.append(module_path);
        command_line.append(L"\" write-marker ");
        command_line.append(argv[2]);
        command_line.append(L" \"");
        command_line.append(argv[3]);
        command_line.append(L"\"");

        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION information {};
        if (CreateProcessW(module_path.c_str(), command_line.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information) == FALSE)
            return 95;

        CloseHandle(information.hThread);
        CloseHandle(information.hProcess);
        write_bytes(STD_OUTPUT_HANDLE, "spawned\n");
        Sleep(60000);
        return 0;
    }

    // 아무 출력 없이 대기만 한다. 상속한 표준 handle을 잡은 채 살아남는 손자 역할이다.
    int run_hold_handles(const int argc, wchar_t** const argv)
    {
        if (argc < 3)
            return usage_exit_code;
        Sleep(static_cast<DWORD>(std::wcstol(argv[2], nullptr, 10)));
        return 0;
    }

    // 표준 handle을 물려준 손자를 만들고 기다리지 않은 채 즉시 끝난다. 자식이 정상
    // 종료해도 출력 pipe를 잡은 프로세스가 남는 상황을 재현한다.
    int run_spawn_detached(const int argc, wchar_t** const argv)
    {
        if (argc < 3)
            return usage_exit_code;

        std::wstring module_path(MAX_PATH, L'\0');
        const DWORD length { GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size())) };
        if (length == 0 || length >= module_path.size())
            return 94;
        module_path.resize(static_cast<std::size_t>(length));

        std::wstring command_line { L"\"" };
        command_line.append(module_path);
        command_line.append(L"\" hold-handles ");
        command_line.append(argv[2]);

        STARTUPINFOW startup {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION information {};
        if (CreateProcessW(module_path.c_str(), command_line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &information) == FALSE)
            return 95;

        CloseHandle(information.hThread);
        CloseHandle(information.hProcess);
        write_bytes(STD_OUTPUT_HANDLE, "detached\n");
        return 0;
    }

    int run_read_stdin()
    {
        char buffer[64] {};
        DWORD read_count { 0 };
        const BOOL succeeded { ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &read_count, nullptr) };
        // stdin이 NUL이면 오류 없이 0 byte를 읽는다. 프롬프트 대기가 없다는 뜻이다.
        return (succeeded != FALSE && read_count == 0) ? 0 : 92;
    }
} // namespace

int wmain(const int argc, wchar_t** const argv)
{
    if (argc < 2)
        return usage_exit_code;

    const std::wstring_view command { argv[1] };
    if (command == L"exit")
        return argc >= 3 ? static_cast<int>(std::wcstol(argv[2], nullptr, 10)) : usage_exit_code;
    if (command == L"echo-args")
        return run_echo_args(argc, argv);
    if (command == L"echo-cwd")
        return run_echo_cwd();
    if (command == L"echo-env")
        return run_echo_env(argc, argv);
    if (command == L"emit")
        return run_emit(argc, argv);
    if (command == L"interleave")
        return run_interleave(argc, argv);
    if (command == L"emit-bytes")
        return run_emit_bytes(argc, argv);
    if (command == L"emit-mixed")
        return run_emit_mixed();
    if (command == L"emit-split")
        return run_emit_split();
    if (command == L"sleep")
        return run_sleep(argc, argv);
    if (command == L"write-marker")
        return run_write_marker(argc, argv);
    if (command == L"spawn-child")
        return run_spawn_child(argc, argv);
    if (command == L"hold-handles")
        return run_hold_handles(argc, argv);
    if (command == L"spawn-detached")
        return run_spawn_detached(argc, argv);
    if (command == L"read-stdin")
        return run_read_stdin();
    return unknown_command_exit_code;
}
