#include "domain/function_button.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "infrastructure/command_line_builder.h"
#include "infrastructure/secret_masking.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
    gitman::project_definition make_project()
    {
        gitman::project_definition project {};
        project.id = { u8"frontend" };
        project.path.original = u8"frontend";
        project.path.normalized = u8"E:/repos/frontend";
        project.display_name = u8"Frontend Web";
        return project;
    }

    gitman::repository_snapshot make_git_snapshot()
    {
        gitman::repository_snapshot snapshot {};
        snapshot.kind = gitman::repository_kind::git;
        snapshot.repository_root = u8"E:/repos/frontend";
        snapshot.current_reference = u8"feature/login";
        snapshot.local_revision = u8"a1b2c3d";
        return snapshot;
    }

    gitman::repository_snapshot make_svn_snapshot()
    {
        gitman::repository_snapshot snapshot {};
        snapshot.kind = gitman::repository_kind::subversion;
        snapshot.repository_root = u8"E:/repos/frontend";
        snapshot.current_reference = u8"^/branches/release";
        snapshot.local_revision = u8"1234";
        snapshot.svn_repository_root = u8"https://svn.example.com/repo/";
        return snapshot;
    }

    // test가 고른 이름만 정의된 환경이다. 실제 프로세스 환경을 읽지 않는다.
    gitman::variable_lookup make_lookup(const gitman::repository_snapshot* snapshot, std::map<std::u8string, std::u8string> environment = {})
    {
        gitman::variable_lookup lookup {};
        lookup.builtins = gitman::make_builtin_variables(u8"E:/repos/team.version-list", make_project(), snapshot);
        lookup.environment = [environment](const std::u8string_view name) -> std::optional<std::u8string>
        {
            const auto found { environment.find(std::u8string { name }) };
            if (found == environment.end())
                return std::nullopt;
            return found->second;
        };
        return lookup;
    }

    gitman::variable_expansion expand(const std::u8string_view text, const gitman::variable_lookup& lookup)
    {
        return gitman::expand_variables(text, lookup);
    }

    std::u8string expanded_text(const std::u8string_view text, const gitman::variable_lookup& lookup)
    {
        const gitman::variable_expansion result { expand(text, lookup) };
        REQUIRE(result.succeeded());
        return result.text;
    }

    gitman::expansion_error_kind error_kind(const std::u8string_view text, const gitman::variable_lookup& lookup)
    {
        const gitman::variable_expansion result { expand(text, lookup) };
        REQUIRE(result.succeeded() == false);
        return result.error->kind;
    }

    std::u8string segment_text(const gitman::variable_expansion& expansion, const gitman::expansion_segment& segment)
    {
        return expansion.text.substr(segment.offset, segment.length);
    }

    std::u8string join_for_display(const std::u8string_view program, const std::span<const std::u8string> arguments)
    {
        return gitman::build_windows_command_line(program, arguments);
    }

    std::u8string mask_for_display(const std::u8string_view text)
    {
        return gitman::mask_secrets(text);
    }

    gitman::function_button make_button(const std::u8string_view id, std::optional<std::u8string> placement)
    {
        gitman::function_button button {};
        button.id = std::u8string { id };
        button.label = std::u8string { id };
        button.program = u8"C:\\Tools\\run.exe";
        button.placement = std::move(placement);
        return button;
    }
} // namespace

TEST_CASE("Built-in variables expand from the document and the project", "[function_buttons][t2]")
{
    const gitman::variable_lookup lookup { make_lookup(nullptr) };

    REQUIRE(expanded_text(u8"${workspaceFolder}", lookup) == u8"E:\\repos");
    REQUIRE(expanded_text(u8"${workspaceFile}", lookup) == u8"E:\\repos\\team.version-list");
    REQUIRE(expanded_text(u8"${projectRoot}\\build", lookup) == u8"E:\\repos\\frontend\\build");
    REQUIRE(expanded_text(u8"${projectId}", lookup) == u8"frontend");
    REQUIRE(expanded_text(u8"${projectName}", lookup) == u8"Frontend Web");
    // 글자 그대로의 부분과 변수가 한 인자 안에 섞여도 된다.
    REQUIRE(expanded_text(u8"--root=${projectRoot};id=${projectId}", lookup) == u8"--root=E:\\repos\\frontend;id=frontend");
}

TEST_CASE("Built-in variables fall back to the original path and the id", "[function_buttons][t2]")
{
    gitman::project_definition project { make_project() };
    project.path.normalized.clear();
    project.path.original = u8"D:/work/legacy";
    project.display_name.clear();

    const std::vector<gitman::variable_binding> builtins { gitman::make_builtin_variables(u8"D:/work/team.version-list", project, nullptr) };
    gitman::variable_lookup lookup {};
    lookup.builtins = builtins;

    REQUIRE(expanded_text(u8"${projectRoot}", lookup) == u8"D:\\work\\legacy");
    REQUIRE(expanded_text(u8"${projectName}", lookup) == u8"frontend");
}

TEST_CASE("Path variables unify separators to backslashes", "[function_buttons][t2]")
{
    gitman::repository_snapshot snapshot { make_git_snapshot() };
    snapshot.repository_root = u8"E:/repos\\frontend/sub";
    const gitman::variable_lookup lookup { make_lookup(&snapshot) };

    REQUIRE(expanded_text(u8"${workspaceFolder}|${workspaceFile}", lookup) == u8"E:\\repos|E:\\repos\\team.version-list");
    REQUIRE(expanded_text(u8"${projectRoot}", lookup) == u8"E:\\repos\\frontend");
    REQUIRE(expanded_text(u8"${repositoryRoot}", lookup) == u8"E:\\repos\\frontend\\sub");
    // 경로가 아닌 값은 `/`를 그대로 둔다.
    REQUIRE(expanded_text(u8"${branch}", lookup) == u8"feature/login");
}

