#include "infrastructure/json_workspace_document.h"
#include "infrastructure/workspace_document_paths.h"
#include "platform/win32/project_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {
    bool u8_equal(const std::u8string_view left, const std::u8string_view right) noexcept
    {
        return left == right;
    }

    class temporary_directory_fixture
    {
    public:
        temporary_directory_fixture()
        {
            std::error_code error {};
            const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
            REQUIRE_FALSE(static_cast<bool>(error));

            const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
            for (std::size_t attempt = 0; attempt < 100; ++attempt)
            {
                error.clear();
                const std::filesystem::path candidate { base / (L"gitman-path-tests-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
                if (std::filesystem::create_directory(candidate, error))
                {
                    root_ = candidate;
                    break;
                }
            }
            REQUIRE_FALSE(root_.empty());
        }

        ~temporary_directory_fixture()
        {
            std::error_code error {};
            std::filesystem::remove_all(root_, error);
        }

        temporary_directory_fixture(const temporary_directory_fixture&) = delete;
        temporary_directory_fixture& operator=(const temporary_directory_fixture&) = delete;

        [[nodiscard]] const std::filesystem::path& root() const noexcept
        {
            return root_;
        }

    private:
        std::filesystem::path root_ {};
    };

    void require_directories(const std::filesystem::path& path)
    {
        std::error_code error {};
        const bool created { std::filesystem::create_directories(path, error) };
        REQUIRE_FALSE(static_cast<bool>(error));
        REQUIRE((created || std::filesystem::is_directory(path)));
    }

    void create_file(const std::filesystem::path& path)
    {
        std::ofstream stream { path, std::ios::binary };
        REQUIRE(stream.is_open());
        stream << "gitman";
        stream.close();
        REQUIRE(stream.good());
    }

    const gitman::diagnostic* find_diagnostic(const gitman::workspace_document_parse_result& result, const gitman::diagnostic_code code, const std::u8string_view json_pointer) noexcept
    {
        for (const auto& diagnostic : result.diagnostics)
            if (diagnostic.code == code && diagnostic.source.json_pointer == json_pointer)
                return &diagnostic;
        return nullptr;
    }

    bool is_unavailable_state(const gitman::configured_path_state state) noexcept
    {
        return state == gitman::configured_path_state::missing || state == gitman::configured_path_state::inaccessible;
    }

    constexpr std::u8string_view workspace_path_source() noexcept
    {
        return u8R"({
    "schema_version": 1,
    "projects": [
        { "path": "ignored" },
        { "id": "first", "path": "Existing Repo" },
        { "id": "duplicate", "path": "existing repo/./" },
        { "id": "missing", "path": "Missing Repo" },
        { "id": "invalid", "path": "bad/*" },
        { "id": "file", "path": "project.txt" }
    ]
})";
    }
} // namespace

TEST_CASE("Project paths resolve relative to workspace documents", "[workspace][path]")
{
    temporary_directory_fixture fixture {};
    const std::filesystem::path document_directory { fixture.root() / L"workspace" / L"lists" };
    const std::filesystem::path repository { fixture.root() / L"workspace" / L"repositories" / L"한글 😀 공간" / L"repo" };
    require_directories(document_directory);
    require_directories(repository);

    const std::filesystem::path document_path { document_directory / L"active.version-list" };
    const gitman::win32::project_path_resolution result { gitman::win32::resolve_project_path(u8"../repositories/한글 😀 공간/./child/../repo/", document_path.u8string()) };

    REQUIRE(result.state == gitman::configured_path_state::available);
    REQUIRE_FALSE(result.native_error.has_value());
    REQUIRE(gitman::win32::normalized_project_paths_equal(result.normalized, repository.u8string()));
    REQUIRE(result.normalized.find(u8'/') == std::u8string::npos);
    REQUIRE(result.normalized.back() != u8'\\');
}

