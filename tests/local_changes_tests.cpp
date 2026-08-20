#include "application/logic_controller.h"
#include "domain/local_changes.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/git_command_builder.h"
#include "infrastructure/git_repository_provider.h"
#include "infrastructure/git_status_parser.h"
#include "infrastructure/local_change_reader.h"
#include "infrastructure/svn_command_builder.h"
#include "infrastructure/svn_output_parser.h"
#include "presentation/diff_presentation.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/local_changes_dialog_element.h"
#include "presentation/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    constexpr std::u8string_view repository_path { u8"C:\\작업 공간\\repo" };

    std::u8string join_lines(const std::vector<std::u8string_view>& values)
    {
        std::u8string text {};
        for (const std::u8string_view value : values)
        {
            text.append(value);
            text.push_back(u8'\n');
        }
        return text;
    }

    gitman::vcs_tool_info available_git_tool()
    {
        gitman::vcs_tool_info tool {};
        tool.kind = gitman::repository_kind::git;
        tool.availability = gitman::vcs_tool_availability::available;
        tool.executable = u8"C:\\Program Files\\Git\\cmd\\git.exe";
        tool.version = { 2, 52, 0 };
        return tool;
    }

    gitman::project_definition make_project()
    {
        gitman::project_definition project {};
        project.id.value = u8"repo-1";
        project.path.original = repository_path;
        project.path.normalized = repository_path;
        return project;
    }

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

    // 카드 alpha 하나를 갖춘 controller다. 초기 query_local 요청은 기록에서 지운다.
    struct dialog_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        dialog_fixture()
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
            submitter.requests.clear();
        }
    };

    gitman::local_changes_event make_list_event(const std::uint64_t operation_id, std::vector<gitman::local_change_entry> entries)
    {
        gitman::local_changes_event event {};
        event.operation_id = operation_id;
        event.id.value = u8"alpha";
        event.result.entries = std::move(entries);
        return event;
    }
} // namespace

TEST_CASE("Status entries map to local change entries with kinds", "[infrastructure][local-changes][parser]")
{
    // Git: porcelain v2의 레코드 종류와 XY 문자를 항목 종류로 옮긴다.
    const std::vector<std::u8string> git_lines {
        u8"# branch.oid abc",
        u8"# branch.head main",
        u8"1 .M N... 100644 100644 100644 a a modified.txt",
        u8"1 A. N... 000000 100644 100644 a a added.txt",
        u8"1 .D N... 100644 100644 000000 a a deleted.txt",
        u8"2 R. N... 100644 100644 100644 a a R100 renamed.txt\told.txt",
        u8"u UU N... 100644 100644 100644 100644 a b c conflicted.txt",
        u8"? untracked.txt",
        u8"! ignored.txt",
    };
    const std::vector<gitman::local_change_entry> git_entries { gitman::collect_git_local_changes(gitman::parse_git_status_porcelain_v2(git_lines)) };
    REQUIRE(git_entries.size() == 6u);
    REQUIRE(git_entries[0].kind == gitman::local_change_kind::modified);
    REQUIRE(git_entries[1].kind == gitman::local_change_kind::added);
    REQUIRE(git_entries[2].kind == gitman::local_change_kind::deleted);
    REQUIRE(git_entries[3].kind == gitman::local_change_kind::renamed);
    REQUIRE(git_entries[3].path == u8"renamed.txt");
    REQUIRE(git_entries[4].kind == gitman::local_change_kind::conflicted);
    REQUIRE(git_entries[5].kind == gitman::local_change_kind::untracked);
    REQUIRE(git_entries[5].path == u8"untracked.txt");

    // SVN: 상태 문자를 같은 종류로 옮기고 무시·외부 항목은 뺀다.
    const std::vector<std::u8string> svn_lines {
        u8"M       modified.txt",
        u8"A       added.txt",
        u8"D       deleted.txt",
        u8"?       untracked.txt",
        u8"C       conflicted.txt",
        u8"I       ignored.txt",
        u8"X       external",
    };
    const std::vector<gitman::local_change_entry> svn_entries { gitman::collect_svn_local_changes(gitman::parse_svn_status(svn_lines)) };
    REQUIRE(svn_entries.size() == 5u);
    REQUIRE(svn_entries[0].kind == gitman::local_change_kind::modified);
    REQUIRE(svn_entries[1].kind == gitman::local_change_kind::added);
    REQUIRE(svn_entries[2].kind == gitman::local_change_kind::deleted);
    REQUIRE(svn_entries[3].kind == gitman::local_change_kind::untracked);
    REQUIRE(svn_entries[4].kind == gitman::local_change_kind::conflicted);
}