TEST_CASE("Workspace folder without a separator is unresolved", "[function_buttons][t2]")
{
    gitman::variable_lookup lookup {};
    lookup.builtins = gitman::make_builtin_variables(u8"team.version-list", make_project(), nullptr);

    REQUIRE(error_kind(u8"${workspaceFolder}", lookup) == gitman::expansion_error_kind::unresolved);
    REQUIRE(expanded_text(u8"${workspaceFile}", lookup) == u8"team.version-list");
}

TEST_CASE("Workspace folder at a drive root keeps the root separator", "[function_buttons][t2]")
{
    gitman::variable_lookup lookup {};
    lookup.builtins = gitman::make_builtin_variables(u8"E:/team.version-list", make_project(), nullptr);

    // `E:`만 남으면 드라이브 기준 상대 경로라 다른 폴더를 가리킬 수 있다.
    REQUIRE(expanded_text(u8"${workspaceFolder}", lookup) == u8"E:\\");
    REQUIRE(expanded_text(u8"${workspaceFile}", lookup) == u8"E:\\team.version-list");

    lookup.builtins = gitman::make_builtin_variables(u8"E:\\team.version-list", make_project(), nullptr);
    REQUIRE(expanded_text(u8"${workspaceFolder}", lookup) == u8"E:\\");
}

TEST_CASE("Empty built-in values are unresolved instead of empty arguments", "[function_buttons][t2]")
{
    gitman::project_definition project { make_project() };
    project.path.normalized.clear();
    project.path.original.clear();

    gitman::repository_snapshot snapshot { make_git_snapshot() };
    snapshot.repository_root.clear();
    snapshot.local_revision.clear();
    snapshot.current_reference.clear();

    gitman::variable_lookup lookup {};
    lookup.builtins = gitman::make_builtin_variables(u8"", project, &snapshot);

    for (const std::u8string_view name : { u8"${workspaceFile}", u8"${projectRoot}", u8"${repositoryRoot}", u8"${revision}", u8"${branch}" })
    {
        const gitman::variable_expansion result { expand(name, lookup) };
        REQUIRE(result.succeeded() == false);
        REQUIRE(result.error->kind == gitman::expansion_error_kind::unresolved);
    }
    REQUIRE(expanded_text(u8"${vcs}", lookup) == u8"git");
}

TEST_CASE("Unknown repository kind leaves vcs dependent variables unresolved", "[function_buttons][t2]")
{
    gitman::repository_snapshot snapshot { make_git_snapshot() };
    snapshot.kind = gitman::repository_kind::unknown;
    snapshot.svn_repository_root = u8"https://svn.example.com/repo";
    snapshot.current_reference = u8"^/trunk";
    const gitman::variable_lookup lookup { make_lookup(&snapshot) };

    REQUIRE(error_kind(u8"${vcs}", lookup) == gitman::expansion_error_kind::unresolved);
    REQUIRE(error_kind(u8"${branch}", lookup) == gitman::expansion_error_kind::unresolved);
    REQUIRE(error_kind(u8"${svnUrl}", lookup) == gitman::expansion_error_kind::unresolved);
    // 종류와 무관한 조회 값은 그대로 풀린다.
    REQUIRE(expanded_text(u8"${repositoryRoot} ${revision}", lookup) == u8"E:\\repos\\frontend a1b2c3d");
}

TEST_CASE("Runtime variables resolve only after a local query", "[function_buttons][t2]")
{
    const gitman::variable_lookup before_query { make_lookup(nullptr) };
    for (const std::u8string_view name : { u8"${vcs}", u8"${repositoryRoot}", u8"${branch}", u8"${svnUrl}", u8"${revision}" })
    {
        const gitman::variable_expansion result { expand(name, before_query) };
        REQUIRE(result.succeeded() == false);
        REQUIRE(result.error->kind == gitman::expansion_error_kind::unresolved);
        // 빈 값으로 조용히 치환하지 않는다 (D13).
        REQUIRE(result.text.empty());
    }

    const gitman::repository_snapshot git { make_git_snapshot() };
    const gitman::variable_lookup git_lookup { make_lookup(&git) };
    REQUIRE(expanded_text(u8"${vcs} ${branch} ${revision}", git_lookup) == u8"git feature/login a1b2c3d");

    const gitman::repository_snapshot svn { make_svn_snapshot() };
    const gitman::variable_lookup svn_lookup { make_lookup(&svn) };
    REQUIRE(expanded_text(u8"${vcs} ${revision}", svn_lookup) == u8"svn 1234");
    // root의 끝 `/`를 떼고 `^`를 뺀 상대 reference를 잇는다.
    REQUIRE(expanded_text(u8"${svnUrl}", svn_lookup) == u8"https://svn.example.com/repo/branches/release");
}

TEST_CASE("Branch is unresolved when detached or on svn", "[function_buttons][t2]")
{
    gitman::repository_snapshot detached { make_git_snapshot() };
    detached.working_tree.is_detached = true;
    detached.current_reference = u8"(detached) a1b2c3d";
    REQUIRE(error_kind(u8"${branch}", make_lookup(&detached)) == gitman::expansion_error_kind::unresolved);

    const gitman::repository_snapshot svn { make_svn_snapshot() };
    const gitman::variable_expansion result { expand(u8"${branch}", make_lookup(&svn)) };
    REQUIRE(result.succeeded() == false);
    REQUIRE(result.error->kind == gitman::expansion_error_kind::unresolved);
    REQUIRE(result.error->name == u8"branch");

    // svnUrl은 git과 `^/`로 시작하지 않는 reference에서 풀리지 않는다.
    const gitman::repository_snapshot git { make_git_snapshot() };
    REQUIRE(error_kind(u8"${svnUrl}", make_lookup(&git)) == gitman::expansion_error_kind::unresolved);
    gitman::repository_snapshot odd_svn { make_svn_snapshot() };
    odd_svn.current_reference = u8"branches/release";
    REQUIRE(error_kind(u8"${svnUrl}", make_lookup(&odd_svn)) == gitman::expansion_error_kind::unresolved);
    odd_svn.current_reference = u8"^/trunk";
    odd_svn.svn_repository_root.clear();
    REQUIRE(error_kind(u8"${svnUrl}", make_lookup(&odd_svn)) == gitman::expansion_error_kind::unresolved);
}

