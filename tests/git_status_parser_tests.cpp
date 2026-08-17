#include "domain/repository_snapshot.h"
#include "infrastructure/git_status_parser.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // fixture는 호스트의 Git 2.52.0이 실제로 낸 출력을 그대로 저장한 것이다. 파서가
    // 실제 형식에서 벗어나지 않도록 고정한다.
    std::vector<std::u8string> read_fixture_lines(const char* const name)
    {
        const std::filesystem::path path { std::filesystem::path { GITMAN_VCS_FIXTURE_DIRECTORY } / "git" / name };
        std::ifstream stream { path, std::ios::binary };
        REQUIRE(stream.is_open());

        const std::string content { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
        REQUIRE(content.empty() == false);

        // 단계 3의 파이프라인과 같은 규칙으로 나눈다. 줄 끝 문자는 레코드에 남지 않고
        // 마지막 빈 조각은 레코드가 되지 않는다.
        std::vector<std::u8string> lines {};
        std::size_t begin { 0 };
        while (begin <= content.size())
        {
            const std::size_t end { content.find('\n', begin) };
            const bool final_segment { end == std::string::npos };
            std::string_view line { final_segment ? std::string_view { content }.substr(begin) : std::string_view { content }.substr(begin, end - begin) };
            if (line.empty() == false && line.back() == '\r')
                line.remove_suffix(1);

            if (final_segment)
            {
                if (line.empty() == false)
                    lines.emplace_back(reinterpret_cast<const char8_t*>(line.data()), line.size());
                break;
            }
            lines.emplace_back(reinterpret_cast<const char8_t*>(line.data()), line.size());
            begin = end + 1;
        }
        return lines;
    }

    std::vector<std::u8string> lines_of(const std::vector<std::u8string_view>& values)
    {
        std::vector<std::u8string> lines {};
        lines.reserve(values.size());
        for (const std::u8string_view value : values)
            lines.emplace_back(value);
        return lines;
    }
} // namespace

TEST_CASE("Quoted Git paths are unescaped", "[infrastructure][git][parser]")
{
    REQUIRE(gitman::unquote_git_path(u8"a.txt") == u8"a.txt");
    REQUIRE(gitman::unquote_git_path(u8"sub dir/a b.txt") == u8"sub dir/a b.txt");
    // `core.quotepath=false`에서 비ASCII는 인용되지 않는다.
    REQUIRE(gitman::unquote_git_path(u8"한글 이름 😀.txt") == u8"한글 이름 😀.txt");

    REQUIRE(gitman::unquote_git_path(u8"\"a b.txt\"") == u8"a b.txt");
    REQUIRE(gitman::unquote_git_path(u8"\"a\\nb.txt\"") == u8"a\nb.txt");
    REQUIRE(gitman::unquote_git_path(u8"\"a\\tb.txt\"") == u8"a\tb.txt");
    REQUIRE(gitman::unquote_git_path(u8"\"a\\rb.txt\"") == u8"a\rb.txt");
    REQUIRE(gitman::unquote_git_path(u8"\"a\\\"b\"") == u8"a\"b");
    REQUIRE(gitman::unquote_git_path(u8"\"a\\\\b\"") == u8"a\\b");
    // 8진 이스케이프는 byte 하나다. UTF-8 sequence는 여러 개가 이어져 복원된다.
    REQUIRE(gitman::unquote_git_path(u8"\"\\355\\225\\234.txt\"") == u8"한.txt");
    REQUIRE(gitman::unquote_git_path(u8"\"\\a\\b\\f\\v\"") == u8"\a\b\f\v");

    // 형식이 깨진 값은 추측하지 않고 원문을 남긴다.
    REQUIRE(gitman::unquote_git_path(u8"\"unterminated") == u8"\"unterminated");
    REQUIRE(gitman::unquote_git_path(u8"\"a\\\"") == u8"a\\");
    REQUIRE(gitman::unquote_git_path(u8"\"\"") == u8"");
    REQUIRE(gitman::unquote_git_path(u8"") == u8"");
}

