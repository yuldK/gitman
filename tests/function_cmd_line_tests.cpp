#include "application/function_run_services.h"
#include "application/process_request.h"
#include "domain/cmd_command_line.h"
#include "domain/function_button.h"
#include "domain/process_execution.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "infrastructure/cmd_target_inspection.h"
#include "infrastructure/command_line_builder.h"
#include "infrastructure/function_execution_policy.h"
#include "platform/win32/win32_process_runner.h"
#include "platform/win32/win32_vcs_file_probe.h"

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

// `program`이 `cmd.exe`일 때의 cmd 전용 조립과 보정이다 (function-buttons-design 4.3, D26·D27·D30, 6.1 R1·R4).
// 실제 `cmd.exe` 아래에서 `gitman_process_test_child`를 띄워 보정한 값이 그대로 닿는지 본다. 배치 파일은 test가
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

    gitman::cmd_text_piece literal(const std::u8string_view text)
    {
        return { std::u8string { text }, true, std::nullopt };
    }

    gitman::cmd_text_piece value(const std::u8string_view text)
    {
        return { std::u8string { text }, false, std::nullopt };
    }

    // 조각 하나짜리 인자들이다. `literal`이면 문서 작성자가 글자 그대로 쓴 인자다.
    std::vector<gitman::cmd_argument> arguments_of(const std::vector<std::u8string_view>& texts, const bool literal_text)
    {
        std::vector<gitman::cmd_argument> arguments {};
        for (const std::u8string_view text : texts)
            arguments.push_back({ { literal_text ? literal(text) : value(text) } });
        return arguments;
    }

    gitman::cmd_command_line assemble(const std::vector<gitman::cmd_argument>& arguments, const std::vector<gitman::cmd_exported_value>& exported = {})
    {
        return gitman::assemble_cmd_command_line(value(system_cmd), arguments, exported);
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

    // `D:\R&D`, `OneDrive - 회사명`, `R&D (x86) !bang` 꼴의 폴더를 만들고 각 폴더에 test 자식과 그 자식을 `%*`로
    // 부르는 배치, 저장소 루트 환경 변수를 따옴표 안에서 읽는 배치를 둔다.
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
            bang_ = root_ / L"R&D (x86) !bang";
            for (const std::filesystem::path& folder : { research_, onedrive_, bang_ })
            {
                REQUIRE(std::filesystem::create_directory(folder));
                REQUIRE(std::filesystem::copy_file(module_directory() / L"gitman_process_test_child.exe", folder / L"gitman_process_test_child.exe"));
                // 배치는 자기 폴더의 자식에게 받은 인자를 `%*`로 그대로 넘긴다. `%~dp0`은 따옴표 안이라 `&`가 든 폴더도 된다.
                write_batch(folder / L"run.bat", "@\"%~dp0gitman_process_test_child.exe\" echo-args %*\r\n");
                // 설명서 12.7의 권장대로 내보낸 환경 변수를 따옴표 안에서 읽는다.
                write_batch(folder / L"root.bat", "@\"%~dp0gitman_process_test_child.exe\" echo-args \"%GITMAN_REPOSITORY_ROOT%\" \"%GITMAN_BRANCH%\" %*\r\n");
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

        [[nodiscard]] const std::filesystem::path& bang() const noexcept
        {
            return bang_;
        }

        [[nodiscard]] bool injected() const
        {
            return std::filesystem::exists(research_ / injected_marker)
                || std::filesystem::exists(onedrive_ / injected_marker)
                || std::filesystem::exists(bang_ / injected_marker)
                || std::filesystem::exists(root_ / injected_marker);
        }

        static void write_batch(const std::filesystem::path& path, const std::string_view text)
        {
            std::ofstream stream { path, std::ios::binary };
            REQUIRE(stream.is_open());
            stream.write(text.data(), static_cast<std::streamsize>(text.size()));
            REQUIRE(stream.good());
        }

    private:
        std::filesystem::path root_ {};
        std::filesystem::path research_ {};
        std::filesystem::path onedrive_ {};
        std::filesystem::path bang_ {};
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

    // 문서는 `R&D` 폴더에 있다. 저장소는 기본이 `OneDrive - 회사명` 폴더이고 test가 바꿀 수 있다. 브랜치는 test가 고른다.
    gitman::variable_lookup make_lookup(const cmd_folders& folders, const std::u8string_view branch, const std::filesystem::path* repository = nullptr)
    {
        gitman::project_definition project {};
        project.id = { u8"frontend" };
        project.path.original = (repository != nullptr ? *repository : folders.onedrive()).u8string();
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
        gitman::cmd_command_line assembly {};
    };

    // logic이 클릭 시점에 하는 일(치환 → 줄바꿈 거부 → cmd 조립과 보정 → 요청 조립)을 그대로 밟아 실제 `cmd.exe`로
    // 돌린다. 사용자가 확인 dialog에서 승인한 것으로 본다. `program_text`가 비면 System32의 `cmd.exe`를 쓴다.
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
        run.assembly = gitman::assemble_function_cmd_command_line(program, arguments, lookup.builtins);
        REQUIRE_FALSE(run.assembly.rejection.has_value());

        gitman::function_command command {};
        command.program = program.text;
        for (const gitman::variable_expansion& argument : arguments)
            command.arguments.push_back(argument.text);
        command.working_directory = working_directory.text;
        command.command_line = run.assembly.text;
        const gitman::process_request request { std::get<gitman::process_request>(gitman::make_function_command(std::move(command), gitman::function_run_mode::capture, 60, lookup.builtins)) };
        REQUIRE(request.syntax == gitman::command_line_syntax::cmd);
        REQUIRE(gitman::validate_process_request(request).empty());

        const std::unique_ptr<gitman::process_runner> runner { gitman::win32::make_process_runner() };
        recording_sink sink {};
        const gitman::process_cancellation_token token {};
        run.result = runner->run(request, &sink, token);
        run.lines = std::move(sink.lines);
        run.errors = std::move(sink.errors);
        return run;
    }

    // 조각으로 직접 지은 인자를 실제 `cmd.exe`로 돌린다. 작업 폴더는 `working_directory`다.
    cmd_run run_assembled(const std::vector<gitman::cmd_argument>& arguments, const std::filesystem::path& working_directory, const gitman::cmd_target_facts* facts = nullptr)
    {
        cmd_run run {};
        run.assembly = gitman::assemble_cmd_command_line(value(cmd_program_path()), arguments, {}, facts);
        REQUIRE_FALSE(run.assembly.rejection.has_value());

        gitman::function_command command {};
        command.program = cmd_program_path();
        for (const gitman::cmd_argument& argument : arguments)
            command.arguments.push_back(argument.text());
        command.working_directory = working_directory.u8string();
        command.command_line = run.assembly.text;
        const gitman::process_request request { std::get<gitman::process_request>(gitman::make_function_command(std::move(command), gitman::function_run_mode::capture, 60, {})) };
        REQUIRE(gitman::validate_process_request(request).empty());

        const std::unique_ptr<gitman::process_runner> runner { gitman::win32::make_process_runner() };
        recording_sink sink {};
        const gitman::process_cancellation_token token {};
        run.result = runner->run(request, &sink, token);
        run.lines = std::move(sink.lines);
        run.errors = std::move(sink.errors);
        return run;
    }

    bool has_warning(const gitman::cmd_command_line& assembly, const gitman::cmd_warning_kind kind, const std::size_t argument)
    {
        for (const gitman::cmd_warning& warning : assembly.warnings)
        {
            if (warning.kind == kind && warning.argument == std::optional<std::size_t> { argument })
                return true;
        }
        return false;
    }

    std::u8string bracketed(const std::u8string_view text)
    {
        return u8"[" + std::u8string { text } + u8"]";
    }

    bool has_kind(const gitman::cmd_correction& correction, const gitman::cmd_correction_kind kind)
    {
        for (const gitman::cmd_correction_kind found : correction.kinds)
        {
            if (found == kind)
                return true;
        }
        return false;
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
    // 출처를 모르는 값(변수로 치환한 값)은 명령 문장으로 넘기지 않는다. 공백이 든 값 한 덩어리는 감싸져 한 토큰이다.
    REQUIRE(cmd_line({ u8"/c", u8"echo hi" }) == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"echo hi\"\"");
    // cmd는 argv[0] 안의 `/cmd.exe`도 `/c`로 읽으므로 argv[0]은 `\`로 쓴다.
    REQUIRE(gitman::build_cmd_command_line(u8"C:/Windows/System32/cmd.exe", std::vector<std::u8string> { u8"/d", u8"x" })
        == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"x\""
    );

    const std::vector<std::u8string> arguments { u8"/c", u8"a&b" };
    REQUIRE(gitman::build_process_command_line(system_cmd, arguments, gitman::command_line_syntax::cmd) == cmd_line(arguments));
    REQUIRE(gitman::build_process_command_line(system_cmd, arguments, gitman::command_line_syntax::argv) == gitman::build_windows_command_line(system_cmd, arguments));
}

TEST_CASE("cmd assembly corrects metacharacters, percent and quotes and lists each change", "[function_buttons][r4]")
{
    // 공백·`, ; =`만 감싸는 것은 보통 조립이라 보정이 아니다. 메타 문자를 따옴표 안에 넣은 것만 보정이다.
    const gitman::cmd_command_line plain { assemble(arguments_of({ u8"/d", u8"/c", u8"C:\\OneDrive - x\\run.bat", u8"k=v", u8"" }, false)) };
    REQUIRE(plain.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"\"C:\\OneDrive - x\\run.bat\" \"k=v\" \"\"\"");
    REQUIRE(plain.corrections.empty());
    REQUIRE(plain.warnings.empty());
    REQUIRE_FALSE(plain.needs_approval());

    const gitman::cmd_command_line meta { assemble(arguments_of({ u8"/c", u8"run.bat", u8"main&whoami" }, false)) };
    REQUIRE(meta.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"run.bat \"main&whoami\"\"");
    REQUIRE(meta.corrections
        == std::vector<gitman::cmd_correction> { { std::optional<std::size_t> { 2u }, u8"main&whoami", u8"\"main&whoami\"", { gitman::cmd_correction_kind::metacharacter } } }
    );
    REQUIRE(meta.needs_approval());

    // `%`는 조각마다 감싸고 그 사이를 따옴표 밖의 `^%`로 쓴다. 펼칠 이름이 `"`로 시작하거나 `^`로 끝나 어떤 변수와도
    // 맞지 않고 cmd가 `^`를 지운다 (2026-11-03 실측).
    const gitman::cmd_command_line percent { assemble(arguments_of({ u8"/c", u8"run.bat", u8"a%PATH%b", u8"100%", u8"%PATH:a=b%" }, false)) };
    REQUIRE(percent.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"run.bat \"a\"^%\"PATH\"^%\"b\" \"100\"^%\"\" \"\"^%\"PATH:a=b\"^%\"\"\"");
    REQUIRE(percent.corrections.size() == 3u);
    REQUIRE(percent.corrections[0].argument == std::optional<std::size_t> { 2u });
    REQUIRE(percent.corrections[0].kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::percent });

    // `"`는 감싼 토큰 안에서 `""`로 겹쳐 따옴표 짝을 맞춘다. 앞의 `\`는 두 배로 늘린다.
    const gitman::cmd_command_line quote { assemble(arguments_of({ u8"/c", u8"run.bat", u8"say \"hi\"&x", u8"a\\\"b" }, false)) };
    REQUIRE(quote.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"run.bat \"say \"\"hi\"\"&x\" \"a\\\\\"\"b\"\"");
    REQUIRE(quote.corrections.size() == 2u);
    // 공백 때문에 어차피 감싸는 토큰의 `&`는 보정으로 세지 않는다. `"`는 감싸기만으로 막지 못해 센다.
    REQUIRE(quote.corrections[0].kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::quote });
    REQUIRE(quote.corrections[1].kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::quote });

    // 셋이 한 값에 섞여도 각 규칙이 겹친다.
    const gitman::cmd_command_line mixed { assemble(arguments_of({ u8"/c", u8"run.bat", u8"a\"%X%&\\" }, false)) };
    REQUIRE(mixed.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"run.bat \"a\"\"\"^%\"X\"^%\"&\\\\\"\"");

    REQUIRE_FALSE(gitman::cmd_correction_reason(meta.corrections[0].kinds).empty());
}

