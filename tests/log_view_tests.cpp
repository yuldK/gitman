#include "application/logic_controller.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
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
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::operation_request> requests {};
    };

    gitman::operation_log_entry make_entry(const std::u8string_view text, const gitman::log_entry_kind kind, const gitman::diagnostic_severity severity = gitman::diagnostic_severity::information)
    {
        gitman::operation_log_entry entry {};
        entry.kind = kind;
        entry.severity = severity;
        entry.text = text;
        return entry;
    }

    // 문서와 카드 하나, 실행 중인 update와 로그 3줄(stdout, stderr, lifecycle 시작)을
    // 준비한 controller다.
    struct log_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        log_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            gitman::project_definition project {};
            project.id.value = u8"alpha";
            project.display_name = u8"alpha";
            project.path.original = u8"C:\\work\\alpha";
            project.path.normalized = project.path.original;
            document.projects.push_back(std::move(project));
            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));

            // 초기 로컬 조회를 끝내 카드 busy를 풀어야 update를 시작할 수 있다.
            gitman::query_completed_event local {};
            local.id.value = u8"alpha";
            local.generation = 1;
            local.final_event = true;
            local.result.snapshot.project.value = u8"alpha";
            local.result.snapshot.availability = gitman::repository_availability::ready;
            local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
            controller.handle(std::move(local));

            controller.handle(gitman::select_card_intent { { gitman::project_id { u8"alpha" } } });
            controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });

            gitman::operation_log_event log {};
            log.operation_id = submitter.requests.back().operation_id;
            log.id.value = u8"alpha";
            log.entries.push_back(make_entry(u8"output-line", gitman::log_entry_kind::standard_output));
            log.entries.push_back(make_entry(u8"error-line", gitman::log_entry_kind::standard_error));
            controller.handle(std::move(log));
        }
    };
} // namespace

TEST_CASE("Log filters keep the rules shared between logic and UI", "[log][presentation]")
{
    const gitman::operation_log_entry output { make_entry(u8"o", gitman::log_entry_kind::standard_output) };
    const gitman::operation_log_entry error { make_entry(u8"e", gitman::log_entry_kind::standard_error) };
    const gitman::operation_log_entry info { make_entry(u8"i", gitman::log_entry_kind::lifecycle) };
    const gitman::operation_log_entry warning { make_entry(u8"w", gitman::log_entry_kind::lifecycle, gitman::diagnostic_severity::warning) };

    REQUIRE(gitman::log_entry_matches_filter(output, gitman::log_stream_filter::all));
    REQUIRE(gitman::log_entry_matches_filter(error, gitman::log_stream_filter::all));

    REQUIRE(gitman::log_entry_matches_filter(output, gitman::log_stream_filter::output));
    REQUIRE(gitman::log_entry_matches_filter(info, gitman::log_stream_filter::output));
    REQUIRE_FALSE(gitman::log_entry_matches_filter(error, gitman::log_stream_filter::output));

    REQUIRE(gitman::log_entry_matches_filter(error, gitman::log_stream_filter::errors));
    REQUIRE(gitman::log_entry_matches_filter(warning, gitman::log_stream_filter::errors));
    REQUIRE_FALSE(gitman::log_entry_matches_filter(output, gitman::log_stream_filter::errors));
    REQUIRE_FALSE(gitman::log_entry_matches_filter(info, gitman::log_stream_filter::errors));

    // 필터 버튼은 전체 → 출력 → 오류 → 전체로 순환한다.
    REQUIRE(gitman::next_log_filter(gitman::log_stream_filter::all) == gitman::log_stream_filter::output);
    REQUIRE(gitman::next_log_filter(gitman::log_stream_filter::output) == gitman::log_stream_filter::errors);
    REQUIRE(gitman::next_log_filter(gitman::log_stream_filter::errors) == gitman::log_stream_filter::all);
}

TEST_CASE("The copy text carries the visible records with time and stream tags", "[log][presentation]")
{
    gitman::log_view_model log {};
    gitman::operation_log_record first {};
    first.sequence = 1;
    first.entry = make_entry(u8"hello", gitman::log_entry_kind::standard_output);
    gitman::operation_log_record second {};
    second.sequence = 2;
    second.entry = make_entry(u8"world", gitman::log_entry_kind::standard_error);
    log.records.push_back(std::move(first));
    log.records.push_back(std::move(second));

    const std::u8string text { gitman::format_log_copy_text(log) };
    REQUIRE(text.find(u8"[stdout] hello\r\n") != std::u8string::npos);
    REQUIRE(text.find(u8"[stderr] world\r\n") != std::u8string::npos);
    // 시각 형식은 HH:MM:SS다.
    REQUIRE(gitman::format_log_timestamp(std::chrono::system_clock::now()).size() == 8u);
}

