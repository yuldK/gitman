#include "application/version_list_generation_service.h"

#include "helpers/discovery_test_doubles.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/json_project_store.h"
#include "platform/win32/project_file_system.h"
#include "platform/win32/win32_directory_enumerator.h"
#include "platform/win32/win32_vcs_file_probe.h"
#include "platform/win32/workspace_document_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 생성 단위 test가 실제 디스크를 조회하지 않도록 lexical 규칙만 흉내 낸다.
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

    // 출력 문서의 존재 여부를 통제하고 저장 호출을 기록하는 store 대역이다. 존재하지
    // 않는 문서의 load는 실제 store처럼 `document_not_found` 진단과 missing revision을
    // 돌려준다.
    class fake_project_store final : public gitman::project_store
    {
    public:
        [[nodiscard]] gitman::project_store_load_result load(const std::u8string_view document_path) noexcept override
        {
            try
            {
                gitman::project_store_load_result result {};
                if (existing_)
                {
                    gitman::workspace_document document {};
                    document.document_path = document_path;
                    result.document = { std::move(document) };
                    result.revision = make_revision_token(revision_file_state::present, std::u8string { document_path }, {}, {}, {});
                    return result;
                }

                gitman::diagnostic missing {};
                missing.code = gitman::diagnostic_code::document_not_found;
                missing.severity = gitman::diagnostic_severity::error;
                missing.message = u8"문서가 없습니다.";
                result.diagnostics.push_back(std::move(missing));
                result.revision = make_revision_token(revision_file_state::missing, std::u8string { document_path }, {}, {}, {});
                return result;
            }
            catch (...)
            {
                return {};
            }
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

        void set_existing(const bool value) noexcept
        {
            existing_ = value;
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
        bool existing_ { false };
        std::size_t save_count_ { 0 };
        std::u8string last_document_path_ {};
        gitman::workspace_document last_saved_ {};
    };

    constexpr std::u8string_view scan_root { u8"C:\\scan" };
    constexpr std::u8string_view output_path { u8"C:\\scan\\projects.version-list" };

    gitman::directory_entry make_entry(const std::u8string_view name, const bool is_directory = true, const bool is_reparse_point = false)
    {
        return { std::u8string { name }, is_directory, is_reparse_point };
    }

    // 생성 service와 그 협력자들을 한 번에 조립하는 test 본체다.
    struct generation_fixture
    {
        gitman::testing::fake_directory_enumerator enumerator {};
        gitman::testing::fake_vcs_file_probe probe {};
        fake_project_path_resolver resolver {};
        fake_project_store store {};
        gitman::discovery_service discovery { enumerator, probe, resolver };
        gitman::project_registration_service registration { store, resolver };
        gitman::version_list_generation_service service { discovery, registration, store };

        void register_git_child(const std::u8string_view name)
        {
            probe.add_directory(std::u8string { scan_root } + u8"\\" + std::u8string { name } + u8"\\.git");
        }

        void register_svn_child(const std::u8string_view name)
        {
            probe.add_directory(std::u8string { scan_root } + u8"\\" + std::u8string { name } + u8"\\.svn");
        }
    };

    bool has_diagnostic(const std::vector<gitman::diagnostic>& diagnostics, const gitman::diagnostic_code code)
    {
        for (const gitman::diagnostic& value : diagnostics)
            if (value.code == code)
                return true;
        return false;
    }
} // namespace

TEST_CASE("Generation rejects invalid roots and output paths without touching the filesystem", "[discovery][generation]")
{
    generation_fixture fixture {};

    SECTION("상대 스캔 경로")
    {
        const gitman::version_list_generation_result result { fixture.service.generate(u8"relative\\path", output_path, {}) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(result.has_errors());
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::generation_request_invalid));
    }

    SECTION("상대 출력 경로")
    {
        const gitman::version_list_generation_result result { fixture.service.generate(scan_root, u8"projects.version-list", {}) };
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::generation_request_invalid));
    }

    SECTION("확장자가 다른 출력 경로")
    {
        const gitman::version_list_generation_result result { fixture.service.generate(scan_root, u8"C:\\scan\\projects.json", {}) };
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::generation_request_invalid));
    }

    REQUIRE(fixture.store.save_count() == 0u);
    REQUIRE(fixture.enumerator.enumeration_count() == 0u);
}

TEST_CASE("Generation refuses to overwrite an existing document", "[discovery][generation]")
{
    generation_fixture fixture {};
    fixture.store.set_existing(true);

    const gitman::version_list_generation_result result { fixture.service.generate(scan_root, output_path, {}) };
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::generation_output_exists));
    REQUIRE(fixture.store.save_count() == 0u);
    // 출력 확인에서 거부되면 탐색도 시작하지 않는다.
    REQUIRE(fixture.enumerator.enumeration_count() == 0u);
}

