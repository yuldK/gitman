#include "application/project_path_resolver.h"
#include "application/project_registration_service.h"
#include "domain/discovery.h"
#include "domain/function_trust.h"
#include "helpers/discovery_test_doubles.h"
#include "infrastructure/json_project_store.h"
#include "infrastructure/json_workspace_document.h"
#include "platform/win32/workspace_document_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#ifndef GITMAN_TEST_FIXTURE_DIRECTORY
#error GITMAN_TEST_FIXTURE_DIRECTORY must identify the schema fixture directory.
#endif

namespace {
    constexpr std::u8string_view test_document_path { u8"E:/repos/team.version-list" };

    std::u8string load_fixture(const std::string_view file_name)
    {
        const std::filesystem::path fixture_path { std::filesystem::path { GITMAN_TEST_FIXTURE_DIRECTORY } / std::string { file_name } };
        std::ifstream stream { fixture_path, std::ios::binary };
        REQUIRE(stream.is_open());
        const std::string bytes { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
        REQUIRE_FALSE(stream.bad());
        return {
            reinterpret_cast<const char8_t*>(bytes.data()),
            bytes.size(),
        };
    }

    std::u8string read_file(const std::u8string_view path)
    {
        std::ifstream stream { std::filesystem::path { path }, std::ios::binary };
        REQUIRE(stream.is_open());
        const std::string bytes { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
        return {
            reinterpret_cast<const char8_t*>(bytes.data()),
            bytes.size(),
        };
    }

    void write_file(const std::u8string_view path, const std::u8string_view bytes)
    {
        std::ofstream stream { std::filesystem::path { path }, std::ios::binary | std::ios::trunc };
        REQUIRE(stream.is_open());
        stream.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        stream.close();
        REQUIRE(stream.good());
    }

    // 저장 test가 디스크의 저장소 경로를 조회하지 않도록 lexical 규칙만 흉내 낸다.
    class fake_project_path_resolver final : public gitman::project_path_resolver
    {
    public:
        [[nodiscard]] gitman::project_path_resolution resolve(const std::u8string_view original_path, const std::u8string_view) noexcept override
        {
            try
            {
                return { std::u8string { original_path }, gitman::configured_path_state::available, std::nullopt };
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] bool normalized_equal(const std::u8string_view left, const std::u8string_view right) noexcept override
        {
            return left == right;
        }
    };

    const gitman::diagnostic* find_diagnostic(const std::span<const gitman::diagnostic> diagnostics, const std::u8string_view json_pointer) noexcept
    {
        for (const gitman::diagnostic& value : diagnostics)
        {
            if (value.source.json_pointer == json_pointer)
                return &value;
        }
        return nullptr;
    }

    std::size_t count_diagnostics(const std::span<const gitman::diagnostic> diagnostics, const std::u8string_view json_pointer) noexcept
    {
        std::size_t count { 0 };
        for (const gitman::diagnostic& value : diagnostics)
        {
            if (value.source.json_pointer == json_pointer)
                ++count;
        }
        return count;
    }

    std::u8string button_pointer(const std::size_t index)
    {
        std::u8string pointer { u8"/function_buttons/buttons/" };
        for (const char digit : std::to_string(index))
            pointer.push_back(static_cast<char8_t>(digit));
        return pointer;
    }

    bool is_function_button_pointer(const std::u8string_view pointer) noexcept
    {
        return pointer == u8"/function_buttons" || pointer.starts_with(u8"/function_buttons/");
    }

    const gitman::function_button& button_at(const gitman::function_buttons_definition& definition, const std::size_t index)
    {
        REQUIRE(index < definition.buttons.size());
        const gitman::function_button* const button { std::get_if<gitman::function_button>(&definition.buttons[index]) };
        REQUIRE(button != nullptr);
        return *button;
    }

    const gitman::function_source_text& source_at(const gitman::function_buttons_definition& definition, const std::size_t index)
    {
        REQUIRE(index < definition.buttons.size());
        const gitman::function_source_text* const source { std::get_if<gitman::function_source_text>(&definition.buttons[index]) };
        REQUIRE(source != nullptr);
        return *source;
    }

    gitman::workspace_document_parse_result parse(const std::u8string_view source)
    {
        return gitman::parse_workspace_document_json(source, test_document_path);
    }

    // 신뢰 해시 비교용 문서다. 두 번째 버튼은 id 규칙을 어겨 원문으로 남는다.
    std::u8string digest_document(const std::u8string_view label, const std::u8string_view icon, const std::u8string_view placement, const std::u8string_view program)
    {
        std::u8string source { u8R"({"schema_version":1,"function_buttons":{"variables":{"tool":"C:\\Tools"},"buttons":[)" };
        source.append(u8R"({"id":"build","label":")");
        source.append(label);
        source.append(u8R"(","icon":")");
        source.append(icon);
        source.append(u8R"(","placement":")");
        source.append(placement);
        source.append(u8R"(","program":")");
        source.append(program);
        source.append(u8R"(","args":["${projectRoot}"]},)");
        source.append(u8R"({"id":"Bad Id","label":")");
        source.append(label);
        source.append(u8R"(","icon":")");
        source.append(icon);
        source.append(u8R"(","placement":")");
        source.append(placement);
        source.append(u8R"(","program":"C:\\Tools\\x.exe"}]},"projects":[{"id":"alpha","path":"alpha"}]})");
        return source;
    }

    // 한 줄짜리 문서다. 분기 하나씩을 고정하는 test가 쓴다.
    std::u8string document_with(const std::u8string_view function_buttons, const std::u8string_view projects)
    {
        std::u8string source { u8R"({"schema_version":1,"function_buttons":)" };
        source.append(function_buttons);
        source.append(u8R"(,"projects":)");
        source.append(projects);
        source.push_back(u8'}');
        return source;
    }

    std::u8string numbered(const std::u8string_view prefix, const std::size_t index)
    {
        std::u8string text { prefix };
        for (const char digit : std::to_string(index))
            text.push_back(static_cast<char8_t>(digit));
        return text;
    }

    std::u8string repeated(const std::u8string_view unit, const std::size_t count)
    {
        std::u8string text {};
        for (std::size_t index = 0; index < count; ++index)
            text.append(unit);
        return text;
    }

    std::u8string digest_input_of(const std::u8string_view source)
    {
        const gitman::workspace_document_parse_result result { parse(source) };
        REQUIRE(result.document.has_value());
        REQUIRE(result.document->function_buttons.has_value());
        return result.document->function_buttons->source_digest_input;
    }
} // namespace

TEST_CASE("Function button fixture parses without warnings", "[function_buttons][t3]")
{
    const std::u8string source { load_fixture("function-buttons.version-list") };
    const gitman::workspace_document_parse_result result { parse(source) };

    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE_FALSE(result.has_warnings());
    REQUIRE(result.document->projects.size() == 2u);
    REQUIRE(result.document->function_buttons.has_value());

    const gitman::function_buttons_definition& definition { *result.document->function_buttons };
    REQUIRE(definition.variables.size() == 2u);
    REQUIRE(definition.variables[0].name == u8"unity");
    REQUIRE(definition.variables[1].name == u8"build_config");
    REQUIRE(definition.variables[1].value == std::variant<std::u8string, gitman::function_source_text> { std::u8string { u8"Debug" } });
    REQUIRE(definition.project_variables.size() == 1u);
    REQUIRE(definition.project_variables[0].project == u8"frontend");
    REQUIRE(definition.buttons.size() == 5u);

    const gitman::function_button& build { button_at(definition, 0) };
    REQUIRE(build.id == u8"build");
    REQUIRE(build.label == u8"빌드");
    REQUIRE(build.icon == std::optional<std::u8string> { u8"tools" });
    REQUIRE(build.program == u8"C:\\Program Files\\CMake\\bin\\cmake.exe");
    REQUIRE(build.args == std::vector<std::u8string> { u8"--build", u8"${projectRoot}\\build", u8"--config", u8"${build_config}" });
    REQUIRE(build.timeout_seconds == 1800);
    REQUIRE(build.project_ids == std::optional<std::vector<std::u8string>> { std::vector<std::u8string> { u8"frontend", u8"backend" } });
    REQUIRE(build.placement == std::optional<std::u8string> { u8"card" });

    const gitman::function_button& unity { button_at(definition, 1) };
    REQUIRE(unity.mode == gitman::function_run_mode::detached);
    REQUIRE(unity.program == u8"${unity}");

    const gitman::function_button& clean { button_at(definition, 2) };
    REQUIRE(clean.vcs == gitman::function_vcs_scope::git);
    REQUIRE(clean.confirm);

    const gitman::function_button& status { button_at(definition, 4) };
    REQUIRE(status.cwd == std::optional<std::u8string> { u8"${workspaceFolder}" });
    REQUIRE_FALSE(status.icon.has_value());
    REQUIRE_FALSE(status.project_ids.has_value());

    const std::vector<std::optional<gitman::function_placement>> placements { gitman::resolve_visible_placements(definition.buttons) };
    REQUIRE(placements[0] == gitman::function_placement::card);
    REQUIRE(placements[1] == gitman::function_placement::card);
    REQUIRE(placements[2] == gitman::function_placement::menu);
    REQUIRE(placements[3] == gitman::function_placement::bar);
}

