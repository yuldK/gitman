#include "domain/command_definition.h"
#include "domain/script_interpreter.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace {
    gitman::custom_command make_command(const std::u8string_view id)
    {
        gitman::custom_command command {};
        command.id = id;
        command.label = u8"빌드 테스트";
        command.script = u8"scripts/build.py";
        return command;
    }

    gitman::command_match_context make_context(const std::u8string_view id, const std::u8string_view path, const gitman::command_target_vcs vcs = gitman::command_target_vcs::git)
    {
        gitman::command_match_context context {};
        context.project_id = id;
        context.repository_path = path;
        context.display_name = id;
        context.vcs = vcs;
        return context;
    }
} // namespace

TEST_CASE("A command pattern matches with wildcards, mixed separators and case", "[domain][commands]")
{
    // 사용자는 문서에 `\`와 `/`를 섞어 쓰고 Windows는 대소문자를 구분하지 않는다.
    REQUIRE(gitman::command_pattern_matches(u8"", u8"무엇이든"));
    REQUIRE(gitman::command_pattern_matches(u8"*", u8""));
    REQUIRE(gitman::command_pattern_matches(u8"C:/work/*", u8"C:\\work\\alpha"));
    REQUIRE(gitman::command_pattern_matches(u8"c:\\WORK\\*", u8"C:/work/alpha"));
    REQUIRE(gitman::command_pattern_matches(u8"*/engine", u8"C:/work/engine"));
    REQUIRE(gitman::command_pattern_matches(u8"*engine*", u8"C:/work/engine/core"));
    REQUIRE(gitman::command_pattern_matches(u8"alph?", u8"alpha"));

    REQUIRE_FALSE(gitman::command_pattern_matches(u8"alph?", u8"alphabet"));
    REQUIRE_FALSE(gitman::command_pattern_matches(u8"C:/other/*", u8"C:/work/alpha"));
    REQUIRE_FALSE(gitman::command_pattern_matches(u8"*/engine", u8"C:/work/engine/core"));
}

TEST_CASE("Command filters combine with AND", "[domain][commands]")
{
    // VCS 종류·카드별 지정·경로 패턴 셋을 모두 통과해야 그 카드에 보인다
    // (2026-09-10 지시).
    gitman::custom_command command { make_command(u8"build") };
    REQUIRE(gitman::command_matches(command, make_context(u8"alpha", u8"C:/work/alpha")));

    command.vcs = gitman::command_target_vcs::subversion;
    REQUIRE_FALSE(gitman::command_matches(command, make_context(u8"alpha", u8"C:/work/alpha")));
    REQUIRE(gitman::command_matches(command, make_context(u8"alpha", u8"C:/work/alpha", gitman::command_target_vcs::subversion)));

    // 아직 종류를 모르는 카드에는 VCS를 지정한 명령을 띄우지 않는다. 눌러 봐야
    // 실패를 아는 상태가 되기 때문이다.
    REQUIRE_FALSE(gitman::command_matches(command, make_context(u8"alpha", u8"C:/work/alpha", gitman::command_target_vcs::any)));

    command.vcs = gitman::command_target_vcs::any;
    command.projects = { std::u8string { u8"beta" } };
    REQUIRE_FALSE(gitman::command_matches(command, make_context(u8"alpha", u8"C:/work/alpha")));
    REQUIRE(gitman::command_matches(command, make_context(u8"beta", u8"C:/work/beta")));

    command.projects.clear();
    command.path_pattern = u8"C:/work/engine*";
    REQUIRE_FALSE(gitman::command_matches(command, make_context(u8"alpha", u8"C:/work/alpha")));
    REQUIRE(gitman::command_matches(command, make_context(u8"engine", u8"C:/work/engine")));
}

TEST_CASE("A command pattern also matches the display name", "[domain][commands]")
{
    // 사용자는 경로보다 카드 이름으로 기억한다. 둘 중 하나만 맞아도 통과다.
    gitman::custom_command command { make_command(u8"build") };
    command.path_pattern = u8"*engine*";

    gitman::command_match_context context { make_context(u8"card-1", u8"D:/repos/x9") };
    context.display_name = u8"Engine";
    REQUIRE(gitman::command_matches(command, context));
}

TEST_CASE("A malformed command never matches", "[domain][commands]")
{
    // 반쯤 채워진 정의가 메뉴에 뜨면 눌러 봐야 실패를 알게 된다.
    gitman::custom_command command {};
    REQUIRE(gitman::custom_command_error(command).empty() == false);
    REQUIRE_FALSE(gitman::command_matches(command, make_context(u8"alpha", u8"C:/work/alpha")));

    command = make_command(u8"build");
    command.label.clear();
    REQUIRE(gitman::custom_command_error(command).empty() == false);

    command = make_command(u8"build");
    command.id = u8"build\x1f" u8"extra";
    REQUIRE(gitman::custom_command_error(command).empty() == false);

    command = make_command(u8"build");
    command.timeout_seconds = -1;
    REQUIRE(gitman::custom_command_error(command).empty() == false);
}