TEST_CASE("Diff request builders quote the path and use the query class", "[infrastructure][local-changes][command]")
{
    const gitman::process_request git { gitman::make_git_diff_request(u8"C:\\tools\\git.exe", repository_path, u8"a b.txt") };
    // 공통 인자 7개 뒤가 명령이다.
    REQUIRE(git.arguments.size() >= 5u);
    const std::vector<std::u8string> git_tail { git.arguments.end() - 5, git.arguments.end() };
    REQUIRE(git_tail == std::vector<std::u8string> { u8"diff", u8"HEAD", u8"--no-color", u8"--", u8"a b.txt" });
    REQUIRE(*git.timeout == std::chrono::milliseconds { 600000 });

    const gitman::process_request svn { gitman::make_svn_diff_request(u8"C:\\tools\\svn.exe", repository_path, u8"dir\\file.txt") };
    const std::vector<std::u8string> svn_tail { svn.arguments.end() - 3, svn.arguments.end() };
    REQUIRE(svn_tail == std::vector<std::u8string> { u8"diff", u8"--", u8"dir\\file.txt" });
    REQUIRE(svn.arguments.front() == u8"--non-interactive");
}

TEST_CASE("Untracked file reads become added lines with binary and directory guards", "[infrastructure][local-changes]")
{
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_file(u8"C:\\작업 공간\\repo\\새 파일.txt", u8"첫 줄\r\n둘째 줄\n마지막");
    probe.add_file(u8"C:\\작업 공간\\repo\\binary.bin", std::u8string_view { u8"PK\0\3", 4 });
    probe.add_directory(u8"C:\\작업 공간\\repo\\new-dir");

    const gitman::file_diff_result text { gitman::read_untracked_file_diff(probe, repository_path, u8"새 파일.txt") };
    REQUIRE(text.lines == std::vector<std::u8string> { u8"+첫 줄", u8"+둘째 줄", u8"+마지막" });
    REQUIRE_FALSE(text.binary);
    REQUIRE_FALSE(text.truncated);

    const gitman::file_diff_result binary { gitman::read_untracked_file_diff(probe, repository_path, u8"binary.bin") };
    REQUIRE(binary.binary);
    REQUIRE(binary.lines.empty());

    // Git status가 접어 보고한 미추적 디렉터리(`/` 접미)도 같은 경로 계산으로 판정한다.
    const gitman::file_diff_result directory { gitman::read_untracked_file_diff(probe, repository_path, u8"new-dir/") };
    REQUIRE(directory.directory);

    const gitman::file_diff_result missing { gitman::read_untracked_file_diff(probe, repository_path, u8"없는 파일.txt") };
    REQUIRE(missing.diagnostics.empty() == false);
}

TEST_CASE("Diff line output stops at the display limit", "[infrastructure][local-changes]")
{
    gitman::file_diff_result result {};
    std::vector<std::u8string> lines {};
    // 한 줄 약 1 KiB × 300줄 = 상한(256 KiB) 초과.
    for (int index = 0; index < 300; ++index)
        lines.push_back(std::u8string(1024, u8'a'));
    gitman::append_diff_lines_limited(result, std::move(lines));
    REQUIRE(result.truncated);
    REQUIRE(result.lines.size() < 300u);
    REQUIRE(result.lines.size() > 200u);
}