TEST_CASE("Omitted placement reads as menu without a warning", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result { parse(load_fixture("function-buttons.version-list")) };
    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_warnings());

    const gitman::function_buttons_definition& definition { *result.document->function_buttons };
    const gitman::function_button& status { button_at(definition, 4) };
    REQUIRE_FALSE(status.placement.has_value());
    REQUIRE(gitman::resolve_visible_placements(definition.buttons)[4] == gitman::function_placement::menu);
}

TEST_CASE("Invalid function button items warn and keep their source in place", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result { parse(load_fixture("function-buttons-invalid.version-list")) };

    // 기능 버튼 진단은 모두 warning이라 문서와 project가 그대로 열린다 (3.8).
    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.document->projects.size() == 2u);
    for (const gitman::diagnostic& value : result.diagnostics)
    {
        REQUIRE(value.severity == gitman::diagnostic_severity::warning);
        REQUIRE(is_function_button_pointer(value.source.json_pointer));
        REQUIRE_FALSE(value.source.project_index.has_value());
    }
    // 첫 세 card는 아이콘을 생략해 같은 기본 아이콘이라 둘째·셋째에 아이콘 경고가 하나씩 붙는다 (V, 2.6).
    REQUIRE(result.diagnostics.size() == 26u);
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/1/icon") == 1u);
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/2/icon") == 1u);

    REQUIRE(result.document->function_buttons.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };
    REQUIRE(definition.buttons.size() == 32u);

    // 실패한 항목은 같은 자리에 원문으로 남는다.
    for (std::size_t index = 5; index <= 15; ++index)
    {
        static_cast<void>(source_at(definition, index));
        const gitman::diagnostic* const skipped { find_diagnostic(result.diagnostics, button_pointer(index)) };
        REQUIRE(skipped != nullptr);
        REQUIRE(skipped->code == gitman::diagnostic_code::invalid_function_button);
    }
    REQUIRE(source_at(definition, 5).json.find(u8"\"Bad Id\"") != std::u8string::npos);
    REQUIRE(source_at(definition, 6).json.find(u8"dup.exe") != std::u8string::npos);
    REQUIRE(source_at(definition, 15).json == u8"\"not an object\"");
    // 원문은 문서의 키 순서를 지킨다.
    REQUIRE(source_at(definition, 9).json == u8R"({"id":"badargs","label":"인자","program":"C:\\Tools\\x.exe","args":["a",1]})");

    // 통과한 항목은 실패 항목 사이에서도 문서 순서를 지킨다.
    REQUIRE(button_at(definition, 0).id == u8"build");
    REQUIRE(button_at(definition, 16).id == u8"future");
    REQUIRE(button_at(definition, 31).id == u8"fill-14");

    // 위치를 담은 문구다 (3.8).
    const gitman::diagnostic* const bad_id { find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/5") };
    REQUIRE(bad_id->message.starts_with(u8"function_buttons.buttons[5] (\"잘못된 id\"): "));
    const gitman::diagnostic* const not_object { find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/15") };
    REQUIRE(not_object->message.starts_with(u8"function_buttons.buttons[15]: "));
}

TEST_CASE("Function button warnings that keep the item", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result { parse(load_fixture("function-buttons-invalid.version-list")) };
    REQUIRE(result.document.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };

    // 4번째 card와 알 수 없는 placement는 건너뛰지 않고 bar로 보인다. 모델 값은 원문이다.
    const gitman::diagnostic* const fourth_card { find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/3/placement") };
    REQUIRE(fourth_card != nullptr);
    REQUIRE(fourth_card->code == gitman::diagnostic_code::function_button_warning);
    const gitman::diagnostic* const unknown_placement { find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/4/placement") };
    REQUIRE(unknown_placement != nullptr);
    REQUIRE(unknown_placement->code == gitman::diagnostic_code::function_button_warning);
    REQUIRE(button_at(definition, 3).placement == std::optional<std::u8string> { u8"card" });
    REQUIRE(button_at(definition, 4).placement == std::optional<std::u8string> { u8"sidebar" });
    const std::vector<std::optional<gitman::function_placement>> placements { gitman::resolve_visible_placements(definition.buttons) };
    REQUIRE(placements[3] == gitman::function_placement::bar);
    REQUIRE(placements[4] == gitman::function_placement::bar);

    // 첫 세 card는 경고하지 않는다.
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/0/placement") == 0u);
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/2/placement") == 0u);

    // 카드당 적용 기능은 20개까지다. alpha에 21번째로 적용되는 항목에 경고 하나를 낸다.
    const gitman::diagnostic* const over_limit { find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/31") };
    REQUIRE(over_limit != nullptr);
    REQUIRE(over_limit->code == gitman::diagnostic_code::function_button_warning);
    REQUIRE(over_limit->message.find(u8"alpha") != std::u8string::npos);
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/30") == 0u);

    // 알 수 없는 키, 문서에 없는 project id, detached의 timeout, 긴 label은 경고만 한다.
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/16/future_key")->code == gitman::diagnostic_code::unknown_field);
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/16/project_ids/1") != nullptr);
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/16/timeout_seconds") != nullptr);
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/17/label") != nullptr);
    const gitman::function_button& future { button_at(definition, 16) };
    REQUIRE(future.project_ids == std::optional<std::vector<std::u8string>> { std::vector<std::u8string> { u8"alpha", u8"ghost" } });
    REQUIRE(future.timeout_seconds == 10);
    // 이름 표에 없는 아이콘은 경고하지 않는다.
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/16/icon") == 0u);
    // 저장소 덮어쓰기에만 있는 변수와 `${env:}`는 정적 검사를 통과한다.
    REQUIRE(button_at(definition, 18).id == u8"fill-01");
    REQUIRE(button_at(definition, 19).id == u8"fill-02");
}

TEST_CASE("Invalid function variables warn and stay in the model", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result { parse(load_fixture("function-buttons-invalid.version-list")) };
    REQUIRE(result.document.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };

    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/future_section")->code == gitman::diagnostic_code::unknown_field);

    // 문서 순서를 지키고 잘못된 변수도 원문 보존을 위해 남긴다.
    REQUIRE(definition.variables.size() == 4u);
    REQUIRE(definition.variables[0].name == u8"tools");
    REQUIRE(definition.variables[1].name == u8"bad name");
    REQUIRE(definition.variables[2].name == u8"projectRoot");
    REQUIRE(definition.variables[3].name == u8"count");
    REQUIRE(gitman::is_usable_function_variable(definition.variables[0]));
    REQUIRE_FALSE(gitman::is_usable_function_variable(definition.variables[1]));
    REQUIRE_FALSE(gitman::is_usable_function_variable(definition.variables[2]));
    REQUIRE(definition.variables[3].value == std::variant<std::u8string, gitman::function_source_text> { gitman::function_source_text { u8"3" } });
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/variables/bad name")->code == gitman::diagnostic_code::invalid_function_variable);
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/variables/projectRoot")->code == gitman::diagnostic_code::invalid_function_variable);
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/variables/count")->code == gitman::diagnostic_code::invalid_function_variable);

    REQUIRE(definition.project_variables.size() == 3u);
    REQUIRE(definition.project_variables[0].project == u8"alpha");
    REQUIRE(definition.project_variables[1].project == u8"ghost");
    REQUIRE(definition.project_variables[2].project == u8"beta");
    REQUIRE(definition.project_variables[2].variables
        == std::variant<std::vector<gitman::function_variable>, gitman::function_source_text> { gitman::function_source_text { u8"\"not-an-object\"" } }
    );
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/project_variables/ghost")->code == gitman::diagnostic_code::function_button_warning);
    REQUIRE(find_diagnostic(result.diagnostics, u8"/function_buttons/project_variables/beta")->code == gitman::diagnostic_code::invalid_function_variable);
}

TEST_CASE("A function button section that is not an object opens without buttons", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result { parse(u8R"({"schema_version":1,"function_buttons":[1,2],"projects":[{"id":"alpha","path":"alpha"}]})") };
    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.document->projects.size() == 1u);
    REQUIRE_FALSE(result.document->function_buttons.has_value());
    REQUIRE(result.diagnostics.size() == 1u);
    REQUIRE(result.diagnostics[0].source.json_pointer == u8"/function_buttons");
    REQUIRE(result.diagnostics[0].code == gitman::diagnostic_code::invalid_function_buttons);
    REQUIRE(result.diagnostics[0].severity == gitman::diagnostic_severity::warning);

    // 키가 없거나 null이면 기능 버튼 없이 경고 없이 연다.
    const gitman::workspace_document_parse_result absent { parse(u8R"({"schema_version":1,"projects":[]})") };
    REQUIRE_FALSE(absent.document->function_buttons.has_value());
    REQUIRE_FALSE(absent.has_warnings());
    const gitman::workspace_document_parse_result null_value { parse(u8R"({"schema_version":1,"function_buttons":null,"projects":[]})") };
    REQUIRE_FALSE(null_value.document->function_buttons.has_value());
    REQUIRE_FALSE(null_value.has_warnings());
}

TEST_CASE("The function button key is known like other optional sections", "[function_buttons][t3]")
{
    // 구버전 gitman은 이 키를 알 수 없는 top-level 필드로 보존하고 경고 1건만 낸다
    // (unknown-fields fixture와 같은 방식). 이번 버전은 화이트리스트에 있어 경고가 없다.
    const gitman::workspace_document_parse_result known { parse(u8R"({"schema_version":1,"function_buttons":{"buttons":[]},"projects":[]})") };
    REQUIRE_FALSE(known.has_warnings());
    REQUIRE(known.document->function_buttons.has_value());
    REQUIRE(known.document->function_buttons->buttons.empty());

    const std::u8string source { u8R"({"schema_version":1,"function_buttons_v2":{"buttons":[{"id":"x"}]},"projects":[]})" };
    const gitman::workspace_document_parse_result future { parse(source) };
    REQUIRE(future.document.has_value());
    REQUIRE(future.diagnostics.size() == 1u);
    REQUIRE(future.diagnostics[0].code == gitman::diagnostic_code::unknown_field);
    REQUIRE(future.diagnostics[0].source.json_pointer == u8"/function_buttons_v2");
    REQUIRE(future.shadow.source_json == source);
}

TEST_CASE("Function button diagnostic code names are stable", "[function_buttons][t3]")
{
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::invalid_function_buttons) == std::u8string_view { u8"invalid_function_buttons" });
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::invalid_function_button) == std::u8string_view { u8"invalid_function_button" });
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::invalid_function_variable) == std::u8string_view { u8"invalid_function_variable" });
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::function_button_warning) == std::u8string_view { u8"function_button_warning" });
}

