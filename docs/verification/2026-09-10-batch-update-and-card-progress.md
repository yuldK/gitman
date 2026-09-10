# 1단계 — 일괄 업데이트 버튼과 카드 진행 표시

날짜: 2026-09-10 · 설계: `docs/custom-commands-and-svn-hooks-design.md` (U, P) ·
지시: 2026-09-10 사용자 요청 · 요구사항: REQ-018, REQ-019

## 1. 변경 요약

| 증상·요청 | 반영 |
| --- | --- |
| 일괄 업데이트 버튼을 위쪽에 제공한다 | 도구 막대 왼쪽, 전체 새로 고침 바로 오른쪽에 `kind_toolbar_update_all`을 두었다. 변경 작업이 돌고 있으면 같은 버튼이 "모두 중지"가 된다 |
| 업데이트 시간이 콘솔에만 나온다 | 카드 세 번째 줄 오른쪽 끝에 실행 중 경과 `MM:SS`와 마지막 완료 시각(`3분 전`)을 그린다 |
| 상태만으로는 멎었는지 도는지 모른다 | 실행 중에는 강조색으로 초가 흐르고, 멎으면 흐린 상대 시각으로 바뀐다. 조회(refresh)도 센다 |

부수 수정: `cmake/platform/windows.cmake`의 clang-format 탐색이 Visual Studio
2026의 `VC/Tools/Llvm/<host>/bin` 배치를 찾지 못해 `vs2026-tests` preset이
configure에서 멈추던 것을 고쳤다. 기존 `Llvm/bin`도 그대로 찾는다.

## 2. 구현 경계

- `update_all_intent`·`cancel_all_operations_intent`를 `logic_message`에 더하고
  `handle_update_all`·`handle_cancel_all_operations`를 만들었다. 대상은 **필터와
  무관하게** 문서의 모든 활성 카드다 — 전체 새로 고침과 같은 규칙이다.
- `begin_change`가 busy 카드를 거부하는 기존 동작은 그대로 두고, 일괄 업데이트가
  거부 수를 세어 배너 한 줄로 요약한다 (`batch_notice_`). 건너뛴 것이 없으면
  배너를 띄우지 않는다 — 카드가 도는 것이 곧 피드백이라서다.
- `card_state`에 `operation_started_at`(steady)와 `last_finished_at`(system)을
  더했다. 기존 `change_started_at`·`change_elapsed`는 하단 로그 헤더의 것이라
  건드리지 않았다. 새 값은 조회까지 포함해 "이 카드가 마지막으로 뭔가 한 시각"을
  뜻한다.
- 표시는 `card_element`가 한다. `arrange`가 `layout_card_elapsed_width`(64 논리
  픽셀)를 먼저 예약하고 상태 조각이 남은 폭을 쓴다. 자리가 모자라면
  `elapsed_right_ = 0`으로 두어 그리지 않는다 (로그 헤더와 같은 관용구).
- `card_element::next_update`가 멎은 카드에는 다음 초(60초 미만)·15초(1시간
  미만)·1분(하루 미만) 뒤를 돌려주고, 하루가 지나면 `MM-DD HH:MM`이라 더 깨우지
  않는다. `context.now`를 그대로 돌려주면 멎은 카드가 창을 33ms마다 다시 그린다.
- `format_relative_time`은 `presentation/log_presentation`에 두었다. Skia를 모르는
  순수 함수라 값 test가 된다.

설계와 다르게 한 것은 없다. 설계 C·H·X장(2~4단계)은 손대지 않았다.

## 3. 검증

빌드·test 환경: Visual Studio Community 2026 (18.9.12120.119, MSVC 19.51),
CMake 4.4.2, preset `vs2026-tests` / `vs2026-tests-release`.

### 새 TEST_CASE

| 파일 | TEST_CASE |
| --- | --- |
| `tests/log_view_tests.cpp` | Relative time reads as a distance until a day has passed |
| `tests/ui_tree_build_tests.cpp` | The toolbar carries a batch update button next to refresh |
| `tests/ui_tree_build_tests.cpp` | The batch update button is hidden without a document |
| `tests/ui_tree_build_tests.cpp` | A card asks for repaints while it works and for a slow tick after it stops |
| `tests/ui_interaction_tests.cpp` | The batch update button starts updates and becomes a stop button |
| `tests/logic_change_operation_tests.cpp` | Update all starts every ready card and says nothing when none are skipped |
| `tests/logic_change_operation_tests.cpp` | Update all skips busy cards and reports how many |
| `tests/logic_change_operation_tests.cpp` | Cancel all requests cancellation for every running change |
| `tests/logic_change_operation_tests.cpp` | The snapshot tells the toolbar whether a batch update can start |
| `tests/logic_change_operation_tests.cpp` | A card carries when its work started and when it last finished |

### 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-release` | 성공, 경고 없음 |
| `ctest --preset vs2026-tests-release` | **822 / 822 통과** (76.1초) |
| `scripts/check_source_style.ps1 -root .` | 431개 파일 통과 |

기준선 대비 test 수: 812(2026-09-06 luil 이식 최종) → **822** (+10).

`gitman_source_style`은 CTest 항목에 포함되어 있어 위 822에 들어 있다.

### 검증하지 않은 것

- Visual Studio 2022 toolchain과 Debug 구성은 이번에 돌리지 않았다. 이 환경에
  VS2022가 설치되어 있지 않고, Debug는 Skia Debug 패키지(1GB)까지 링크해야 해
  단계마다 돌리기에는 비싸다. 네 단계가 끝나는 시점에 전체 matrix를 돌린다.
- `/analyze`(`vs2026-analysis`)도 최종 단계로 미뤘다.

## 4. 남은 수동 검수

- 카드가 여럿인 문서에서 일괄 업데이트를 누르고, 도는 카드의 `MM:SS`가 1초마다
  올라가는지 확인한다.
- 도는 동안 같은 버튼이 중지 아이콘으로 바뀌고, 눌렀을 때 모든 카드가 취소되는지
  확인한다.
- 일부 카드가 이미 돌고 있을 때 일괄 업데이트를 눌러 "N개는 아직 준비되지 않아
  건너뛰었습니다" 배너가 뜨고, 첫 작업이 끝나면 사라지는지 확인한다.
- 작업이 끝난 뒤 카드의 시간이 `방금 전` → `1분 전`으로 바뀌는지, 그 사이에 창이
  과도하게 다시 그려지지 않는지(작업 관리자의 CPU) 확인한다.
- 창을 좁혀 카드 버튼이 숨겨질 때까지 줄였을 때 시간 표시가 사라지고 상태 조각이
  남는지 확인한다.
