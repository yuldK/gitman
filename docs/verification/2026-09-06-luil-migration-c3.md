# luil 이식 C3 — 수용한 동작 변화 반영

날짜: 2026-09-06 · 계획: `docs/luil-migration-plan.md` C3 · 설계: `docs/luil-migration-design.md` D1-B·D2-A·D3-A·D5·5장 · 브랜치: `feature/luil-migration`

## 1. 변경 요약

C2에서 "모양·동작을 그대로" 옮긴 뒤, 설계 검수에서 승인한 동작 변화 네 가지를 커밋 하나씩으로 들였다.

| 커밋 | 내용 |
|---|---|
| `72760d3` C3-1 `feat: Tab 초점을 버튼·dialog 컨트롤·텍스트 칸에만 둠` | D1-B. 카드 body·root·카드 목록·도구 막대 배너·컨텍스트 메뉴·목록 행·행 아이콘·dialog scrim·스크롤 막대를 `set_tab_stop(false)`로 Tab 순회에서 뺐다. 정책 `on_click`이 root·카드 목록·도구 막대 클릭에서 `focused_card_`를 지운다 |
| `f411fce` C3-2 `refactor: dialog 뼈대를 luil modal_host로 재구성함` | D2-A. dialog 7종(switch·settings·font·generate·discovery·local_changes·notice)의 본체가 luil `modal_host_element`의 content가 됐다. scrim·가운데 배치·초점 가둠·Esc는 host가, 본체는 panel·행·버튼만 그린다. `presentation/ui/dialog_host.h`의 `make_dialog_host(dialog_host_spec, content)`와 dialog마다 `make_<x>_dialog(view, window_width, window_height)` factory(논리 픽셀). 확인 버튼은 `default_button`(switch 제외 — 질의 칸 Enter가 `submit_switch_query_intent`여야 한다). 정책의 Esc dialog 사슬 삭제 — luil `process_dismiss_key`가 마지막 가둠의 dismiss를 내므로 tree에 다는 순서(switch → settings → font → generate → discovery → local_changes → notice)가 Esc 우선순위다. 죽은 `scale` 생성자 인자 제거 |
| `d435a9b` C3-3 `refactor: 컨텍스트 메뉴를 luil popup 창으로 옮김` | D3-A. 카드·배너 메뉴가 주 tree 안 overlay(`context_menu_element`)에서 `ui_frame::popups`의 popup 창(`luil::menu_element`)으로 옮겨 갔다. `presentation/ui/context_menu_popup.{h,cpp}`의 `build_context_menu_popup(view, scale)`, 항목 key = entry 이름, popup id `card-menu`·`document-menu`. 외부 열기 3종은 select factory가 `app_ui_command`를 곧바로 내고 delegate가 실행 뒤 `close_context_menu_intent`를 게시, 나머지는 `select_context_menu_item_intent { entry }`로 logic이 닫고 처리한다(늦은 클릭은 view와 같은 판정으로 다시 거른다). 닫힘 계기(바깥 클릭·주 창 휠·Esc·창 이동·크기 변경·활성 전환)는 `ui_popup::dismiss`가 낸다. popup 위의 휠은 정책이 흘리지 않는다. kind 3개(`context_menu`·`context_menu_panel`·`context_menu_item`)와 `list_metrics`의 메뉴 행 높이·여백 상수 삭제 |
| `160424f` C3-4 `feat: IME 조합 중 점진 검색과 hover 그리기 정리` | D5. `logic_controller::search_query_for`가 luil `search_query`의 두 줄 규칙(조합 글이 섞인 표시 글로 먼저 거르고, 하나도 맞지 않으면 확정 글로 물러선다 — 확정 글이 비어 있으면 물러서지 않는다)을 옮겨 적고 `filtered_switch_candidates`(Git만)·`filtered_font_families`가 쓴다. Enter 첫 후보·스크롤 상한·행 수가 같은 목록을 본다. 행 hover 채움 5곳(discovery 후보·font 글꼴·local_changes 변경·switch 후보·SVN 디렉터리)을 luil `draw_hover_fill`로 바꾸고, dialog 7종 panel의 중복 `set_hit_opaque(true)`를 지웠다(흡수는 host가 content에 건다). 조합으로 화면 글이 달라지면 편집과 같은 규칙으로 선택·tracking 확인 단계·스크롤을 지운다 |

## 2. 구현 경계와 설계 대비 차이