TEST_CASE("Trust digest input leaves out only placement", "[function_buttons][t3][r3][s1]")
{
    const std::u8string base { digest_input_of(digest_document(u8"빌드", u8"tools", u8"bar", u8"C:\\\\Tools\\\\build.exe")) };
    REQUIRE(base.empty() == false);
    REQUIRE(base.find(u8"\"label\"") != std::u8string::npos);
    REQUIRE(base.find(u8"\"icon\"") != std::u8string::npos);
    REQUIRE(base.find(u8"\"placement\"") == std::u8string::npos);
    REQUIRE(base.find(u8"build.exe") != std::u8string::npos);
    REQUIRE(base.find(u8"\"variables\"") != std::u8string::npos);

    // `bar`·`menu` 사이 배치는 해시 입력이 아니다 (D29, D31). `card`는 `Alt+N`이 가리키는 기능이라 넣는다 (D31).
    REQUIRE(digest_input_of(digest_document(u8"빌드", u8"tools", u8"menu", u8"C:\\\\Tools\\\\build.exe")) == base);
    REQUIRE(digest_input_of(digest_document(u8"빌드", u8"tools", u8"card", u8"C:\\\\Tools\\\\build.exe")) != base);
    // `label`(신뢰 dialog가 명령과 쌍으로 보인 이름)과 `icon`(`card` 버튼에 보이는 유일한 표시)은 해시 입력이다 (D29).
    REQUIRE(digest_input_of(digest_document(u8"빌드 다시", u8"tools", u8"bar", u8"C:\\\\Tools\\\\build.exe")) != base);
    REQUIRE(digest_input_of(digest_document(u8"빌드", u8"rocket", u8"bar", u8"C:\\\\Tools\\\\build.exe")) != base);
    // 실행에 쓰는 필드는 해시 입력을 바꾼다.
    REQUIRE(digest_input_of(digest_document(u8"빌드", u8"tools", u8"bar", u8"C:\\\\Tools\\\\other.exe")) != base);
}

TEST_CASE("Only the visible card placement and its order enter the trust digest input", "[function_buttons][s1][trust]")
{
    // D31: `card` 배치 여부와 `card` 안의 순서(`Alt+N` 번호)만 해시에 넣는다. 손 편집·pull로 `Alt+1~3`이 가리키는 기능이
    // 바뀌면 다시 묻고, `bar`·`menu` 사이 이동은 묻지 않는다.
    const auto digest_of = [](const std::u8string_view a, const std::u8string_view b, const std::u8string_view c, const std::u8string_view d)
    {
        const auto item = [](const std::u8string_view id, const std::u8string_view placement)
        {
            std::u8string text { u8R"({"id":")" };
            text.append(id);
            text.append(u8R"(","label":")");
            text.append(id);
            text.append(u8R"(","program":"C:\\x.exe")");
            if (placement.empty() == false)
            {
                text.append(u8R"(,"placement":")");
                text.append(placement);
                text.push_back(u8'"');
            }
            text.push_back(u8'}');
            return text;
        };
        std::u8string section { u8R"({"buttons":[)" };
        section.append(item(u8"a", a));
        section.push_back(u8',');
        section.append(item(u8"b", b));
        section.push_back(u8',');
        section.append(item(u8"c", c));
        section.push_back(u8',');
        section.append(item(u8"d", d));
        section.append(u8"]}");
        return digest_input_of(document_with(section, u8R"([{"id":"alpha","path":"alpha"}])"));
    };

    const std::u8string base { digest_of(u8"card", u8"bar", u8"menu", u8"") };

    SECTION("bar와 menu 사이 이동과 생략·알 수 없는 값은 같은 해시 입력")
    {
        REQUIRE(digest_of(u8"card", u8"menu", u8"bar", u8"") == base);
        REQUIRE(digest_of(u8"card", u8"", u8"menu", u8"bar") == base);
        REQUIRE(digest_of(u8"card", u8"sidebar", u8"menu", u8"") == base);
    }

    SECTION("card에 넣거나 빼면 다른 해시 입력")
    {
        REQUIRE(digest_of(u8"bar", u8"bar", u8"menu", u8"") != base);
        REQUIRE(digest_of(u8"card", u8"card", u8"menu", u8"") != base);
    }

    SECTION("Alt+N이 가리키는 기능이 바뀌면 다른 해시 입력")
    {
        const std::u8string two { digest_of(u8"card", u8"card", u8"menu", u8"") };
        // `Alt+1`이 a에서 b로 옮겨 간다.
        REQUIRE(digest_of(u8"bar", u8"card", u8"card", u8"") != two);
        REQUIRE(digest_of(u8"menu", u8"card", u8"card", u8"") == digest_of(u8"bar", u8"card", u8"card", u8""));
    }

    SECTION("넷째 card는 기능 줄로 보이므로 bar와 같은 해시 입력")
    {
        REQUIRE(digest_of(u8"card", u8"card", u8"card", u8"card") != digest_of(u8"card", u8"card", u8"card", u8"bar"));
        REQUIRE(digest_of(u8"card", u8"card", u8"card", u8"card") != digest_of(u8"card", u8"card", u8"bar", u8"card"));
    }
}

TEST_CASE("Filling the label of a failed function item changes the trust digest", "[function_buttons][r3][trust]")
{
    // label이 없거나 비어 검증에 실패한 항목은 신뢰 dialog에 보이지 않는다. 신뢰한 뒤 label만 채워 그 항목이 유효 버튼이
    // 되면 한 번도 보이지 않은 program이 확인 없이 돌 수 있으므로, 실패 항목의 원문은 배치 말고 모두 해시에 남는다 (D29).
    constexpr std::u8string_view projects { u8R"([{"id":"alpha","path":"alpha"}])" };
    constexpr std::u8string_view valid { u8R"({"id":"build","label":"빌드","program":"C:\\Tools\\build.exe"})" };
    const auto document_of = [&](const std::u8string_view failed)
    {
        std::u8string section { u8R"({"buttons":[)" };
        section.append(valid);
        section.push_back(u8',');
        section.append(failed);
        section.append(u8"]}");
        return document_with(section, projects);
    };
    const auto parsed_of = [](const std::u8string& source)
    {
        const gitman::workspace_document_parse_result result { parse(source) };
        REQUIRE(result.document.has_value());
        REQUIRE(result.document->function_buttons.has_value());
        return *result.document;
    };

    const std::u8string_view filled { u8R"({"id":"clean","label":"정리","program":"C:\\Tools\\wipe.exe","args":["-all"]})" };
    const gitman::workspace_document after { parsed_of(document_of(filled)) };
    REQUIRE(std::holds_alternative<gitman::function_button>(after.function_buttons->buttons[1]));
    const std::u8string after_digest { gitman::function_trust_digest(*after.function_buttons, after.projects) };

    SECTION("label이 없던 항목")
    {
        const gitman::workspace_document before { parsed_of(document_of(u8R"({"id":"clean","program":"C:\\Tools\\wipe.exe","args":["-all"]})")) };
        REQUIRE(std::holds_alternative<gitman::function_source_text>(before.function_buttons->buttons[1]));
        REQUIRE(before.function_buttons->source_digest_input != after.function_buttons->source_digest_input);
        REQUIRE(gitman::function_trust_digest(*before.function_buttons, before.projects) != after_digest);
    }

    SECTION("label이 비어 있던 항목")
    {
        const gitman::workspace_document before { parsed_of(document_of(u8R"({"id":"clean","label":"","program":"C:\\Tools\\wipe.exe","args":["-all"]})")) };
        REQUIRE(std::holds_alternative<gitman::function_source_text>(before.function_buttons->buttons[1]));
        REQUIRE(before.function_buttons->source_digest_input != after.function_buttons->source_digest_input);
        REQUIRE(gitman::function_trust_digest(*before.function_buttons, before.projects) != after_digest);
    }

    SECTION("배치만 다른 실패 항목은 같은 해시")
    {
        const gitman::workspace_document left { parsed_of(document_of(u8R"({"id":"clean","label":"","placement":"card","program":"C:\\Tools\\wipe.exe"})")) };
        const gitman::workspace_document right { parsed_of(document_of(u8R"({"id":"clean","label":"","placement":"menu","program":"C:\\Tools\\wipe.exe"})")) };
        REQUIRE(std::holds_alternative<gitman::function_source_text>(left.function_buttons->buttons[1]));
        REQUIRE(left.function_buttons->source_digest_input == right.function_buttons->source_digest_input);
    }
}

