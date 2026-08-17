#include "infrastructure/json_workspace_document.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

#ifndef GITMAN_TEST_FIXTURE_DIRECTORY
#error GITMAN_TEST_FIXTURE_DIRECTORY must identify the schema fixture directory.
#endif

namespace {
    constexpr std::u8string_view test_document_path { u8"E:/work/example.version-list" };

    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

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

    const gitman::diagnostic* find_diagnostic(const gitman::workspace_document_parse_result& result, const gitman::diagnostic_code code, const std::u8string_view json_pointer) noexcept
    {
        for (const auto& diagnostic : result.diagnostics)
            if (diagnostic.code == code && diagnostic.source.json_pointer == json_pointer)
                return &diagnostic;
        return nullptr;
    }

    std::u8string document_with_project(const std::u8string_view project_json)
    {
        std::u8string source { u8"{\"schema_version\":1,\"projects\":[" };
        source.append(project_json);
        source.append(u8"]}");
        return source;
    }

    struct document_error_case
    {
        std::u8string_view source {};
        gitman::diagnostic_code code { gitman::diagnostic_code::unknown };
        std::u8string_view json_pointer {};
    };

    struct project_error_case
    {
        std::u8string_view project_json {};
        gitman::diagnostic_code code { gitman::diagnostic_code::unknown };
        std::u8string_view json_pointer {};
    };
} // namespace

TEST_CASE("Schema parser loads complete and defaulted projects", "[workspace][schema]")
{
    const std::u8string source { load_fixture("valid-complete.version-list") };
    const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, test_document_path) };

    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE_FALSE(result.has_warnings());
    REQUIRE(result.diagnostics.empty());
    REQUIRE(u8_equal(result.shadow.source_json, source));

    const gitman::workspace_document& document { *result.document };
    REQUIRE(document.schema_version == 1);
    REQUIRE(u8_equal(document.document_path, test_document_path));
    REQUIRE(document.projects.size() == 2);

    const gitman::project_definition& alpha { document.projects[0] };
    REQUIRE(u8_equal(alpha.id.value, u8"alpha"));
    REQUIRE(u8_equal(alpha.path.original, u8"D:/작업/alpha"));
    REQUIRE(alpha.path.normalized.empty());
    REQUIRE(alpha.path.state == gitman::configured_path_state::unchecked);
    REQUIRE(u8_equal(alpha.display_name, u8"알파 😀"));
    REQUIRE(alpha.hint == gitman::vcs_hint::git);
    REQUIRE_FALSE(alpha.enabled);
    REQUIRE(alpha.preferred_remote.has_value());
    REQUIRE(u8_equal(*alpha.preferred_remote, u8"upstream"));
    REQUIRE(alpha.svn_switch_targets.size() == 2);
    REQUIRE(u8_equal(alpha.svn_switch_targets[0], u8"https://svn.example.test/project/trunk"));
    REQUIRE(u8_equal(alpha.svn_switch_targets[1], u8"https://svn.example.test/project/branches/release"));

    const gitman::project_definition& beta { document.projects[1] };
    REQUIRE(u8_equal(beta.id.value, u8"beta"));
    REQUIRE(u8_equal(beta.path.original, u8"relative/beta/"));
    REQUIRE(u8_equal(beta.display_name, u8"beta"));
    REQUIRE(beta.hint == gitman::vcs_hint::automatic);
    REQUIRE(beta.enabled);
    REQUIRE_FALSE(beta.preferred_remote.has_value());
    REQUIRE(beta.svn_switch_targets.empty());
}

