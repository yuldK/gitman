#include "application/process_request.h"
#include "domain/repository_snapshot.h"
#include "infrastructure/vcs_execution_policy.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {
    const gitman::process_environment_override* find_override(const gitman::process_request& request, const std::u8string_view name) noexcept
    {
        for (const gitman::process_environment_override& entry : request.environment_overrides)
            if (entry.name == name)
                return &entry;
        return nullptr;
    }

    bool has_argument_pair(const gitman::process_request& request, const std::u8string_view first, const std::u8string_view second)
    {
        for (std::size_t index = 0; index + 1 < request.arguments.size(); ++index)
            if (request.arguments[index] == first && request.arguments[index + 1] == second)
                return true;
        return false;
    }

    gitman::process_request make_git_request(const gitman::vcs_command_class command_class, std::vector<std::u8string> arguments)
    {
        return gitman::make_vcs_process_request(gitman::repository_kind::git, u8"C:\\tools\\git.exe", u8"D:\\작업 공간\\repo", std::move(arguments), command_class);
    }
} // namespace

TEST_CASE("Command limits match the approved per class values", "[infrastructure][vcs][policy]")
{
    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::tool_probe).timeout == std::chrono::milliseconds { 5000 });
    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::local_query).timeout == std::chrono::milliseconds { 30000 });
    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::remote_query).timeout == std::chrono::milliseconds { 120000 });
    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::update).timeout == std::chrono::milliseconds { 600000 });
    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::switch_target).timeout == std::chrono::milliseconds { 300000 });

    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::tool_probe).maximum_captured_bytes_per_stream == 64u * 1024u);
    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::local_query).maximum_captured_bytes_per_stream == 8u * 1024u * 1024u);
    // update는 submodule 출력까지 담아야 하므로 상한이 더 크다.
    REQUIRE(gitman::vcs_limits_for(gitman::vcs_command_class::update).maximum_captured_bytes_per_stream == 32u * 1024u * 1024u);

    // 알 수 없는 값도 사용 가능한 한도를 돌려주어 요청 검증을 통과시킨다.
    REQUIRE(gitman::vcs_limits_for(static_cast<gitman::vcs_command_class>(-1)).maximum_captured_bytes_per_stream > 0);
}

TEST_CASE("Git requests carry the non interactive environment", "[infrastructure][vcs][policy]")
{
    const gitman::process_request request { make_git_request(gitman::vcs_command_class::remote_query, { u8"fetch", u8"--prune", u8"origin" }) };

    const auto* const terminal_prompt { find_override(request, u8"GIT_TERMINAL_PROMPT") };
    REQUIRE(terminal_prompt != nullptr);
    REQUIRE(terminal_prompt->value.has_value());
    REQUIRE(*terminal_prompt->value == u8"0");

    const auto* const credential_manager { find_override(request, u8"GCM_INTERACTIVE") };
    REQUIRE(credential_manager != nullptr);
    REQUIRE(credential_manager->value.has_value());
    REQUIRE(*credential_manager->value == u8"Never");

    const auto* const ssh { find_override(request, u8"GIT_SSH_COMMAND") };
    REQUIRE(ssh != nullptr);
    REQUIRE(ssh->value.has_value());
    // BatchMode는 SSH 암호와 호스트 키 확인 프롬프트 대신 즉시 실패하게 만든다.
    REQUIRE(ssh->value->find(u8"BatchMode=yes") != std::u8string::npos);

    const auto* const optional_locks { find_override(request, u8"GIT_OPTIONAL_LOCKS") };
    REQUIRE(optional_locks != nullptr);
    REQUIRE(optional_locks->value.has_value());
    REQUIRE(*optional_locks->value == u8"0");

    // askpass GUI 경로를 세 변수 모두 삭제해서 막는다.
    for (const std::u8string_view name : { std::u8string_view { u8"GIT_ASKPASS" }, std::u8string_view { u8"SSH_ASKPASS" }, std::u8string_view { u8"DISPLAY" } })
    {
        const auto* const entry { find_override(request, name) };
        REQUIRE(entry != nullptr);
        REQUIRE_FALSE(entry->value.has_value());
    }
}

TEST_CASE("The locale is deliberately left to the system", "[infrastructure][vcs][policy]")
{
    // 사용자 결정에 따라 시스템 언어 메시지를 그대로 보여 준다. 오류 분류는
    // `vcs_error_classifier`가 로캘 독립 신호로만 수행한다.
    const gitman::process_request git { make_git_request(gitman::vcs_command_class::local_query, { u8"status" }) };
    REQUIRE(find_override(git, u8"LC_ALL") == nullptr);
    REQUIRE(find_override(git, u8"LANG") == nullptr);
    REQUIRE(find_override(git, u8"LANGUAGE") == nullptr);

    const gitman::process_request svn {
        gitman::make_vcs_process_request(gitman::repository_kind::subversion, u8"C:\\tools\\svn.exe", u8"D:\\repo", { u8"info" }, gitman::vcs_command_class::local_query),
    };
    REQUIRE(find_override(svn, u8"LC_ALL") == nullptr);
    REQUIRE(svn.environment_overrides.empty());
}