- **C3-2 clamp 규칙**: 7종 모두 예전 arrange의 clamp를 논리 픽셀로 그대로 옮겼다
  (settings·switch는 `dialog_available_height`, 나머지 다섯은 창 크기). 구현
  agent가 font·discovery에 `dialog_available_height`를 넣었던 것은 검수에서
  "모양 보존" 규칙대로 되돌렸다.
- **C3-2 생성자**: switch는 `(view, window_height)`(논리 픽셀), settings도 같은
  꼴이며 font·discovery·local_changes는 view만 받는다. factory가 유일한 생성
  경로다. `dialog_host_spec`에 offset은 없다 (gitman dialog는 끌기가 없다).
- **C3-2 그리기 순서**: Esc 우선순위를 맞추느라 generate가 font 위에 그려진다
  (예전과 반대). 둘이 함께 열리는 경로는 없다.
- **C3-3 menu owner**: 계약은 `menu_config::owner = popup id`였으나 luil
  `find_menu`가 owner 없는 `{menu}`로 찾아 owner를 채우면 ↑/↓/Enter/Esc가 메뉴에
  닿지 않는다. owner를 비우고 popup 구분은 `ui_popup::id`가, 항목 구분은 entry
  키가 한다 (test agent가 발견, 검수가 확인).
- **C3-3 popup 위 휠**: luil은 popup 표면의 휠을 닫힘 계기로 보지 않고 popup
  tree를 정책에 넘긴다. 정책이 그대로 흘리면 뒤의 카드 목록이 스크롤되어 메뉴가
  앵커에서 뜨므로 `on_wheel` 맨 앞에서 `{menu}`가 있는 tree면 아무것도 내지 않게
  했다 (검수 발견). 주 창 위의 휠은 UI thread의 dismiss가 먼저 닫는다.
- **C3-3 창 밖 다듬기**: 예전 element는 창 안으로 밀었고 luil은 모니터 작업
  영역 안으로 민다 (설계 5장이 수용한 "메뉴가 창 밖으로 나감"). 우클릭 흡수도
  사라졌지만 바깥 누름에서 dismiss가 누름보다 먼저 app_inbox에 들어가 다른 카드
  우클릭이 새 메뉴를 연다 (검수가 luil `window_surface.cpp`에서 확인).
- **C3-3 메뉴 모양**: 행 높이 26은 같고 위아래 여백이 5 → 4(luil `menu_padding`),
  hover·강조 채움이 luil 메뉴 색이다.
- **C3-3 `context_menu_view::anchor_x/y`**: 물리 픽셀 그대로 두고 popup 빌더가
  배율로 나눈다 (계획서 C3-3 줄을 이에 맞게 고쳤다).
- **C3-4 hover 색**: 행 hover가 `primary_foreground` 0.06 → 팔레트
  `button_hover_background`(+눌림 `button_pressed_background`)로 바뀐다 (설계 L5가
  수용). `selected`·`current` 채움과 settings의 토글·배지 채움, 시작 페이지 행
  hover(pin/제거 아이콘과 묶인 규칙)는 그대로다. SVN 디렉터리 행은 selected →
  hover/눌림 → current 순서를 지키느라 hover 판정을 한 번 더 본다.
- **C3-4 빈 조합 글**: 일부 IME가 조합 시작에 빈 글을 보내면 빈 질의 = "전부"로
  목록이 잠깐 되살아난다. luil `search_query`와 같은 동작이라 그대로 따랐다.
- **font·settings panel의 `hit_test` 재정의**: 마지막 `(interactive() || hit_opaque())`
  갈래가 panel 흡수를 지운 뒤 죽은 갈래가 됐다. luil 기본 규칙과 같은 모양이라
  남겼다 (정리 후보).
- **local_changes diff pane·log pane의 `set_hit_opaque`**: 휠 라우팅 기준
  영역이라 hit 대상이어야 해서 남겼다.
- **계약 밖 CMake 편집**: C3-2에서 dialog 담당 agent가 `dialog_host.h`·
  `generate_document_dialog_tests.cpp`를 목록에 더했다 (필요한 변경, 기록만).
- **test 도우미 함정**: `as_intent`·`as_app_command`·`as_message`는 넘긴 vector를
  가리키는 포인터라 임시 vector를 넘기고 뒤에 쓰면 dangling이다. C2-4에 이어
  C3-2의 generate test 5곳에서 같은 실수가 나와 지역 변수로 고쳤다.