TEST_CASE("User variables expand with project overrides", "[function_buttons][t2]")
{
    gitman::function_buttons_definition definition {};
    definition.variables.push_back({ u8"build_config", { std::u8string { u8"Debug" } } });
    definition.variables.push_back({ u8"out_dir", { std::u8string { u8"${projectRoot}\\out\\${build_config}" } } });
    definition.variables.push_back({ u8"token_dir", { std::u8string { u8"${env:TOOLS}\\bin" } } });
    definition.project_variables.push_back({ u8"frontend", { std::vector<gitman::function_variable> { { u8"build_config", { std::u8string { u8"Release" } } } } } });
    definition.project_variables.push_back({ u8"backend", { std::vector<gitman::function_variable> { { u8"build_config", { std::u8string { u8"Profile" } } } } } });

    gitman::variable_lookup lookup { make_lookup(nullptr, { { u8"TOOLS", u8"C:\\Tools" } }) };
    lookup.user = gitman::make_user_variables(definition, u8"frontend");

    // 같은 이름이면 저장소 값이 이긴다.
    REQUIRE(expanded_text(u8"${build_config}", lookup) == u8"Release");
    // 값 안의 내장 변수와 env는 한 단계 푼다.
    REQUIRE(expanded_text(u8"${token_dir}", lookup) == u8"C:\\Tools\\bin");
    // 사용자 변수가 사용자 변수를 참조하면 미정의다.
    const gitman::variable_expansion nested { expand(u8"${out_dir}", lookup) };
    REQUIRE(nested.succeeded() == false);
    REQUIRE(nested.error->kind == gitman::expansion_error_kind::undefined);
    REQUIRE(nested.error->name == u8"build_config");

    lookup.user = gitman::make_user_variables(definition, u8"other");
    REQUIRE(expanded_text(u8"${build_config}", lookup) == u8"Debug");
}

TEST_CASE("User variable values escape and report inner environment names", "[function_buttons][t2]")
{
    gitman::function_buttons_definition definition {};
    definition.variables.push_back({ u8"template", { std::u8string { u8"a$${projectId}b" } } });
    definition.variables.push_back({ u8"secret", { std::u8string { u8"--key=${env:MISSING_KEY}" } } });

    gitman::variable_lookup lookup { make_lookup(nullptr) };
    lookup.user = gitman::make_user_variables(definition, u8"frontend");

    // 값 안의 `$${`도 글자 그대로의 `${`이고 문서 출처다.
    const gitman::variable_expansion escaped { expand(u8"${template}", lookup) };
    REQUIRE(escaped.succeeded());
    REQUIRE(escaped.text == u8"a${projectId}b");
    REQUIRE(escaped.segments.size() == 1);
    REQUIRE(escaped.segments[0].origin == gitman::variable_origin::document);
    REQUIRE(escaped.segments[0].variable == u8"template");

    // 값 안의 미정의 env는 안쪽 이름으로, 위치는 바깥 참조로 알린다.
    const gitman::variable_expansion missing { expand(u8"x ${secret}", lookup) };
    REQUIRE(missing.succeeded() == false);
    REQUIRE(missing.error->kind == gitman::expansion_error_kind::environment_undefined);
    REQUIRE(missing.error->name == u8"MISSING_KEY");
    REQUIRE(missing.error->offset == 2);
    REQUIRE(missing.text.empty());
    REQUIRE(missing.segments.empty());
}

TEST_CASE("Unusable user variables are left out of the lookup", "[function_buttons][t2]")
{
    gitman::function_buttons_definition definition {};
    definition.variables.push_back({ u8"projectRoot", { std::u8string { u8"C:\\shadow" } } });
    definition.variables.push_back({ u8"env", { std::u8string { u8"x" } } });
    definition.variables.push_back({ u8"9lives", { std::u8string { u8"x" } } });
    definition.variables.push_back({ u8"number", { gitman::function_source_text { u8"5" } } });
    definition.variables.push_back({ u8"good", { std::u8string { u8"ok" } } });
    definition.project_variables.push_back({ u8"frontend", { gitman::function_source_text { u8"[1,2]" } } });

    const std::vector<gitman::user_variable> user { gitman::make_user_variables(definition, u8"frontend") };
    REQUIRE(user.size() == 1);
    REQUIRE(user.front().name == u8"good");

    gitman::variable_lookup lookup { make_lookup(nullptr) };
    lookup.user = user;
    // 내장 변수 이름을 쓴 사용자 변수는 내장 변수를 가리지 못한다.
    REQUIRE(expanded_text(u8"${projectRoot}", lookup) == u8"E:\\repos\\frontend");
    REQUIRE(error_kind(u8"${number}", lookup) == gitman::expansion_error_kind::undefined);
}

TEST_CASE("Environment variables expand at click time", "[function_buttons][t2]")
{
    const gitman::variable_lookup lookup { make_lookup(nullptr, { { u8"API_KEY", u8"s3cr3t" }, { u8"EMPTY", u8"" } }) };

    REQUIRE(expanded_text(u8"--key=${env:API_KEY}", lookup) == u8"--key=s3cr3t");
    REQUIRE(expanded_text(u8"[${env:EMPTY}]", lookup) == u8"[]");

    const gitman::variable_expansion missing { expand(u8"${env:MISSING}", lookup) };
    REQUIRE(missing.succeeded() == false);
    REQUIRE(missing.error->kind == gitman::expansion_error_kind::environment_undefined);
    REQUIRE(missing.error->name == u8"MISSING");

    // 환경 조회가 없으면 어떤 env 이름도 정의되지 않은 것으로 본다.
    gitman::variable_lookup without_environment {};
    REQUIRE(error_kind(u8"${env:API_KEY}", without_environment) == gitman::expansion_error_kind::environment_undefined);
}

