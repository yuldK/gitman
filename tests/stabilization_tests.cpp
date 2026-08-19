#include "application/logic_controller.h"
#include "domain/operation_log.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace {
    class recording_submitter final : public gitman::operation_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::operation_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::operation_request> requests {};
    };

    gitman::operation_log_entry make_output_entry(const std::u8string_view text)
    {
        gitman::operation_log_entry entry {};
        entry.kind = gitman::log_entry_kind::standard_output;
        entry.severity = gitman::diagnostic_severity::information;
        entry.text = text;
        return entry;
    }

    // 문서와 준비된 카드 alpha를 갖춘 controller다 (단계 8 안정화 시험용).
    struct stabilization_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        stabilization_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            gitman::project_definition alpha {};
            alpha.id.value = u8"alpha";
            alpha.display_name = u8"alpha";
            alpha.path.original = u8"C:\\work\\alpha";
            alpha.path.normalized = alpha.path.original;
            document.projects.push_back(std::move(alpha));
            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));

            gitman::query_completed_event local {};
            local.id.value = u8"alpha";
            local.generation = 1;
            local.final_event = true;
            local.result.snapshot.project.value = u8"alpha";
            local.result.snapshot.availability = gitman::repository_availability::ready;
            local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
            controller.handle(std::move(local));
            submitter.requests.clear();
        }
    };
} // namespace

TEST_CASE("Shutdown cancels the running change and discovery scan", "[stability][logic]")
{
    stabilization_fixture fixture {};

    // 실행 중인 update와 진행 중인 탐색을 만든다. 각 요청은 작업별 취소 token을
    // 싣는다 (stage-7-plan 4.4, stage-8-plan 5.2).
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    fixture.controller.handle(gitman::begin_discovery_intent { u8"C:\\work\\scan" });
    REQUIRE(fixture.submitter.requests.size() == 2u);
    const gitman::process_cancellation_token update_token { fixture.submitter.requests[0].token };
    const gitman::process_cancellation_token scan_token { fixture.submitter.requests[1].token };
    REQUIRE(update_token.cancelled() == false);
    REQUIRE(scan_token.cancelled() == false);

    // 창 닫기: 실행 중 작업이 전부 취소되어야 앱 종료가 프로세스에 매달리지
    // 않는다 (plan 3.4의 종료 정책 실측).
    fixture.controller.handle(gitman::close_intent {});
    REQUIRE(fixture.controller.shutdown_requested());
    REQUIRE(update_token.cancelled());
    REQUIRE(scan_token.cancelled());
    REQUIRE(fixture.controller.cancellation().cancelled());
}

TEST_CASE("Late events after shutdown are absorbed without new work", "[stability][logic]")
{
    stabilization_fixture fixture {};
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    const std::uint64_t operation_id { fixture.submitter.requests.back().operation_id };
    fixture.controller.handle(gitman::close_intent {});
    fixture.submitter.requests.clear();

    // 종료 후 도착한 완료 event는 자동 refresh 연쇄를 만들지 않는다.
    gitman::change_completed_event completed {};
    completed.operation_id = operation_id;
    completed.id.value = u8"alpha";
    completed.kind = gitman::operation_kind::update;
    completed.result.executed = true;
    completed.result.succeeded = false;
    fixture.controller.handle(std::move(completed));
    REQUIRE(fixture.submitter.requests.empty());

    // 종료 후의 새 작업 요청도 제출되지 않는다.
    fixture.controller.handle(gitman::refresh_all_intent {});
    fixture.controller.handle(gitman::begin_discovery_intent { u8"C:\\work\\scan" });
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("A flood of output keeps the log buffer bounded and ordered", "[stability][log]")
{
    stabilization_fixture fixture {};
    fixture.controller.handle(gitman::select_card_intent { { gitman::project_id { u8"alpha" } } });
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    const std::uint64_t operation_id { fixture.submitter.requests.back().operation_id };

    // 대량 출력: 상한(1,000)의 세 배를 배치로 쏟는다 (plan 3.9의 크기 제한).
    constexpr std::size_t flood_count { 3000 };
    constexpr std::size_t batch_size { 100 };
    for (std::size_t batch = 0; batch < flood_count / batch_size; ++batch)
    {
        gitman::operation_log_event event {};
        event.operation_id = operation_id;
        event.id.value = u8"alpha";
        for (std::size_t index = 0; index < batch_size; ++index)
            event.entries.push_back(make_output_entry(u8"output"));
        fixture.controller.handle(std::move(event));
    }

    const gitman::operation_log_buffer* const buffer { fixture.controller.card_log(gitman::project_id { u8"alpha" }) };
    REQUIRE(buffer != nullptr);
    REQUIRE(buffer->records().size() == gitman::operation_log_capacity);
    // 시작 lifecycle 1개 + 출력 3,000개 중 오래된 것부터 제거됐다.
    REQUIRE(buffer->total_appended() == flood_count + 1);
    REQUIRE(buffer->dropped_count() == flood_count + 1 - gitman::operation_log_capacity);

    // sequence는 제거 후에도 단조 증가로 남는다 (REQ-008의 순서 보장).
    std::uint64_t previous { 0 };
    for (const gitman::operation_log_record& record : buffer->records())
    {
        REQUIRE(record.sequence > previous);
        previous = record.sequence;
    }

    // 뷰는 제거 사실을 알리고 표시 목록도 상한 안이다.
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->log.has_value());
    REQUIRE(view->log->truncated);
    REQUIRE(view->log->lines.size() <= gitman::operation_log_capacity);
}
