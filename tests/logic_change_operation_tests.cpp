#include "application/logic_controller.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
    class recording_submitter final : public gitman::operation_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::operation_request request) override
        {
            if (reject_submissions)
                return false;
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::operation_request> requests {};
        bool reject_submissions { false };
    };

    gitman::project_definition make_project(const std::u8string_view id)
    {
        gitman::project_definition project {};
        project.id.value = id;
        project.display_name = id;
        project.path.original = std::u8string { u8"C:\\work\\" } + std::u8string { id };
        project.path.normalized = project.path.original;
        return project;
    }

    gitman::document_loaded_event make_loaded_document(const std::vector<gitman::project_definition>& projects)
    {
        gitman::document_loaded_event event {};
        gitman::workspace_document document {};
        document.document_path = u8"C:\\work\\projects.version-list";
        document.projects = projects;
        event.document = { std::move(document) };
        return event;
    }

    gitman::query_completed_event make_local_result(const std::u8string_view id, const std::uint64_t generation)
    {
        gitman::query_completed_event event {};
        event.id.value = id;
        event.generation = generation;
        event.final_event = true;
        event.result.snapshot.project.value = id;
        event.result.snapshot.kind = gitman::repository_kind::git;
        event.result.snapshot.availability = gitman::repository_availability::ready;
        event.result.snapshot.current_reference = u8"main";
        event.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
        return event;
    }

    // 문서를 열고 카드 하나의 초기 조회까지 끝낸 controller를 만든다.
    struct change_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        explicit change_fixture(const std::vector<std::u8string_view> ids = { u8"alpha" })
        {
            controller.handle(gitman::open_document_intent { u8"C:\\work\\projects.version-list" });
            std::vector<gitman::project_definition> projects {};
            for (const std::u8string_view id : ids)
                projects.push_back(make_project(id));
            controller.handle(make_loaded_document(projects));
            for (const std::u8string_view id : ids)
                controller.handle(make_local_result(id, 1u));
            submitter.requests.clear();
        }

        [[nodiscard]] gitman::change_completed_event make_completion(const gitman::operation_request& request) const
        {
            gitman::change_completed_event event {};
            event.operation_id = request.operation_id;
            event.generation = request.generation;
            event.id = request.project.id;
            event.kind = request.kind;
            event.result.executed = true;
            event.result.succeeded = true;
            event.result.snapshot.project = request.project.id;
            event.result.snapshot.kind = gitman::repository_kind::git;
            event.result.snapshot.availability = gitman::repository_availability::ready;
            event.result.snapshot.current_reference = u8"updated";
            return event;
        }

        [[nodiscard]] const gitman::operation_log_buffer& log(const std::u8string_view id) const
        {
            const gitman::operation_log_buffer* const buffer { controller.card_log(gitman::project_id { std::u8string { id } }) };
            REQUIRE(buffer != nullptr);
            return *buffer;
        }
    };

    const gitman::card_view_model* find_card(const gitman::view_snapshot& view, const std::u8string_view id)
    {
        for (const gitman::card_view_model& card : view.cards)
            if (card.id.value == id)
                return &card;
        return nullptr;
    }
} // namespace

TEST_CASE("An update request submits a change operation with its own cancellation token", "[logic][change]")
{
    change_fixture fixture {};
    gitman::update_options options {};
    // intent의 값은 무시되고 submodule 여부는 문서 settings가 정한다 (2026-08-20
    // 검수: 확인 overlay 제거). 이 문서의 설정은 기본값(off)이다.
    options.update_submodules = true;
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, options });

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& request { fixture.submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::update);
    REQUIRE(request.options.update_submodules == false);
    REQUIRE(request.project.id.value == u8"alpha");
    REQUIRE(request.token.cancellable());
    REQUIRE_FALSE(request.token.cancelled());

    // 카드는 실행 중으로 표시되고 시작 로그가 남는다.
    REQUIRE(find_card(*fixture.controller.make_view_snapshot(), u8"alpha")->busy);
    REQUIRE(fixture.log(u8"alpha").records().size() == 1u);
    REQUIRE(fixture.log(u8"alpha").records().front().entry.kind == gitman::log_entry_kind::lifecycle);
}