TEST_CASE("Schema parser reads workspace settings and keeps unknown keys", "[workspace][schema][settings]")
{
    const std::u8string source { load_fixture("workspace-settings.version-list") };
    const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, test_document_path) };

    REQUIRE(result.document.has_value());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.document->projects.size() == 1);
    REQUIRE(u8_equal(result.document->settings.git_executable, u8"C:/Program Files/Git/cmd/git.exe"));
    // 빈 값은 "지정하지 않음"이며 자동 탐색으로 간다.
    REQUIRE(result.document->settings.svn_executable.empty());
    REQUIRE_FALSE(result.document->settings.is_default());
    // 원문 byte를 그대로 보존해야 저장 시 알 수 없는 키를 되돌려 쓸 수 있다.
    REQUIRE(u8_equal(result.shadow.source_json, source));

    const gitman::diagnostic* unknown { find_diagnostic(result, gitman::diagnostic_code::unknown_field, u8"/settings/future~1option~0v2") };
    REQUIRE(unknown != nullptr);
    REQUIRE(unknown->severity == gitman::diagnostic_severity::warning);
    REQUIRE_FALSE(unknown->source.project_index.has_value());
}

TEST_CASE("Documents without settings keep working with defaults", "[workspace][schema][settings]")
{
    // 스키마 버전을 올리지 않았으므로 `settings`를 모르는 기존 문서도 그대로 열린다.
    for (const std::string_view fixture : { std::string_view { "valid-complete.version-list" }, std::string_view { "unknown-fields.version-list" } })
    {
        const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(load_fixture(fixture), test_document_path) };
        REQUIRE(result.document.has_value());
        REQUIRE(result.document->settings.is_default());
    }

    constexpr std::u8string_view explicit_null { u8"{\"schema_version\":1,\"settings\":null,\"projects\":[]}" };
    const gitman::workspace_document_parse_result null_result { gitman::parse_workspace_document_json(explicit_null, test_document_path) };
    REQUIRE(null_result.document.has_value());
    REQUIRE(null_result.document->settings.is_default());
    // `null`은 "값 없음"이므로 경고를 만들지 않는다.
    REQUIRE_FALSE(null_result.has_warnings());
    REQUIRE_FALSE(null_result.has_errors());
}

TEST_CASE("Settings executables must be absolute or empty", "[workspace][schema][settings]")
{
    // 상대 경로는 실행 시점의 현재 디렉터리에 따라 다른 프로그램을 가리킬 수 있다.
    constexpr std::u8string_view relative { u8"{\"schema_version\":1,\"settings\":{\"git_executable\":\"git.exe\"},\"projects\":[]}" };
    const gitman::workspace_document_parse_result relative_result { gitman::parse_workspace_document_json(relative, test_document_path) };
    REQUIRE(relative_result.has_errors());
    const gitman::diagnostic* rejected { find_diagnostic(relative_result, gitman::diagnostic_code::vcs_tool_path_invalid, u8"/settings/git_executable") };
    REQUIRE(rejected != nullptr);
    REQUIRE(rejected->severity == gitman::diagnostic_severity::error);
    // 거부한 값은 문서에 남기지 않는다.
    REQUIRE(relative_result.document.has_value());
    REQUIRE(relative_result.document->settings.git_executable.empty());

    constexpr std::u8string_view wrong_type { u8"{\"schema_version\":1,\"settings\":{\"svn_executable\":7},\"projects\":[]}" };
    const gitman::workspace_document_parse_result type_result { gitman::parse_workspace_document_json(wrong_type, test_document_path) };
    REQUIRE(find_diagnostic(type_result, gitman::diagnostic_code::invalid_project_field, u8"/settings/svn_executable") != nullptr);

    constexpr std::u8string_view wrong_root { u8"{\"schema_version\":1,\"settings\":[],\"projects\":[]}" };
    const gitman::workspace_document_parse_result root_result { gitman::parse_workspace_document_json(wrong_root, test_document_path) };
    REQUIRE(find_diagnostic(root_result, gitman::diagnostic_code::invalid_project_field, u8"/settings") != nullptr);

    constexpr std::u8string_view unc { u8"{\"schema_version\":1,\"settings\":{\"svn_executable\":\"\\\\\\\\build\\\\tools\\\\svn.exe\"},\"projects\":[]}" };
    const gitman::workspace_document_parse_result unc_result { gitman::parse_workspace_document_json(unc, test_document_path) };
    REQUIRE_FALSE(unc_result.has_errors());
    REQUIRE(unc_result.document.has_value());
    REQUIRE(u8_equal(unc_result.document->settings.svn_executable, u8"\\\\build\\tools\\svn.exe"));
}

