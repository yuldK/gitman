#include "domain/repository_snapshot.h"
#include "infrastructure/vcs_error_classifier.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

namespace {
    gitman::vcs_command_result make_failure(const std::u8string_view standard_error, const std::int32_t exit_code = 128)
    {
        gitman::vcs_command_result result {};
        result.process.completion = gitman::process_completion::exited;
        result.process.exit_code = exit_code;
        result.standard_error_lines.push_back(std::u8string { standard_error });
        return result;
    }

    // 같은 실패의 영어 출력과 한국어 출력이 같은 분류를 내야 한다. 로캘을 강제하지
    // 않기로 한 결정이 분류를 흔들지 않는다는 것을 이 도우미로 고정한다.
    void require_same_classification(const gitman::repository_kind kind, const std::u8string_view english, const std::u8string_view korean, const gitman::vcs_failure_kind expected)
    {
        REQUIRE(gitman::classify_vcs_error_text(kind, english) == expected);
        REQUIRE(gitman::classify_vcs_error_text(kind, korean) == expected);
    }
} // namespace

TEST_CASE("SSH authentication failures are recognised in any language", "[infrastructure][vcs][classifier]")
{
    // OpenSSH가 만드는 문자열이라 Git의 번역 catalog와 무관하다.
    require_same_classification(gitman::repository_kind::git, u8"git@github.com: Permission denied (publickey).\nfatal: Could not read from remote repository.",
        u8"git@github.com: Permission denied (publickey).\n치명적: 리모트 저장소에서 읽을 수 없습니다.", gitman::vcs_failure_kind::authentication_required);

    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"Host key verification failed.") == gitman::vcs_failure_kind::authentication_required);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"Permission denied, please try again.") == gitman::vcs_failure_kind::authentication_required);
}

TEST_CASE("HTTP status numbers survive translation and drive the classification", "[infrastructure][vcs][classifier]")
{
    require_same_classification(gitman::repository_kind::git, u8"fatal: unable to access 'https://example.com/repo.git/': The requested URL returned error: 403",
        u8"치명적: 'https://example.com/repo.git/'에 접근할 수 없습니다: 요청한 URL이 오류를 반환했습니다: 403", gitman::vcs_failure_kind::authentication_required);

    require_same_classification(gitman::repository_kind::git, u8"fatal: unable to access 'https://example.com/repo.git/': The requested URL returned error: 401",
        u8"치명적: 'https://example.com/repo.git/'에 접근할 수 없습니다: 요청한 URL이 오류를 반환했습니다: 401", gitman::vcs_failure_kind::authentication_required);

    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"fatal: unable to access 'https://example.com/x.git/': The requested URL returned error: 404")
        == gitman::vcs_failure_kind::repository_not_found);
}

TEST_CASE("Status numbers outside an HTTP context are not treated as failures", "[infrastructure][vcs][classifier]")
{
    // 오탐 방지 규칙 두 가지를 함께 고정한다. HTTP 문맥이 있어야 하고 앞뒤가 숫자가
    // 아닌 독립 토큰이어야 한다.
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"error: pathspec 'issue-403' did not match any file(s)") == gitman::vcs_failure_kind::command_failed);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"fatal: branch 404 already exists") == gitman::vcs_failure_kind::command_failed);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"fatal: unable to access 'https://example.com/x/': returned error: 4031") == gitman::vcs_failure_kind::command_failed);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"fatal: unable to access 'https://example.com/x/': returned error: 1401") == gitman::vcs_failure_kind::command_failed);
}

TEST_CASE("libcurl network failures are recognised in any language", "[infrastructure][vcs][classifier]")
{
    // libcurl에는 번역 catalog가 없어 Git이 문장을 번역해도 이 부분은 영어로 남는다.
    require_same_classification(gitman::repository_kind::git, u8"fatal: unable to access 'https://example.com/repo.git/': Could not resolve host: example.com",
        u8"치명적: 'https://example.com/repo.git/'에 접근할 수 없습니다: Could not resolve host: example.com", gitman::vcs_failure_kind::offline);

    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"Failed to connect to example.com port 443 after 21 ms") == gitman::vcs_failure_kind::offline);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"Connection timed out after 30001 milliseconds") == gitman::vcs_failure_kind::offline);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"Connection refused") == gitman::vcs_failure_kind::offline);
}

