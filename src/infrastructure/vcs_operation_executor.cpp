#include "infrastructure/vcs_operation_executor.h"

#include "application/discovery_service.h"
#include "application/project_registration_service.h"
#include "application/version_list_generation_service.h"
#include "domain/discovery.h"
#include "infrastructure/git_repository_provider.h"
#include "infrastructure/svn_repository_provider.h"

#include <chrono>
#include <cstddef>
#include <future>
#include <utility>
#include <vector>

namespace gitman {
    namespace {
        query_completed_event make_query_event(const operation_request& request, const bool remote, const bool final_event, repository_query_result result)
        {
            query_completed_event event {};
            event.operation_id = request.operation_id;
            event.generation = request.generation;
            event.id = request.project.id;
            event.remote = remote;
            event.final_event = final_event;
            event.result = std::move(result);
            return event;
        }

        // 배치 하나로 보내는 로그 record 수다. record마다 메시지를 만들면 대용량
        // 출력에서 logic inbox가 넘치므로 모아서 보낸다 (stage-7-plan 4.1).
        constexpr std::size_t operation_log_flush_count { 16 };

        // provider의 log sink 자리에 꽂혀 프로세스 출력을 operation_log_event로
        // 옮기는 sink다. runner가 호출을 직렬화하므로 스스로 동기화하지 않는다.
        class operation_log_forwarder final : public process_output_sink
        {
        public:
            operation_log_forwarder(const operation_request& request, const std::function<void(logic_message)>& emit) noexcept
                : request_ { &request }
                , emit_ { &emit }
            {}

            void on_record(const process_output_record& record) override
            {
                operation_log_entry entry {};
                entry.kind = record.stream == process_stream::standard_error ? log_entry_kind::standard_error : log_entry_kind::standard_output;
                entry.severity = diagnostic_severity::information;
                entry.text = record.text;
                entry.progress = record.progress;
                entry.time = std::chrono::system_clock::now();
                entries_.push_back(std::move(entry));
                if (entries_.size() >= operation_log_flush_count)
                    flush();
            }

            void flush()
            {
                if (entries_.empty())
                    return;

                operation_log_event event {};
                event.operation_id = request_->operation_id;
                event.id = request_->project.id;
                event.entries = std::move(entries_);
                entries_ = {};
                (*emit_)(std::move(event));
            }

        private:
            const operation_request* request_ { nullptr };
            const std::function<void(logic_message)>* emit_ { nullptr };
            std::vector<operation_log_entry> entries_ {};
        };

        change_completed_event make_change_event(const operation_request& request, repository_change_result result)
        {
            change_completed_event event {};
            event.operation_id = request.operation_id;
            event.generation = request.generation;
            event.id = request.project.id;
            event.kind = request.kind;
            event.result = std::move(result);
            return event;
        }
    } // namespace

    vcs_operation_executor::vcs_operation_executor(project_store& store, process_runner& runner, const vcs_file_probe& probe, vcs_tool_environment environment) noexcept
        : store_ { &store }
        , runner_ { &runner }
        , probe_ { &probe }
        , environment_ { std::move(environment) }
    {}

    vcs_operation_executor::vcs_operation_executor(
        project_store& store, process_runner& runner, const vcs_file_probe& probe, const directory_enumerator& enumerator, project_path_resolver& resolver, vcs_tool_environment environment) noexcept
        : store_ { &store }
        , runner_ { &runner }
        , probe_ { &probe }
        , enumerator_ { &enumerator }
        , resolver_ { &resolver }
        , environment_ { std::move(environment) }
    {}