TEST_CASE("Dollar escapes and stray dollars stay literal", "[function_buttons][t2]")
{
    const gitman::variable_lookup lookup { make_lookup(nullptr) };

    REQUIRE(expanded_text(u8"$${projectId}", lookup) == u8"${projectId}");
    REQUIRE(expanded_text(u8"a$${b", lookup) == u8"a${b");
    REQUIRE(expanded_text(u8"$5 $$x $", lookup) == u8"$5 $$x $");
    REQUIRE(expanded_text(u8"$$${projectId}", lookup) == u8"$${projectId}");
    REQUIRE(expanded_text(u8"cost: $ ${projectId}", lookup) == u8"cost: $ frontend");
    REQUIRE(expanded_text(u8"", lookup).empty());
}

TEST_CASE("Undefined and malformed references fail the expansion", "[function_buttons][t2]")
{
    const gitman::variable_lookup lookup { make_lookup(nullptr) };

    const gitman::variable_expansion undefined { expand(u8"--x ${nope}", lookup) };
    REQUIRE(undefined.succeeded() == false);
    REQUIRE(undefined.error->kind == gitman::expansion_error_kind::undefined);
    REQUIRE(undefined.error->name == u8"nope");
    REQUIRE(undefined.error->offset == 4);

    const gitman::variable_expansion unterminated { expand(u8"${projectRoot", lookup) };
    REQUIRE(unterminated.succeeded() == false);
    REQUIRE(unterminated.error->kind == gitman::expansion_error_kind::unterminated);
    REQUIRE(unterminated.error->offset == 0);

    REQUIRE(error_kind(u8"ok ${", lookup) == gitman::expansion_error_kind::unterminated);
    REQUIRE(error_kind(u8"${}", lookup) == gitman::expansion_error_kind::empty_name);
    REQUIRE(error_kind(u8"${env:}", lookup) == gitman::expansion_error_kind::empty_name);
    // 이름은 대소문자를 구분한다.
    REQUIRE(error_kind(u8"${projectroot}", lookup) == gitman::expansion_error_kind::undefined);
    REQUIRE(error_kind(u8"${ENV:PATH}", lookup) == gitman::expansion_error_kind::undefined);
}

TEST_CASE("Expanded values are not scanned again", "[function_buttons][t2]")
{
    gitman::project_definition project { make_project() };
    project.display_name = u8"name ${projectId} $${x}";

    gitman::variable_lookup lookup { make_lookup(nullptr, { { u8"LOOP", u8"${env:LOOP}${projectRoot}" } }) };
    lookup.builtins = gitman::make_builtin_variables(u8"E:/repos/team.version-list", project, nullptr);

    REQUIRE(expanded_text(u8"${projectName}", lookup) == u8"name ${projectId} $${x}");
    REQUIRE(expanded_text(u8"${env:LOOP}", lookup) == u8"${env:LOOP}${projectRoot}");
}

TEST_CASE("Each argument stays one argv element after expansion", "[function_buttons][t2]")
{
    gitman::project_definition project { make_project() };
    project.path.normalized = u8"E:/my repos/a&b";
    gitman::variable_lookup lookup {};
    lookup.builtins = gitman::make_builtin_variables(u8"E:/my repos/team.version-list", project, nullptr);

    const gitman::variable_expansion program { expand(u8"C:\\Tools\\run.exe", lookup) };
    const std::vector<gitman::variable_expansion> arguments { expand(u8"--root", lookup), expand(u8"${projectRoot}", lookup) };
    REQUIRE(arguments[1].text == u8"E:\\my repos\\a&b");

    const std::u8string line { gitman::build_display_command_line(program, arguments, join_for_display, mask_for_display) };
    // 공백이 든 값은 한 인자로 인용되고 `&`는 값의 일부다.
    REQUIRE(line == u8"\"C:\\Tools\\run.exe\" --root \"E:\\my repos\\a&b\"");
}

TEST_CASE("Segments record where each span of text came from", "[function_buttons][t2]")
{
    gitman::function_buttons_definition definition {};
    definition.variables.push_back({ u8"target", { std::u8string { u8"out-${branch}-${env:SUFFIX}" } } });

    const gitman::repository_snapshot snapshot { make_git_snapshot() };
    gitman::variable_lookup lookup { make_lookup(&snapshot, { { u8"SUFFIX", u8"x64" } }) };
    lookup.user = gitman::make_user_variables(definition, u8"frontend");

    const gitman::variable_expansion result { expand(u8"--id=${projectId}&${branch}|${env:SUFFIX}|${target}", lookup) };
    REQUIRE(result.succeeded());
    REQUIRE(result.text == u8"--id=frontend&feature/login|x64|out-feature/login-x64");

    struct expected_segment
    {
        std::u8string_view text {};
        gitman::variable_origin origin { gitman::variable_origin::literal };
        std::u8string_view variable {};
    };
    const std::vector<expected_segment> expected {
        { u8"--id=", gitman::variable_origin::literal, u8"" },
        { u8"frontend", gitman::variable_origin::document, u8"projectId" },
        { u8"&", gitman::variable_origin::literal, u8"" },
        { u8"feature/login", gitman::variable_origin::runtime, u8"branch" },
        { u8"|", gitman::variable_origin::literal, u8"" },
        { u8"x64", gitman::variable_origin::environment, u8"SUFFIX" },
        { u8"|", gitman::variable_origin::literal, u8"" },
        // 사용자 변수 값의 글자는 문서 출처이고, 그 안의 내장 변수와 env는 제 출처를 지킨다.
        { u8"out-", gitman::variable_origin::document, u8"target" },
        { u8"feature/login", gitman::variable_origin::runtime, u8"branch" },
        { u8"-", gitman::variable_origin::document, u8"target" },
        { u8"x64", gitman::variable_origin::environment, u8"SUFFIX" },
    };
    REQUIRE(result.segments.size() == expected.size());

    std::size_t covered { 0 };
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const gitman::expansion_segment& segment { result.segments[index] };
        REQUIRE(segment.offset == covered);
        REQUIRE(segment_text(result, segment) == expected[index].text);
        REQUIRE(segment.origin == expected[index].origin);
        REQUIRE(segment.variable == expected[index].variable);
        covered += segment.length;
    }
    // 구간은 결과 글 전체를 빈틈없이 덮는다.
    REQUIRE(covered == result.text.size());
}

