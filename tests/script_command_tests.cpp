#include "helpers/vcs_test_doubles.h"
#include "infrastructure/script_command_builder.h"
#include "infrastructure/script_interpreter_discovery.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace {
    gitman::vcs_tool_environment make_environment()
    {
        gitman::vcs_tool_environment environment {};
        environment.path_environment = u8"C:\\tools\\shadow;C:\\Python312;C:\\Program Files\\nodejs";
        environment.system_directory = u8"C:\\Windows\\System32";
        return environment;
    }
} // namespace

TEST_CASE("Interpreter discovery finds the tool on PATH", "[infrastructure][scripts]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Python312\\python.exe");

    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\work\\build.py", make_environment(), probe) };
    REQUIRE(resolved.usable());
    REQUIRE(resolved.kind == gitman::script_interpreter_kind::python);
    REQUIRE(resolved.executable == u8"C:\\Python312\\python.exe");
    REQUIRE(resolved.diagnostics.empty());
}

TEST_CASE("A shell is taken from the system directory before PATH", "[infrastructure][scripts]")
{
    // PATH 앞에 놓인 동명 파일이 cmd.exe를 가로채지 못하게 한다 (설계 X3).
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\tools\\shadow\\cmd.exe");
    probe.add_file(u8"C:\\Windows\\System32\\cmd.exe");

    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\work\\build.bat", make_environment(), probe) };
    REQUIRE(resolved.executable == u8"C:\\Windows\\System32\\cmd.exe");
}

TEST_CASE("PowerShell falls back to the Windows edition", "[infrastructure][scripts]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Windows\\System32\\powershell.exe");

    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\work\\build.ps1", make_environment(), probe) };
    REQUIRE(resolved.executable == u8"C:\\Windows\\System32\\powershell.exe");

    // pwsh가 있으면 그쪽이 이긴다. 디렉터리 순서보다 이름 순서가 앞선다.
    probe.add_file(u8"C:\\Program Files\\nodejs\\pwsh.exe");
    const gitman::resolved_script_interpreter preferred { gitman::resolve_script_interpreter(u8"C:\\work\\build.ps1", make_environment(), probe) };
    REQUIRE(preferred.executable == u8"C:\\Program Files\\nodejs\\pwsh.exe");
}

TEST_CASE("An executable script is run directly", "[infrastructure][scripts]")
{
    const gitman::testing::fake_vcs_file_probe probe {};
    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\tools\\build.exe", make_environment(), probe) };
    REQUIRE(resolved.usable());
    REQUIRE(resolved.executable == u8"C:\\tools\\build.exe");

    // 상대 경로는 프로세스 요청 검증 전에 여기서 걸러 정확한 사유를 남긴다.
    const gitman::resolved_script_interpreter relative { gitman::resolve_script_interpreter(u8"tools\\build.exe", make_environment(), probe) };
    REQUIRE_FALSE(relative.usable());
    REQUIRE(relative.diagnostics.front().code == gitman::diagnostic_code::script_interpreter_unavailable);
}

TEST_CASE("An unknown extension and a missing interpreter both report why", "[infrastructure][scripts]")
{
    const gitman::testing::fake_vcs_file_probe probe {};

    const gitman::resolved_script_interpreter unknown { gitman::resolve_script_interpreter(u8"C:\\work\\notes.txt", make_environment(), probe) };
    REQUIRE_FALSE(unknown.usable());
    REQUIRE(unknown.diagnostics.size() == 1u);
    REQUIRE(unknown.diagnostics.front().code == gitman::diagnostic_code::script_interpreter_unavailable);

    const gitman::resolved_script_interpreter missing { gitman::resolve_script_interpreter(u8"C:\\work\\build.py", make_environment(), probe) };
    REQUIRE_FALSE(missing.usable());
    REQUIRE(missing.diagnostics.size() == 1u);
    REQUIRE(missing.diagnostics.front().code == gitman::diagnostic_code::script_interpreter_unavailable);
}

