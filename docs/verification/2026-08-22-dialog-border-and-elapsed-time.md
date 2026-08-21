# 후속 — dialog 목록 실선 경계 · 로그 헤더 경과 시간

날짜: 2026-08-22 · 지시: G3 진행 전 처리 (2026-08-22 사용자)

## 1. 변경 요약

| 지시 | 반영 |
|---|---|
| 전환 dialog 스크롤 영역은 그림자 대신 하단 로그 콘솔 같은 solid border | 목록 영역 둘레에 1px 실선 경계. 그림자 경로(`draw_upward_shadow` 포함)는 제거. 탐색 dialog도 동일 |
| update·switch 실행 시 로그 콘솔에 경과 시간을 실시간 표시 (`00:00`, 초 단위) | 로그 pane 헤더 오른쪽(버튼 왼쪽)에 `MM:SS`를 그리고 1초마다 갱신 |

`docs/verification/2026-08-22-g4-switch-scroll-tooltip.md`의 경계 그림자 항목은
이 실선 경계로 대체되었다.

## 2. 구현 경계

- **실선 경계**: switch·discovery panel이 목록 clip을 마친 뒤
  `primary_foreground` 25% · 1px stroke로 목록 영역을 두른다 (dialog panel
  테두리와 같은 계열). 스크롤 위치와 무관하게 항상 보인다.
- **경과 시간 값**: `card_state`에 `change_started_at`(steady_clock)을 두고
  `begin_change`에서 기록한다. 선택 카드의 변경 작업이 실행 중일 때만
  `log_view_model::change_started_at`으로 게시되고, 완료·취소로
  `change_operation_id`가 0이 되면 사라진다 (완료 후 자동 재조회 중에는 표시
  없음 — 조회는 대상 밖).
- **표시**: `format_elapsed_time`(순수 함수, `MM:SS`, 초 내림, 음수는 0, 60분
  초과 시 분 자리 확장)를 `log_presentation`에 두고, `log_view_element`가 헤더
  버튼 왼쪽에 예약한 자리(`layout_log_elapsed_width`)에 오른쪽 정렬로 그린다.
  제목은 그만큼 좁아지고, 자리가 없으면 그리지 않는다.
- **실시간 갱신**: 값은 draw 시각(`draw_context::now`) 기준이므로 UI thread가
  `elapsed_timer_id` timer로 경과가 다음 초로 넘어가는 시점마다 다시 그린다.
  caret·tooltip timer와 같은 1회성 재예약 패턴이며, 실행 중이 아니면 꺼진다.

## 3. 검증

| 테스트 | 결과 |
|---|---|
| `Elapsed time formats as minutes and seconds` (신규) | 통과 |
| `A running change exposes its start time to the log view header` (신규) | 통과 |
| 기존 기능 접촉분: `[log],[change],[switch-ui],[discovery-ui]` 58 case | 통과 (1,386 assertions) |

빌드: `vs2022-tests` Debug에서 `gitman_tests`·`gitman`(앱) 모두 성공.

## 4. 남은 수동 검수

- 전환·탐색 dialog 목록에 실선 경계가 보이고 그림자가 사라졌는지.
- 카드 update·switch 실행 중 로그 헤더에 `00:00`이 초 단위로 올라가고,
  완료·취소 시 사라지는지. 1분 초과 시 `01:00`으로 이어지는지.
