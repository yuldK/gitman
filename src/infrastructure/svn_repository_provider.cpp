#include "infrastructure/svn_repository_provider.h"

#include "application/vcs_tool_registry.h"
#include "domain/path_syntax.h"
#include "infrastructure/local_change_reader.h"
#include "infrastructure/svn_command_builder.h"
#include "infrastructure/svn_output_parser.h"
#include "infrastructure/vcs_command_runner.h"
#include "infrastructure/vcs_error_classifier.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace gitman {
    namespace {
        diagnostic make_diagnostic(const diagnostic_code code, const diagnostic_severity severity, std::u8string message, const project_definition& project)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = std::move(message);
            value.source.project_id = project.id.value;
            return value;
        }

        std::u8string describe_failure(const vcs_failure_kind failure, const vcs_command_result& result)
        {
            std::u8string message { vcs_failure_message(failure) };
            if (result.standard_error_lines.empty())
                return message;

            message.append(u8" (");
            message.append(result.standard_error_lines.front());
            message.push_back(u8')');
            return message;
        }

        std::u8string_view svn_working_directory(const project_definition& project) noexcept
        {
            return project.path.normalized.empty() ? std::u8string_view { project.path.original } : std::u8string_view { project.path.normalized };
        }

        std::uint64_t parse_revision(const std::u8string_view value) noexcept
        {
            std::uint64_t revision { 0 };
            for (const char8_t character : value)
            {
                if (character < u8'0' || character > u8'9')
                    return 0;
                revision = revision * 10u + static_cast<std::uint64_t>(character - u8'0');
            }
            return value.empty() ? 0 : revision;
        }

        svn_browser_query_error browser_error_for_failure(const vcs_failure_kind failure) noexcept
        {
            switch (failure)
            {
            case vcs_failure_kind::none:
                return svn_browser_query_error::none;
            case vcs_failure_kind::authentication_required:
                return svn_browser_query_error::authentication_required;
            case vcs_failure_kind::offline:
                return svn_browser_query_error::offline;
            case vcs_failure_kind::repository_not_found:
                return svn_browser_query_error::repository_not_found;
            case vcs_failure_kind::timed_out:
                return svn_browser_query_error::timed_out;
            case vcs_failure_kind::cancelled:
                return svn_browser_query_error::cancelled;
            case vcs_failure_kind::execution_failed:
            case vcs_failure_kind::command_failed:
                return svn_browser_query_error::failed;
            }
            return svn_browser_query_error::failed;
        }
    } // namespace

    svn_repository_provider::svn_repository_provider(
        vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* const log, vcs_timeout_overrides timeouts, const bool ignore_local_changes) noexcept
        : tool_ { std::move(tool) }
        , runner_ { &runner }
        , probe_ { &probe }
        , log_ { log }
        , timeouts_ { timeouts }
        , ignore_local_changes_ { ignore_local_changes }
    {}

    const vcs_tool_info& svn_repository_provider::tool() const noexcept
    {
        return tool_;
    }

    void svn_repository_provider::set_tool(vcs_tool_info tool)
    {
        tool_ = std::move(tool);
    }

    repository_kind svn_repository_provider::kind() const noexcept
    {
        return repository_kind::subversion;
    }

    bool svn_repository_provider::available() const noexcept
    {
        return tool_.usable();
    }

    repository_query_result svn_repository_provider::query_local(const project_definition& project, const process_cancellation_token& token) noexcept
    {
        try
        {
            return query_local_impl(project, token, false);
        }
        catch (...)
        {
            repository_query_result result {};
            result.snapshot.project = project.id;
            result.snapshot.kind = repository_kind::subversion;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"저장소 상태를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    repository_query_result svn_repository_provider::query_local_metadata(const project_definition& project, const process_cancellation_token& token) noexcept
    {
        try
        {
            return query_local_metadata_impl(project, token);
        }
        catch (...)
        {
            repository_query_result result {};
            result.snapshot.project = project.id;
            result.snapshot.kind = repository_kind::subversion;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"저장소 상태를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    void svn_repository_provider::finish_local_query(repository_query_result& result, const project_definition& project, const process_cancellation_token& token) noexcept
    {
        try
        {
            finish_local_query_impl(result, project, token, false);
        }
        catch (...)
        {
            result.snapshot.availability = repository_availability::unknown;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"저장소 상태를 조회하는 중 내부 오류가 발생했습니다." }, project));
        }
    }

    repository_query_result svn_repository_provider::query_local_impl(const project_definition& project, const process_cancellation_token& token, const bool include_revision_scan)
    {
        repository_query_result result { query_local_metadata_impl(project, token) };
        if (result.snapshot.availability != repository_availability::ready)
            return result;
        finish_local_query_impl(result, project, token, include_revision_scan);
        return result;
    }

    // svn info까지만 수행하는 빠른 로컬 조회다. 작업 트리 요약(status 순회)은
    // finish_local_query_impl이 채운다.
    repository_query_result svn_repository_provider::query_local_metadata_impl(const project_definition& project, const process_cancellation_token& token)
    {
        repository_query_result result {};
        result.snapshot.project = project.id;
        result.snapshot.kind = repository_kind::subversion;
        result.snapshot.local_checked_at = std::chrono::system_clock::now();

        if (available() == false)
        {
            result.snapshot.availability = repository_availability::tool_unavailable;
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::subversion, tool_.availability) }, project));
            return result;
        }

        const std::u8string_view working_directory { svn_working_directory(project) };
        if (is_absolute_windows_path(working_directory) == false)
        {
            result.snapshot.availability = repository_availability::path_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::invalid_project_path, diagnostic_severity::error, std::u8string { u8"프로젝트 경로가 절대 경로가 아닙니다." }, project));
            return result;
        }
        if (probe_->probe(working_directory) != vcs_path_kind::directory)
        {
            result.snapshot.availability = repository_availability::path_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::path_missing, diagnostic_severity::error, std::u8string { u8"프로젝트 경로를 찾을 수 없습니다." }, project));
            return result;
        }

        // 모든 항목을 `svn info --xml` 한 번으로 받는다. 항목마다 `--show-item` 프로세스를
        // 새로 띄우던 방식은 조회 한 번에 실행 고정 비용을 다섯 번 냈다. XML 요소 이름은
        // 로캘과 무관하므로 사람이 읽는 목록을 파싱하지 않는다는 원칙은 그대로다.
        // XML 출력은 기계 해석용이라 카드 로그에 그대로 흘러보내면 읽을 수 없는 문서 덩어리가 찍힌다.
        // 로그로 전달하지 않고, 해석 실패는 진단(diagnostic)으로만 알린다.
        const vcs_command_result info_result { run_vcs_command(*runner_, make_svn_info_request(tool_.executable, working_directory, timeouts_), token, nullptr) };
        if (info_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, info_result) };
            if (info_result.process.completion == process_completion::exited)
            {
                // 실행은 됐는데 값을 얻지 못했다. 등록 경로가 작업 복사본이 아니라는
                // 뜻이며 SVN 오류 코드가 있으면 분류기가 같은 결론을 낸다.
                result.snapshot.availability = repository_availability::not_a_repository;
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::repository_not_found, diagnostic_severity::error, std::u8string { u8"등록한 경로가 SVN 작업 복사본이 아닙니다." }, project));
                return result;
            }

            result.snapshot.availability = repository_availability::unknown;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, info_result), project));
            return result;
        }

        const svn_info_fields info { parse_svn_info_xml(info_result.standard_output_lines) };
        if (info.parsed == false)
        {
            result.snapshot.availability = repository_availability::unknown;
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_output_unparsable, diagnostic_severity::error, std::u8string { u8"svn info 출력을 해석하지 못했습니다." }, project));
            return result;
        }
        result.snapshot.repository_root = info.working_copy_root;
        result.snapshot.svn_repository_root = info.repository_root;
        result.snapshot.svn_repository_uuid = info.repository_uuid;
        result.snapshot.availability = repository_availability::ready;
        // `^/trunk` 형태의 저장소 상대 URL이 카드가 보여 줄 현재 위치다.
        result.snapshot.current_reference = info.relative_url;
        // WC 리비전(entry attribute)은 update 직후 저장소 전역 HEAD가 되어 브랜치와
        // 무관한 값이 보인다. 카드에는 이 경로의 마지막 커밋(last-changed) 리비전을
        // 쓰고, 원격 비교도 같은 기준을 쓴다. commit 요소가 없는 예외적 출력에서만
        // WC 리비전으로 되돌아간다.
        result.snapshot.local_revision = info.last_changed_revision.empty() ? info.revision : info.last_changed_revision;
        return result;
    }

    // query_local_metadata_impl이 ready로 끝난 결과에 작업 트리 요약을 채운다. status
    // 순회는 대형 작업 복사본에서 분 단위로 걸리므로 refresh는 이 단계를 원격 조회와
    // 병렬로 돌린다.
    void svn_repository_provider::finish_local_query_impl(repository_query_result& result, const project_definition& project, const process_cancellation_token& token, const bool include_revision_scan)
    {
        const std::u8string_view working_directory { svn_working_directory(project) };

        svn_status_summary status {};
        if (ignore_local_changes_)
        {
            // 로컬 변경을 상관하지 않는 설정이다. status 순회를 통째로 건너뛰고 작업
            // 트리가 깨끗하다고 믿는다. 실제 변경이 있었다면 update·switch 실행 결과에서
            // 드러나며, 그때 사용자에게 직접 확인하라고 알린다. 단 mixed revision과
            // switched 판정은 로컬 변경이 아니라 작업 복사본 구조라서 이 설정의 대상이
            // 아니다. update·switch 직전 검증의 svnversion 검사는 그대로 수행해 F3의
            // 전환 차단 정책을 유지한다.
            result.snapshot.working_tree.state = working_tree_state::clean;
            if (include_revision_scan == false)
                return;
        }
        else
        {
            const vcs_command_result status_result { run_vcs_command(*runner_, make_svn_status_request(tool_.executable, working_directory, timeouts_), token, log_) };
            if (status_result.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, status_result) };
                result.snapshot.availability = repository_availability::unknown;
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, status_result), project));
                return;
            }

            status = parse_svn_status(status_result.standard_output_lines);
            result.snapshot.working_tree = summarize_svn_working_tree(status);
            if (status.unparsable_records > 0)
                result.diagnostics.push_back(make_diagnostic(
                    diagnostic_code::vcs_output_unparsable, diagnostic_severity::warning, std::u8string { u8"해석하지 못한 상태 줄이 있어 작업 트리 상태를 확정하지 못했습니다." }, project));

            if (include_revision_scan == false)
            {
                // `svnversion`은 status와 별개로 작업 복사본 전체를 한 번 더 걷는다. 큰 작업
                // 복사본에서는 새로 고침 비용을 사실상 두 배로 만들므로 리비전 혼합 판정이
                // 실제로 필요한 update·switch 직전 검증에서만 실행한다. 거짓과 미상을
                // 구분하려고 값은 비워 둔다.
                if (has_svn_switched_entry(status))
                    result.snapshot.has_switched_subtree = true;
                return;
            }
        }

        if (tool_.auxiliary_executable.empty())
        {
            // `svnversion`이 없으면 mixed revision과 switched 판정만 포기하고 나머지
            // 조회는 계속한다. 거짓과 미상을 구분하려고 값을 비워 둔다.
            if (has_svn_switched_entry(status))
                result.snapshot.has_switched_subtree = true;
            return;
        }

        const vcs_command_result version_result { run_vcs_command(*runner_, make_svnversion_request(tool_.auxiliary_executable, working_directory, timeouts_), token, log_) };
        const svn_version_info version { version_result.succeeded() ? parse_svnversion(version_result.first_output_line()) : svn_version_info {} };
        if (version.parsed == false)
        {
            if (has_svn_switched_entry(status))
                result.snapshot.has_switched_subtree = true;
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_output_unparsable, diagnostic_severity::warning, std::u8string { u8"svnversion 출력을 해석하지 못해 리비전 혼합 여부를 확인하지 못했습니다." }, project));
            return;
        }

        result.snapshot.has_mixed_revision = version.mixed_revision();
        result.snapshot.has_switched_subtree = version.switched || has_svn_switched_entry(status);
    }

    repository_query_result svn_repository_provider::query_remote(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token) noexcept
    {
        try
        {
            return query_remote_impl(project, local, token);
        }
        catch (...)
        {
            repository_query_result result {};
            result.snapshot = local;
            result.snapshot.project = project.id;
            result.snapshot.kind = repository_kind::subversion;
            result.snapshot.sync_state = remote_sync_state::error;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"원격 상태를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    repository_query_result svn_repository_provider::query_remote_impl(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token)
    {
        repository_query_result result {};
        // Git과 같이 로컬 상태를 다시 만들지 않는다. 실패해도 직전 값이 남는다.
        result.snapshot = local;
        result.snapshot.project = project.id;
        result.snapshot.kind = repository_kind::subversion;

        if (available() == false)
        {
            result.snapshot.availability = repository_availability::tool_unavailable;
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::subversion, tool_.availability) }, project));
            return result;
        }
        if (local.availability != repository_availability::ready)
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::repository_unavailable, diagnostic_severity::warning, std::u8string { u8"로컬 상태를 먼저 확인해야 원격 상태를 조회할 수 있습니다." }, project));
            return result;
        }

        const std::u8string_view working_directory { svn_working_directory(project) };
        if (is_absolute_windows_path(working_directory) == false || probe_->probe(working_directory) != vcs_path_kind::directory)
        {
            result.snapshot.availability = repository_availability::path_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::path_missing, diagnostic_severity::error, std::u8string { u8"프로젝트 경로를 찾을 수 없습니다." }, project));
            return result;
        }

        // 로컬 조회가 방금 채운 저장소 루트와 상대 URL을 이어 붙여 현재 URL을 만든다.
        // `svn info` 프로세스를 한 번 더 띄우지 않기 위해서다. 형태가 예상과 다르면
        // 이전처럼 물어본다.
        std::u8string url {};
        if (local.svn_repository_root.empty() == false && local.current_reference.starts_with(u8"^/"))
        {
            url = local.svn_repository_root;
            while (url.ends_with(u8'/'))
                url.pop_back();
            url.append(local.current_reference.substr(1));
        }
        if (url.empty())
        {
            const vcs_command_result url_result { run_vcs_command(*runner_, make_svn_info_item_request(tool_.executable, working_directory, svn_info_item::url, {}, timeouts_), token, log_) };
            url = url_result.succeeded() ? parse_svn_info_item(url_result.standard_output_lines) : std::u8string {};
            if (url.empty())
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, url_result) };
                result.snapshot.sync_state = remote_sync_state_for_failure(failure);
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, url_result), project));
                return result;
            }
        }

        const vcs_command_result head_result { run_vcs_command(*runner_, make_svn_remote_revision_request(tool_.executable, working_directory, url, timeouts_), token, log_) };
        if (head_result.succeeded() == false)
        {
            // offline, 인증 필요와 그 밖의 실패를 구분한다. SVN은 번역된 메시지에도
            // `E<숫자>` 코드를 붙이므로 분류가 로캘에 의존하지 않는다.
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, head_result) };
            result.snapshot.sync_state = remote_sync_state_for_failure(failure);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, head_result), project));
            return result;
        }

        const std::uint64_t head_revision { parse_revision(parse_svn_info_item(head_result.standard_output_lines)) };
        const std::uint64_t local_revision { parse_revision(local.local_revision) };
        if (head_revision == 0 || local_revision == 0)
        {
            result.snapshot.sync_state = remote_sync_state::error;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::vcs_output_unparsable, diagnostic_severity::error, std::u8string { u8"리비전 번호를 해석하지 못했습니다." }, project));
            return result;
        }

        result.snapshot.remote_checked_at = std::chrono::system_clock::now();
        result.snapshot.comparison = comparison_source::remote;
        result.snapshot.comparison_target = url;
        result.snapshot.ahead_count = 0;
        // SVN에는 Git의 `ahead`와 `diverged`에 해당하는 개념이 없다. 커밋하지 않은 로컬
        // 변경은 `working_tree`로만 보고한다.
        result.snapshot.behind_count = head_revision > local_revision ? head_revision - local_revision : 0;
        result.snapshot.sync_state = result.snapshot.behind_count > 0 ? remote_sync_state::behind : remote_sync_state::up_to_date;
        return result;
    }

    switch_candidate_result svn_repository_provider::query_switch_candidates(const project_definition& project, const process_cancellation_token& token) noexcept
    {
        switch_candidate_result result {};
        if (available() == false)
        {
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::subversion, tool_.availability) }, project));
            return result;
        }

        try
        {
            const std::u8string_view working_directory { svn_working_directory(project) };
            svn_repository_browser_info browser {};
            // 루트와 현재 URL을 `svn info --xml` 한 번으로 받는다.
            // XML 출력은 기계 해석용이라 카드 로그에 그대로 흘러보내면 읽을 수 없는 문서 덩어리가 찍힌다.
            // 로그로 전달하지 않고, 해석 실패는 진단(diagnostic)으로만 알린다.
            const vcs_command_result info_result { run_vcs_command(*runner_, make_svn_info_request(tool_.executable, working_directory, timeouts_), token, nullptr) };
            if (info_result.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, info_result) };
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, info_result), project));
                return result;
            }
            const svn_info_fields info { parse_svn_info_xml(info_result.standard_output_lines) };
            browser.repository_root_url = info.repository_root;
            browser.current_url = info.url;
            if (browser.repository_root_url.empty() || browser.current_url.empty())
            {
                result.diagnostics.push_back(make_diagnostic(diagnostic_code::vcs_output_unparsable, diagnostic_severity::error, std::u8string { u8"SVN 저장소 URL을 읽지 못했습니다." }, project));
                return result;
            }

            browser.repository_root_url = normalize_svn_browser_url(browser.repository_root_url);
            browser.current_url = normalize_svn_browser_url(browser.current_url);
            if (is_supported_svn_url(browser.repository_root_url) == false || is_supported_svn_url(browser.current_url) == false
                || svn_browser_url_contains(browser.repository_root_url, browser.current_url) == false)
            {
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::vcs_output_unparsable, diagnostic_severity::error, std::u8string { u8"현재 SVN URL이 저장소 루트 아래에 있지 않습니다." }, project));
                return result;
            }
            result.svn_browser = std::move(browser);
            return result;
        }
        catch (...)
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"SVN 저장소 브라우저를 여는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    svn_directory_query_result svn_repository_provider::query_directory(
        const project_definition& project, const std::u8string_view repository_root_url, const std::u8string_view url, const process_cancellation_token& token) noexcept
    {
        svn_directory_query_result result {};
        try
        {
            if (available() == false)
            {
                result.error = svn_browser_query_error::failed;
                result.diagnostics.push_back(make_diagnostic(
                    diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::subversion, tool_.availability) }, project));
                return result;
            }

            const std::u8string root { normalize_svn_browser_url(repository_root_url) };
            const std::u8string target { normalize_svn_browser_url(url) };
            if (is_supported_svn_url(root) == false || is_supported_svn_url(target) == false || svn_browser_url_contains(root, target) == false)
            {
                result.error = svn_browser_query_error::failed;
                result.diagnostics.push_back(
                    make_diagnostic(diagnostic_code::switch_target_rejected, diagnostic_severity::error, std::u8string { u8"저장소 루트 밖의 SVN 디렉터리는 조회하지 않습니다." }, project));
                return result;
            }

            const vcs_command_result list_result {
                run_vcs_command(*runner_, make_svn_list_request(tool_.executable, svn_working_directory(project), target, timeouts_), token, log_),
            };
            if (list_result.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, list_result) };
                result.error = browser_error_for_failure(failure);
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, list_result), project));
                return result;
            }
            result.directories = parse_svn_directory_list(list_result.standard_output_lines);
            return result;
        }
        catch (...)
        {
            result.error = svn_browser_query_error::failed;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"SVN 디렉터리를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    local_changes_result svn_repository_provider::query_local_changes(const project_definition& project, const process_cancellation_token& token) noexcept
    {
        try
        {
            local_changes_result result {};
            if (available() == false)
            {
                result.diagnostics.push_back(make_diagnostic(
                    diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::subversion, tool_.availability) }, project));
                return result;
            }

            const std::u8string_view working_directory { svn_working_directory(project) };
            if (is_absolute_windows_path(working_directory) == false || probe_->probe(working_directory) != vcs_path_kind::directory)
            {
                result.diagnostics.push_back(make_diagnostic(diagnostic_code::path_missing, diagnostic_severity::error, std::u8string { u8"프로젝트 경로를 찾을 수 없습니다." }, project));
                return result;
            }

            const vcs_command_result status_result { run_vcs_command(*runner_, make_svn_status_request(tool_.executable, working_directory, timeouts_), token, log_) };
            if (status_result.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, status_result) };
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, status_result), project));
                return result;
            }

            result.entries = collect_svn_local_changes(parse_svn_status(status_result.standard_output_lines));
            return result;
        }
        catch (...)
        {
            local_changes_result result {};
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"로컬 변경을 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    file_diff_result svn_repository_provider::query_file_diff(const project_definition& project, const local_change_entry& entry, const process_cancellation_token& token) noexcept
    {
        try
        {
            file_diff_result result {};
            const std::u8string_view working_directory { svn_working_directory(project) };
            if (is_absolute_windows_path(working_directory) == false || probe_->probe(working_directory) != vcs_path_kind::directory)
            {
                result.diagnostics.push_back(make_diagnostic(diagnostic_code::path_missing, diagnostic_severity::error, std::u8string { u8"프로젝트 경로를 찾을 수 없습니다." }, project));
                return result;
            }

            // 미버전(미추적) 파일은 저장소가 모르는 파일이라 diff 대신 내용을 읽는다.
            if (entry.kind == local_change_kind::untracked)
                return read_untracked_file_diff(*probe_, working_directory, entry.path);

            if (available() == false)
            {
                result.diagnostics.push_back(make_diagnostic(
                    diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::subversion, tool_.availability) }, project));
                return result;
            }

            const vcs_command_result diff_result { run_vcs_command(*runner_, make_svn_diff_request(tool_.executable, working_directory, entry.path, timeouts_), token, log_) };
            if (diff_result.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, diff_result) };
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, diff_result), project));
                return result;
            }

            append_diff_lines_limited(result, diff_result.standard_output_lines);
            return result;
        }
        catch (...)
        {
            file_diff_result result {};
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"diff를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    update_block_reason evaluate_svn_update_preflight(const repository_snapshot& snapshot) noexcept
    {
        if (snapshot.availability != repository_availability::ready)
            return update_block_reason::repository_unavailable;
        if (snapshot.working_tree.state == working_tree_state::conflicted)
            return update_block_reason::working_tree_conflicted;
        if (snapshot.working_tree.has_tracked_changes())
            // 수정·충돌과 `unknown`을 막는다. 미추적 파일만 있는 상태는 막지
            // 않는다 — `svn update`는 미버전 파일을 건드리지 않고 충돌 시 tree
            // conflict로 남긴다 (field-feedback-design 2.2).
            return update_block_reason::working_tree_dirty;
        if (snapshot.has_switched_subtree.value_or(false))
            return update_block_reason::switched_subtree;
        if (snapshot.has_mixed_revision.value_or(false))
            return update_block_reason::mixed_revision;
        return update_block_reason::none;
    }

    repository_change_result svn_repository_provider::update(const project_definition& project, const update_options&, const process_cancellation_token& token) noexcept
    {
        try
        {
            // SVN에는 submodule이 없어 `update_options`를 쓰지 않는다.
            return update_impl(project, token);
        }
        catch (...)
        {
            repository_change_result result {};
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"업데이트 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    namespace {
        // 로컬 변경을 상관하지 않는 설정은 사전 status 검사를 건너뛰므로 update·switch
        // 실행 출력이 문제를 알아챌 유일한 근거다. 자동 복구는 시도하지 않고 직접 해결을
        // 안내한다. 실패 원인이 로컬 변경이 아닐 수도 있어(예: 네트워크 오류) 단정하지
        // 않는 문구를 쓴다.
        void append_ignore_local_notice(repository_change_result& result, const vcs_command_result& change_result, const project_definition& project)
        {
            if (result.succeeded && svn_change_output_reports_conflict(change_result.standard_output_lines) == false)
                return;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error,
                std::u8string { u8"로컬 변경을 상관하지 않는 설정으로 실행했습니다. 실패나 충돌이 커밋하지 않은 로컬 변경 때문일 수 있으니 작업 복사본을 직접 확인해 주세요." }, project));
        }
    } // namespace

    repository_change_result svn_repository_provider::update_impl(const project_definition& project, const process_cancellation_token& token)
    {
        repository_change_result result {};
        if (available() == false)
        {
            result.blocked_by = update_block_reason::tool_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::update_blocked, diagnostic_severity::warning, std::u8string { update_block_reason_message(result.blocked_by) }, project));
            return result;
        }

        const repository_query_result before { query_local_impl(project, token, true) };
        result.snapshot = before.snapshot;
        for (const diagnostic& value : before.diagnostics)
            result.diagnostics.push_back(value);

        result.blocked_by = evaluate_svn_update_preflight(before.snapshot);
        if (result.blocked_by != update_block_reason::none)
        {
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::update_blocked, diagnostic_severity::warning, std::u8string { update_block_reason_message(result.blocked_by) }, project));
            return result;
        }

        const vcs_command_result update_result { run_vcs_command(*runner_, make_svn_update_request(tool_.executable, svn_working_directory(project)), token, log_) };
        result.executed = true;
        result.succeeded = update_result.succeeded();
        if (result.succeeded == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, update_result) };
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, update_result), project));
        }

        if (ignore_local_changes_)
            append_ignore_local_notice(result, update_result, project);

        // 성공과 실패 모두 실행 직후 상태를 다시 조회한다.
        const repository_query_result after { query_local_impl(project, token, false) };
        result.snapshot = after.snapshot;
        for (const diagnostic& value : after.diagnostics)
            result.diagnostics.push_back(value);
        return result;
    }

    repository_change_result svn_repository_provider::switch_to(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token) noexcept
    {
        try
        {
            return switch_to_impl(project, target, token);
        }
        catch (...)
        {
            repository_change_result result {};
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"전환 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    repository_change_result svn_repository_provider::switch_to_impl(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token)
    {
        repository_change_result result {};
        const auto reject = [&result, &project](const switch_rejection rejection) {
            result.rejected_by = rejection;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::switch_target_rejected, diagnostic_severity::warning, std::u8string { switch_rejection_message(rejection) }, project));
        };

        if (target.kind != switch_candidate_kind::subversion_url || target.target.empty())
        {
            // 대상 자체가 이 provider의 것이 아니거나 비어 있다. 조회하지 않고 거부한다.
            reject(switch_rejection::target_not_found);
            return result;
        }
        if (available() == false)
        {
            reject(switch_rejection::tool_unavailable);
            return result;
        }

        const repository_query_result before { query_local_impl(project, token, true) };
        result.snapshot = before.snapshot;
        for (const diagnostic& value : before.diagnostics)
            result.diagnostics.push_back(value);
        if (before.snapshot.availability != repository_availability::ready)
        {
            reject(switch_rejection::repository_unavailable);
            return result;
        }

        const std::u8string_view working_directory { svn_working_directory(project) };
        // 현재 URL을 다시 물어본다. 상대 URL과 저장소 루트를 이어 붙이는 것보다 규칙이
        // 하나 적다. `query_remote`와 같은 방식이다.
        const vcs_command_result url_result { run_vcs_command(*runner_, make_svn_info_item_request(tool_.executable, working_directory, svn_info_item::url, {}, timeouts_), token, log_) };
        const std::u8string current_url { url_result.succeeded() ? normalize_svn_browser_url(parse_svn_info_item(url_result.standard_output_lines)) : std::u8string {} };
        if (current_url.empty())
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, url_result) };
            result.rejected_by = switch_rejection::repository_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, url_result), project));
            return result;
        }

        // 네트워크를 쓰기 전에 URL 형식, 현재 위치와 작업 트리 상태를 먼저 본다. 여기서
        // 걸리면 원격에 접속하지 않는다. 문서의 svn_switch_targets는 읽고 보존만 한다.
        const switch_validation_result local_validation { validate_svn_switch_target(target, before.snapshot, current_url) };
        if (local_validation.approved == false)
        {
            result.rejected_by = local_validation.rejection;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::switch_target_rejected, diagnostic_severity::warning, local_validation.message, project));
            return result;
        }

        // 대상 URL이 실제로 있는지와 같은 저장소인지를 확인한다. 여기서 처음 네트워크를
        // 쓴다.
        struct identity_field
        {
            svn_info_item item {};
            std::u8string* target {};
        };

        std::u8string target_root {};
        std::u8string target_uuid {};
        const identity_field identity_fields[] {
            { svn_info_item::repository_root, &target_root },
            { svn_info_item::repository_uuid, &target_uuid },
        };

        for (const identity_field& field : identity_fields)
        {
            const vcs_command_result identity_result {
                run_vcs_command(*runner_, make_svn_remote_info_item_request(tool_.executable, working_directory, field.item, target.target, timeouts_), token, log_),
            };
            if (identity_result.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, identity_result) };
                result.rejected_by = switch_rejection::target_unreachable;
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, identity_result), project));
                return result;
            }
            *field.target = parse_svn_info_item(identity_result.standard_output_lines);
        }

        const switch_validation_result identity_validation { validate_svn_repository_identity(before.snapshot, target_root, target_uuid) };
        if (identity_validation.approved == false)
        {
            // 검증에 실패하면 `switch` 명령을 만들지 않는다. REQ-007의 수용 기준이다.
            result.rejected_by = identity_validation.rejection;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::switch_target_rejected, diagnostic_severity::warning, identity_validation.message, project));
            return result;
        }

        const vcs_command_result switch_result { run_vcs_command(*runner_, make_svn_switch_request(tool_.executable, working_directory, target.target), token, log_) };
        result.executed = true;
        result.succeeded = switch_result.succeeded();
        if (result.succeeded == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, switch_result) };
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, switch_result), project));
        }

        if (ignore_local_changes_)
            append_ignore_local_notice(result, switch_result, project);

        // 성공과 실패 모두 실행 직후 상태를 다시 조회한다.
        const repository_query_result after { query_local_impl(project, token, false) };
        result.snapshot = after.snapshot;
        for (const diagnostic& value : after.diagnostics)
            result.diagnostics.push_back(value);
        return result;
    }
} // namespace gitman
