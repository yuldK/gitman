#include "application/logic_controller.h"
#include "helpers/ui_test_support.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"

#include "luil/ui/ui_events.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <deque>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace gitman::testing;

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

    gitman::operation_log_entry make_progress_entry(const std::u8string_view text)
    {
        gitman::operation_log_entry entry { make_entry(text, gitman::log_entry_kind::standard_error) };
        entry.progress = true;
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
    REQUIRE(gitman::log_entry_matches_filter(error, gitman::log_stream_filter::output) == false);

    REQUIRE(gitman::log_entry_matches_filter(error, gitman::log_stream_filter::errors));
    REQUIRE(gitman::log_entry_matches_filter(warning, gitman::log_stream_filter::errors));
    REQUIRE(gitman::log_entry_matches_filter(output, gitman::log_stream_filter::errors) == false);
    REQUIRE(gitman::log_entry_matches_filter(info, gitman::log_stream_filter::errors) == false);

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

TEST_CASE("Elapsed time formats as minutes and seconds", "[log][presentation]")
{
    // 로그 헤더의 경과 시간 표시다. 초 단위 내림이며 60분을 넘으면 분 자리가
    // 그대로 늘어난다.
    using namespace std::chrono_literals;
    REQUIRE(gitman::format_elapsed_time(0s) == u8"00:00");
    REQUIRE(gitman::format_elapsed_time(59s + 900ms) == u8"00:59");
    REQUIRE(gitman::format_elapsed_time(60s) == u8"01:00");
    REQUIRE(gitman::format_elapsed_time(75min + 7s) == u8"75:07");
    REQUIRE(gitman::format_elapsed_time(125min + 7s) == u8"125:07");
    REQUIRE(gitman::format_elapsed_time(-5s) == u8"00:00");
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

    // pane은 항상 열려 있다 (2026-08-20 검수). 선택을 해제하면 안내 제목의 빈
    // 모델이 된다.
    fixture.controller.handle(gitman::select_card_intent {});
    const auto empty_view { fixture.controller.make_view_snapshot() };
    REQUIRE(empty_view->log.has_value());
    REQUIRE(empty_view->log->card.value.empty());
    REQUIRE(empty_view->log->records.empty());
    REQUIRE(empty_view->log->title.empty() == false);
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

    // 헤더 버튼의 왼쪽 클릭 액션을 그 자리에서 실행한다. 어떤 액션이 나오는지가
    // gitman의 몫이고, 누름·뗌의 상태 기계는 luil test가 본다.
    std::vector<luil::input_action> invoke_click(const luil::ui_tree& tree, const luil::ui_element_kind kind, const std::u8string_view owner = u8"")
    {
        const luil::ui_element* const element { find(tree, kind, owner) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const action { element->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }
} // namespace

TEST_CASE("The tree shows the log pane with wired header buttons", "[log][ui]")
{
    const gitman::view_snapshot view { make_view_with_log() };
    const auto tree { make_tree(view) };

    const luil::ui_element* const pane { find(*tree, gitman::ui::kind_log_pane) };
    REQUIRE(pane != nullptr);
    REQUIRE(pane->visible());
    // D1-B: pane 자체는 클릭을 흡수만 하는 자리라 Tab이 서지 않는다. 헤더 버튼은 자리다.
    REQUIRE(pane->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_log_filter, u8"alpha")->tab_stop());

    const std::vector<luil::input_action> filter_actions { invoke_click(*tree, gitman::ui::kind_log_filter, u8"alpha") };
    const auto* const filter_intent { as_intent<gitman::set_log_filter_intent>(filter_actions) };
    REQUIRE(filter_intent != nullptr);
    REQUIRE(filter_intent->filter == gitman::log_stream_filter::output);

    const std::vector<luil::input_action> autoscroll_actions { invoke_click(*tree, gitman::ui::kind_log_autoscroll, u8"alpha") };
    const auto* const autoscroll_intent { as_intent<gitman::set_log_auto_scroll_intent>(autoscroll_actions) };
    REQUIRE(autoscroll_intent != nullptr);
    REQUIRE(autoscroll_intent->enabled == false);

    // 복사는 logic을 거치지 않고 클릭 시점의 표시 글을 클립보드 요청에 담는다.
    const std::vector<luil::input_action> copy_actions { invoke_click(*tree, gitman::ui::kind_log_copy, u8"alpha") };
    REQUIRE(copy_actions.size() == 1u);
    const auto* const copy_request { std::get_if<luil::clipboard_copy_request>(&copy_actions.front()) };
    REQUIRE(copy_request != nullptr);
    REQUIRE(copy_request->text == gitman::format_log_copy_text(*view.log));

    const std::vector<luil::input_action> clear_actions { invoke_click(*tree, gitman::ui::kind_log_clear, u8"alpha") };
    const auto* const clear_intent { as_intent<gitman::clear_log_intent>(clear_actions) };
    REQUIRE(clear_intent != nullptr);
    REQUIRE(clear_intent->id.value == u8"alpha");
}

TEST_CASE("An empty log disables copy and clear and a plain view has no pane", "[log][ui]")
{
    const auto tree { make_tree(make_view_with_log(false)) };
    REQUIRE(find(*tree, gitman::ui::kind_log_copy, u8"alpha")->enabled() == false);
    REQUIRE(find(*tree, gitman::ui::kind_log_clear, u8"alpha")->enabled() == false);

    gitman::view_snapshot plain { make_view_with_log() };
    plain.log.reset();
    const auto without { make_tree(plain) };
    REQUIRE(find(*without, gitman::ui::kind_log_pane) == nullptr);
}

TEST_CASE("Consecutive progress records collapse to the last one for display", "[log][presentation]")
{
    std::deque<gitman::operation_log_record> records {};
    const auto push = [&records](gitman::operation_log_entry entry) {
        gitman::operation_log_record record {};
        record.sequence = records.size() + 1;
        record.entry = std::move(entry);
        records.push_back(std::move(record));
    };
    push(make_entry(u8"시작", gitman::log_entry_kind::lifecycle));
    push(make_progress_entry(u8"진행 1"));
    push(make_progress_entry(u8"진행 2"));
    push(make_progress_entry(u8"진행 3"));
    push(make_entry(u8"중간", gitman::log_entry_kind::standard_output));
    push(make_progress_entry(u8"진행 4"));
    push(make_entry(u8"끝", gitman::log_entry_kind::lifecycle));

    const std::vector<gitman::log_display_line> lines { gitman::build_log_display_lines(records, gitman::log_stream_filter::all) };
    REQUIRE(lines.size() == 5u);
    REQUIRE(lines[0].record.entry.text == u8"시작");
    // run의 마지막 record만 남고 접힌 수가 표식으로 남는다.
    REQUIRE(lines[1].record.entry.text == u8"진행 3");
    REQUIRE(lines[1].collapsed == 2u);
    REQUIRE(lines[2].record.entry.text == u8"중간");
    // 길이 1의 run은 접히지 않는다.
    REQUIRE(lines[3].record.entry.text == u8"진행 4");
    REQUIRE(lines[3].collapsed == 0u);
    REQUIRE(lines[4].record.entry.text == u8"끝");

    // 줄 끝이 progress run이어도 마지막 run이 flush된다. count 함수는 build와
    // 같은 수를 센다.
    push(make_progress_entry(u8"진행 5"));
    push(make_progress_entry(u8"진행 6"));
    const std::vector<gitman::log_display_line> tail { gitman::build_log_display_lines(records, gitman::log_stream_filter::all) };
    REQUIRE(tail.size() == 6u);
    REQUIRE(tail.back().record.entry.text == u8"진행 6");
    REQUIRE(tail.back().collapsed == 1u);
    REQUIRE(gitman::log_display_line_count(records, gitman::log_stream_filter::all) == tail.size());

    // 필터를 통과하지 못한 record는 run 계산에도 들어가지 않는다. progress는
    // stderr라 output 필터에서 전부 사라진다.
    const std::vector<gitman::log_display_line> filtered { gitman::build_log_display_lines(records, gitman::log_stream_filter::output) };
    REQUIRE(filtered.size() == 3u);
    REQUIRE(gitman::log_display_line_count(records, gitman::log_stream_filter::output) == 3u);
}

TEST_CASE("The view snapshot carries collapsed lines and the scroll limit follows them", "[log][logic]")
{
    log_fixture fixture {};
    // 진행 표시 40줄과 일반 출력 1줄을 쏟는다. 표시 줄은 진행 1 + 일반 1만 는다.
    gitman::operation_log_event bulk {};
    bulk.operation_id = fixture.submitter.requests.back().operation_id;
    bulk.id.value = u8"alpha";
    for (int index = 0; index < 40; ++index)
        bulk.entries.push_back(make_progress_entry(u8"진행"));
    bulk.entries.push_back(make_entry(u8"완료", gitman::log_entry_kind::standard_output));
    fixture.controller.handle(std::move(bulk));

    const auto view { fixture.controller.make_view_snapshot() };
    // 기존 3줄(시작·stdout·stderr) + 접힌 진행 1줄 + 완료 1줄.
    REQUIRE(view->log->lines.size() == 5u);
    REQUIRE(view->log->lines[3].collapsed == 39u);
    // 복사용 records는 접지 않는다 (buffer 보존).
    REQUIRE(view->log->records.size() == 44u);

    // 표시 줄 5개는 pane 화면(논리 134px)보다 짧아 스크롤 여유가 없다. 위로
    // 굴려도 자동 스크롤이 꺼질 뿐 offset은 0이다.
    fixture.controller.handle(gitman::log_scroll_intent { -48.0f });
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == 0.0f);
}

TEST_CASE("A long log shows the pane scrollbar and dragging it scrolls the log", "[log][ui]")
{
    log_fixture fixture {};
    gitman::operation_log_event bulk {};
    bulk.operation_id = fixture.submitter.requests.back().operation_id;
    bulk.id.value = u8"alpha";
    for (int index = 0; index < 60; ++index)
        bulk.entries.push_back(make_entry(u8"line", gitman::log_entry_kind::standard_output));
    fixture.controller.handle(std::move(bulk));

    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const scrollbar { find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_log) };
    REQUIRE(scrollbar != nullptr);
    REQUIRE(scrollbar->visible());

    // 막대를 잡고 위로 끌면 log_scroll_intent가 흐른다 (자동 스크롤 규칙이
    // 유지되는 경로). 끌기의 산술은 luil test가 본다.
    interaction_harness harness { tree };
    const luil::rect_f box { scrollbar->bounds() };
    const float x { box.x + box.width / 2.0f };
    static_cast<void>(harness.process(luil::pointer_pressed_event { x, box.y + 1.0f, luil::pointer_button::left, at(0) }));
    const std::vector<luil::input_action> actions { harness.process(luil::pointer_moved_event { x, box.y - 20.0f, at(10) }) };
    const auto* const intent { as_intent<gitman::log_scroll_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->delta < 0.0f);
}