TEST_CASE("cmd assembly turns off delayed expansion for a value with ! unless the author chose it", "[function_buttons][r4]")
{
    const gitman::cmd_command_line inserted { assemble(arguments_of({ u8"/d", u8"/c", u8"run.bat", u8"C:\\R&D (x86) !bang" }, false)) };
    REQUIRE(inserted.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /v:off /s /c \"run.bat \"C:\\R&D (x86) !bang\"\"");
    // 공백이 든 경로라 괄호·`&`를 감싼 것은 보통 조립이다. 보정은 넣은 `/v:off` 하나다.
    REQUIRE(inserted.corrections == std::vector<gitman::cmd_correction> { { std::nullopt, u8"", u8"/v:off", { gitman::cmd_correction_kind::delayed_expansion } } });
    REQUIRE(inserted.warnings.empty());

    // 작성자가 `/v:off`를 적었으면 넣을 것이 없다. `/v:on`이면 무력화하지 못한 글자로 경고한다.
    const gitman::cmd_command_line off { assemble(arguments_of({ u8"/V:OFF", u8"/c", u8"run.bat", u8"a!b" }, false)) };
    REQUIRE(off.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /V:OFF /s /c \"run.bat a!b\"");
    REQUIRE_FALSE(off.needs_approval());

    const gitman::cmd_command_line on { assemble(arguments_of({ u8"/v:on", u8"/c", u8"run.bat", u8"a!b" }, false)) };
    REQUIRE(on.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /v:on /s /c \"run.bat a!b\"");
    REQUIRE(on.corrections.empty());
    REQUIRE(on.warnings == std::vector<gitman::cmd_warning> { { gitman::cmd_warning_kind::delayed_expansion_enabled, u8"", u8"", std::optional<std::size_t> { 3u }, u8'!' } });
    REQUIRE(on.needs_approval());
    REQUIRE_FALSE(gitman::cmd_warning_message(on.warnings.front()).empty());
}

TEST_CASE("cmd assembly passes a literal argument with spaces as a command sentence and still corrects its variables", "[function_buttons][r4]")
{
    // `["/c", "echo hi"]`는 R1에서 명령 이름 전체가 되어 실패했다. 글자 그대로 쓴 공백이 든 첫 인자는 명령 문장이다.
    const gitman::cmd_command_line echo { assemble(arguments_of({ u8"/c", u8"echo hi" }, true)) };
    REQUIRE(echo.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"echo hi\"");
    REQUIRE(echo.corrections == std::vector<gitman::cmd_correction> { { std::optional<std::size_t> { 1u }, u8"echo hi", u8"echo hi", { gitman::cmd_correction_kind::sentence } } });

    // 문장 안의 변수 구간은 토큰처럼 감싼다. 작성자의 따옴표 안이면 감싸지 않고 `"`·`%`만 고친다.
    const std::vector<gitman::cmd_argument> mixed {
        { { literal(u8"/d") } },
        { { literal(u8"/c") } },
        { { literal(u8"dir /b "), value(u8"C:\\R&D"), literal(u8" & echo \""), value(u8"a\"b%c"), literal(u8"\"") } },
        { { value(u8"x y") } },
    };
    const gitman::cmd_command_line sentence { assemble(mixed) };
    REQUIRE(sentence.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"dir /b \"C:\\R&D\" & echo \"a\"\"b\"^%\"c\" \"x y\"\"");
    REQUIRE(sentence.corrections.size() == 1u);
    REQUIRE(sentence.corrections.front().argument == std::optional<std::size_t> { 2u });
    REQUIRE(sentence.corrections.front().kinds
        == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::sentence, gitman::cmd_correction_kind::metacharacter, gitman::cmd_correction_kind::percent, gitman::cmd_correction_kind::quote }
    );

    // 문장에 글자 그대로 쓴 `%`·`!`는 작성자의 cmd 문법이라 그대로 두고 경고한다.
    const gitman::cmd_command_line path { assemble(arguments_of({ u8"/c", u8"echo %PATH%" }, true)) };
    REQUIRE(path.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"echo %PATH%\"");
    REQUIRE(path.warnings == std::vector<gitman::cmd_warning> { { gitman::cmd_warning_kind::sentence_expansion, u8"", u8"", std::optional<std::size_t> { 1u }, u8'%' } });

    // 절대 경로로 시작하는 글자 그대로의 인자는 경로다. 명령 문장이 아니고 둘째 인자부터는 늘 토큰이다.
    const gitman::cmd_command_line program_files { assemble(arguments_of({ u8"/c", u8"C:\\Program Files\\x.exe", u8"a b" }, true)) };
    REQUIRE(program_files.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"C:\\Program Files\\x.exe\" \"a b\"\"");
    REQUIRE(program_files.corrections.empty());
}