TEST_CASE("A script request carries the interpreter, the script and the user arguments", "[infrastructure][scripts]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Windows\\System32\\powershell.exe");
    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\work\\build.ps1", make_environment(), probe) };

    gitman::script_command command {};
    command.script = u8"C:\\work\\build.ps1";
    command.arguments = { std::u8string { u8"-Config" }, std::u8string { u8"Release" } };
    command.working_directory = u8"C:\\work\\alpha";

    const gitman::process_request request { gitman::make_script_process_request(resolved, command) };
    REQUIRE(request.executable == u8"C:\\Windows\\System32\\powershell.exe");
    REQUIRE(request.working_directory == u8"C:\\work\\alpha");
    // 해석기 인자 → 스크립트 → 사용자 인자 순서다.
    REQUIRE(request.arguments.size() == 8u);
    REQUIRE(request.arguments.front() == u8"-NoProfile");
    REQUIRE(request.arguments[4] == u8"-File");
    REQUIRE(request.arguments[5] == u8"C:\\work\\build.ps1");
    REQUIRE(request.arguments[6] == u8"-Config");
    REQUIRE(request.arguments[7] == u8"Release");

    // git·svn 공통 인자가 섞이면 안 된다 — `make_vcs_process_request`를 쓰지 않는
    // 이유다 (설계 X4).
    for (const std::u8string& argument : request.arguments)
    {
        REQUIRE(argument != u8"--non-interactive");
        REQUIRE(argument != u8"--no-pager");
    }

    // Windows 스크립트의 출력은 보통 콘솔 코드 페이지다.
    REQUIRE(request.text_encoding == gitman::process_text_encoding::active_code_page_fallback);
    // 무엇을 하는 스크립트인지 앱이 모르므로 기본은 제한 없음이다.
    REQUIRE_FALSE(request.timeout.has_value());
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("An executable script is not repeated as an argument", "[infrastructure][scripts]")
{
    const gitman::testing::fake_vcs_file_probe probe {};
    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\tools\\build.exe", make_environment(), probe) };

    gitman::script_command command {};
    command.script = u8"C:\\tools\\build.exe";
    command.arguments = { std::u8string { u8"--all" } };
    command.working_directory = u8"C:\\work\\alpha";

    const gitman::process_request request { gitman::make_script_process_request(resolved, command) };
    REQUIRE(request.executable == u8"C:\\tools\\build.exe");
    REQUIRE(request.arguments == std::vector<std::u8string> { u8"--all" });
}

TEST_CASE("A per command timeout overrides the unlimited default", "[infrastructure][scripts]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Python312\\python.exe");
    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\work\\build.py", make_environment(), probe) };

    gitman::script_command command {};
    command.script = u8"C:\\work\\build.py";
    command.working_directory = u8"C:\\work\\alpha";
    command.timeout_seconds = 90;

    const gitman::process_request request { gitman::make_script_process_request(resolved, command) };
    REQUIRE(request.timeout.has_value());
    REQUIRE(*request.timeout == std::chrono::milliseconds { 90000 });
}

TEST_CASE("An unusable interpreter yields no request at all", "[infrastructure][scripts]")
{
    const gitman::testing::fake_vcs_file_probe probe {};
    const gitman::resolved_script_interpreter resolved { gitman::resolve_script_interpreter(u8"C:\\work\\build.py", make_environment(), probe) };

    gitman::script_command command {};
    command.script = u8"C:\\work\\build.py";
    command.working_directory = u8"C:\\work\\alpha";

    REQUIRE(gitman::make_script_process_request(resolved, command).executable.empty());
}

TEST_CASE("The script environment drops empty values and folds duplicate names", "[infrastructure][scripts]")
{
    // `process_request`가 대소문자 무시 중복을 오류로 보므로 만드는 쪽에서 정리한다.
    gitman::script_environment_builder builder {};
    builder.set(gitman::script_environment_project_id, u8"alpha");
    builder.set(gitman::script_environment_reference, u8"");
    builder.set(u8"gitman_project_id", u8"beta");

    REQUIRE(builder.values().size() == 1u);
    REQUIRE(builder.values().front().name == gitman::script_environment_project_id);
    REQUIRE(builder.values().front().value == std::u8string { u8"beta" });
}

TEST_CASE("A command line description names the interpreter and every argument", "[infrastructure][scripts]")
{
    // 문서 신뢰 게이트를 두지 않기로 한 대신, 무엇이 실행되는지 로그로 추적할 수
    // 있어야 한다 (설계 0장, X6).
    gitman::process_request request {};
    request.executable = u8"C:\\Windows\\System32\\cmd.exe";
    request.arguments = { std::u8string { u8"/d" }, std::u8string { u8"/c" }, std::u8string { u8"C:\\my scripts\\build.bat" }, std::u8string { u8"--all" } };

    REQUIRE(gitman::describe_process_command(request) == u8"cmd.exe /d /c \"C:\\my scripts\\build.bat\" --all");
}