TEST_CASE("Invalid function button items survive a normal save", "[function_buttons][t3]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string document_path { directory.path_of(u8"team.version-list") };
    write_file(document_path, load_fixture("function-buttons-invalid.version-list"));

    gitman::win32::workspace_document_file_system file_system {};
    fake_project_path_resolver resolver {};
    gitman::json_project_store store { file_system, resolver };

    gitman::project_store_load_result loaded { store.load(document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE_FALSE(loaded.has_errors());
    const std::size_t warning_count { loaded.diagnostics.size() };
    const std::u8string digest_input { loaded.document->function_buttons->source_digest_input };

    // 카드 토글처럼 기능 버튼과 무관한 변경을 저장한다. 기능 버튼 경고가 저장을 막지 않는다.
    loaded.document->projects[1].enabled = false;
    const gitman::project_store_save_result saved { store.save(document_path, *loaded.document, loaded.revision) };
    REQUIRE(saved.succeeded());

    const std::u8string bytes { read_file(document_path) };
    REQUIRE(bytes.find(u8"\"enabled\": false") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"Bad Id\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"dup.exe") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"not an object\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"sidebar\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"future_section\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"future_key\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"not-an-object\"") != std::u8string::npos);

    const gitman::project_store_load_result reloaded { store.load(document_path) };
    REQUIRE(reloaded.document.has_value());
    REQUIRE_FALSE(reloaded.has_errors());
    REQUIRE(reloaded.document->projects.size() == 2u);
    REQUIRE(reloaded.diagnostics.size() == warning_count);
    REQUIRE(reloaded.document->function_buttons->buttons.size() == 32u);
    // gitman이 키 순서를 바꿔 다시 써도 신뢰 해시 입력은 그대로다.
    REQUIRE(reloaded.document->function_buttons->source_digest_input == digest_input);
}

TEST_CASE("Invalid function button items survive a registration save", "[function_buttons][t3]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string document_path { directory.path_of(u8"team.version-list") };
    write_file(document_path, load_fixture("function-buttons-invalid.version-list"));
    const std::u8string repository { directory.make_directory(u8"scan\\gamma") };

    gitman::win32::workspace_document_file_system file_system {};
    fake_project_path_resolver resolver {};
    gitman::json_project_store store { file_system, resolver };

    const gitman::project_store_load_result loaded { store.load(document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE_FALSE(loaded.has_errors());
    const std::u8string digest_input { loaded.document->function_buttons->source_digest_input };

    gitman::discovery_candidate candidate {};
    candidate.directory_name = u8"gamma";
    candidate.absolute_path = repository;
    candidate.normalized_path = repository;
    candidate.kind = gitman::repository_kind::git;

    gitman::project_registration_service service { store, resolver };
    const std::vector<gitman::discovery_candidate> selected { candidate };
    const gitman::project_registration_result registered { service.register_candidates(*loaded.document, loaded.revision, selected) };
    REQUIRE(registered.succeeded);

    const std::u8string bytes { read_file(document_path) };
    REQUIRE(bytes.find(u8"\"gamma\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"Bad Id\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"not an object\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"sidebar\"") != std::u8string::npos);
    REQUIRE(bytes.find(u8"\"future_key\"") != std::u8string::npos);

    const gitman::project_store_load_result reloaded { store.load(document_path) };
    REQUIRE(reloaded.document.has_value());
    REQUIRE_FALSE(reloaded.has_errors());
    REQUIRE(reloaded.document->projects.size() == 3u);
    REQUIRE(reloaded.document->function_buttons->buttons.size() == 32u);
    REQUIRE(reloaded.document->function_buttons->source_digest_input == digest_input);
}

TEST_CASE("Overrides of projects outside the card list do not define variables", "[function_buttons][t3]")
{
    // 정적 검사의 "어느 한 project의 덮어쓰기"는 카드로 읽은 project만 센다 (3.7).
    const std::u8string source {
        document_with(
            u8R"({"project_variables":{"ghost":{"only_ghost":"C:\\G"},"alpha":{"only_alpha":"C:\\A"}},"buttons":[)"
            u8R"({"id":"ghost-var","label":"유령","program":"C:\\x.exe","args":["${only_ghost}"]},)"
            u8R"({"id":"alpha-var","label":"알파","program":"C:\\x.exe","args":["${only_alpha}"]}]})"
            , u8R"([{"id":"alpha","path":"alpha"}])"
        )
    };
    const gitman::workspace_document_parse_result result { parse(source) };
    REQUIRE(result.document.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };

    static_cast<void>(source_at(definition, 0));
    const gitman::diagnostic* const skipped { find_diagnostic(result.diagnostics, button_pointer(0)) };
    REQUIRE(skipped != nullptr);
    REQUIRE(skipped->code == gitman::diagnostic_code::invalid_function_button);
    REQUIRE(skipped->message.find(u8"${only_ghost}") != std::u8string::npos);
    REQUIRE(button_at(definition, 1).id == u8"alpha-var");

    // 카드 목록에 없는 project의 덮어쓰기도 원문째 남고 경고 하나를 낸다.
    REQUIRE(definition.project_variables.size() == 2u);
    const gitman::diagnostic* const ghost { find_diagnostic(result.diagnostics, u8"/function_buttons/project_variables/ghost") };
    REQUIRE(ghost != nullptr);
    REQUIRE(ghost->message.find(u8"카드 목록에 없는 project id") != std::u8string::npos);
}

TEST_CASE("Malformed function button subsections warn and read as empty", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result {
        parse(document_with(u8R"({"variables":[1],"project_variables":"x","buttons":{"id":"a"}})", u8R"([{"id":"alpha","path":"alpha"}])"))
    };
    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.document->projects.size() == 1u);
    REQUIRE(result.diagnostics.size() == 3u);
    for (const std::u8string_view pointer : { u8"/function_buttons/variables", u8"/function_buttons/project_variables", u8"/function_buttons/buttons" })
    {
        const gitman::diagnostic* const value { find_diagnostic(result.diagnostics, pointer) };
        REQUIRE(value != nullptr);
        REQUIRE(value->code == gitman::diagnostic_code::invalid_function_buttons);
        REQUIRE(value->severity == gitman::diagnostic_severity::warning);
    }

    // 절은 있으나 비어 있다. 원문은 shadow에만 남는다 (T10 쓰기 함수가 되살린다).
    REQUIRE(result.document->function_buttons.has_value());
    REQUIRE(result.document->function_buttons->variables.empty());
    REQUIRE(result.document->function_buttons->project_variables.empty());
    REQUIRE(result.document->function_buttons->buttons.empty());
}

TEST_CASE("A placement that is not a string keeps its JSON text", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result {
        parse(document_with(
            u8R"({"buttons":[)"
            u8R"({"id":"number","label":"숫자","program":"C:\\x.exe","placement":5},)"
            u8R"({"id":"null","label":"널","program":"C:\\x.exe","placement":null},)"
            u8R"({"id":"object","label":"객체","program":"C:\\x.exe","placement":{"x":1}}]})"
            , u8R"([])"
        ))
    };
    REQUIRE(result.document.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };

    // 건너뛰지 않고 알 수 없는 배치로 들어 bar로 보인다 (3.2).
    REQUIRE(button_at(definition, 0).placement == std::optional<std::u8string> { u8"5" });
    REQUIRE(button_at(definition, 1).placement == std::optional<std::u8string> { u8"null" });
    REQUIRE(button_at(definition, 2).placement == std::optional<std::u8string> { u8R"({"x":1})" });
    const std::vector<std::optional<gitman::function_placement>> placements { gitman::resolve_visible_placements(definition.buttons) };
    for (std::size_t index = 0; index < 3; ++index)
    {
        REQUIRE(placements[index] == gitman::function_placement::bar);
        std::u8string pointer { button_pointer(index) };
        pointer.append(u8"/placement");
        const gitman::diagnostic* const warning { find_diagnostic(result.diagnostics, pointer) };
        REQUIRE(warning != nullptr);
        REQUIRE(warning->code == gitman::diagnostic_code::function_button_warning);
    }
    REQUIRE(result.diagnostics.size() == 3u);
}

TEST_CASE("Timeout seconds accepts integers from 0 to 86400", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result {
        parse(document_with(
            u8R"({"buttons":[)"
            u8R"({"id":"zero","label":"0","program":"C:\\x.exe","timeout_seconds":0},)"
            u8R"({"id":"max","label":"최대","program":"C:\\x.exe","timeout_seconds":86400},)"
            u8R"({"id":"negative","label":"음수","program":"C:\\x.exe","timeout_seconds":-1},)"
            u8R"({"id":"real","label":"실수","program":"C:\\x.exe","timeout_seconds":1800.0},)"
            u8R"({"id":"over","label":"초과","program":"C:\\x.exe","timeout_seconds":86401},)"
            u8R"({"id":"text","label":"글","program":"C:\\x.exe","timeout_seconds":"60"}]})"
            , u8R"([])"
        ))
    };
    REQUIRE(result.document.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };
    REQUIRE(button_at(definition, 0).timeout_seconds == 0);
    REQUIRE(button_at(definition, 1).timeout_seconds == 86400);
    for (std::size_t index = 2; index < 6; ++index)
    {
        static_cast<void>(source_at(definition, index));
        const gitman::diagnostic* const skipped { find_diagnostic(result.diagnostics, button_pointer(index)) };
        REQUIRE(skipped != nullptr);
        REQUIRE(skipped->message.find(u8"timeout_seconds") != std::u8string::npos);
    }
    REQUIRE(result.diagnostics.size() == 4u);
}

TEST_CASE("Labels longer than 40 code points warn and are kept", "[function_buttons][t3]")
{
    std::u8string buttons { u8R"({"buttons":[{"id":"forty","program":"C:\\x.exe","label":")" };
    buttons.append(repeated(u8"가", 40));
    buttons.append(u8R"("},{"id":"forty-one","program":"C:\\x.exe","label":")");
    buttons.append(repeated(u8"가", 41));
    buttons.append(u8R"("}]})");
    const gitman::workspace_document_parse_result result { parse(document_with(buttons, u8R"([])")) };
    REQUIRE(result.document.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };

    REQUIRE(button_at(definition, 0).label == repeated(u8"가", 40));
    REQUIRE(button_at(definition, 1).label == repeated(u8"가", 41));
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/0/label") == 0u);
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/1/label") == 1u);
    REQUIRE(result.diagnostics.size() == 1u);
}

