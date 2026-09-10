#include "domain/vcs_tool.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/script_command_builder.h"
#include "infrastructure/svn_hook_runner.h"
#include "infrastructure/svn_repository_provider.h"
#include "infrastructure/tortoise_svn_hooks.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    constexpr std::u8string_view working_copy { u8"C:\\work\\alpha" };

    // 훅 인자 파일을 메모리에 담는 대역이다. 실제 파일을 만들지 않고도 인자 순서와
    // 내용을 확인할 수 있다.
    class fake_temp_file_writer final : public gitman::temp_file_writer
    {
    public:
        [[nodiscard]] std::u8string write(const std::u8string_view name_hint, const std::u8string_view content) noexcept override
        {
            try
            {
                std::u8string path { u8"C:\\temp\\gitman-hooks\\" };
                path.append(name_hint);
                const std::string digits { std::to_string(++counter_) };
                path.push_back(u8'-');
                path.append(digits.begin(), digits.end());
                path.append(u8".txt");
                files.push_back({ path, std::u8string { content } });
                return path;
            }
            catch (...)
            {
                return {};
            }
        }

        void remove(const std::u8string_view path) noexcept override
        {
            removed.push_back(std::u8string { path });
        }

        struct entry
        {
            std::u8string path {};
            std::u8string content {};
        };

        std::vector<entry> files {};
        std::vector<std::u8string> removed {};

    private:
        std::size_t counter_ { 0 };
    };

    gitman::project_definition make_project()
    {
        gitman::project_definition project {};
        project.id.value = u8"alpha";
        project.display_name = u8"알파";
        project.path.original = working_copy;
        project.path.normalized = working_copy;
        project.hint = gitman::vcs_hint::subversion;
        return project;
    }

    gitman::vcs_tool_environment make_environment()
    {
        gitman::vcs_tool_environment environment {};
        environment.path_environment = u8"C:\\Python312";
        environment.system_directory = u8"C:\\Windows\\System32";
        return environment;
    }

    gitman::svn_hook_context make_context(const gitman::svn_hook_phase phase)
    {
        gitman::svn_hook_context context {};
        context.project = make_project();
        context.working_copy_path = working_copy;
        context.phase = phase;
        context.revision = u8"4168";
        context.reference = u8"https://svn.example.com/repo/trunk";
        return context;
    }

    gitman::svn_hook make_hook(const gitman::svn_hook_phase phase, const std::u8string_view script)
    {
        gitman::svn_hook hook {};
        hook.phase = phase;
        hook.script = script;
        return hook;
    }
} // namespace

TEST_CASE("TortoiseSVN hook records are read six lines at a time", "[infrastructure][hooks][tortoise]")
{
    // TortoiseSVN은 훅 전부를 값 하나에 담는다. 개행으로 나뉜 여섯 줄이 훅 하나다.
    constexpr std::u8string_view value {
        u8"start_update_hook\n"
        u8"C:\\work\\alpha\n"
        u8"C:\\hooks\\start.bat\n"
        u8"true\n"
        u8"hide\n"
        u8"enforce\n"
        u8"pre_update_hook\n"
        u8"*\n"
        u8"\"C:\\hooks\\pre check.bat\" --strict\n"
        u8"false\n"
        u8"show\n"
        u8"ask\n"
    };

    const std::vector<gitman::tortoise_svn_hook> hooks { gitman::parse_tortoise_svn_hooks(value) };
    REQUIRE(hooks.size() == 2u);

    REQUIRE(hooks[0].phase == gitman::svn_hook_phase::start_update);
    REQUIRE(hooks[0].working_copy_path == u8"C:\\work\\alpha");
    REQUIRE(hooks[0].wait);
    REQUIRE_FALSE(hooks[0].show_console);
    REQUIRE(hooks[0].enforce);

    REQUIRE(hooks[1].phase == gitman::svn_hook_phase::pre_update);
    REQUIRE(hooks[1].working_copy_path == u8"*");
    REQUIRE_FALSE(hooks[1].wait);
    REQUIRE(hooks[1].show_console);
    REQUIRE_FALSE(hooks[1].enforce);
}

