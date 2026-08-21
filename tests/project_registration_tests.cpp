#include "application/project_registration_service.h"
#include "helpers/discovery_test_doubles.h"
#include "infrastructure/json_project_store.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/workspace_document_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 등록 단위 test가 실제 디스크를 조회하지 않도록 lexical 규칙만 흉내 낸다.
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

    // 저장 호출을 기록하는 store 대역이다. "전체 거부 시 저장이 호출되지 않는다"를
    // 호출 수로 직접 단정한다.
    class fake_project_store final : public gitman::project_store
    {
    public:
        [[nodiscard]] gitman::project_store_load_result load(std::u8string_view) noexcept override
        {
            return {};
        }

        [[nodiscard]] gitman::project_store_save_result save(
            const std::u8string_view document_path, const gitman::workspace_document& document, const gitman::workspace_revision_token&) noexcept override
        {
            try
            {
                ++save_count_;
                last_document_path_ = document_path;
                last_saved_ = document;

                gitman::project_store_save_result result {};
                result.revision = { make_revision_token(revision_file_state::present, std::u8string { document_path }, {}, {}, {}) };
                return result;
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] std::size_t save_count() const noexcept
        {
            return save_count_;
        }

        [[nodiscard]] const gitman::workspace_document& last_saved() const noexcept
        {
            return last_saved_;
        }

        [[nodiscard]] const std::u8string& last_document_path() const noexcept
        {
            return last_document_path_;
        }

    private:
        std::size_t save_count_ { 0 };
        std::u8string last_document_path_ {};
        gitman::workspace_document last_saved_ {};
    };

    gitman::discovery_candidate make_candidate(const std::u8string_view name, const gitman::repository_kind kind = gitman::repository_kind::git)
    {
        gitman::discovery_candidate candidate {};
        candidate.directory_name = name;
        candidate.absolute_path = std::u8string { u8"C:\\scan\\" } + std::u8string { name };
        candidate.normalized_path = candidate.absolute_path;
        candidate.kind = kind;
        return candidate;
    }

    gitman::workspace_document make_document()
    {
        gitman::workspace_document document {};
        document.document_path = u8"C:\\workspace\\projects.version-list";
        return document;
    }

    bool has_rejection(const gitman::project_registration_result& result)
    {
        for (const gitman::diagnostic& value : result.diagnostics)
            if (value.code == gitman::diagnostic_code::registration_candidate_rejected && value.severity == gitman::diagnostic_severity::error)
                return true;
        return false;
    }
} // namespace

TEST_CASE("Unique registration ids append numeric suffixes from two", "[discovery][registration]")
{
    const std::vector<std::u8string> taken { u8"repo", u8"repo-2", u8"다른-저장소" };
    REQUIRE(gitman::make_unique_registration_id(u8"fresh", taken) == u8"fresh");
    REQUIRE(gitman::make_unique_registration_id(u8"repo", taken) == u8"repo-3");
    REQUIRE(gitman::make_unique_registration_id(u8"다른-저장소", taken) == u8"다른-저장소-2");
    REQUIRE(gitman::make_unique_registration_id(u8"repo-2", taken) == u8"repo-2-2");
}

TEST_CASE("Selected candidates become project definitions with the confirmed kind", "[discovery][registration]")
{
    fake_project_store store {};
    fake_project_path_resolver resolver {};
    gitman::project_registration_service service { store, resolver };

    gitman::workspace_document document { make_document() };
    document.settings.git_executable = u8"C:\\tools\\git.exe";
    gitman::project_definition existing {};
    existing.id.value = u8"existing";
    existing.path.original = u8"C:\\elsewhere\\existing";
    existing.path.normalized = u8"C:\\elsewhere\\existing";
    document.projects.push_back(existing);

    const std::vector<gitman::discovery_candidate> selected {
        make_candidate(u8"repo"),
        make_candidate(u8"svn-copy", gitman::repository_kind::subversion),
    };
    const gitman::project_registration_result result { service.register_candidates(document, {}, selected) };

    REQUIRE(result.succeeded);
    REQUIRE(store.save_count() == 1u);
    REQUIRE(store.last_document_path() == document.document_path);
    REQUIRE(result.revision.has_value());
    REQUIRE(result.saved_document.has_value());

    const gitman::workspace_document& saved { store.last_saved() };
    REQUIRE(saved.projects.size() == 3u);
    // 기존 항목과 문서 설정은 그대로 남는다.
    REQUIRE(saved.projects[0].id.value == u8"existing");
    REQUIRE(saved.settings.git_executable == u8"C:\\tools\\git.exe");

    const gitman::project_definition& git_project { saved.projects[1] };
    REQUIRE(git_project.id.value == u8"repo");
    REQUIRE(git_project.display_name == u8"repo");
    REQUIRE(git_project.path.original == u8"C:\\scan\\repo");
    REQUIRE(git_project.path.normalized == u8"C:\\scan\\repo");
    REQUIRE(git_project.hint == gitman::vcs_hint::git);
    REQUIRE(git_project.enabled);
    REQUIRE_FALSE(git_project.preferred_remote.has_value());
    REQUIRE(git_project.svn_switch_targets.empty());

    const gitman::project_definition& svn_project { saved.projects[2] };
    REQUIRE(svn_project.id.value == u8"svn-copy");
    REQUIRE(svn_project.hint == gitman::vcs_hint::subversion);
    REQUIRE(svn_project.svn_switch_targets.empty());
}