## 3. 자동 검증

| 항목 | 결과 |
|---|---|
| `cmake --build --preset vs2026-tests-debug` (C3-1~C3-4 각각) | 성공, 오류 0·경고 0 |
| 영향 범위 ctest — C3-1 (`focus|ui_|interaction|tree`) | 통과 (C3-1 기록) |
| 영향 범위 ctest — C3-2 (`dialog|ui_|interaction|notice|switch|settings|discovery|font|local|generate|tree|focus|policy`) | 202 / 202 통과 (첫 실행 실패 1 = generate test의 dangling 포인터, 고친 뒤 통과) |
| 영향 범위 test — C3-3 (`[context-menu]`·`[popup]`·`[driver]`) | 28 사례 통과 (첫 실행 실패 1 = popup이 빠진 뒤 ↓가 카드 선택으로 가는 것을 빈 목록으로 기대한 test, 기대를 고침) |
| 영향 범위 test — C3-4 (dialog·`[ime]`·`[ui]` 태그) | 219 사례 통과, 검수 반영 뒤 dialog·IME·modal 태그 101 사례 재통과 |
| 전체 `ctest --preset vs2026-tests-debug` | C3-3 뒤 **805 / 805**, C3-4 뒤 **812 / 812** (skip 3 = 실제 원격) |
| `scripts/check_source_style.ps1 -root .` | 통과 (429 files) |
| 검수 | 체크포인트마다 별도 agent가 계약·luil 소스 대비로 검수. C3-2: 조건부 통과(clamp 2건·정리 항목) → 반영. C3-3: should 1건(popup 위 휠)·nit 2건 → 반영, 빠진 test 4건 추가(늦은 전환 클릭·배율 0·driver 배율 왕복·popup 위 휠). C3-4: should 1건(조합으로 화면 글이 달라질 때 선택·tracking 확인 단계·스크롤을 편집과 같은 규칙으로 정리)·nit 2건(SVN 디렉터리 행 hover 갈래의 `enabled()` 가드, 빈 줄) → 반영, 빠진 test 3건 추가(다른 칸의 조합 무영향·조합 시 선택 해제·panel 빈 자리 클릭이 dialog를 닫지 않음) |

## 4. 수동 확인 (창 메시지로 입력을 넣고 client 영역을 캡처, DPI 150%, Debug)

| 시나리오 | 결과 |
|---|---|
| 환경설정 dialog (C3-2) | luil scrim이 창을 어둡게 하고 panel이 가운데에 선다. 탭 rail·배지·찾아보기·제한 시간 칸·저장(강조 채움 = 기본 버튼)·취소가 예전 모양. Esc로 닫힘 |
| 카드 선택·F5·경로 토글·Esc 해제·창 닫기 (C3-2·C3-3) | 예전과 같음, 종료 코드 0 |
| 컨텍스트 메뉴 popup (C3-3) | C3-3 직후 캡처는 사용자의 전면 창(게임)이 앱 창을 덮어 보지 못했고, C3-4 뒤 다시 돌린 캡처에서 확인했다: 카드 우클릭 자리에 6항목 메뉴(아이콘·라벨)가 popup 창으로 뜨고, ↓↓로 강조가 둘째 항목(VSCode로 열기)으로 옮겨지며, Esc로 닫힌다. 예전 overlay와 같은 위치·항목이고 여백·강조 색만 luil 메뉴 것이다 |
| hover 채움 색 (C3-4) | 자동 입력이 행 위에 머무르는 단계가 없어 캡처하지 않았다. luil 팔레트 값(hover 흰색 α26/255, 눌림 α45/255)이며 메뉴 항목 강조(위 캡처)가 같은 색이다 |

사용자가 같은 PC를 쓰는 동안이라 창을 전면으로 끌어오는 자동 입력은 짧게(20초)
두 번만 돌렸다.

## 5. 알려진 제한

- 행 hover 색은 화면으로 확인하지 않았다 (4장).
- delegate의 외부 열기 뒤 `close_context_menu_intent` 게시는 실제 셸을 부르는
  경로라 test가 없다 (이음매 없음). 메뉴가 닫히는 경로가 이 게시 하나뿐이다.
- `docs/build.md`·`README.md` 등 문서는 C4에서 갱신한다 (작업 트리에 초안 있음).

## 6. 다음 체크포인트

C4 문서·전역 검증 (`docs/verification/2026-09-06-luil-migration.md`).
