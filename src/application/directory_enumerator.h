#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    struct directory_entry
    {
        std::u8string name {};
        bool is_directory { false };
        // 심볼릭 링크와 junction을 포함한 모든 reparse point다. 탐색 정책은 이 항목을
        // 판정 없이 제외한다 (단계 5 계획 4.3).
        bool is_reparse_point { false };
    };

    struct directory_listing
    {
        bool succeeded { false };
        std::vector<directory_entry> entries {};
        // UTF-8로 표현할 수 없는 이름이라 목록에서 빠진 항목 수다. 조용히 누락하는
        // 대신 개수로 남겨 호출자가 진단으로 알릴 수 있게 한다.
        std::uint32_t unreadable_name_count { 0 };
        std::optional<std::uint32_t> native_error {};
    };

    // 디렉터리의 깊이 1 항목 열거 계약이다. 탐색 로직이 OS API를 직접 호출하지 않게
    // filesystem 접근을 분리하고 구현은 Win32 adapter에 둔다. 단계 4의
    // `vcs_file_probe`와 같은 선례다.
    class directory_enumerator
    {
    public:
        directory_enumerator() = default;
        directory_enumerator(const directory_enumerator&) = delete;
        directory_enumerator(directory_enumerator&&) = delete;
        directory_enumerator& operator=(const directory_enumerator&) = delete;
        directory_enumerator& operator=(directory_enumerator&&) = delete;
        virtual ~directory_enumerator() = default;

        // `absolute_directory`는 절대 경로여야 한다. `.`과 `..`은 반환하지 않고
        // 예외를 던지지 않는다. 실패하면 `succeeded`가 꺼진 결과에 가능한 경우
        // Win32 오류 번호를 담는다.
        [[nodiscard]] virtual directory_listing enumerate(std::u8string_view absolute_directory) const noexcept = 0;
    };
} // namespace gitman