TEST_CASE("Repository layout output is parsed in argument order", "[infrastructure][git][parser]")
{
    const gitman::git_repository_layout layout {
        gitman::parse_git_repository_layout(lines_of({ u8"C:/repo/.git", u8"false", u8"true", u8"C:/repo" })),
    };
    REQUIRE(layout.parsed);
    REQUIRE(layout.git_directory == u8"C:/repo/.git");
    REQUIRE(layout.work_tree_root == u8"C:/repo");
    REQUIRE_FALSE(layout.bare);
    REQUIRE(layout.inside_work_tree);
}

TEST_CASE("Bare repository layout keeps the values printed before the failure", "[infrastructure][git][parser]")
{
    // `--show-toplevel`이 실패해 세 줄만 남는다. 앞의 값만으로 배치를 판정한다.
    const gitman::git_repository_layout bare {
        gitman::parse_git_repository_layout(lines_of({ u8"C:/bare", u8"true", u8"false" })),
    };
    REQUIRE(bare.parsed);
    REQUIRE(bare.bare);
    REQUIRE_FALSE(bare.inside_work_tree);
    REQUIRE(bare.work_tree_root.empty());

    const gitman::git_repository_layout inside_git_dir {
        gitman::parse_git_repository_layout(lines_of({ u8"C:/repo/.git", u8"false", u8"false" })),
    };
    REQUIRE(inside_git_dir.parsed);
    REQUIRE_FALSE(inside_git_dir.bare);
    REQUIRE_FALSE(inside_git_dir.inside_work_tree);
}

TEST_CASE("Incomplete layout output is not accepted", "[infrastructure][git][parser]")
{
    REQUIRE_FALSE(gitman::parse_git_repository_layout({}).parsed);
    REQUIRE_FALSE(gitman::parse_git_repository_layout(lines_of({ u8"C:/repo/.git" })).parsed);
    REQUIRE_FALSE(gitman::parse_git_repository_layout(lines_of({ u8"C:/repo/.git", u8"false" })).parsed);
    REQUIRE_FALSE(gitman::parse_git_repository_layout(lines_of({ u8"", u8"false", u8"true" })).parsed);
}

TEST_CASE("Captured clean status is parsed", "[infrastructure][git][parser][fixture]")
{
    const gitman::git_status_summary status { gitman::parse_git_status_porcelain_v2(read_fixture_lines("status-clean.txt")) };

    REQUIRE(status.has_branch_header);
    REQUIRE(status.oid == u8"809c24e6087a2e27bc87e929f3b53f460d5c7520");
    REQUIRE(status.head == u8"main");
    REQUIRE(status.upstream == u8"origin/main");
    REQUIRE(status.has_ahead_behind);
    REQUIRE(status.ahead == 0);
    REQUIRE(status.behind == 0);
    REQUIRE(status.entries.empty());
    REQUIRE(status.unparsable_records == 0);

    const gitman::working_tree_summary summary { gitman::summarize_git_working_tree(status) };
    REQUIRE(summary.state == gitman::working_tree_state::clean);
    REQUIRE(summary.is_safe_for_change());
}

TEST_CASE("Captured dirty status with a rename is parsed", "[infrastructure][git][parser][fixture]")
{
    const gitman::git_status_summary status { gitman::parse_git_status_porcelain_v2(read_fixture_lines("status-dirty-rename.txt")) };

    REQUIRE(status.has_ahead_behind);
    REQUIRE(status.ahead == 1);
    REQUIRE(status.behind == 0);
    REQUIRE(status.entries.size() == 4);
    REQUIRE(status.unparsable_records == 0);

    REQUIRE(status.entries[0].kind == gitman::git_status_entry_kind::ordinary);
    REQUIRE(status.entries[0].index_state == u8'.');
    REQUIRE(status.entries[0].work_tree_state == u8'M');
    REQUIRE(status.entries[0].path == u8"a.txt");

    // rename 레코드는 `<새 경로><TAB><원래 경로>`다. 새 경로에 공백, 한글과 emoji가 있다.
    REQUIRE(status.entries[1].kind == gitman::git_status_entry_kind::renamed_or_copied);
    REQUIRE(status.entries[1].index_state == u8'R');
    REQUIRE(status.entries[1].path == u8"한글 이름 😀.txt");
    REQUIRE(status.entries[1].original_path == u8"renamed.txt");

    // `--untracked-files=normal`은 미추적 디렉터리를 항목 하나로 접어서 보고한다.
    REQUIRE(status.entries[2].kind == gitman::git_status_entry_kind::untracked);
    REQUIRE(status.entries[2].path == u8"sub dir/");
    REQUIRE(status.entries[3].path == u8"새 파일.txt");

    const gitman::working_tree_summary summary { gitman::summarize_git_working_tree(status) };
    REQUIRE(summary.state == gitman::working_tree_state::modified);
    REQUIRE(summary.modified_count == 2);
    REQUIRE(summary.untracked_count == 2);
    REQUIRE(summary.conflicted_count == 0);
    REQUIRE_FALSE(summary.is_safe_for_change());
}

