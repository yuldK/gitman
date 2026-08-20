#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace gitman {
    // 로컬 변경 확인 dialog가 다루는 작업 트리 변경 한 항목이다
    // (field-feedback-design 2.3). status 출력에서 경로를 보존해 만든다.
    enum class local_change_kind
    {
        modified,
        added,
        deleted,
        renamed,
        conflicted,
        untracked,
        other,
    };

    struct local_change_entry
    {
        local_change_kind kind { local_change_kind::modified };
        std::u8string path {};

        [[nodiscard]] bool operator==(const local_change_entry&) const noexcept = default;
    };

    // diff·내용 표시의 상한이다 (field-feedback-design 2.3). 넘으면 앞부분만
    // 표시하고 생략 안내를 붙인다.
    inline constexpr std::size_t local_change_diff_display_limit { 256u * 1024u };

    // 목록 행의 종류 배지 문구다.
    [[nodiscard]] std::u8string_view local_change_kind_badge(local_change_kind kind) noexcept;

    // status가 준 상대 경로를 작업 복사본 기준 절대 경로로 잇는다. 미추적 디렉터리
    // 표기의 끝 `/`는 떼어 낸다. 파일 읽기와 외부 열기(VSCode·탐색기)가 같은 규칙을
    // 쓴다.
    [[nodiscard]] std::u8string join_local_change_path(std::u8string_view working_directory, std::u8string_view relative_path);
} // namespace gitman
