#include "application/logic_controller.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
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

TEST_CASE("The log header keeps the elapsed time after the change finishes", "[logic][change]")
{
    // 로그 헤더의 경과 시간(MM:SS)이다. 실행 중에는 시작 시각을 기준으로 흐르고,
    // 끝난 뒤에는 걸린 시간을 그대로 세워 둔다 (2026-08-22 지시). 다음 명령이
    // 시작되면 그 값을 지우고 다시 흐른다.
    change_fixture fixture {};
    fixture.controller.handle(gitman::select_card_intent { { gitman::project_id { u8"alpha" } } });
    {
        // 아직 아무 명령도 실행하지 않았으면 표시가 없다.
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->log.has_value());
        REQUIRE_FALSE(view->log->change_started_at.has_value());
        REQUIRE_FALSE(view->log->change_elapsed.has_value());
    }

    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->log->change_started_at.has_value());
        REQUIRE_FALSE(view->log->change_elapsed.has_value());
    }

    fixture.controller.handle(fixture.make_completion(fixture.submitter.requests.front()));
    {
        // 끝나면 흐름은 멈추고 걸린 시간이 남는다.
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE_FALSE(view->log->change_started_at.has_value());
        REQUIRE(view->log->change_elapsed.has_value());
        REQUIRE(*view->log->change_elapsed >= std::chrono::steady_clock::duration::zero());
    }

    // 완료 뒤 자동 재조회가 끝나야 카드가 다음 명령을 받는다. 그동안에도 걸린
    // 시간은 그대로 남는다.
    REQUIRE(fixture.submitter.requests.size() == 2u);
    fixture.controller.handle(make_local_result(u8"alpha", fixture.submitter.requests.back().generation));
    REQUIRE(fixture.controller.make_view_snapshot()->log->change_elapsed.has_value());

    // 다음 명령이 남은 값을 지우고 다시 흐른다.
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->log->change_started_at.has_value());
    REQUIRE_FALSE(view->log->change_elapsed.has_value());
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

TEST_CASE("Update all starts every ready card and says nothing when none are skipped", "[logic][change][batch]")
{
    change_fixture fixture { { u8"alpha", u8"beta" } };
    fixture.controller.handle(gitman::update_all_intent {});

    REQUIRE(fixture.submitter.requests.size() == 2u);
    for (const gitman::operation_request& request : fixture.submitter.requests)
        REQUIRE(request.kind == gitman::operation_kind::update);

    // 전부 시작했으면 알릴 것이 없다 — 카드가 도는 것이 곧 피드백이다 (REQ-018).
    REQUIRE(fixture.controller.make_view_snapshot()->notices.empty());
}

TEST_CASE("Update all skips busy cards and reports how many", "[logic][change][batch]")
{
    change_fixture fixture { { u8"alpha", u8"beta" } };
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    fixture.submitter.requests.clear();

    fixture.controller.handle(gitman::update_all_intent {});

    // alpha는 이미 돌고 있어 건너뛴다. begin_change의 조용한 거부를 그대로 물려받지
    // 않고 몇 개를 못 넣었는지 배너로 알린다 (REQ-018).
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().project.id.value == u8"beta");

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->notices.empty() == false);
    REQUIRE(view->notices.front().find(u8"건너뛰었습니다") != std::u8string::npos);
}

TEST_CASE("Cancel all requests cancellation for every running change", "[logic][change][batch]")
{
    change_fixture fixture { { u8"alpha", u8"beta" } };
    fixture.controller.handle(gitman::update_all_intent {});
    const std::vector<gitman::operation_request> started { fixture.submitter.requests };
    REQUIRE(started.size() == 2u);

    fixture.controller.handle(gitman::cancel_all_operations_intent {});
    for (const gitman::operation_request& request : started)
        REQUIRE(request.token.cancelled());
}

TEST_CASE("The snapshot tells the toolbar whether a batch update can start", "[logic][change][batch]")
{
    change_fixture fixture {};
    const auto ready { fixture.controller.make_view_snapshot() };
    REQUIRE(ready->can_update_all);
    REQUIRE_FALSE(ready->any_change_running);

    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    const auto running { fixture.controller.make_view_snapshot() };
    REQUIRE_FALSE(running->can_update_all);
    REQUIRE(running->any_change_running);
}

TEST_CASE("A card carries when its work started and when it last finished", "[logic][change][elapsed]")
{
    // 카드만 보고 도는 중인지 멎었는지 알 수 있어야 한다 (REQ-019). 조회도 센다.
    change_fixture fixture {};
    const gitman::card_view_model* card { find_card(*fixture.controller.make_view_snapshot(), u8"alpha") };
    REQUIRE(card != nullptr);
    REQUIRE_FALSE(card->operation_started_at.has_value());
    REQUIRE(card->last_finished_at.has_value());

    fixture.controller.handle(gitman::refresh_card_intent { gitman::project_id { u8"alpha" } });
    card = find_card(*fixture.controller.make_view_snapshot(), u8"alpha");
    REQUIRE(card->busy);
    REQUIRE(card->operation_started_at.has_value());

    fixture.controller.handle(make_local_result(u8"alpha", 2u));
    card = find_card(*fixture.controller.make_view_snapshot(), u8"alpha");
    REQUIRE_FALSE(card->busy);
    REQUIRE_FALSE(card->operation_started_at.has_value());
    REQUIRE(card->last_finished_at.has_value());
}

TEST_CASE("A rejected submission does not leave the card busy", "[logic][change]")
{
    change_fixture fixture {};
    fixture.submitter.reject_submissions = true;
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });

    REQUIRE_FALSE(find_card(*fixture.controller.make_view_snapshot(), u8"alpha")->busy);
    REQUIRE(fixture.log(u8"alpha").records().back().entry.severity == gitman::diagnostic_severity::error);
}