TEST_CASE("A document catalog replaces the global one item group at a time", "[domain][commands]")
{
    // 목록은 항목별 병합 규칙을 둘 수 없어 — "전역의 이 항목만 지우기"를 표현할
    // 방법이 없다 — 정의된 묶음을 통째로 갈아 끼운다 (설계 C2).
    gitman::command_catalog base {};
    base.custom.push_back(make_command(u8"global"));
    base.svn_hooks.use_tortoise_svn = false;

    gitman::command_catalog_overrides overrides {};
    REQUIRE(overrides.empty());
    REQUIRE(gitman::apply_overrides(base, overrides) == base);

    overrides.custom = { std::vector<gitman::custom_command> { make_command(u8"document") } };
    const gitman::command_catalog replaced { gitman::apply_overrides(base, overrides) };
    REQUIRE(replaced.custom.size() == 1u);
    REQUIRE(replaced.custom.front().id == u8"document");
    // 정의하지 않은 묶음은 그대로 전역을 따른다.
    REQUIRE(replaced.svn_hooks.use_tortoise_svn == false);

    // 빈 배열도 정의다. 이 문서에서는 명령을 쓰지 않겠다는 뜻이다.
    overrides.custom = { std::vector<gitman::custom_command> {} };
    REQUIRE(gitman::apply_overrides(base, overrides).custom.empty());
}

TEST_CASE("An SVN hook is skipped when disabled, malformed or out of path", "[domain][commands]")
{
    gitman::svn_hook hook {};
    hook.script = u8"C:/hooks/check.py";
    REQUIRE(gitman::svn_hook_matches(hook, u8"C:/work/alpha"));

    hook.enabled = false;
    REQUIRE_FALSE(gitman::svn_hook_matches(hook, u8"C:/work/alpha"));

    hook.enabled = true;
    hook.path_pattern = u8"C:/work/beta*";
    REQUIRE_FALSE(gitman::svn_hook_matches(hook, u8"C:/work/alpha"));
    REQUIRE(gitman::svn_hook_matches(hook, u8"C:/work/beta"));

    hook.path_pattern.clear();
    hook.script.clear();
    REQUIRE(gitman::svn_hook_error(hook).empty() == false);
    REQUIRE_FALSE(gitman::svn_hook_matches(hook, u8"C:/work/alpha"));
}

TEST_CASE("The interpreter table follows the script extension", "[domain][commands][interpreter]")
{
    // `CreateProcessW`가 lpApplicationName을 받으므로 스크립트를 직접 실행할 수
    // 없다. 확장자가 해석기를 정한다 (설계 X2).
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.py").kind == gitman::script_interpreter_kind::python);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.PY").kind == gitman::script_interpreter_kind::python);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.pyw").kind == gitman::script_interpreter_kind::python);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.js").kind == gitman::script_interpreter_kind::node);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.mjs").kind == gitman::script_interpreter_kind::node);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.bat").kind == gitman::script_interpreter_kind::command_shell);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.cmd").kind == gitman::script_interpreter_kind::command_shell);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/build.ps1").kind == gitman::script_interpreter_kind::power_shell);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/tool.exe").kind == gitman::script_interpreter_kind::executable);

    REQUIRE(gitman::script_interpreter_for(u8"C:/x/notes.txt").kind == gitman::script_interpreter_kind::unsupported);
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/README").kind == gitman::script_interpreter_kind::unsupported);
    // 점으로 시작하는 이름은 확장자가 아니다.
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/.gitignore").kind == gitman::script_interpreter_kind::unsupported);

    // 셸은 비대화형을 명시하고 PATH보다 시스템 디렉터리를 먼저 본다.
    const gitman::script_interpreter_spec shell { gitman::script_interpreter_for(u8"C:/x/build.ps1") };
    REQUIRE(shell.prefer_system_directory);
    REQUIRE(shell.alternate_executable_name == u8"powershell.exe");
    bool has_non_interactive { false };
    for (const std::u8string& argument : shell.leading_arguments)
        if (argument == u8"-NonInteractive")
            has_non_interactive = true;
    REQUIRE(has_non_interactive);
    REQUIRE(shell.leading_arguments.back() == u8"-File");

    const gitman::script_interpreter_spec batch { gitman::script_interpreter_for(u8"C:/x/build.cmd") };
    REQUIRE(batch.prefer_system_directory);
    REQUIRE(batch.leading_arguments == std::vector<std::u8string> { u8"/d", u8"/c" });

    // 실행 파일은 해석기를 찾지 않는다.
    REQUIRE(gitman::script_interpreter_for(u8"C:/x/tool.exe").executable_name.empty());
}