TEST_CASE("The list layout gives the log pane room and shrinks it before the list disappears", "[log][layout]")
{
    const gitman::list_layout without { gitman::compute_list_layout(600.0f, 1.0f, false, false) };
    REQUIRE(without.log_height == 0.0f);

    const gitman::list_layout with { gitman::compute_list_layout(600.0f, 1.0f, false, true) };
    REQUIRE(with.log_height == gitman::layout_log_pane_height);
    REQUIRE(with.viewport_height == without.viewport_height - gitman::layout_log_pane_height);
    REQUIRE(with.log_top == with.content_top + with.viewport_height);

    // 창이 아주 작으면 로그가 먼저 줄어들어 목록이 음수가 되지 않는다.
    const gitman::list_layout tiny { gitman::compute_list_layout(100.0f, 1.0f, false, true) };
    REQUIRE(tiny.viewport_height >= 0.0f);
    REQUIRE(tiny.log_height <= 100.0f);
}

TEST_CASE("Selecting a card exposes its log in the view snapshot", "[log][logic]")
{
    log_fixture fixture {};
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->log.has_value());
    REQUIRE(view->log->card.value == u8"alpha");
    REQUIRE(view->log->title == u8"alpha");
    // 시작 lifecycle + stdout + stderr.
    REQUIRE(view->log->records.size() == 3u);
    REQUIRE(view->log->auto_scroll);

    fixture.controller.handle(gitman::select_card_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->log.has_value() == false);
}

TEST_CASE("The log filter narrows the visible records and selection change resets it", "[log][logic]")
{
    log_fixture fixture {};
    fixture.controller.handle(gitman::set_log_filter_intent { gitman::log_stream_filter::errors });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->log->filter == gitman::log_stream_filter::errors);
        REQUIRE(view->log->records.size() == 1u);
        REQUIRE(view->log->records.front().entry.text == u8"error-line");
    }

    // 선택을 풀었다 다시 잡으면 필터가 기본값으로 돌아간다.
    fixture.controller.handle(gitman::select_card_intent {});
    fixture.controller.handle(gitman::select_card_intent { { gitman::project_id { u8"alpha" } } });
    REQUIRE(fixture.controller.make_view_snapshot()->log->filter == gitman::log_stream_filter::all);
}

TEST_CASE("Scrolling the log up disables auto scroll and the bottom re-enables it", "[log][logic]")
{
    log_fixture fixture {};
    // 화면보다 긴 로그를 만든다.
    gitman::operation_log_event bulk {};
    bulk.operation_id = fixture.submitter.requests.back().operation_id;
    bulk.id.value = u8"alpha";
    for (int index = 0; index < 60; ++index)
        bulk.entries.push_back(make_entry(u8"line", gitman::log_entry_kind::standard_output));
    fixture.controller.handle(std::move(bulk));

    fixture.controller.handle(gitman::log_scroll_intent { -48.0f });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->log->auto_scroll == false);
        REQUIRE(view->log->scroll_offset >= 0.0f);
    }

    // 아주 큰 값으로 내리면 맨 아래에 고정되고 자동 스크롤이 돌아온다.
    fixture.controller.handle(gitman::log_scroll_intent { 100000.0f });
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll);

    // 토글 intent도 동작한다.
    fixture.controller.handle(gitman::set_log_auto_scroll_intent { false });
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll == false);
}

namespace {
    gitman::view_snapshot make_view_with_log(const bool with_records = true)
    {
        gitman::view_snapshot view {};
        view.document_path = u8"C:\\work\\p.version-list";
        view.window_width = 800.0f;
        view.window_height = 600.0f;
        view.scale = 1.0f;

        gitman::card_view_model card {};
        card.id.value = u8"alpha";
        card.display_name = u8"alpha";
        card.selected = true;
        view.cards.push_back(std::move(card));
        view.selected = { gitman::project_id { u8"alpha" } };

        gitman::log_view_model log {};
        log.card.value = u8"alpha";
        log.title = u8"alpha";
        if (with_records)
        {
            gitman::operation_log_record record {};
            record.sequence = 1;
            record.entry = make_entry(u8"line", gitman::log_entry_kind::standard_output);
            log.records.push_back(std::move(record));
        }
        view.log = { std::move(log) };
        return view;
    }

    // 첫 logic_message 액션을 꺼낸다. 없으면 monostate다.
    gitman::logic_message first_logic_message(const std::vector<gitman::ui::input_action>& actions)
    {
        for (const gitman::ui::input_action& action : actions)
            if (const auto* const message { std::get_if<gitman::logic_message>(&action) }; message != nullptr)
                return *message;
        return gitman::logic_message { gitman::shutdown_message {} };
    }
} // namespace

