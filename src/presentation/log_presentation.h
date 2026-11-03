#pragma once

#include "domain/operation_log.h"
#include "presentation/view_snapshot.h"

#include <chrono>
#include <cstddef>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 필터 규칙을 logic(뷰 구성)과 UI(버튼 tooltip)가 한 곳에서 공유한다
    // (stage-7-plan 4.3).
    [[nodiscard]] bool log_entry_matches_filter(const operation_log_entry& entry, log_stream_filter filter) noexcept;

    // 필터 버튼 클릭이 순환하는 다음 값이다: 전체 → 출력 → 오류 → 전체.
    [[nodiscard]] log_stream_filter next_log_filter(log_stream_filter filter) noexcept;

    [[nodiscard]] std::u8string_view log_stream_filter_label(log_stream_filter filter) noexcept;

    // 시스템 로캘의 지역 시각 `HH:MM:SS`다. 변환 실패는 자리 표시자를 돌려준다.
    [[nodiscard]] std::u8string format_log_timestamp(std::chrono::system_clock::time_point time);

    // 실행 중 변경 작업의 경과 시간 `MM:SS`다 (초 단위 내림, 음수는 0으로).
    // 60분을 넘으면 분 자리가 그대로 늘어난다 (예: `125:07`).
    [[nodiscard]] std::u8string format_elapsed_time(std::chrono::steady_clock::duration elapsed);

    // 클립보드 복사용 텍스트다. 현재 뷰에 표시 중인(필터 적용 후) record를 CRLF로
    // 잇는다. UI thread는 이 결과를 그대로 클립보드에 넣는다. progress 접기는
    // 표시 전용이라 복사에는 적용하지 않는다 (stage-8-plan 5.3).
    [[nodiscard]] std::u8string format_log_copy_text(const log_view_model& log);

    // 표시 줄 하나의 본문 글이다 (시각 열 뒤에 그리는 글). 접힌 진행 표시는 접힌 수를 붙인다: `… (진행 표시 N줄 접힘)`.
    // 그리기와 접근 이름이 이 한 함수를 써서 둘이 어긋나지 않는다.
    [[nodiscard]] std::u8string format_log_line_text(const log_display_line& line);

    // 표시 줄 하나를 보조 기술에 내는 이름이다: `HH:MM:SS [오류: |경고: ]본문` (function-buttons-design S4). 오류·경고는
    // 화면에서 색으로만 갈리므로 이름이 말한다.
    [[nodiscard]] std::u8string log_line_access_name(const log_display_line& line);

    // 로그가 비었을 때 본문 첫 줄 자리에 그리는 안내다. 선택이 없는지, 필터가 모두인지에 따라 다르다.
    [[nodiscard]] std::u8string_view log_empty_hint(const log_view_model& log) noexcept;

    // 로그 본문 목록의 접근 이름이다 (S4 검토): `로그: 카드 이름[ (오래된 로그 제거됨)], N줄`. 제목만 쓰면 카드 항목과 같은
    // 이름이라 낭독으로 구별되지 않고, 보조 기술은 보이는 항목만 세므로 전체 줄 수(표시 줄, 접힌 진행 표시는 한 줄)를
    // 이름이 말한다. 비었으면 줄 수 대신 그려진 안내를 붙인다. 선택이 없으면 `로그, 안내`다.
    [[nodiscard]] std::u8string log_list_access_name(const log_view_model& log);

    // 필터 적용 후 record에서 progress 접기를 계산한 표시 목록이다. 연속된
    // progress record는 마지막 하나만 남고 접힌 수가 표식으로 남는다. 뷰 구성과
    // 스크롤 높이 계산이 같은 규칙을 써야 그리기와 한계가 어긋나지 않는다.
    [[nodiscard]] std::vector<log_display_line> build_log_display_lines(const std::deque<operation_log_record>& records, log_stream_filter filter);

    // 위 함수가 만들 표시 줄 수만 센다. 스크롤 한계 계산처럼 자주 불리는 경로가
    // 목록을 만들지 않게 한다.
    [[nodiscard]] std::size_t log_display_line_count(const std::deque<operation_log_record>& records, log_stream_filter filter) noexcept;
} // namespace gitman
