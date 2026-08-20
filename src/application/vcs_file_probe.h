#pragma once

#include <cstddef>
#include <string>
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

    // `read_prefix`의 결과다 (field-feedback-design 2.3). `kind`가 `file`일 때만
    // `bytes`에 값이 있다. 디렉터리와 부재는 구분해 안내가 달라진다.
    struct vcs_file_content
    {
        vcs_path_kind kind { vcs_path_kind::missing };
        std::u8string bytes {};
        // `maximum_bytes` 상한에 걸려 앞부분만 담았다.
        bool truncated { false };
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

        // 파일 앞부분을 최대 `maximum_bytes`까지 읽는다. 미추적 파일의 내용 표시
        // (field-feedback-design 2.3)처럼 VCS 명령이 없는 읽기에 쓴다. 인코딩을
        // 해석하지 않고 바이트 그대로 돌려주며, 예외를 던지지 않는다.
        [[nodiscard]] virtual vcs_file_content read_prefix(std::u8string_view absolute_path, std::size_t maximum_bytes) const noexcept = 0;
    };
} // namespace gitman