TEST_CASE("A second change request on a busy card is refused and logged", "[logic][change]")
{
    change_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(fixture.submitter.requests.size() == 1u);

    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    // 시작 1 + 거부 1이다.
    REQUIRE(fixture.log(u8"alpha").records().size() == 2u);
    REQUIRE(fixture.log(u8"alpha").records().back().entry.severity == gitman::diagnostic_severity::warning);
}

TEST_CASE("A completed change applies the requeried snapshot and chains an automatic refresh", "[logic][change]")
{
    change_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    const gitman::operation_request request { fixture.submitter.requests.front() };
    fixture.submitter.requests.clear();

    fixture.controller.handle(fixture.make_completion(request));

    const auto view { fixture.controller.make_view_snapshot() };
    // 자동 refresh가 곧바로 시작되므로 카드는 다시 busy다 (plan 5.2의 8).
    REQUIRE(find_card(*view, u8"alpha")->busy);
    REQUIRE(find_card(*view, u8"alpha")->reference == u8"updated");
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::refresh);
    REQUIRE(fixture.submitter.requests.front().generation == 2u);

    // 완료 로그가 남는다: 시작 + 완료.
    REQUIRE(fixture.log(u8"alpha").records().size() == 2u);
    REQUIRE(fixture.log(u8"alpha").records().back().entry.severity == gitman::diagnostic_severity::information);
}

TEST_CASE("A blocked change logs the reason and does not wipe the card state", "[logic][change]")
{
    change_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    const gitman::operation_request request { fixture.submitter.requests.front() };
    fixture.submitter.requests.clear();

    gitman::change_completed_event blocked {};
    blocked.operation_id = request.operation_id;
    blocked.id = request.project.id;
    blocked.kind = request.kind;
    blocked.result.executed = false;
    blocked.result.blocked_by = gitman::update_block_reason::working_tree_dirty;
    // 도구 부재처럼 조회 없이 차단된 결과는 빈 snapshot을 남긴다.
    blocked.result.snapshot.availability = gitman::repository_availability::unknown;
    fixture.controller.handle(std::move(blocked));

    const auto view { fixture.controller.make_view_snapshot() };
    // 이전 조회 결과가 지워지지 않는다.
    REQUIRE(find_card(*view, u8"alpha")->reference == u8"main");
    // 차단 사유가 경고 로그로 남는다.
    bool found_warning { false };
    for (const gitman::operation_log_record& record : fixture.log(u8"alpha").records())
        if (record.entry.severity == gitman::diagnostic_severity::warning)
            found_warning = true;
    REQUIRE(found_warning);
}

TEST_CASE("Late change results and logs after completion are discarded", "[logic][change]")
{
    change_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    const gitman::operation_request request { fixture.submitter.requests.front() };
    fixture.submitter.requests.clear();

    // 다른 작업 id의 완료는 무시된다.
    gitman::change_completed_event stale { fixture.make_completion(request) };
    stale.operation_id = request.operation_id + 99;
    fixture.controller.handle(std::move(stale));
    REQUIRE(find_card(*fixture.controller.make_view_snapshot(), u8"alpha")->busy);
    REQUIRE(fixture.submitter.requests.empty());

    // 정상 완료 후 도착한 이전 작업의 로그도 무시된다.
    fixture.controller.handle(fixture.make_completion(request));
    gitman::operation_log_event late {};
    late.operation_id = request.operation_id;
    late.id = request.project.id;
    late.entries.push_back({});
    const std::size_t before { fixture.log(u8"alpha").records().size() };
    fixture.controller.handle(std::move(late));
    REQUIRE(fixture.log(u8"alpha").records().size() == before);
}

