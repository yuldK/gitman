#pragma once

#include "domain/operation_log.h"
#include "presentation/view_snapshot.h"

#include <chrono>
#include <string>
#include <string_view>

namespace gitman {
    // 필터 규칙을 logic(뷰 구성)과 UI(버튼 tooltip)가 한 곳에서 공유한다
    // (stage-7-plan 4.3).
    [[nodiscard]] bool log_entry_matches_filter(const operation_log_entry& entry, log_stream_filter filter) noexcept;

    // 필터 버튼 클릭이 순환하는 다음 값이다: 전체 → 출력 → 오류 → 전체.
    [[nodiscard]] log_stream_filter next_log_filter(log_stream_filter filter) noexcept;

    [[nodiscard]] std::u8string_view log_stream_filter_label(log_stream_filter filter) noexcept;

    // 시스템 로캘의 지역 시각 `HH:MM:SS`다. 변환 실패는 자리 표시자를 돌려준다.
    [[nodiscard]] std::u8string format_log_timestamp(std::chrono::system_clock::time_point time);

    // 클립보드 복사용 텍스트다. 현재 뷰에 표시 중인(필터 적용 후) record를 CRLF로
    // 잇는다. UI thread는 이 결과를 그대로 클립보드에 넣는다.
    [[nodiscard]] std::u8string format_log_copy_text(const log_view_model& log);
} // namespace gitman