TEST_CASE("cmd assembly warns about exported values and refuses only line breaks", "[function_buttons][r4]")
{
    const std::vector<gitman::cmd_exported_value> exported { { u8"GITMAN_BRANCH", u8"main&whoami" }, { u8"GITMAN_REPOSITORY_ROOT", u8"C:\\work\\frontend" }, { u8"GITMAN_REVISION", u8"x==x,md" } };
    const gitman::cmd_command_line warned { assemble(arguments_of({ u8"/c", u8"run.bat" }, false), exported) };
    REQUIRE(warned.corrections.empty());
    REQUIRE(warned.warnings
        == std::vector<gitman::cmd_warning> {
            { gitman::cmd_warning_kind::exported_environment, u8"GITMAN_BRANCH", u8"main&whoami", std::nullopt, u8'&' },
            { gitman::cmd_warning_kind::exported_environment, u8"GITMAN_REVISION", u8"x==x,md", std::nullopt, u8'=' },
        }
    );
    REQUIRE(gitman::cmd_warning_message(warned.warnings.front()).find(u8"GITMAN_BRANCH") != std::u8string::npos);

    for (const std::u8string_view broken : { u8"a\nb", u8"a\rb" })
    {
        const gitman::cmd_command_line refused { assemble(arguments_of({ u8"/c", u8"run.bat", broken }, false)) };
        REQUIRE(refused.rejection == std::optional<gitman::cmd_rejection> { gitman::cmd_rejection { 2u, broken[1] } });
    }

    // 표시용 글은 같은 결정으로 짓되 `${env:}` 값 대신 이름을 쓴다. 보정 목록에도 값이 남지 않는다.
    std::vector<gitman::cmd_argument> secret { { { literal(u8"/c") } }, { { literal(u8"run.bat") } }, { { gitman::cmd_text_piece { u8"tok&en", false, u8"${env:TOKEN}" } } } };
    const gitman::cmd_command_line masked { assemble(secret) };
    REQUIRE(masked.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"run.bat \"tok&en\"\"");
    REQUIRE(masked.display_text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"run.bat \"${env:TOKEN}\"\"");
    REQUIRE(masked.corrections.front().original == u8"${env:TOKEN}");
    REQUIRE(masked.corrections.front().corrected == u8"\"${env:TOKEN}\"");
}

TEST_CASE("cmd.exe runs batches in R&D and OneDrive folders and the child receives every corrected value unchanged", "[function_buttons][r1][r4]")
{
    const cmd_folders folders {};
    const gitman::variable_lookup lookup { make_lookup(folders, injected_branch) };

    const std::vector<std::u8string> templates {
        u8"/d", u8"/c", u8"${workspaceFolder}\\run.bat", u8"${branch}", u8"a^b", u8"(x)", u8"p|q", u8"<>", u8"two words", u8"한글", u8"", u8"${projectRoot}", u8"k=v", u8"a,b;c",
        u8"a%PATH%b", u8"100%", u8"say \"hi\"&echo.>INJECTED", u8"a!PATH!b", u8"tail\\",
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
            u8"[a%PATH%b]",
            u8"[100%]",
            u8"[say \"hi\"&echo.>INJECTED]",
            u8"[a!PATH!b]",
            u8"[tail\\]",
        }
    );
    REQUIRE_FALSE(folders.injected());
    // 브랜치의 `&`는 보정되고, 배치가 읽을 `GITMAN_BRANCH`는 경고로 남는다 (D26을 D30이 경고로 바꿈).
    REQUIRE(research.assembly.needs_approval());
    REQUIRE(research.assembly.warnings.front() == gitman::cmd_warning { gitman::cmd_warning_kind::exported_environment, u8"GITMAN_BRANCH", std::u8string { injected_branch }, std::nullopt, u8'&' });

    const cmd_run onedrive { run_through_cmd(lookup, { u8"/d", u8"/c", u8"${projectRoot}\\run.bat", u8"${workspaceFolder}", u8"${branch}" }) };
    INFO(reinterpret_cast<const char*>(onedrive.display_command_line.c_str()));
    REQUIRE(onedrive.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(onedrive.lines == std::vector<std::u8string> { bracketed(folders.research().u8string()), bracketed(injected_branch) });
    REQUIRE_FALSE(folders.injected());
}

TEST_CASE("cmd.exe runs a program directly with corrected values and the display line is the real one", "[function_buttons][r1][r4]")
{
    const cmd_folders folders {};
    const gitman::variable_lookup lookup { make_lookup(folders, injected_branch) };

    const cmd_run direct { run_through_cmd(lookup, { u8"/d", u8"/c", u8"${workspaceFolder}\\gitman_process_test_child.exe", u8"echo-args", u8"${branch}", u8"${workspaceFolder}\\", u8"x\"%PATH%\"y" }) };
    REQUIRE(direct.result.exit_code == std::optional<std::int32_t> { 0 });
    // 끝 `\`는 닫는 따옴표 앞에서 두 배가 되어 자식에게는 하나로 닿는다.
    REQUIRE(direct.lines == std::vector<std::u8string> { bracketed(injected_branch), bracketed(folders.research().u8string() + u8"\\"), u8"[x\"%PATH%\"y]" });
    REQUIRE_FALSE(folders.injected());

    // 로그 시작 줄·미리보기의 명령줄이 runner가 실제로 쓴 명령줄(가린 값)과 같다 (3.6).
    REQUIRE(direct.display_command_line == direct.result.masked_command_line);
    REQUIRE(direct.display_command_line == direct.assembly.text);
    REQUIRE(direct.display_command_line.find(u8" /d /s /c \"\"") != std::u8string::npos);
}

