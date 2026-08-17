#include "application/discovery_service.h"

#include "domain/path_syntax.h"

#include <algorithm>
#include <utility>

namespace gitman {
    namespace {
        std::u8string join_child_path(const std::u8string_view directory, const std::u8string_view name)
        {
            std::u8string joined { directory };
            if (joined.empty() == false && joined.back() != u8'\\' && joined.back() != u8'/')
                joined.push_back(u8'\\');
            joined.append(name);
            return joined;
        }

        diagnostic make_discovery_diagnostic(const diagnostic_code code, const diagnostic_severity severity, std::u8string message)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = std::move(message);
            return value;
        }

        // 문서에 이미 있는 경로인지 정규화 비교로 판정한다. 비활성 프로젝트도 문서에
        // 있는 항목이므로 함께 중복으로 본다 (계획 4.5).
        bool already_registered(project_path_resolver& resolver, const std::u8string_view normalized, const workspace_document& document)
        {
            if (normalized.empty())
                return false;
            for (const project_definition& project : document.projects)
                if (project.path.normalized.empty() == false && resolver.normalized_equal(normalized, project.path.normalized))
                    return true;
            return false;
        }
    } // namespace

    repository_marker_set collect_repository_markers(const vcs_file_probe& probe, const std::u8string_view absolute_directory)
    {
        repository_marker_set markers {};

        const vcs_path_kind git_entry { probe.probe(join_child_path(absolute_directory, u8".git")) };
        markers.has_git_directory = git_entry == vcs_path_kind::directory;
        markers.has_git_file = git_entry == vcs_path_kind::file;
        markers.has_svn_directory = probe.probe(join_child_path(absolute_directory, u8".svn")) == vcs_path_kind::directory;

        // bare 휴리스틱 표식은 작업 복사본 표식이 하나도 없을 때만 필요하지만, 판정
        // 순서는 순수 함수가 소유하므로 여기서는 값만 채운다.
        markers.has_head_file = probe.probe(join_child_path(absolute_directory, u8"HEAD")) == vcs_path_kind::file;
        markers.has_objects_directory = probe.probe(join_child_path(absolute_directory, u8"objects")) == vcs_path_kind::directory;
        markers.has_refs_directory = probe.probe(join_child_path(absolute_directory, u8"refs")) == vcs_path_kind::directory;
        return markers;
    }

    discovery_service::discovery_service(const directory_enumerator& enumerator, const vcs_file_probe& probe, project_path_resolver& resolver) noexcept
        : enumerator_ { &enumerator }
        , probe_ { &probe }
        , resolver_ { &resolver }
    {}

    discovery_result discovery_service::discover_children(const std::u8string_view scan_root, const workspace_document& document, const process_cancellation_token& token) noexcept
    {
        try
        {
            discovery_result result {};

            if (is_absolute_windows_path(scan_root) == false)
            {
                result.diagnostics.push_back(make_discovery_diagnostic(diagnostic_code::discovery_root_unavailable, diagnostic_severity::error, u8"탐색 루트는 절대 경로여야 합니다."));
                return result;
            }

            // 열거를 시작하기 전에도 취소를 확인한다. 취소된 요청이 filesystem을 건드릴
            // 이유가 없다.
            if (token.cancelled())
            {
                result.diagnostics.push_back(make_discovery_diagnostic(diagnostic_code::discovery_cancelled, diagnostic_severity::warning, u8"탐색이 취소되었습니다."));
                return result;
            }

            const directory_listing listing { enumerator_->enumerate(scan_root) };
            if (listing.succeeded == false)
            {
                diagnostic failure { make_discovery_diagnostic(diagnostic_code::discovery_root_unavailable, diagnostic_severity::error, u8"탐색 루트를 열거할 수 없습니다.") };
                failure.native_error = listing.native_error;
                result.diagnostics.push_back(std::move(failure));
                return result;
            }

            if (listing.unreadable_name_count > 0)
                result.diagnostics.push_back(
                    make_discovery_diagnostic(diagnostic_code::discovery_child_skipped, diagnostic_severity::warning, u8"UTF-8로 표현할 수 없는 이름의 항목이 목록에서 제외되었습니다."));

            // 저장소 내부를 루트로 지정한 실수를 UI가 알릴 수 있게 루트 자체도 판정한다.
            // bare 저장소 역시 저장소이므로 함께 참으로 본다. 루트는 후보가 아니다.
            const discovery_classification root_classification { classify_discovery_markers(collect_repository_markers(*probe_, scan_root)) };
            result.root_is_repository = root_classification.exclusion == discovery_exclusion::none || root_classification.exclusion == discovery_exclusion::bare_repository;

            bool cancelled { false };
            for (const directory_entry& entry : listing.entries)
            {
                if (entry.is_directory == false)
                    continue;

                // 취소는 자식 경계에서만 확인한다. 진행 중인 표식 확인 하나를 끊을
                // 수단은 없으며, 다음 자식으로 넘어가지 않는 것까지를 보장한다.
                if (token.cancelled())
                {
                    cancelled = true;
                    break;
                }

                discovery_candidate candidate {};
                candidate.directory_name = entry.name;
                candidate.absolute_path = join_child_path(scan_root, entry.name);

                const project_path_resolution resolution { resolver_->resolve(candidate.absolute_path, document.document_path) };
                candidate.normalized_path = resolution.normalized;

                if (entry.is_reparse_point)
                {
                    // 링크는 판정하지 않는다 (계획 4.3). 이중 등록과 cycle을 막는 정책
                    // 제외이며 목록에는 남겨 사용자가 이유를 볼 수 있게 한다.
                    candidate.exclusion = discovery_exclusion::reparse_point;
                }
                else
                {
                    const discovery_classification classification { classify_discovery_markers(collect_repository_markers(*probe_, candidate.absolute_path)) };
                    candidate.kind = classification.kind;
                    candidate.via_git_file = classification.via_git_file;
                    candidate.exclusion = classification.exclusion;

                    // 중복 표시는 선택 가능한 후보에만 씌운다. 이미 다른 사유로 제외된
                    // 항목의 사유를 덮으면 사용자가 실제 원인을 알 수 없다.
                    if (candidate.exclusion == discovery_exclusion::none && already_registered(*resolver_, candidate.normalized_path, document))
                        candidate.exclusion = discovery_exclusion::already_registered;
                }

                result.candidates.push_back(std::move(candidate));
            }

            std::sort(result.candidates.begin(), result.candidates.end(), discovery_candidate_before);

            if (cancelled)
            {
                result.diagnostics.push_back(make_discovery_diagnostic(diagnostic_code::discovery_cancelled, diagnostic_severity::warning, u8"탐색이 취소되어 일부 자식만 확인했습니다."));
                return result;
            }

            result.completed = true;
            return result;
        }
        catch (...)
        {
            discovery_result failure {};
            failure.diagnostics.push_back(make_discovery_diagnostic(diagnostic_code::discovery_root_unavailable, diagnostic_severity::error, u8"탐색 중 내부 오류가 발생했습니다."));
            return failure;
        }
    }
} // namespace gitman
