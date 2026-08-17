#pragma once

#include "application/project_path_resolver.h"
#include "application/project_store.h"
#include "domain/discovery.h"
#include "domain/project.h"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 문서 안에서 유일한 프로젝트 id를 만든다. 디렉터리 이름을 그대로 쓰고 이미 있는
    // id와 충돌하면 `-2`부터 숫자 접미사를 붙인다 (계획 4.7). 사람이 읽을 수 있고
    // 재현 가능하며 난수 dependency가 없다.
    [[nodiscard]] std::u8string make_unique_registration_id(std::u8string_view directory_name, const std::vector<std::u8string>& taken);

    struct project_registration_result
    {
        bool succeeded { false };
        // 성공한 경우에만 값이 있다. 등록 항목이 더해진 문서와 새 revision이며,
        // 호출자(단계 6~7의 logic thread)가 활성 문서 상태를 이것으로 바꾼다.
        std::optional<workspace_document> saved_document {};
        std::optional<workspace_revision_token> revision {};
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool has_errors() const noexcept;
    };

    // 탐색 후보의 선택 등록이다 (REQ-004). 부분 등록은 하지 않는다. 선택 목록에
    // 부적격 후보가 하나라도 있으면 아무것도 저장하지 않고 전체를 거부한다. 사용자가
    // 고른 목록 전체가 원자적으로 들어가거나 전체가 거부되는 편이 결과를 예측하기
    // 쉽기 때문이다 (계획 4.7).
    class project_registration_service
    {
    public:
        // 주입받은 계약들은 service보다 오래 살아 있어야 한다.
        project_registration_service(project_store& store, project_path_resolver& resolver) noexcept;

        // `document`와 `expected_revision`은 같은 load에서 나온 쌍이어야 한다. 저장
        // 충돌 감지는 단계 2 store의 revision 비교를 그대로 사용하며 이 service는
        // 병합을 시도하지 않는다 (계획 4.8).
        [[nodiscard]] project_registration_result register_candidates(
            const workspace_document& document, const workspace_revision_token& expected_revision, std::span<const discovery_candidate> selected) noexcept;

    private:
        project_store* store_ { nullptr };
        project_path_resolver* resolver_ { nullptr };
    };
} // namespace gitman
