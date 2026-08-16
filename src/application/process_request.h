#pragma once

#include "domain/diagnostic.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 줄 끝을 만나지 못한 레코드를 강제로 끊는 기본 크기다. 진행 표시가 없는 도구가
    // 한 줄로 대량 출력해도 로그 레코드 하나가 무한히 커지지 않게 한다.
    inline constexpr std::size_t default_process_record_byte_limit { 8u * 1024u };
    // 강제 분할이 UTF-8 sequence 하나를 쪼개지 않도록 최소 상한을 둔다.
    inline constexpr std::size_t minimum_process_record_byte_limit { 4u };
    // pipe에서 한 번에 읽는 크기다. 값이 너무 작으면 대용량 출력에서 호출이 늘고
    // 너무 크면 첫 레코드 지연이 커진다.
    inline constexpr std::size_t process_read_block_byte_size { 64u * 1024u };

    enum class process_text_encoding
    {
        // 출력을 UTF-8로만 해석하고 잘못된 byte는 U+FFFD로 대체한다.
        utf8,
        // UTF-8로 해석되지 않는 레코드만 활성 code page에서 변환한다.
        active_code_page_fallback,
    };

    struct process_environment_override
    {
        std::u8string name {};
        // 값이 없으면 부모 환경에서 해당 변수를 삭제한다.
        std::optional<std::u8string> value {};
    };

    struct process_request
    {
        // 절대 경로만 허용한다. PATH 탐색과 확장자 추론을 하지 않으므로 실행 파일
        // 발견은 호출자(단계 4의 provider)가 담당한다.
        std::u8string executable {};
        // argv[1] 이후의 인자다. 셸을 사용하지 않으므로 metacharacter는 값의 일부다.
        std::vector<std::u8string> arguments {};
        std::u8string working_directory {};
        std::vector<process_environment_override> environment_overrides {};
        // 값이 없으면 무제한이며, 취소만으로 종료를 제어한다.
        std::optional<std::chrono::milliseconds> timeout {};
        // 스트림별 캡처 상한이다. 상한을 넘으면 레코드를 버리지만 pipe는 계속 비운다.
        std::size_t maximum_captured_bytes_per_stream {};
        std::size_t maximum_record_bytes { default_process_record_byte_limit };
        process_text_encoding text_encoding { process_text_encoding::utf8 };
    };

    // 요청 값만으로 판정할 수 있는 오류를 반환한다. filesystem 조회는 하지 않는다.
    [[nodiscard]] std::vector<diagnostic> validate_process_request(const process_request& request);
    [[nodiscard]] bool is_absolute_windows_path(std::u8string_view path) noexcept;
    [[nodiscard]] std::u8string_view process_text_encoding_name(process_text_encoding encoding) noexcept;
} // namespace gitman