TEST_CASE("Every VCS request decodes with the active code page fallback", "[infrastructure][vcs][policy]")
{
    // 로캘을 강제하지 않으므로 두 도구 모두 시스템 언어 메시지를 낼 수 있다. 유효한
    // UTF-8 레코드는 변환하지 않으므로 기계 판독 출력에는 영향이 없다.
    REQUIRE(make_git_request(gitman::vcs_command_class::local_query, { u8"status" }).text_encoding == gitman::process_text_encoding::active_code_page_fallback);
    REQUIRE(gitman::make_vcs_process_request(gitman::repository_kind::subversion, u8"C:\\tools\\svn.exe", u8"D:\\repo", { u8"info" }, gitman::vcs_command_class::local_query).text_encoding
        == gitman::process_text_encoding::active_code_page_fallback);
}

TEST_CASE("Git common arguments precede the command and disable background work", "[infrastructure][vcs][policy]")
{
    const gitman::process_request request { make_git_request(gitman::vcs_command_class::local_query, { u8"status", u8"--porcelain=v2" }) };

    const auto common { gitman::git_common_arguments() };
    REQUIRE(request.arguments.size() == common.size() + 2);
    for (std::size_t index = 0; index < common.size(); ++index)
        REQUIRE(request.arguments[index] == common[index]);
    REQUIRE(request.arguments[common.size()] == u8"status");
    REQUIRE(request.arguments.back() == u8"--porcelain=v2");

    // 비ASCII 경로를 8진 이스케이프 없이 UTF-8 원문으로 받는다.
    REQUIRE(has_argument_pair(request, u8"-c", u8"core.quotepath=false"));
    // background `gc`가 출력 pipe를 상속한 채 남는 경로를 원천 차단한다.
    REQUIRE(has_argument_pair(request, u8"-c", u8"gc.auto=0"));
    REQUIRE(has_argument_pair(request, u8"-c", u8"color.ui=false"));
    REQUIRE(std::ranges::find(request.arguments, std::u8string { u8"--no-pager" }) != request.arguments.end());
}

TEST_CASE("Subversion requests are always non interactive", "[infrastructure][vcs][policy]")
{
    const gitman::process_request request {
        gitman::make_vcs_process_request(gitman::repository_kind::subversion, u8"C:\\tools\\svn.exe", u8"D:\\repo", { u8"info", u8"--show-item", u8"url" }, gitman::vcs_command_class::local_query),
    };

    REQUIRE(request.arguments.size() == 4);
    REQUIRE(request.arguments[0] == u8"--non-interactive");
    REQUIRE(request.arguments[1] == u8"info");
    REQUIRE(request.arguments.back() == u8"url");
    // 인증서 오류를 무시하는 option은 넣지 않는다.
    REQUIRE(std::ranges::find(request.arguments, std::u8string { u8"--trust-server-cert" }) == request.arguments.end());
}

TEST_CASE("Generated requests satisfy the stage three validation contract", "[infrastructure][vcs][policy]")
{
    // 단계 4가 만드는 요청이 단계 3의 계약을 깨지 않는지 부류마다 확인한다.
    constexpr gitman::vcs_command_class classes[] {
        gitman::vcs_command_class::tool_probe,
        gitman::vcs_command_class::local_query,
        gitman::vcs_command_class::remote_query,
        gitman::vcs_command_class::update,
        gitman::vcs_command_class::switch_target,
    };
    for (const gitman::vcs_command_class command_class : classes)
    {
        REQUIRE(gitman::validate_process_request(make_git_request(command_class, { u8"status" })).empty());
        REQUIRE(gitman::validate_process_request(gitman::make_vcs_process_request(gitman::repository_kind::subversion, u8"C:\\tools\\svn.exe", u8"D:\\작업 공간\\repo", { u8"info" }, command_class))
                .empty());
    }
}

TEST_CASE("Environment and argument policies differ per repository kind", "[infrastructure][vcs][policy]")
{
    REQUIRE(gitman::vcs_common_arguments(gitman::repository_kind::git) == gitman::git_common_arguments());
    REQUIRE(gitman::vcs_common_arguments(gitman::repository_kind::subversion) == gitman::svn_common_arguments());
    REQUIRE(gitman::vcs_environment_overrides(gitman::repository_kind::subversion).empty());
    REQUIRE(gitman::vcs_environment_overrides(gitman::repository_kind::git).size() == gitman::git_environment_overrides().size());
    // 알 수 없는 종류는 Git 정책을 쓴다.
    REQUIRE(gitman::vcs_common_arguments(gitman::repository_kind::unknown) == gitman::git_common_arguments());
}
