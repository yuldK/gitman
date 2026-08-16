#include "infrastructure/git_repository_provider.h"

#include "application/vcs_tool_registry.h"
#include "domain/path_syntax.h"
#include "infrastructure/git_command_builder.h"
#include "infrastructure/git_status_parser.h"
#include "infrastructure/vcs_command_runner.h"
#include "infrastructure/vcs_error_classifier.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <utility>

namespace gitman {
    namespace {
        constexpr std::u8string_view merge_marker { u8"MERGE_HEAD" };
        constexpr std::u8string_view rebase_merge_marker { u8"rebase-merge" };
        constexpr std::u8string_view rebase_apply_marker { u8"rebase-apply" };
        constexpr std::u8string_view cherry_pick_marker { u8"CHERRY_PICK_HEAD" };
        constexpr std::u8string_view revert_marker { u8"REVERT_HEAD" };
        constexpr std::u8string_view bisect_marker { u8"BISECT_LOG" };
        constexpr std::u8string_view index_lock_marker { u8"index.lock" };
        // detached HEAD에는 branch 이름이 없다. 어떤 상태인지와 어느 커밋인지를 함께
        // 보여 주려고 접두어와 짧은 커밋 ID를 붙인다.
        constexpr std::u8string_view detached_reference_prefix { u8"(detached) " };
        constexpr std::size_t short_revision_length { 7 };

        bool is_separator(const char8_t value) noexcept
        {
            return value == u8'\\' || value == u8'/';
        }

        std::u8string join_path(const std::u8string_view directory, const std::u8string_view name)
        {
            std::u8string result { directory };
            if (result.empty() == false && is_separator(result.back()) == false)
                // `rev-parse`가 보고하는 경로는 `/`를 쓰므로 같은 구분자로 잇는다. Win32
                // API는 두 구분자를 모두 받아들인다.
                result.push_back(u8'/');
            result.append(name);
            return result;
        }

        diagnostic make_diagnostic(const diagnostic_code code, const diagnostic_severity severity, std::u8string message, const project_definition& project)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = std::move(message);
            value.source.project_id = project.id.value;
            return value;
        }

        // 실패 사유와 도구가 낸 첫 줄을 합친다. 출력은 파이프라인이 이미 마스킹했다.
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

        remote_sync_state sync_state_from_counts(const std::uint64_t ahead, const std::uint64_t behind) noexcept
        {
            if (ahead > 0 && behind > 0)
                return remote_sync_state::diverged;
            if (ahead > 0)
                return remote_sync_state::ahead;
            if (behind > 0)
                return remote_sync_state::behind;
            return remote_sync_state::up_to_date;
        }

        // 원격 비교 대상을 정하지 못한 상태다. 이전 조회가 남긴 비교 값은 지운다.
        void set_remote_target_missing(repository_snapshot& snapshot)
        {
            snapshot.comparison = comparison_source::none;
            snapshot.comparison_target.clear();
            snapshot.ahead_count = 0;
            snapshot.behind_count = 0;
            snapshot.sync_state = remote_sync_state::remote_target_missing;
        }

        std::u8string remote_target_missing_message(const git_remote_target_reason reason)
        {
            switch (reason)
            {
            case git_remote_target_reason::ambiguous_remote:
                // 자동으로 고르면 사용자가 의도하지 않은 원격과 비교하게 된다.
                return std::u8string { u8"remote가 여러 개여서 비교 대상을 자동으로 고를 수 없습니다. upstream 또는 preferred_remote를 지정하세요." };
            case git_remote_target_reason::detached_head:
                return std::u8string { u8"detached HEAD에는 비교할 원격 branch가 없습니다." };
            case git_remote_target_reason::no_branch:
                return std::u8string { u8"현재 branch 이름을 알 수 없어 비교 대상을 고르지 못했습니다." };
            default:
                return std::u8string { u8"비교할 원격 branch를 찾지 못했습니다." };
            }
        }

        std::u8string make_tracking_reference(const std::u8string_view remote, const std::u8string_view branch)
        {
            std::u8string reference { u8"refs/remotes/" };
            reference.append(remote);
            reference.push_back(u8'/');
            reference.append(branch);
            return reference;
        }