TEST_CASE("A short log hides the pane scrollbar", "[log][ui]")
{
    log_fixture fixture {};
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const scrollbar { find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_log) };
    REQUIRE(scrollbar != nullptr);
    REQUIRE(scrollbar->visible() == false);
    const auto scroll { find(*tree, gitman::ui::kind_log_lines, u8"alpha")->accessibility().scroll };
    REQUIRE(scroll.has_value());
    REQUIRE(scroll->vertical_percent == luil::access_no_scroll);
    REQUIRE(scroll->vertical_view_size == 100.0f);
}

TEST_CASE("The wheel over the log pane scrolls the log instead of the list", "[log][ui]")
{
    const gitman::view_snapshot view { make_view_with_log() };
    interaction_harness harness { make_tree(view) };

    // 로그 pane 위: window 600 높이에서 pane은 아래 160이다.
    const std::vector<luil::input_action> log_actions { harness.process(luil::mouse_wheel_event { 400.0f, 520.0f, -120.0f, at(0) }) };
    REQUIRE(as_intent<gitman::log_scroll_intent>(log_actions) != nullptr);

    // 카드 목록 위는 기존 목록 스크롤이다.
    const std::vector<luil::input_action> list_actions { harness.process(luil::mouse_wheel_event { 400.0f, 200.0f, -120.0f, at(0) }) };
    REQUIRE(as_intent<gitman::scroll_intent>(list_actions) != nullptr);
}

