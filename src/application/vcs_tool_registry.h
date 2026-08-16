#pragma once

#include "domain/repository_snapshot.h"
#include "domain/vcs_tool.h"

#include <string_view>
#include <vector>

namespace gitman {
    // 도구 조사 결과를 보관하는 값 container다. 탐색과 버전 확인은 프로세스 실행이
    // 필요하므로 infrastructure의 `resolve_vcs_tools`가 담당하고, 이 type은 결과를
    // 담아 provider와 UI에 전달하는 역할만 한다. 매 명령마다 `--version`을 다시
    // 실행하지 않도록 결과를 여기에 두고 명시적 재조사에서만 갱신한다.
    class vcs_tool_registry
    {
    public:
        vcs_tool_registry() = default;
        explicit vcs_tool_registry(vcs_tool_set tools) noexcept;

        void set_tools(vcs_tool_set tools) noexcept;
        [[nodiscard]] const vcs_tool_set& tools() const noexcept;
        [[nodiscard]] const vcs_tool_info& tool(repository_kind kind) const noexcept;
        [[nodiscard]] bool available(repository_kind kind) const noexcept;

        // Git과 SVN이 모두 없는 환경에서도 앱은 계속 동작해야 한다. 호출자는 이 값으로
        // 조회 및 변경 동작만 비활성화하고 프로젝트 목록은 그대로 보여 준다.
        [[nodiscard]] bool any_available() const noexcept;
        [[nodiscard]] bool none_available() const noexcept;

        // 사용할 수 없는 도구의 진단을 모아 준다. 카드와 진단 화면이 같은 문구를 쓴다.
        [[nodiscard]] std::vector<diagnostic> unavailable_diagnostics() const;

    private:
        vcs_tool_set tools_ {};
    };

    // 도구가 없을 때 카드에 표시할 한국어 사유다. 진단과 UI가 같은 문장을 공유한다.
    [[nodiscard]] std::u8string_view vcs_tool_unavailable_message(repository_kind kind, vcs_tool_availability availability) noexcept;
} // namespace gitman