TEST_CASE("Schema parser accepts minimal and empty documents", "[workspace][schema]")
{
    constexpr std::u8string_view source { u8"{\"schema_version\":1,\"projects\":[]}" };
    const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, test_document_path) };

    REQUIRE(result.document.has_value());
    REQUIRE(result.document->projects.empty());
    REQUIRE_FALSE(result.has_errors());
    REQUIRE_FALSE(result.has_warnings());
    REQUIRE(u8_equal(result.shadow.source_json, source));
}

TEST_CASE("Schema parser rejects malformed JSON and invalid roots", "[workspace][schema]")
{
    const std::u8string malformed_source { load_fixture("malformed.version-list") };
    const gitman::workspace_document_parse_result malformed { gitman::parse_workspace_document_json(malformed_source, test_document_path) };
    REQUIRE_FALSE(malformed.document.has_value());
    REQUIRE(malformed.has_errors());
    REQUIRE(find_diagnostic(malformed, gitman::diagnostic_code::malformed_document, u8"") != nullptr);
    REQUIRE(u8_equal(malformed.shadow.source_json, malformed_source));

    constexpr std::u8string_view array_root_source { u8"[]" };
    const gitman::workspace_document_parse_result array_root { gitman::parse_workspace_document_json(array_root_source, test_document_path) };
    REQUIRE_FALSE(array_root.document.has_value());
    REQUIRE(find_diagnostic(array_root, gitman::diagnostic_code::invalid_document_root, u8"") != nullptr);

    std::u8string invalid_utf8_source { u8"{\"schema_version\":1,\"projects\":[{\"id\":\"" };
    invalid_utf8_source.push_back(static_cast<char8_t>(0xff));
    invalid_utf8_source.append(u8"\",\"path\":\"repo\"}]}");
    const gitman::workspace_document_parse_result invalid_utf8 { gitman::parse_workspace_document_json(invalid_utf8_source, test_document_path) };
    REQUIRE_FALSE(invalid_utf8.document.has_value());
    REQUIRE(find_diagnostic(invalid_utf8, gitman::diagnostic_code::malformed_document, u8"") != nullptr);
    REQUIRE(u8_equal(invalid_utf8.shadow.source_json, invalid_utf8_source));
}

TEST_CASE("Schema parser rejects invalid document contracts", "[workspace][schema]")
{
    constexpr std::array cases {
        document_error_case { u8"{\"projects\":[]}", gitman::diagnostic_code::missing_schema_version, u8"/schema_version" },
        document_error_case { u8"{\"schema_version\":\"1\",\"projects\":[]}", gitman::diagnostic_code::invalid_schema_version, u8"/schema_version" },
        document_error_case { u8"{\"schema_version\":1.0,\"projects\":[]}", gitman::diagnostic_code::invalid_schema_version, u8"/schema_version" },
        document_error_case { u8"{\"schema_version\":1}", gitman::diagnostic_code::missing_projects, u8"/projects" },
        document_error_case { u8"{\"schema_version\":1,\"projects\":{}}", gitman::diagnostic_code::invalid_projects, u8"/projects" },
    };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        CAPTURE(index);
        const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(cases[index].source, test_document_path) };
        REQUIRE_FALSE(result.document.has_value());
        REQUIRE(result.has_errors());
        const gitman::diagnostic* diagnostic { find_diagnostic(result, cases[index].code, cases[index].json_pointer) };
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->severity == gitman::diagnostic_severity::error);
        REQUIRE(u8_equal(diagnostic->source.document_path, test_document_path));
    }
}

