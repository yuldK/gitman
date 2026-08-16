#include "application/process_request.h"
#include "infrastructure/git_command_builder.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace {
    constexpr std::u8string_view git_executable { u8"C:\\Program Files\\Git\\cmd\\git.exe" };
    constexpr std::u8string_view working_directory { u8"D:\\작업 공간\\repo 😀" };
    // 공통 인자는 `-c` 쌍 3개와 `--no-pager`다. 명령 인자는 그 뒤에서 시작한다.
    constexpr std::size_t command_argument_offset { 7 };

    const gitman::process_environment_override* find_override(const gitman::process_request& request, const std::u8string_view name) noexcept
    {
        for (const gitman::process_environment_override& entry : request.environment_overrides)
            if (entry.name == name)
                return &entry;
        return nullptr;
    }

    std::vector<std::u8string> command_arguments(const gitman::process_request& request)
    {
        REQUIRE(request.arguments.size() >= command_argument_offset);
        return { request.arguments.begin() + command_argument_offset, request.arguments.end() };
    }
} // namespace

TEST_CASE("Git layout requests keep the failing argument last", "[infrastructure][git][command]")
{
    const gitman::process_request request { gitman::make_git_layout_request(git_executable, working_directory) };

    // `--show-toplevel`은 bare 저장소에서 실패한다. 마지막에 두어야 앞의 값이 출력에
    // 남고 그 값으로 배치를 판정할 수 있다.
    const std::vector<std::u8string> expected {
        u8"rev-parse",
        u8"--absolute-git-dir",
        u8"--is-bare-repository",
        u8"--is-inside-work-tree",
        u8"--show-toplevel",
    };
    REQUIRE(command_arguments(request) == expected);
    REQUIRE(request.executable == git_executable);
    REQUIRE(request.working_directory == working_directory);
}

TEST_CASE("Git status requests use line oriented porcelain v2", "[infrastructure][git][command]")
{
    const gitman::process_request request { gitman::make_git_status_request(git_executable, working_directory) };

    // `-z`를 쓰지 않는다. 단계 3 파이프라인이 줄 끝 문자를 남기지 않아 NUL 구분 출력은
    // 경계 정보를 잃고, 개행이 든 경로가 오히려 손상된다.
    const std::vector<std::u8string> expected {
        u8"status",
        u8"--porcelain=v2",
        u8"--branch",
        u8"--untracked-files=normal",
        u8"--ignored=no",
    };
    REQUIRE(command_arguments(request) == expected);
    for (const std::u8string& argument : request.arguments)
        REQUIRE(argument != u8"-z");
}

TEST_CASE("Git status requests raise the record limit", "[infrastructure][git][command]")
{
    const gitman::process_request status { gitman::make_git_status_request(git_executable, working_directory) };
    const gitman::process_request layout { gitman::make_git_layout_request(git_executable, working_directory) };

    // rename 레코드는 한 줄에 경로 두 개를 담는다. 기본 상한에서 줄이 끊기면 파서가
    // 다른 레코드로 오해하므로 이 명령에만 상한을 올린다.
    REQUIRE(status.maximum_record_bytes == gitman::git_status_record_byte_limit);
    REQUIRE(status.maximum_record_bytes > gitman::default_process_record_byte_limit);
    REQUIRE(layout.maximum_record_bytes == gitman::default_process_record_byte_limit);
}

TEST_CASE("Remote enumeration does not ask for URLs", "[infrastructure][git][command]")
{
    const gitman::process_request request { gitman::make_git_remote_list_request(git_executable, working_directory) };

    // `-v`를 쓰지 않는다. URL이 필요하지 않고, URL에 자격 증명이 들어 있으면 로그로
    // 흘러나갈 수 있다.
    REQUIRE(command_arguments(request) == std::vector<std::u8string> { u8"remote" });
    REQUIRE(*request.timeout == std::chrono::milliseconds { 30000 });
}