    void vcs_operation_executor::execute(const operation_request& request, const std::function<void(logic_message)>& emit) noexcept
    {
        try
        {
            if (request.kind == operation_kind::load_document)
            {
                project_store_load_result loaded { store_->load(request.document_path) };
                document_loaded_event event {};
                event.operation_id = request.operation_id;
                event.document = std::move(loaded.document);
                event.revision = loaded.revision;
                event.diagnostics = std::move(loaded.diagnostics);
                emit(std::move(event));
                return;
            }

            if (request.kind == operation_kind::generate_document)
            {
                execute_generate_document(request, emit);
                return;
            }

            if (request.kind == operation_kind::save_document)
            {
                document_saved_event event {};
                event.operation_id = request.operation_id;
                if (request.document.has_value())
                {
                    project_store_save_result saved { store_->save(request.document_path, *request.document, request.revision) };
                    event.revision = std::move(saved.revision);
                    event.diagnostics = std::move(saved.diagnostics);
                }
                else
                {
                    diagnostic value {};
                    value.code = diagnostic_code::operation_failed;
                    value.severity = diagnostic_severity::error;
                    value.message = u8"저장할 문서 내용이 요청에 없습니다.";
                    event.diagnostics.push_back(std::move(value));
                }
                emit(std::move(event));
                return;
            }

            if (request.kind == operation_kind::discover_projects)
            {
                execute_discover_projects(request, emit);
                return;
            }

            if (request.kind == operation_kind::register_projects)
            {
                execute_register_projects(request, emit);
                return;
            }

            if (request.kind == operation_kind::update || request.kind == operation_kind::switch_to)
            {
                execute_change(request, emit);
                return;
            }

            if (request.kind == operation_kind::query_switch_candidates)
            {
                execute_switch_candidates(request, emit);
                return;
            }

            if (request.kind == operation_kind::query_svn_directory)
            {
                execute_svn_directory(request, emit);
                return;
            }

            if (request.kind == operation_kind::query_local_changes)
            {
                execute_local_changes(request, emit);
                return;
            }

            if (request.kind == operation_kind::query_file_diff)
            {
                execute_file_diff(request, emit);
                return;
            }

            execute_query(request, emit);
        }
        catch (...)
        {
            // logic의 busy·저장 대기 상태는 마지막 event로만 풀린다. 어떤 실패에서도
            // 종류에 맞는 final event를 보낸다.
            diagnostic value {};
            value.code = diagnostic_code::operation_failed;
            value.severity = diagnostic_severity::error;
            value.message = u8"작업 실행 중 내부 오류가 발생했습니다.";
            try
            {
                if (request.kind == operation_kind::save_document)
                {
                    document_saved_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::load_document)
                {
                    document_loaded_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::generate_document)
                {
                    document_generated_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.document_path = request.document_path;
                    failure.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::discover_projects)
                {
                    discovery_completed_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.result.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::register_projects)
                {
                    projects_registered_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::update || request.kind == operation_kind::switch_to)
                {
                    change_completed_event failure { make_change_event(request, {}) };
                    failure.result.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::query_switch_candidates)
                {
                    switch_candidates_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.id = request.project.id;
                    failure.result.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::query_svn_directory)
                {
                    svn_directory_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.id = request.project.id;
                    failure.url = request.svn_directory_url;
                    failure.result.error = svn_browser_query_error::failed;
                    failure.result.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::query_local_changes)
                {
                    local_changes_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.id = request.project.id;
                    failure.result.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else if (request.kind == operation_kind::query_file_diff)
                {
                    file_diff_event failure {};
                    failure.operation_id = request.operation_id;
                    failure.id = request.project.id;
                    failure.result.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
                else
                {
                    query_completed_event failure { make_query_event(request, false, true, {}) };
                    failure.result.diagnostics.push_back(std::move(value));
                    emit(std::move(failure));
                }
            }
            catch (...)
            {}
        }
    }

    void vcs_operation_executor::execute_change(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        const vcs_tool_set tools { tools_for(request.settings, request.token) };
        const repository_kind kind { decide_kind(request.project) };

        // switch 대상이 없는 요청은 명령을 만들지 않고 거부한다. dialog가 채우는
        // 값이므로 정상 경로에서는 도달하지 않는다.
        if (request.kind == operation_kind::switch_to && request.switch_target.has_value() == false)
        {
            repository_change_result rejected {};
            rejected.rejected_by = switch_rejection::target_not_found;
            diagnostic value {};
            value.code = diagnostic_code::switch_target_rejected;
            value.severity = diagnostic_severity::error;
            value.message = u8"전환 대상이 요청에 없습니다.";
            rejected.diagnostics.push_back(std::move(value));
            emit(make_change_event(request, std::move(rejected)));
            return;
        }

        // 사전 검사 조회를 포함한 이 작업의 모든 프로세스 출력이 카드 로그로 간다.
        operation_log_forwarder log { request, emit };
        repository_change_result result {};
        if (kind == repository_kind::subversion)
        {
            svn_repository_provider provider { tools.subversion, *runner_, *probe_, &log, vcs_timeouts_from_settings(request.settings), request.settings.ignore_local_changes };
            result = request.kind == operation_kind::update ? provider.update(request.project, request.options, request.token)
                                                            : provider.switch_to(request.project, *request.switch_target, request.token);
        }
        else
        {
            git_repository_provider provider { tools.git, *runner_, *probe_, &log, vcs_timeouts_from_settings(request.settings) };
            result = request.kind == operation_kind::update ? provider.update(request.project, request.options, request.token)
                                                            : provider.switch_to(request.project, *request.switch_target, request.token);
        }

        // 마지막 event인 change_completed보다 로그가 먼저 도착해야 한다.
        log.flush();
        emit(make_change_event(request, std::move(result)));
    }

    void vcs_operation_executor::execute_switch_candidates(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        const vcs_tool_set tools { tools_for(request.settings, request.token) };
        const repository_kind kind { decide_kind(request.project) };

        switch_candidates_event event {};
        event.operation_id = request.operation_id;
        event.id = request.project.id;
        if (kind == repository_kind::subversion)
        {
            svn_repository_provider provider { tools.subversion, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
            event.result = provider.query_switch_candidates(request.project, request.token);
        }
        else
        {
            git_repository_provider provider { tools.git, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
            event.result = provider.query_switch_candidates(request.project, request.token);
        }
        emit(std::move(event));
    }

    void vcs_operation_executor::execute_svn_directory(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        const vcs_tool_set tools { tools_for(request.settings, request.token) };
        svn_directory_event event {};
        event.operation_id = request.operation_id;
        event.id = request.project.id;
        event.url = request.svn_directory_url;

        svn_repository_provider provider { tools.subversion, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
        event.result = provider.query_directory(request.project, request.svn_repository_root_url, request.svn_directory_url, request.token);
        emit(std::move(event));
    }

    void vcs_operation_executor::execute_local_changes(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        const vcs_tool_set tools { tools_for(request.settings, request.token) };
        const repository_kind kind { decide_kind(request.project) };

        local_changes_event event {};
        event.operation_id = request.operation_id;
        event.id = request.project.id;
        if (kind == repository_kind::subversion)
        {
            svn_repository_provider provider { tools.subversion, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
            event.result = provider.query_local_changes(request.project, request.token);
        }
        else
        {
            git_repository_provider provider { tools.git, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
            event.result = provider.query_local_changes(request.project, request.token);
        }
        emit(std::move(event));
    }

    void vcs_operation_executor::execute_file_diff(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        file_diff_event event {};
        event.operation_id = request.operation_id;
        event.id = request.project.id;

        if (request.diff_target.has_value() == false)
        {
            diagnostic value {};
            value.code = diagnostic_code::operation_failed;
            value.severity = diagnostic_severity::error;
            value.message = u8"diff 대상이 요청에 없습니다.";
            event.result.diagnostics.push_back(std::move(value));
            emit(std::move(event));
            return;
        }

        const vcs_tool_set tools { tools_for(request.settings, request.token) };
        const repository_kind kind { decide_kind(request.project) };
        if (kind == repository_kind::subversion)
        {
            svn_repository_provider provider { tools.subversion, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
            event.result = provider.query_file_diff(request.project, *request.diff_target, request.token);
        }
        else
        {
            git_repository_provider provider { tools.git, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
            event.result = provider.query_file_diff(request.project, *request.diff_target, request.token);
        }
        emit(std::move(event));
    }

    void vcs_operation_executor::execute_generate_document(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        document_generated_event event {};
        event.operation_id = request.operation_id;
        event.document_path = request.document_path;

        if (enumerator_ == nullptr || resolver_ == nullptr)
        {
            diagnostic value {};
            value.code = diagnostic_code::generation_failed;
            value.severity = diagnostic_severity::error;
            value.message = u8"이 조립은 .version-list 생성을 지원하지 않습니다.";
            event.diagnostics.push_back(std::move(value));
            emit(std::move(event));
            return;
        }

        discovery_service discovery { *enumerator_, *probe_, *resolver_ };
        project_registration_service registration { *store_, *resolver_ };
        version_list_generation_service generator { discovery, registration, *store_ };
        version_list_generation_result generated { generator.generate(request.scan_root, request.document_path, request.token) };
        event.document = std::move(generated.document);
        event.revision = std::move(generated.revision);
        event.diagnostics = std::move(generated.diagnostics);
        emit(std::move(event));
    }

    void vcs_operation_executor::execute_discover_projects(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        discovery_completed_event event {};
        event.operation_id = request.operation_id;

        if (enumerator_ == nullptr || resolver_ == nullptr || request.document.has_value() == false)
        {
            diagnostic value {};
            value.code = diagnostic_code::operation_failed;
            value.severity = diagnostic_severity::error;
            value.message
                = enumerator_ == nullptr || resolver_ == nullptr ? std::u8string { u8"이 조립은 저장소 탐색을 지원하지 않습니다." } : std::u8string { u8"탐색할 문서 내용이 요청에 없습니다." };
            event.result.diagnostics.push_back(std::move(value));
            emit(std::move(event));
            return;
        }

        discovery_service discovery { *enumerator_, *probe_, *resolver_ };
        event.result = discovery.discover_children(request.scan_root, *request.document, request.token);
        emit(std::move(event));
    }

    void vcs_operation_executor::execute_register_projects(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        projects_registered_event event {};
        event.operation_id = request.operation_id;

        if (resolver_ == nullptr || request.document.has_value() == false)
        {
            diagnostic value {};
            value.code = diagnostic_code::operation_failed;
            value.severity = diagnostic_severity::error;
            value.message = resolver_ == nullptr ? std::u8string { u8"이 조립은 선택 등록을 지원하지 않습니다." } : std::u8string { u8"등록할 문서 내용이 요청에 없습니다." };
            event.diagnostics.push_back(std::move(value));
            emit(std::move(event));
            return;
        }

        project_registration_service registration { *store_, *resolver_ };
        project_registration_result registered { registration.register_candidates(*request.document, request.revision, request.discovery_selection) };
        event.document = std::move(registered.saved_document);
        event.revision = std::move(registered.revision);
        event.diagnostics = std::move(registered.diagnostics);
        emit(std::move(event));
    }

    void vcs_operation_executor::execute_query(const operation_request& request, const std::function<void(logic_message)>& emit)
    {
        const vcs_tool_set tools { tools_for(request.settings, request.token) };
        const repository_kind kind { decide_kind(request.project) };

        if (kind == repository_kind::subversion)
        {
            svn_repository_provider provider { tools.subversion, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings), request.settings.ignore_local_changes };
            if (request.kind == operation_kind::query_local)
            {
                emit(make_query_event(request, false, true, provider.query_local(request.project, request.token)));
                return;
            }

            // refresh는 `svn info`까지만 먼저 받고, 대형 작업 복사본에서 분 단위로 걸리는
            // status 순회와 원격 조회를 병렬로 돌린다.
            repository_query_result local { provider.query_local_metadata(request.project, request.token) };
            if (local.snapshot.availability != repository_availability::ready)
            {
                // metadata 단계에서 실패하면 기존 순서 그대로 처리한다. 원격 조회도 ready가
                // 아닌 로컬 상태를 보고 즉시 물러난다.
                emit(make_query_event(request, false, false, local));
                emit(make_query_event(request, true, true, provider.query_remote(request.project, local.snapshot, request.token)));
                return;
            }

            {
                // status 순회가 도는 동안 카드가 "로컬 변경 확인 중"을 표시하도록 metadata
                // 결과를 중간 event로 먼저 게시한다.
                repository_query_result scanning { local };
                scanning.snapshot.working_tree_scan_pending = true;
                emit(make_query_event(request, false, false, std::move(scanning)));
            }

            // 원격 조회는 별도 thread에서 진행하고 끝나는 즉시 중간 event로 게시해 로컬
            // 순회가 끝나기 전에도 원격 상태가 보이게 한다. logic inbox 게시는 worker들이
            // 이미 동시에 하고 있으므로 다른 thread에서 불러도 안전하다.
            const auto remote_query = [&provider, &request, &emit, snapshot = local.snapshot] {
                repository_query_result remote { provider.query_remote(request.project, snapshot, request.token) };
                repository_query_result preview { remote };
                preview.snapshot.working_tree_scan_pending = true;
                emit(make_query_event(request, true, false, std::move(preview)));
                return remote;
            };
            std::future<repository_query_result> remote_future { std::async(std::launch::async, remote_query) };

            // status 순회는 이 thread에서 이어 간다. 로컬 변경을 상관하지 않는 설정이면
            // provider가 순회를 건너뛰고 즉시 돌아온다.
            provider.finish_local_query(local, request.project, request.token);

            repository_query_result merged { remote_future.get() };
            // 원격 결과의 snapshot은 metadata 기반이므로 방금 끝난 로컬 결과를 합쳐
            // 최종본을 만든다. controller는 event의 snapshot으로 카드를 통째로 바꾼다.
            // availability는 나쁜 쪽이 이긴다. 원격 조회가 경로 소실(path_unavailable)을
            // 발견했는데 로컬 단계의 ready로 되돌리면 없는 경로가 ready로 보인다.
            if (merged.snapshot.availability == repository_availability::ready)
                merged.snapshot.availability = local.snapshot.availability;
            merged.snapshot.working_tree = local.snapshot.working_tree;
            merged.snapshot.has_switched_subtree = local.snapshot.has_switched_subtree;
            merged.snapshot.has_mixed_revision = local.snapshot.has_mixed_revision;
            merged.snapshot.local_checked_at = local.snapshot.local_checked_at;
            for (const diagnostic& value : local.diagnostics)
                merged.diagnostics.push_back(value);
            emit(make_query_event(request, true, true, std::move(merged)));
            return;
        }

        // automatic에서 표식이 없는 경로도 Git provider가 `not_a_repository`를 그대로
        // 보고하므로 기본 provider는 Git이다.
        git_repository_provider provider { tools.git, *runner_, *probe_, nullptr, vcs_timeouts_from_settings(request.settings) };
        repository_query_result local { provider.query_local(request.project, request.token) };
        if (request.kind == operation_kind::query_local)
        {
            emit(make_query_event(request, false, true, std::move(local)));
            return;
        }
        emit(make_query_event(request, false, false, local));
        emit(make_query_event(request, true, true, provider.query_remote(request.project, local.snapshot, request.token)));
    }

    vcs_tool_set vcs_operation_executor::tools_for(const workspace_settings& settings, const process_cancellation_token& token)
    {
        {
            const std::lock_guard<std::mutex> lock { cache_mutex_ };
            if (cache_valid_ && cached_git_setting_ == settings.git_executable && cached_svn_setting_ == settings.svn_executable)
                return cached_tools_;
        }

        // 조사 자체는 잠금 밖에서 한다. 프로세스 실행을 잠금 아래 두면 다른 worker의
        // cache 조회까지 같이 멈춘다. 동시에 두 worker가 조사하면 늦게 끝난 쪽이
        // cache를 덮지만 결과는 같다.
        const vcs_tool_set tools { resolve_vcs_tools(settings, environment_, *runner_, *probe_, token) };

        const std::lock_guard<std::mutex> lock { cache_mutex_ };
        cache_valid_ = true;
        cached_git_setting_ = settings.git_executable;
        cached_svn_setting_ = settings.svn_executable;
        cached_tools_ = tools;
        return tools;
    }

    repository_kind vcs_operation_executor::decide_kind(const project_definition& project) const
    {
        if (project.hint == vcs_hint::git)
            return repository_kind::git;
        if (project.hint == vcs_hint::subversion)
            return repository_kind::subversion;

        // automatic은 단계 5의 표식 판정을 재사용한다. 프로세스 없이 종류만 정하고,
        // 정확한 상태는 provider 조회가 판정한다.
        const std::u8string_view path { project.path.normalized.empty() ? std::u8string_view { project.path.original } : std::u8string_view { project.path.normalized } };
        const discovery_classification classification { classify_discovery_markers(collect_repository_markers(*probe_, path)) };
        if (classification.kind == repository_kind::subversion)
            return repository_kind::subversion;
        return repository_kind::git;
    }
} // namespace gitman
