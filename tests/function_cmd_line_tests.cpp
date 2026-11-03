#include "application/function_run_services.h"
#include "application/process_request.h"
#include "domain/function_button.h"
#include "domain/process_execution.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "infrastructure/command_line_builder.h"
#include "infrastructure/function_execution_policy.h"
#include "platform/win32/win32_process_runner.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

// `program`이 `cmd.exe`일 때의 cmd 전용 조립과 거부다 (function-buttons-design 4.3, D26·D27, 6.1 R1).
// 실제 `cmd.exe` 아래에서 `gitman_process_test_child`를 띄워 값이 그대로 닿는지 본다. 배치 파일은 test가
// 임시 폴더에 만든다.

namespace {
    constexpr std::u8string_view system_cmd { u8"C:\\Windows\\System32\\cmd.exe" };
    // 주입이 실행되면 작업 폴더에 이 이름의 빈 파일이 생긴다. `whoami` 대신 흔적만 남기는 무해한 명령이다.
    constexpr std::u8string_view injected_branch { u8"main&echo.>INJECTED" };
    constexpr std::wstring_view injected_marker { L"INJECTED" };

    std::u8string cmd_line(const std::vector<std::u8string>& arguments)
    {
        return gitman::build_cmd_command_line(system_cmd, std::span<const std::u8string> { arguments });
    }