TEST_CASE("Empty variable values keep a zero length segment", "[function_buttons][t2]")
{
    const gitman::variable_lookup lookup { make_lookup(nullptr, { { u8"EMPTY", u8"" } }) };

    const gitman::variable_expansion result { expand(u8"a${env:EMPTY}b", lookup) };
    REQUIRE(result.succeeded());
    REQUIRE(result.text == u8"ab");
    REQUIRE(result.segments.size() == 3);
    REQUIRE(result.segments[1].origin == gitman::variable_origin::environment);
    REQUIRE(result.segments[1].length == 0);
    // 표시용 글에서는 이름이 그대로 남는다.
    REQUIRE(gitman::display_text(result) == u8"a${env:EMPTY}b");
}

TEST_CASE("Escaped environment text looks like a name but carries no value", "[function_buttons][t2]")
{
    const gitman::variable_lookup lookup { make_lookup(nullptr, { { u8"X", u8"real-value" } }) };

    const gitman::variable_expansion escaped { expand(u8"$${env:X}", lookup) };
    const gitman::variable_expansion real { expand(u8"${env:X}", lookup) };
    REQUIRE(escaped.succeeded());
    REQUIRE(real.succeeded());

    // 표시용 글은 같지만 실제 argv의 값은 다르다.
    REQUIRE(gitman::display_text(escaped) == u8"${env:X}");
    REQUIRE(gitman::display_text(real) == u8"${env:X}");
    REQUIRE(escaped.text == u8"${env:X}");
    REQUIRE(real.text == u8"real-value");
    REQUIRE(escaped.segments.size() == 1);
    REQUIRE(escaped.segments[0].origin == gitman::variable_origin::literal);
}

TEST_CASE("Display command lines keep environment spans as names", "[function_buttons][t2]")
{
    gitman::function_buttons_definition definition {};
    definition.variables.push_back({ u8"auth", { std::u8string { u8"Bearer ${env:API_KEY}" } } });

    gitman::variable_lookup lookup { make_lookup(nullptr, { { u8"API_KEY", u8"s3cr3t-value" }, { u8"TOOLS", u8"C:\\Tools" } }) };
    lookup.user = gitman::make_user_variables(definition, u8"frontend");

    const gitman::variable_expansion program { expand(u8"${env:TOOLS}\\run.exe", lookup) };
    const std::vector<gitman::variable_expansion> arguments {
        expand(u8"--key", lookup),
        expand(u8"${env:API_KEY}", lookup),
        expand(u8"${auth}", lookup),
        expand(u8"${projectRoot}", lookup),
    };
    REQUIRE(program.succeeded());
    for (const gitman::variable_expansion& argument : arguments)
        REQUIRE(argument.succeeded());

    // 실제 argv에는 값이 들어간다.
    REQUIRE(program.text == u8"C:\\Tools\\run.exe");
    REQUIRE(arguments[1].text == u8"s3cr3t-value");

    const std::u8string line { gitman::build_display_command_line(program, arguments, join_for_display, mask_for_display) };
    REQUIRE(line == u8"\"${env:TOOLS}\\run.exe\" --key ${env:API_KEY} \"Bearer ${env:API_KEY}\" E:\\repos\\frontend");
    REQUIRE(line.find(u8"s3cr3t") == std::u8string::npos);
}

TEST_CASE("Display command lines apply secret masking", "[function_buttons][t2]")
{
    gitman::function_buttons_definition definition {};
    definition.variables.push_back({ u8"remote", { std::u8string { u8"https://kim:hunter2@git.example.com/repo.git" } } });

    gitman::variable_lookup lookup { make_lookup(nullptr) };
    lookup.user = gitman::make_user_variables(definition, u8"frontend");

    const gitman::variable_expansion program { expand(u8"C:\\Tools\\sync.exe", lookup) };
    const std::vector<gitman::variable_expansion> arguments {
        expand(u8"--password", lookup),
        expand(u8"plain-secret", lookup),
        expand(u8"${remote}", lookup),
    };

    const std::u8string line { gitman::build_display_command_line(program, arguments, join_for_display, mask_for_display) };
    REQUIRE(line == u8"\"C:\\Tools\\sync.exe\" --password *** https://kim:***@git.example.com/repo.git");

    // 마스킹 함수가 결과 전체에 한 번 적용되는지 본다.
    std::vector<std::u8string> masked_inputs {};
    const std::u8string recorded {
        gitman::build_display_command_line(program, arguments, join_for_display
            , [&masked_inputs](const std::u8string_view text)
            {
                masked_inputs.emplace_back(text);
                return std::u8string { u8"masked" };
            }
        )
    };
    REQUIRE(recorded == u8"masked");
    REQUIRE(masked_inputs.size() == 1);
    REQUIRE(masked_inputs.front() == u8"\"C:\\Tools\\sync.exe\" --password plain-secret https://kim:hunter2@git.example.com/repo.git");
}