TEST_CASE("cmd.exe runs the three cases R1 broke once the corrections are approved", "[function_buttons][r4]")
{
    const cmd_folders folders {};

    // 1. 글자 그대로의 명령 문장 `["/c", "echo hi"]`이 다시 동작한다.
    const gitman::variable_lookup plain { make_lookup(folders, u8"main") };
    const cmd_run echo { run_through_cmd(plain, { u8"/c", u8"echo hi" }) };
    REQUIRE(echo.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(echo.lines == std::vector<std::u8string> { u8"hi" });
    REQUIRE(echo.assembly.corrections.front().kinds.front() == gitman::cmd_correction_kind::sentence);

    // 문장 안의 브랜치 값은 보정되어 `whoami` 대신 넣은 표식 명령이 돌지 않는다. echo는 따옴표째 찍는다.
    const gitman::variable_lookup injected { make_lookup(folders, injected_branch) };
    const cmd_run sentence { run_through_cmd(injected, { u8"/d", u8"/c", u8"echo ${branch}" }) };
    REQUIRE(sentence.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(sentence.lines == std::vector<std::u8string> { u8"\"" + std::u8string { injected_branch } + u8"\"" });
    REQUIRE_FALSE(folders.injected());

    // 2·3. 저장소 루트에 `&`·괄호·`!`가 있다. 배치가 따옴표 안에서 읽는 환경 변수와 인자가 그대로 닿는다.
    const gitman::variable_lookup bang { make_lookup(folders, injected_branch, &folders.bang()) };
    const cmd_run root { run_through_cmd(bang, { u8"/d", u8"/c", u8"${projectRoot}\\root.bat", u8"${repositoryRoot}", u8"${projectRoot}\\" }) };
    INFO(reinterpret_cast<const char*>(root.display_command_line.c_str()));
    REQUIRE(root.result.exit_code == std::optional<std::int32_t> { 0 });
    const std::u8string bang_path { folders.bang().u8string() };
    REQUIRE(root.lines == std::vector<std::u8string> { bracketed(bang_path), bracketed(injected_branch), bracketed(bang_path), bracketed(bang_path + u8"\\") });
    REQUIRE_FALSE(folders.injected());
    // `!` 때문에 `/v:off`를 넣고, 저장소 루트·브랜치 환경 변수는 경고다.
    REQUIRE(root.display_command_line.find(u8" /d /v:off /s /c ") != std::u8string::npos);
    bool root_warning { false };
    for (const gitman::cmd_warning& warning : root.assembly.warnings)
        root_warning = root_warning || warning.environment == u8"GITMAN_REPOSITORY_ROOT";
    REQUIRE(root_warning);
    REQUIRE(has_kind(root.assembly.corrections.front(), gitman::cmd_correction_kind::delayed_expansion));
}

TEST_CASE("cmd.exe written with forward slashes runs the requested command, not md", "[function_buttons][r1]")
{
    const cmd_folders folders {};
    const gitman::variable_lookup lookup { make_lookup(folders, injected_branch) };

    std::u8string forward { cmd_program_path() };
    for (char8_t& character : forward)
    {
        if (character == u8'\\')
            character = u8'/';
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

TEST_CASE("Only line breaks are refused whatever their origin; % ! and quotes are corrected", "[function_buttons][r1][r4]")
{
    const cmd_folders folders {};
    const gitman::variable_expansion program { gitman::expand_variables(system_cmd, {}) };

    for (const std::u8string_view branch : { u8"a\nb", u8"a\rb" })
    {
        const gitman::variable_lookup lookup { make_lookup(folders, branch) };
        const std::vector<gitman::variable_expansion> arguments { gitman::expand_variables(u8"/c", lookup), gitman::expand_variables(u8"echo", lookup), gitman::expand_variables(u8"${branch}", lookup) };
        const std::optional<gitman::cmd_injection_finding> finding { gitman::find_cmd_injection(program, arguments) };
        REQUIRE(finding.has_value());
        REQUIRE(finding->argument == std::optional<std::size_t> { 2u });
        REQUIRE(finding->origin == gitman::variable_origin::runtime);
        REQUIRE(finding->variable == u8"branch");
        REQUIRE(gitman::cmd_injection_message(*finding) == u8"cmd.exe에 넘기는 ${branch} 값에 cmd 명령줄로 넘길 수 없는 문자(줄바꿈)가 있습니다.");
    }

    for (const std::u8string_view branch : { u8"a\"b", u8"a%PATH%b", u8"100%", u8"a!b" })
    {
        INFO(reinterpret_cast<const char*>(std::u8string { branch }.c_str()));
        const gitman::variable_lookup lookup { make_lookup(folders, branch) };
        const std::vector<gitman::variable_expansion> arguments { gitman::expand_variables(u8"/c", lookup), gitman::expand_variables(u8"echo", lookup), gitman::expand_variables(u8"${branch}", lookup) };
        REQUIRE_FALSE(gitman::find_cmd_injection(program, arguments).has_value());
        REQUIRE(gitman::assemble_function_cmd_command_line(program, arguments, lookup.builtins).needs_approval());
    }

    // 문서 경로 구간의 `%`도 보정한다.
    gitman::project_definition project {};
    project.id = { u8"frontend" };
    project.path.original = u8"D:\\100%\\frontend";
    gitman::variable_lookup document {};
    document.builtins = gitman::make_builtin_variables(u8"D:\\100%\\team.version-list", project, nullptr);
    const std::vector<gitman::variable_expansion> path { gitman::expand_variables(u8"/c", document), gitman::expand_variables(u8"${workspaceFolder}\\run.bat", document) };
    REQUIRE_FALSE(gitman::find_cmd_injection(program, path).has_value());
    REQUIRE(gitman::assemble_function_cmd_command_line(program, path, document.builtins).text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"D:\\100\"^%\"\\run.bat\"\"");
}

TEST_CASE("A cmd request carrying a line break or a stray command line is an invalid request", "[function_buttons][r1][r4]")
{
    gitman::process_request request {};
    request.executable = system_cmd;
    request.working_directory = u8"C:\\work";
    request.maximum_captured_bytes_per_stream = 1024u;
    request.syntax = gitman::command_line_syntax::cmd;
    for (const std::u8string_view correctable : { u8"a&b", u8"a%b", u8"a!b", u8"a\"b" })
    {
        request.arguments = { u8"/c", u8"echo", std::u8string { correctable } };
        REQUIRE(gitman::validate_process_request(request).empty());
    }

    for (const std::u8string_view value : { u8"a\nb", u8"a\rb" })
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

    // 미리 지은 명령줄은 cmd 요청에만 있고 줄바꿈을 담을 수 없다.
    request.arguments = { u8"/c", u8"echo" };
    request.command_line = u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"echo\nx\"";
    REQUIRE_FALSE(gitman::validate_process_request(request).empty());
    request.command_line = u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"echo hi\"";
    REQUIRE(gitman::validate_process_request(request).empty());
    request.syntax = gitman::command_line_syntax::argv;
    REQUIRE_FALSE(gitman::validate_process_request(request).empty());

    // 같은 값도 argv 규칙이면 셸을 거치지 않으므로 받는다.
    request.command_line.clear();
    request.arguments = { u8"/c", u8"echo", u8"a\nb" };
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Exported runtime values with cmd metacharacters are warnings, not refusals", "[function_buttons][r1][r4]")
{
    const cmd_folders folders {};
    const gitman::variable_expansion program { gitman::expand_variables(system_cmd, {}) };
    // `x==x,md,INJECTED`는 git이 받는 브랜치 이름이다. 배치의 `if %GITMAN_BRANCH%==main …`에서 `==`가 비교를 일찍
    // 끝내고 `,`·`;`·`=`가 뒤를 `md INJECTED`로 가른다(2026-11-03 실측). 그래서 경고에 넣는다.
    for (const std::u8string_view branch : { u8"main&whoami", u8"a|b", u8"a<b", u8"a>b", u8"a^b", u8"a(b", u8"a)b", u8"a%b", u8"a!b", u8"a\"b", u8"x==x,md,INJECTED", u8"x==x;md;INJECTED", u8"a=b", u8"a,b", u8"a;b" })
    {
        INFO(reinterpret_cast<const char*>(std::u8string { branch }.c_str()));
        const gitman::variable_lookup lookup { make_lookup(folders, branch) };
        const std::vector<gitman::variable_expansion> arguments { gitman::expand_variables(u8"/c", lookup), gitman::expand_variables(u8"run.bat", lookup) };
        const gitman::cmd_command_line assembly { gitman::assemble_function_cmd_command_line(program, arguments, lookup.builtins) };
        REQUIRE_FALSE(assembly.rejection.has_value());
        REQUIRE(assembly.warnings.size() == 1u);
        REQUIRE(assembly.warnings.front().kind == gitman::cmd_warning_kind::exported_environment);
        REQUIRE(assembly.warnings.front().environment == u8"GITMAN_BRANCH");
    }

    // 저장소 루트(런타임)도 같다. 문서 경로(`GITMAN_WORKSPACE_FOLDER`의 `R&D`)는 문서 작성자의 것이라 보지 않는다.
    gitman::project_definition project {};
    project.id = { u8"frontend" };
    project.path.original = u8"C:\\work\\frontend";
    gitman::repository_snapshot snapshot {};
    snapshot.kind = gitman::repository_kind::git;
    snapshot.repository_root = u8"C:\\work\\R&D";
    snapshot.current_reference = u8"main";
    snapshot.local_revision = u8"a1b2c3d";
    const std::vector<gitman::variable_binding> root { gitman::make_builtin_variables(u8"C:\\R&D\\team.version-list", project, &snapshot) };
    const std::vector<gitman::cmd_exported_value> exported { gitman::function_cmd_exported_values(root) };
    REQUIRE(exported.size() == 4u);
    const std::vector<gitman::variable_expansion> arguments { gitman::expand_variables(u8"/c", {}), gitman::expand_variables(u8"run.bat", {}) };
    const gitman::cmd_command_line assembly { gitman::assemble_function_cmd_command_line(program, arguments, root) };
    REQUIRE(assembly.warnings == std::vector<gitman::cmd_warning> { { gitman::cmd_warning_kind::exported_environment, u8"GITMAN_REPOSITORY_ROOT", u8"C:\\work\\R&D", std::nullopt, u8'&' } });

    const gitman::variable_lookup plain { make_lookup(folders, u8"feature/login-2") };
    REQUIRE_FALSE(gitman::assemble_function_cmd_command_line(program, arguments, plain.builtins).needs_approval());
}

TEST_CASE("A path with spaces that also has parentheses or & is ordinary quoting, not a correction", "[function_buttons][r4]")
{
    // 공백 때문에 어차피 감싸므로 결과가 보통 조립과 같다. 설명서 12.5대로 묻지 않는다 (R4 검토).
    for (const std::u8string_view path : { u8"C:\\Program Files (x86)\\Tool\\run.bat", u8"D:\\R&D team\\run.bat" })
    {
        INFO(reinterpret_cast<const char*>(std::u8string { path }.c_str()));
        const gitman::cmd_command_line assembly { assemble(arguments_of({ u8"/c", path }, false)) };
        REQUIRE(assembly.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"" + std::u8string { path } + u8"\"\"");
        REQUIRE_FALSE(assembly.needs_approval());
    }
    // 공백이 없으면 따옴표가 `&`를 막는 보정이다.
    const gitman::cmd_command_line research { assemble(arguments_of({ u8"/c", u8"D:\\R&D\\run.bat" }, false)) };
    REQUIRE(research.corrections.size() == 1u);
    REQUIRE(research.corrections.front().kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::metacharacter });
}

TEST_CASE("A command sentence is told from a path by the executable extension and a path continued after a variable", "[function_buttons][r4]")
{
    // 변수 값(절대 경로) 뒤에 `\`로 이어 쓴 경로는 공백이 있어도 한 토큰이다. R1~R3처럼 경로 하나로 넘긴다.
    const std::vector<gitman::cmd_argument> continued { { { literal(u8"/c") } }, { { value(u8"C:\\Repo (x86)"), literal(u8"\\Build Tools\\build.bat") } } };
    const gitman::cmd_command_line path { assemble(continued) };
    REQUIRE(path.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"C:\\Repo (x86)\\Build Tools\\build.bat\"\"");
    REQUIRE(path.corrections.empty());

    // 변수 값 뒤가 공백으로 시작하면 문장이다(`${tool} --flag`).
    const std::vector<gitman::cmd_argument> flagged { { { literal(u8"/c") } }, { { value(u8"C:\\tools\\x.exe"), literal(u8" --flag") } } };
    const gitman::cmd_command_line tool { assemble(flagged) };
    REQUIRE(tool.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"C:\\tools\\x.exe --flag\"");
    REQUIRE(tool.corrections.front().kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::sentence });

    // 글자 그대로 쓴 절대 경로는 실행 확장자 뒤에 공백과 글이 더 있으면 문장, 확장자로 끝나면 경로다.
    const gitman::cmd_command_line arguments_after { assemble(arguments_of({ u8"/c", u8"C:\\tools\\x.exe echo-args one" }, true)) };
    REQUIRE(arguments_after.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"C:\\tools\\x.exe echo-args one\"");
    REQUIRE(arguments_after.corrections.front().kinds.front() == gitman::cmd_correction_kind::sentence);
    for (const std::u8string_view batch : { u8"C:\\Program Files\\Tool\\RUN.BAT", u8"C:\\Program Files\\Tool\\run.cmd", u8"C:\\Program Files\\Tool\\x.com" })
    {
        const gitman::cmd_command_line single { assemble(arguments_of({ u8"/c", batch }, true)) };
        REQUIRE(single.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"" + std::u8string { batch } + u8"\"\"");
        REQUIRE(single.corrections.empty());
    }
}

TEST_CASE("cmd.exe runs a batch under a folder with spaces after a variable and a written path followed by arguments", "[function_buttons][r4]")
{
    const cmd_folders folders {};
    const std::filesystem::path tools { folders.research() / L"Build Tools" };
    REQUIRE(std::filesystem::create_directory(tools));
    cmd_folders::write_batch(tools / L"run.bat", "@\"%~dp0..\\gitman_process_test_child.exe\" echo-args %*\r\n");

    // R4 첫 구현은 이 정의를 문장으로 넘겨 `Build`에서 명령 이름이 끊겼다.
    const gitman::variable_lookup lookup { make_lookup(folders, u8"main") };
    const cmd_run batch { run_through_cmd(lookup, { u8"/d", u8"/c", u8"${workspaceFolder}\\Build Tools\\run.bat", u8"x y" }) };
    INFO(reinterpret_cast<const char*>(batch.display_command_line.c_str()));
    REQUIRE(batch.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(batch.lines == std::vector<std::u8string> { u8"[x y]" });
    REQUIRE_FALSE(batch.assembly.needs_approval());

    // 절대 경로 + 인자를 한 글로 쓴 정의는 R1 전처럼 동작한다. 문장은 cmd가 공백에서 가르므로 공백 없는 경로여야 한다.
    const std::u8string child { (module_directory() / L"gitman_process_test_child.exe").u8string() };
    if (child.find(u8' ') != std::u8string::npos)
        SKIP("test 실행 파일 경로에 공백이 있어 글자 그대로의 문장 test를 건너뜁니다");
    const cmd_run written { run_assembled(arguments_of({ u8"/c", child + u8" echo-args one two" }, true), folders.onedrive()) };
    INFO(reinterpret_cast<const char*>(written.assembly.text.c_str()));
    REQUIRE(written.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(written.lines == std::vector<std::u8string> { u8"[one]", u8"[two]" });
}

TEST_CASE("A value inside the author's quotes keeps a trailing backslash before the closing quote", "[function_buttons][r4]")
{
    const cmd_folders folders {};
    const std::u8string child { (module_directory() / L"gitman_process_test_child.exe").u8string() };
    if (child.find(u8' ') != std::u8string::npos)
        SKIP("test 실행 파일 경로에 공백이 있어 글자 그대로의 문장 test를 건너뜁니다");

    // `"${projectRoot}"`의 값이 드라이브 루트(`D:\`)다. 끝 `\`를 늘리지 않으면 받는 쪽이 `D:"`로 읽는다 (R4 검토).
    const std::vector<gitman::cmd_argument> arguments {
        { { literal(u8"/c") } },
        { { literal(child + u8" echo-args \""), value(u8"D:\\"), literal(u8"\" tail \""), value(u8"C:\\a b\\"), literal(u8"\"") } },
    };
    const cmd_run run { run_assembled(arguments, folders.onedrive()) };
    INFO(reinterpret_cast<const char*>(run.assembly.text.c_str()));
    REQUIRE(run.assembly.text.ends_with(u8" echo-args \"D:\\\\\" tail \"C:\\a b\\\\\"\""));
    REQUIRE(run.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(run.lines == std::vector<std::u8string> { u8"[D:\\]", u8"[tail]", u8"[C:\\a b\\]" });
}

TEST_CASE("A value the command sentence leaves unprotected is a warning, not a claimed correction", "[function_buttons][r4]")
{
    // 글자 그대로 쓴 `^`로 끝난 바로 뒤의 값이다. cmd가 여는 따옴표를 글자로 읽어 `&`가 따옴표 밖에 남는다 (R4 보안 검토 실측).
    const std::vector<gitman::cmd_argument> caret { { { literal(u8"/c") } }, { { literal(u8"echo x ^"), value(u8"a&echo.>INJECTED") } } };
    const gitman::cmd_command_line escaped { assemble(caret) };
    REQUIRE(escaped.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"echo x ^\"a&echo.>INJECTED\"\"");
    REQUIRE(escaped.corrections.size() == 1u);
    REQUIRE(escaped.corrections.front().kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::sentence });
    REQUIRE(escaped.warnings == std::vector<gitman::cmd_warning> { { gitman::cmd_warning_kind::unprotected_value, u8"", u8"", std::optional<std::size_t> { 1u }, u8'^' } });
    REQUIRE(gitman::cmd_warning_message(escaped.warnings.front()).find(u8"보호되지 않습니다") != std::u8string::npos);

    // 명령 문장이 따옴표를 연 채로 끝났다. 다음 토큰을 감싼 따옴표가 그 따옴표를 닫는다. 고칠 것이 없는 토큰은 괜찮다.
    const std::vector<gitman::cmd_argument> open {
        { { literal(u8"/c") } },
        { { literal(u8"\"C:\\tools\\child.exe\" echo-args \"x") } },
        { { literal(u8"plain") } },
        { { value(u8"a&echo.>INJECTED") } },
        { { value(u8"b%PATH%") } },
    };
    const gitman::cmd_command_line opened { assemble(open) };
    REQUIRE(opened.corrections.size() == 1u);
    REQUIRE(opened.corrections.front().argument == std::optional<std::size_t> { 1u });
    REQUIRE_FALSE(has_warning(opened, gitman::cmd_warning_kind::unprotected_value, 2u));
    REQUIRE(has_warning(opened, gitman::cmd_warning_kind::unprotected_value, 3u));
    REQUIRE(has_warning(opened, gitman::cmd_warning_kind::unprotected_value, 4u));
    REQUIRE(opened.needs_approval());

    // 따옴표 짝이 맞는 문장 뒤의 토큰은 그대로 보정이다.
    const std::vector<gitman::cmd_argument> closed { { { literal(u8"/c") } }, { { literal(u8"echo \"x\"") } }, { { value(u8"a&b") } } };
    const gitman::cmd_command_line balanced { assemble(closed) };
    REQUIRE(balanced.warnings.empty());
    REQUIRE(balanced.corrections.back().kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::metacharacter });
}

TEST_CASE("The ! texts say when cmd expands it", "[function_buttons][r4]")
{
    const gitman::cmd_command_line bang { assemble(arguments_of({ u8"/c", u8"echo !GMX!" }, true)) };
    REQUIRE(gitman::cmd_warning_message(bang.warnings.front()).find(u8"지연 확장이 켜져 있으면") != std::u8string::npos);
    const gitman::cmd_correction_kind off[] { gitman::cmd_correction_kind::delayed_expansion };
    REQUIRE(gitman::cmd_correction_reason(off).find(u8"배치가 지연 확장을 켜면") != std::u8string::npos);
}

TEST_CASE("The cmd target query names the batch file and the relative path the worker must check", "[function_buttons][s3]")
{
    // 변수로 시작하는 배치 경로다. 글자만으로 경로로 정해지고, 부를 배치가 그 경로다.
    const std::vector<gitman::cmd_argument> continued { { { literal(u8"/d") } }, { { literal(u8"/c") } }, { { value(u8"D:\\R&D"), literal(u8"\\tools\\Run.CMD") } }, { { value(u8"x") } } };
    const std::optional<gitman::cmd_target_query> path { gitman::find_cmd_target_query(continued) };
    REQUIRE(path.has_value());
    REQUIRE(path->argument == 2u);
    REQUIRE(path->sentence == false);
    REQUIRE(path->relative_path.has_value() == false);
    REQUIRE(path->batch_if_path == u8"D:\\R&D\\tools\\Run.CMD");
    REQUIRE(path->batch_if_sentence.empty());

    // 공백이 든 상대 경로는 글자만으로는 문장이다. 작업 폴더에서 그 파일이 있는지 worker가 본다.
    const std::optional<gitman::cmd_target_query> relative { gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"tools\\Build Tools\\run.bat", u8"y" }, true)) };
    REQUIRE(relative.has_value());
    REQUIRE(relative->argument == 1u);
    REQUIRE(relative->sentence);
    REQUIRE(relative->relative_path == std::optional<std::u8string> { u8"tools\\Build Tools\\run.bat" });
    REQUIRE(relative->batch_if_path == u8"tools\\Build Tools\\run.bat");
    REQUIRE(relative->batch_if_sentence.empty());
    REQUIRE(gitman::cmd_target_batch_path(*relative, true) == u8"tools\\Build Tools\\run.bat");
    REQUIRE(gitman::cmd_target_batch_path(*relative, false).empty());

    // 문장이면 첫 토큰(따옴표면 따옴표 안)이 부를 배치다. 경로 꼴이 아닌 문장은 있는지 보지 않는다.
    const std::optional<gitman::cmd_target_query> sentence { gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"build.bat --all & echo done" }, true)) };
    REQUIRE(sentence.has_value());
    REQUIRE(sentence->sentence);
    REQUIRE(sentence->relative_path.has_value() == false);
    REQUIRE(sentence->batch_if_sentence == u8"build.bat");
    const std::optional<gitman::cmd_target_query> quoted { gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"\"C:\\a b\\x.cmd\" y" }, true)) };
    REQUIRE(quoted.has_value());
    REQUIRE(quoted->relative_path.has_value() == false);
    REQUIRE(quoted->batch_if_sentence == u8"C:\\a b\\x.cmd");

    // 확인할 것이 없으면 값이 없다. worker를 거치지 않고 바로 판정한다.
    REQUIRE(gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"echo hi" }, true)).has_value() == false);
    REQUIRE(gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"C:\\tools\\x.exe" }, true)).has_value() == false);
    REQUIRE(gitman::find_cmd_target_query(arguments_of({ u8"/d", u8"/c" }, true)).has_value() == false);

    // 상대 경로는 작업 폴더에 잇는다. 절대 경로는 그대로다.
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws\\", u8"tools\\run.bat") == u8"D:\\ws\\tools\\run.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"tools\\run.bat") == u8"D:\\ws\\tools\\run.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"C:\\x.bat") == u8"C:\\x.bat");
}

