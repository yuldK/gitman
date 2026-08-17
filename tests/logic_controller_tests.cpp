#include "application/logic_controller.h"
#include "presentation/list_metrics.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // 제출된 작업을 기록만 하는 대역이다. logic이 언제 무엇을 위임하는지 그대로
    // 관찰한다.
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

    gitman::project_definition make_project(const std::u8string_view id, const bool enabled = true)
    {
        gitman::project_definition project {};
        project.id.value = id;
        project.display_name = id;
        project.path.original = std::u8string { u8"C:\\work\\" } + std::u8string { id };
        project.path.normalized = project.path.original;
        project.enabled = enabled;
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

    gitman::query_completed_event make_local_result(const std::u8string_view id, const std::uint64_t generation, const bool final_event = true)
    {
        gitman::query_completed_event event {};
        event.id.value = id;
        event.generation = generation;
        event.remote = false;
        event.final_event = final_event;
        event.result.snapshot.project.value = id;
        event.result.snapshot.kind = gitman::repository_kind::git;
        event.result.snapshot.availability = gitman::repository_availability::ready;
        event.result.snapshot.current_reference = u8"main";
        event.result.snapshot.sync_state = gitman::remote_sync_state::up_to_date;
        event.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
        return event;
    }

    const gitman::card_view_model* find_card(const gitman::view_snapshot& view, const std::u8string_view id)
    {
        for (const gitman::card_view_model& card : view.cards)
            if (card.id.value == id)
                return &card;
        return nullptr;
    }
} // namespace

TEST_CASE("Opening a document delegates the load and reports the loading state", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };

    controller.handle(gitman::open_document_intent { u8"C:\\work\\projects.version-list" });
    REQUIRE(submitter.requests.size() == 1u);
    REQUIRE(submitter.requests.front().kind == gitman::operation_kind::load_document);
    REQUIRE(submitter.requests.front().document_path == u8"C:\\work\\projects.version-list");

    const auto view { controller.make_view_snapshot() };
    REQUIRE(view->empty_state == gitman::view_empty_state::document_loading);
}

TEST_CASE("A loaded document creates cards and queries local state for enabled projects only", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\projects.version-list" });
    submitter.requests.clear();

    controller.handle(make_loaded_document({ make_project(u8"alpha"), make_project(u8"beta", false), make_project(u8"gamma") }));

    // 초기 조회는 로컬만, 활성 카드만이다 (plan 5.1).
    REQUIRE(submitter.requests.size() == 2u);
    for (const gitman::operation_request& request : submitter.requests)
    {
        REQUIRE(request.kind == gitman::operation_kind::query_local);
        REQUIRE(request.generation == 1u);
        REQUIRE(request.project.id.value != u8"beta");
    }

    const auto view { controller.make_view_snapshot() };
    REQUIRE(view->cards.size() == 3u);
    REQUIRE(find_card(*view, u8"alpha")->state == gitman::card_view_state::running);
    REQUIRE(find_card(*view, u8"beta")->state == gitman::card_view_state::disabled);
}

TEST_CASE("A completed local query fills the card and outdated generations are discarded", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha") }));

    controller.handle(make_local_result(u8"alpha", 1u));
    {
        const auto view { controller.make_view_snapshot() };
        const gitman::card_view_model* const card { find_card(*view, u8"alpha") };
        REQUIRE(card->state == gitman::card_view_state::ready);
        REQUIRE(card->reference == u8"main");
        REQUIRE_FALSE(card->busy);
        REQUIRE(std::u8string_view { card->status.codicon } == u8"pass");
    }

    // 오래된 세대의 늦은 결과는 폐기된다 (ADR-004 검증 항목).
    gitman::query_completed_event stale { make_local_result(u8"alpha", 1u) };
    stale.result.snapshot.current_reference = u8"stale-branch";
    controller.handle(gitman::refresh_card_intent { gitman::project_id { u8"alpha" } });
    controller.handle(std::move(stale));
    {
        const auto view { controller.make_view_snapshot() };
        REQUIRE(find_card(*view, u8"alpha")->reference == u8"main");
        REQUIRE(find_card(*view, u8"alpha")->busy);
    }

    // 삭제된(존재하지 않는) 카드의 결과도 폐기된다.
    controller.handle(make_local_result(u8"ghost", 1u));
    REQUIRE(controller.make_view_snapshot()->cards.size() == 1u);
}