TEST_CASE("Server supplied Git messages are classified without translation", "[infrastructure][vcs][classifier]")
{
    // 서버가 그대로 돌려주는 문장이라 Git이 번역하지 않는다.
    require_same_classification(gitman::repository_kind::git, u8"remote: Repository not found.\nfatal: repository 'https://example.com/x.git/' not found",
        u8"remote: Repository not found.\n치명적: 'https://example.com/x.git/' 저장소를 찾을 수 없습니다", gitman::vcs_failure_kind::repository_not_found);
}

TEST_CASE("Subversion error codes drive the classification in any language", "[infrastructure][vcs][classifier]")
{
    // SVN은 번역된 메시지에도 `E<숫자>` 코드를 그대로 붙인다.
    require_same_classification(gitman::repository_kind::subversion, u8"svn: E170013: Unable to connect to a repository at URL 'https://example.com/svn'\nsvn: E215004: No more credentials",
        u8"svn: E170013: 'https://example.com/svn' URL의 저장소에 연결할 수 없습니다\nsvn: E215004: 자격 증명이 더 이상 없습니다", gitman::vcs_failure_kind::authentication_required);

    require_same_classification(gitman::repository_kind::subversion, u8"svn: E170013: Unable to connect to a repository at URL 'https://example.com/svn'",
        u8"svn: E170013: 'https://example.com/svn' URL의 저장소에 연결할 수 없습니다", gitman::vcs_failure_kind::offline);

    require_same_classification(gitman::repository_kind::subversion, u8"svn: E155007: 'D:\\work' is not a working copy", u8"svn: E155007: 'D:\\work'은(는) 작업 사본이 아닙니다",
        gitman::vcs_failure_kind::repository_not_found);

    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::subversion, u8"svn: E170001: Authorization failed") == gitman::vcs_failure_kind::authentication_required);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::subversion, u8"svn: E175002: Unexpected HTTP status") == gitman::vcs_failure_kind::offline);
}

TEST_CASE("Unrecognised failures are reported as such instead of being guessed", "[infrastructure][vcs][classifier]")
{
    // 잘못된 `offline` 판정보다 미분류가 안전하다.
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"fatal: 알 수 없는 이유로 실패했습니다") == gitman::vcs_failure_kind::command_failed);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::subversion, u8"svn: E200030: SQLite 오류") == gitman::vcs_failure_kind::command_failed);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"") == gitman::vcs_failure_kind::command_failed);
}

TEST_CASE("English only fallbacks still work when no locale independent signal exists", "[infrastructure][vcs][classifier]")
{
    // 번역 대상이라 1차 근거로 쓰지 않지만, 영어 환경에서는 유용한 보조 신호다.
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"fatal: could not read Username for 'https://x': terminal prompts disabled")
        == gitman::vcs_failure_kind::authentication_required);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::git, u8"fatal: not a git repository (or any of the parent directories): .git") == gitman::vcs_failure_kind::repository_not_found);
    REQUIRE(gitman::classify_vcs_error_text(gitman::repository_kind::subversion, u8"svn: Authentication failed") == gitman::vcs_failure_kind::authentication_required);
}

TEST_CASE("Process level outcomes take precedence over stderr inspection", "[infrastructure][vcs][classifier]")
{
    gitman::vcs_command_result result {};

    result.process.completion = gitman::process_completion::timed_out;
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, result) == gitman::vcs_failure_kind::timed_out);

    result.process.completion = gitman::process_completion::cancelled;
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, result) == gitman::vcs_failure_kind::cancelled);

    result.process.completion = gitman::process_completion::start_failed;
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, result) == gitman::vcs_failure_kind::execution_failed);

    result.process.completion = gitman::process_completion::internal_error;
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, result) == gitman::vcs_failure_kind::execution_failed);

    result.process.completion = gitman::process_completion::invalid_request;
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, result) == gitman::vcs_failure_kind::execution_failed);

    // timeout으로 끝난 실행에 인증 메시지가 남아 있어도 프로세스 결과가 우선이다.
    result.standard_error_lines.push_back(u8"Permission denied (publickey).");
    result.process.completion = gitman::process_completion::timed_out;
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, result) == gitman::vcs_failure_kind::timed_out);
}