TEST_CASE("A failed item does not reserve its id", "[function_buttons][t3]")
{
    const gitman::workspace_document_parse_result result {
        parse(document_with(
            u8R"({"buttons":[)"
            u8R"({"id":"same","label":"프로그램 없음"},)"
            u8R"({"id":"same","label":"통과","program":"C:\\x.exe"},)"
            u8R"({"id":"same","label":"중복","program":"C:\\y.exe"}]})"
            , u8R"([])"
        ))
    };
    REQUIRE(result.document.has_value());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };

    static_cast<void>(source_at(definition, 0));
    REQUIRE(button_at(definition, 1).label == u8"통과");
    static_cast<void>(source_at(definition, 2));
    REQUIRE(count_diagnostics(result.diagnostics, button_pointer(1)) == 0u);
    REQUIRE(find_diagnostic(result.diagnostics, button_pointer(2))->message.find(u8"같은 id") != std::u8string::npos);
}

TEST_CASE("A wrong type in a known button field skips the button with one warning", "[function_buttons][t3]")
{
    struct case_entry
    {
        std::u8string_view item {};
        std::u8string_view reason {};
    };

    constexpr std::array cases {
        case_entry { u8R"({"id":"a","label":"A","program":"C:\\x.exe","icon":1})", u8"icon은" },
        case_entry { u8R"({"id":"b","label":"B","program":"C:\\x.exe","icon":null})", u8"icon은" },
        case_entry { u8R"({"id":"c","label":"C","program":"C:\\x.exe","cwd":false})", u8"cwd는" },
        case_entry { u8R"({"id":"d","label":"D","program":"C:\\x.exe","confirm":"yes"})", u8"confirm은" },
        case_entry { u8R"({"id":"e","label":"E","program":"C:\\x.exe","project_ids":"alpha"})", u8"project_ids는" },
        case_entry { u8R"({"id":"f","label":"F","program":"C:\\x.exe","project_ids":["alpha",2]})", u8"project_ids는" },
        case_entry { u8R"({"id":"g","label":"G","program":"C:\\x.exe","vcs":3})", u8"vcs는" },
        case_entry { u8R"({"id":"h","label":"H","program":"C:\\x.exe","vcs":"hg"})", u8"vcs는" },
        case_entry { u8R"({"id":"i","label":"I","program":"C:\\x.exe","mode":1})", u8"mode는" },
        case_entry { u8R"({"id":5,"label":"J","program":"C:\\x.exe"})", u8"id는" },
    };

    std::u8string buttons { u8R"({"buttons":[)" };
    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        if (index != 0)
            buttons.push_back(u8',');
        buttons.append(cases[index].item);
    }
    buttons.append(u8"]}");
    const gitman::workspace_document_parse_result result { parse(document_with(buttons, u8R"([{"id":"alpha","path":"alpha"}])")) };
    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };
    REQUIRE(definition.buttons.size() == cases.size());
    REQUIRE(result.diagnostics.size() == cases.size());

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        static_cast<void>(source_at(definition, index));
        REQUIRE(count_diagnostics(result.diagnostics, button_pointer(index)) == 1u);
        const gitman::diagnostic* const skipped { find_diagnostic(result.diagnostics, button_pointer(index)) };
        REQUIRE(skipped->code == gitman::diagnostic_code::invalid_function_button);
        REQUIRE(skipped->message.find(cases[index].reason) != std::u8string::npos);
    }
}

TEST_CASE("Functions per card are counted with vcs scope and vcs hint", "[function_buttons][t3]")
{
    // svn 한정 기능 21개다. git 카드에는 세지 않고 svn 카드와 auto 카드에는 센다 (B4, B6).
    std::u8string buttons { u8R"({"buttons":[)" };
    for (std::size_t index = 0; index < 21; ++index)
    {
        if (index != 0)
            buttons.push_back(u8',');
        buttons.append(u8R"({"id":")");
        buttons.append(numbered(u8"svn-", index));
        buttons.append(u8R"(","label":"S","program":"C:\\x.exe","vcs":"svn"})");
    }
    buttons.append(u8"]}");
    const std::u8string_view projects {
        u8R"([{"id":"alpha","path":"alpha","vcs_hint":"git"},{"id":"beta","path":"beta","vcs_hint":"svn"},{"id":"gamma","path":"gamma","vcs_hint":"auto"}])"
    };
    const gitman::workspace_document_parse_result result { parse(document_with(buttons, projects)) };
    REQUIRE(result.document.has_value());
    REQUIRE(result.document->projects.size() == 3u);

    REQUIRE(count_diagnostics(result.diagnostics, button_pointer(19)) == 0u);
    REQUIRE(count_diagnostics(result.diagnostics, button_pointer(20)) == 2u);
    REQUIRE(result.diagnostics.size() == 2u);
    REQUIRE(result.diagnostics[0].message.find(u8"beta 카드") != std::u8string::npos);
    REQUIRE(result.diagnostics[1].message.find(u8"gamma 카드") != std::u8string::npos);
}

TEST_CASE("Each card over 20 functions gets one warning on its own 21st item", "[function_buttons][t3]")
{
    // 앞의 21개는 alpha에만, 뒤의 21개는 모든 카드에 적용된다. alpha는 20번 항목에서
    // 넘치고, beta는 뒤의 21개만 받아 41번 항목에서 넘친다.
    std::u8string buttons { u8R"({"buttons":[)" };
    for (std::size_t index = 0; index < 42; ++index)
    {
        if (index != 0)
            buttons.push_back(u8',');
        buttons.append(u8R"({"id":")");
        buttons.append(numbered(u8"fill-", index));
        buttons.append(u8R"(","label":"F","program":"C:\\x.exe")");
        if (index < 21)
            buttons.append(u8R"(,"project_ids":["alpha"])");
        buttons.push_back(u8'}');
    }
    buttons.append(u8"]}");
    const gitman::workspace_document_parse_result result {
        parse(document_with(buttons, u8R"([{"id":"alpha","path":"alpha"},{"id":"beta","path":"beta"}])"))
    };
    REQUIRE(result.document.has_value());

    REQUIRE(result.diagnostics.size() == 2u);
    const gitman::diagnostic* const alpha { find_diagnostic(result.diagnostics, button_pointer(20)) };
    REQUIRE(alpha != nullptr);
    REQUIRE(alpha->message.find(u8"alpha 카드") != std::u8string::npos);
    const gitman::diagnostic* const beta { find_diagnostic(result.diagnostics, button_pointer(41)) };
    REQUIRE(beta != nullptr);
    REQUIRE(beta->message.find(u8"beta 카드") != std::u8string::npos);
}

TEST_CASE("A static program outside the allow list skips only that button", "[function_buttons][t4]")
{
    const std::u8string source {
        document_with(
            u8R"({"buttons":[)"
            u8R"({"id":"before","label":"앞","program":"C:\\Tools\\x.exe"},)"
            u8R"({"id":"script","label":"스크립트","program":"C:\\Tools\\run.bat"},)"
            u8R"({"id":"dot","label":"점","program":"C:\\Tools\\run.bat."},)"
            u8R"({"id":"space","label":"공백","program":"C:\\Tools\\run.exe "},)"
            u8R"({"id":"relative","label":"상대","program":"tools\\run.exe"},)"
            u8R"({"id":"escaped","label":"이스케이프","program":"C:\\Tools\\$${x}.bat"},)"
            u8R"({"id":"after","label":"뒤","program":"C:\\Tools\\RUN.EXE"}]})"
            , u8R"([{"id":"alpha","path":"alpha"}])"
        )
    };
    const gitman::workspace_document_parse_result result { parse(source) };
    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.document->projects.size() == 1u);
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };
    REQUIRE(definition.buttons.size() == 7u);

    REQUIRE(button_at(definition, 0).id == u8"before");
    REQUIRE(button_at(definition, 6).id == u8"after");
    REQUIRE(count_diagnostics(result.diagnostics, button_pointer(0)) == 0u);
    REQUIRE(count_diagnostics(result.diagnostics, button_pointer(6)) == 0u);

    struct case_entry
    {
        std::size_t index {};
        std::u8string_view reason {};
    };

    constexpr std::array cases {
        case_entry { 1, u8"허용 목록에 없습니다" },
        case_entry { 2, u8"점이나 공백으로 끝납니다" },
        case_entry { 3, u8"점이나 공백으로 끝납니다" },
        case_entry { 4, u8"절대 경로여야 합니다" },
        case_entry { 5, u8"허용 목록에 없습니다" },
    };
    for (const case_entry& entry : cases)
    {
        static_cast<void>(source_at(definition, entry.index));
        REQUIRE(count_diagnostics(result.diagnostics, button_pointer(entry.index)) == 1u);
        const gitman::diagnostic* const skipped { find_diagnostic(result.diagnostics, button_pointer(entry.index)) };
        REQUIRE(skipped->code == gitman::diagnostic_code::invalid_function_button);
        REQUIRE(skipped->severity == gitman::diagnostic_severity::warning);
        REQUIRE(skipped->message.find(entry.reason) != std::u8string::npos);
    }
    REQUIRE(find_diagnostic(result.diagnostics, button_pointer(1))->message.find(u8"powershell.exe -NoProfile -File") != std::u8string::npos);
    REQUIRE(source_at(definition, 1).json == u8R"({"id":"script","label":"스크립트","program":"C:\\Tools\\run.bat"})");
    REQUIRE(result.diagnostics.size() == cases.size());
}