TEST_CASE("A batch file scan finds setlocal EnableDelayedExpansion and call outside comments", "[function_buttons][s3]")
{
    REQUIRE(gitman::scan_cmd_batch_text("@echo off\r\nSETLOCAL EnableExtensions   enabledelayedexpansion\r\necho %*\r\n") == gitman::cmd_batch_findings { true, false, false });
    REQUIRE(gitman::scan_cmd_batch_text("@echo off\r\n  call \"%~dp0next.bat\" %*\r\n") == gitman::cmd_batch_findings { false, true, false });
    REQUIRE(gitman::scan_cmd_batch_text("if 1==1 (@CALL :sub)\r\n") == gitman::cmd_batch_findings { false, true, false });
    REQUIRE(gitman::scan_cmd_batch_text("x.exe & call:label\n") == gitman::cmd_batch_findings { false, true, false });
    // 주석과 다른 낱말 안의 글은 세지 않는다.
    REQUIRE(gitman::scan_cmd_batch_text("rem call x.bat\r\n:: setlocal EnableDelayedExpansion\r\n@REM call y\r\nrecall.exe\r\nsetlocal\r\necho callback\r\n") == gitman::cmd_batch_findings {});
}

TEST_CASE("A batch file scan uses every cmd word boundary, carets, empty variables and a BOM", "[function_buttons][s3]")
{
    constexpr gitman::cmd_batch_findings delayed { true, false, false };
    constexpr gitman::cmd_batch_findings call { false, true, false };
    // cmd가 그대로 지연 확장을 켜는 줄이다(S3 검토, 실제 cmd.exe에서 `!GMX!`가 펼쳐짐).
    REQUIRE(gitman::scan_cmd_batch_text("setlocal EnableDelayedExpansion&echo hi\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("(setlocal EnableDelayedExpansion)\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("setlocal EnableDelayedExpansion>nul\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("setlocal,EnableDelayedExpansion\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("\xEF\xBB\xBFsetlocal EnableDelayedExpansion\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("set^local EnableDelayed^Expansion\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("%nothing%setlocal Enable%nothing%DelayedExpansion\r\n") == delayed);
    // 자식 cmd도 지연 확장을 켠다. `/v`만으로는 다른 명령의 스위치일 수 있어 같은 줄의 `cmd`를 본다.
    REQUIRE(gitman::scan_cmd_batch_text("cmd /v:on /c echo %~1\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("for /f %%i in ('cmd /d /v /c echo %~1') do echo %%i\r\n") == delayed);
    REQUIRE(gitman::scan_cmd_batch_text("findstr /v x y.txt\r\ncmd /v:off /c echo x\r\n") == gitman::cmd_batch_findings {});

    REQUIRE(gitman::scan_cmd_batch_text("call,echo %~1\r\n") == call);
    REQUIRE(gitman::scan_cmd_batch_text("call;echo %~1\r\n") == call);
    REQUIRE(gitman::scan_cmd_batch_text("call=echo %~1\r\n") == call);
    REQUIRE(gitman::scan_cmd_batch_text("call\x0B" "echo %~1\r\n") == call);
    REQUIRE(gitman::scan_cmd_batch_text("echo x&call\"x.bat\"\r\n") == call);
    REQUIRE(gitman::scan_cmd_batch_text("\xEF\xBB\xBF" "call x.bat\r\n") == call);
    REQUIRE(gitman::scan_cmd_batch_text("ca^ll x.bat\r\n") == call);
    REQUIRE(gitman::scan_cmd_batch_text("%nothing%call x.bat\r\n") == call);
    // `%…%` 구간을 지운 사본만 보면 사이의 낱말을 놓친다. 원래 줄도 본다.
    REQUIRE(gitman::scan_cmd_batch_text("echo %~1 call %~2\r\n") == call);
}

TEST_CASE("The cmd target path folds dot segments, roots and drives like Win32", "[function_buttons][s3]")
{
    // probe의 `\\?\`는 `.`·`..`를 풀지 않으므로 글자로 접어 넘긴다 (S3 검토).
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8".\\run.bat") == u8"D:\\ws\\run.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"./run.bat") == u8"D:\\ws\\run.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws\\sub", u8"..\\sub dir\\x.bat") == u8"D:\\ws\\sub dir\\x.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"tools/../x.bat") == u8"D:\\ws\\x.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws\\..\\ws\\", u8"x.bat") == u8"D:\\ws\\x.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"..\\..\\..\\x.bat") == u8"D:\\x.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"C:\\a\\..\\b.bat") == u8"C:\\b.bat");
    // 루트 상대는 작업 폴더의 드라이브나 share 뿌리, 같은 드라이브의 드라이브 상대는 작업 폴더 기준이다.
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws\\sub", u8"\\tools\\x.bat") == u8"D:\\tools\\x.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"\\\\host\\share\\ws", u8"\\x.bat") == u8"\\\\host\\share\\x.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"\\\\host\\share\\ws", u8"..\\..\\x.bat") == u8"\\\\host\\share\\x.bat");
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"d:x.bat") == u8"D:\\ws\\x.bat");
    // 다른 드라이브의 현재 폴더는 알 수 없다. 장치 경로는 정규화하지 않는다.
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"C:x.bat").empty());
    REQUIRE(gitman::cmd_target_absolute_path(u8"ws", u8"x.bat").empty());
    REQUIRE(gitman::cmd_target_absolute_path(u8"D:\\ws", u8"\\\\?\\C:\\a\\..\\x.bat") == u8"\\\\?\\C:\\a\\..\\x.bat");

    // 신뢰하기 전에는 드라이브 문자 경로만 읽는다. 읽지 않은 답은 배치 후보가 있으면 확인하지 못했다는 것이다.
    gitman::cmd_target_query query { *gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"run.bat" }, true)) };
    query.working_directory = u8"D:\\ws";
    REQUIRE(gitman::cmd_target_is_local(query));
    query.working_directory = u8"\\\\host\\share\\ws";
    REQUIRE(gitman::cmd_target_is_local(query) == false);
    const gitman::cmd_target_facts unread { gitman::cmd_target_unread_facts(query) };
    REQUIRE(unread.query == query);
    REQUIRE(unread.path_exists == false);
    REQUIRE(unread.batch == std::optional<gitman::cmd_batch_findings> { gitman::cmd_batch_findings { false, false, true } });
    gitman::cmd_target_query unc { *gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"\\\\host\\share\\x.bat" }, true)) };
    unc.working_directory = u8"D:\\ws";
    REQUIRE(gitman::cmd_target_is_local(unc) == false);

    // 명령 문장의 첫 토큰은 cmd가 건너뛰는 앞 글자와 `^`를 벗기고 본다.
    REQUIRE(gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"@run.bat x" }, true))->batch_if_sentence == u8"run.bat");
    REQUIRE(gitman::find_cmd_target_query(arguments_of({ u8"/c", u8"(run.bat x)" }, true))->batch_if_sentence == u8"run.bat");
    REQUIRE(gitman::find_cmd_target_query(arguments_of({ u8"/c", u8";^run.bat x" }, true))->batch_if_sentence == u8"run.bat");
}