TEST_CASE("Refresh bumps the generation and duplicate requests merge into one follow up", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha") }));
    controller.handle(make_local_result(u8"alpha", 1u));
    submitter.requests.clear();

    controller.handle(gitman::refresh_card_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(submitter.requests.size() == 1u);
    REQUIRE(submitter.requests.front().kind == gitman::operation_kind::refresh);
    REQUIRE(submitter.requests.front().generation == 2u);

    // 실행 중의 중복 refresh는 즉시 제출되지 않고 하나로 병합된다.
    controller.handle(gitman::refresh_card_intent { gitman::project_id { u8"alpha" } });
    controller.handle(gitman::refresh_card_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(submitter.requests.size() == 1u);

    // 마지막 event가 도착하면 병합된 refresh 하나가 새 세대로 제출된다.
    controller.handle(make_local_result(u8"alpha", 2u));
    REQUIRE(submitter.requests.size() == 2u);
    REQUIRE(submitter.requests.back().generation == 3u);

    // 후속 refresh까지 끝나면 더 제출하지 않는다.
    controller.handle(make_local_result(u8"alpha", 3u));
    REQUIRE(submitter.requests.size() == 2u);
}

TEST_CASE("Refresh all touches every enabled card", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha"), make_project(u8"beta", false), make_project(u8"gamma") }));
    controller.handle(make_local_result(u8"alpha", 1u));
    controller.handle(make_local_result(u8"gamma", 1u));
    submitter.requests.clear();

    controller.handle(gitman::refresh_all_intent {});
    REQUIRE(submitter.requests.size() == 2u);
    for (const gitman::operation_request& request : submitter.requests)
        REQUIRE(request.kind == gitman::operation_kind::refresh);
}

TEST_CASE("Filter and sort shape the visible cards deterministically", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"zulu"), make_project(u8"Alpha"), make_project(u8"beta") }));
    for (const std::u8string_view id : { std::u8string_view { u8"zulu" }, std::u8string_view { u8"Alpha" }, std::u8string_view { u8"beta" } })
        controller.handle(make_local_result(id, 1u));

    {
        const auto view { controller.make_view_snapshot() };
        REQUIRE(view->cards.size() == 3u);
        REQUIRE(view->cards[0].display_name == u8"Alpha");
        REQUIRE(view->cards[1].display_name == u8"beta");
        REQUIRE(view->cards[2].display_name == u8"zulu");
    }

    // 상태 정렬은 문제가 있는 카드를 위로 올린다.
    gitman::query_completed_event failed { make_local_result(u8"zulu", 1u) };
    failed.result.snapshot.availability = gitman::repository_availability::not_a_repository;
    controller.handle(gitman::refresh_card_intent { gitman::project_id { u8"zulu" } });
    failed.generation = 2u;
    controller.handle(std::move(failed));
    controller.handle(gitman::set_sort_intent { gitman::card_sort_key::status });
    {
        const auto view { controller.make_view_snapshot() };
        REQUIRE(view->cards.front().display_name == u8"zulu");
        REQUIRE(view->cards.front().state == gitman::card_view_state::failed);
        REQUIRE(std::u8string_view { view->cards.front().status.codicon } == u8"error");
    }

    // 대소문자 무시 부분 문자열 필터다. 일치가 없으면 빈 상태로 알린다.
    controller.handle(gitman::set_filter_intent { u8"ALP" });
    REQUIRE(controller.make_view_snapshot()->cards.size() == 1u);
    controller.handle(gitman::set_filter_intent { u8"없는-이름" });
    REQUIRE(controller.make_view_snapshot()->empty_state == gitman::view_empty_state::no_filter_match);
}

TEST_CASE("Selection follows existing cards and clears for unknown ids", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha") }));

    controller.handle(gitman::select_card_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(controller.make_view_snapshot()->selected.has_value());
    REQUIRE(find_card(*controller.make_view_snapshot(), u8"alpha")->selected);

    controller.handle(gitman::select_card_intent { gitman::project_id { u8"ghost" } });
    REQUIRE(controller.make_view_snapshot()->selected.has_value() == false);
}

TEST_CASE("Shutdown cancels outstanding work and refuses new refreshes", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha") }));
    submitter.requests.clear();

    REQUIRE_FALSE(controller.cancellation().cancelled());
    controller.handle(gitman::close_intent {});
    REQUIRE(controller.shutdown_requested());
    REQUIRE(controller.cancellation().cancelled());
    REQUIRE(controller.make_view_snapshot()->shutting_down);

    controller.handle(gitman::refresh_all_intent {});
    REQUIRE(submitter.requests.empty());
}

TEST_CASE("Requests carry the document settings and the cancellation token", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });

    gitman::document_loaded_event event { make_loaded_document({ make_project(u8"alpha") }) };
    event.document->settings.git_executable = u8"C:\\tools\\git.exe";
    controller.handle(std::move(event));

    REQUIRE(submitter.requests.size() == 2u);
    const gitman::operation_request& query { submitter.requests.back() };
    REQUIRE(query.settings.git_executable == u8"C:\\tools\\git.exe");
    REQUIRE(query.token.cancellable());
}

