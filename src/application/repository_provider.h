#pragma once

#include "application/process_cancellation.h"
#include "domain/diagnostic.h"
#include "domain/project.h"
#include "domain/repository_snapshot.h"
#include "domain/vcs_operation.h"

#include <vector>

namespace gitman {
    struct repository_query_result
    {
        repository_snapshot snapshot {};
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool has_errors() const noexcept;
    };

    struct switch_candidate_result
    {
        std::vector<switch_candidate> candidates {};
        // fetch가 실패해 cache된 remote tracking ref만으로 만든 목록이다.
        bool stale { false };
        std::vector<diagnostic> diagnostics {};
    };

    struct repository_change_result
    {
        // 사전 검사를 통과해 실제로 명령을 실행했는지 여부다. `false`이면 어떤
        // process request도 만들지 않았다는 뜻이다.
        bool executed { false };
        bool succeeded { false };
        update_block_reason blocked_by { update_block_reason::none };
        switch_rejection rejected_by { switch_rejection::none };
        // 성공 여부와 관계없이 실행 직후 다시 조회한 상태다.
        repository_snapshot snapshot {};
        std::vector<diagnostic> diagnostics {};
    };

    // 단계 4의 provider 계약은 단계 3과 같이 호출 스레드를 점유하는 동기 API다.
    // 카드별 lane, 동시 실행 상한과 결과 전달 경로는 단계 6~7이 정한다.
    //
    // 구현체는 `process_runner`를 주입받고 OS API를 직접 호출하지 않는다. 따라서
    // 대부분의 동작을 실제 Git/SVN 없이 fake runner로 검증할 수 있다.
    class repository_provider
    {
    public:
        repository_provider() = default;
        repository_provider(const repository_provider&) = delete;
        repository_provider(repository_provider&&) = delete;
        repository_provider& operator=(const repository_provider&) = delete;
        repository_provider& operator=(repository_provider&&) = delete;
        virtual ~repository_provider() = default;

        [[nodiscard]] virtual repository_kind kind() const noexcept = 0;

        // 도구가 없으면 `false`다. 이때 호출자는 조회와 변경을 시도하지 않고 카드에
        // 사유만 표시한다. Git과 SVN이 모두 없는 환경에서도 앱은 계속 동작한다.
        [[nodiscard]] virtual bool available() const noexcept = 0;

        // 네트워크에 접근하지 않는 조회다. 시작과 문서 reload에서 사용한다.
        [[nodiscard]] virtual repository_query_result query_local(const project_definition& project, const process_cancellation_token& token) noexcept = 0;

        // 명시적 refresh에서만 호출하는 원격 조회다. `local`은 직전 로컬 조회 결과다.
        [[nodiscard]] virtual repository_query_result query_remote(const project_definition& project, const repository_snapshot& local, const process_cancellation_token& token) noexcept = 0;

        [[nodiscard]] virtual switch_candidate_result query_switch_candidates(const project_definition& project, const process_cancellation_token& token) noexcept = 0;

        [[nodiscard]] virtual repository_change_result update(const project_definition& project, const update_options& options, const process_cancellation_token& token) noexcept = 0;

        // 검증에 실패하면 명령을 만들지 않고 `executed == false`로 반환한다.
        [[nodiscard]] virtual repository_change_result switch_to(const project_definition& project, const switch_candidate& target, const process_cancellation_token& token) noexcept = 0;
    };
} // namespace gitman