TEST_CASE("Project paths normalize drive extended and UNC forms", "[workspace][path]")
{
    temporary_directory_fixture fixture {};
    const std::filesystem::path repository { fixture.root() / L"Drive Repo" };
    require_directories(repository);

    std::u8string mixed_drive { repository.parent_path().u8string() };
    std::ranges::replace(mixed_drive, u8'\\', u8'/');
    mixed_drive.append(u8"/./Drive Repo//");
    const gitman::win32::project_path_resolution drive { gitman::win32::resolve_project_path(mixed_drive, {}) };
    REQUIRE(drive.state == gitman::configured_path_state::available);
    REQUIRE(gitman::win32::normalized_project_paths_equal(drive.normalized, repository.u8string()));

    std::u8string extended_drive { u8"\\\\?\\" };
    extended_drive.append(repository.u8string());
    extended_drive.push_back(u8'\\');
    const gitman::win32::project_path_resolution extended { gitman::win32::resolve_project_path(extended_drive, {}) };
    REQUIRE(extended.state == gitman::configured_path_state::available);
    REQUIRE(gitman::win32::normalized_project_paths_equal(extended.normalized, drive.normalized));

    constexpr std::u8string_view expected_unc { u8"\\\\127.0.0.1\\gitman-no-such-share\\repo" };
    const gitman::win32::project_path_resolution unc { gitman::win32::resolve_project_path(u8"//127.0.0.1/gitman-no-such-share/folder/../repo//", {}) };
    REQUIRE(unc.state != gitman::configured_path_state::invalid);
    REQUIRE(u8_equal(unc.normalized, expected_unc));
    REQUIRE((unc.state == gitman::configured_path_state::available || is_unavailable_state(unc.state)));

    const gitman::win32::project_path_resolution extended_unc { gitman::win32::resolve_project_path(u8"\\\\?\\UNC\\127.0.0.1\\gitman-no-such-share\\folder\\..\\repo\\", {}) };
    REQUIRE(extended_unc.state != gitman::configured_path_state::invalid);
    REQUIRE(u8_equal(extended_unc.normalized, expected_unc));
}

TEST_CASE("Project path states distinguish directories files missing and invalid inputs", "[workspace][path]")
{
    temporary_directory_fixture fixture {};
    const std::filesystem::path directory { fixture.root() / L"repository" };
    const std::filesystem::path file { fixture.root() / L"project.txt" };
    require_directories(directory);
    create_file(file);

    const gitman::win32::project_path_resolution available { gitman::win32::resolve_project_path(directory.u8string(), {}) };
    REQUIRE(available.state == gitman::configured_path_state::available);
    REQUIRE_FALSE(available.native_error.has_value());

    const gitman::win32::project_path_resolution not_directory { gitman::win32::resolve_project_path(file.u8string(), {}) };
    REQUIRE(not_directory.state == gitman::configured_path_state::not_directory);
    REQUIRE_FALSE(not_directory.native_error.has_value());

    const gitman::win32::project_path_resolution missing { gitman::win32::resolve_project_path((fixture.root() / L"missing").u8string(), {}) };
    REQUIRE(missing.state == gitman::configured_path_state::missing);
    REQUIRE(missing.native_error.has_value());

    const std::array invalid_paths {
        std::u8string { u8"C:/invalid/*/repo" },
        std::u8string { u8"C:drive-relative" },
        std::u8string { u8"\\root-relative" },
        std::u8string { u8"\\\\.\\pipe\\gitman" },
        std::u8string { u8"\\\\?\\Volume{00000000-0000-0000-0000-000000000000}\\repo" },
    };
    for (std::size_t index = 0; index < invalid_paths.size(); ++index)
    {
        CAPTURE(index);
        const gitman::win32::project_path_resolution invalid { gitman::win32::resolve_project_path(invalid_paths[index], {}) };
        REQUIRE(invalid.state == gitman::configured_path_state::invalid);
        REQUIRE(invalid.normalized.empty());
        REQUIRE(invalid.native_error.has_value());
    }

    std::u8string invalid_utf8 { u8"C:/invalid/" };
    invalid_utf8.push_back(static_cast<char8_t>(0xff));
    const gitman::win32::project_path_resolution invalid_encoding { gitman::win32::resolve_project_path(invalid_utf8, {}) };
    REQUIRE(invalid_encoding.state == gitman::configured_path_state::invalid);
    REQUIRE(invalid_encoding.native_error.has_value());

    const gitman::win32::project_path_resolution invalid_document { gitman::win32::resolve_project_path(u8"relative/repository", {}) };
    REQUIRE(invalid_document.state == gitman::configured_path_state::invalid);
    REQUIRE(invalid_document.native_error.has_value());
}