TEST_CASE("Registration ids stay unique against the document and the same batch", "[discovery][registration]")
{
    fake_project_store store {};
    fake_project_path_resolver resolver {};
    gitman::project_registration_service service { store, resolver };

    gitman::workspace_document document { make_document() };
    gitman::project_definition existing {};
    existing.id.value = u8"repo";
    existing.path.normalized = u8"C:\\elsewhere\\repo";
    document.projects.push_back(existing);

    // 같은 이름의 디렉터리가 다른 스캔 루트에서 온 경우다. 경로는 다르므로 등록은
    // 허용되고 id만 접미사로 달라진다.
    gitman::discovery_candidate other { make_candidate(u8"repo") };
    other.absolute_path = u8"C:\\other-scan\\repo";
    other.normalized_path = other.absolute_path;

    const std::vector<gitman::discovery_candidate> selected { other };
    const gitman::project_registration_result result { service.register_candidates(document, {}, selected) };

    REQUIRE(result.succeeded);
    REQUIRE(store.last_saved().projects.size() == 2u);
    REQUIRE(store.last_saved().projects[1].id.value == u8"repo-2");
    REQUIRE(store.last_saved().projects[1].display_name == u8"repo");
}

TEST_CASE("A single unfit candidate rejects the whole selection without saving", "[discovery][registration]")
{
    fake_project_store store {};
    fake_project_path_resolver resolver {};
    gitman::project_registration_service service { store, resolver };
    const gitman::workspace_document document { make_document() };

    SECTION("제외된 후보")
    {
        gitman::discovery_candidate excluded { make_candidate(u8"bare") };
        excluded.exclusion = gitman::discovery_exclusion::bare_repository;
        const std::vector<gitman::discovery_candidate> selected { make_candidate(u8"good"), excluded };
        const gitman::project_registration_result result { service.register_candidates(document, {}, selected) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_rejection(result));
    }

    SECTION("종류를 판정하지 못한 후보")
    {
        const std::vector<gitman::discovery_candidate> selected { make_candidate(u8"unknown", gitman::repository_kind::unknown) };
        const gitman::project_registration_result result { service.register_candidates(document, {}, selected) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_rejection(result));
    }

    SECTION("상대 경로 후보")
    {
        gitman::discovery_candidate relative { make_candidate(u8"rel") };
        relative.absolute_path = u8"relative\\rel";
        const std::vector<gitman::discovery_candidate> selected { relative };
        const gitman::project_registration_result result { service.register_candidates(document, {}, selected) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_rejection(result));
    }

    SECTION("빈 선택")
    {
        const gitman::project_registration_result result { service.register_candidates(document, {}, {}) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_rejection(result));
    }

    // 어느 경우에도 저장은 호출되지 않고 문서는 반환되지 않는다.
    REQUIRE(store.save_count() == 0u);
}

TEST_CASE("Duplicates against the document and inside the batch reject the selection", "[discovery][registration]")
{
    fake_project_store store {};
    fake_project_path_resolver resolver {};
    gitman::project_registration_service service { store, resolver };

    SECTION("문서와의 중복")
    {
        gitman::workspace_document document { make_document() };
        gitman::project_definition existing {};
        existing.id.value = u8"existing";
        existing.path.normalized = u8"C:\\scan\\repo";
        document.projects.push_back(existing);

        const std::vector<gitman::discovery_candidate> selected { make_candidate(u8"repo") };
        const gitman::project_registration_result result { service.register_candidates(document, {}, selected) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_rejection(result));
    }

    SECTION("선택 목록 안의 중복")
    {
        const gitman::workspace_document document { make_document() };
        const std::vector<gitman::discovery_candidate> selected { make_candidate(u8"repo"), make_candidate(u8"repo") };
        const gitman::project_registration_result result { service.register_candidates(document, {}, selected) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_rejection(result));
    }

    REQUIRE(store.save_count() == 0u);
}

