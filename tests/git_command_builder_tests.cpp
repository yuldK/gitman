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

TEST_CASE("Git local queries carry the approved execution policy", "[infrastructure][git][command]")
{
    const gitman::process_request requests[] {
        gitman::make_git_layout_request(git_executable, working_directory),
        gitman::make_git_status_request(git_executable, working_directory),
    };

    for (const gitman::process_request& request : requests)
    {
        REQUIRE(gitman::validate_process_request(request).empty());
        REQUIRE(request.timeout.has_value());
        REQUIRE(*request.timeout == std::chrono::milliseconds { 30000 });
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
