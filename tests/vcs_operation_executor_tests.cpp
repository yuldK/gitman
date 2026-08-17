#include "infrastructure/vcs_operation_executor.h"

#include "helpers/vcs_test_doubles.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {
    // 스크립트된 문서를 돌려주는 store 대역이다.
    class scripted_project_store final : public gitman::project_store
    {
    public:
        [[nodiscard]] gitman::project_store_load_result load(const std::u8string_view document_path) noexcept override
        {
            try
            {
                ++load_count_;
                last_path_ = document_path;
                gitman::project_store_load_result result {};
                gitman::workspace_document document {};
                document.document_path = document_path;
                gitman::project_definition project {};
                project.id.value = u8"loaded";
                document.projects.push_back(std::move(project));
                result.document = { std::move(document) };
                result.revision = make_revision_token(revision_file_state::present, std::u8string { document_path }, {}, {}, {});
                gitman::diagnostic warning {};
                warning.severity = gitman::diagnostic_severity::warning;
                warning.message = u8"경고 하나";
                result.diagnostics.push_back(std::move(warning));
                return result;
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] gitman::project_store_load_result load_backup(std::u8string_view) noexcept override
        {
            return {};
        }

        [[nodiscard]] gitman::project_store_save_result save(
            const std::u8string_view document_path, const gitman::workspace_document& document, const gitman::workspace_revision_token&) noexcept override
        {
            try
            {
                ++save_count_;
                last_path_ = document_path;
                saved_order_.clear();
                for (const gitman::project_definition& project : document.projects)
                    saved_order_.push_back(project.id.value);

                gitman::project_store_save_result result {};
                if (save_fails_)
                {
                    gitman::diagnostic error {};
                    error.severity = gitman::diagnostic_severity::error;
                    error.message = u8"저장 실패";
                    result.diagnostics.push_back(std::move(error));
                    return result;
                }
                result.revision = { make_revision_token(revision_file_state::present, std::u8string { document_path }, {}, {}, {}) };
                return result;
            }
            catch (...)
            {
                return {};
            }
        }

        void fail_saves() noexcept
        {
            save_fails_ = true;
        }

        [[nodiscard]] std::size_t load_count() const noexcept
        {
            return load_count_;
        }

        [[nodiscard]] std::size_t save_count() const noexcept
        {
            return save_count_;
        }

        [[nodiscard]] const std::vector<std::u8string>& saved_order() const noexcept
        {
            return saved_order_;
        }

        [[nodiscard]] const std::u8string& last_path() const noexcept
        {
            return last_path_;
        }

    private:
        std::size_t load_count_ { 0 };
        std::size_t save_count_ { 0 };
        bool save_fails_ { false };
        std::vector<std::u8string> saved_order_ {};
        std::u8string last_path_ {};
    };

    struct executor_fixture
    {
        scripted_project_store store {};
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        // 빈 환경이라 도구 탐색이 프로세스를 만들지 않고 not_found로 끝난다.
        gitman::vcs_operation_executor executor { store, runner, probe, {} };
        std::vector<gitman::logic_message> emitted {};

        void run(const gitman::operation_request& request)
        {
            executor.execute(request, [this](gitman::logic_message message) { emitted.push_back(std::move(message)); });
        }
    };

    gitman::operation_request make_query(const std::u8string_view path, const gitman::vcs_hint hint, const gitman::operation_kind kind = gitman::operation_kind::query_local)
    {
        gitman::operation_request request {};
        request.operation_id = 11;
        request.generation = 3;
        request.kind = kind;
        request.project.id.value = u8"card";
        request.project.path.original = path;
        request.project.path.normalized = path;
        request.project.hint = hint;
        return request;
    }
} // namespace

TEST_CASE("The executor loads documents through the store", "[executor][app]")
{
    executor_fixture fixture {};
    gitman::operation_request request {};
    request.operation_id = 5;
    request.kind = gitman::operation_kind::load_document;
    request.document_path = u8"C:\\work\\p.version-list";
    fixture.run(request);

    REQUIRE(fixture.store.load_count() == 1u);
    REQUIRE(fixture.store.last_path() == u8"C:\\work\\p.version-list");
    REQUIRE(fixture.emitted.size() == 1u);
    const auto* const event { std::get_if<gitman::document_loaded_event>(&fixture.emitted.front()) };
    REQUIRE(event != nullptr);
    REQUIRE(event->operation_id == 5u);
    REQUIRE(event->document.has_value());
    REQUIRE(event->document->projects.size() == 1u);
    REQUIRE(event->diagnostics.size() == 1u);
}

TEST_CASE("The executor saves documents through the store and reports the new revision", "[executor][app]")
{
    executor_fixture fixture {};
    gitman::operation_request request {};
    request.operation_id = 7;
    request.kind = gitman::operation_kind::save_document;
    request.document_path = u8"C:\\work\\p.version-list";
    gitman::workspace_document document {};
    document.document_path = request.document_path;
    gitman::project_definition first {};
    first.id.value = u8"beta";
    gitman::project_definition second {};
    second.id.value = u8"alpha";
    document.projects.push_back(std::move(first));
    document.projects.push_back(std::move(second));
    request.document = { std::move(document) };

    fixture.run(request);

    REQUIRE(fixture.store.save_count() == 1u);
    REQUIRE(fixture.store.last_path() == u8"C:\\work\\p.version-list");
    REQUIRE(fixture.store.saved_order() == std::vector<std::u8string> { u8"beta", u8"alpha" });
    REQUIRE(fixture.emitted.size() == 1u);
    const auto* const event { std::get_if<gitman::document_saved_event>(&fixture.emitted.front()) };
    REQUIRE(event != nullptr);
    REQUIRE(event->operation_id == 7u);
    REQUIRE(event->revision.has_value());
}

