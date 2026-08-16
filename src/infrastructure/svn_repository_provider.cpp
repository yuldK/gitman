#include "infrastructure/svn_repository_provider.h"

#include "application/vcs_tool_registry.h"
#include "domain/path_syntax.h"
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
    } // namespace

    svn_repository_provider::svn_repository_provider(vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* const log) noexcept
        : tool_ { std::move(tool) }
        , runner_ { &runner }
        , probe_ { &probe }
        , log_ { log }
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
            return query_local_impl(project, token);
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

    repository_query_result svn_repository_provider::query_local_impl(const project_definition& project, const process_cancellation_token& token)
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

        // 값 하나만 내는 항목을 차례로 받는다. 항목마다 프로세스를 새로 띄우지만 명시적
        // 조회에서만 일어나고, 그 대신 사람이 읽는 목록을 파싱하지 않아도 된다.
        struct info_field
        {
            svn_info_item item {};
            std::u8string* target {};
        };

        std::u8string relative_url {};
        std::u8string revision {};
        const info_field fields[] {
            { svn_info_item::working_copy_root, &result.snapshot.repository_root },
            { svn_info_item::relative_url, &relative_url },
            { svn_info_item::revision, &revision },
            { svn_info_item::repository_root, &result.snapshot.svn_repository_root },
            { svn_info_item::repository_uuid, &result.snapshot.svn_repository_uuid },
        };

        for (const info_field& field : fields)
        {
            const vcs_command_result info_result { run_vcs_command(*runner_, make_svn_info_item_request(tool_.executable, working_directory, field.item), token, log_) };
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
            *field.target = parse_svn_info_item(info_result.standard_output_lines);
        }

        result.snapshot.availability = repository_availability::ready;
        // `^/trunk` 형태의 저장소 상대 URL이 카드가 보여 줄 현재 위치다.
        result.snapshot.current_reference = relative_url;
        result.snapshot.local_revision = revision;

        const vcs_command_result status_result { run_vcs_command(*runner_, make_svn_status_request(tool_.executable, working_directory), token, log_) };
        if (status_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, status_result) };
            result.snapshot.availability = repository_availability::unknown;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, status_result), project));
            return result;
        }

        const svn_status_summary status { parse_svn_status(status_result.standard_output_lines) };
        result.snapshot.working_tree = summarize_svn_working_tree(status);
        if (status.unparsable_records > 0)
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_output_unparsable, diagnostic_severity::warning, std::u8string { u8"해석하지 못한 상태 줄이 있어 작업 트리 상태를 확정하지 못했습니다." }, project));

        if (tool_.auxiliary_executable.empty())
        {
            // `svnversion`이 없으면 mixed revision과 switched 판정만 포기하고 나머지
            // 조회는 계속한다. 거짓과 미상을 구분하려고 값을 비워 둔다.
            if (has_svn_switched_entry(status))
                result.snapshot.has_switched_subtree = true;
            return result;
        }

        const vcs_command_result version_result { run_vcs_command(*runner_, make_svnversion_request(tool_.auxiliary_executable, working_directory), token, log_) };
        const svn_version_info version { version_result.succeeded() ? parse_svnversion(version_result.first_output_line()) : svn_version_info {} };
        if (version.parsed == false)
        {
            if (has_svn_switched_entry(status))
                result.snapshot.has_switched_subtree = true;
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_output_unparsable, diagnostic_severity::warning, std::u8string { u8"svnversion 출력을 해석하지 못해 리비전 혼합 여부를 확인하지 못했습니다." }, project));
            return result;
        }

        result.snapshot.has_mixed_revision = version.mixed_revision();
        result.snapshot.has_switched_subtree = version.switched || has_svn_switched_entry(status);
        return result;
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

        // 현재 URL을 다시 물어본다. snapshot의 상대 URL과 저장소 루트를 이어 붙이는
        // 것보다 규칙이 하나 적고, 명시적 조회에서만 실행된다.
        const vcs_command_result url_result { run_vcs_command(*runner_, make_svn_info_item_request(tool_.executable, working_directory, svn_info_item::url), token, log_) };
        const std::u8string url { url_result.succeeded() ? parse_svn_info_item(url_result.standard_output_lines) : std::u8string {} };
        if (url.empty())
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, url_result) };
            result.snapshot.sync_state = remote_sync_state_for_failure(failure);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, url_result), project));
            return result;
        }

        const vcs_command_result head_result { run_vcs_command(*runner_, make_svn_remote_revision_request(tool_.executable, working_directory, url), token, log_) };
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

    svn_switch_candidate_set build_svn_switch_candidates(const std::vector<std::u8string>& allowed_targets)
    {
        svn_switch_candidate_set result {};
        for (const std::u8string& url : allowed_targets)
        {
            if (is_supported_svn_url(url) == false)
            {
                // 문서에 적혀 있어도 URL로 다룰 수 없는 값은 후보에 넣지 않는다.
                result.rejected.push_back(url);
                continue;
            }
            // 같은 URL이 여러 번 적혀 있어도 후보는 하나만 만든다.
            if (std::ranges::any_of(result.candidates, [&url](const switch_candidate& candidate) { return candidate.target == url; }))
                continue;

            switch_candidate candidate {};
            candidate.kind = switch_candidate_kind::subversion_url;
            candidate.display_name = url;
            candidate.target = url;
            result.candidates.push_back(std::move(candidate));
        }
        return result;
    }

    switch_candidate_result svn_repository_provider::query_switch_candidates(const project_definition& project, const process_cancellation_token&) noexcept
    {
        switch_candidate_result result {};
        if (available() == false)
        {
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::subversion, tool_.availability) }, project));
            return result;
        }

        // 후보는 문서의 허용 목록뿐이라 저장소를 조회하지 않는다. 이 동작은 어떤 process
        // request도 만들지 않으며 `stale`도 될 수 없다.
        svn_switch_candidate_set candidates { build_svn_switch_candidates(project.svn_switch_targets) };
        for (const std::u8string& rejected : candidates.rejected)
        {
            std::u8string message { u8"svn_switch_targets의 URL 형식을 해석할 수 없어 후보에서 제외했습니다: " };
            message.append(rejected);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::invalid_project_field, diagnostic_severity::warning, std::move(message), project));
        }
        result.candidates = std::move(candidates.candidates);
        return result;
    }

    update_block_reason evaluate_svn_update_preflight(const repository_snapshot& snapshot) noexcept
    {
        if (snapshot.availability != repository_availability::ready)
            return update_block_reason::repository_unavailable;
        if (snapshot.working_tree.state == working_tree_state::conflicted)
            return update_block_reason::working_tree_conflicted;
        if (snapshot.working_tree.state != working_tree_state::clean)
            // `modified`와 `unknown`을 함께 막는다.
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

    repository_change_result svn_repository_provider::update_impl(const project_definition& project, const process_cancellation_token& token)
    {
        repository_change_result result {};
        if (available() == false)
        {
            result.blocked_by = update_block_reason::tool_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::update_blocked, diagnostic_severity::warning, std::u8string { update_block_reason_message(result.blocked_by) }, project));
            return result;
        }

        const repository_query_result before { query_local_impl(project, token) };
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

        // 성공과 실패 모두 실행 직후 상태를 다시 조회한다.
        const repository_query_result after { query_local_impl(project, token) };
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

        const repository_query_result before { query_local_impl(project, token) };
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
        const vcs_command_result url_result { run_vcs_command(*runner_, make_svn_info_item_request(tool_.executable, working_directory, svn_info_item::url), token, log_) };
        const std::u8string current_url { url_result.succeeded() ? parse_svn_info_item(url_result.standard_output_lines) : std::u8string {} };
        if (current_url.empty())
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::subversion, url_result) };
            result.rejected_by = switch_rejection::repository_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, url_result), project));
            return result;
        }

        // 네트워크를 쓰기 전에 문서 허용 목록, URL 형식, 현재 위치와 작업 트리 상태를
        // 먼저 본다. 여기서 걸리면 원격에 접속하지 않는다.
        const switch_validation_result local_validation { validate_svn_switch_target(project.svn_switch_targets, target, before.snapshot, current_url) };
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
                run_vcs_command(*runner_, make_svn_remote_info_item_request(tool_.executable, working_directory, field.item, target.target), token, log_),
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

        // 성공과 실패 모두 실행 직후 상태를 다시 조회한다.
        const repository_query_result after { query_local_impl(project, token) };
        result.snapshot = after.snapshot;
        for (const diagnostic& value : after.diagnostics)
            result.diagnostics.push_back(value);
        return result;
    }
} // namespace gitman