TEST_CASE("The git provider lists local changes and fetches a tracked diff", "[infrastructure][local-changes][provider]")
{
    gitman::testing::fake_process_runner runner {};
    runner.push_response({ gitman::process_completion::exited, 0, join_lines({ u8"# branch.oid abc", u8"# branch.head main", u8"1 .M N... 100644 100644 100644 a a b.txt", u8"? new.txt" }), {} });
    gitman::testing::fake_vcs_file_probe probe {};
    probe.add_directory(repository_path);
    gitman::git_repository_provider provider { available_git_tool(), runner, probe };

    const gitman::local_changes_result listed { provider.query_local_changes(make_project(), {}) };
    REQUIRE(listed.diagnostics.empty());
    REQUIRE(listed.entries.size() == 2u);
    REQUIRE(listed.entries[0].path == u8"b.txt");

    // 추적 파일 diff는 `git diff HEAD --no-color -- <path>`를 실행한다.
    runner.push_response({ gitman::process_completion::exited, 0, join_lines({ u8"diff --git a/b.txt b/b.txt", u8"@@ -1 +1 @@", u8"-old", u8"+new" }), {} });
    const gitman::file_diff_result diff { provider.query_file_diff(make_project(), listed.entries[0], {}) };
    REQUIRE(diff.diagnostics.empty());
    REQUIRE(diff.lines.size() == 4u);
    REQUIRE(diff.lines[2] == u8"-old");
    const gitman::process_request& request { runner.requests().back() };
    REQUIRE(request.arguments[request.arguments.size() - 5] == u8"diff");
    REQUIRE(request.arguments.back() == u8"b.txt");

    // 미추적 항목은 프로세스를 만들지 않고 probe로 내용을 읽는다.
    const std::size_t before { runner.request_count() };
    probe.add_file(u8"C:\\작업 공간\\repo\\new.txt", u8"내용");
    const gitman::file_diff_result untracked { provider.query_file_diff(make_project(), listed.entries[1], {}) };
    REQUIRE(runner.request_count() == before);
    REQUIRE(untracked.lines == std::vector<std::u8string> { u8"+내용" });
}

TEST_CASE("Opening the dialog queries the list and auto selects the first entry", "[logic][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& list_request { fixture.submitter.requests[0] };
    REQUIRE(list_request.kind == gitman::operation_kind::query_local_changes);
    REQUIRE(list_request.project.id.value == u8"alpha");

    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->local_changes_dialog.has_value());
        REQUIRE(view->local_changes_dialog->loading);
        REQUIRE(view->local_changes_dialog->title == u8"alpha");
    }

    // 목록이 오면 첫 항목이 선택되고 diff 조회가 이어진다.
    std::vector<gitman::local_change_entry> entries {
        { gitman::local_change_kind::modified, u8"a.txt" },
        { gitman::local_change_kind::untracked, u8"new-dir/" },
    };
    fixture.controller.handle(make_list_event(list_request.operation_id, std::move(entries)));

    REQUIRE(fixture.submitter.requests.size() == 2u);
    const gitman::operation_request& diff_request { fixture.submitter.requests[1] };
    REQUIRE(diff_request.kind == gitman::operation_kind::query_file_diff);
    REQUIRE(diff_request.diff_target.has_value());
    REQUIRE(diff_request.diff_target->path == u8"a.txt");

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->local_changes_dialog->loading == false);
    REQUIRE(view->local_changes_dialog->rows.size() == 2u);
    REQUIRE(view->local_changes_dialog->rows[0].badge == u8"수정");
    REQUIRE(view->local_changes_dialog->rows[0].selected);
    // 미추적 디렉터리는 배지에 표기가 붙는다.
    REQUIRE(view->local_changes_dialog->rows[1].badge == u8"미추적 (디렉터리)");
    REQUIRE(view->local_changes_dialog->diff_loading);
}