TEST_CASE("Captured conflicted status is parsed", "[infrastructure][git][parser][fixture]")
{
    const gitman::git_status_summary status { gitman::parse_git_status_porcelain_v2(read_fixture_lines("status-conflicted.txt")) };

    REQUIRE(status.entries.size() == 1);
    REQUIRE(status.entries[0].kind == gitman::git_status_entry_kind::unmerged);
    REQUIRE(status.entries[0].index_state == u8'U');
    REQUIRE(status.entries[0].work_tree_state == u8'U');
    REQUIRE(status.entries[0].path == u8"f.txt");
    // 충돌 저장소에는 upstream 헤더가 없다.
    REQUIRE(status.upstream.empty());
    REQUIRE_FALSE(status.has_ahead_behind);

    const gitman::working_tree_summary summary { gitman::summarize_git_working_tree(status) };
    REQUIRE(summary.state == gitman::working_tree_state::conflicted);
    REQUIRE(summary.conflicted_count == 1);
    REQUIRE(summary.modified_count == 0);
}

TEST_CASE("Captured detached and unborn status are parsed", "[infrastructure][git][parser][fixture]")
{
    const gitman::git_status_summary detached { gitman::parse_git_status_porcelain_v2(read_fixture_lines("status-detached.txt")) };
    REQUIRE(detached.detached);
    REQUIRE(detached.head == u8"(detached)");
    REQUIRE(detached.oid == u8"9177764286a3e810b8b2bc731efb21c5d2242e07");
    REQUIRE(gitman::summarize_git_working_tree(detached).is_detached);

    const gitman::git_status_summary unborn { gitman::parse_git_status_porcelain_v2(read_fixture_lines("status-unborn.txt")) };
    REQUIRE(unborn.unborn);
    // 커밋이 없으므로 표시할 리비전이 없다. `(initial)`을 값으로 담지 않는다.
    REQUIRE(unborn.oid.empty());
    REQUIRE(unborn.head == u8"main");
    REQUIRE_FALSE(unborn.detached);
}

TEST_CASE("Status records with spaces and quoting keep their boundaries", "[infrastructure][git][parser]")
{
    const gitman::git_status_summary status {
        gitman::parse_git_status_porcelain_v2(lines_of({
            u8"# branch.oid abc",
            u8"# branch.head main",
            u8"1 .M N... 100644 100644 100644 5626abf 5626abf sub dir/a b.txt",
            u8"2 C. N... 100644 100644 100644 587be6b 587be6b C75 새 이름.txt\t원래 이름.txt",
            u8"u DU N... 100644 100644 100644 100644 df967b9 ba2906d e45c9c2 conflict dir/f.txt",
            u8"? \"quoted\\nname.txt\"",
            u8"! ignored.txt",
        })),
    };

    REQUIRE(status.unparsable_records == 0);
    REQUIRE(status.entries.size() == 5);
    REQUIRE(status.entries[0].path == u8"sub dir/a b.txt");
    REQUIRE(status.entries[1].kind == gitman::git_status_entry_kind::renamed_or_copied);
    REQUIRE(status.entries[1].path == u8"새 이름.txt");
    REQUIRE(status.entries[1].original_path == u8"원래 이름.txt");
    REQUIRE(status.entries[2].path == u8"conflict dir/f.txt");
    // 줄 단위 출력에서도 개행이 든 경로가 인용 덕분에 한 레코드로 남는다.
    REQUIRE(status.entries[3].path == u8"quoted\nname.txt");
    REQUIRE(status.entries[4].kind == gitman::git_status_entry_kind::ignored);

    const gitman::working_tree_summary summary { gitman::summarize_git_working_tree(status) };
    // 무시된 항목은 어느 수에도 들어가지 않는다.
    REQUIRE(summary.modified_count == 2);
    REQUIRE(summary.conflicted_count == 1);
    REQUIRE(summary.untracked_count == 1);
}

