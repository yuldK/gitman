#pragma once

#include "domain/diagnostic.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 실행 요청의 인자 배열을 명령줄 하나로 잇는 규칙이다.
    enum class command_line_syntax
    {
        // 인자마다 `CommandLineToArgvW` 규칙으로 인용한다. 셸을 거치지 않으므로 메타 문자는 값의 일부다.
        argv,
        // `cmd.exe`가 `/c` 뒤의 글을 자기 규칙으로 다시 해석하는 것에 맞춰 조립한다. 메타 문자가 든 토큰은
        // 따옴표로 감싸 원래 값 그대로 닿게 한다 (function-buttons-design 4.3, D27).
        cmd,
    };

    // `cmd` 조립으로도 원래 값을 지킬 수 없는 문자다. `%`는 따옴표 안에서도 정의된 환경 변수 이름이면 값으로
    // 바뀌고 `^`로 막을 수 없다. `!`는 지연 확장(`/v:on`)이 켜지면 같다. 따옴표 안의 `"`를 나타낼 길이 없고
    // 줄바꿈은 명령을 끝낸다 (2026-11-03 실측, R1).
    inline constexpr std::u8string_view cmd_uncorrectable_characters { u8"%!\"\r\n" };

    enum class process_stream
    {
        standard_output,
        standard_error,
    };

    enum class process_completion
    {
        exited,
        start_failed,
        timed_out,
        cancelled,
        invalid_request,
        // 프로세스는 시작했지만 대기, 출력 수집 또는 종료 코드 확인이 실패해 결과를
        // 신뢰할 수 없는 경우다. 이때 자식은 정리된다.
        internal_error,
    };

    // 출력 레코드는 줄 단위이며 카드 로그가 그대로 표시할 수 있는 UTF-8 및 마스킹 완료 값이다.
    // 원문 byte와 마스킹되지 않은 텍스트는 어떤 public 값에도 보관하지 않는다.
    struct process_output_record
    {
        std::uint64_t sequence {};
        process_stream stream { process_stream::standard_output };
        std::u8string text {};
        // Git의 진행 표시처럼 단독 `\r`로 끝난 줄이다. 단계 7의 로그 뷰가 접을 수 있다.
        bool progress { false };
        // 줄 끝을 만나지 못하고 레코드 크기 상한 때문에 끊긴 줄이다.
        bool continued { false };
        bool replaced_invalid_bytes { false };
        bool transcoded_from_active_code_page { false };
    };

    struct process_result
    {
        process_completion completion { process_completion::invalid_request };
        // `exited`일 때만 값이 있다. timeout과 취소는 자식의 종료 코드를 신뢰하지 않는다.
        std::optional<std::int32_t> exit_code {};
        std::chrono::system_clock::time_point started_at {};
        std::chrono::system_clock::time_point finished_at {};
        std::chrono::milliseconds duration {};
        std::uint64_t record_count {};
        std::size_t captured_bytes {};
        bool output_truncated { false };
        std::optional<std::uint32_t> native_error {};
        // 로그와 재현에 사용하는 마스킹된 명령줄이다.
        std::u8string masked_command_line {};
        std::vector<diagnostic> diagnostics {};

        [[nodiscard]] bool succeeded() const noexcept;
        [[nodiscard]] bool has_errors() const noexcept;
        [[nodiscard]] bool has_warnings() const noexcept;
    };

    [[nodiscard]] std::u8string_view process_stream_name(process_stream stream) noexcept;
    [[nodiscard]] std::u8string_view process_completion_name(process_completion completion) noexcept;
} // namespace gitman