TEST_CASE("Diff events fill the pane and selection switches entries", "[logic][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, { { gitman::local_change_kind::modified, u8"a.txt" }, { gitman::local_change_kind::untracked, u8"b.txt" } }));

    gitman::file_diff_event diff {};
    diff.operation_id = fixture.submitter.requests[1].operation_id;
    diff.id.value = u8"alpha";
    diff.result.lines = { u8"+new" };
    diff.result.truncated = true;
    fixture.controller.handle(std::move(diff));

    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->local_changes_dialog->diff_loading == false);
        REQUIRE(view->local_changes_dialog->diff_rows.size() == 1u);
        REQUIRE(view->local_changes_dialog->diff_rows[0].right == u8"new");
        REQUIRE(view->local_changes_dialog->diff_rows[0].has_left == false);
        REQUIRE(view->local_changes_dialog->diff_notice.empty() == false);
    }

    // 다른 행을 고르면 그 항목의 diff 조회가 새로 제출된다. 같은 행은 무시한다.
    fixture.controller.handle(gitman::select_local_change_intent { 1 });
    REQUIRE(fixture.submitter.requests.size() == 3u);
    REQUIRE(fixture.submitter.requests[2].diff_target->path == u8"b.txt");
    fixture.controller.handle(gitman::select_local_change_intent { 1 });
    REQUIRE(fixture.submitter.requests.size() == 3u);

    // 늦게 도착한 이전 diff 결과는 버린다.
    gitman::file_diff_event stale {};
    stale.operation_id = fixture.submitter.requests[1].operation_id;
    stale.result.lines = { u8"-old" };
    fixture.controller.handle(std::move(stale));
    REQUIRE(fixture.controller.make_view_snapshot()->local_changes_dialog->diff_loading);

    // 닫으면 이후 event가 무시된다.
    fixture.controller.handle(gitman::cancel_local_changes_dialog_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->local_changes_dialog.has_value() == false);
}

TEST_CASE("An empty change list shows a notice instead of a diff query", "[logic][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, {}));

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->local_changes_dialog->rows.empty());
    REQUIRE(view->local_changes_dialog->message == u8"표시할 로컬 변경이 없습니다.");
}

TEST_CASE("Diff line classification colors added, removed, and headings", "[presentation][local-changes]")
{
    REQUIRE(gitman::classify_diff_line(u8"+new line") == gitman::diff_line_class::added);
    REQUIRE(gitman::classify_diff_line(u8"-old line") == gitman::diff_line_class::removed);
    REQUIRE(gitman::classify_diff_line(u8" context") == gitman::diff_line_class::context);
    REQUIRE(gitman::classify_diff_line(u8"@@ -1,2 +1,2 @@") == gitman::diff_line_class::heading);
    REQUIRE(gitman::classify_diff_line(u8"diff --git a/x b/x") == gitman::diff_line_class::heading);
    // `+++`/`---` 파일 헤더는 추가·삭제 줄로 칠하지 않는다.
    REQUIRE(gitman::classify_diff_line(u8"+++ b/x.txt") == gitman::diff_line_class::heading);
    REQUIRE(gitman::classify_diff_line(u8"--- a/x.txt") == gitman::diff_line_class::heading);
    REQUIRE(gitman::classify_diff_line(u8"Index: x.txt") == gitman::diff_line_class::heading);
}

TEST_CASE("The dialog tree exposes rows, the diff pane, and close paths", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, { { gitman::local_change_kind::modified, u8"a.txt" }, { gitman::local_change_kind::untracked, u8"b.txt" } }));

    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::local_changes_dialog }) != nullptr);
    REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::local_changes_diff }) != nullptr);

    // 행 클릭은 선택 intent다.
    const gitman::ui::ui_element* const row { tree->find(gitman::ui::local_changes_item_id(1)) };
    REQUIRE(row != nullptr);
    const auto actions { (*row->action(gitman::ui::ui_trigger::left_click))({}) };
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::select_local_change_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->index == 1u);

    gitman::ui::interaction_controller interaction {};
    interaction.set_tree(tree);

    // 휠은 diff pane 위에서 diff를, 그 밖에서 목록을 스크롤한다.
    const gitman::ui::rect_f diff { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::local_changes_diff })->bounds() };
    const auto diff_scroll { interaction.process(gitman::ui::mouse_wheel_event { diff.x + 5.0f, diff.y + 5.0f, -120.0f }) };
    REQUIRE(std::holds_alternative<gitman::local_changes_diff_scroll_intent>(*std::get_if<gitman::logic_message>(&diff_scroll.front())));
    const auto list_scroll { interaction.process(gitman::ui::mouse_wheel_event { diff.x + 5.0f, diff.y - 60.0f, -120.0f }) };
    REQUIRE(std::holds_alternative<gitman::local_changes_scroll_intent>(*std::get_if<gitman::logic_message>(&list_scroll.front())));

    // Esc는 dialog 닫기다.
    const auto escape { interaction.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape }) };
    REQUIRE(std::holds_alternative<gitman::cancel_local_changes_dialog_intent>(*std::get_if<gitman::logic_message>(&escape.front())));
}