TEST_CASE("Variable references can be scanned without values", "[function_buttons][t2]")
{
    const gitman::variable_scan scan { gitman::scan_variable_references(u8"${projectRoot}\\$${x}\\${env:HOME}\\${build_config}") };
    REQUIRE(scan.error.has_value() == false);
    REQUIRE(scan.references.size() == 3);
    REQUIRE(scan.references[0].name == u8"projectRoot");
    REQUIRE(scan.references[0].environment == false);
    REQUIRE(scan.references[1].name == u8"HOME");
    REQUIRE(scan.references[1].environment);
    REQUIRE(scan.references[2].name == u8"build_config");

    const gitman::variable_scan broken { gitman::scan_variable_references(u8"${a} ${") };
    REQUIRE(broken.error.has_value());
    REQUIRE(broken.error->kind == gitman::expansion_error_kind::unterminated);
    REQUIRE(broken.error->offset == 5);
}

TEST_CASE("Function names follow the documented rules", "[function_buttons][t2]")
{
    REQUIRE(gitman::is_function_button_id(u8"build"));
    REQUIRE(gitman::is_function_button_id(u8"open-unity_2"));
    REQUIRE(gitman::is_function_button_id(std::u8string(64, u8'a')));
    REQUIRE(gitman::is_function_button_id(std::u8string(65, u8'a')) == false);
    REQUIRE(gitman::is_function_button_id(u8"") == false);
    REQUIRE(gitman::is_function_button_id(u8"Build") == false);
    REQUIRE(gitman::is_function_button_id(u8"a/b") == false);

    REQUIRE(gitman::is_function_variable_name(u8"build_config"));
    REQUIRE(gitman::is_function_variable_name(u8"_Unity2"));
    REQUIRE(gitman::is_function_variable_name(u8"2x") == false);
    REQUIRE(gitman::is_function_variable_name(u8"a-b") == false);
    REQUIRE(gitman::is_function_variable_name(u8"") == false);

    REQUIRE(gitman::is_reserved_variable_name(u8"env"));
    REQUIRE(gitman::is_reserved_variable_name(u8"projectRoot"));
    REQUIRE(gitman::is_reserved_variable_name(u8"svnUrl"));
    REQUIRE(gitman::is_reserved_variable_name(u8"build_config") == false);
    REQUIRE(gitman::is_runtime_variable_name(u8"branch"));
    REQUIRE(gitman::is_runtime_variable_name(u8"projectRoot") == false);
}

TEST_CASE("Function buttons expose documented defaults", "[function_buttons][t2]")
{
    const gitman::function_button button {};
    REQUIRE(button.mode == gitman::function_run_mode::capture);
    REQUIRE(button.vcs == gitman::function_vcs_scope::any);
    REQUIRE(button.project_ids.has_value() == false);
    REQUIRE(button.confirm == false);
    REQUIRE(button.timeout_seconds == 3600);
    REQUIRE(button.placement.has_value() == false);
    REQUIRE(button.icon.has_value() == false);
    REQUIRE(button.working_directory() == u8"${projectRoot}");
    REQUIRE(button.icon_name() == u8"play");

    gitman::function_button configured { button };
    configured.cwd = { u8"${workspaceFolder}" };
    configured.icon = { u8"tools" };
    REQUIRE(configured.working_directory() == u8"${workspaceFolder}");
    REQUIRE(configured.icon_name() == u8"tools");

    // `"icon": ""`은 생략과 다른 값으로 남지만 보일 때는 기본 아이콘이다.
    gitman::function_button empty_icon { button };
    empty_icon.icon = { u8"" };
    REQUIRE(empty_icon.icon.has_value());
    REQUIRE(empty_icon != button);
    REQUIRE(empty_icon.icon_name() == u8"play");

    REQUIRE(gitman::parse_function_run_mode(u8"detached") == gitman::function_run_mode::detached);
    REQUIRE(gitman::parse_function_run_mode(u8"capture") == gitman::function_run_mode::capture);
    REQUIRE(gitman::parse_function_run_mode(u8"hidden").has_value() == false);
    REQUIRE(gitman::parse_function_vcs_scope(u8"git") == gitman::function_vcs_scope::git);
    REQUIRE(gitman::parse_function_vcs_scope(u8"svn") == gitman::function_vcs_scope::subversion);
    REQUIRE(gitman::parse_function_vcs_scope(u8"hg").has_value() == false);

    // 문서 모델에는 기능 버튼 절이 없는 것이 기본이다. 없으면 저장 때 그 키를 건드리지 않는다.
    const gitman::workspace_document document {};
    REQUIRE(document.function_buttons.has_value() == false);
}

TEST_CASE("Omitted placement means menu", "[function_buttons][t2]")
{
    const gitman::function_button omitted { make_button(u8"a", std::nullopt) };
    REQUIRE(gitman::declared_function_placement(omitted) == gitman::function_placement::menu);

    const std::vector<gitman::function_button_entry> buttons { omitted };
    const std::vector<std::optional<gitman::function_placement>> visible { gitman::resolve_visible_placements(buttons) };
    REQUIRE(visible.size() == 1);
    REQUIRE(visible[0] == gitman::function_placement::menu);

    REQUIRE(gitman::parse_function_placement(u8"card") == gitman::function_placement::card);
    REQUIRE(gitman::parse_function_placement(u8"bar") == gitman::function_placement::bar);
    REQUIRE(gitman::parse_function_placement(u8"menu") == gitman::function_placement::menu);
    REQUIRE(gitman::parse_function_placement(u8"Card").has_value() == false);
    REQUIRE(gitman::function_placement_name(gitman::function_placement::bar) == u8"bar");
}