TEST_CASE("Unchanged tracked entries are not counted", "[infrastructure][git][parser]")
{
    const gitman::git_status_summary status {
        gitman::parse_git_status_porcelain_v2(lines_of({
            u8"# branch.oid abc",
            u8"# branch.head main",
            u8"1 .. N... 100644 100644 100644 5626abf 5626abf unchanged.txt",
        })),
    };

    REQUIRE(status.entries.size() == 1);
    const gitman::working_tree_summary summary { gitman::summarize_git_working_tree(status) };
    REQUIRE(summary.modified_count == 0);
    REQUIRE(summary.state == gitman::working_tree_state::clean);
}

TEST_CASE("Untracked only working trees are reported as modified", "[infrastructure][git][parser]")
{
    const gitman::git_status_summary status {
        gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.oid abc", u8"# branch.head main", u8"? a.txt" })),
    };

    const gitman::working_tree_summary summary { gitman::summarize_git_working_tree(status) };
    // 카드가 둘을 구분해 표시할 수 있도록 개수는 따로 남긴다.
    REQUIRE(summary.state == gitman::working_tree_state::modified);
    REQUIRE(summary.modified_count == 0);
    REQUIRE(summary.untracked_count == 1);
    REQUIRE_FALSE(summary.is_safe_for_change());
}

TEST_CASE("Unknown branch headers do not break parsing", "[infrastructure][git][parser]")
{
    const gitman::git_status_summary status {
        gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.oid abc", u8"# branch.head main", u8"# stash 3", u8"# branch.future value" })),
    };

    // Git이 헤더를 추가해도 기존 판정이 깨지지 않아야 한다.
    REQUIRE(status.unparsable_records == 0);
    REQUIRE(status.head == u8"main");
    REQUIRE(gitman::summarize_git_working_tree(status).state == gitman::working_tree_state::clean);
}

