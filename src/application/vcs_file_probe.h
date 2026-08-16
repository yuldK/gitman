#pragma once

#include <string_view>

namespace gitman {
    enum class vcs_path_kind
    {
        // 존재하지 않거나 접근할 수 없다. 두 경우를 구분하지 않는 이유는 호출자가
        // 표식 파일의 유무만 알면 되기 때문이다.
        missing,
        file,
        directory,
    };

    // Git이 진행 중인 작업을 보고하는 기계 판독 명령이 없어 git dir 안의 표식 파일
    // (`MERGE_HEAD`, `rebase-merge/` 등)을 직접 확인해야 한다. 계층 규칙을 지키려고
    // filesystem 접근을 계약으로 분리하고 구현은 Win32 adapter에 둔다.
    class vcs_file_probe
    {
    public:
        vcs_file_probe() = default;
        vcs_file_probe(const vcs_file_probe&) = delete;
        vcs_file_probe(vcs_file_probe&&) = delete;
        vcs_file_probe& operator=(const vcs_file_probe&) = delete;
        vcs_file_probe& operator=(vcs_file_probe&&) = delete;
        virtual ~vcs_file_probe() = default;

        // `absolute_path`는 절대 경로여야 한다. 예외를 던지지 않는다.
        [[nodiscard]] virtual vcs_path_kind probe(std::u8string_view absolute_path) const noexcept = 0;
    };
} // namespace gitman