TEST_CASE("An older five line record and unrelated hook types are tolerated", "[infrastructure][hooks][tortoise]")
{
    // 옛 판은 여섯 번째 줄이 없다 (ticket #482). 여섯 번째 토큰이 아는 종류 이름이면
    // 그것이 다음 record의 시작이다.
    constexpr std::u8string_view value {
        u8"post_update_hook\n"
        u8"C:\\work\\alpha\n"
        u8"C:\\hooks\\post.py\n"
        u8"true\n"
        u8"hide\n"
        u8"pre_commit_hook\n"
        u8"*\n"
        u8"C:\\hooks\\commit.bat\n"
        u8"true\n"
        u8"hide\n"
        u8"ask\n"
    };

    const std::vector<gitman::tortoise_svn_hook> hooks { gitman::parse_tortoise_svn_hooks(value) };
    // commit 훅은 gitman이 하지 않는 작업이라 버린다.
    REQUIRE(hooks.size() == 1u);
    REQUIRE(hooks.front().phase == gitman::svn_hook_phase::post_update);
    REQUIRE(hooks.front().command_line == u8"C:\\hooks\\post.py");

    REQUIRE(gitman::parse_tortoise_svn_hooks({}).empty());
    REQUIRE(gitman::parse_tortoise_svn_hooks(u8"쓰레기\n줄\n").empty());
}

TEST_CASE("A hook is chosen by the closest matching path", "[infrastructure][hooks][tortoise]")
{
    std::vector<gitman::tortoise_svn_hook> hooks {};
    gitman::tortoise_svn_hook wildcard {};
    wildcard.phase = gitman::svn_hook_phase::pre_update;
    wildcard.working_copy_path = u8"*";
    wildcard.command_line = u8"C:\\hooks\\any.bat";
    gitman::tortoise_svn_hook exact {};
    exact.phase = gitman::svn_hook_phase::pre_update;
    exact.working_copy_path = u8"c:/work/alpha";
    exact.command_line = u8"C:\\hooks\\alpha.bat";
    gitman::tortoise_svn_hook parent {};
    parent.phase = gitman::svn_hook_phase::pre_update;
    parent.working_copy_path = u8"C:\\work";
    parent.command_line = u8"C:\\hooks\\work.bat";
    hooks.push_back(wildcard);
    hooks.push_back(exact);
    hooks.push_back(parent);

    // 대소문자와 구분자를 가리지 않고 정확히 일치하는 것이 먼저다.
    const gitman::tortoise_svn_hook* const chosen { gitman::select_tortoise_svn_hook(hooks, gitman::svn_hook_phase::pre_update, working_copy) };
    REQUIRE(chosen != nullptr);
    REQUIRE(chosen->command_line == u8"C:\\hooks\\alpha.bat");

    // 그 다음이 부모, 마지막이 `*`다.
    const gitman::tortoise_svn_hook* const by_parent { gitman::select_tortoise_svn_hook(hooks, gitman::svn_hook_phase::pre_update, u8"C:\\work\\beta") };
    REQUIRE(by_parent != nullptr);
    REQUIRE(by_parent->command_line == u8"C:\\hooks\\work.bat");

    const gitman::tortoise_svn_hook* const by_wildcard { gitman::select_tortoise_svn_hook(hooks, gitman::svn_hook_phase::pre_update, u8"D:\\other") };
    REQUIRE(by_wildcard != nullptr);
    REQUIRE(by_wildcard->command_line == u8"C:\\hooks\\any.bat");

    // 다른 단계는 걸리지 않는다.
    REQUIRE(gitman::select_tortoise_svn_hook(hooks, gitman::svn_hook_phase::post_update, working_copy) == nullptr);
}

TEST_CASE("A stored command line splits into a program and arguments", "[infrastructure][hooks][tortoise]")
{
    const gitman::tortoise_command_line simple { gitman::split_tortoise_command_line(u8"C:\\hooks\\pre.bat --strict") };
    REQUIRE(simple.program == u8"C:\\hooks\\pre.bat");
    REQUIRE(simple.arguments == std::vector<std::u8string> { u8"--strict" });

    // 공백이 있는 경로는 따옴표로 감싼다.
    const gitman::tortoise_command_line quoted { gitman::split_tortoise_command_line(u8"\"C:\\my hooks\\pre.bat\" \"두 낱말\"") };
    REQUIRE(quoted.program == u8"C:\\my hooks\\pre.bat");
    REQUIRE(quoted.arguments == std::vector<std::u8string> { u8"두 낱말" });

    // 이름만 적는 것도 허용한다 — PATH에서 찾는다.
    const gitman::tortoise_command_line bare { gitman::split_tortoise_command_line(u8"python C:\\hooks\\pre.py") };
    REQUIRE(bare.program == u8"python");
    REQUIRE(bare.arguments == std::vector<std::u8string> { u8"C:\\hooks\\pre.py" });
}