TEST_CASE("Malformed ahead behind headers are ignored", "[infrastructure][git][parser]")
{
    REQUIRE_FALSE(gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.ab 2 -3" })).has_ahead_behind);
    REQUIRE_FALSE(gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.ab +2" })).has_ahead_behind);
    REQUIRE_FALSE(gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.ab +x -3" })).has_ahead_behind);
    REQUIRE_FALSE(gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.ab +2 +3" })).has_ahead_behind);

    const gitman::git_status_summary valid { gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.ab +12 -34" })) };
    REQUIRE(valid.has_ahead_behind);
    REQUIRE(valid.ahead == 12);
    REQUIRE(valid.behind == 34);
}

TEST_CASE("Unreadable status output leaves the working tree unknown", "[infrastructure][git][parser]")
{
    const gitman::git_status_summary unknown_record {
        gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.oid abc", u8"# branch.head main", u8"3 unknown record", u8"? a.txt" })),
    };
    REQUIRE(unknown_record.unparsable_records == 1);
    REQUIRE(unknown_record.entries.size() == 1);

    // 출력을 다 읽지 못한 저장소를 깨끗하다고 보고하면 보호 정책이 무력해진다.
    const gitman::working_tree_summary summary { gitman::summarize_git_working_tree(unknown_record) };
    REQUIRE(summary.state == gitman::working_tree_state::unknown);
    REQUIRE(summary.untracked_count == 1);
    REQUIRE_FALSE(summary.is_safe_for_change());

    const gitman::git_status_summary short_record {
        gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.oid abc", u8"# branch.head main", u8"1 .M N... 100644 a.txt" })),
    };
    REQUIRE(short_record.unparsable_records == 1);

    const gitman::git_status_summary missing_path {
        gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.oid abc", u8"# branch.head main", u8"? " })),
    };
    REQUIRE(missing_path.unparsable_records == 1);

    const gitman::git_status_summary bad_states {
        gitman::parse_git_status_porcelain_v2(lines_of({ u8"# branch.oid abc", u8"# branch.head main", u8"1 M N... 100644 100644 100644 5626abf 5626abf a.txt" })),
    };
    REQUIRE(bad_states.unparsable_records == 1);
}

TEST_CASE("Remote names are read one per line", "[infrastructure][git][parser]")
{
    const std::vector<std::u8string> names { gitman::parse_git_remote_names(lines_of({ u8"origin", u8"", u8"  upstream  ", u8"fork" })) };

    REQUIRE(names.size() == 3);
    REQUIRE(names[0] == u8"origin");
    REQUIRE(names[1] == u8"upstream");
    REQUIRE(names[2] == u8"fork");
    REQUIRE(gitman::parse_git_remote_names({}).empty());
    REQUIRE(gitman::parse_git_remote_names(lines_of({ u8"", u8"   " })).empty());
}

TEST_CASE("Submodule status markers are turned into meanings", "[infrastructure][git][parser]")
{
    const std::vector<gitman::submodule_status> submodules {
        gitman::parse_git_submodule_status(lines_of({
            u8" fa95e2a150ae50dd8e2094ccc71e9ea3c80e5be1 모듈 하나 (heads/main)",
            u8"+fdfdc34b44d8cb3770512055430fdb6b77a285e2 vendor/lib (v1.2-3-gabcdef0)",
            u8"-0000000000000000000000000000000000000000 아직 없는 모듈",
            u8"U1111111111111111111111111111111111111111 conflicted/module (heads/main)",
        })),
    };

    REQUIRE(submodules.size() == 4);
    // 공백이 든 경로를 자르지 않고 표시용 describe 접미사만 떼어 낸다.
    REQUIRE(submodules[0].relative_path == u8"모듈 하나");
    REQUIRE(submodules[0].revision == u8"fa95e2a150ae50dd8e2094ccc71e9ea3c80e5be1");
    REQUIRE(submodules[0].initialized);
    REQUIRE_FALSE(submodules[0].revision_mismatch);
    REQUIRE_FALSE(submodules[0].conflicted);

    REQUIRE(submodules[1].revision_mismatch);
    REQUIRE(submodules[1].relative_path == u8"vendor/lib");
    // describe가 없는 줄도 그대로 읽는다.
    REQUIRE_FALSE(submodules[2].initialized);
    REQUIRE(submodules[2].relative_path == u8"아직 없는 모듈");
    REQUIRE(submodules[3].conflicted);

    REQUIRE(gitman::parse_git_submodule_status({}).empty());
    REQUIRE(gitman::parse_git_submodule_status(lines_of({ u8"짧음", u8"" })).empty());
}

TEST_CASE("Ahead behind counts are read from one line", "[infrastructure][git][parser]")
{
    const gitman::git_ahead_behind counts { gitman::parse_git_ahead_behind(u8"3\t5") };
    REQUIRE(counts.parsed);
    // 왼쪽이 로컬, 오른쪽이 원격이다.
    REQUIRE(counts.ahead == 3);
    REQUIRE(counts.behind == 5);

    REQUIRE(gitman::parse_git_ahead_behind(u8"0\t0").parsed);
    REQUIRE(gitman::parse_git_ahead_behind(u8" 12\t34 ").ahead == 12);
    REQUIRE(gitman::parse_git_ahead_behind(u8"2 7").behind == 7);

    // 형식이 다르면 추측하지 않는다.
    REQUIRE_FALSE(gitman::parse_git_ahead_behind(u8"").parsed);
    REQUIRE_FALSE(gitman::parse_git_ahead_behind(u8"3").parsed);
    REQUIRE_FALSE(gitman::parse_git_ahead_behind(u8"a\tb").parsed);
    REQUIRE_FALSE(gitman::parse_git_ahead_behind(u8"-1\t2").parsed);
    REQUIRE_FALSE(gitman::parse_git_ahead_behind(u8"1\t").parsed);
}

TEST_CASE("Status output without branch headers is not trusted", "[infrastructure][git][parser]")
{
    const gitman::git_status_summary status { gitman::parse_git_status_porcelain_v2(lines_of({ u8"? a.txt" })) };

    REQUIRE_FALSE(status.has_branch_header);
    REQUIRE(status.unparsable_records == 0);
    REQUIRE(gitman::summarize_git_working_tree(status).state == gitman::working_tree_state::unknown);

    const gitman::git_status_summary empty { gitman::parse_git_status_porcelain_v2({}) };
    REQUIRE(empty.entries.empty());
    REQUIRE(gitman::summarize_git_working_tree(empty).state == gitman::working_tree_state::unknown);
}

TEST_CASE("Reference lines are split on tabs", "[infrastructure][git][parser]")
{
    const std::vector<std::u8string> lines {
        u8"refs/heads/main\t0123456789abcdef0123456789abcdef01234567\trefs/remotes/origin/main\t*\t",
        u8"refs/heads/작업\tfedcba9876543210fedcba9876543210fedcba98\t\t \t",
        u8"refs/remotes/origin/HEAD\t0123456789abcdef0123456789abcdef01234567\t\t \trefs/remotes/origin/main",
        u8"refs/remotes/origin/main\t0123456789abcdef0123456789abcdef01234567\t\t \t",
    };
    const std::vector<gitman::git_reference_entry> references { gitman::parse_git_reference_list(lines) };

    REQUIRE(references.size() == 4);
    REQUIRE(references[0].name == u8"refs/heads/main");
    REQUIRE(references[0].object_id == u8"0123456789abcdef0123456789abcdef01234567");
    REQUIRE(references[0].upstream == u8"refs/remotes/origin/main");
    // `%(HEAD)`는 현재 branch에서만 `*`이고 나머지는 공백 한 칸이다.
    REQUIRE(references[0].head);
    REQUIRE_FALSE(references[0].symbolic());

    REQUIRE(references[1].name == u8"refs/heads/작업");
    REQUIRE(references[1].upstream.empty());
    REQUIRE_FALSE(references[1].head);

    // 심볼릭 항목은 이름이 아니라 값으로 판정한다.
    REQUIRE(references[2].symbolic());
    REQUIRE(references[2].symbolic_target == u8"refs/remotes/origin/main");
    REQUIRE_FALSE(references[3].symbolic());
}

TEST_CASE("Reference lines that are not references are dropped", "[infrastructure][git][parser]")
{
    const std::vector<std::u8string> lines {
        u8"쓰레기 줄",
        u8"",
        u8"HEAD\tabc\t\t*\t",
        u8"refs/heads/only\tabc",
    };
    const std::vector<gitman::git_reference_entry> references { gitman::parse_git_reference_list(lines) };

    // `refs/`로 시작하지 않는 줄은 버린다. 칸이 모자란 줄은 읽을 수 있는 값만 채운다.
    REQUIRE(references.size() == 1);
    REQUIRE(references[0].name == u8"refs/heads/only");
    REQUIRE(references[0].object_id == u8"abc");
    REQUIRE(references[0].upstream.empty());
    REQUIRE_FALSE(references[0].head);
    REQUIRE_FALSE(references[0].symbolic());
}

TEST_CASE("Worktree output yields only checked out branches", "[infrastructure][git][parser]")
{
    const std::vector<std::u8string> lines {
        u8"worktree C:/작업 공간/repo",
        u8"HEAD 0123456789abcdef0123456789abcdef01234567",
        u8"branch refs/heads/main",
        u8"",
        u8"worktree C:/작업 공간/detached",
        u8"HEAD 0123456789abcdef0123456789abcdef01234567",
        u8"detached",
        u8"",
        u8"worktree C:/작업 공간/linked",
        u8"HEAD fedcba9876543210fedcba9876543210fedcba98",
        u8"branch refs/heads/feature/a",
        u8"",
        u8"worktree C:/작업 공간/bare",
        u8"bare",
    };
    const std::vector<std::u8string> branches { gitman::parse_git_worktree_branches(lines) };

    // detached worktree와 bare 항목에는 `branch` 줄이 없다.
    REQUIRE(branches.size() == 2);
    REQUIRE(branches[0] == u8"main");
    // `/`가 든 branch 이름도 그대로 남는다.
    REQUIRE(branches[1] == u8"feature/a");

    REQUIRE(gitman::parse_git_worktree_branches({}).empty());
    const std::vector<std::u8string> incomplete { u8"branch " };
    REQUIRE(gitman::parse_git_worktree_branches(incomplete).empty());
}