TEST_CASE("Fetch requests prune and separate the remote name", "[infrastructure][git][command]")
{
    const gitman::process_request request { gitman::make_git_fetch_request(git_executable, working_directory, u8"origin") };

    // remote 이름은 저장소 설정에서 오므로 `-`로 시작해도 인자로 해석되지 않아야 한다.
    const std::vector<std::u8string> expected { u8"fetch", u8"--prune", u8"--", u8"origin" };
    REQUIRE(command_arguments(request) == expected);
    // 원격을 실제로 확인하는 유일한 조회 명령이라 한도가 다르다.
    REQUIRE(*request.timeout == std::chrono::milliseconds { 120000 });
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Reference checks avoid the path separator of rev-parse", "[infrastructure][git][command]")
{
    const gitman::process_request request { gitman::make_git_verify_reference_request(git_executable, working_directory, u8"refs/remotes/origin/main") };

    // `rev-parse`의 `--`는 뒤의 값을 경로로 만들어 확인이 항상 실패한다. 대신 완전한
    // ref만 넘긴다.
    const std::vector<std::u8string> expected { u8"rev-parse", u8"--verify", u8"--quiet", u8"refs/remotes/origin/main" };
    REQUIRE(command_arguments(request) == expected);
    REQUIRE(*request.timeout == std::chrono::milliseconds { 30000 });
}

TEST_CASE("Ahead behind requests compare HEAD with the tracking reference", "[infrastructure][git][command]")
{
    const gitman::process_request request { gitman::make_git_ahead_behind_request(git_executable, working_directory, u8"refs/remotes/origin/feature/a b") };

    // `HEAD`를 쓰면 branch 이름을 인자로 넘기지 않아도 되어 이름에 특수 문자가 있어도
    // 안전하다.
    const std::vector<std::u8string> expected { u8"rev-list", u8"--left-right", u8"--count", u8"HEAD...refs/remotes/origin/feature/a b" };
    REQUIRE(command_arguments(request) == expected);
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Pull requests only fast forward", "[infrastructure][git][command]")
{
    const gitman::process_request request { gitman::make_git_pull_request(git_executable, working_directory, u8"origin", u8"feature/새 기능", gitman::git_submodule_recursion::none) };

    // fast-forward가 불가능하면 merge를 만들지 않고 실패한다. remote와 branch를 명시해
    // 설정에 따라 다른 대상이 당겨지지 않게 한다.
    const std::vector<std::u8string> expected { u8"pull", u8"--ff-only", u8"--recurse-submodules=no", u8"--", u8"origin", u8"feature/새 기능" };
    REQUIRE(command_arguments(request) == expected);
    for (const std::u8string& argument : request.arguments)
    {
        REQUIRE(argument != u8"--force");
        REQUIRE(argument != u8"-f");
        REQUIRE(argument != u8"--rebase");
        REQUIRE(argument != u8"--autostash");
        REQUIRE(argument != u8"--no-ff");
    }

    // 변경 명령은 한도가 다르다.
    REQUIRE(*request.timeout == std::chrono::milliseconds { 600000 });
    REQUIRE(request.maximum_captured_bytes_per_stream == 32u * 1024u * 1024u);
    REQUIRE(gitman::validate_process_request(request).empty());
}

TEST_CASE("Pull requests carry the submodule choice", "[infrastructure][git][command]")
{
    const gitman::process_request off { gitman::make_git_pull_request(git_executable, working_directory, u8"origin", u8"main", gitman::git_submodule_recursion::none) };
    const gitman::process_request on { gitman::make_git_pull_request(git_executable, working_directory, u8"origin", u8"main", gitman::git_submodule_recursion::on_demand) };

    REQUIRE(command_arguments(off)[2] == u8"--recurse-submodules=no");
    REQUIRE(command_arguments(on)[2] == u8"--recurse-submodules=on-demand");
}

TEST_CASE("Submodule requests survey before they change anything", "[infrastructure][git][command]")
{
    const gitman::process_request survey { gitman::make_git_submodule_status_request(git_executable, working_directory) };
    REQUIRE(command_arguments(survey) == std::vector<std::u8string> { u8"submodule", u8"status", u8"--recursive" });
    // 조사는 네트워크를 쓰지 않는 로컬 조회다.
    REQUIRE(*survey.timeout == std::chrono::milliseconds { 30000 });

    const gitman::process_request update { gitman::make_git_submodule_update_request(git_executable, working_directory) };
    REQUIRE(command_arguments(update) == std::vector<std::u8string> { u8"submodule", u8"update", u8"--init", u8"--recursive" });
    REQUIRE(*update.timeout == std::chrono::milliseconds { 600000 });
    REQUIRE(gitman::validate_process_request(update).empty());
}

TEST_CASE("Git local queries carry the approved execution policy", "[infrastructure][git][command]")
{
    const gitman::process_request requests[] {
        gitman::make_git_layout_request(git_executable, working_directory),
        gitman::make_git_status_request(git_executable, working_directory),
        gitman::make_git_remote_list_request(git_executable, working_directory),
        gitman::make_git_fetch_request(git_executable, working_directory, u8"origin"),
        gitman::make_git_verify_reference_request(git_executable, working_directory, u8"refs/remotes/origin/main"),
        gitman::make_git_ahead_behind_request(git_executable, working_directory, u8"refs/remotes/origin/main"),
    };

    for (const gitman::process_request& request : requests)
    {
        REQUIRE(gitman::validate_process_request(request).empty());
        REQUIRE(request.timeout.has_value());
        REQUIRE(request.maximum_captured_bytes_per_stream == 8u * 1024u * 1024u);
        REQUIRE(request.text_encoding == gitman::process_text_encoding::active_code_page_fallback);

        REQUIRE(request.arguments[0] == u8"-c");
        REQUIRE(request.arguments[1] == u8"core.quotepath=false");
        REQUIRE(request.arguments[2] == u8"-c");
        REQUIRE(request.arguments[3] == u8"gc.auto=0");
        REQUIRE(request.arguments[4] == u8"-c");
        REQUIRE(request.arguments[5] == u8"color.ui=false");
        REQUIRE(request.arguments[6] == u8"--no-pager");

        const auto* const terminal_prompt { find_override(request, u8"GIT_TERMINAL_PROMPT") };
        REQUIRE(terminal_prompt != nullptr);
        REQUIRE(terminal_prompt->value.has_value());
        REQUIRE(*terminal_prompt->value == u8"0");

        const auto* const askpass { find_override(request, u8"GIT_ASKPASS") };
        REQUIRE(askpass != nullptr);
        REQUIRE_FALSE(askpass->value.has_value());

        // 로캘은 강제하지 않는다. 시스템 언어 메시지를 그대로 보여 주는 결정이다.
        REQUIRE(find_override(request, u8"LC_ALL") == nullptr);
        REQUIRE(find_override(request, u8"LANG") == nullptr);
    }
}