TEST_CASE("Attribute query errors map to distinct path states", "[workspace][path]")
{
    // 로컬 NTFS는 속성 조회를 부모 디렉터리 메타데이터로 처리하므로 대상에 deny
    // ACE를 붙여도 GetFileAttributesW가 실패하지 않는다. 접근 거부는 네트워크 공유나
    // 잠긴 파일에서만 실제로 발생하므로 `inaccessible` 분기는 오류 매핑으로 검증한다.
    REQUIRE(gitman::win32::project_path_state_from_error(ERROR_ACCESS_DENIED) == gitman::configured_path_state::inaccessible);
    REQUIRE(gitman::win32::project_path_state_from_error(ERROR_SHARING_VIOLATION) == gitman::configured_path_state::inaccessible);
    REQUIRE(gitman::win32::project_path_state_from_error(ERROR_FILE_NOT_FOUND) == gitman::configured_path_state::missing);
    REQUIRE(gitman::win32::project_path_state_from_error(ERROR_PATH_NOT_FOUND) == gitman::configured_path_state::missing);
    REQUIRE(gitman::win32::project_path_state_from_error(ERROR_BAD_NETPATH) == gitman::configured_path_state::missing);
    REQUIRE(gitman::win32::project_path_state_from_error(ERROR_INVALID_NAME) == gitman::configured_path_state::invalid);
    REQUIRE(gitman::win32::project_path_state_from_error(ERROR_FILENAME_EXCED_RANGE) == gitman::configured_path_state::invalid);
}

TEST_CASE("Project paths preserve Unicode spaces and long lexical values", "[workspace][path]")
{
    temporary_directory_fixture fixture {};
    std::u8string long_relative {};
    for (std::size_t index = 0; index < 10; ++index)
        long_relative.append(u8"아주 긴 경로 😀 0123456789abcdef/");
    long_relative.append(u8"repository");

    const std::filesystem::path document_path { fixture.root() / L"long-path.version-list" };
    const gitman::win32::project_path_resolution result { gitman::win32::resolve_project_path(long_relative, document_path.u8string()) };

    REQUIRE(result.state == gitman::configured_path_state::missing);
    REQUIRE(result.native_error.has_value());
    REQUIRE(result.normalized.size() > 260);
    REQUIRE(result.normalized.find(u8"아주 긴 경로 😀") != std::u8string::npos);
    REQUIRE(result.normalized.find(u8'/') == std::u8string::npos);
}