TEST_CASE("A card double click opens the local changes dialog", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    gitman::ui::interaction_controller interaction { gitman::ui::interaction_config { 500ms, 4.0f, 6.0f } };
    interaction.set_tree(tree);

    const gitman::ui::rect_f card { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::card_body, gitman::project_id { u8"alpha" } })->bounds() };
    const auto at = [](const int milliseconds) { return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds }; };

    // 첫 클릭은 선택이고, 임계 시간 안의 두 번째 클릭이 dialog를 연다.
    static_cast<void>(interaction.process(gitman::ui::pointer_pressed_event { card.x + 5.0f, card.y + 5.0f, gitman::ui::pointer_button::left, at(0) }));
    const auto first { interaction.process(gitman::ui::pointer_released_event { card.x + 5.0f, card.y + 5.0f, gitman::ui::pointer_button::left, at(10) }) };
    REQUIRE(std::holds_alternative<gitman::select_card_intent>(*std::get_if<gitman::logic_message>(&first.front())));

    static_cast<void>(interaction.process(gitman::ui::pointer_pressed_event { card.x + 5.0f, card.y + 5.0f, gitman::ui::pointer_button::left, at(100) }));
    const auto second { interaction.process(gitman::ui::pointer_released_event { card.x + 5.0f, card.y + 5.0f, gitman::ui::pointer_button::left, at(110) }) };
    const auto* const open { std::get_if<gitman::open_local_changes_intent>(std::get_if<gitman::logic_message>(&second.front())) };
    REQUIRE(open != nullptr);
    REQUIRE(open->id.value == u8"alpha");
}

TEST_CASE("Unified diff lines become paired two way rows", "[presentation][local-changes][diff]")
{
    const std::vector<std::u8string> unified {
        u8"diff --git a/x b/x",
        u8"@@ -1,3 +1,3 @@",
        u8" context",
        u8"-old-1",
        u8"-old-2",
        u8"+new-1",
        u8" tail",
        u8"+extra",
    };
    const std::vector<gitman::two_way_diff_row> rows { gitman::build_two_way_diff(unified) };

    REQUIRE(rows.size() == 7u);
    REQUIRE(rows[0].heading);
    REQUIRE(rows[1].heading);
    // 문맥 줄은 접두 공백을 떼고 양쪽에 같은 값이다.
    REQUIRE(rows[2].left == u8"context");
    REQUIRE(rows[2].right == u8"context");
    REQUIRE(rows[2].changed == false);
    // `-` 묶음과 `+` 묶음이 순서대로 짝지어지고 남는 쪽은 빈 칸이다.
    REQUIRE(rows[3].changed);
    REQUIRE(rows[3].left == u8"old-1");
    REQUIRE(rows[3].right == u8"new-1");
    REQUIRE(rows[4].left == u8"old-2");
    REQUIRE(rows[4].has_right == false);
    REQUIRE(rows[5].left == u8"tail");
    // 문맥 뒤의 추가 줄은 오른쪽만 있다.
    REQUIRE(rows[6].changed);
    REQUIRE(rows[6].has_left == false);
    REQUIRE(rows[6].right == u8"extra");
}