TEST_CASE("Fourth card and unknown placements are visible as bar", "[function_buttons][t2]")
{
    std::vector<gitman::function_button_entry> buttons {
        make_button(u8"one", u8"card"),
        make_button(u8"two", u8"card"),
        // 검증에 실패한 항목은 화면에 나오지 않으므로 card 수에 들지 않는다.
        gitman::function_source_text { u8"{\"id\":\"broken\",\"placement\":\"card\"}" },
        make_button(u8"three", u8"bar"),
        make_button(u8"four", u8"card"),
        make_button(u8"five", u8"card"),
        make_button(u8"six", u8"side"),
        make_button(u8"seven", std::nullopt),
    };
    const std::vector<gitman::function_button_entry> original { buttons };

    const std::vector<std::optional<gitman::function_placement>> visible { gitman::resolve_visible_placements(buttons) };
    REQUIRE(visible.size() == buttons.size());
    REQUIRE(visible[0] == gitman::function_placement::card);
    REQUIRE(visible[1] == gitman::function_placement::card);
    REQUIRE(visible[2].has_value() == false);
    REQUIRE(visible[3] == gitman::function_placement::bar);
    REQUIRE(visible[4] == gitman::function_placement::card);
    REQUIRE(visible[5] == gitman::function_placement::bar);
    REQUIRE(visible[6] == gitman::function_placement::bar);
    REQUIRE(visible[7] == gitman::function_placement::menu);

    // 모델 값(원문 문자열)은 그대로다.
    REQUIRE(buttons == original);
    REQUIRE(std::get<gitman::function_button>(buttons[5]).placement == u8"card");
    REQUIRE(std::get<gitman::function_button>(buttons[6]).placement == u8"side");
    REQUIRE(gitman::declared_function_placement(std::get<gitman::function_button>(buttons[6])).has_value() == false);

    // 앞 3개 중 하나를 card에서 내리면 넘쳐 있던 card가 카드로 올라온다.
    std::get<gitman::function_button>(buttons[0]).placement = u8"menu";
    const std::vector<std::optional<gitman::function_placement>> promoted { gitman::resolve_visible_placements(buttons) };
    REQUIRE(promoted[0] == gitman::function_placement::menu);
    REQUIRE(promoted[4] == gitman::function_placement::card);
    REQUIRE(promoted[5] == gitman::function_placement::card);
}

TEST_CASE("Program allow list accepts absolute exe and com paths", "[function_buttons][t4]")
{
    const std::vector<std::u8string_view> programs {
        u8"C:\\Tools\\run.exe",
        u8"C:\\Tools\\RUN.EXE",
        u8"C:\\Tools\\legacy.Com",
        u8"C:/Tools/run.exe",
        u8"\\\\server\\share\\tools\\run.exe",
        u8"C:\\Program Files\\도구\\빌드.exe",
    };
    for (const std::u8string_view program : programs)
    {
        INFO(reinterpret_cast<const char*>(std::u8string { program }.c_str()));
        REQUIRE_FALSE(gitman::check_function_program(program).has_value());
    }
}

TEST_CASE("Program allow list rejects scripts trailing dots spaces and relative paths", "[function_buttons][t4]")
{
    struct case_entry
    {
        std::u8string_view program {};
        gitman::function_program_rejection rejection { gitman::function_program_rejection::not_absolute };
    };

    const std::vector<case_entry> cases {
        { u8"run.bat", gitman::function_program_rejection::not_absolute },
        { u8"tools\\run.exe", gitman::function_program_rejection::not_absolute },
        { u8"C:run.exe", gitman::function_program_rejection::not_absolute },
        { u8"\\Tools\\run.exe", gitman::function_program_rejection::not_absolute },
        { u8"C:\\Tools\\run.bat", gitman::function_program_rejection::extension_not_allowed },
        { u8"C:\\Tools\\run.cmd", gitman::function_program_rejection::extension_not_allowed },
        { u8"C:\\Tools\\clean.ps1", gitman::function_program_rejection::extension_not_allowed },
        { u8"C:\\Tools\\run", gitman::function_program_rejection::extension_not_allowed },
        { u8"C:\\Tools\\.exe", gitman::function_program_rejection::extension_not_allowed },
        { u8"C:\\Tools\\", gitman::function_program_rejection::extension_not_allowed },
        { u8"C:\\Tools\\run.exe.bat", gitman::function_program_rejection::extension_not_allowed },
        { u8"C:\\Tools\\run.bat.", gitman::function_program_rejection::trailing_dot_or_space },
        { u8"C:\\Tools\\run.exe.", gitman::function_program_rejection::trailing_dot_or_space },
        { u8"C:\\Tools\\run.exe ", gitman::function_program_rejection::trailing_dot_or_space },
        { u8"C:\\Tools\\run.bat . .", gitman::function_program_rejection::trailing_dot_or_space },
    };

    for (const case_entry& entry : cases)
    {
        INFO(reinterpret_cast<const char*>(std::u8string { entry.program }.c_str()));
        const std::optional<gitman::function_program_rejection> rejection { gitman::check_function_program(entry.program) };
        REQUIRE(rejection.has_value());
        REQUIRE(*rejection == entry.rejection);
    }
}

TEST_CASE("Allow list messages name the field and suggest an interpreter", "[function_buttons][t4]")
{
    const std::u8string extension { gitman::function_program_rejection_message(gitman::function_program_rejection::extension_not_allowed) };
    REQUIRE(extension.starts_with(u8"program"));
    REQUIRE(extension.find(u8".exe 또는 .com") != std::u8string::npos);
    REQUIRE(extension.find(u8"powershell.exe -NoProfile -File") != std::u8string::npos);
    REQUIRE(extension.find(u8"cmd.exe /d /c") != std::u8string::npos);
    REQUIRE(gitman::function_program_rejection_message(gitman::function_program_rejection::not_absolute).starts_with(u8"program"));
    REQUIRE(gitman::function_program_rejection_message(gitman::function_program_rejection::trailing_dot_or_space).starts_with(u8"program"));
}