TEST_CASE("Workspace path resolution excludes invalid and duplicate projects", "[workspace][path]")
{
    temporary_directory_fixture fixture {};
    const std::filesystem::path existing { fixture.root() / L"Existing Repo" };
    const std::filesystem::path file { fixture.root() / L"project.txt" };
    require_directories(existing);
    create_file(file);

    constexpr std::u8string_view source { workspace_path_source() };
    const std::filesystem::path document_path { fixture.root() / L"workspace.version-list" };
    gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(source, document_path.u8string()) };
    REQUIRE(result.document.has_value());
    REQUIRE(result.document->projects.size() == 5);
    const std::vector<std::size_t> parsed_source_indices { 1, 2, 3, 4, 5 };
    REQUIRE(result.shadow.project_source_indices == parsed_source_indices);

    const std::unique_ptr<gitman::project_path_resolver> path_resolver { gitman::win32::make_project_path_resolver() };
    REQUIRE(path_resolver != nullptr);
    gitman::resolve_workspace_document_paths(result, *path_resolver);

    REQUIRE(result.document.has_value());
    REQUIRE(result.document->projects.size() == 3);
    REQUIRE(result.has_errors());
    REQUIRE(result.has_warnings());
    REQUIRE(result.diagnostics.size() == 5);
    REQUIRE(u8_equal(result.document->projects[0].id.value, u8"first"));
    REQUIRE(result.document->projects[0].path.state == gitman::configured_path_state::available);
    REQUIRE(u8_equal(result.document->projects[1].id.value, u8"missing"));
    REQUIRE(result.document->projects[1].path.state == gitman::configured_path_state::missing);
    REQUIRE(u8_equal(result.document->projects[2].id.value, u8"file"));
    REQUIRE(result.document->projects[2].path.state == gitman::configured_path_state::not_directory);

    const std::vector<std::size_t> resolved_source_indices { 1, 3, 5 };
    REQUIRE(result.shadow.project_source_indices == resolved_source_indices);

    const gitman::diagnostic* duplicate { find_diagnostic(result, gitman::diagnostic_code::duplicate_project_path, u8"/projects/2/path") };
    REQUIRE(duplicate != nullptr);
    REQUIRE(duplicate->source.project_index == 2);
    REQUIRE(duplicate->source.project_id.has_value());
    REQUIRE(u8_equal(*duplicate->source.project_id, u8"duplicate"));

    const gitman::diagnostic* missing { find_diagnostic(result, gitman::diagnostic_code::path_missing, u8"/projects/3/path") };
    REQUIRE(missing != nullptr);
    REQUIRE(missing->severity == gitman::diagnostic_severity::warning);
    REQUIRE(missing->native_error.has_value());

    const gitman::diagnostic* invalid { find_diagnostic(result, gitman::diagnostic_code::invalid_project_path, u8"/projects/4/path") };
    REQUIRE(invalid != nullptr);
    REQUIRE(invalid->severity == gitman::diagnostic_severity::error);
    REQUIRE(invalid->source.project_index == 4);

    const gitman::diagnostic* not_directory { find_diagnostic(result, gitman::diagnostic_code::path_not_directory, u8"/projects/5/path") };
    REQUIRE(not_directory != nullptr);
    REQUIRE(not_directory->severity == gitman::diagnostic_severity::warning);
}

TEST_CASE("Workspace path resolution ignores failed schema documents", "[workspace][path]")
{
    gitman::workspace_document_parse_result result { gitman::parse_workspace_document_json(u8"[]", u8"E:/work/invalid.version-list") };
    REQUIRE_FALSE(result.document.has_value());
    const std::vector<gitman::diagnostic> original_diagnostics { result.diagnostics };

    const std::unique_ptr<gitman::project_path_resolver> path_resolver { gitman::win32::make_project_path_resolver() };
    REQUIRE(path_resolver != nullptr);
    gitman::resolve_workspace_document_paths(result, *path_resolver);

    REQUIRE_FALSE(result.document.has_value());
    REQUIRE(result.diagnostics.size() == original_diagnostics.size());
    REQUIRE(result.diagnostics[0].code == original_diagnostics[0].code);
    REQUIRE(result.shadow.project_source_indices.empty());
}

TEST_CASE("Normalized project path equality is ordinal and case insensitive", "[workspace][path]")
{
    REQUIRE(gitman::win32::normalized_project_paths_equal(u8"C:\\Repositories\\Project", u8"c:\\repositories\\project"));
    REQUIRE(gitman::win32::normalized_project_paths_equal(u8"C:\\Repositories\\한글", u8"c:\\repositories\\한글"));
    REQUIRE_FALSE(gitman::win32::normalized_project_paths_equal(u8"C:\\Repositories\\First", u8"C:\\Repositories\\Second"));
}