namespace {
    // 로그 줄 element의 owner다: 카드 id + `:` + 그 줄 record의 sequence(10진). 로그 fixture의 카드는 `alpha`다.
    std::u8string log_line_owner(const gitman::log_display_line& line)
    {
        const std::string digits { std::to_string(line.record.sequence) };
        return std::u8string { u8"alpha:" } + std::u8string { digits.begin(), digits.end() };
    }

    // 60줄 출력 뒤에 오류 한 줄을 더한 긴 로그다. 자동 스크롤이라 맨 아래를 보고 있다.
    void add_long_log(log_fixture& fixture)
    {
        gitman::operation_log_event bulk {};
        bulk.operation_id = fixture.submitter.requests.back().operation_id;
        bulk.id.value = u8"alpha";
        for (int index = 0; index < 60; ++index)
        {
            const std::string digits { std::to_string(index) };
            std::u8string text { u8"line " };
            text.append(digits.begin(), digits.end());
            bulk.entries.push_back(make_entry(text, gitman::log_entry_kind::standard_output));
        }
        bulk.entries.push_back(make_entry(u8"실패", gitman::log_entry_kind::standard_error, gitman::diagnostic_severity::error));
        fixture.controller.handle(std::move(bulk));
    }
} // namespace

TEST_CASE("Assistive technology sets the log scrollbar and following resumes at the end", "[function_buttons][s4][log][ui]")
{
    log_fixture fixture {};
    add_long_log(fixture);

    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const bar { find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_log) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->visible());
    const float maximum { bar->accessibility().range->maximum };
    REQUIRE(bar->accessibility().range->value == maximum);

    const std::vector<luil::input_action> actions { set_scroll_value(*bar, 0.0f) };
    const auto* const intent { as_intent<gitman::log_scroll_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->offset == std::optional<float> { 0.0f });

    // 맨 위로 정하면 따라가기가 꺼진다. 지금 자리를 보지 않는 절대 자리다.
    fixture.controller.handle(*intent);
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == 0.0f);
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll == false);
    fixture.controller.handle(gitman::log_scroll_intent { 0.0f, 50.0f });
    fixture.controller.handle(gitman::log_scroll_intent { 0.0f, 50.0f });
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == 50.0f);
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll == false);

    // 끝으로 정하면 휠로 끝에 닿을 때처럼 다시 따라간다.
    fixture.controller.handle(gitman::log_scroll_intent { 0.0f, maximum });
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == maximum);
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll);
}