TEST_CASE("Generation collects depth-one repositories into a new document", "[discovery][generation]")
{
    generation_fixture fixture {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.entries.push_back(make_entry(u8"beta"));
    listing.entries.push_back(make_entry(u8"alpha"));
    listing.entries.push_back(make_entry(u8"plain"));
    listing.entries.push_back(make_entry(u8"note.txt", false));
    listing.entries.push_back(make_entry(u8"junction", true, true));
    fixture.enumerator.set_listing(scan_root, std::move(listing));
    fixture.register_git_child(u8"alpha");
    fixture.register_svn_child(u8"beta");

    const gitman::version_list_generation_result result { fixture.service.generate(scan_root, output_path, {}) };
    REQUIRE(result.succeeded);
    REQUIRE_FALSE(result.has_errors());
    REQUIRE(result.document.has_value());
    REQUIRE(result.revision.has_value());
    REQUIRE(fixture.store.save_count() == 1u);
    REQUIRE(fixture.store.last_document_path() == output_path);

    // 저장소가 아닌 자식과 reparse point는 문서에 들어가지 않고, 후보 정렬 순서가
    // 곧 문서 순서다.
    const gitman::workspace_document& saved { fixture.store.last_saved() };
    REQUIRE(saved.projects.size() == 2u);
    REQUIRE(saved.projects[0].id.value == u8"alpha");
    REQUIRE(saved.projects[0].hint == gitman::vcs_hint::git);
    REQUIRE(saved.projects[0].path.original == u8"C:\\scan\\alpha");
    REQUIRE(saved.projects[1].id.value == u8"beta");
    REQUIRE(saved.projects[1].hint == gitman::vcs_hint::subversion);
    REQUIRE(result.document->projects.size() == 2u);
}

TEST_CASE("Generation refuses to create a document when no repositories are found", "[discovery][generation]")
{
    generation_fixture fixture {};
    gitman::directory_listing listing {};
    listing.succeeded = true;
    listing.entries.push_back(make_entry(u8"plain"));
    fixture.enumerator.set_listing(scan_root, std::move(listing));

    SECTION("저장소 없는 폴더")
    {
        const gitman::version_list_generation_result result { fixture.service.generate(scan_root, output_path, {}) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE_FALSE(result.has_errors());
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::generation_no_repositories));
    }

    SECTION("스캔 루트 자체가 저장소")
    {
        fixture.probe.add_directory(std::u8string { scan_root } + u8"\\.git");
        const gitman::version_list_generation_result result { fixture.service.generate(scan_root, output_path, {}) };
        REQUIRE_FALSE(result.succeeded);
        REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::generation_no_repositories));
    }

    REQUIRE(fixture.store.save_count() == 0u);
}

TEST_CASE("Generation propagates a cancelled discovery without saving", "[discovery][generation]")
{
    generation_fixture fixture {};
    gitman::process_cancellation_source cancellation {};
    cancellation.request_cancellation();

    const gitman::version_list_generation_result result { fixture.service.generate(scan_root, output_path, cancellation.token()) };
    REQUIRE_FALSE(result.succeeded);
    REQUIRE(has_diagnostic(result.diagnostics, gitman::diagnostic_code::discovery_cancelled));
    REQUIRE(fixture.store.save_count() == 0u);
}

TEST_CASE("Generation writes a loadable document from a real directory tree", "[discovery][generation][integration]")
{
    const gitman::testing::scoped_scan_directory scan {};
    REQUIRE(scan.available());
    static_cast<void>(scan.make_directory(u8"alpha\\.git"));
    static_cast<void>(scan.make_directory(u8"beta\\.svn"));
    static_cast<void>(scan.make_directory(u8"plain"));

    gitman::win32::workspace_document_file_system file_system {};
    const std::unique_ptr<gitman::project_path_resolver> resolver { gitman::win32::make_project_path_resolver() };
    const std::unique_ptr<gitman::vcs_file_probe> probe { gitman::win32::make_vcs_file_probe() };
    const std::unique_ptr<gitman::directory_enumerator> enumerator { gitman::win32::make_directory_enumerator() };
    gitman::json_project_store store { file_system, *resolver };
    gitman::discovery_service discovery { *enumerator, *probe, *resolver };
    gitman::project_registration_service registration { store, *resolver };
    gitman::version_list_generation_service service { discovery, registration, store };

    const std::u8string document_path { scan.path_of(u8"projects.version-list") };
    const gitman::version_list_generation_result result { service.generate(scan.root(), document_path, {}) };
    REQUIRE(result.succeeded);
    REQUIRE(result.document.has_value());
    REQUIRE(result.document->projects.size() == 2u);

    // 저장된 파일을 실제 store가 다시 읽을 수 있어야 한다.
    const gitman::project_store_load_result loaded { store.load(document_path) };
    REQUIRE(loaded.document.has_value());
    REQUIRE(loaded.document->projects.size() == 2u);
    REQUIRE(loaded.document->projects[0].id.value == u8"alpha");
    REQUIRE(loaded.document->projects[1].id.value == u8"beta");

    // 같은 경로로 다시 생성하면 기존 문서를 보호한다.
    const gitman::version_list_generation_result second { service.generate(scan.root(), document_path, {}) };
    REQUIRE_FALSE(second.succeeded);
    REQUIRE(has_diagnostic(second.diagnostics, gitman::diagnostic_code::generation_output_exists));
}