        std::u8string make_display_name(const std::u8string_view remote, const std::u8string_view branch)
        {
            std::u8string name { remote };
            name.push_back(u8'/');
            name.append(branch);
            return name;
        }

        // upstream 문자열에서 remote 이름을 떼어 낸다. branch 이름에도 `/`가 들어갈 수
        // 있으므로 설정된 remote 이름 중 가장 긴 접두사를 고른다.
        const std::u8string* find_upstream_remote(const std::vector<std::u8string>& remotes, const std::u8string_view upstream) noexcept
        {
            const std::u8string* match { nullptr };
            for (const std::u8string& remote : remotes)
            {
                if (upstream.size() <= remote.size() + 1)
                    continue;
                if (upstream.starts_with(remote) == false || upstream[remote.size()] != u8'/')
                    continue;
                if (match == nullptr || remote.size() > match->size())
                    match = &remote;
            }
            return match;
        }

        bool contains_remote(const std::vector<std::u8string>& remotes, const std::u8string_view name) noexcept
        {
            return std::ranges::find(remotes, name) != remotes.end();
        }

        std::u8string describe_current_reference(const git_status_summary& status)
        {
            if (status.detached == false)
                return status.head;

            std::u8string reference { detached_reference_prefix };
            reference.append(status.oid.substr(0, std::min(status.oid.size(), short_revision_length)));
            return reference;
        }
    } // namespace

    bool git_in_progress_markers::operation_in_progress() const noexcept
    {
        // `index.lock`은 다른 Git 프로세스가 작업 중이라는 뜻이지 중단된 작업이 아니다.
        // 보호 정책은 둘을 모두 막지만 카드는 사유를 구분해 보여 준다.
        return merge || rebase || cherry_pick || revert || bisect;
    }

    git_in_progress_markers detect_git_in_progress_markers(const vcs_file_probe& probe, const std::u8string_view git_directory)
    {
        git_in_progress_markers markers {};
        if (git_directory.empty())
            return markers;

        markers.merge = probe.probe(join_path(git_directory, merge_marker)) == vcs_path_kind::file;
        markers.rebase
            = probe.probe(join_path(git_directory, rebase_merge_marker)) == vcs_path_kind::directory || probe.probe(join_path(git_directory, rebase_apply_marker)) == vcs_path_kind::directory;
        markers.cherry_pick = probe.probe(join_path(git_directory, cherry_pick_marker)) == vcs_path_kind::file;
        markers.revert = probe.probe(join_path(git_directory, revert_marker)) == vcs_path_kind::file;
        markers.bisect = probe.probe(join_path(git_directory, bisect_marker)) == vcs_path_kind::file;
        markers.index_locked = probe.probe(join_path(git_directory, index_lock_marker)) == vcs_path_kind::file;
        return markers;
    }

    git_remote_target select_git_remote_target(
        const std::vector<std::u8string>& remotes, const std::u8string_view branch, const std::u8string_view upstream, const std::u8string_view preferred_remote, const bool detached)
    {
        git_remote_target target {};
        target.preferred_remote_missing = preferred_remote.empty() == false && contains_remote(remotes, preferred_remote) == false;
        if (detached || branch.empty())
        {
            target.reason = detached ? git_remote_target_reason::detached_head : git_remote_target_reason::no_branch;
            return target;
        }
        if (remotes.empty())
        {
            // remote가 하나도 없는 저장소는 `local_only`다. fetch할 곳이 없다.
            target.reason = git_remote_target_reason::no_remote;
            return target;
        }

        if (const std::u8string* const upstream_remote { find_upstream_remote(remotes, upstream) }; upstream_remote != nullptr)
        {
            // upstream이 있으면 그대로 쓴다. 사용자가 이미 정한 비교 대상이다.
            target.resolved = true;
            target.reason = git_remote_target_reason::upstream;
            target.remote = *upstream_remote;
            target.branch = upstream.substr(upstream_remote->size() + 1);
            target.tracking_reference = make_tracking_reference(target.remote, target.branch);
            target.display_name = std::u8string { upstream };
            return target;
        }

        // upstream이 없거나 local branch를 가리킨다. 남은 규칙으로 remote를 고른다.
        target.branch = branch;
        if (target.preferred_remote_missing == false && preferred_remote.empty() == false)
            target.reason = git_remote_target_reason::preferred_remote;
        else if (contains_remote(remotes, u8"origin"))
            target.reason = git_remote_target_reason::origin;
        else if (remotes.size() == 1)
            target.reason = git_remote_target_reason::only_remote;
        else
            // 후보가 여럿인데 규칙으로 좁혀지지 않으면 자동으로 고르지 않는다.
            target.reason = git_remote_target_reason::ambiguous_remote;

        switch (target.reason)
        {
        case git_remote_target_reason::preferred_remote:
            target.remote = preferred_remote;
            break;
        case git_remote_target_reason::origin:
            target.remote = u8"origin";
            break;
        case git_remote_target_reason::only_remote:
            target.remote = remotes.front();
            break;
        default:
            target.branch.clear();
            return target;
        }

        target.resolved = true;
        target.tracking_reference = make_tracking_reference(target.remote, target.branch);
        target.display_name = make_display_name(target.remote, target.branch);
        return target;
    }

    std::u8string_view git_working_directory(const project_definition& project) noexcept
    {
        return project.path.normalized.empty() ? std::u8string_view { project.path.original } : std::u8string_view { project.path.normalized };
    }

    git_repository_provider::git_repository_provider(vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* const log) noexcept
        : tool_ { std::move(tool) }
        , runner_ { &runner }
        , probe_ { &probe }
        , log_ { log }
    {}

    const vcs_tool_info& git_repository_provider::tool() const noexcept
    {
        return tool_;
    }

    void git_repository_provider::set_tool(vcs_tool_info tool)
    {
        tool_ = std::move(tool);
    }

    repository_kind git_repository_provider::kind() const noexcept
    {
        return repository_kind::git;
    }

    bool git_repository_provider::available() const noexcept
    {
        return tool_.usable();
    }

    repository_query_result git_repository_provider::query_local(const project_definition& project, const process_cancellation_token& token) noexcept
    {
        try
        {
            return query_local_impl(project, token);
        }
        catch (...)
        {
            repository_query_result result {};
            result.snapshot.project = project.id;
            result.snapshot.kind = repository_kind::git;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"저장소 상태를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    repository_query_result git_repository_provider::query_local_impl(const project_definition& project, const process_cancellation_token& token)
    {
        repository_query_result result {};
        result.snapshot.project = project.id;
        result.snapshot.kind = repository_kind::git;
        result.snapshot.local_checked_at = std::chrono::system_clock::now();

        if (available() == false)
        {
            // 도구가 없어도 앱은 계속 동작한다. 카드는 목록에 남고 동작만 비활성화된다.
            result.snapshot.availability = repository_availability::tool_unavailable;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::git, tool_.availability) }, project));
            return result;
        }

        const std::u8string_view working_directory { git_working_directory(project) };
        if (is_absolute_windows_path(working_directory) == false)
        {
            result.snapshot.availability = repository_availability::path_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::invalid_project_path, diagnostic_severity::error, std::u8string { u8"프로젝트 경로가 절대 경로가 아닙니다." }, project));
            return result;
        }

        if (probe_->probe(working_directory) != vcs_path_kind::directory)
        {
            // 등록 경로가 사라졌거나 접근할 수 없다. 프로세스를 띄워 시작 실패로 알아내는
            // 대신 여기서 사유를 확정한다. 단계 2가 기록한 경로 상태는 오래됐을 수 있다.
            result.snapshot.availability = repository_availability::path_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::path_missing, diagnostic_severity::error, std::u8string { u8"프로젝트 경로를 찾을 수 없습니다." }, project));
            return result;
        }

        const vcs_command_result layout_result { run_vcs_command(*runner_, make_git_layout_request(tool_.executable, working_directory), token, log_) };
        const git_repository_layout layout { parse_git_repository_layout(layout_result.standard_output_lines) };
        const vcs_failure_kind layout_failure { classify_vcs_failure(repository_kind::git, layout_result) };

        if (layout.parsed == false)
        {
            if (layout_result.process.completion == process_completion::exited)
            {
                // 실행은 됐는데 아무 값도 얻지 못했다. 메시지 언어와 무관하게 등록 경로가
                // Git 저장소가 아니라는 뜻이다. 번역되는 문장으로 판정하지 않는다.
                result.snapshot.availability = repository_availability::not_a_repository;
                result.diagnostics.push_back(make_diagnostic(diagnostic_code::repository_not_found, diagnostic_severity::error, std::u8string { u8"등록한 경로가 Git 저장소가 아닙니다." }, project));
                return result;
            }

            result.snapshot.availability = repository_availability::unknown;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(layout_failure), diagnostic_severity::error, describe_failure(layout_failure, layout_result), project));
            return result;
        }

        if (layout.bare || layout.inside_work_tree == false)
        {
            // bare 저장소와 git dir 안의 경로에는 작업 트리가 없어 상태를 만들 수 없다.
            // 지원 범위는 단계 5에서 정하고, 단계 4는 이 상태를 미설치나 잘못된 경로와
            // 구분해 보고만 한다.
            result.snapshot.availability = repository_availability::unsupported_layout;
            result.snapshot.repository_root = layout.git_directory;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::repository_unavailable, diagnostic_severity::warning, std::u8string { u8"작업 트리가 없는 Git 저장소는 카드에서 다루지 않습니다." }, project));
            return result;
        }

        if (layout_result.succeeded() == false || layout.work_tree_root.empty())
        {
            result.snapshot.availability = repository_availability::unknown;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(layout_failure), diagnostic_severity::error, describe_failure(layout_failure, layout_result), project));
            return result;
        }

        result.snapshot.repository_root = layout.work_tree_root;

        const vcs_command_result status_result { run_vcs_command(*runner_, make_git_status_request(tool_.executable, working_directory), token, log_) };
        if (status_result.succeeded() == false)
        {
            // 저장소인 것은 확인했지만 상태를 읽지 못했다. 상태를 단정하지 않는다.
            const vcs_failure_kind status_failure { classify_vcs_failure(repository_kind::git, status_result) };
            result.snapshot.availability = repository_availability::unknown;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(status_failure), diagnostic_severity::error, describe_failure(status_failure, status_result), project));
            return result;
        }

        const git_status_summary status { parse_git_status_porcelain_v2(status_result.standard_output_lines) };
        result.snapshot.availability = repository_availability::ready;
        result.snapshot.current_reference = describe_current_reference(status);
        result.snapshot.local_revision = status.oid;
        result.snapshot.working_tree = summarize_git_working_tree(status);

        const git_in_progress_markers markers { detect_git_in_progress_markers(*probe_, layout.git_directory) };
        result.snapshot.working_tree.operation_in_progress = markers.operation_in_progress();
        result.snapshot.working_tree.has_index_lock = markers.index_locked;

        if (status.upstream.empty() == false)
        {
            // 이미 받아 둔 remote tracking ref와의 비교다. 네트워크를 쓰지 않으므로
            // 최신이 아닐 수 있고, `comparison_source::local`이 그 사실을 나타낸다.
            // 원격을 실제로 확인한 remote-first 판정은 `S4-D3`의 `query_remote`가 덮어쓴다.
            result.snapshot.comparison = comparison_source::local;
            result.snapshot.comparison_target = status.upstream;
            if (status.has_ahead_behind)
            {
                result.snapshot.ahead_count = status.ahead;
                result.snapshot.behind_count = status.behind;
                result.snapshot.sync_state = sync_state_from_counts(status.ahead, status.behind);
            }
        }

        if (status.unparsable_records > 0)
            result.diagnostics.push_back(make_diagnostic(
                diagnostic_code::vcs_output_unparsable, diagnostic_severity::warning, std::u8string { u8"해석하지 못한 상태 레코드가 있어 작업 트리 상태를 확정하지 못했습니다." }, project));
        if (status.has_branch_header == false)
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::vcs_output_unparsable, diagnostic_severity::warning, std::u8string { u8"상태 출력에서 branch 헤더를 찾지 못했습니다." }, project));
        return result;
    }

    repository_query_result git_repository_provider::query_remote(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token) noexcept
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
            result.snapshot.kind = repository_kind::git;
            result.snapshot.sync_state = remote_sync_state::error;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"원격 상태를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    repository_query_result git_repository_provider::query_remote_impl(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token)
    {
        repository_query_result result {};
        // 원격 조회는 로컬 상태를 다시 만들지 않는다. 실패해도 직전 로컬 값과 마지막
        // 성공 원격 확인 시각이 그대로 남는다.
        result.snapshot = local;
        result.snapshot.project = project.id;
        result.snapshot.kind = repository_kind::git;

        if (available() == false)
        {
            result.snapshot.availability = repository_availability::tool_unavailable;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::git, tool_.availability) }, project));
            return result;
        }
        if (local.availability != repository_availability::ready)
        {
            // 로컬 조회가 저장소로 인정하지 않은 경로다. 원격을 확인할 이유가 없다.
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::repository_unavailable, diagnostic_severity::warning, std::u8string { u8"로컬 상태를 먼저 확인해야 원격 상태를 조회할 수 있습니다." }, project));
            return result;
        }

        const std::u8string_view working_directory { git_working_directory(project) };
        if (is_absolute_windows_path(working_directory) == false || probe_->probe(working_directory) != vcs_path_kind::directory)
        {
            result.snapshot.availability = repository_availability::path_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::path_missing, diagnostic_severity::error, std::u8string { u8"프로젝트 경로를 찾을 수 없습니다." }, project));
            return result;
        }

        if (local.working_tree.is_detached)
        {
            // 비교할 local branch 이름이 없다. upstream 규칙을 적용하지 않고 네트워크도
            // 쓰지 않는다.
            set_remote_target_missing(result.snapshot);
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::repository_unavailable, diagnostic_severity::information, std::u8string { u8"detached HEAD에는 비교할 원격 branch가 없습니다." }, project));
            return result;
        }

        const vcs_command_result remote_result { run_vcs_command(*runner_, make_git_remote_list_request(tool_.executable, working_directory), token, log_) };
        if (remote_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, remote_result) };
            result.snapshot.sync_state = remote_sync_state_for_failure(failure);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, remote_result), project));
            return result;
        }

        const std::vector<std::u8string> remotes { parse_git_remote_names(remote_result.standard_output_lines) };
        // 로컬 조회가 `comparison_source::local`로 남긴 값이 곧 현재 branch의 upstream이다.
        const std::u8string_view upstream { local.comparison == comparison_source::local ? std::u8string_view { local.comparison_target } : std::u8string_view {} };
        const git_remote_target target {
            select_git_remote_target(remotes, local.current_reference, upstream, project.preferred_remote.value_or(std::u8string {}), local.working_tree.is_detached),
        };

        if (target.preferred_remote_missing)
            // 지정한 값이 조용히 무시되면 사용자는 다른 remote와 비교되고 있다는 사실을
            // 알 수 없다.
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::invalid_project_field, diagnostic_severity::warning, std::u8string { u8"지정한 preferred_remote가 저장소에 없습니다." }, project));

        if (target.resolved == false)
        {
            if (target.reason == git_remote_target_reason::no_remote)
            {
                // remote가 없으면 fetch하지 않는다. 비교 대상이 없는 상태와 원격 확인에
                // 실패한 상태는 사용자가 할 일이 다르다.
                result.snapshot.comparison = comparison_source::none;
                result.snapshot.comparison_target.clear();
                result.snapshot.ahead_count = 0;
                result.snapshot.behind_count = 0;
                result.snapshot.sync_state = remote_sync_state::local_only;
                return result;
            }

            set_remote_target_missing(result.snapshot);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::repository_not_found, diagnostic_severity::warning, remote_target_missing_message(target.reason), project));
            return result;
        }

        const vcs_command_result fetch_result { run_vcs_command(*runner_, make_git_fetch_request(tool_.executable, working_directory, target.remote), token, log_) };
        if (fetch_result.succeeded() == false)
        {
            // offline, 인증 필요와 그 밖의 실패를 구분한다. 판정 근거는 로캘 독립 신호뿐이며
            // 직전 로컬 비교 값은 그대로 남겨 둔다.
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, fetch_result) };
            result.snapshot.sync_state = remote_sync_state_for_failure(failure);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, fetch_result), project));
            return result;
        }

        // 원격에 실제로 닿은 시각이다. 비교 대상이 없더라도 확인 자체는 성공했다.
        result.snapshot.remote_checked_at = std::chrono::system_clock::now();

        const vcs_command_result verify_result { run_vcs_command(*runner_, make_git_verify_reference_request(tool_.executable, working_directory, target.tracking_reference), token, log_) };
        if (verify_result.process.completion != process_completion::exited)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, verify_result) };
            result.snapshot.sync_state = remote_sync_state_for_failure(failure);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, verify_result), project));
            return result;
        }
        if (verify_result.succeeded() == false)
        {
            // fetch 뒤에도 tracking ref가 없으면 원격에 같은 이름의 branch가 없다는 뜻이다.
            // local 비교로 물러서지 않는다.
            std::u8string message { u8"원격에 비교할 branch가 없습니다: " };
            message.append(target.display_name);
            set_remote_target_missing(result.snapshot);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::repository_not_found, diagnostic_severity::warning, std::move(message), project));
            return result;
        }

        if (local.local_revision.empty())
        {
            // 커밋이 하나도 없으면 `HEAD`가 없어 대칭 차이를 계산할 수 없다.
            result.snapshot.comparison = comparison_source::remote;
            result.snapshot.comparison_target = target.display_name;
            result.snapshot.ahead_count = 0;
            result.snapshot.behind_count = 0;
            result.snapshot.sync_state = remote_sync_state::unknown;
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::vcs_output_unparsable, diagnostic_severity::information, std::u8string { u8"커밋이 없어 원격과 비교할 수 없습니다." }, project));
            return result;
        }

        const vcs_command_result count_result { run_vcs_command(*runner_, make_git_ahead_behind_request(tool_.executable, working_directory, target.tracking_reference), token, log_) };
        const git_ahead_behind counts { parse_git_ahead_behind(count_result.first_output_line()) };
        if (count_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, count_result) };
            result.snapshot.sync_state = remote_sync_state_for_failure(failure);
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, count_result), project));
            return result;
        }
        if (counts.parsed == false)
        {
            result.snapshot.sync_state = remote_sync_state::error;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::vcs_output_unparsable, diagnostic_severity::error, std::u8string { u8"원격과의 커밋 차이를 해석하지 못했습니다." }, project));
            return result;
        }

        result.snapshot.comparison = comparison_source::remote;
        result.snapshot.comparison_target = target.display_name;
        result.snapshot.ahead_count = counts.ahead;
        result.snapshot.behind_count = counts.behind;
        result.snapshot.sync_state = sync_state_from_counts(counts.ahead, counts.behind);
        return result;
    }

    switch_candidate_result git_repository_provider::query_switch_candidates(const project_definition&, const process_cancellation_token&) noexcept
    {
        // 후보 조회는 `S4-D6-CODE` 구간이다.
        return {};
    }

    repository_change_result git_repository_provider::update(const project_definition&, const update_options&, const process_cancellation_token&) noexcept
    {
        // update는 `S4-D5-CODE` 구간이다. `executed`가 거짓이므로 어떤 process request도
        // 만들지 않았다는 계약은 그대로 지켜진다.
        return {};
    }

    repository_change_result git_repository_provider::switch_to(const project_definition&, const switch_candidate&, const process_cancellation_token&) noexcept
    {
        // switch는 `S4-D6-CODE` 구간이다.
        return {};
    }
} // namespace gitman