TEST_CASE("Rows carry untracked flags, absolute paths, and open buttons", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id,
        { { gitman::local_change_kind::modified, u8"src/a.txt" }, { gitman::local_change_kind::untracked, u8"new-dir/" } }));

    const auto view { fixture.controller.make_view_snapshot() };
    // 미추적·디렉터리 표시와 외부 열기용 절대 경로가 행에 실린다.
    REQUIRE(view->local_changes_dialog->rows[0].untracked == false);
    // 상대 경로의 `/` 구분자는 Windows 구분자로 통일된다 (explorer /select 요구).
    REQUIRE(view->local_changes_dialog->rows[0].absolute_path == u8"C:\\work\\alpha\\src\\a.txt");
    REQUIRE(view->local_changes_dialog->rows[1].untracked);
    REQUIRE(view->local_changes_dialog->rows[1].directory);
    REQUIRE(view->local_changes_dialog->rows[1].absolute_path == u8"C:\\work\\alpha\\new-dir");

    const auto tree { gitman::ui::build_ui_tree(*view) };

    // 행 오른쪽의 아이콘이 외부 열기 요청을 만든다.
    gitman::ui::ui_element_id vscode_id { gitman::ui::ui_element_kind::local_changes_open_vscode };
    vscode_id.owner.value = gitman::ui::local_changes_item_id(0).owner.value;
    const gitman::ui::ui_element* const vscode { tree->find(vscode_id) };
    REQUIRE(vscode != nullptr);
    const auto vscode_actions { (*vscode->action(gitman::ui::ui_trigger::left_click))({}) };
    const auto* const vscode_request { std::get_if<gitman::ui::open_external_request>(&vscode_actions.front()) };
    REQUIRE(vscode_request != nullptr);
    REQUIRE(vscode_request->target == gitman::ui::external_open_target::vscode);
    REQUIRE(vscode_request->absolute_path == u8"C:\\work\\alpha\\src\\a.txt");

    gitman::ui::ui_element_id explorer_id { gitman::ui::ui_element_kind::local_changes_open_explorer };
    explorer_id.owner.value = gitman::ui::local_changes_item_id(1).owner.value;
    const gitman::ui::ui_element* const explorer { tree->find(explorer_id) };
    REQUIRE(explorer != nullptr);
    const auto explorer_actions { (*explorer->action(gitman::ui::ui_trigger::left_click))({}) };
    const auto* const explorer_request { std::get_if<gitman::ui::open_external_request>(&explorer_actions.front()) };
    REQUIRE(explorer_request != nullptr);
    REQUIRE(explorer_request->target == gitman::ui::external_open_target::explorer);
}

TEST_CASE("Scrollbars appear only when the list or diff overflows", "[ui][local-changes]")
{
    dialog_fixture fixture {};
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });

    // 6행(132px)까지는 목록 막대가 없고, 그보다 많으면 나타난다.
    std::vector<gitman::local_change_entry> few {};
    for (int index = 0; index < 3; ++index)
        few.push_back({ gitman::local_change_kind::modified, u8"a.txt" });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[0].operation_id, few));
    {
        const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
        REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::local_changes_list_scrollbar })->visible() == false);
        REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::local_changes_diff_scrollbar })->visible() == false);
    }

    // 목록·diff가 넘치면 각각의 막대가 보인다.
    gitman::file_diff_event diff {};
    diff.operation_id = fixture.submitter.requests[1].operation_id;
    diff.id.value = u8"alpha";
    for (int index = 0; index < 40; ++index)
        diff.result.lines.push_back(u8"+line");
    fixture.controller.handle(std::move(diff));

    fixture.controller.handle(gitman::cancel_local_changes_dialog_intent {});
    fixture.controller.handle(gitman::open_local_changes_intent { gitman::project_id { u8"alpha" } });
    std::vector<gitman::local_change_entry> many {};
    for (int index = 0; index < 20; ++index)
        many.push_back({ gitman::local_change_kind::modified, u8"a.txt" });
    fixture.controller.handle(make_list_event(fixture.submitter.requests[2].operation_id, many));
    gitman::file_diff_event long_diff {};
    long_diff.operation_id = fixture.submitter.requests[3].operation_id;
    long_diff.id.value = u8"alpha";
    for (int index = 0; index < 40; ++index)
        long_diff.result.lines.push_back(u8"+line");
    fixture.controller.handle(std::move(long_diff));

    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::local_changes_list_scrollbar })->visible());
    REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::local_changes_diff_scrollbar })->visible());
}
