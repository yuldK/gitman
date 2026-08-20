# 실환경 피드백 F5 — 카드 컨텍스트 메뉴

날짜: 2026-08-20 · 설계: `docs/field-feedback-design.md` 3장

## 1. 변경 요약

카드 body 우클릭이 클릭 지점에 붙는 in-app 팝업 메뉴를 연다. 항목은 설계 1차
목록 그대로 5개다: 저장소 열기 · 로컬 변경 확인(F4 dialog) · 상태 갱신 ·
업데이트 · 전환….

- **intent 2종**: `open_context_menu_intent { id, anchor_x, anchor_y }`(카드
  body `right_click` 액션이 클릭 좌표를 실어 보냄) / `close_context_menu_intent`.
- **logic**: `context_menu_state { card, anchor }` — 열 때 카드를 선택 카드로
  만든다(로그 pane 연동). 항목 목록·활성 여부는 view 구성 시 카드 상태로
  그때그때 계산한다: `저장소 열기/로컬 변경 확인/상태 갱신`은 항상 활성,
  `업데이트`는 `can_change`이고 실행 중이 아닐 때, `전환…`은 `can_change`일 때
  활성 — 카드 버튼과 같은 판정이다. 없는 카드는 열지 않고 문서 교체 시 닫힌다.
- **view**: `context_menu_view { owner, anchor, repository_path, items }`,
  `context_menu_item_view { entry, label, enabled }`.
- **UI**: `context_menu_element` — 투명 전체 화면 overlay(좌·우클릭 모두 닫기,
  dialog 위 최상위 자식) + 앵커에 붙는 소형 panel(그림자·테두리, 창 밖으로
  나가면 안쪽으로 밀기). 항목 행은 codicon + 라벨이고 비활성은 흐림, hover와
  키보드 강조는 같은 모양이다. 항목 클릭은 [닫기 intent → 본 동작] 순서의 액션
  2개를 낸다.
- **저장소 열기**: `external_open_target::explorer_folder` 신설 — F4의 외부
  열기 경로(input → app_runtime 큐 → UI thread shell 실행)를 그대로 타고,
  `/select` 없이 폴더 자체를 연다. 대상 절대 경로는 logic이 카드 정규화 경로로
  view에 싣는다.
- **키보드** (설계: 메뉴가 ↑/↓/Enter/Esc를 흡수): interaction controller가 메뉴
  열림을 감지하면 카드 탐색 대신 메뉴 키 처리로 들어간다. ↑/↓는
  `interaction_snapshot::focused_input`과 같은 급의 입력 정규화 상태인
  `menu_highlight`를 활성 항목 사이에서만 움직이고(비활성 건너뜀), Enter는 강조
  항목의 클릭 액션을 실행하며(외부 열기도 같은 경로), Esc는 닫는다. 여닫는
  edge에서 강조를 지워 새 메뉴는 강조 없이 시작한다.
- **구현 조정**: 메뉴가 떠 있는 동안 휠은 무시한다 — 앵커에 붙은 메뉴 아래로
  목록이 흘러가면 대상 카드와 어긋난다. 실행 중 카드의 `업데이트` 항목은
  비활성이다(카드 버튼은 취소 버튼으로 바뀌지만 메뉴에 취소 항목은 두지 않음).

## 2. 테스트 (이 단계 추가·직접 영향분만 실행)

- 신규 8 case, 73 단정 (`tests/context_menu_tests.cpp`): 우클릭 intent →
  선택+메뉴 열림(앵커·경로·항목 5개), 로딩 카드의 업데이트/전환 비활성, 실행 중
  업데이트 비활성, 없는 카드/닫기 intent/문서 교체의 닫힘, panel 앵커 배치·창 안
  clamp, 항목 클릭의 [닫기, 동작] 액션 쌍(폴더 열기·상태 갱신·로컬 변경
  확인)과 비활성 무시·바깥 좌/우클릭 닫기·휠 무시, 카드 body 우클릭의 좌표
  전달, 키보드(↓/↑ 비활성 건너뜀, Enter 실행, Esc 닫기, 닫힘 시 강조 소거).
- stale 단정 수정: `local_changes_tests.cpp:270`이 F4 3차 검수 이전의
  "미추적 (디렉터리)" 배지 문자열을 기대해 HEAD에서도 실패하고 있었다. 최종
  구현(배지 안 folder 아이콘 + `directory` 플래그)에 맞게 고쳤다.
- 직접 영향 범위: `[logic],[ui],[runtime],[update-ui],[local-changes],[settings-ui],[log]`
  145 case / 5245 단정 통과 (VS2022 Debug). 전체 suite는 FV에서 실행한다.

## 3. 빌드·스타일

- VS2022 Debug: `gitman_tests`, `gitman` 빌드 성공
- `check_source_style.ps1` 통과 (CRLF 정규화 포함)

## 4. 수동 확인 권장 항목

1. 카드 우클릭으로 클릭 지점에 메뉴가 뜨고, 우클릭한 카드가 선택되는지
2. 창 오른쪽 아래 모서리 근처 우클릭 시 메뉴가 창 안쪽으로 밀리는지
3. `저장소 열기`가 탐색기로 작업 복사본 폴더를 여는지
4. `로컬 변경 확인`이 F4 dialog를, `전환…`이 switch dialog를 여는지
5. 준비 전/실행 중 카드에서 업데이트·전환 항목이 흐리게 나오고 눌리지 않는지
6. 바깥 클릭(좌·우)·Esc로 닫히고, ↑/↓/Enter로 항목을 고르고 실행할 수 있는지
7. 메뉴가 떠 있는 동안 휠이 목록을 흘리지 않는지