TEST_CASE("A program with variables passes the allow list when read", "[function_buttons][t4]")
{
    const gitman::workspace_document_parse_result result {
        parse(document_with(
            u8R"({"variables":{"tools":"C:\\Tools"},"buttons":[)"
            u8R"({"id":"user","label":"사용자","program":"${tools}\\run.bat"},)"
            u8R"({"id":"env","label":"환경","program":"${env:SCRIPT}"},)"
            u8R"({"id":"root","label":"루트","program":"${projectRoot}\\tools\\run"}]})"
            , u8R"([{"id":"alpha","path":"alpha"}])"
        ))
    };
    REQUIRE(result.document.has_value());
    REQUIRE(result.diagnostics.empty());
    const gitman::function_buttons_definition& definition { *result.document->function_buttons };
    REQUIRE(button_at(definition, 0).program == u8"${tools}\\run.bat");
    REQUIRE(button_at(definition, 1).program == u8"${env:SCRIPT}");
    REQUIRE(button_at(definition, 2).program == u8"${projectRoot}\\tools\\run");
}

namespace {
    // 쓰기 함수(function-buttons-design 3.9, T10) test의 한 번 저장이다. 원문을 임시 파일로 쓰고 읽어 모델을 고친 뒤
    // 저장하고 다시 읽는다. `change`가 없으면 기능 버튼과 무관한 변경(첫 카드 제외 토글)을 저장한다.
    struct saved_round_trip
    {
        gitman::project_store_load_result loaded {};
        std::u8string bytes {};
        gitman::project_store_load_result reloaded {};
    };

    saved_round_trip save_round_trip(const std::u8string_view source, const std::function<void(gitman::workspace_document&)>& change = {})
    {
        const gitman::testing::scoped_scan_directory directory {};
        REQUIRE(directory.available());
        const std::u8string document_path { directory.path_of(u8"team.version-list") };
        write_file(document_path, source);

        gitman::win32::workspace_document_file_system file_system {};
        fake_project_path_resolver resolver {};
        gitman::json_project_store store { file_system, resolver };

        saved_round_trip result {};
        result.loaded = store.load(document_path);
        REQUIRE(result.loaded.document.has_value());
        REQUIRE_FALSE(result.loaded.has_errors());
        gitman::workspace_document document { *result.loaded.document };
        if (change != nullptr)
            change(document);
        else if (document.projects.empty() == false)
            document.projects[0].enabled = document.projects[0].enabled == false;
        const gitman::project_store_save_result saved { store.save(document_path, document, result.loaded.revision) };
        REQUIRE(saved.succeeded());
        result.bytes = read_file(document_path);
        result.reloaded = store.load(document_path);
        REQUIRE(result.reloaded.document.has_value());
        REQUIRE_FALSE(result.reloaded.has_errors());
        return result;
    }

    gitman::function_button& mutable_button(gitman::workspace_document& document, const std::u8string_view id)
    {
        REQUIRE(document.function_buttons.has_value());
        gitman::function_button* found { nullptr };
        for (gitman::function_button_entry& entry : document.function_buttons->buttons)
        {
            gitman::function_button* const button { std::get_if<gitman::function_button>(&entry) };
            if (button != nullptr && button->id == id)
                found = button;
        }
        REQUIRE(found != nullptr);
        return *found;
    }

    std::vector<std::u8string> diagnostic_pointers(const std::span<const gitman::diagnostic> diagnostics)
    {
        std::vector<std::u8string> pointers {};
        for (const gitman::diagnostic& value : diagnostics)
            pointers.push_back(value.source.json_pointer);
        std::ranges::sort(pointers);
        return pointers;
    }
}

TEST_CASE("The function button writer round trips the section", "[function_buttons][t10][store]")
{
    const saved_round_trip trip { save_round_trip(load_fixture("function-buttons.version-list")) };

    // 다시 읽은 절이 쓰기 전과 같다 — 변수 순서, 실패 항목 자리, 신뢰 해시 입력까지 (`operator==`).
    REQUIRE(trip.reloaded.document->function_buttons == trip.loaded.document->function_buttons);
    REQUIRE_FALSE(trip.reloaded.has_warnings());
    // 생략한 필드에 기본값 키를 새로 만들지 않는다. 만들면 다음 열기의 신뢰 해시가 바뀐다 (3.10).
    REQUIRE(trip.bytes.find(u8"\"capture\"") == std::u8string::npos);
    REQUIRE(trip.bytes.find(u8"\"confirm\": false") == std::u8string::npos);
    REQUIRE(trip.bytes.find(u8"3600") == std::u8string::npos);
    REQUIRE(trip.bytes.find(u8"\"icon\": \"play\"") == std::u8string::npos);
    REQUIRE(trip.bytes.find(u8"\"placement\": \"menu\"") != std::u8string::npos);
}

TEST_CASE("Unknown placement values round trip until a placement is chosen", "[function_buttons][t10][store]")
{
    const std::u8string source {
        document_with(
            u8R"({"buttons":[)"
            u8R"({"id":"a","label":"가","program":"C:\\x.exe","placement":"card"},)"
            u8R"({"id":"b","label":"나","program":"C:\\x.exe","placement":"card"},)"
            u8R"({"id":"c","label":"다","program":"C:\\x.exe","placement":"card"},)"
            u8R"({"id":"d","label":"라","program":"C:\\x.exe","placement":"card"},)"
            u8R"({"id":"side","label":"옆","program":"C:\\x.exe","placement":"sidebar"},)"
            u8R"({"id":"number","label":"숫자","program":"C:\\x.exe","placement":5},)"
            u8R"({"id":"null","label":"널","program":"C:\\x.exe","placement":null},)"
            u8R"({"id":"object","label":"객체","program":"C:\\x.exe","placement":{"x":1}}]})"
            , u8R"([{"id":"alpha","path":"alpha"}])"
        )
    };

    SECTION("다른 일로 저장해도 파일의 값이 그대로다")
    {
        const saved_round_trip trip { save_round_trip(source) };
        const gitman::function_buttons_definition& definition { *trip.reloaded.document->function_buttons };
        // 4번째 `card`는 `bar`로 보이지만 파일에는 `card` 그대로다 (3.2).
        REQUIRE(button_at(definition, 3).placement == std::optional<std::u8string> { u8"card" });
        REQUIRE(button_at(definition, 4).placement == std::optional<std::u8string> { u8"sidebar" });
        REQUIRE(button_at(definition, 5).placement == std::optional<std::u8string> { u8"5" });
        REQUIRE(button_at(definition, 6).placement == std::optional<std::u8string> { u8"null" });
        REQUIRE(button_at(definition, 7).placement == std::optional<std::u8string> { u8R"({"x":1})" });
        REQUIRE(trip.bytes.find(u8"\"placement\": 5") != std::u8string::npos);
        REQUIRE(trip.bytes.find(u8"\"placement\": null") != std::u8string::npos);
        REQUIRE(diagnostic_pointers(trip.reloaded.diagnostics) == diagnostic_pointers(trip.loaded.diagnostics));
    }

    SECTION("배치를 고르면 그때 새 값으로 쓴다")
    {
        const saved_round_trip trip {
            save_round_trip(source
                , [](gitman::workspace_document& document)
                {
                    mutable_button(document, u8"number").placement = std::u8string { u8"bar" };
                    mutable_button(document, u8"d").placement = std::u8string { u8"menu" };
                }
            )
        };
        const gitman::function_buttons_definition& definition { *trip.reloaded.document->function_buttons };
        REQUIRE(button_at(definition, 3).placement == std::optional<std::u8string> { u8"menu" });
        REQUIRE(button_at(definition, 5).placement == std::optional<std::u8string> { u8"bar" });
        // 고르지 않은 알 수 없는 값은 그대로다.
        REQUIRE(button_at(definition, 4).placement == std::optional<std::u8string> { u8"sidebar" });
        REQUIRE(button_at(definition, 6).placement == std::optional<std::u8string> { u8"null" });
    }
}