TEST_CASE("The tree shows the log pane with wired header buttons", "[log][ui]")
{
    const auto tree { gitman::ui::build_ui_tree(make_view_with_log()) };

    const gitman::ui::ui_element* const pane { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::log_pane }) };
    REQUIRE(pane != nullptr);
    REQUIRE(pane->visible());

    const gitman::ui::ui_element_id owner_id { gitman::ui::ui_element_kind::log_filter, gitman::project_id { u8"alpha" } };
    const gitman::ui::ui_element* const filter { tree->find(owner_id) };
    REQUIRE(filter != nullptr);
    const auto filter_actions { (*filter->action(gitman::ui::ui_trigger::left_click))({}) };
    const gitman::logic_message filter_message { first_logic_message(filter_actions) };
    const auto* const filter_intent { std::get_if<gitman::set_log_filter_intent>(&filter_message) };
    REQUIRE(filter_intent != nullptr);
    REQUIRE(filter_intent->filter == gitman::log_stream_filter::output);

    const gitman::ui::ui_element* const autoscroll { tree->find({ gitman::ui::ui_element_kind::log_autoscroll, gitman::project_id { u8"alpha" } }) };
    REQUIRE(autoscroll != nullptr);
    const auto autoscroll_actions { (*autoscroll->action(gitman::ui::ui_trigger::left_click))({}) };
    const gitman::logic_message autoscroll_message { first_logic_message(autoscroll_actions) };
    const auto* const autoscroll_intent { std::get_if<gitman::set_log_auto_scroll_intent>(&autoscroll_message) };
    REQUIRE(autoscroll_intent != nullptr);
    REQUIRE(autoscroll_intent->enabled == false);

    const gitman::ui::ui_element* const copy { tree->find({ gitman::ui::ui_element_kind::log_copy, gitman::project_id { u8"alpha" } }) };
    REQUIRE(copy != nullptr);
    const auto copy_actions { (*copy->action(gitman::ui::ui_trigger::left_click))({}) };
    bool found_copy_command { false };
    for (const gitman::ui::input_action& action : copy_actions)
        if (const auto* const command { std::get_if<gitman::ui::ui_command>(&action) }; command != nullptr && *command == gitman::ui::ui_command::copy_selected_log)
            found_copy_command = true;
    REQUIRE(found_copy_command);

    const gitman::ui::ui_element* const clear { tree->find({ gitman::ui::ui_element_kind::log_clear, gitman::project_id { u8"alpha" } }) };
    REQUIRE(clear != nullptr);
    const auto clear_actions { (*clear->action(gitman::ui::ui_trigger::left_click))({}) };
    const gitman::logic_message clear_message { first_logic_message(clear_actions) };
    const auto* const clear_intent { std::get_if<gitman::clear_log_intent>(&clear_message) };
    REQUIRE(clear_intent != nullptr);
    REQUIRE(clear_intent->id.value == u8"alpha");
}

TEST_CASE("An empty log disables copy and clear and a plain view has no pane", "[log][ui]")
{
    const auto tree { gitman::ui::build_ui_tree(make_view_with_log(false)) };
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::log_copy, gitman::project_id { u8"alpha" } })->enabled() == false);
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::log_clear, gitman::project_id { u8"alpha" } })->enabled() == false);

    gitman::view_snapshot plain { make_view_with_log() };
    plain.log.reset();
    const auto without { gitman::ui::build_ui_tree(plain) };
    REQUIRE(without->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::log_pane }) == nullptr);
}

TEST_CASE("The wheel over the log pane scrolls the log instead of the list", "[log][ui]")
{
    gitman::ui::interaction_controller controller {};
    controller.set_tree(gitman::ui::build_ui_tree(make_view_with_log()));

    // 로그 pane 위: window 600 높이에서 pane은 아래 160이다.
    const auto log_actions { controller.process(gitman::ui::mouse_wheel_event { 400.0f, 520.0f, 120.0f }) };
    const gitman::logic_message log_message { first_logic_message(log_actions) };
    REQUIRE(std::get_if<gitman::log_scroll_intent>(&log_message) != nullptr);

    // 카드 목록 위는 기존 목록 스크롤이다.
    const auto list_actions { controller.process(gitman::ui::mouse_wheel_event { 400.0f, 200.0f, 120.0f }) };
    const gitman::logic_message list_message { first_logic_message(list_actions) };
    REQUIRE(std::get_if<gitman::scroll_intent>(&list_message) != nullptr);
}
