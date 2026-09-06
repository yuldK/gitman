#include "domain/local_changes.h"
#include "helpers/vcs_test_doubles.h"
#include "infrastructure/git_command_builder.h"
#include "infrastructure/git_repository_provider.h"
#include "infrastructure/git_status_parser.h"
#include "infrastructure/local_change_reader.h"
#include "infrastructure/svn_command_builder.h"
#include "infrastructure/svn_output_parser.h"
#include "presentation/diff_presentation.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

// 로컬 변경의 파서·명령 조립·provider·diff 표시 모델이다. UI 계층을 들이지 않아
// luil 어휘와 무관하다 (luil 이식 C2-4에서 `local_changes_tests.cpp`에서 나뉘었다).
namespace {
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