TEST_CASE("Log events append only to the card that owns the operation", "[logic][change]")
{
    change_fixture fixture { { u8"alpha", u8"beta" } };
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"beta" }, {} });
    REQUIRE(fixture.submitter.requests.size() == 2u);
    const gitman::operation_request alpha_request { fixture.submitter.requests[0] };
    const gitman::operation_request beta_request { fixture.submitter.requests[1] };

    // 병렬 작업의 출력이 서로 섞여 도착해도 각 카드 buffer에는 자기 로그만 남는다
    // (REQ-008).
    gitman::operation_log_event alpha_log {};
    alpha_log.operation_id = alpha_request.operation_id;
    alpha_log.id = alpha_request.project.id;
    gitman::operation_log_entry alpha_entry {};
    alpha_entry.kind = gitman::log_entry_kind::standard_output;
    alpha_entry.text = u8"alpha-output";
    alpha_log.entries.push_back(std::move(alpha_entry));

    gitman::operation_log_event beta_log {};
    beta_log.operation_id = beta_request.operation_id;
    beta_log.id = beta_request.project.id;
    gitman::operation_log_entry beta_entry {};
    beta_entry.kind = gitman::log_entry_kind::standard_error;
    beta_entry.text = u8"beta-error";
    beta_log.entries.push_back(std::move(beta_entry));

    fixture.controller.handle(std::move(alpha_log));
    fixture.controller.handle(std::move(beta_log));

    const gitman::operation_log_buffer& alpha { fixture.log(u8"alpha") };
    const gitman::operation_log_buffer& beta { fixture.log(u8"beta") };
    for (const gitman::operation_log_record& record : alpha.records())
        REQUIRE(record.entry.text != u8"beta-error");
    for (const gitman::operation_log_record& record : beta.records())
        REQUIRE(record.entry.text != u8"alpha-output");
    REQUIRE(alpha.records().back().entry.text == u8"alpha-output");
    REQUIRE(beta.records().back().entry.text == u8"beta-error");
}

TEST_CASE("Cancelling a running change signals only that operation's token", "[logic][change]")
{
    change_fixture fixture { { u8"alpha", u8"beta" } };
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"beta" }, {} });

    fixture.controller.handle(gitman::cancel_operation_intent { gitman::project_id { u8"alpha" } });

    REQUIRE(fixture.submitter.requests[0].token.cancelled());
    REQUIRE_FALSE(fixture.submitter.requests[1].token.cancelled());
    // 전역(종료) 취소와는 무관하다.
    REQUIRE_FALSE(fixture.controller.cancellation().cancelled());
    REQUIRE(fixture.log(u8"alpha").records().back().entry.severity == gitman::diagnostic_severity::warning);
}

TEST_CASE("A switch request carries the validated candidate", "[logic][change]")
{
    change_fixture fixture {};
    gitman::switch_candidate target {};
    target.kind = gitman::switch_candidate_kind::git_remote_branch;
    target.display_name = u8"origin/feature";
    target.target = u8"refs/remotes/origin/feature";
    target.remote_name = u8"origin";
    fixture.controller.handle(gitman::request_switch_intent { gitman::project_id { u8"alpha" }, target });

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& request { fixture.submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::switch_to);
    REQUIRE(request.switch_target.has_value());
    REQUIRE(request.switch_target->target == u8"refs/remotes/origin/feature");
}

TEST_CASE("Clearing a card log removes its records", "[logic][change]")
{
    change_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(fixture.log(u8"alpha").records().empty() == false);

    fixture.controller.handle(gitman::clear_log_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(fixture.log(u8"alpha").records().empty());
}

TEST_CASE("Shutdown cancels running change operations", "[logic][change]")
{
    change_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    fixture.controller.handle(gitman::close_intent {});

    REQUIRE(fixture.submitter.requests.front().token.cancelled());
}

TEST_CASE("Opening another document cancels running changes of the previous one", "[logic][change]")
{
    change_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    const gitman::operation_request request { fixture.submitter.requests.front() };

    fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
    REQUIRE(request.token.cancelled());
}

TEST_CASE("A rejected submission does not leave the card busy", "[logic][change]")
{
    change_fixture fixture {};
    fixture.submitter.reject_submissions = true;
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });

    REQUIRE_FALSE(find_card(*fixture.controller.make_view_snapshot(), u8"alpha")->busy);
    REQUIRE(fixture.log(u8"alpha").records().back().entry.severity == gitman::diagnostic_severity::error);
}
