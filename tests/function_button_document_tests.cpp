#include "application/project_path_resolver.h"
#include "application/project_registration_service.h"
#include "domain/discovery.h"
#include "helpers/discovery_test_doubles.h"
#include "infrastructure/json_project_store.h"
#include "infrastructure/json_workspace_document.h"
#include "platform/win32/workspace_document_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
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
    REQUIRE(result.diagnostics.size() == 24u);

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

TEST_CASE("Trust digest input ignores display fields", "[function_buttons][t3]")
{
    const std::u8string base { digest_input_of(digest_document(u8"빌드", u8"tools", u8"card", u8"C:\\\\Tools\\\\build.exe")) };
    REQUIRE(base.empty() == false);
    REQUIRE(base.find(u8"\"label\"") == std::u8string::npos);
    REQUIRE(base.find(u8"\"icon\"") == std::u8string::npos);
    REQUIRE(base.find(u8"\"placement\"") == std::u8string::npos);
    REQUIRE(base.find(u8"build.exe") != std::u8string::npos);
    REQUIRE(base.find(u8"\"variables\"") != std::u8string::npos);

    // 실패해 원문으로 든 항목의 표시 필드도 뺀다.
    REQUIRE(digest_input_of(digest_document(u8"빌드 다시", u8"rocket", u8"bar", u8"C:\\\\Tools\\\\build.exe")) == base);
    REQUIRE(digest_input_of(digest_document(u8"빌드", u8"tools", u8"menu", u8"C:\\\\Tools\\\\build.exe")) == base);
    // 실행에 쓰는 필드는 해시 입력을 바꾼다.
    REQUIRE(digest_input_of(digest_document(u8"빌드", u8"tools", u8"card", u8"C:\\\\Tools\\\\other.exe")) != base);
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