TEST_CASE("Failed items and unknown keys stay in place through the writer", "[function_buttons][t10][store]")
{
    const saved_round_trip trip { save_round_trip(load_fixture("function-buttons-invalid.version-list")) };
    const gitman::function_buttons_definition& before { *trip.loaded.document->function_buttons };
    const gitman::function_buttons_definition& after { *trip.reloaded.document->function_buttons };

    // 실패 항목은 원문 그대로 같은 자리다. 통과 항목도 같은 자리다 (중복 id의 뒤 항목 포함).
    REQUIRE(after.buttons.size() == before.buttons.size());
    std::size_t failed { 0 };
    for (std::size_t index = 0; index < before.buttons.size(); ++index)
    {
        REQUIRE(after.buttons[index].index() == before.buttons[index].index());
        if (const gitman::function_source_text* const source { std::get_if<gitman::function_source_text>(&before.buttons[index]) }; source != nullptr)
        {
            REQUIRE(source_at(after, index) == *source);
            ++failed;
        }
        else
            REQUIRE(button_at(after, index) == button_at(before, index));
    }
    REQUIRE(failed > 5u);
    // 쓸 수 없는 변수와 object가 아닌 덮어쓰기도 같은 자리다.
    REQUIRE(after.variables == before.variables);
    REQUIRE(after.project_variables == before.project_variables);

    // 알 수 없는 키는 버튼 안(`future_key`)과 절 바로 아래(`future_section`) 모두 남아 같은 경고가 다시 난다.
    REQUIRE(diagnostic_pointers(trip.reloaded.diagnostics) == diagnostic_pointers(trip.loaded.diagnostics));
    REQUIRE(find_diagnostic(trip.reloaded.diagnostics, u8"/function_buttons/future_section") != nullptr);
    REQUIRE(find_diagnostic(trip.reloaded.diagnostics, button_pointer(16) + u8"/future_key") != nullptr);
    REQUIRE(trip.bytes.find(u8"\"keep\": true") != std::u8string::npos);
    // 중복 id의 뒤 항목(실패)은 앞 항목의 template이 되지 않는다. 앞 항목은 알 수 없는 키 없이 그대로다.
    REQUIRE(button_at(after, 0).program == u8"C:\\Tools\\build.exe");
    REQUIRE(after.source_digest_input == before.source_digest_input);
}

TEST_CASE("A missing or non object function button section is left untouched", "[function_buttons][t10][store]")
{
    SECTION("키가 없으면 만들지 않는다")
    {
        const saved_round_trip trip { save_round_trip(u8R"({"schema_version":1,"projects":[{"id":"alpha","path":"alpha"}]})") };
        REQUIRE(trip.bytes.find(u8"function_buttons") == std::u8string::npos);
    }

    SECTION("object가 아니면 원문 그대로다")
    {
        const saved_round_trip trip { save_round_trip(u8R"({"schema_version":1,"function_buttons":[1,"two"],"projects":[{"id":"alpha","path":"alpha"}]})") };
        REQUIRE_FALSE(trip.reloaded.document->function_buttons.has_value());
        REQUIRE(find_diagnostic(trip.reloaded.diagnostics, u8"/function_buttons")->code == gitman::diagnostic_code::invalid_function_buttons);
        REQUIRE(trip.bytes.find(u8"\"two\"") != std::u8string::npos);
    }

    SECTION("null이면 null 그대로다")
    {
        const saved_round_trip trip { save_round_trip(u8R"({"schema_version":1,"function_buttons":null,"projects":[{"id":"alpha","path":"alpha"}]})") };
        REQUIRE(trip.bytes.find(u8"\"function_buttons\": null") != std::u8string::npos);
    }
}

TEST_CASE("Malformed function button subsections keep their source through the writer", "[function_buttons][t10][store]")
{
    // T3 "설계에서 달라진 것": 모델에 원문 자리가 없는 값은 쓰기 함수가 template의 원문 값을 되살린다.
    const saved_round_trip trip {
        save_round_trip(document_with(u8R"({"variables":[1],"project_variables":"x","buttons":{"id":"a"},"extra":{"y":2}})", u8R"([{"id":"alpha","path":"alpha"}])"))
    };
    REQUIRE(diagnostic_pointers(trip.reloaded.diagnostics) == diagnostic_pointers(trip.loaded.diagnostics));
    REQUIRE(trip.bytes.find(u8"\"project_variables\": \"x\"") != std::u8string::npos);
    REQUIRE(trip.bytes.find(u8"\"id\": \"a\"") != std::u8string::npos);
    REQUIRE(trip.bytes.find(u8"\"y\": 2") != std::u8string::npos);

    // null도 키가 없는 것과 같게 읽히지만 파일의 null은 그대로다.
    const saved_round_trip nulls { save_round_trip(document_with(u8R"({"variables":null,"buttons":null})", u8R"([{"id":"alpha","path":"alpha"}])")) };
    REQUIRE(nulls.bytes.find(u8"\"variables\": null") != std::u8string::npos);
    REQUIRE(nulls.bytes.find(u8"\"buttons\": null") != std::u8string::npos);
}

TEST_CASE("A placement change saved and read again keeps the trust digest", "[function_buttons][t10][s1][store][trust]")
{
    // 배치 메뉴(T10)는 모델의 배치만 바꿔 저장한다. 해시 입력은 파일 원문에서 만들므로, 쓴 뒤 다시 읽어도 같은
    // 해시여야 `bar`·`menu` 사이 재배치가 다음 열기에서 신뢰를 다시 묻지 않는다 (T7에서 넘어온 일, 3.10, D31).
    const saved_round_trip trip {
        save_round_trip(load_fixture("function-buttons.version-list")
            , [](gitman::workspace_document& document)
            {
                mutable_button(document, u8"test").placement = std::u8string { u8"menu" };
                mutable_button(document, u8"clean").placement = std::u8string { u8"bar" };
                mutable_button(document, u8"status").placement = std::u8string { u8"bar" };
            }
        )
    };
    const gitman::workspace_document& before { *trip.loaded.document };
    const gitman::workspace_document& after { *trip.reloaded.document };
    REQUIRE(button_at(*after.function_buttons, 3).placement == std::optional<std::u8string> { u8"menu" });
    REQUIRE(button_at(*after.function_buttons, 4).placement == std::optional<std::u8string> { u8"bar" });
    REQUIRE(after.function_buttons->source_digest_input == before.function_buttons->source_digest_input);
    REQUIRE(gitman::function_trust_digest(*after.function_buttons, after.projects) == gitman::function_trust_digest(*before.function_buttons, before.projects));

    // `card`에 넣으면 `Alt+3`이 생기므로 해시가 바뀐다 (D31). 저장 결과의 해시 입력은 다시 읽은 값과 같다.
    const saved_round_trip carded {
        save_round_trip(load_fixture("function-buttons.version-list")
            , [](gitman::workspace_document& document)
            {
                mutable_button(document, u8"test").placement = std::u8string { u8"card" };
            }
        )
    };
    REQUIRE(carded.reloaded.document->function_buttons->source_digest_input != carded.loaded.document->function_buttons->source_digest_input);

    // 실행에 쓰는 필드를 바꾸면 해시가 바뀐다 — 위의 같음이 우연이 아니다.
    const saved_round_trip changed {
        save_round_trip(load_fixture("function-buttons.version-list")
            , [](gitman::workspace_document& document)
            {
                mutable_button(document, u8"test").args.push_back(u8"--all");
            }
        )
    };
    REQUIRE(changed.reloaded.document->function_buttons->source_digest_input != changed.loaded.document->function_buttons->source_digest_input);
}

TEST_CASE("A placement change keeps stated default fields and the trust digest", "[function_buttons][t10][s1][store][trust]")
{
    // 기본값과 같은 값을 손으로 적은 문서다. 쓰기 함수는 원문에 있던 키를 기본값이어도 지우지 않는다 — 지우면 다시 읽은
    // 해시 입력이 바뀌어 재배치 뒤 다음 열기가 신뢰를 다시 묻는다 (3.10, T7에서 넘어온 일). `card`를 건드리면 해시가
    // 바뀌므로(D31) `bar`·`menu` 사이로만 옮긴다.
    const std::u8string source {
        document_with(
            u8R"({"buttons":[)"
            u8R"({"id":"a","label":"가","program":"C:\\x.exe","args":[],"mode":"capture","confirm":false,"timeout_seconds":3600,"placement":"bar"},)"
            u8R"({"id":"b","label":"나","program":"C:\\x.exe"}]})"
            , u8R"([{"id":"alpha","path":"alpha"}])"
        )
    };
    const saved_round_trip trip {
        save_round_trip(source
            , [](gitman::workspace_document& document)
            {
                mutable_button(document, u8"a").placement = std::u8string { u8"menu" };
                mutable_button(document, u8"b").placement = std::u8string { u8"bar" };
            }
        )
    };
    const gitman::workspace_document& before { *trip.loaded.document };
    const gitman::workspace_document& after { *trip.reloaded.document };
    REQUIRE(button_at(*after.function_buttons, 0).placement == std::optional<std::u8string> { u8"menu" });
    REQUIRE(button_at(*after.function_buttons, 1).placement == std::optional<std::u8string> { u8"bar" });
    REQUIRE(after.function_buttons->source_digest_input == before.function_buttons->source_digest_input);
    REQUIRE(gitman::function_trust_digest(*after.function_buttons, after.projects) == gitman::function_trust_digest(*before.function_buttons, before.projects));

    // 적었던 기본값 키는 파일에 남고, 적지 않았던 `b`에는 새로 생기지 않는다 (키가 하나씩만 있다).
    const auto occurrences = [&](const std::u8string_view needle)
    {
        std::size_t count { 0 };
        for (std::size_t position = trip.bytes.find(needle); position != std::u8string::npos; position = trip.bytes.find(needle, position + needle.size()))
            ++count;
        return count;
    };
    REQUIRE(occurrences(u8"\"args\": []") == 1u);
    REQUIRE(occurrences(u8"\"mode\": \"capture\"") == 1u);
    REQUIRE(occurrences(u8"\"confirm\": false") == 1u);
    REQUIRE(occurrences(u8"\"timeout_seconds\": 3600") == 1u);
}

