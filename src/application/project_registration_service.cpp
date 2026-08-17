#include "application/project_registration_service.h"

#include "domain/path_syntax.h"

#include <algorithm>
#include <utility>

namespace gitman {
    namespace {
        bool id_taken(const std::u8string_view candidate, const std::vector<std::u8string>& taken)
        {
            return std::ranges::any_of(taken, [candidate](const std::u8string& value) { return value == candidate; });
        }

        diagnostic make_rejection(std::u8string message, const std::u8string_view candidate_name)
        {
            diagnostic value {};
            value.code = diagnostic_code::registration_candidate_rejected;
            value.severity = diagnostic_severity::error;
            value.message = std::move(message);
            value.source.project_id = { std::u8string { candidate_name } };
            return value;
        }

        vcs_hint hint_of(const repository_kind kind) noexcept
        {
            return kind == repository_kind::git ? vcs_hint::git : vcs_hint::subversion;
        }
    } // namespace

    std::u8string make_unique_registration_id(const std::u8string_view directory_name, const std::vector<std::u8string>& taken)
    {
        std::u8string base { directory_name };
        if (id_taken(base, taken) == false)
            return base;

        // 숫자 접미사는 2부터 시작한다. `name`이 이미 있을 때 다음 항목이 `name-2`인
        // 편이 "두 번째 항목"이라는 뜻으로 읽히기 때문이다.
        for (std::uint64_t suffix = 2;; ++suffix)
        {
            std::u8string candidate { base };
            candidate.push_back(u8'-');
            const std::string digits { std::to_string(suffix) };
            candidate.append(digits.begin(), digits.end());
            if (id_taken(candidate, taken) == false)
                return candidate;
        }
    }

    bool project_registration_result::has_errors() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::error; });
    }

    project_registration_service::project_registration_service(project_store& store, project_path_resolver& resolver) noexcept
        : store_ { &store }
        , resolver_ { &resolver }
    {}

    project_registration_result project_registration_service::register_candidates(
        const workspace_document& document, const workspace_revision_token& expected_revision, const std::span<const discovery_candidate> selected) noexcept
    {
        try
        {
            project_registration_result result {};

            if (selected.empty())
            {
                result.diagnostics.push_back(make_rejection(u8"등록할 후보가 선택되지 않았습니다.", {}));
                return result;
            }

            // 저장을 호출하기 전에 선택 전체를 다시 검증한다. dialog 검증과 등록 사이에
            // 문서나 선택이 달라졌을 수 있고, 부적격 후보가 하나라도 있으면 아무것도
            // 저장하지 않는다.
            std::vector<project_path_resolution> resolutions {};
            resolutions.reserve(selected.size());
            for (const discovery_candidate& candidate : selected)
            {
                if (candidate.directory_name.empty() || is_absolute_windows_path(candidate.absolute_path) == false)
                {
                    result.diagnostics.push_back(make_rejection(u8"후보의 경로가 올바르지 않습니다.", candidate.directory_name));
                    resolutions.push_back({});
                    continue;
                }
                if (candidate.selectable() == false)
                {
                    std::u8string message { u8"제외된 후보는 등록할 수 없습니다: " };
                    message.append(discovery_exclusion_name(candidate.exclusion));
                    result.diagnostics.push_back(make_rejection(std::move(message), candidate.directory_name));
                    resolutions.push_back({});
                    continue;
                }
                if (candidate.kind != repository_kind::git && candidate.kind != repository_kind::subversion)
                {
                    result.diagnostics.push_back(make_rejection(u8"저장소 종류를 판정하지 못한 후보는 등록할 수 없습니다.", candidate.directory_name));
                    resolutions.push_back({});
                    continue;
                }

                // 탐색 시점의 정규화 값을 쓰지 않고 등록 시점에 다시 해석한다. 문서
                // 기준 경로가 다른 문서로 등록될 수도 있고, 오래된 값으로 중복 검사를
                // 통과시키면 안 된다.
                project_path_resolution resolution { resolver_->resolve(candidate.absolute_path, document.document_path) };

                for (const project_definition& project : document.projects)
                {
                    if (project.path.normalized.empty() == false && resolution.normalized.empty() == false && resolver_->normalized_equal(resolution.normalized, project.path.normalized))
                    {
                        result.diagnostics.push_back(make_rejection(u8"이미 문서에 등록된 경로입니다.", candidate.directory_name));
                        break;
                    }
                }

                for (const project_path_resolution& earlier : resolutions)
                {
                    if (earlier.normalized.empty() == false && resolution.normalized.empty() == false && resolver_->normalized_equal(resolution.normalized, earlier.normalized))
                    {
                        result.diagnostics.push_back(make_rejection(u8"선택 목록 안에서 경로가 중복됩니다.", candidate.directory_name));
                        break;
                    }
                }

                resolutions.push_back(std::move(resolution));
            }

            if (result.has_errors())
                return result;

            std::vector<std::u8string> taken_ids {};
            taken_ids.reserve(document.projects.size() + selected.size());
            for (const project_definition& project : document.projects)
                taken_ids.push_back(project.id.value);

            workspace_document updated { document };
            for (std::size_t index = 0; index < selected.size(); ++index)
            {
                const discovery_candidate& candidate { selected[index] };

                project_definition definition {};
                definition.id.value = make_unique_registration_id(candidate.directory_name, taken_ids);
                taken_ids.push_back(definition.id.value);
                definition.display_name = candidate.directory_name;
                definition.path.original = candidate.absolute_path;
                definition.path.normalized = resolutions[index].normalized;
                definition.path.state = resolutions[index].state;
                // 탐색이 이미 종류를 확정했으므로 `automatic`으로 되돌려 provider가
                // 다시 추측하게 하지 않는다 (계획 4.7).
                definition.hint = hint_of(candidate.kind);
                definition.enabled = true;
                updated.projects.push_back(std::move(definition));
            }

            project_store_save_result saved { store_->save(document.document_path, updated, expected_revision) };
            result.diagnostics.insert(result.diagnostics.end(), saved.diagnostics.begin(), saved.diagnostics.end());
            if (saved.succeeded() == false)
                return result;

            result.succeeded = true;
            result.saved_document = { std::move(updated) };
            result.revision = std::move(saved.revision);
            return result;
        }
        catch (...)
        {
            project_registration_result failure {};
            failure.diagnostics.push_back(make_rejection(u8"등록 중 내부 오류가 발생했습니다.", {}));
            return failure;
        }
    }
} // namespace gitman
