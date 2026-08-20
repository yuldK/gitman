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
#include <iterator>
#include <string>
#include <utility>
#include <vector>

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

        constexpr std::u8string_view local_reference_prefix { u8"refs/heads/" };
        constexpr std::u8string_view remote_reference_prefix { u8"refs/remotes/" };
        // `refs/remotes/<remote>/HEAD`는 기본 branch를 가리키는 심볼릭 ref이며 전환
        // 후보가 아니다. `%(symref)`가 비어 있는 배포판을 위해 이름으로도 걸러 낸다.
        constexpr std::u8string_view remote_head_suffix { u8"/HEAD" };

        struct remote_reference_parts
        {
            std::u8string_view remote {};
            std::u8string_view branch {};
            bool parsed { false };
        };

        remote_reference_parts split_remote_reference(const std::u8string_view name, const std::vector<std::u8string>& remotes) noexcept
        {
            remote_reference_parts parts {};
            if (name.starts_with(remote_reference_prefix) == false)
                return parts;

            const std::u8string_view rest { name.substr(remote_reference_prefix.size()) };
            // remote 이름에도 `/`가 들어갈 수 있으므로 설정된 remote 중 가장 긴 접두사를
            // 고른다. `find_upstream_remote`와 같은 규칙이다.
            std::size_t boundary { 0 };
            for (const std::u8string& remote : remotes)
            {
                if (rest.size() <= remote.size() + 1 || rest.starts_with(remote) == false || rest[remote.size()] != u8'/')
                    continue;
                boundary = std::max(boundary, remote.size());
            }
            if (boundary == 0)
            {
                // 지워진 remote가 남긴 tracking ref다. 첫 `/`로 나눠 후보에는 남긴다.
                const std::size_t separator { rest.find(u8'/') };
                if (separator == std::u8string_view::npos || separator == 0 || separator + 1 >= rest.size())
                    return parts;
                boundary = separator;
            }

            parts.remote = rest.substr(0, boundary);
            parts.branch = rest.substr(boundary + 1);
            parts.parsed = parts.remote.empty() == false && parts.branch.empty() == false;
            return parts;
        }

        const git_local_branch_state* find_local_branch(const std::vector<git_local_branch_state>& branches, const std::u8string_view name) noexcept
        {
            for (const git_local_branch_state& branch : branches)
                if (branch.name == name)
                    return &branch;
            return nullptr;
        }

        // 검증 서비스와 같은 기준으로 고른다. 표시 이름과 tracking 정보는 조회 시점의
        // 값이라 대상을 고르는 데 쓰지 않는다.
        const switch_candidate* find_switch_candidate(const std::vector<switch_candidate>& candidates, const switch_candidate& target) noexcept
        {
            for (const switch_candidate& candidate : candidates)
                if (candidate.kind == target.kind && candidate.target == target.target && candidate.remote_name == target.remote_name)
                    return &candidate;
            return nullptr;
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

    git_repository_provider::git_repository_provider(vcs_tool_info tool, process_runner& runner, const vcs_file_probe& probe, process_output_sink* const log, vcs_timeout_overrides timeouts) noexcept
        : tool_ { std::move(tool) }
        , runner_ { &runner }
        , probe_ { &probe }
        , log_ { log }
        , timeouts_ { timeouts }
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

        const vcs_command_result layout_result { run_vcs_command(*runner_, make_git_layout_request(tool_.executable, working_directory, timeouts_), token, log_) };
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

        const vcs_command_result status_result { run_vcs_command(*runner_, make_git_status_request(tool_.executable, working_directory, timeouts_), token, log_) };
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

        const vcs_command_result remote_result { run_vcs_command(*runner_, make_git_remote_list_request(tool_.executable, working_directory, timeouts_), token, log_) };
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

        const vcs_command_result fetch_result { run_vcs_command(*runner_, make_git_fetch_request(tool_.executable, working_directory, target.remote, timeouts_), token, log_) };
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

        const vcs_command_result verify_result { run_vcs_command(*runner_, make_git_verify_reference_request(tool_.executable, working_directory, target.tracking_reference, timeouts_), token, log_) };
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

        const vcs_command_result count_result { run_vcs_command(*runner_, make_git_ahead_behind_request(tool_.executable, working_directory, target.tracking_reference, timeouts_), token, log_) };
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

    std::u8string select_git_candidate_fetch_remote(const std::vector<std::u8string>& remotes, const std::u8string_view preferred_remote)
    {
        if (remotes.empty())
            return {};
        if (preferred_remote.empty() == false && contains_remote(remotes, preferred_remote))
            return std::u8string { preferred_remote };
        if (contains_remote(remotes, u8"origin"))
            return std::u8string { u8"origin" };
        if (remotes.size() == 1)
            return remotes.front();
        // 여럿 중 하나를 자동으로 고르면 사용자가 의도하지 않은 원격만 새로 고쳐진다.
        // 어느 것도 고르지 않고 이미 받아 둔 목록을 그대로 쓴다.
        return {};
    }

    std::vector<git_local_branch_state> collect_git_local_branches(const std::vector<git_reference_entry>& references)
    {
        std::vector<git_local_branch_state> branches {};
        for (const git_reference_entry& entry : references)
        {
            if (entry.symbolic() || entry.name.starts_with(local_reference_prefix) == false)
                continue;

            git_local_branch_state branch {};
            branch.name = entry.name.substr(local_reference_prefix.size());
            if (branch.name.empty())
                continue;
            // `branch.<name>.remote = .`처럼 local branch를 가리키는 upstream은 원격
            // 전환 판정에 쓸 수 없다. 값이 없는 것과 같이 다룬다.
            if (entry.upstream.starts_with(remote_reference_prefix))
                branch.upstream = entry.upstream;
            branches.push_back(std::move(branch));
        }
        return branches;
    }

    std::vector<switch_candidate> build_git_switch_candidates(const std::vector<git_reference_entry>& references, const std::vector<std::u8string>& remotes, const std::u8string_view refreshed_remote)
    {
        const std::vector<git_local_branch_state> local_branches { collect_git_local_branches(references) };

        std::vector<switch_candidate> remote_candidates {};
        for (const git_reference_entry& entry : references)
        {
            if (entry.symbolic() || entry.name.ends_with(remote_head_suffix))
                continue;

            const remote_reference_parts parts { split_remote_reference(entry.name, remotes) };
            if (parts.parsed == false)
                continue;

            switch_candidate candidate {};
            candidate.kind = switch_candidate_kind::git_remote_branch;
            candidate.display_name = make_display_name(parts.remote, parts.branch);
            candidate.target = entry.name;
            candidate.remote_name = parts.remote;
            if (const git_local_branch_state* const local { find_local_branch(local_branches, parts.branch) }; local != nullptr)
                candidate.local_branch = local->name;
            // 대응하는 local branch가 없으면 만들어야 전환할 수 있다. 실제 생성은
            // 사용자가 확인한 뒤에만 이뤄진다.
            candidate.requires_tracking_branch = candidate.local_branch.empty();
            candidate.stale = refreshed_remote.empty() || parts.remote != refreshed_remote;
            remote_candidates.push_back(std::move(candidate));
        }

        std::vector<switch_candidate> local_candidates {};
        for (const git_local_branch_state& local : local_branches)
        {
            bool reachable { false };
            for (const switch_candidate& candidate : remote_candidates)
            {
                // upstream이 그 remote와 다른 local branch는 remote 후보를 골라도
                // 도달할 수 없다. 그런 branch만 local 후보로 남는다.
                if (candidate.local_branch == local.name && (local.upstream.empty() || local.upstream == candidate.target))
                    reachable = true;
            }
            if (reachable)
                continue;

            switch_candidate candidate {};
            candidate.kind = switch_candidate_kind::git_local_branch;
            candidate.display_name = local.name;
            candidate.target = std::u8string { local_reference_prefix };
            candidate.target.append(local.name);
            candidate.local_branch = local.name;
            local_candidates.push_back(std::move(candidate));
        }

        // remote branch를 먼저, local branch를 다음에 둔다. 두 묶음 안의 순서는
        // `for-each-ref`의 ref 이름 정렬 그대로다.
        std::vector<switch_candidate> candidates { std::move(remote_candidates) };
        candidates.insert(candidates.end(), std::make_move_iterator(local_candidates.begin()), std::make_move_iterator(local_candidates.end()));
        return candidates;
    }

    switch_candidate_result git_repository_provider::query_switch_candidates(const project_definition& project, const process_cancellation_token& token) noexcept
    {
        try
        {
            return query_switch_candidates_impl(project, token);
        }
        catch (...)
        {
            switch_candidate_result result {};
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"전환 후보를 조회하는 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    switch_candidate_result git_repository_provider::query_switch_candidates_impl(const project_definition& project, const process_cancellation_token& token)
    {
        switch_candidate_result result {};
        if (available() == false)
        {
            result.diagnostics.push_back(
                make_diagnostic(diagnostic_code::vcs_tool_not_found, diagnostic_severity::warning, std::u8string { vcs_tool_unavailable_message(repository_kind::git, tool_.availability) }, project));
            return result;
        }

        const std::u8string_view working_directory { git_working_directory(project) };
        if (is_absolute_windows_path(working_directory) == false || probe_->probe(working_directory) != vcs_path_kind::directory)
        {
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::path_missing, diagnostic_severity::error, std::u8string { u8"프로젝트 경로를 찾을 수 없습니다." }, project));
            return result;
        }

        // remote 목록은 새로 고칠 대상을 고르는 데도, tracking ref를 remote와 branch로
        // 나누는 데도 필요하다.
        const vcs_command_result remote_result { run_vcs_command(*runner_, make_git_remote_list_request(tool_.executable, working_directory, timeouts_), token, log_) };
        if (remote_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, remote_result) };
            result.stale = true;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, remote_result), project));
            return result;
        }

        const std::vector<std::u8string> remotes { parse_git_remote_names(remote_result.standard_output_lines) };
        std::u8string refreshed_remote { select_git_candidate_fetch_remote(remotes, project.preferred_remote.value_or(std::u8string {})) };
        if (refreshed_remote.empty() == false)
        {
            const vcs_command_result fetch_result { run_vcs_command(*runner_, make_git_fetch_request(tool_.executable, working_directory, refreshed_remote, timeouts_), token, log_) };
            if (fetch_result.succeeded() == false)
            {
                // 원격을 새로 고치지 못해도 목록 자체는 만든다. 이미 받아 둔 tracking
                // ref로 만든 목록이라는 사실은 `stale`로 알린다.
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, fetch_result) };
                refreshed_remote.clear();
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::warning, describe_failure(failure, fetch_result), project));
            }
        }

        const vcs_command_result reference_result { run_vcs_command(*runner_, make_git_reference_list_request(tool_.executable, working_directory, timeouts_), token, log_) };
        if (reference_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, reference_result) };
            result.stale = true;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, reference_result), project));
            return result;
        }

        result.candidates = build_git_switch_candidates(parse_git_reference_list(reference_result.standard_output_lines), remotes, refreshed_remote);
        // 하나라도 이번에 새로 고치지 않은 remote의 후보가 있으면 목록 전체를 stale로
        // 표시한다. 카드가 "지금 원격을 확인한 목록"이라고 오해하지 않게 한다.
        result.stale = std::ranges::any_of(result.candidates, [](const switch_candidate& candidate) { return candidate.stale; });
        return result;
    }

    update_block_reason evaluate_git_update_preflight(const repository_snapshot& snapshot) noexcept
    {
        if (snapshot.availability != repository_availability::ready)
            return update_block_reason::repository_unavailable;
        if (snapshot.working_tree.state == working_tree_state::conflicted)
            return update_block_reason::working_tree_conflicted;
        if (snapshot.working_tree.operation_in_progress)
            return update_block_reason::operation_in_progress;
        if (snapshot.working_tree.has_index_lock)
            return update_block_reason::index_locked;
        if (snapshot.working_tree.is_detached)
            // 비교할 branch가 없어 fast-forward 대상을 정할 수 없다.
            return update_block_reason::detached_head;
        if (snapshot.working_tree.state != working_tree_state::clean)
            // `modified`와 `unknown`을 함께 막는다. ADR-003의 기본 보호 정책이다.
            return update_block_reason::working_tree_dirty;
        if (snapshot.sync_state == remote_sync_state::diverged)
            return update_block_reason::diverged;
        return update_block_reason::none;
    }

    update_block_reason evaluate_git_submodule_preflight(const std::vector<submodule_status>& submodules) noexcept
    {
        for (const submodule_status& submodule : submodules)
        {
            // 초기화되지 않은 submodule은 위험하지 않다. `--init`이 그대로 처리한다.
            if (submodule.conflicted || submodule.revision_mismatch)
                return update_block_reason::submodule_unsafe;
        }
        return update_block_reason::none;
    }

    repository_change_result git_repository_provider::update(const project_definition& project, const update_options& options, const process_cancellation_token& token) noexcept
    {
        try
        {
            return update_impl(project, options, token);
        }
        catch (...)
        {
            repository_change_result result {};
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::operation_failed, diagnostic_severity::error, std::u8string { u8"업데이트 중 내부 오류가 발생했습니다." }, project));
            return result;
        }
    }

    repository_change_result git_repository_provider::update_impl(const project_definition& project, const update_options& options, const process_cancellation_token& token)
    {
        repository_change_result result {};
        if (available() == false)
        {
            result.blocked_by = update_block_reason::tool_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::update_blocked, diagnostic_severity::warning, std::u8string { update_block_reason_message(result.blocked_by) }, project));
            return result;
        }

        // 사전 검사는 지금 상태로 한다. 카드가 들고 있는 값은 오래됐을 수 있다.
        const repository_query_result before { query_local_impl(project, token) };
        result.snapshot = before.snapshot;
        for (const diagnostic& value : before.diagnostics)
            result.diagnostics.push_back(value);

        result.blocked_by = evaluate_git_update_preflight(before.snapshot);
        if (result.blocked_by != update_block_reason::none)
        {
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::update_blocked, diagnostic_severity::warning, std::u8string { update_block_reason_message(result.blocked_by) }, project));
            return result;
        }

        const std::u8string_view working_directory { git_working_directory(project) };
        const vcs_command_result remote_result { run_vcs_command(*runner_, make_git_remote_list_request(tool_.executable, working_directory, timeouts_), token, log_) };
        if (remote_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, remote_result) };
            result.blocked_by = update_block_reason::no_remote_target;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, remote_result), project));
            return result;
        }

        const std::u8string_view upstream { before.snapshot.comparison == comparison_source::local ? std::u8string_view { before.snapshot.comparison_target } : std::u8string_view {} };
        const git_remote_target target {
            select_git_remote_target(parse_git_remote_names(remote_result.standard_output_lines), before.snapshot.current_reference, upstream, project.preferred_remote.value_or(std::u8string {}),
                before.snapshot.working_tree.is_detached),
        };
        if (target.resolved == false)
        {
            // 어디서 무엇을 당길지 모르면 명령을 만들지 않는다.
            result.blocked_by = update_block_reason::no_remote_target;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::update_blocked, diagnostic_severity::warning, remote_target_missing_message(target.reason), project));
            return result;
        }

        std::vector<submodule_status> submodules {};
        if (options.update_submodules)
        {
            const vcs_command_result submodule_result { run_vcs_command(*runner_, make_git_submodule_status_request(tool_.executable, working_directory, timeouts_), token, log_) };
            if (submodule_result.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, submodule_result) };
                result.blocked_by = update_block_reason::submodule_unsafe;
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, submodule_result), project));
                return result;
            }

            submodules = parse_git_submodule_status(submodule_result.standard_output_lines);
            result.snapshot.submodules = submodules;
            result.blocked_by = evaluate_git_submodule_preflight(submodules);
            if (result.blocked_by != update_block_reason::none)
            {
                // 하나라도 위험하면 parent pull을 시작하지 않는다.
                result.diagnostics.push_back(make_diagnostic(diagnostic_code::update_blocked, diagnostic_severity::warning, std::u8string { update_block_reason_message(result.blocked_by) }, project));
                return result;
            }
        }

        const git_submodule_recursion recursion { options.update_submodules ? git_submodule_recursion::on_demand : git_submodule_recursion::none };
        const vcs_command_result pull_result { run_vcs_command(*runner_, make_git_pull_request(tool_.executable, working_directory, target.remote, target.branch, recursion), token, log_) };
        result.executed = true;
        result.succeeded = pull_result.succeeded();
        if (result.succeeded == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, pull_result) };
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, pull_result), project));
        }

        if (result.succeeded && options.update_submodules)
        {
            // parent가 성공한 경우에만 실행한다. 실패한 pull 뒤에 submodule을 옮기면
            // 되돌리기 어려운 조합이 남는다.
            const vcs_command_result submodule_update { run_vcs_command(*runner_, make_git_submodule_update_request(tool_.executable, working_directory), token, log_) };
            if (submodule_update.succeeded() == false)
            {
                const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, submodule_update) };
                result.succeeded = false;
                result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, submodule_update), project));
            }
        }

        // 성공과 실패 모두 실행 직후 상태를 다시 조회한다. 실행의 성공 여부와 조회 결과는
        // 분리해 보고한다.
        const repository_query_result after { query_local_impl(project, token) };
        result.snapshot = after.snapshot;
        result.snapshot.submodules = std::move(submodules);
        for (const diagnostic& value : after.diagnostics)
            result.diagnostics.push_back(value);
        return result;
    }

    repository_change_result git_repository_provider::switch_to(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token) noexcept
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

    repository_change_result git_repository_provider::switch_to_impl(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token)
    {
        repository_change_result result {};
        const auto reject = [&result, &project](const switch_rejection rejection) {
            result.rejected_by = rejection;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::switch_target_rejected, diagnostic_severity::warning, std::u8string { switch_rejection_message(rejection) }, project));
        };

        if (target.kind == switch_candidate_kind::subversion_url || target.target.empty())
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

        // 검증은 dialog가 들고 있는 값이 아니라 지금 다시 조회한 상태로 한다.
        const repository_query_result before { query_local_impl(project, token) };
        result.snapshot = before.snapshot;
        for (const diagnostic& value : before.diagnostics)
            result.diagnostics.push_back(value);
        if (before.snapshot.availability != repository_availability::ready)
        {
            reject(switch_rejection::repository_unavailable);
            return result;
        }

        const std::u8string_view working_directory { git_working_directory(project) };
        const vcs_command_result remote_result { run_vcs_command(*runner_, make_git_remote_list_request(tool_.executable, working_directory, timeouts_), token, log_) };
        if (remote_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, remote_result) };
            result.rejected_by = switch_rejection::repository_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, remote_result), project));
            return result;
        }

        // 실행 직전에는 fetch하지 않는다. 전환은 이미 받아 둔 ref로만 하며 `--no-guess`가
        // 목록에 없던 대상으로의 암묵 전환을 막는다.
        const vcs_command_result reference_result { run_vcs_command(*runner_, make_git_reference_list_request(tool_.executable, working_directory, timeouts_), token, log_) };
        if (reference_result.succeeded() == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, reference_result) };
            result.rejected_by = switch_rejection::repository_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, reference_result), project));
            return result;
        }

        const vcs_command_result worktree_result { run_vcs_command(*runner_, make_git_worktree_list_request(tool_.executable, working_directory, timeouts_), token, log_) };
        if (worktree_result.succeeded() == false)
        {
            // 어떤 branch가 다른 worktree에 잡혀 있는지 모르는 채로 전환하지 않는다.
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, worktree_result) };
            result.rejected_by = switch_rejection::repository_unavailable;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code_for_failure(failure), diagnostic_severity::error, describe_failure(failure, worktree_result), project));
            return result;
        }

        const std::vector<std::u8string> remotes { parse_git_remote_names(remote_result.standard_output_lines) };
        const std::vector<git_reference_entry> references { parse_git_reference_list(reference_result.standard_output_lines) };
        const std::vector<switch_candidate> candidates { build_git_switch_candidates(references, remotes, {}) };

        git_switch_context context {};
        context.snapshot = before.snapshot;
        context.local_branches = collect_git_local_branches(references);
        context.checked_out_branches = parse_git_worktree_branches(worktree_result.standard_output_lines);

        const switch_validation_result validation { validate_git_switch(candidates, target, context) };
        if (validation.approved == false)
        {
            // 검증에 실패하면 어떤 process request도 만들지 않는다. REQ-007의 수용
            // 기준이며 `executed == false`가 그 사실을 나타낸다.
            result.rejected_by = validation.rejection;
            result.diagnostics.push_back(make_diagnostic(diagnostic_code::switch_target_rejected, diagnostic_severity::warning, validation.message, project));
            return result;
        }

        const switch_candidate* const resolved { find_switch_candidate(candidates, target) };
        if (resolved == nullptr)
        {
            // 검증을 통과했다면 반드시 있다. 방어적으로만 남긴다.
            reject(switch_rejection::target_not_found);
            return result;
        }

        const bool create_tracking_branch { resolved->kind == switch_candidate_kind::git_remote_branch && resolved->local_branch.empty() };
        std::u8string branch {};
        if (create_tracking_branch)
            // `<remote>/<branch>` 표시 이름에서 remote 부분을 뗀 값이 새 local branch 이름이다.
            branch = resolved->display_name.substr(resolved->remote_name.size() + 1);
        else if (resolved->kind == switch_candidate_kind::git_remote_branch)
            branch = resolved->local_branch;
        else
            branch = resolved->display_name;

        process_request request {};
        if (create_tracking_branch)
            request = make_git_create_tracking_branch_request(tool_.executable, working_directory, branch, resolved->target);
        else
            request = make_git_switch_request(tool_.executable, working_directory, branch);

        const vcs_command_result switch_result { run_vcs_command(*runner_, request, token, log_) };
        result.executed = true;
        result.succeeded = switch_result.succeeded();
        if (result.succeeded == false)
        {
            const vcs_failure_kind failure { classify_vcs_failure(repository_kind::git, switch_result) };
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