TEST_CASE("Document diagnostics surface as notices", "[logic][app]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\missing.version-list" });

    gitman::document_loaded_event failure {};
    gitman::diagnostic error {};
    error.severity = gitman::diagnostic_severity::error;
    error.message = u8"문서를 찾을 수 없습니다.";
    failure.diagnostics.push_back(error);
    controller.handle(std::move(failure));

    const auto view { controller.make_view_snapshot() };
    REQUIRE(view->empty_state == gitman::view_empty_state::no_document);
    REQUIRE(view->notices.size() == 1u);
    REQUIRE(view->notices.front() == u8"문서를 찾을 수 없습니다.");
}

TEST_CASE("Reordering a card rewrites document order, switches to custom sort, and saves", "[logic][app][reorder]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha"), make_project(u8"beta"), make_project(u8"gamma") }));
    submitter.requests.clear();

    // alpha를 gamma 뒤로 옮긴다.
    controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"gamma" }, true });

    const auto view { controller.make_view_snapshot() };
    REQUIRE(view->sort == gitman::card_sort_key::custom);
    REQUIRE(view->cards.size() == 3u);
    REQUIRE(view->cards[0].id.value == u8"beta");
    REQUIRE(view->cards[1].id.value == u8"gamma");
    REQUIRE(view->cards[2].id.value == u8"alpha");

    // 문서 저장이 새 순서로 예약된다.
    REQUIRE(submitter.requests.size() == 1u);
    const gitman::operation_request& request { submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::save_document);
    REQUIRE(request.document.has_value());
    REQUIRE(request.document->projects.size() == 3u);
    REQUIRE(request.document->projects[0].id.value == u8"beta");
    REQUIRE(request.document->projects[1].id.value == u8"gamma");
    REQUIRE(request.document->projects[2].id.value == u8"alpha");
}

TEST_CASE("A drop that lands in place only switches to document order", "[logic][app][reorder]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha"), make_project(u8"beta") }));
    submitter.requests.clear();

    // alpha를 beta 앞에 놓아도 위치가 그대로다. 저장은 없고 정렬만 문서 순서가 된다.
    controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"beta" }, false });

    REQUIRE(submitter.requests.empty());
    const auto view { controller.make_view_snapshot() };
    REQUIRE(view->sort == gitman::card_sort_key::custom);
    REQUIRE(view->cards[0].id.value == u8"alpha");
    REQUIRE(view->cards[1].id.value == u8"beta");
}

TEST_CASE("An unknown reorder participant is ignored", "[logic][app][reorder]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha"), make_project(u8"beta") }));
    submitter.requests.clear();

    controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"ghost" }, gitman::project_id { u8"beta" }, false });
    controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"ghost" }, false });

    REQUIRE(submitter.requests.empty());
    REQUIRE(controller.make_view_snapshot()->sort == gitman::card_sort_key::name);
}

TEST_CASE("Saves are serialized, coalesced, and report failures as a notice", "[logic][app][reorder]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
    controller.handle(make_loaded_document({ make_project(u8"alpha"), make_project(u8"beta"), make_project(u8"gamma") }));
    submitter.requests.clear();

    // 첫 순서 변경이 저장을 내보내고, 진행 중의 두 번째 변경은 한 번으로 병합된다.
    controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"alpha" }, gitman::project_id { u8"gamma" }, true });
    controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"beta" }, gitman::project_id { u8"alpha" }, true });
    REQUIRE(submitter.requests.size() == 1u);

    // 실패 응답: notice가 맨 앞에 보이고 병합된 저장이 이어서 나간다.
    gitman::document_saved_event failed {};
    failed.operation_id = submitter.requests.front().operation_id;
    gitman::diagnostic error {};
    error.severity = gitman::diagnostic_severity::error;
    error.message = u8"문서를 저장하지 못했습니다.";
    failed.diagnostics.push_back(std::move(error));
    controller.handle(std::move(failed));

    REQUIRE(submitter.requests.size() == 2u);
    REQUIRE(controller.make_view_snapshot()->notices.front() == u8"문서를 저장하지 못했습니다.");

    // 최신 순서가 저장 내용이다: gamma, alpha, beta.
    const gitman::operation_request& queued { submitter.requests.back() };
    REQUIRE(queued.document->projects[0].id.value == u8"gamma");
    REQUIRE(queued.document->projects[1].id.value == u8"alpha");
    REQUIRE(queued.document->projects[2].id.value == u8"beta");

    // 성공 응답은 실패 notice를 지운다.
    gitman::document_saved_event saved {};
    saved.operation_id = queued.operation_id;
    saved.revision = { gitman::workspace_revision_token {} };
    controller.handle(std::move(saved));
    REQUIRE(controller.make_view_snapshot()->notices.empty());
}