TEST_CASE("Gitman hooks run in order and a failing pre hook blocks the operation", "[infrastructure][hooks]")
{
    gitman::svn_hook_settings settings {};
    settings.use_tortoise_svn = false;
    settings.hooks.push_back(make_hook(gitman::svn_hook_phase::pre_update, u8"C:\\hooks\\first.py"));
    settings.hooks.push_back(make_hook(gitman::svn_hook_phase::pre_update, u8"C:\\hooks\\second.py"));
    // 다른 단계의 훅은 이번 실행에서 돌지 않는다.
    settings.hooks.push_back(make_hook(gitman::svn_hook_phase::post_update, u8"C:\\hooks\\after.py"));

    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, u8"첫 훅\n", {} });
    runner.push_response({ gitman::process_completion::exited, 5, {}, u8"두 번째 훅 실패\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Python312\\python.exe");

    gitman::svn_hook_runner hooks { settings, {}, u8"C:\\work\\p.version-list", runner, probe, make_environment(), nullptr, nullptr };
    REQUIRE_FALSE(hooks.empty(working_copy));

    const gitman::svn_hook_run_result result { hooks.run(make_context(gitman::svn_hook_phase::pre_update), {}) };
    REQUIRE(result.executed == 2u);
    REQUIRE(result.blocked);
    REQUIRE(result.diagnostics.empty() == false);

    REQUIRE(runner.request_count() == 2u);
    REQUIRE(runner.request(0).arguments.front() == u8"C:\\hooks\\first.py");
    REQUIRE(runner.request(1).arguments.front() == u8"C:\\hooks\\second.py");
    // 훅은 작업 복사본에서 돈다.
    REQUIRE(runner.request(0).working_directory == working_copy);

    // 단계와 작업 종류를 환경 변수로 알린다.
    bool has_phase { false };
    bool has_operation { false };
    for (const gitman::process_environment_override& value : runner.request(0).environment_overrides)
    {
        if (value.name == gitman::script_environment_hook_phase && value.value == std::optional<std::u8string> { u8"pre_update" })
            has_phase = true;
        if (value.name == gitman::script_environment_hook_operation && value.value == std::optional<std::u8string> { u8"update" })
            has_operation = true;
    }
    REQUIRE(has_phase);
    REQUIRE(has_operation);
}

TEST_CASE("A failing post hook warns but does not block", "[infrastructure][hooks]")
{
    // 이미 실행된 명령을 되돌릴 수 없다 (설계 H1).
    gitman::svn_hook_settings settings {};
    settings.use_tortoise_svn = false;
    settings.hooks.push_back(make_hook(gitman::svn_hook_phase::post_update, u8"C:\\hooks\\after.py"));

    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 1, {}, u8"실패\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Python312\\python.exe");

    gitman::svn_hook_runner hooks { settings, {}, {}, runner, probe, make_environment(), nullptr, nullptr };
    const gitman::svn_hook_run_result result { hooks.run(make_context(gitman::svn_hook_phase::post_update), {}) };
    REQUIRE(result.executed == 1u);
    REQUIRE_FALSE(result.blocked);
    REQUIRE(result.diagnostics.empty() == false);
}

TEST_CASE("A hook outside the path pattern does not run", "[infrastructure][hooks]")
{
    gitman::svn_hook_settings settings {};
    settings.use_tortoise_svn = false;
    gitman::svn_hook hook { make_hook(gitman::svn_hook_phase::pre_update, u8"C:\\hooks\\only.py") };
    hook.path_pattern = u8"C:/work/beta*";
    settings.hooks.push_back(hook);

    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Python312\\python.exe");

    gitman::svn_hook_runner hooks { settings, {}, {}, runner, probe, make_environment(), nullptr, nullptr };
    REQUIRE(hooks.empty(working_copy));
    const gitman::svn_hook_run_result result { hooks.run(make_context(gitman::svn_hook_phase::pre_update), {}) };
    REQUIRE(result.executed == 0u);
    REQUIRE(runner.request_count() == 0u);
}

TEST_CASE("TortoiseSVN hooks receive the parameter files in order", "[infrastructure][hooks][tortoise]")
{
    gitman::tortoise_svn_hook tortoise {};
    tortoise.phase = gitman::svn_hook_phase::post_update;
    tortoise.working_copy_path = u8"*";
    tortoise.command_line = u8"C:\\hooks\\post.bat --verbose";

    gitman::testing::fake_process_runner runner {};
    runner.set_default_response({ gitman::process_completion::exited, 0, {}, {} });
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Windows\\System32\\cmd.exe");
    fake_temp_file_writer temp_files {};

    gitman::svn_hook_settings settings {};
    gitman::svn_hook_runner hooks { settings, { tortoise }, {}, runner, probe, make_environment(), &temp_files, nullptr };

    gitman::svn_hook_context context { make_context(gitman::svn_hook_phase::post_update) };
    context.error_text = u8"업데이트 실패";
    const gitman::svn_hook_run_result result { hooks.run(context, {}) };
    REQUIRE(result.executed == 1u);
    REQUIRE_FALSE(result.blocked);

    REQUIRE(runner.request_count() == 1u);
    const std::vector<std::u8string>& arguments { runner.request(0).arguments };
    // cmd.exe /d /c <스크립트> --verbose PATH DEPTH REVISION ERROR CWD RESULTPATH
    REQUIRE(arguments.size() == 10u);
    REQUIRE(arguments[0] == u8"/d");
    REQUIRE(arguments[1] == u8"/c");
    REQUIRE(arguments[2] == u8"C:\\hooks\\post.bat");
    REQUIRE(arguments[3] == u8"--verbose");
    REQUIRE(arguments[5] == u8"3");
    REQUIRE(arguments[6] == u8"HEAD");
    REQUIRE(arguments[8] == working_copy);

    // PATH 파일에는 작업 복사본 한 줄이, ERROR 파일에는 실패 요약이 들어간다.
    REQUIRE(temp_files.files.size() == 3u);
    REQUIRE(temp_files.files[0].content == std::u8string { working_copy } + std::u8string { u8"\r\n" });
    REQUIRE(temp_files.files[1].content == u8"업데이트 실패");
    // gitman은 update 출력을 경로 목록으로 파싱하지 않아 결과 파일은 비어 있다.
    REQUIRE(temp_files.files[2].content.empty());
    // 쓴 파일은 모두 지운다.
    REQUIRE(temp_files.removed.size() == 3u);
}

