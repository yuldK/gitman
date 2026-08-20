#include "domain/repository_snapshot.h"
#include "infrastructure/svn_output_parser.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace {
    // SVN fixture는 실제 출력을 받을 수 없어 공식 문서의 출력 계약을 근거로 작성했다.
    // 출처를 파일 안에 남기려고 `#`로 시작하는 줄을 주석으로 쓰고 여기서 버린다. SVN은
    // 상태 줄을 `#`로 시작하지 않으므로 실제 출력과 충돌하지 않는다.
    std::vector<std::u8string> read_fixture_lines(const char* const name)
    {
        const std::filesystem::path path { std::filesystem::path { GITMAN_VCS_FIXTURE_DIRECTORY } / "svn" / name };
        std::ifstream stream { path, std::ios::binary };
        REQUIRE(stream.is_open());

        const std::string content { std::istreambuf_iterator<char> { stream }, std::istreambuf_iterator<char> {} };
        REQUIRE(content.empty() == false);

        std::vector<std::u8string> lines {};
        std::size_t begin { 0 };
        while (begin <= content.size())
        {
            const std::size_t end { content.find('\n', begin) };
            const bool final_segment { end == std::string::npos };
            std::string_view line { final_segment ? std::string_view { content }.substr(begin) : std::string_view { content }.substr(begin, end - begin) };
            if (line.empty() == false && line.back() == '\r')
                line.remove_suffix(1);

            if (line.starts_with('#') == false && (line.empty() == false || final_segment == false))
                lines.emplace_back(reinterpret_cast<const char8_t*>(line.data()), line.size());
            if (final_segment)
                break;
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

TEST_CASE("Single value SVN output keeps only the value", "[infrastructure][svn][parser]")
{
    REQUIRE(gitman::parse_svn_info_item(lines_of({ u8"^/trunk" })) == u8"^/trunk");
    REQUIRE(gitman::parse_svn_info_item(lines_of({ u8"", u8"  https://svn.example.com/repo  " })) == u8"https://svn.example.com/repo");
    // 값이 여러 줄로 오면 첫 값만 쓴다.
    REQUIRE(gitman::parse_svn_info_item(lines_of({ u8"4168", u8"군더더기" })) == u8"4168");
    REQUIRE(gitman::parse_svn_info_item({}).empty());
    REQUIRE(gitman::parse_svn_info_item(lines_of({ u8"   " })).empty());
}

TEST_CASE("Documented SVN list output keeps directories only", "[infrastructure][svn][parser][fixture]")
{
    const std::vector<std::u8string> directories { gitman::parse_svn_directory_list(read_fixture_lines("ls-repository-root.txt")) };
    REQUIRE(directories == std::vector<std::u8string> { u8"branches", u8"tags", u8"trunk", u8"공용 자료" });

    // 빈 이름과 파일은 빠지고 서버가 준 디렉터리 순서는 유지된다.
    REQUIRE(gitman::parse_svn_directory_list(lines_of({ u8"", u8"/", u8"a.txt", u8"z/", u8"a/", u8" 앞뒤 공백 /" })) == std::vector<std::u8string> { u8"z", u8"a", u8" 앞뒤 공백 " });
}

TEST_CASE("Captured SVN status states are parsed", "[infrastructure][svn][parser][fixture]")
{
    const gitman::svn_status_summary status { gitman::parse_svn_status(read_fixture_lines("status-mixed-states.txt")) };

    REQUIRE(status.unparsable_records == 0);
    REQUIRE(status.entries.size() == 12);
    REQUIRE(status.entries[0].item_state == u8'M');
    REQUIRE(status.entries[0].path == u8"trunk/source/a.txt");
    // 속성만 바뀐 항목은 2번 칸으로만 드러난다.
    REQUIRE(status.entries[1].item_state == u8' ');
    REQUIRE(status.entries[1].property_state == u8'M');
    REQUIRE(status.entries[1].path == u8"trunk/속성만 바뀐 파일.txt");
    REQUIRE(status.entries[3].path == u8"trunk/새 파일 😀.txt");

    const gitman::working_tree_summary summary { gitman::summarize_svn_working_tree(status) };
    REQUIRE(summary.state == gitman::working_tree_state::conflicted);
    REQUIRE(summary.conflicted_count == 1);
    REQUIRE(summary.untracked_count == 1);
    // `M`, ` M`, `MM`, `A`, `D`, `R`, `!`, `~` 여덟 개다. `I`와 `X`는 세지 않는다.
    REQUIRE(summary.modified_count == 8);
    REQUIRE_FALSE(summary.is_safe_for_change());
}

TEST_CASE("Captured SVN switched and tree conflict columns are parsed", "[infrastructure][svn][parser][fixture]")
{
    const gitman::svn_status_summary status { gitman::parse_svn_status(read_fixture_lines("status-switched-conflict.txt")) };

    // `> moved from` 같은 부가 설명 줄은 항목이 아니다.
    REQUIRE(status.unparsable_records == 0);
    REQUIRE(status.entries.size() == 4);
    REQUIRE(status.entries[0].switched);
    REQUIRE(status.entries[0].path == u8"trunk/전환된 폴더");
    REQUIRE(gitman::has_svn_switched_entry(status));

    const gitman::svn_status_entry& tree_conflict { status.entries.back() };
    REQUIRE(tree_conflict.tree_conflict);
    REQUIRE(tree_conflict.item_state == u8' ');
    REQUIRE(tree_conflict.path == u8"trunk/tree conflict 파일.txt");

    const gitman::working_tree_summary summary { gitman::summarize_svn_working_tree(status) };
    // tree conflict는 항목 칸이 비어 있어도 충돌이다.
    REQUIRE(summary.state == gitman::working_tree_state::conflicted);
    REQUIRE(summary.conflicted_count == 1);
}

TEST_CASE("SVN status paths keep spaces and are not cut by padding", "[infrastructure][svn][parser]")
{
    const gitman::svn_status_summary status {
        gitman::parse_svn_status(lines_of({ u8"M       trunk/a b c.txt", u8"M        trunk/여백이 더 많은 경로.txt", u8"?       trunk/미추적 폴더/" })),
    };

    REQUIRE(status.entries.size() == 3);
    REQUIRE(status.entries[0].path == u8"trunk/a b c.txt");
    // 상태 칸 뒤의 공백을 모두 건너뛰므로 패딩이 하나 더 있어도 경로가 흔들리지 않는다.
    REQUIRE(status.entries[1].path == u8"trunk/여백이 더 많은 경로.txt");
    REQUIRE(status.entries[2].path == u8"trunk/미추적 폴더/");
    REQUIRE(status.unparsable_records == 0);
}

TEST_CASE("Unreadable SVN status lines leave the working tree unknown", "[infrastructure][svn][parser]")
{
    const gitman::svn_status_summary status { gitman::parse_svn_status(lines_of({ u8"M       trunk/a.txt", u8"M          " })) };

    REQUIRE(status.entries.size() == 1);
    REQUIRE(status.unparsable_records == 1);

    // 출력을 다 읽지 못한 작업 복사본을 깨끗하다고 단정하지 않는다.
    const gitman::working_tree_summary summary { gitman::summarize_svn_working_tree(status) };
    REQUIRE(summary.state == gitman::working_tree_state::unknown);
    REQUIRE(summary.modified_count == 1);
    REQUIRE_FALSE(summary.is_safe_for_change());
}

TEST_CASE("Clean and untracked only SVN working trees are separated", "[infrastructure][svn][parser]")
{
    const gitman::working_tree_summary clean { gitman::summarize_svn_working_tree(gitman::parse_svn_status({})) };
    REQUIRE(clean.state == gitman::working_tree_state::clean);
    REQUIRE(clean.is_safe_for_change());

    const gitman::working_tree_summary untracked { gitman::summarize_svn_working_tree(gitman::parse_svn_status(lines_of({ u8"?       새 파일.txt" }))) };
    REQUIRE(untracked.state == gitman::working_tree_state::modified);
    REQUIRE(untracked.untracked_count == 1);
    REQUIRE(untracked.modified_count == 0);

    const gitman::working_tree_summary ignored { gitman::summarize_svn_working_tree(gitman::parse_svn_status(lines_of({ u8"I       무시.txt", u8"X       외부 항목" }))) };
    // 무시된 항목과 외부 항목은 사용자의 변경이 아니다.
    REQUIRE(ignored.state == gitman::working_tree_state::clean);
}

TEST_CASE("svnversion output is parsed for mixed and switched working copies", "[infrastructure][svn][parser]")
{
    const gitman::svn_version_info single { gitman::parse_svnversion(u8"4168") };
    REQUIRE(single.parsed);
    REQUIRE(single.low_revision == 4168);
    REQUIRE(single.high_revision == 4168);
    REQUIRE_FALSE(single.mixed_revision());
    REQUIRE_FALSE(single.modified);
    REQUIRE_FALSE(single.switched);

    const gitman::svn_version_info range { gitman::parse_svnversion(u8"4123:4168MS") };
    REQUIRE(range.parsed);
    REQUIRE(range.low_revision == 4123);
    REQUIRE(range.high_revision == 4168);
    REQUIRE(range.mixed_revision());
    REQUIRE(range.modified);
    REQUIRE(range.switched);
    REQUIRE_FALSE(range.partial);

    const gitman::svn_version_info partial { gitman::parse_svnversion(u8"  4168P  ") };
    REQUIRE(partial.parsed);
    REQUIRE(partial.partial);
    REQUIRE_FALSE(partial.mixed_revision());
}

TEST_CASE("svnversion output that is not a working copy is refused", "[infrastructure][svn][parser]")
{
    // 작업 복사본이 아니면 문장을 낸다. 값을 추측하지 않는다.
    REQUIRE_FALSE(gitman::parse_svnversion(u8"Unversioned directory").parsed);
    REQUIRE_FALSE(gitman::parse_svnversion(u8"exported").parsed);
    REQUIRE_FALSE(gitman::parse_svnversion(u8"").parsed);
    REQUIRE_FALSE(gitman::parse_svnversion(u8"   ").parsed);
    // 형식이 어긋나면 부분값도 남기지 않는다.
    REQUIRE_FALSE(gitman::parse_svnversion(u8"4123:").parsed);
    REQUIRE_FALSE(gitman::parse_svnversion(u8"4123:abc").parsed);
    REQUIRE_FALSE(gitman::parse_svnversion(u8"4168Z").parsed);
    REQUIRE_FALSE(gitman::parse_svnversion(u8":4168").parsed);

    const gitman::svn_version_info refused { gitman::parse_svnversion(u8"4123:") };
    REQUIRE(refused.low_revision == 0);
    REQUIRE(refused.high_revision == 0);
}