TEST_CASE("Schema parser rejects unsupported versions without changing source", "[workspace][schema]")
{
    const std::u8string legacy_source { load_fixture("legacy-version.version-list") };
    const gitman::workspace_document_parse_result legacy { gitman::parse_workspace_document_json(legacy_source, test_document_path) };
    REQUIRE_FALSE(legacy.document.has_value());
    REQUIRE(find_diagnostic(legacy, gitman::diagnostic_code::unsupported_legacy_schema, u8"/schema_version") != nullptr);
    REQUIRE(u8_equal(legacy.shadow.source_json, legacy_source));

    constexpr std::u8string_view negative_source { u8"{\"schema_version\":-1,\"projects\":[]}" };
    const gitman::workspace_document_parse_result negative { gitman::parse_workspace_document_json(negative_source, test_document_path) };
    REQUIRE_FALSE(negative.document.has_value());
    REQUIRE(find_diagnostic(negative, gitman::diagnostic_code::unsupported_legacy_schema, u8"/schema_version") != nullptr);
    REQUIRE(u8_equal(negative.shadow.source_json, negative_source));

    const std::u8string future_source { load_fixture("future-version.version-list") };
    const gitman::workspace_document_parse_result future { gitman::parse_workspace_document_json(future_source, test_document_path) };
    REQUIRE_FALSE(future.document.has_value());
    REQUIRE(find_diagnostic(future, gitman::diagnostic_code::unsupported_future_schema, u8"/schema_version") != nullptr);
    REQUIRE(u8_equal(future.shadow.source_json, future_source));
}

TEST_CASE("Schema parser returns valid projects from partially invalid input", "[workspace][schema]")
{
    const std::u8string source { load_fixture("partial-invalid.version-list") };
    const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, test_document_path) };

    REQUIRE(result.document.has_value());
    REQUIRE(result.has_errors());
    REQUIRE_FALSE(result.has_warnings());
    REQUIRE(result.document->projects.size() == 2);
    REQUIRE(u8_equal(result.document->projects[0].id.value, u8"valid-a"));
    REQUIRE(u8_equal(result.document->projects[1].id.value, u8"valid-b"));
    REQUIRE(result.diagnostics.size() == 4);

    const gitman::diagnostic* missing_id { find_diagnostic(result, gitman::diagnostic_code::missing_project_field, u8"/projects/1/id") };
    REQUIRE(missing_id != nullptr);
    REQUIRE(missing_id->source.project_index == 1);
    REQUIRE_FALSE(missing_id->source.project_id.has_value());

    const gitman::diagnostic* duplicate_id { find_diagnostic(result, gitman::diagnostic_code::duplicate_project_id, u8"/projects/3/id") };
    REQUIRE(duplicate_id != nullptr);
    REQUIRE(duplicate_id->source.project_index == 3);
    REQUIRE(duplicate_id->source.project_id.has_value());
    REQUIRE(u8_equal(*duplicate_id->source.project_id, u8"valid-a"));

    const gitman::diagnostic* invalid_hint { find_diagnostic(result, gitman::diagnostic_code::invalid_vcs_hint, u8"/projects/4/vcs_hint") };
    REQUIRE(invalid_hint != nullptr);
    REQUIRE(invalid_hint->source.project_index == 4);
    REQUIRE(invalid_hint->source.project_id.has_value());
    REQUIRE(u8_equal(*invalid_hint->source.project_id, u8"invalid-hint"));

    const gitman::diagnostic* invalid_item { find_diagnostic(result, gitman::diagnostic_code::invalid_project_field, u8"/projects/5") };
    REQUIRE(invalid_item != nullptr);
    REQUIRE(invalid_item->source.project_index == 5);
}