TEST_CASE("A failed or empty save still emits a final saved event", "[executor][app]")
{
    SECTION("store 실패는 진단으로 전달된다")
    {
        executor_fixture fixture {};
        fixture.store.fail_saves();
        gitman::operation_request request {};
        request.operation_id = 8;
        request.kind = gitman::operation_kind::save_document;
        request.document = { gitman::workspace_document {} };
        fixture.run(request);

        const auto* const event { std::get_if<gitman::document_saved_event>(&fixture.emitted.front()) };
        REQUIRE(event != nullptr);
        REQUIRE(event->revision.has_value() == false);
        REQUIRE(event->diagnostics.size() == 1u);
        REQUIRE(event->diagnostics.front().message == u8"저장 실패");
    }

    SECTION("문서가 없는 요청은 store를 부르지 않고 오류를 보고한다")
    {
        executor_fixture fixture {};
        gitman::operation_request request {};
        request.operation_id = 9;
        request.kind = gitman::operation_kind::save_document;
        fixture.run(request);

        REQUIRE(fixture.store.save_count() == 0u);
        const auto* const event { std::get_if<gitman::document_saved_event>(&fixture.emitted.front()) };
        REQUIRE(event != nullptr);
        REQUIRE(event->revision.has_value() == false);
        REQUIRE(event->diagnostics.empty() == false);
    }
}

TEST_CASE("A git hinted card without tools reports tool unavailability without processes", "[executor][app]")
{
    executor_fixture fixture {};
    fixture.probe.add_directory(u8"C:\\work\\repo");
    fixture.run(make_query(u8"C:\\work\\repo", gitman::vcs_hint::git));

    REQUIRE(fixture.emitted.size() == 1u);
    const auto* const event { std::get_if<gitman::query_completed_event>(&fixture.emitted.front()) };
    REQUIRE(event != nullptr);
    REQUIRE(event->operation_id == 11u);
    REQUIRE(event->generation == 3u);
    REQUIRE(event->final_event);
    REQUIRE_FALSE(event->remote);
    REQUIRE(event->result.snapshot.kind == gitman::repository_kind::git);
    REQUIRE(event->result.snapshot.availability == gitman::repository_availability::tool_unavailable);
    // 도구가 없으니 어떤 프로세스도 만들지 않는다.
    REQUIRE(fixture.runner.request_count() == 0u);
}

TEST_CASE("Automatic cards pick their provider from the stage five markers", "[executor][app]")
{
    SECTION(".svn 표식은 SVN provider를 고른다")
    {
        executor_fixture fixture {};
        fixture.probe.add_directory(u8"C:\\work\\copy");
        fixture.probe.add_directory(u8"C:\\work\\copy\\.svn");
        fixture.run(make_query(u8"C:\\work\\copy", gitman::vcs_hint::automatic));

        const auto* const event { std::get_if<gitman::query_completed_event>(&fixture.emitted.front()) };
        REQUIRE(event != nullptr);
        REQUIRE(event->result.snapshot.kind == gitman::repository_kind::subversion);
    }

    SECTION(".git 표식은 Git provider를 고른다")
    {
        executor_fixture fixture {};
        fixture.probe.add_directory(u8"C:\\work\\repo");
        fixture.probe.add_directory(u8"C:\\work\\repo\\.git");
        fixture.run(make_query(u8"C:\\work\\repo", gitman::vcs_hint::automatic));

        const auto* const event { std::get_if<gitman::query_completed_event>(&fixture.emitted.front()) };
        REQUIRE(event != nullptr);
        REQUIRE(event->result.snapshot.kind == gitman::repository_kind::git);
    }
}

TEST_CASE("A refresh emits the local result first and the remote result last", "[executor][app]")
{
    executor_fixture fixture {};
    fixture.probe.add_directory(u8"C:\\work\\repo");
    fixture.run(make_query(u8"C:\\work\\repo", gitman::vcs_hint::git, gitman::operation_kind::refresh));

    REQUIRE(fixture.emitted.size() == 2u);
    const auto* const local { std::get_if<gitman::query_completed_event>(&fixture.emitted[0]) };
    REQUIRE(local != nullptr);
    REQUIRE_FALSE(local->remote);
    REQUIRE_FALSE(local->final_event);

    const auto* const remote { std::get_if<gitman::query_completed_event>(&fixture.emitted[1]) };
    REQUIRE(remote != nullptr);
    REQUIRE(remote->remote);
    REQUIRE(remote->final_event);
    REQUIRE(remote->generation == 3u);
}

TEST_CASE("Tool discovery results are cached per settings", "[executor][app]")
{
    executor_fixture fixture {};
    // 존재하지 않는 지정 경로는 probe 확인으로 끝나 프로세스 없이 path_invalid가
    // 된다. cache가 동작하면 두 번째 조회는 같은 결과를 재사용한다.
    gitman::operation_request request { make_query(u8"C:\\work\\repo", gitman::vcs_hint::git) };
    request.settings.git_executable = u8"C:\\tools\\missing-git.exe";
    fixture.run(request);
    fixture.run(request);

    REQUIRE(fixture.emitted.size() == 2u);
    const auto* const first { std::get_if<gitman::query_completed_event>(&fixture.emitted[0]) };
    const auto* const second { std::get_if<gitman::query_completed_event>(&fixture.emitted[1]) };
    REQUIRE(first->result.snapshot.availability == gitman::repository_availability::tool_unavailable);
    REQUIRE(second->result.snapshot.availability == gitman::repository_availability::tool_unavailable);
    REQUIRE(fixture.runner.request_count() == 0u);
}