TEST_CASE("TortoiseSVN hooks are skipped when parameter files cannot be made", "[infrastructure][hooks][tortoise]")
{
    // 인자를 임시 파일로 넘기는 규약을 지킬 수 없으면 실행하지 않는다. 규약을 어긴
    // 채 돌리면 남의 스크립트가 엉뚱한 인자를 받는다.
    gitman::tortoise_svn_hook tortoise {};
    tortoise.phase = gitman::svn_hook_phase::pre_update;
    tortoise.working_copy_path = u8"*";
    tortoise.command_line = u8"C:\\hooks\\pre.bat";

    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Windows\\System32\\cmd.exe");

    const gitman::svn_hook_settings settings {};
    gitman::svn_hook_runner hooks { settings, { tortoise }, {}, runner, probe, make_environment(), nullptr, nullptr };
    REQUIRE(hooks.empty(working_copy));
    REQUIRE(hooks.run(make_context(gitman::svn_hook_phase::pre_update), {}).executed == 0u);
    REQUIRE(runner.request_count() == 0u);
}

TEST_CASE("Turning off TortoiseSVN reuse leaves its hooks alone", "[infrastructure][hooks][tortoise]")
{
    gitman::tortoise_svn_hook tortoise {};
    tortoise.phase = gitman::svn_hook_phase::pre_update;
    tortoise.working_copy_path = u8"*";
    tortoise.command_line = u8"C:\\hooks\\pre.bat";

    gitman::testing::fake_process_runner runner {};
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Windows\\System32\\cmd.exe");
    fake_temp_file_writer temp_files {};

    gitman::svn_hook_settings settings {};
    settings.use_tortoise_svn = false;
    gitman::svn_hook_runner hooks { settings, { tortoise }, {}, runner, probe, make_environment(), &temp_files, nullptr };
    REQUIRE(hooks.empty(working_copy));
    REQUIRE(hooks.run(make_context(gitman::svn_hook_phase::pre_update), {}).executed == 0u);
}

TEST_CASE("A failing start hook stops the update before any svn command", "[infrastructure][hooks][provider]")
{
    // start_update는 사전 검사 앞이다. 훅이 막으면 `svn info`조차 돌지 않는다.
    gitman::svn_hook_settings settings {};
    settings.use_tortoise_svn = false;
    settings.hooks.push_back(make_hook(gitman::svn_hook_phase::start_update, u8"C:\\hooks\\start.py"));

    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 2, {}, u8"막았다\n" });
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\Python312\\python.exe");
    probe.add_directory(working_copy);

    gitman::svn_hook_runner hooks { settings, {}, {}, runner, probe, make_environment(), nullptr, nullptr };

    gitman::vcs_tool_info tool {};
    tool.kind = gitman::repository_kind::subversion;
    tool.availability = gitman::vcs_tool_availability::available;
    tool.executable = u8"C:\\Program Files\\TortoiseSVN\\bin\\svn.exe";
    tool.version = { 1, 14, 5 };

    gitman::svn_repository_provider provider { tool, runner, probe, nullptr, {}, false, &hooks };
    const gitman::repository_change_result result { provider.update(make_project(), {}, {}) };

    REQUIRE(result.blocked_by == gitman::update_block_reason::hook_rejected);
    REQUIRE_FALSE(result.executed);
    // 훅 프로세스 하나뿐이다 — svn은 부르지 않았다.
    REQUIRE(runner.request_count() == 1u);
    REQUIRE(runner.requests_for_executable_suffix(u8"svn.exe") == 0u);
}