TEST_CASE("Cmd defense rejects metacharacters from runtime variables", "[function_buttons][t4]")
{
    gitman::repository_snapshot snapshot { make_git_snapshot() };
    snapshot.current_reference = u8"feature&calc";
    const gitman::variable_lookup lookup { make_lookup(&snapshot) };

    const gitman::variable_expansion program { expand(u8"C:\\Windows\\System32\\cmd.exe", lookup) };
    const std::vector<gitman::variable_expansion> arguments {
        expand(u8"/d", lookup),
        expand(u8"/c", lookup),
        expand(u8"${projectRoot}\\build.bat", lookup),
        expand(u8"--branch=${branch}", lookup),
    };

    const std::optional<gitman::cmd_injection_finding> finding { gitman::find_cmd_injection(program, arguments) };
    REQUIRE(finding.has_value());
    REQUIRE(finding->argument == std::optional<std::size_t> { 3u });
    REQUIRE(finding->origin == gitman::variable_origin::runtime);
    REQUIRE(finding->variable == u8"branch");
    REQUIRE(finding->character == u8'&');
    REQUIRE(gitman::cmd_injection_message(*finding).find(u8"${branch}") != std::u8string::npos);
    REQUIRE(gitman::cmd_injection_message(*finding).find(u8"(&)") != std::u8string::npos);
}

TEST_CASE("Cmd defense checks every metacharacter and environment values", "[function_buttons][t4]")
{
    const gitman::variable_expansion program { expand(u8"C:\\Windows\\System32\\CMD.EXE", make_lookup(nullptr)) };
    for (const std::u8string_view value : { u8"a%PATH%", u8"a&b", u8"a|b", u8"a^b", u8"a<b", u8"a>b", u8"a\"b", u8"a\nb", u8"a\rb" })
    {
        INFO(reinterpret_cast<const char*>(std::u8string { value }.c_str()));
        const gitman::variable_lookup lookup { make_lookup(nullptr, { { u8"TARGET", std::u8string { value } } }) };
        const std::vector<gitman::variable_expansion> arguments { expand(u8"/c", lookup), expand(u8"${env:TARGET}", lookup) };
        const std::optional<gitman::cmd_injection_finding> finding { gitman::find_cmd_injection(program, arguments) };
        REQUIRE(finding.has_value());
        REQUIRE(finding->origin == gitman::variable_origin::environment);
        REQUIRE(finding->variable == u8"TARGET");
        REQUIRE(gitman::cmd_injection_message(*finding).find(u8"${env:TARGET}") != std::u8string::npos);
    }

    const gitman::variable_lookup newline { make_lookup(nullptr, { { u8"TARGET", u8"a\nb" } }) };
    const std::vector<gitman::variable_expansion> arguments { expand(u8"${env:TARGET}", newline) };
    REQUIRE(gitman::cmd_injection_message(*gitman::find_cmd_injection(program, arguments)).find(u8"줄바꿈") != std::u8string::npos);
}

TEST_CASE("Cmd defense lets document and literal segments through", "[function_buttons][t4]")
{
    gitman::project_definition project { make_project() };
    project.display_name = u8"Tom & Jerry";
    gitman::variable_lookup lookup { make_lookup(nullptr) };
    lookup.builtins = gitman::make_builtin_variables(u8"E:/repos/team.version-list", project, nullptr);
    lookup.user = { { u8"flags", u8"a&b|c" } };

    const gitman::variable_expansion program { expand(u8"C:\\Windows\\System32\\cmd.exe", lookup) };
    const std::vector<gitman::variable_expansion> arguments {
        expand(u8"/c", lookup),
        expand(u8"echo a&b", lookup),
        expand(u8"${projectName}", lookup),
        expand(u8"${flags}", lookup),
    };
    REQUIRE_FALSE(gitman::find_cmd_injection(program, arguments).has_value());
}

TEST_CASE("Cmd defense follows runtime values through user variables", "[function_buttons][t4]")
{
    gitman::repository_snapshot snapshot { make_git_snapshot() };
    snapshot.current_reference = u8"x|y";
    gitman::variable_lookup lookup { make_lookup(&snapshot) };
    lookup.user = { { u8"target", u8"refs/${branch}" } };

    const gitman::variable_expansion program { expand(u8"C:\\Windows\\System32\\cmd.exe", lookup) };
    const std::vector<gitman::variable_expansion> arguments { expand(u8"${target}", lookup) };
    const std::optional<gitman::cmd_injection_finding> finding { gitman::find_cmd_injection(program, arguments) };
    REQUIRE(finding.has_value());
    REQUIRE(finding->variable == u8"branch");
    REQUIRE(finding->character == u8'|');
}

TEST_CASE("Cmd defense applies only when the program is cmd", "[function_buttons][t4]")
{
    gitman::repository_snapshot snapshot { make_git_snapshot() };
    snapshot.current_reference = u8"feature&calc";
    const gitman::variable_lookup lookup { make_lookup(&snapshot) };
    const std::vector<gitman::variable_expansion> arguments { expand(u8"${branch}", lookup) };

    REQUIRE_FALSE(gitman::find_cmd_injection(expand(u8"C:\\Tools\\run.exe", lookup), arguments).has_value());
    REQUIRE_FALSE(gitman::find_cmd_injection(expand(u8"C:\\Tools\\mycmd.exe", lookup), arguments).has_value());
    REQUIRE(gitman::is_cmd_program(u8"C:/Windows/System32/Cmd.Exe"));
    REQUIRE_FALSE(gitman::is_cmd_program(u8"C:\\Windows\\System32\\cmd.com"));
}
