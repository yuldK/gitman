#pragma once

#include "domain/diagnostic.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

namespace gitman {
    // 카드 로그 항목의 출처다. 수명 주기는 logic이 만드는 작업 시작·결과 요약이고
    // 나머지 둘은 프로세스 출력 스트림이다 (plan 3.9).
    enum class log_entry_kind
    {
        lifecycle,
        standard_output,
        standard_error,
    };

    // 카드 로그의 항목 하나다. sequence는 buffer가 append 시점에 부여하므로 여기에는
    // 없다. 텍스트는 단계 3의 파이프라인을 거친 마스킹 완료 UTF-8 값이다.
    struct operation_log_entry
    {
        log_entry_kind kind { log_entry_kind::lifecycle };
        diagnostic_severity severity { diagnostic_severity::information };
        std::u8string text {};
        // Git의 진행 표시처럼 단독 `\r`로 끝난 줄이다. 로그 뷰가 접을 수 있도록
        // 표식을 유지한다.
        bool progress { false };
        std::chrono::system_clock::time_point time {};
    };

    // 카드에 표시되는 로그 record다. sequence는 카드별 단조 증가 값이다 (plan 3.9).
    struct operation_log_record
    {
        std::uint64_t sequence { 0 };
        operation_log_entry entry {};
    };

    // 카드당 유지하는 record 상한이다. 초과하면 오래된 record부터 제거한다
    // (stage-7-plan 4.1).
    inline constexpr std::size_t operation_log_capacity { 1000 };

    // 카드 하나의 로그 ring buffer다. logic thread만 만진다 (ADR-004). 총 발행 수와
    // 유지 개수를 분리해 두어 제거가 일어난 사실을 뷰가 알 수 있다.
    class operation_log_buffer
    {
    public:
        explicit operation_log_buffer(std::size_t capacity = operation_log_capacity) noexcept;

        void append(operation_log_entry entry);
        void clear() noexcept;

        [[nodiscard]] const std::deque<operation_log_record>& records() const noexcept;
        // 지금까지 부여한 sequence 수다. 지우기 후에도 이어지므로 record 순서의
        // 기준이 유지된다.
        [[nodiscard]] std::uint64_t total_appended() const noexcept;
        // 상한 초과로 제거된 record 수다. clear는 세지 않는다.
        [[nodiscard]] std::uint64_t dropped_count() const noexcept;

    private:
        std::size_t capacity_ { operation_log_capacity };
        std::deque<operation_log_record> records_ {};
        std::uint64_t total_appended_ { 0 };
        std::uint64_t dropped_ { 0 };
    };

    [[nodiscard]] std::u8string_view log_entry_kind_name(log_entry_kind kind) noexcept;
} // namespace gitman