TEST_CASE("Registered candidates round trip through the real store", "[discovery][registration][integration]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());

    // unknown field와 설정이 등록 저장을 거쳐도 보존되는 것을 함께 확인한다.
    const std::u8string document_path { directory.path_of(u8"projects.version-list") };
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary };
        stream << "{\"schema_version\":1,\"custom_field\":7,\"settings\":{\"git_executable\":\"\",\"future_key\":true},\"projects\":[]}";
    }

    const std::u8string repo { directory.make_directory(u8"scan\\repo") };
    const std::u8string svn_copy { directory.make_directory(u8"scan\\svn-copy") };

    gitman::win32::workspace_document_file_system file_system {};
    const std::unique_ptr<gitman::project_path_resolver> resolver { gitman::win32::make_project_path_resolver() };
    REQUIRE(resolver != nullptr);
    gitman::json_project_store store { file_system, *resolver };

    const gitman::project_store_load_result loaded { store.load(document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE_FALSE(loaded.has_errors());

    gitman::discovery_candidate git_candidate { make_candidate(u8"repo") };
    git_candidate.absolute_path = repo;
    git_candidate.normalized_path = resolver->resolve(repo, document_path).normalized;
    gitman::discovery_candidate svn_candidate { make_candidate(u8"svn-copy", gitman::repository_kind::subversion) };
    svn_candidate.absolute_path = svn_copy;
    svn_candidate.normalized_path = resolver->resolve(svn_copy, document_path).normalized;

    gitman::project_registration_service service { store, *resolver };
    const std::vector<gitman::discovery_candidate> selected { git_candidate, svn_candidate };
    const gitman::project_registration_result result { service.register_candidates(*loaded.document, loaded.revision, selected) };
    REQUIRE(result.succeeded);
    REQUIRE(result.revision.has_value());

    // 다시 load해 선택 항목 2개만 추가되고 unknown field가 남아 있는 것을 확인한다.
    const gitman::project_store_load_result reloaded { store.load(document_path) };
    REQUIRE(reloaded.document.has_value());
    REQUIRE(reloaded.document->projects.size() == 2u);
    REQUIRE(reloaded.document->projects[0].id.value == u8"repo");
    REQUIRE(reloaded.document->projects[0].hint == gitman::vcs_hint::git);
    REQUIRE(reloaded.document->projects[1].id.value == u8"svn-copy");
    REQUIRE(reloaded.document->projects[1].hint == gitman::vcs_hint::subversion);

    std::ifstream stream { std::filesystem::path { document_path }, std::ios::binary };
    const std::string bytes { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    REQUIRE(bytes.find("custom_field") != std::string::npos);
    REQUIRE(bytes.find("future_key") != std::string::npos);
}

TEST_CASE("A concurrent modification refuses the save and keeps the file intact", "[discovery][registration][integration]")
{
    const gitman::testing::scoped_scan_directory directory {};
    REQUIRE(directory.available());

    const std::u8string document_path { directory.path_of(u8"projects.version-list") };
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary };
        stream << "{\"schema_version\":1,\"projects\":[]}";
    }

    gitman::win32::workspace_document_file_system file_system {};
    const std::unique_ptr<gitman::project_path_resolver> resolver { gitman::win32::make_project_path_resolver() };
    REQUIRE(resolver != nullptr);
    gitman::json_project_store store { file_system, *resolver };

    const gitman::project_store_load_result loaded { store.load(document_path) };
    REQUIRE(loaded.document.has_value());

    // load와 등록 사이에 다른 프로세스가 문서를 바꾼 상황이다.
    const std::string external { "{\"schema_version\":1,\"projects\":[{\"id\":\"external\",\"path\":\"C:/external\"}]}" };
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary | std::ios::trunc };
        stream << external;
    }

    const std::u8string repo { directory.make_directory(u8"scan\\repo") };
    gitman::discovery_candidate candidate { make_candidate(u8"repo") };
    candidate.absolute_path = repo;
    candidate.normalized_path = resolver->resolve(repo, document_path).normalized;

    gitman::project_registration_service service { store, *resolver };
    const std::vector<gitman::discovery_candidate> selected { candidate };
    const gitman::project_registration_result result { service.register_candidates(*loaded.document, loaded.revision, selected) };

    REQUIRE_FALSE(result.succeeded);
    REQUIRE_FALSE(result.saved_document.has_value());
    bool concurrent { false };
    for (const gitman::diagnostic& value : result.diagnostics)
        if (value.code == gitman::diagnostic_code::concurrent_modification)
            concurrent = true;
    REQUIRE(concurrent);

    // 외부 수정본이 그대로 남아 있어야 한다. 등록이 절반쯤 쓴 파일은 없다.
    std::ifstream stream { std::filesystem::path { document_path }, std::ios::binary };
    const std::string bytes { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
    REQUIRE(bytes == external);
}

TEST_CASE("Registration diagnostic code names are stable", "[discovery][registration]")
{
    REQUIRE(gitman::diagnostic_code_name(gitman::diagnostic_code::registration_candidate_rejected) == std::u8string_view { u8"registration_candidate_rejected" });
}