TEST_CASE("Log lines stand in the access tree as list items", "[function_buttons][s4][log][ui]")
{
    log_fixture fixture {};
    add_long_log(fixture);

    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { make_tree(*view) };
    const std::vector<gitman::log_display_line>& lines { view->log->lines };
    REQUIRE(lines.size() > 60u);

    // 본문은 목록이다. 이름은 `로그: 카드 이름, N줄`이라 카드 항목과 갈리고, 보이는 항목만 세는 보조 기술에 전체 줄 수를
    // 말한다 (S4 검토). 헤더 버튼은 그 목록 밖이다.
    const luil::ui_element* const list { find(*tree, gitman::ui::kind_log_lines, u8"alpha") };
    REQUIRE(list != nullptr);
    REQUIRE(list->accessibility().role == luil::access_role::list);
    const std::string count { std::to_string(lines.size()) };
    REQUIRE(list->accessibility().name == u8"로그: " + view->log->title + u8", " + std::u8string { count.begin(), count.end() } + u8"줄");
    REQUIRE(in_access_tree(*tree, list->id()));
    const luil::ui_element* const filter_parent { luil::access_parent(*tree, luil::ui_element_id { gitman::ui::kind_log_filter, std::u8string { u8"alpha" } }) };
    REQUIRE((filter_parent == nullptr || filter_parent->id() != list->id()));

    // 맨 아래를 보고 있으므로 마지막 줄이 항목으로 서고 첫 줄은 없다. 항목 이름은 그려진 글 그대로이고, 색으로만 보이던
    // 오류 줄은 이름이 오류라고 말한다.
    const gitman::log_display_line& last { lines.back() };
    const luil::ui_element* const last_item { find(*tree, gitman::ui::kind_log_line, log_line_owner(last)) };
    REQUIRE(last_item != nullptr);
    REQUIRE(last_item->accessibility().role == luil::access_role::list_item);
    REQUIRE(last_item->accessibility().name == gitman::format_log_timestamp(last.record.entry.time) + u8" 오류: 실패");
    REQUIRE(in_access_tree(*tree, last_item->id()));
    REQUIRE(luil::access_parent(*tree, last_item->id()) == list);
    const gitman::log_display_line& before_last { lines[lines.size() - 2u] };
    REQUIRE(find(*tree, gitman::ui::kind_log_line, log_line_owner(before_last))->accessibility().name == gitman::format_log_timestamp(before_last.record.entry.time) + u8" line 59");
    REQUIRE(find(*tree, gitman::ui::kind_log_line, log_line_owner(lines.front())) == nullptr);
    // 항목은 누를 것이 없고 Tab 자리도 아니다. 빈 곳 클릭은 지금처럼 pane이 받는다.
    REQUIRE(last_item->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(last_item->tab_stop() == false);
    REQUIRE(tree->hit_test(last_item->bounds().x + 5.0f, last_item->bounds().y + 2.0f)->id().kind == gitman::ui::kind_log_pane);

    // 맨 위로 흘리면 첫 줄이 서고 마지막 줄은 빠진다.
    fixture.controller.handle(gitman::log_scroll_intent { -100000.0f });
    const auto top_view { fixture.controller.make_view_snapshot() };
    const auto top { make_tree(*top_view) };
    REQUIRE(find(*top, gitman::ui::kind_log_line, log_line_owner(top_view->log->lines.front())) != nullptr);
    REQUIRE(find(*top, gitman::ui::kind_log_line, log_line_owner(top_view->log->lines.back())) == nullptr);
}

TEST_CASE("A log line half above the body is clipped to the body", "[function_buttons][s4][log][ui]")
{
    log_fixture fixture {};
    add_long_log(fixture);
    // 첫 줄이 본문 위로 10px 나가게 흘린다 (줄 높이 안이라 항목으로 남는다).
    fixture.controller.handle(gitman::log_scroll_intent { 0.0f, gitman::layout_log_text_inset + 10.0f });
    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { make_tree(*view) };
    const luil::ui_element* const list { find(*tree, gitman::ui::kind_log_lines, u8"alpha") };
    REQUIRE(list != nullptr);
    const float body_top { list->bounds().y };
    const luil::ui_element* const first { find(*tree, gitman::ui::kind_log_line, log_line_owner(view->log->lines.front())) };
    REQUIRE(first != nullptr);
    REQUIRE(first->bounds().y < body_top);

    // 보조 기술이 보는 사각형(UIA BoundingRectangle)은 본문 안으로 잘린다 (S4 검토).
    const std::optional<luil::rect_f> visible { tree->visible_bounds(*first) };
    REQUIRE(visible.has_value());
    REQUIRE(visible->y >= body_top);

    // 헤더 단추 아래쪽을 짚으면 걸친 줄이 아니라 그 단추다.
    const luil::ui_element* const copy { find(*tree, gitman::ui::kind_log_copy, u8"alpha") };
    REQUIRE(copy != nullptr);
    const float probe_y { copy->bounds().y + copy->bounds().height - 1.0f };
    REQUIRE(probe_y > first->bounds().y);
    const luil::ui_element* const at { luil::access_element_at(*tree, copy->bounds().x + copy->bounds().width / 2.0f, probe_y) };
    REQUIRE(at != nullptr);
    REQUIRE(at->id() == copy->id());
}

TEST_CASE("Log line ids carry the card so switching cards makes new items", "[function_buttons][s4][log][ui]")
{
    // sequence는 카드마다 1부터 센다. owner에 카드가 없으면 카드를 바꿀 때 같은 id가 다른 줄을 가리킨다 (S4 검토).
    REQUIRE(gitman::ui::log_line_owner(u8"alpha", 5u) == u8"alpha:5");
    REQUIRE(gitman::ui::log_line_owner(u8"beta", 5u) == u8"beta:5");
    REQUIRE(gitman::ui::log_line_owner(u8"a:b", 0u) == u8"a:b:0");
    REQUIRE(gitman::ui::log_line_owner(u8"x", 1234567890123u) == u8"x:1234567890123");
}

TEST_CASE("An empty log has an empty line list", "[function_buttons][s4][log][ui]")
{
    const gitman::view_snapshot view { make_view_with_log(false) };
    const auto tree { make_tree(view) };
    const luil::ui_element* const list { find(*tree, gitman::ui::kind_log_lines, u8"alpha") };
    REQUIRE(list != nullptr);
    REQUIRE(list->accessibility().role == luil::access_role::list);
    REQUIRE(tree->ids_of_kind(gitman::ui::kind_log_line).empty());
    // 비었을 때 그려지는 안내는 목록 이름이 말한다 (S4 검토). 오래된 줄이 지워졌으면 제목처럼 그것도 말한다.
    REQUIRE(list->accessibility().name == u8"로그: " + view.log->title + u8", 이 카드에서 실행한 작업의 로그가 여기에 표시됩니다.");

    gitman::log_view_model none {};
    none.title = u8"로그";
    REQUIRE(gitman::log_list_access_name(none) == u8"로그, 카드를 선택하면 그 카드에서 실행한 작업의 로그가 여기에 표시됩니다.");
    gitman::log_view_model trimmed { *view.log };
    trimmed.truncated = true;
    trimmed.filter = gitman::log_stream_filter::errors;
    REQUIRE(gitman::log_list_access_name(trimmed) == u8"로그: " + view.log->title + u8" (오래된 로그 제거됨), 필터와 일치하는 로그가 없습니다.");
}

TEST_CASE("Setting a published log scrollbar to its own end follows even after more lines arrive", "[function_buttons][s4][log][ui]")
{
    log_fixture fixture {};
    add_long_log(fixture);
    fixture.controller.handle(gitman::log_scroll_intent { 0.0f, 0.0f });
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll == false);

    // 같은 발행본 안에서다: 그 막대의 끝은 그 사이 logic에 줄이 더 와도 끝을 뜻한다. luil은 값 정하기를 받은 때의 최신
    // 발행본에서 막대를 찾으므로, 실제 앱에서 이 길은 읽기와 정하기 사이에 새 발행본이 없을 때만이다 (아래 한계).
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const bar { find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_log) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->accessibility().name == u8"로그 스크롤");
    const float stale_end { bar->accessibility().range->maximum };
    add_long_log(fixture);

    const std::vector<luil::input_action> actions { set_scroll_value(*bar, stale_end) };
    const auto* const intent { as_intent<gitman::log_scroll_intent>(actions) };
    REQUIRE(intent != nullptr);
    fixture.controller.handle(*intent);
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->log->auto_scroll);
    REQUIRE(view->log->scroll_offset > stale_end);

    // 끝이 아닌 값은 그 자리다.
    const std::vector<luil::input_action> middle_actions { set_scroll_value(*bar, stale_end - 40.0f) };
    const auto* const middle { as_intent<gitman::log_scroll_intent>(middle_actions) };
    REQUIRE(middle != nullptr);
    REQUIRE(middle->offset == std::optional<float> { stale_end - 40.0f });

    // RangeValue의 한계: 옛 끝을 새 막대에 정하면 그 픽셀에 선다. 끝을 뜻하려면 아래 Scroll 백분율 경로를 쓴다.
    fixture.controller.handle(gitman::log_scroll_intent { 0.0f, 0.0f });
    const auto read_tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const float read_end { find(*read_tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_log)->accessibility().range->maximum };
    add_long_log(fixture);
    const auto latest_tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const latest_bar { find(*latest_tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_log) };
    REQUIRE(latest_bar->accessibility().range->maximum > read_end);
    const std::vector<luil::input_action> late_actions { set_scroll_value(*latest_bar, read_end) };
    const auto* const late { as_intent<gitman::log_scroll_intent>(late_actions) };
    REQUIRE(late != nullptr);
    fixture.controller.handle(*late);
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll == false);
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == read_end);

    // 값을 정하는 길이 NaN을 보내도 스크롤 값에 남지 않는다 (S4 검토). luil의 범위 확인은 NaN을 거르지 않는다.
    fixture.controller.handle(gitman::log_scroll_intent { 0.0f, std::numeric_limits<float>::quiet_NaN() });
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == read_end);
}

