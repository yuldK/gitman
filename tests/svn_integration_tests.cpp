#include "application/process_cancellation.h"
#include "application/process_runner.h"
#include "application/vcs_file_probe.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_tool.h"
#include "infrastructure/svn_repository_provider.h"
#include "infrastructure/vcs_tool_discovery.h"
#include "platform/win32/win32_process_runner.h"
#include "platform/win32/win32_vcs_file_probe.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace {
    // 호스트를 조사해 실제 SVN 실행 파일을 찾는다. 사용자가 SVN CLI를 설치하지 않기로
    // 했으므로 이 개발 호스트에서는 항상 비어 있고 통합 test는 스스로 skip한다.
    class svn_host_fixture
    {
    public:
        svn_host_fixture()
        {
            runner_ = gitman::win32::make_process_runner();
            probe_ = gitman::win32::make_vcs_file_probe();
            if (runner_ == nullptr || probe_ == nullptr)
                return;

            tool_ = gitman::resolve_vcs_tools({}, gitman::win32::current_vcs_tool_environment(), *runner_, *probe_, {}).subversion;

            std::error_code error {};
            const std::filesystem::path base { std::filesystem::temp_directory_path(error) };
            if (static_cast<bool>(error))
                return;

            const auto token { std::chrono::steady_clock::now().time_since_epoch().count() };
            for (std::size_t attempt = 0; attempt < 100; ++attempt)
            {
                error.clear();
                const std::filesystem::path candidate { base / (L"gitman-svn-tests-" + std::to_wstring(token) + L"-" + std::to_wstring(attempt)) };
                if (std::filesystem::create_directory(candidate, error))
                {
                    root_ = candidate;
                    break;
                }
            }
        }

        svn_host_fixture(const svn_host_fixture&) = delete;
        svn_host_fixture(svn_host_fixture&&) = delete;
        svn_host_fixture& operator=(const svn_host_fixture&) = delete;
        svn_host_fixture& operator=(svn_host_fixture&&) = delete;

        ~svn_host_fixture()
        {
            if (root_.empty())
                return;

            std::error_code error {};
            std::filesystem::remove_all(root_, error);
        }

        [[nodiscard]] bool available() const noexcept
        {
            return runner_ != nullptr && probe_ != nullptr && root_.empty() == false && tool_.usable();
        }

        [[nodiscard]] const gitman::vcs_tool_info& tool() const noexcept
        {
            return tool_;
        }

        [[nodiscard]] gitman::process_runner& runner() const noexcept
        {
            return *runner_;
        }

        [[nodiscard]] const gitman::vcs_file_probe& probe() const noexcept
        {
            return *probe_;
        }

        [[nodiscard]] std::u8string root() const
        {
            return root_.u8string();
        }

    private:
        std::filesystem::path root_ {};
        std::unique_ptr<gitman::process_runner> runner_ {};
        std::unique_ptr<gitman::vcs_file_probe> probe_ {};
        gitman::vcs_tool_info tool_ {};
    };

    gitman::project_definition make_project(const std::u8string_view path)
    {
        gitman::project_definition project {};
        project.id.value = u8"svn-integration";
        project.path.original = path;
        project.path.normalized = path;
        return project;
    }
} // namespace

TEST_CASE("A host without Subversion keeps the application usable", "[integration][svn]")
{
    svn_host_fixture fixture {};

    if (fixture.tool().usable())
        SKIP("이 호스트에 SVN이 설치되어 있어 미설치 경로를 확인할 수 없습니다");

    // 사용자가 SVN CLI를 설치하지 않기로 했다. 이 호스트에서 실제로 확인할 수 있는
    // 유일한 SVN 경로이며 앱 전체가 막히지 않는다는 것을 단정한다.
    REQUIRE(fixture.tool().availability == gitman::vcs_tool_availability::not_found);
    REQUIRE(fixture.tool().executable.empty());
    for (const gitman::diagnostic& value : fixture.tool().diagnostics)
        REQUIRE(value.severity == gitman::diagnostic_severity::warning);

    gitman::svn_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };
    REQUIRE_FALSE(provider.available());

    const gitman::repository_query_result result { provider.query_local(make_project(fixture.root()), {}) };
    REQUIRE(result.snapshot.availability == gitman::repository_availability::tool_unavailable);
    REQUIRE_FALSE(result.has_errors());
}

TEST_CASE("A real Subversion executable reports a plain directory as no working copy", "[integration][svn]")
{
    svn_host_fixture fixture {};

    if (fixture.available() == false)
        SKIP("호스트에 사용할 수 있는 SVN이 없어 통합 test를 건너뜁니다");

    // 여기부터는 실제 `svn.exe`가 있는 환경에서만 실행된다. 작업 복사본을 만들려면
    // `svnadmin`이 필요하므로 단계 4는 실행 파일 연결과 판정까지만 확인한다. 실제 작업
    // 복사본 통합 검증은 단계 8의 SVN 환경 작업으로 남긴다.
    gitman::svn_repository_provider provider { fixture.tool(), fixture.runner(), fixture.probe() };
    const gitman::repository_query_result result { provider.query_local(make_project(fixture.root()), {}) };

    REQUIRE(result.snapshot.availability == gitman::repository_availability::not_a_repository);
    REQUIRE(result.snapshot.kind == gitman::repository_kind::subversion);
}