TEST_CASE("Known cmd target facts decide a relative path and add batch warnings", "[function_buttons][s3]")
{
    const std::vector<gitman::cmd_argument> relative { arguments_of({ u8"/c", u8"tools\\Build Tools\\run.bat", u8"y" }, true) };
    gitman::cmd_target_facts facts {};
    facts.query = *gitman::find_cmd_target_query(relative);

    // 파일이 없으면 글자만의 판정 그대로 문장이다.
    const gitman::cmd_command_line missing { gitman::assemble_cmd_command_line(value(system_cmd), relative, {}, &facts) };
    REQUIRE(missing.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"tools\\Build Tools\\run.bat y\"");
    REQUIRE(missing.corrections.front().kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::sentence });

    // 작업 폴더에 그 파일이 있으면 경로 하나다. 공백 때문에 감싸는 보통 조립이라 보정이 아니다.
    facts.path_exists = true;
    const gitman::cmd_command_line existing { gitman::assemble_cmd_command_line(value(system_cmd), relative, {}, &facts) };
    REQUIRE(existing.text == u8"\"C:\\Windows\\System32\\cmd.exe\" /s /c \"\"tools\\Build Tools\\run.bat\" y\"");
    REQUIRE(existing.corrections.empty());
    REQUIRE(existing.needs_approval() == false);

    // 부를 배치가 지연 확장을 켜거나 call을 쓰면 경고다. 경고는 그 배치를 가리키는 인자의 것이다.
    facts.batch = gitman::cmd_batch_findings { true, true, false };
    const gitman::cmd_command_line warned { gitman::assemble_cmd_command_line(value(system_cmd), relative, {}, &facts) };
    REQUIRE(has_warning(warned, gitman::cmd_warning_kind::batch_delayed_expansion, 1u));
    REQUIRE(has_warning(warned, gitman::cmd_warning_kind::batch_call, 1u));
    REQUIRE(warned.needs_approval());
    for (const gitman::cmd_warning& warning : warned.warnings)
    {
        const std::u8string message { gitman::cmd_warning_message(warning) };
        INFO(reinterpret_cast<const char*>(message.c_str()));
        if (warning.kind == gitman::cmd_warning_kind::batch_delayed_expansion)
            REQUIRE(message == u8"args[1]이 부르는 배치 파일이 지연 확장을 켭니다(EnableDelayedExpansion, cmd /v:on). 넘긴 값의 !NAME!을 배치 안에서 cmd가 환경 변수로 펼칠 수 있습니다(/v:off로 막지 못합니다).");
        else
            REQUIRE(message == u8"args[1]이 부르는 배치 파일이 call을 씁니다. call로 다시 넘긴 값은 ^가 두 배가 되고 %가 한 번 더 펼쳐져 인자 경계가 바뀔 수 있습니다.");
    }

    // 너무 커서 읽지 않은 배치도 경고로 알린다.
    facts.batch = gitman::cmd_batch_findings { false, false, true };
    const gitman::cmd_command_line unchecked { gitman::assemble_cmd_command_line(value(system_cmd), relative, {}, &facts) };
    REQUIRE(has_warning(unchecked, gitman::cmd_warning_kind::batch_unchecked, 1u));
    REQUIRE(gitman::cmd_warning_message(unchecked.warnings.front()) == u8"args[1]이 부르는 배치 파일을 읽지 못하거나 커서(256KiB 넘음) 지연 확장과 call을 확인하지 못했습니다.");
}