TEST_CASE("Successful commands are not classified as failures", "[infrastructure][vcs][classifier]")
{
    gitman::vcs_command_result result {};
    result.process.completion = gitman::process_completion::exited;
    result.process.exit_code = 0;
    REQUIRE(result.succeeded());
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, result) == gitman::vcs_failure_kind::none);

    // 종료 코드가 0이 아니면 stderr를 본다.
    REQUIRE(gitman::classify_vcs_failure(gitman::repository_kind::git, make_failure(u8"Could not resolve host: example.com")) == gitman::vcs_failure_kind::offline);
}

TEST_CASE("Failure kinds map to sync states and diagnostic codes", "[infrastructure][vcs][classifier]")
{
    REQUIRE(gitman::remote_sync_state_for_failure(gitman::vcs_failure_kind::authentication_required) == gitman::remote_sync_state::authentication_required);
    REQUIRE(gitman::remote_sync_state_for_failure(gitman::vcs_failure_kind::offline) == gitman::remote_sync_state::offline);
    // timeout은 네트워크 문제로 보되 인증 실패와 섞지 않는다.
    REQUIRE(gitman::remote_sync_state_for_failure(gitman::vcs_failure_kind::timed_out) == gitman::remote_sync_state::offline);
    REQUIRE(gitman::remote_sync_state_for_failure(gitman::vcs_failure_kind::repository_not_found) == gitman::remote_sync_state::error);
    REQUIRE(gitman::remote_sync_state_for_failure(gitman::vcs_failure_kind::command_failed) == gitman::remote_sync_state::error);
    // 취소는 실패가 아니므로 마지막 판정을 오류로 덮지 않는다.
    REQUIRE(gitman::remote_sync_state_for_failure(gitman::vcs_failure_kind::cancelled) == gitman::remote_sync_state::unknown);
    REQUIRE(gitman::remote_sync_state_for_failure(gitman::vcs_failure_kind::none) == gitman::remote_sync_state::unknown);

    REQUIRE(gitman::diagnostic_code_for_failure(gitman::vcs_failure_kind::authentication_required) == gitman::diagnostic_code::authentication_required);
    REQUIRE(gitman::diagnostic_code_for_failure(gitman::vcs_failure_kind::offline) == gitman::diagnostic_code::remote_unreachable);
    REQUIRE(gitman::diagnostic_code_for_failure(gitman::vcs_failure_kind::repository_not_found) == gitman::diagnostic_code::repository_not_found);
    REQUIRE(gitman::diagnostic_code_for_failure(gitman::vcs_failure_kind::timed_out) == gitman::diagnostic_code::process_timed_out);
    REQUIRE(gitman::diagnostic_code_for_failure(gitman::vcs_failure_kind::cancelled) == gitman::diagnostic_code::process_cancelled);
    REQUIRE(gitman::diagnostic_code_for_failure(gitman::vcs_failure_kind::execution_failed) == gitman::diagnostic_code::process_start_failed);
    REQUIRE(gitman::diagnostic_code_for_failure(gitman::vcs_failure_kind::command_failed) == gitman::diagnostic_code::vcs_command_failed);
}

TEST_CASE("Failure kinds expose stable names and Korean messages", "[infrastructure][vcs][classifier]")
{
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::none) == u8"none");
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::authentication_required) == u8"authentication_required");
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::offline) == u8"offline");
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::repository_not_found) == u8"repository_not_found");
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::timed_out) == u8"timed_out");
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::cancelled) == u8"cancelled");
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::execution_failed) == u8"execution_failed");
    REQUIRE(gitman::vcs_failure_kind_name(gitman::vcs_failure_kind::command_failed) == u8"command_failed");
    REQUIRE(gitman::vcs_failure_kind_name(static_cast<gitman::vcs_failure_kind>(-1)) == u8"command_failed");

    REQUIRE(gitman::vcs_failure_message(gitman::vcs_failure_kind::authentication_required).empty() == false);
    REQUIRE(gitman::vcs_failure_message(gitman::vcs_failure_kind::offline).empty() == false);
    REQUIRE(gitman::vcs_failure_message(static_cast<gitman::vcs_failure_kind>(-1)).empty() == false);
}