TEST_CASE("Log Scroll percentages use the latest length and keep following at 100 percent", "[function_buttons][luil_followup][log][ui]")
{
    log_fixture fixture {};
    add_long_log(fixture);
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const list { find(*tree, gitman::ui::kind_log_lines, u8"alpha") };
    REQUIRE(list != nullptr);
    const auto state { list->accessibility().scroll };
    REQUIRE(state.has_value());
    REQUIRE(state->horizontal_percent == luil::access_no_scroll);
    REQUIRE(state->vertical_percent == 100.0f);
    REQUIRE(state->vertical_view_size > 0.0f);
    REQUIRE(state->vertical_view_size < 100.0f);
    const auto middle { luil::plan_access_request(*list, { .command = luil::access_command::set_scroll_percent, .vertical_percent = 50.0f }) };
    const auto end { luil::plan_access_request(*list, { .command = luil::access_command::set_scroll_percent, .vertical_percent = 100.0f }) };
    REQUIRE(middle.has_value());
    REQUIRE(end.has_value());
    REQUIRE(as_intent<gitman::log_scroll_intent>(*middle)->percent == 50.0f);

    // 읽은 발행본 뒤에 출력이 늘어도 처리하는 순간의 길이에서 중간과 끝을 찾는다.
    add_long_log(fixture);
    const auto longer { make_tree(*fixture.controller.make_view_snapshot()) };
    const float maximum { find(*longer, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_log)->accessibility().range->maximum };
    fixture.controller.handle(*as_intent<gitman::log_scroll_intent>(*middle));
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == maximum * 0.5f);
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll == false);
    add_long_log(fixture);
    fixture.controller.handle(*as_intent<gitman::log_scroll_intent>(*end));
    REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset > maximum);
    REQUIRE(fixture.controller.make_view_snapshot()->log->auto_scroll);

    // 같은 100%도 실행한다. 흐르는 로그를 따라가는 절대 명령이라 현재 값과 같아도 생략하지 않는다.
    REQUIRE(luil::plan_access_request(*list, { .command = luil::access_command::set_scroll_percent, .vertical_percent = 100.0f })->empty() == false);
    REQUIRE(luil::plan_access_request(*list, { .command = luil::access_command::set_scroll_percent })->empty());
    for (const float invalid : { -2.0f, 101.0f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity() })
    {
        REQUIRE(luil::plan_access_request(*list, { .command = luil::access_command::set_scroll_percent, .vertical_percent = invalid }).has_value() == false);
        const float before { fixture.controller.make_view_snapshot()->log->scroll_offset };
        fixture.controller.handle(gitman::log_scroll_intent { .percent = invalid });
        REQUIRE(fixture.controller.make_view_snapshot()->log->scroll_offset == before);
    }
}