TEST_CASE("cmd.exe runs a relative batch path with spaces only when it exists and a delayed expansion batch is flagged", "[function_buttons][s3]")
{
    const cmd_folders folders {};
    const std::filesystem::path tools { folders.onedrive() / L"tools" / L"Build Tools" };
    REQUIRE(std::filesystem::create_directories(tools));
    cmd_folders::write_batch(tools / L"run.bat", "@\"%~dp0..\\..\\gitman_process_test_child.exe\" echo-args %*\r\n");
    // `/v:off`를 넣어도 배치가 지연 확장을 다시 켜면 넘긴 값의 `!GMX!`가 펼쳐진다 (4.3의 전제, R4 보안 검토 실측).
    cmd_folders::write_batch(folders.onedrive() / L"delayed.bat", "@echo off\r\nset GMX=expanded\r\nsetlocal EnableDelayedExpansion\r\n\"%~dp0gitman_process_test_child.exe\" echo-args %*\r\n");
    const std::unique_ptr<gitman::vcs_file_probe> probe { gitman::win32::make_vcs_file_probe() };
    const std::u8string working_directory { folders.onedrive().u8string() };

    // 작업 폴더에 있는 공백 상대 경로는 경로 하나로 넘겨 배치가 돈다.
    const std::vector<gitman::cmd_argument> relative { arguments_of({ u8"/d", u8"/c", u8"tools\\Build Tools\\run.bat", u8"x y" }, true) };
    gitman::cmd_target_query query { *gitman::find_cmd_target_query(relative) };
    query.working_directory = working_directory;
    const gitman::cmd_target_facts found { gitman::inspect_cmd_target(*probe, query) };
    REQUIRE(found.path_exists);
    REQUIRE(found.batch == std::optional<gitman::cmd_batch_findings> { gitman::cmd_batch_findings {} });
    const cmd_run path_run { run_assembled(relative, folders.onedrive(), &found) };
    INFO(reinterpret_cast<const char*>(path_run.assembly.text.c_str()));
    REQUIRE(path_run.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(path_run.lines == std::vector<std::u8string> { u8"[x y]" });
    REQUIRE(path_run.assembly.needs_approval() == false);

    // 없는 경로는 글자만의 판정 그대로 문장이고, cmd는 `tools\Build`를 명령으로 찾지 못한다.
    const std::vector<gitman::cmd_argument> absent { arguments_of({ u8"/d", u8"/c", u8"tools\\Build Tools\\absent.bat", u8"x y" }, true) };
    gitman::cmd_target_query absent_query { *gitman::find_cmd_target_query(absent) };
    absent_query.working_directory = working_directory;
    const gitman::cmd_target_facts not_found { gitman::inspect_cmd_target(*probe, absent_query) };
    REQUIRE(not_found.path_exists == false);
    REQUIRE(not_found.batch.has_value() == false);
    const cmd_run sentence_run { run_assembled(absent, folders.onedrive(), &not_found) };
    REQUIRE(sentence_run.assembly.corrections.front().kinds == std::vector<gitman::cmd_correction_kind> { gitman::cmd_correction_kind::sentence });
    REQUIRE(sentence_run.result.exit_code != std::optional<std::int32_t> { 0 });
    REQUIRE(sentence_run.lines.empty());

    // 지연 확장을 켜는 배치는 경고로 잡히고, 실제로 `!GMX!`를 펼친다.
    const std::vector<gitman::cmd_argument> delayed { { { literal(u8"/d") } }, { { literal(u8"/c") } }, { { value(working_directory), literal(u8"\\delayed.bat") } }, { { value(u8"!GMX!") } } };
    gitman::cmd_target_query delayed_query { *gitman::find_cmd_target_query(delayed) };
    delayed_query.working_directory = working_directory;
    const gitman::cmd_target_facts flagged { gitman::inspect_cmd_target(*probe, delayed_query) };
    REQUIRE(flagged.batch == std::optional<gitman::cmd_batch_findings> { gitman::cmd_batch_findings { true, false, false } });
    const cmd_run delayed_run { run_assembled(delayed, folders.onedrive(), &flagged) };
    INFO(reinterpret_cast<const char*>(delayed_run.assembly.text.c_str()));
    REQUIRE(has_warning(delayed_run.assembly, gitman::cmd_warning_kind::batch_delayed_expansion, 2u));
    REQUIRE(delayed_run.assembly.text.find(u8"/v:off") != std::u8string::npos);
    REQUIRE(delayed_run.result.exit_code == std::optional<std::int32_t> { 0 });
    REQUIRE(delayed_run.lines == std::vector<std::u8string> { u8"[expanded]" });

    // 크기 상한을 넘는 배치는 읽지 않고 확인하지 못했다고 남긴다.
    cmd_folders::write_batch(folders.onedrive() / L"large.bat", std::string(gitman::cmd_batch_inspection_limit + 1, 'x'));
    const std::vector<gitman::cmd_argument> large { arguments_of({ u8"/c", u8"large.bat" }, true) };
    gitman::cmd_target_query large_query { *gitman::find_cmd_target_query(large) };
    large_query.working_directory = working_directory;
    REQUIRE(gitman::inspect_cmd_target(*probe, large_query).batch == std::optional<gitman::cmd_batch_findings> { gitman::cmd_batch_findings { false, false, true } });
}

TEST_CASE("cmd.exe and the worker agree on dot segments and a locked batch is unchecked", "[function_buttons][s3]")
{
    const cmd_folders folders {};
    const std::filesystem::path work { folders.onedrive() / L"work" };
    const std::filesystem::path sub { folders.onedrive() / L"sub dir" };
    REQUIRE(std::filesystem::create_directories(work));
    REQUIRE(std::filesystem::create_directories(sub));
    const std::string delayed_text { "@echo off\r\nset GMX=expanded\r\nsetlocal EnableDelayedExpansion\r\n\"%~dp0..\\gitman_process_test_child.exe\" echo-args %*\r\n" };
    cmd_folders::write_batch(sub / L"x.bat", delayed_text);
    cmd_folders::write_batch(work / L"y.bat", delayed_text);
    const std::unique_ptr<gitman::vcs_file_probe> probe { gitman::win32::make_vcs_file_probe() };
    const auto inspect = [&](const std::vector<gitman::cmd_argument>& arguments, const std::filesystem::path& working_directory)
    {
        gitman::cmd_target_query query { *gitman::find_cmd_target_query(arguments) };
        query.working_directory = working_directory.u8string();
        return gitman::inspect_cmd_target(*probe, query);
    };
    const auto call_with_bang = [](const std::u8string_view target)
    {
        return std::vector<gitman::cmd_argument> { { { literal(u8"/d") } }, { { literal(u8"/c") } }, { { literal(target) } }, { { value(u8"!GMX!") } } };
    };
    const std::optional<gitman::cmd_batch_findings> delayed { gitman::cmd_batch_findings { true, false, false } };

    // `.\`·`..\`로 부른 배치도 읽어 경고한다. 그대로 두면 cmd는 실제로 `!GMX!`를 펼친다 (S3 검토의 재현).
    // 작업 폴더에 `..`가 들어도 같다.
    // `./y.bat`은 cmd가 `/y.bat`을 스위치로 읽어 실행하지 못하지만 읽기는 같은 파일에 닿는다(넘치는 쪽의 경고).
    REQUIRE(inspect(call_with_bang(u8"./y.bat"), work).batch == delayed);
    const std::array<std::pair<std::u8string_view, std::filesystem::path>, 3> dotted_cases {
        std::pair<std::u8string_view, std::filesystem::path> { u8".\\y.bat", work },
        std::pair<std::u8string_view, std::filesystem::path> { u8".\\y.bat", work / L".." / L"work" },
        std::pair<std::u8string_view, std::filesystem::path> { u8"..\\work\\y.bat", work },
    };
    for (const auto& [target, working_directory] : dotted_cases)
    {
        INFO(reinterpret_cast<const char*>(std::u8string { target }.c_str()));
        const std::vector<gitman::cmd_argument> arguments { call_with_bang(target) };
        const gitman::cmd_target_facts facts { inspect(arguments, working_directory) };
        REQUIRE(facts.batch == delayed);
        // 실행은 정규화한 같은 폴더에서 한다. 여기서 보는 것은 `..`가 든 작업 폴더를 읽기 쪽이 접는지다.
        const cmd_run run { run_assembled(arguments, work, &facts) };
        INFO(reinterpret_cast<const char*>(run.assembly.text.c_str()));
        REQUIRE(has_warning(run.assembly, gitman::cmd_warning_kind::batch_delayed_expansion, 2u));
        REQUIRE(run.lines == std::vector<std::u8string> { u8"[expanded]" });
    }

    // 공백이 든 상대 경로에 `.\`·`..\`가 있어도 있는 파일이면 경로 하나로 넘겨 배치가 돈다.
    for (const std::u8string_view target : { std::u8string_view { u8"..\\sub dir\\x.bat" }, std::u8string_view { u8"../sub dir/x.bat" } })
    {
        INFO(reinterpret_cast<const char*>(std::u8string { target }.c_str()));
        const std::vector<gitman::cmd_argument> arguments { call_with_bang(target) };
        const gitman::cmd_target_facts facts { inspect(arguments, work) };
        REQUIRE(facts.path_exists);
        REQUIRE(facts.batch == delayed);
        const cmd_run run { run_assembled(arguments, work, &facts) };
        INFO(reinterpret_cast<const char*>(run.assembly.text.c_str()));
        REQUIRE(run.result.exit_code == std::optional<std::int32_t> { 0 });
        REQUIRE(run.lines == std::vector<std::u8string> { u8"[expanded]" });
    }
    const std::vector<gitman::cmd_argument> dotted { call_with_bang(u8".\\sub dir\\x.bat") };
    REQUIRE(inspect(dotted, folders.onedrive()).path_exists);

    // 있는데 열지 못한 배치(다른 프로세스가 잠금)는 없는 것과 달리 확인하지 못했다고 경고한다.
    {
        const HANDLE locked { CreateFileW((work / L"y.bat").c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr) };
        REQUIRE(locked != INVALID_HANDLE_VALUE);
        const gitman::cmd_target_facts facts { inspect(call_with_bang(u8"y.bat"), work) };
        CloseHandle(locked);
        REQUIRE(facts.batch == std::optional<gitman::cmd_batch_findings> { gitman::cmd_batch_findings { false, false, true } });
    }
    // 다른 드라이브의 드라이브 상대 경로는 이을 수 없어 확인하지 못했다고 경고한다. 정말 없는 배치는 경고가 없다.
    const wchar_t other_drive { work.wstring().front() == L'Z' || work.wstring().front() == L'z' ? L'Y' : L'Z' };
    const std::u8string other { std::u8string { static_cast<char8_t>(other_drive) } + u8":y.bat" };
    REQUIRE(inspect(call_with_bang(other), work).batch == std::optional<gitman::cmd_batch_findings> { gitman::cmd_batch_findings { false, false, true } });
    REQUIRE(inspect(call_with_bang(u8"absent.bat"), work).batch.has_value() == false);
}
