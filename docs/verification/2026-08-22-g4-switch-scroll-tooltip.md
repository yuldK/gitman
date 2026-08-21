# G4 — 전환·탐색 dialog 스크롤 막대와 hover 재판정

날짜: 2026-08-22 · 설계: `docs/global-settings-and-ui-fixes-design.md` (G4)

## 1. 변경 요약

| 증상 | 수정 |
|---|---|
| 브랜치 전환 후보가 많아도 스크롤 막대가 없어 목록이 더 있는지 보이지 않고 끌 수 없음 | switch dialog 목록 오른쪽에 `scrollbar_element` 추가. 같은 구조의 탐색(discovery) dialog에도 함께 추가 |
| 휠 스크롤 중 hover가 이전 행 id에 붙어 있어 tooltip이 행을 따라 위·아래로 쓸려 다님 | 새 tree를 받을 때 마지막 포인터 위치로 hover를 재판정 |
| 스크롤 목록과 위아래(제목·버튼) 영역이 같은 색이라 경계가 보이지 않음 (2026-08-22 추가 지시) | 목록 경계에 상단 막대 그림자와 같은 규칙의 그림자를 드리움. 목록이 위·아래 영역 밑으로 지나가는 모양이다 |

## 2. 구현 경계

- **스크롤 막대**: 기존 `scrollbar_element`를 id + intent factory로 재사용한다
  (`switch_dialog_scrollbar` → `switch_dialog_scroll_intent`,
  `discovery_dialog_scrollbar` → `discovery_dialog_scroll_intent`). content·
  viewport는 logic의 clamp 계산과 같은 값(행 수 × 행 높이,
  `switch_dialog_list_height`/`layout_discovery_dialog_list_height`)이라 thumb와
  실제 스크롤이 어긋나지 않는다. 내용이 목록보다 짧으면 보이지 않고, 보이면
  행 폭을 `layout_scrollbar_hit_width`만큼 줄여 겹치지 않는다.
- switch dialog의 목록 높이는 창 높이를 따라 줄어들므로
  `switch_dialog_element`가 ctor에서 `scale`·창 높이(물리 픽셀)를 받는다.
  discovery dialog는 목록 높이가 상수라 `scale`만 받는다.
- **hover 재판정**: `interaction_controller`가 마지막 포인터 위치·시각을
  기억하고(이동·휠 이벤트에서 갱신, 창 이탈 시 무효), `set_tree`에서 그 자리로
  hit test를 다시 수행한다. 스크롤 막대 끌기·카드 drag 중에는 hover가 잡은
  대상에 남아야 하므로 재판정하지 않는다. `mouse_wheel_event`에 time 필드를
  추가해 hover 시작 시각의 기준으로 쓴다.
- **input pump**: 이벤트가 없는 타임아웃 턴(250 ms)에도 최신 tree를 받아
  hover를 재판정하고 snapshot이 바뀌면 게시한다. 휠을 멈춘 뒤 도착한 마지막
  tree의 반영이 다음 입력을 기다리지 않는다.
- **경계 그림자**: `draw_upward_shadow`(아래 경계에서 위로 옅어짐, downward의
  대칭)를 primitives에 추가했다. 두 dialog의 panel이 목록 clip 안에서 위로 숨은
  내용이 있으면 상단에 downward, 아래로 이어지는 내용이 있으면 하단에 upward
  그림자를 그린다. 색·높이·진하기는 상단 막대 그림자와 같은 상수
  (`content_shadow`, `layout_content_shadow_height/strength`)이고, 숨은 양이
  그림자 높이보다 적은 구간에서는 비례해 옅게 시작한다. 카드 목록·로컬 변경
  dialog의 아래 경계에도 같은 처리를 원하면 후속으로 확장한다.

## 3. 검증

이번 세부단계에서 추가·수정한 테스트만 실행했다 (2026-08-20 지시).

| 테스트 | 결과 |
|---|---|
| `The switch dialog shows a scrollbar only when the candidates overflow` (신규) | 통과 |
| `The discovery dialog shows a scrollbar only when the candidates overflow` (신규) | 통과 |
| `A new tree refreshes the hover under a resting pointer` (신규) | 통과 |
| 기존 기능 접촉분: `[interaction],[switch-ui],[discovery-ui]` 태그 44 case | 통과 (386 assertions) |

빌드: `vs2022-tests` Debug에서 `gitman_tests`·`gitman`(앱) 모두 성공.

## 4. 남은 수동 검수

- 실제 앱에서 브랜치가 많은 저장소로 전환 dialog를 열어 막대 표시·끌기 확인.
- 후보 행 tooltip이 뜬 상태에서 휠을 굴려 tooltip이 쓸리지 않고 커서 아래
  행으로 갱신되는지 확인.
- 목록 상·하단 경계 그림자가 스크롤 위치(맨 위·중간·맨 아래)에 따라
  나타나고 사라지는지 확인.
