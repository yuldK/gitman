#include "application/version_list_generation_service.h"

#include "domain/path_syntax.h"
#include "domain/project.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>

namespace gitman {
    namespace {
        diagnostic make_generation_diagnostic(const diagnostic_code code, const diagnostic_severity severity, std::u8string message, const std::u8string_view document_path)
        {
            diagnostic value {};
            value.code = code;
            value.severity = severity;
            value.message = std::move(message);
            value.source.document_path = document_path;
            return value;
        }

        char8_t ascii_lower(const char8_t value) noexcept
        {
            if (value >= u8'A' && value <= u8'Z')
                return static_cast<char8_t>(value - u8'A' + u8'a');
            return value;
        }

        // platform 계층의 `has_workspace_document_extension`과 같은 규칙이다. 이
        // 계층은 Win32에 의존하지 않으므로 도메인 상수로 직접 판정한다.
        bool ends_with_document_extension(const std::u8string_view path) noexcept
        {
            if (path.size() <= workspace_document_extension.size())
                return false;
            const std::size_t offset { path.size() - workspace_document_extension.size() };
            for (std::size_t index = 0; index < workspace_document_extension.size(); ++index)
                if (ascii_lower(path[offset + index]) != workspace_document_extension[index])
                    return false;
            return true;
        }

        // "파일이 없는 새 경로"의 load 결과인지 판정한다. 그 외의 진단이
        // 하나라도 섞여 있으면 출력 경로의 상태를 신뢰할 수 없어 생성을 거부한다.
        bool output_is_cleanly_missing(const project_store_load_result& loaded) noexcept
        {
            bool missing { false };
            for (const diagnostic& value : loaded.diagnostics)
            {
                if (value.code != diagnostic_code::document_not_found)
                    return false;
                missing = true;
            }
            return missing;
        }
    } // namespace

    bool version_list_generation_result::has_errors() const noexcept
    {
        return std::ranges::any_of(diagnostics, [](const diagnostic& value) { return value.severity == diagnostic_severity::error; });
    }

    version_list_generation_service::version_list_generation_service(discovery_service& discovery, project_registration_service& registration, project_store& store) noexcept
        : discovery_ { &discovery }
        , registration_ { &registration }
        , store_ { &store }
    {}

    version_list_generation_result version_list_generation_service::generate(
        const std::u8string_view scan_root, const std::u8string_view document_path, const process_cancellation_token& token) noexcept
    {
        try
        {
            version_list_generation_result result {};
            if (is_absolute_windows_path(scan_root) == false || is_absolute_windows_path(document_path) == false || ends_with_document_extension(document_path) == false)
            {
                result.diagnostics.push_back(make_generation_diagnostic(
                    diagnostic_code::generation_request_invalid, diagnostic_severity::error, u8"스캔 폴더와 출력 문서는 절대 경로여야 하고 출력 문서는 .version-list로 끝나야 합니다.", document_path));
                return result;
            }

            // 새 문서 생성은 기존 파일을 조용히 덮어쓰지 않는다. 여기서 얻은 missing
            // revision으로 저장하므로, 확인과 저장 사이에 파일이 생겨도 store의 낙관적
            // 잠금이 충돌로 거른다.
            const project_store_load_result existing { store_->load(document_path) };
            if (existing.document.has_value() || output_is_cleanly_missing(existing) == false)
            {
                result.diagnostics.push_back(make_generation_diagnostic(
                    diagnostic_code::generation_output_exists, diagnostic_severity::error, u8"같은 이름의 .version-list 문서가 이미 있거나 상태를 확인할 수 없습니다.", document_path));
                return result;
            }

            workspace_document document {};
            document.document_path = document_path;
            const discovery_result discovered { discovery_->discover_children(scan_root, document, token) };
            result.diagnostics.insert(result.diagnostics.end(), discovered.diagnostics.begin(), discovered.diagnostics.end());
            if (discovered.completed == false || result.has_errors())
                return result;

            std::vector<discovery_candidate> selected {};
            selected.reserve(discovered.candidates.size());
            for (const discovery_candidate& candidate : discovered.candidates)
                if (candidate.selectable() && (candidate.kind == repository_kind::git || candidate.kind == repository_kind::subversion))
                    selected.push_back(candidate);
            if (selected.empty())
            {
                std::u8string message {};
                if (discovered.root_is_repository)
                    message = u8"지정한 폴더 자체가 저장소입니다. 저장소들을 담은 상위 폴더를 지정하세요.";
                else
                    message = u8"하위 폴더에서 저장소를 찾지 못해 문서를 만들지 않았습니다.";
                result.diagnostics.push_back(make_generation_diagnostic(diagnostic_code::generation_no_repositories, diagnostic_severity::warning, std::move(message), document_path));
                return result;
            }

            // 검증, 유일 id 부여, 원자적 저장은 등록 service의 규칙을 그대로 쓴다.
            // 빈 문서 + missing revision이므로 저장이 곧 신규 파일 생성이다.
            project_registration_result registered { registration_->register_candidates(document, existing.revision, selected) };
            result.diagnostics.insert(result.diagnostics.end(), std::make_move_iterator(registered.diagnostics.begin()), std::make_move_iterator(registered.diagnostics.end()));
            if (registered.succeeded == false)
                return result;

            result.succeeded = true;
            result.document = std::move(registered.saved_document);
            result.revision = std::move(registered.revision);
            return result;
        }
        catch (...)
        {
            version_list_generation_result failure {};
            failure.diagnostics.push_back(
                make_generation_diagnostic(diagnostic_code::generation_failed, diagnostic_severity::error, u8".version-list 생성 중 내부 오류가 발생했습니다.", document_path));
            return failure;
        }
    }
} // namespace gitman