    std::filesystem::path module_directory()
    {
        std::array<wchar_t, MAX_PATH> module_path {};
        const DWORD length { GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size())) };
        REQUIRE(length > 0);
        return std::filesystem::path { std::wstring_view { module_path.data(), static_cast<std::size_t>(length) } }.parent_path();
    }

    std::u8string cmd_program_path()
    {
        std::array<wchar_t, MAX_PATH> buffer {};
        const UINT length { GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size())) };
        REQUIRE(length > 0);
        const std::filesystem::path path { std::filesystem::path { std::wstring_view { buffer.data(), length } } / L"cmd.exe" };
        REQUIRE(std::filesystem::exists(path));
        return path.u8string();
    }

    // `D:\R&D`와 `OneDrive - 회사명` 꼴의 폴더를 만들고 각 폴더에 test 자식과 그 자식을 `%*`로 부르는 배치를 둔다.
    class cmd_folders
    {
    public:
        cmd_folders()
        {
            std::error_code error {};
            const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
            REQUIRE_FALSE(static_cast<bool>(error));

            const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
            for (std::size_t attempt = 0; attempt < 100; ++attempt)
            {
                error.clear();
                const std::filesystem::path candidate { base / (L"gitman cmd test-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
                if (std::filesystem::create_directory(candidate, error))
                {
                    root_ = candidate;
                    break;
                }
            }
            REQUIRE_FALSE(root_.empty());

            research_ = root_ / L"R&D";
            onedrive_ = root_ / L"OneDrive - 회사명";
            for (const std::filesystem::path& folder : { research_, onedrive_ })
            {
                REQUIRE(std::filesystem::create_directory(folder));
                REQUIRE(std::filesystem::copy_file(module_directory() / L"gitman_process_test_child.exe", folder / L"gitman_process_test_child.exe"));
                // 배치는 자기 폴더의 자식에게 받은 인자를 `%*`로 그대로 넘긴다. `%~dp0`은 따옴표 안이라 `&`가 든 폴더도 된다.
                write_batch(folder / L"run.bat", "@\"%~dp0gitman_process_test_child.exe\" echo-args %*\r\n");
            }
        }

        ~cmd_folders()
        {
            std::error_code error {};
            std::filesystem::remove_all(root_, error);
        }

        cmd_folders(const cmd_folders&) = delete;
        cmd_folders(cmd_folders&&) = delete;
        cmd_folders& operator=(const cmd_folders&) = delete;
        cmd_folders& operator=(cmd_folders&&) = delete;

        [[nodiscard]] const std::filesystem::path& research() const noexcept
        {
            return research_;
        }

        [[nodiscard]] const std::filesystem::path& onedrive() const noexcept
        {
            return onedrive_;
        }

        [[nodiscard]] bool injected() const
        {
            return std::filesystem::exists(research_ / injected_marker)
                || std::filesystem::exists(onedrive_ / injected_marker)
                || std::filesystem::exists(root_ / injected_marker);
        }

    private:
        static void write_batch(const std::filesystem::path& path, const std::string_view text)
        {
            std::ofstream stream { path, std::ios::binary };
            REQUIRE(stream.is_open());
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            REQUIRE(stream.good());
        }

        std::filesystem::path root_ {};
        std::filesystem::path research_ {};
        std::filesystem::path onedrive_ {};
    };

    class recording_sink final : public gitman::process_output_sink
    {
    public:
        void on_record(const gitman::process_output_record& record) override
        {
            if (record.stream == gitman::process_stream::standard_output)
                lines.push_back(record.text);
            else
                errors.push_back(record.text);
        }

        std::vector<std::u8string> lines {};
        std::vector<std::u8string> errors {};
    };

    // 문서는 `R&D` 폴더에, 저장소는 `OneDrive - 회사명` 폴더에 있다. 브랜치는 test가 고른다.
    gitman::variable_lookup make_lookup(const cmd_folders& folders, const std::u8string_view branch)
    {
        gitman::project_definition project {};
        project.id = { u8"frontend" };
        project.path.original = folders.onedrive().u8string();
        project.path.normalized = project.path.original;
        project.display_name = u8"Frontend";

        gitman::repository_snapshot snapshot {};
        snapshot.kind = gitman::repository_kind::git;
        snapshot.repository_root = project.path.normalized;
        snapshot.current_reference = branch;
        snapshot.local_revision = u8"a1b2c3d";

        gitman::variable_lookup lookup {};
        lookup.builtins = gitman::make_builtin_variables((folders.research() / L"team.version-list").u8string(), project, &snapshot);
        return lookup;
    }

    struct cmd_run
    {
        gitman::process_result result {};
        std::vector<std::u8string> lines {};
        std::vector<std::u8string> errors {};
        std::u8string display_command_line {};
    };

    // logic이 클릭 시점에 하는 일(치환 → cmd 검사 → 요청 조립)을 그대로 밟아 실제 `cmd.exe`로 돌린다.
    // `program_text`가 비면 System32의 `cmd.exe`를 쓴다.
    cmd_run run_through_cmd(const gitman::variable_lookup& lookup, const std::vector<std::u8string>& templates, const std::u8string_view program_text = {})
    {
        const gitman::variable_expansion program { gitman::expand_variables(program_text.empty() ? cmd_program_path() : std::u8string { program_text }, lookup) };
        std::vector<gitman::variable_expansion> arguments {};
        for (const std::u8string& text : templates)
        {
            arguments.push_back(gitman::expand_variables(text, lookup));
            REQUIRE(arguments.back().succeeded());
        }
        const gitman::variable_expansion working_directory { gitman::expand_variables(u8"${projectRoot}", lookup) };
        REQUIRE_FALSE(gitman::find_cmd_injection(program, arguments).has_value());

        const gitman::function_run_services services { gitman::make_function_run_services([](std::u8string_view) -> std::optional<std::u8string> { return std::nullopt; }) };
        cmd_run run {};
        run.display_command_line = gitman::build_display_command_line(program, arguments, services.join, services.mask);

        gitman::function_command command {};
        command.program = program.text;
        for (const gitman::variable_expansion& argument : arguments)
            command.arguments.push_back(argument.text);
        command.working_directory = working_directory.text;
        const gitman::process_request request { std::get<gitman::process_request>(gitman::make_function_command(std::move(command), gitman::function_run_mode::capture, 60, lookup.builtins)) };
        REQUIRE(request.syntax == gitman::command_line_syntax::cmd);

        const std::unique_ptr<gitman::process_runner> runner { gitman::win32::make_process_runner() };
        recording_sink sink {};
        const gitman::process_cancellation_token token {};
        run.result = runner->run(request, &sink, token);
        run.lines = std::move(sink.lines);
        run.errors = std::move(sink.errors);
        return run;
    }

    std::u8string bracketed(const std::u8string_view value)
    {
        return u8"[" + std::u8string { value } + u8"]";
    }
} // namespace

TEST_CASE("A cmd command line keeps switches, puts /s before /c and quotes only the tokens that need it", "[function_buttons][r1]")
{
    REQUIRE(cmd_line({ u8"/d", u8"/c", u8"C:\\R&D\\run.bat", u8"main&x", u8"plain", u8"", u8"a b", u8"C:\\R&D\\", u8"C:\\plain\\" })
        == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"\"C:\\R&D\\run.bat\" \"main&x\" plain \"\" \"a b\" \"C:\\R&D\\\\\" C:\\plain\\\""
    );
    // cmd가 다시 해석하는 문자와 배치 인자(`%1`)를 가르는 문자가 든 토큰만 감싼다.
    REQUIRE(cmd_line({ u8"/c", u8"run.bat", u8"a^b", u8"(x)", u8"p|q", u8"<>", u8"k=v", u8"a,b;c", u8"tab\there", u8"한글", u8"--flag" })
        == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"run.bat \"a^b\" \"(x)\" \"p|q\" \"<>\" \"k=v\" \"a,b;c\" \"tab\there\" 한글 --flag\""
    );
}

TEST_CASE("A cmd command line takes /k and /r, keeps a written /s and adds /c before the first non-switch", "[function_buttons][r1]")
{
    REQUIRE(cmd_line({}) == u8"\"C:\\Windows\\System32\\cmd.exe\"");
    REQUIRE(cmd_line({ u8"/d" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /d");
    REQUIRE(cmd_line({ u8"/d", u8"run.bat", u8"x" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"run.bat x\"");
    REQUIRE(cmd_line({ u8"/S", u8"/C", u8"run.bat" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /S /C \"run.bat\"");
    REQUIRE(cmd_line({ u8"/d", u8"/K", u8"run.bat" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /K \"run.bat\"");
    REQUIRE(cmd_line({ u8"/d", u8"/r", u8"run.bat" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /r \"run.bat\"");
    REQUIRE(cmd_line({ u8"/v:on", u8"/E:OFF", u8"/c", u8"x" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /v:on /E:OFF /s /c \"x\"");
    REQUIRE(cmd_line({ u8"/c" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"");
    // 스위치 꼴이 아니면(`/dc`는 cmd가 대화형으로 연다) 거기서 명령이 시작된다. `/c` 뒤의 `/s`도 명령의 일부다.
    REQUIRE(cmd_line({ u8"/dc", u8"x" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"/dc x\"");
    REQUIRE(cmd_line({ u8"/c", u8"/s", u8"x" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"/s x\"");
    // `/c:x`·`/K:1`·`/r:x`를 스위치로 두면 cmd가 그 `/c`·`/k`에서 명령을 시작해 아무것도 하지 않거나 대화형으로
    // 남는다. 명령의 시작으로 보면 cmd가 명령을 찾지 못한다는 오류로 끝난다(2026-11-03 실측).
    REQUIRE(cmd_line({ u8"/c:x", u8"run.bat" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"/c:x run.bat\"");
    REQUIRE(cmd_line({ u8"/d", u8"/K:1", u8"run.bat" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"/K:1 run.bat\"");
    REQUIRE(cmd_line({ u8"/r:x", u8"run.bat" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"/r:x run.bat\"");
    // 공백이 든 명령 한 덩어리는 감싸져 명령 이름 전체가 된다. 경로와 구별할 수 없으므로 명령과 인자를 나눠 적는다.
    REQUIRE(cmd_line({ u8"/c", u8"echo hi" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"echo hi\"\"");
    // cmd는 argv[0] 안의 `/cmd.exe`도 `/c`로 읽으므로 argv[0]은 `\`로 쓴다.
    REQUIRE(gitman::build_cmd_command_line(u8"C:/Windows/System32/cmd.exe", std::vector<std::u8string> { u8"/d", u8"x" })
        == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"x\""
    );

    const std::vector<std::u8string> arguments { u8"/c", u8"a&b" };
    REQUIRE(gitman::build_process_command_line(system_cmd, arguments, gitman::command_line_syntax::cmd) == cmd_line(arguments));
    REQUIRE(gitman::build_process_command_line(system_cmd, arguments, gitman::command_line_syntax::argv) == gitman::build_windows_command_line(system_cmd, arguments));
}

TEST_CASE("cmd.exe runs batches in R&D and OneDrive folders and the child receives every value unchanged", "[function_buttons][r1]")
{
    const cmd_folders folders {};
    const gitman::variable_lookup lookup { make_lookup(folders, injected_branch) };

    const std::vector<std::u8string> templates {
        u8"/d", u8"/c", u8"${workspaceFolder}\\run.bat", u8"${branch}", u8"a^b", u8"(x)", u8"p|q", u8"<>", u8"two words", u8"한글", u8"", u8"${projectRoot}", u8"k=v", u8"a,b;c",
    };
    const cmd_run research { run_through_cmd(lookup, templates) };
    INFO(reinterpret_cast<const char*>(research.display_command_line.c_str()));
    REQUIRE(research.result.completion == gitman::process_completion::exited);
    REQUIRE(research.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(research.errors.empty());
    REQUIRE(research.lines
        == std::vector<std::u8string> {
            bracketed(injected_branch),
            u8"[a^b]",
            u8"[(x)]",
            u8"[p|q]",
            u8"[<>]",
            u8"[two words]",
            u8"[한글]",
            u8"[]",
            bracketed(folders.onedrive().u8string()),
            u8"[k=v]",
            u8"[a,b;c]",
        }
    );
    REQUIRE_FALSE(folders.injected());

    const cmd_run onedrive { run_through_cmd(lookup, { u8"/d", u8"/c", u8"${projectRoot}\\run.bat", u8"${workspaceFolder}", u8"${branch}" }) };
    INFO(reinterpret_cast<const char*>(onedrive.display_command_line.c_str()));
    REQUIRE(onedrive.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(onedrive.lines == std::vector<std::u8string> { bracketed(folders.research().u8string()), bracketed(injected_branch) });
    REQUIRE_FALSE(folders.injected());
}

TEST_CASE("cmd.exe runs a program directly with corrected values and the display line is the real one", "[function_buttons][r1]")
{
    const cmd_folders folders {};
    const gitman::variable_lookup lookup { make_lookup(folders, injected_branch) };

    const cmd_run direct { run_through_cmd(lookup, { u8"/d", u8"/c", u8"${workspaceFolder}\\gitman_process_test_child.exe", u8"echo-args", u8"${branch}", u8"${workspaceFolder}\\" }) };
    REQUIRE(direct.result.exit_code == std::optional<std::int32_t> { 0 });
    // 끝 `\`는 닫는 따옴표 앞에서 두 배가 되어 자식에게는 하나로 닿는다.
    REQUIRE(direct.lines == std::vector<std::u8string> { bracketed(injected_branch), bracketed(folders.research().u8string() + u8"\\") });
    REQUIRE_FALSE(folders.injected());

    // 로그 시작 줄·미리보기의 명령줄이 runner가 실제로 쓴 명령줄(가린 값)과 같다 (3.6).
    REQUIRE(direct.display_command_line == direct.result.masked_command_line);
    REQUIRE(direct.display_command_line.find(u8" /d /s /c \"\"") != std::u8string::npos);
}

TEST_CASE("cmd.exe written with forward slashes runs the requested command, not md", "[function_buttons][r1]")
{
    const cmd_folders folders {};
    const gitman::variable_lookup lookup { make_lookup(folders, injected_branch) };

    std::u8string forward { cmd_program_path() };
    for (char8_t& value : forward)
    {
        if (value == u8'\\')
            value = u8'/';
    }
    REQUIRE(gitman::is_cmd_program(forward));

    const cmd_run run { run_through_cmd(lookup, { u8"/d", u8"/c", u8"${workspaceFolder}\\gitman_process_test_child.exe", u8"echo-args", u8"${branch}", u8"x y" }, forward) };
    INFO(reinterpret_cast<const char*>(run.display_command_line.c_str()));
    REQUIRE(run.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(run.lines == std::vector<std::u8string> { bracketed(injected_branch), u8"[x y]" });
    REQUIRE_FALSE(folders.injected());
    // argv[0]의 `/cmd.exe`를 `/c`로 읽으면 내장 `md`가 인자를 받아 작업 폴더에 이 폴더들을 만든다.
    REQUIRE_FALSE(std::filesystem::exists(folders.onedrive() / L"echo-args"));
    REQUIRE_FALSE(std::filesystem::exists(folders.onedrive() / L"x y"));
    REQUIRE(run.display_command_line == run.result.masked_command_line);
    REQUIRE(run.display_command_line.starts_with(u8"\"" + cmd_program_path() + u8"\" /d /s /c "));
}

TEST_CASE("Values cmd would expand or cut are refused whatever their origin", "[function_buttons][r1]")
{
    const cmd_folders folders {};
    const gitman::variable_expansion program { gitman::expand_variables(system_cmd, {}) };

    for (const std::u8string_view branch : { u8"a\"b", u8"a%PATH%b", u8"100%", u8"a!b", u8"a\nb", u8"a\rb" })
    {
        INFO(reinterpret_cast<const char*>(std::u8string { branch }.c_str()));
        const gitman::variable_lookup lookup { make_lookup(folders, branch) };
        const std::vector<gitman::variable_expansion> arguments { gitman::expand_variables(u8"/c", lookup), gitman::expand_variables(u8"echo", lookup), gitman::expand_variables(u8"${branch}", lookup) };
        const std::optional<gitman::cmd_injection_finding> finding { gitman::find_cmd_injection(program, arguments) };
        REQUIRE(finding.has_value());
        REQUIRE(finding->argument == std::optional<std::size_t> { 2u });
        REQUIRE(finding->origin == gitman::variable_origin::runtime);
        REQUIRE(finding->variable == u8"branch");
    }

    const gitman::variable_lookup lookup { make_lookup(folders, u8"main") };
    const std::vector<gitman::variable_expansion> quote { gitman::expand_variables(u8"/c", lookup), gitman::expand_variables(u8"${branch}\"", lookup) };
    REQUIRE(gitman::cmd_injection_message(*gitman::find_cmd_injection(program, quote)) == u8"cmd.exe에 넘기는 args[1]에 gitman이 보정할 수 없는 문자(\")가 있습니다.");

    // 작성자가 쓴 글자 그대로의 `%`도 cmd가 값으로 바꾸므로 받지 않는다. 환경 변수는 `${env:NAME}`으로 넘긴다.
    const std::vector<gitman::variable_expansion> percent { gitman::expand_variables(u8"/c", lookup), gitman::expand_variables(u8"echo", lookup), gitman::expand_variables(u8"%PATH%", lookup) };
    const std::optional<gitman::cmd_injection_finding> literal { gitman::find_cmd_injection(program, percent) };
    REQUIRE(literal.has_value());
    REQUIRE(literal->origin == gitman::variable_origin::literal);
    REQUIRE(literal->character == u8'%');

    // 문서 경로 구간도 보정할 수 없는 문자는 거부한다.
    gitman::project_definition project {};
    project.id = { u8"frontend" };
    project.path.original = u8"D:\\100%\\frontend";
    gitman::variable_lookup document {};
    document.builtins = gitman::make_builtin_variables(u8"D:\\100%\\team.version-list", project, nullptr);
    const std::vector<gitman::variable_expansion> path { gitman::expand_variables(u8"/c", document), gitman::expand_variables(u8"${workspaceFolder}\\run.bat", document) };
    const std::optional<gitman::cmd_injection_finding> folder { gitman::find_cmd_injection(program, path) };
    REQUIRE(folder.has_value());
    REQUIRE(folder->origin == gitman::variable_origin::document);
    REQUIRE(gitman::cmd_injection_message(*folder) == u8"cmd.exe에 넘기는 ${workspaceFolder} 값에 gitman이 보정할 수 없는 문자(%)가 있습니다.");

    // 보정할 수 있는 문자는 거부하지 않는다.
    const gitman::variable_lookup corrected { make_lookup(folders, u8"a&b|c^d(e)<f>") };
    const std::vector<gitman::variable_expansion> safe { gitman::expand_variables(u8"/c", corrected), gitman::expand_variables(u8"${branch}", corrected), gitman::expand_variables(u8"${workspaceFolder}", corrected) };
    REQUIRE_FALSE(gitman::find_cmd_injection(program, safe).has_value());
}

TEST_CASE("A cmd request carrying a value cmd cannot keep is an invalid request", "[function_buttons][r1]")
{
    gitman::process_request request {};
    request.executable = system_cmd;
    request.working_directory = u8"C:\\work";
    request.maximum_captured_bytes_per_stream = 1024u;
    request.syntax = gitman::command_line_syntax::cmd;
    request.arguments = { u8"/c", u8"echo", u8"a&b" };
    REQUIRE(gitman::validate_process_request(request).empty());

    for (const std::u8string_view value : { u8"a%b", u8"a!b", u8"a\"b", u8"a\nb" })
    {
        request.arguments = { u8"/c", u8"echo", std::u8string { value } };
        REQUIRE_FALSE(gitman::validate_process_request(request).empty());

        gitman::process_launch_request launch {};
        launch.executable = request.executable;
        launch.working_directory = request.working_directory;
        launch.arguments = request.arguments;
        launch.syntax = gitman::command_line_syntax::cmd;
        REQUIRE_FALSE(gitman::validate_process_launch_request(launch).empty());
    }

    // 같은 값도 argv 규칙이면 셸을 거치지 않으므로 받는다.
    request.syntax = gitman::command_line_syntax::argv;
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("A batch that reads GITMAN_BRANCH is refused when the exported runtime value has cmd metacharacters", "[function_buttons][r1]")
{
    const cmd_folders folders {};
    // `x==x,md,INJECTED`는 git이 받는 브랜치 이름이다. 배치의 `if %GITMAN_BRANCH%==main …`에서 `==`가 비교를 일찍
    // 끝내고 `,`·`;`·`=`가 뒤를 `md INJECTED`로 가른다(2026-11-03 실측).
    for (const std::u8string_view branch : { u8"main&whoami", u8"a|b", u8"a<b", u8"a>b", u8"a^b", u8"a(b", u8"a)b", u8"a%b", u8"a!b", u8"a\"b", u8"a\nb", u8"x==x,md,INJECTED", u8"x==x;md;INJECTED", u8"a=b", u8"a,b", u8"a;b" })
    {
        INFO(reinterpret_cast<const char*>(std::u8string { branch }.c_str()));
        const gitman::variable_lookup lookup { make_lookup(folders, branch) };
        const std::optional<gitman::cmd_environment_finding> finding { gitman::find_cmd_environment_injection(system_cmd, lookup.builtins) };
        REQUIRE(finding.has_value());
        REQUIRE(finding->environment == u8"GITMAN_BRANCH");
        // cmd가 아니면 배치가 `%GITMAN_BRANCH%`를 다시 해석하지 않는다.
        REQUIRE_FALSE(gitman::find_cmd_environment_injection(u8"C:\\Tools\\run.exe", lookup.builtins).has_value());
    }

    const gitman::variable_lookup ampersand { make_lookup(folders, u8"main&whoami") };
    REQUIRE(gitman::cmd_environment_injection_message(*gitman::find_cmd_environment_injection(u8"c:/windows/system32/CMD.EXE", ampersand.builtins))
        == u8"cmd.exe에 내보내는 GITMAN_BRANCH 값에 cmd가 다시 해석하는 문자(&)가 있습니다."
    );

    // 저장소 루트(런타임)도 같다. 문서 경로(`GITMAN_WORKSPACE_FOLDER`의 `R&D`)는 문서 작성자의 것이라 보지 않는다.
    gitman::project_definition project {};
    project.id = { u8"frontend" };
    project.path.original = u8"C:\\work\\frontend";
    gitman::repository_snapshot snapshot {};
    snapshot.kind = gitman::repository_kind::git;
    snapshot.repository_root = u8"C:\\work\\R&D";
    snapshot.current_reference = u8"main";
    const std::vector<gitman::variable_binding> root { gitman::make_builtin_variables(u8"C:\\work\\team.version-list", project, &snapshot) };
    REQUIRE(gitman::find_cmd_environment_injection(system_cmd, root)->environment == u8"GITMAN_REPOSITORY_ROOT");

    const gitman::variable_lookup plain { make_lookup(folders, u8"feature/login-2") };
    REQUIRE_FALSE(gitman::find_cmd_environment_injection(system_cmd, plain.builtins).has_value());
}