TEST_CASE("A button added with the id of a failed item does not inherit its unknown keys", "[function_buttons][t12][store]")
{
    // 쓰기 함수의 template 대체를 좁혔다 (T10에서 넘어온 일, 3.9). 같은 id의 통과 항목이 없는 실패 항목 원문은 template이 되지 않는다.
    const std::u8string source {
        document_with(u8R"({"buttons":[{"id":"x","label":"엑스","program":"C:\\x.bat","future_key":1},{"id":"b","label":"비","program":"C:\\b.exe"}]})", u8R"([{"id":"alpha","path":"alpha"}])")
    };
    const saved_round_trip trip {
        save_round_trip(source
            , [](gitman::workspace_document& document)
            {
                gitman::function_button added {};
                added.id = u8"x";
                added.label = u8"새 엑스";
                added.program = u8"C:\\x.exe";
                document.function_buttons->buttons.emplace_back(added);
            }
        )
    };
    const gitman::function_buttons_definition& after { *trip.reloaded.document->function_buttons };
    REQUIRE(after.buttons.size() == 3u);
    REQUIRE(source_at(after, 0).json.find(u8"future_key") != std::u8string::npos);
    REQUIRE(button_at(after, 2).label == u8"새 엑스");
    std::size_t keys { 0 };
    for (std::size_t position = trip.bytes.find(u8"future_key"); position != std::u8string::npos; position = trip.bytes.find(u8"future_key", position + 1))
        ++keys;
    REQUIRE(keys == 1u);
}

TEST_CASE("A duplicated id with the same text as its failed copy keeps its template", "[function_buttons][t12][store]")
{
    // 통과 항목과 실패 항목(중복 id의 뒤 항목)의 글이 같으면 실패 항목이 든 수만큼만 건너뛰고 남는 객체가 통과 항목의 template이다.
    const std::u8string source {
        document_with(u8R"({"buttons":[{"id":"a","label":"가","program":"C:\\a.exe","keep":true},{"id":"a","label":"가","program":"C:\\a.exe","keep":true}]})", u8R"([{"id":"alpha","path":"alpha"}])")
    };
    const saved_round_trip trip {
        save_round_trip(source
            , [](gitman::workspace_document& document)
            {
                mutable_button(document, u8"a").label = u8"가나";
            }
        )
    };
    const gitman::function_buttons_definition& after { *trip.reloaded.document->function_buttons };
    REQUIRE(button_at(after, 0).label == u8"가나");
    REQUIRE(std::holds_alternative<gitman::function_source_text>(after.buttons[1]));
    std::size_t keeps { 0 };
    for (std::size_t position = trip.bytes.find(u8"\"keep\": true"); position != std::u8string::npos; position = trip.bytes.find(u8"\"keep\": true", position + 1))
        ++keeps;
    REQUIRE(keeps == 2u);
}

TEST_CASE("The save result carries the digest input of the written section", "[function_buttons][t12][r3][store][trust]")
{
    // 편집 dialog가 고친 정의는 저장 뒤 이 값으로 신뢰 해시를 다시 만든다 (3.10, 4.11). 다시 읽은 값과 같아야 한다.
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string document_path { directory.path_of(u8"team.version-list") };
    write_file(document_path, load_fixture("function-buttons.version-list"));
    gitman::win32::workspace_document_file_system file_system {};
    fake_project_path_resolver resolver {};
    gitman::json_project_store store { file_system, resolver };
    const gitman::project_store_load_result loaded { store.load(document_path) };
    REQUIRE(loaded.document.has_value());

    gitman::workspace_document placed { *loaded.document };
    mutable_button(placed, u8"clean").placement = std::u8string { u8"bar" };
    const gitman::project_store_save_result saved_placed { store.save(document_path, placed, loaded.revision) };
    REQUIRE(saved_placed.succeeded());
    // `bar`·`menu` 사이 배치만 바꾼 저장은 해시 입력이 그대로다 (2.7, 3.10, D29, D31).
    REQUIRE(saved_placed.function_digest_input == std::optional { loaded.document->function_buttons->source_digest_input });

    // 이름과 아이콘은 해시 입력이다 (D29). 저장 결과의 값이 다시 읽은 값과 같아야 편집 dialog가 그 해시로 신뢰를 갱신한다.
    gitman::workspace_document renamed { placed };
    mutable_button(renamed, u8"build").label = u8"새 이름";
    mutable_button(renamed, u8"build").icon = std::u8string { u8"rocket" };
    const gitman::project_store_save_result saved_renamed { store.save(document_path, renamed, *saved_placed.revision) };
    REQUIRE(saved_renamed.succeeded());
    REQUIRE(saved_renamed.function_digest_input != saved_placed.function_digest_input);
    const gitman::project_store_load_result renamed_reloaded { store.load(document_path) };
    REQUIRE(saved_renamed.function_digest_input == std::optional { renamed_reloaded.document->function_buttons->source_digest_input });

    gitman::workspace_document changed { renamed };
    mutable_button(changed, u8"build").args.push_back(u8"--all");
    const gitman::project_store_save_result saved_changed { store.save(document_path, changed, *saved_renamed.revision) };
    REQUIRE(saved_changed.succeeded());
    REQUIRE(saved_changed.function_digest_input != saved_renamed.function_digest_input);
    const gitman::project_store_load_result reloaded { store.load(document_path) };
    REQUIRE(saved_changed.function_digest_input == std::optional { reloaded.document->function_buttons->source_digest_input });

    // 절이 없는 문서의 저장은 해시 입력이 없다.
    const std::u8string plain_path { directory.path_of(u8"plain.version-list") };
    write_file(plain_path, u8R"({"schema_version":1,"projects":[{"id":"alpha","path":"alpha"}]})");
    const gitman::project_store_load_result plain { store.load(plain_path) };
    const gitman::project_store_save_result saved_plain { store.save(plain_path, *plain.document, plain.revision) };
    REQUIRE(saved_plain.succeeded());
    REQUIRE(saved_plain.function_digest_input.has_value() == false);
}

TEST_CASE("A null button list takes the buttons the editor added", "[function_buttons][t12][store]")
{
    // `buttons: null`은 키가 없는 것처럼 빈 목록으로 읽힌다. 편집 dialog로 버튼을 더하면 null 자리에 써야 다시 열어도
    // 남는다. 모델이 비었으면 null 원문 그대로다 (T10 test "Malformed function button subsections…").
    const saved_round_trip trip {
        save_round_trip(document_with(u8R"({"buttons":null,"extra":1})", u8R"([{"id":"alpha","path":"alpha"}])")
            , [](gitman::workspace_document& document)
            {
                REQUIRE(document.function_buttons.has_value());
                gitman::function_button button {};
                button.id = u8"function-1";
                button.label = u8"새 기능";
                button.program = u8"C:\\x.exe";
                document.function_buttons->buttons.emplace_back(std::move(button));
            }
        )
    };
    REQUIRE(trip.bytes.find(u8"\"buttons\": null") == std::u8string::npos);
    REQUIRE(trip.bytes.find(u8"\"extra\": 1") != std::u8string::npos);
    const gitman::function_buttons_definition& reloaded { *trip.reloaded.document->function_buttons };
    REQUIRE(reloaded.buttons.size() == 1u);
    REQUIRE(button_at(reloaded, 0).id == u8"function-1");
}

TEST_CASE("Card functions sharing an icon warn on the later one", "[function_buttons][v]")
{
    // 카드 열은 아이콘만 보인다 (2.6). 생략한 아이콘은 기본 아이콘이라 같은 아이콘이다. bar·menu와 4번째 card는 세지 않는다.
    const gitman::workspace_document_parse_result result {
        parse(document_with(
            u8R"({"buttons":[)"
            u8R"({"id":"a","label":"가","program":"C:\\x.exe","placement":"card"},)"
            u8R"({"id":"b","label":"나","program":"C:\\x.exe","icon":"tools","placement":"bar"},)"
            u8R"({"id":"c","label":"다","program":"C:\\x.exe","icon":"tools","placement":"card"},)"
            u8R"({"id":"d","label":"라","program":"C:\\x.exe","placement":"card"},)"
            u8R"({"id":"e","label":"마","program":"C:\\x.exe","placement":"card"}]})"
            , u8R"([{"id":"alpha","path":"alpha"}])"
        ))
    };
    REQUIRE(result.document.has_value());
    const gitman::diagnostic* const same_icon { find_diagnostic(result.diagnostics, u8"/function_buttons/buttons/3/icon") };
    REQUIRE(same_icon != nullptr);
    REQUIRE(same_icon->code == gitman::diagnostic_code::function_button_warning);
    REQUIRE(same_icon->severity == gitman::diagnostic_severity::warning);
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/2/icon") == 0u);
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/4/icon") == 0u);
    // 4번째 card는 bar로 보인다는 경고 하나뿐이다.
    REQUIRE(count_diagnostics(result.diagnostics, u8"/function_buttons/buttons/4/placement") == 1u);
    REQUIRE(result.diagnostics.size() == 2u);
}
