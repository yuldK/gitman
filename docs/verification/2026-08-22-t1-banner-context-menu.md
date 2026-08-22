# T1 — 배너(도구 막대) 컨텍스트 메뉴

날짜: 2026-08-22 · 설계: `docs/theme-and-banner-menu-design.md` T1 (체크포인트 C2)

## 1. 변경 요약

상단 배너를 우클릭하면 `경로를 탐색기로 열기`·`VSCode로 열기` 두 항목의 메뉴가
앵커 좌표에 뜬다. 문서가 열려 있을 때만 열린다.

## 2. 구현 경계

- 기존 카드 메뉴 overlay를 그대로 쓴다. 바깥 클릭·Esc·↑/↓/Enter 이동이 문서
  메뉴에서도 같은 코드로 동작한다.
- 메뉴 수준의 `repository_path`를 **항목 수준 `target_path`**로 옮겼다. 항목마다
  셸에 넘길 대상이 다르기 때문이다 — 탐색기 항목은 문서 **파일**
  (`explorer /select,`로 폴더를 열고 문서를 선택), VSCode 항목은 문서가 있는
  **폴더**(workspace)다. 카드 메뉴 항목은 모두 작업 복사본 경로를 담는다.
- `context_menu_entry`에 `open_document_folder`·`open_document_in_vscode`를
  추가했다. 아이콘은 카드 메뉴와 같은 `folder-opened`·`vscode`다.
- logic의 `context_menu_state`에 `kind { card, document }`를 두고 새 intent
  `open_document_context_menu_intent { anchor_x, anchor_y }`가 문서 메뉴를 연다.
  문서 메뉴는 **선택 카드를 바꾸지 않는다**(카드 메뉴는 지금처럼 우클릭한 카드를
  선택한다). 문서가 없으면 열지 않는다.
- `toolbar_element`는 문서가 열려 있을 때만 우클릭 액션을 갖는다. 자식 버튼이
  hit test에 먼저 걸리므로 **빈 곳과 문서 경로 글자** 우클릭만 메뉴를 연다.
  우클릭 액션이 생기면서 배너가 hit test 대상이 되므로, 지금까지 root로 흘러가
  선택을 해제하던 좌클릭을 같은 intent로 유지했다.

## 3. 검증

빌드: `cmake --build --preset vs2026-tests-debug` (경고 없음)

| 테스트 | 결과 |
|---|---|
| 배너 우클릭 intent가 문서 메뉴를 열고 선택 카드는 그대로다 (owner 비어 있음, 항목 2개, 대상 경로 파일/폴더) | 통과 |
| 문서가 없으면 문서 메뉴가 열리지 않는다 | 통과 |
| 배너 우클릭이 `open_document_context_menu_intent`를 앵커 좌표와 함께 낸다 / 문서가 없으면 아무 일도 없다 / 좌클릭은 선택 해제 유지 | 통과 |
| 문서 메뉴 항목 클릭이 닫기 + `explorer`(선택 열기) / `vscode`(폴더) 요청을 낸다 | 통과 |
| 기존 카드 메뉴 test를 항목 수준 `target_path`로 갱신 | 통과 |
| `[context-menu]` 12 case (112 assertions) | 통과 |
| 영향 범위 재실행 `[ui],[logic],[display],[start-page]` 164 case (4,393 assertions) | 통과 |

## 4. 남은 수동 검수

- 문서를 연 뒤 배너 빈 곳·문서 경로 글자를 우클릭해 메뉴가 뜨는지, 탐색기가
  문서를 선택한 채 열리고 VSCode가 문서 폴더를 workspace로 여는지 확인.
- 배너의 버튼 위 우클릭은 메뉴를 열지 않는다(의도된 동작).
