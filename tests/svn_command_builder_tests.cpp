#include "application/process_request.h"
#include "infrastructure/svn_command_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <string_view>
#include <vector>

namespace {
    constexpr std::u8string_view svn_executable { u8"C:\\Program Files\\TortoiseSVN\\bin\\svn.exe" };
    constexpr std::u8string_view svnversion_executable { u8"C:\\Program Files\\TortoiseSVN\\bin\\svnversion.exe" };
    constexpr std::u8string_view working_directory { u8"D:\\작업 공간\\작업 복사본" };
} // namespace

TEST_CASE("SVN info requests ask for one machine readable value", "[infrastructure][svn][command]")
{
    const gitman::process_request request { gitman::make_svn_info_item_request(svn_executable, working_directory, gitman::svn_info_item::working_copy_root) };

    // 사람이 읽는 `key: value` 목록을 파싱하지 않으므로 로캘과 무관하다.
    const std::vector<std::u8string> expected { u8"--non-interactive", u8"info", u8"--show-item", u8"wc-root" };
    REQUIRE(request.arguments == expected);
    REQUIRE(request.executable == svn_executable);
    REQUIRE(request.working_directory == working_directory);
    REQUIRE(*request.timeout == std::chrono::milliseconds { 30000 });
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("SVN info item names match the documented values", "[infrastructure][svn][command]")
{
    REQUIRE(gitman::svn_info_item_name(gitman::svn_info_item::url) == u8"url");
    REQUIRE(gitman::svn_info_item_name(gitman::svn_info_item::relative_url) == u8"relative-url");
    REQUIRE(gitman::svn_info_item_name(gitman::svn_info_item::repository_root) == u8"repos-root-url");
    REQUIRE(gitman::svn_info_item_name(gitman::svn_info_item::repository_uuid) == u8"repos-uuid");
    REQUIRE(gitman::svn_info_item_name(gitman::svn_info_item::revision) == u8"revision");
    REQUIRE(gitman::svn_info_item_name(gitman::svn_info_item::working_copy_root) == u8"wc-root");
}

TEST_CASE("SVN status requests stay non verbose", "[infrastructure][svn][command]")
{
    const gitman::process_request request { gitman::make_svn_status_request(svn_executable, working_directory) };

    // `--verbose`는 리비전과 작성자 컬럼을 끼워 넣어 작성자 이름에 공백이 있으면 경로
    // 경계가 흔들린다. 대상을 주지 않아 작업 디렉터리 기준 상대 경로가 나온다.
    const std::vector<std::u8string> expected { u8"--non-interactive", u8"status" };
    REQUIRE(request.arguments == expected);
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("svnversion requests carry no shared arguments", "[infrastructure][svn][command]")
{
    const gitman::process_request request { gitman::make_svnversion_request(svnversion_executable, working_directory) };

    // `svnversion`은 `svn`과 다른 실행 파일이며 `--non-interactive`를 받지 않는다.
    REQUIRE(request.arguments.empty());
    REQUIRE(request.executable == svnversion_executable);
    REQUIRE(request.working_directory == working_directory);
    // 나머지 실행 정책은 다른 명령과 같다.
    REQUIRE(*request.timeout == std::chrono::milliseconds { 30000 });
    REQUIRE(request.maximum_captured_bytes_per_stream == 8u * 1024u * 1024u);
    REQUIRE(request.text_encoding == gitman::process_text_encoding::active_code_page_fallback);
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("SVN remote revision requests target the URL", "[infrastructure][svn][command]")
{
    const gitman::process_request request { gitman::make_svn_remote_revision_request(svn_executable, working_directory, u8"https://svn.example.com/repo/trunk") };

    const std::vector<std::u8string> expected { u8"--non-interactive", u8"info", u8"--show-item", u8"revision", u8"https://svn.example.com/repo/trunk" };
    REQUIRE(request.arguments == expected);
    // 네트워크를 쓰는 유일한 조회다.
    REQUIRE(*request.timeout == std::chrono::milliseconds { 120000 });
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("SVN requests never enable interactive prompts", "[infrastructure][svn][command]")
{
    const gitman::process_request requests[] {
        gitman::make_svn_info_item_request(svn_executable, working_directory, gitman::svn_info_item::url),
        gitman::make_svn_status_request(svn_executable, working_directory),
        gitman::make_svn_remote_revision_request(svn_executable, working_directory, u8"https://svn.example.com/repo"),
    };

    for (const gitman::process_request& request : requests)
    {
        REQUIRE(request.arguments.front() == u8"--non-interactive");
        for (const std::u8string& argument : request.arguments)
        {
            // 인증서 오류는 실패로 처리한다. 신뢰를 자동으로 허용하지 않는다.
            REQUIRE(argument.starts_with(u8"--trust-server-cert") == false);
            REQUIRE(argument != u8"--username");
            REQUIRE(argument != u8"--password");
        }
        // 로캘은 강제하지 않는다.
        REQUIRE(request.environment_overrides.empty());
    }
}