TEST_CASE("Schema parser validates every project field", "[workspace][schema]")
{
    constexpr std::array cases {
        project_error_case { u8"{\"path\":\"repo\"}", gitman::diagnostic_code::missing_project_field, u8"/projects/0/id" },
        project_error_case { u8"{\"id\":42,\"path\":\"repo\"}", gitman::diagnostic_code::invalid_project_field, u8"/projects/0/id" },
        project_error_case { u8"{\"id\":\"\",\"path\":\"repo\"}", gitman::diagnostic_code::invalid_project_id, u8"/projects/0/id" },
        project_error_case { u8"{\"id\":\"project\"}", gitman::diagnostic_code::missing_project_field, u8"/projects/0/path" },
        project_error_case { u8"{\"id\":\"project\",\"path\":42}", gitman::diagnostic_code::invalid_project_field, u8"/projects/0/path" },
        project_error_case { u8"{\"id\":\"project\",\"path\":\"\"}", gitman::diagnostic_code::invalid_project_path, u8"/projects/0/path" },
        project_error_case { u8"{\"id\":\"project\",\"path\":\"repo\",\"display_name\":null}", gitman::diagnostic_code::invalid_project_field, u8"/projects/0/display_name" },
        project_error_case { u8"{\"id\":\"project\",\"path\":\"repo\",\"vcs_hint\":\"hg\"}", gitman::diagnostic_code::invalid_vcs_hint, u8"/projects/0/vcs_hint" },
        project_error_case { u8"{\"id\":\"project\",\"path\":\"repo\",\"enabled\":\"true\"}", gitman::diagnostic_code::invalid_project_field, u8"/projects/0/enabled" },
        project_error_case { u8"{\"id\":\"project\",\"path\":\"repo\",\"preferred_remote\":42}", gitman::diagnostic_code::invalid_project_field, u8"/projects/0/preferred_remote" },
        project_error_case { u8"{\"id\":\"project\",\"path\":\"repo\",\"svn_switch_targets\":{}}", gitman::diagnostic_code::invalid_project_field, u8"/projects/0/svn_switch_targets" },
        project_error_case { u8"{\"id\":\"project\",\"path\":\"repo\",\"svn_switch_targets\":[42]}", gitman::diagnostic_code::invalid_project_field, u8"/projects/0/svn_switch_targets/0" },
    };

    for (std::size_t index = 0; index < cases.size(); ++index)
    {
        CAPTURE(index);
        const std::u8string source { document_with_project(cases[index].project_json) };
        const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, test_document_path) };
        REQUIRE(result.document.has_value());
        REQUIRE(result.document->projects.empty());
        REQUIRE(result.has_errors());
        const gitman::diagnostic* diagnostic { find_diagnostic(result, cases[index].code, cases[index].json_pointer) };
        REQUIRE(diagnostic != nullptr);
        REQUIRE(diagnostic->source.project_index == 0);
    }
}

TEST_CASE("Schema parser warns about unknown fields and preserves source bytes", "[workspace][schema]")
{
    const std::u8string source { load_fixture("unknown-fields.version-list") };
    const gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, test_document_path) };

    REQUIRE(result.document.has_value());
    REQUIRE(result.document->projects.size() == 1);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.has_warnings());
    REQUIRE(result.diagnostics.size() == 2);
    REQUIRE(u8_equal(result.shadow.source_json, source));

    const gitman::diagnostic* top_level { find_diagnostic(result, gitman::diagnostic_code::unknown_field, u8"/future~1settings~0v2") };
    REQUIRE(top_level != nullptr);
    REQUIRE(top_level->severity == gitman::diagnostic_severity::warning);
    REQUIRE_FALSE(top_level->source.project_index.has_value());
    REQUIRE_FALSE(top_level->source.project_id.has_value());

    const gitman::diagnostic* project_field { find_diagnostic(result, gitman::diagnostic_code::unknown_field, u8"/projects/0/project~1option~0v2") };
    REQUIRE(project_field != nullptr);
    REQUIRE(project_field->severity == gitman::diagnostic_severity::warning);
    REQUIRE(project_field->source.project_index == 0);
    REQUIRE(project_field->source.project_id.has_value());
    REQUIRE(u8_equal(*project_field->source.project_id, u8"alpha"));
}
