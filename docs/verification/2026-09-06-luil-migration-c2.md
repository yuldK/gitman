# luil 이식 C2 — 기계적 이식

날짜: 2026-09-06 · 계획: `docs/luil-migration-plan.md` C2 · 설계: `docs/luil-migration-design.md` L2~L9 · 브랜치: `feature/luil-migration`

## 1. 변경 요약

gitman의 UI 프레임워크 사본을 지우고 앱 코드를 luil 공개 API 위로 옮겼다. 커밋 4개다.

| 커밋 | 내용 |
|---|---|
| `48aacbf` C2-1 `refactor: UI 기반 계층을 luil로 바꾸고 Skia 직접 빌드를 제거함` | 프레임워크 파일 삭제(messaging 3, domain 3 cpp, presentation 3쌍, ui 11쌍, platform 10쌍), Skia submodule 8개·GN 인자·패치·스크립트·`skia.cmake`·자산·생성기 삭제. 전달 헤더(`appearance`·`text_edit`·`utf8_text`), `element_kinds.h`(앱 kind 91개), `gitman_caption.h`, `app_commands.h`, `ui_common.h`, `ui_actions.h`, `status_glyphs`. `app_messages.h`에 `startup_intent`·`relay_drain_intent`, `text_input_target : uint32 { none = 0, … }`. `gitman_lib`이 `luil::luil` 링크 |
| `e1ac361` C2-2 `feat: luil driver·policy·delegate로 앱을 조립함` | `gitman_driver`·`operation_result_relay`·`cancellation_registry`·`gitman_policy`·`gitman_delegate`·`main.cpp`. `task_scheduler` sink 생성자, `logic_controller` 보관소 등록·`startup_intent`, `process_cancellation_source::weak_canceller`. `CMAKE_CXX_SCAN_FOR_MODULES OFF` |
| `6a8887c` C2-3 `refactor: 앱 element를 luil ui_element 위로 옮김` | element 28개 파일 |
| `2411c0b` C2-4 `test: luil 이식에 맞춰 test를 정리함` | test 삭제 11·수정 9·재작성 12·신설 7, `helpers/ui_test_support.h`, `tests/CMakeLists.txt` |

## 2. 구현 경계와 설계 대비 차이

- **커밋 분할**: 삭제된 test 12개(`app_runtime`·설계 L9의 11개)가 C2-4가 아니라 C2-2
  커밋에 실렸다 (staging 실수). 내용은 계획과 같다.
- **`ui_common.h`**: element 파일마다 `luil::` 한정자를 붙이는 대신 `gitman::ui` 안에
  luil 어휘를 using으로 들이는 헤더 하나를 두었다. 코디콘도 81곳 치환 대신
  `namespace codicons = luil::codicons;` 별칭이다. 기반이 luil임은 이 헤더 한 곳에
  드러난다.
- **스크롤 막대 kind**: 예전 8종을 `kind_scrollbar` 하나 + owner 문자열
  (`scrollbar_owner_*`)로 합쳤다 (설계 L4).
- **root**: `luil::root_element`는 final이고 자식을 배치하지 않아 gitman 자체 root
  클래스(예약 kind `root`)를 유지했다. 배경 clear는 luil 렌더러 몫이라 지웠다.
- **`hit_test` override 유지 2곳**: 환경설정 panel·글꼴 dialog panel은 자르는 경계가
  자기 bounds가 아니라 항목 viewport/list_area라 `set_clip_children`으로 바꾸면
  창이 낮을 때 항목이 버튼 줄을 뚫고 눌린다. override 본문을 luil 기본 규칙에
  맞추고 남겼다 (C3-2 `modal_host` 재구성에서 정리 후보). switch panel은
  `clip_children`으로 바꿔 hit 자르기 기준이 panel 전체 bounds가 됐다 (모양 동일).
- **`logic_controller` 편의 생성자**: 보관소를 스스로 드는 1·2인자 생성자를 남겨
  test 30여 곳이 그대로 컴파일된다. 앱은 driver가 보관소를 소유하는 생성자를 쓴다.
- **글꼴 (D4 → luil `c971f32`)**: `main.cpp`가 `luil::set_ui_typeface_families({ u8"Malgun Gothic" })`,
  delegate가 `luil::installed_font_families()`. 카드 drag payload는
  `suppress_drop_highlight = true`.
- **relay·종료**: 설계 L3 그대로. `cancel()`이 보관소로 취소를 걸고 종료 중에는
  close만 통과, `make_frame`은 마지막 frame 재사용.
- **luil 요청 추가**: 고지 생성기의 `set(notice_text "a" "b")`가 list를 만들어 고지
  머리글에 `;`가 섞인다 (gitman 예전 생성기도 같았다). 설계 부록 B에 적는다.
- **C3로 미룬 것**: D1-B 초점 정책(`set_tab_stop(false)`), D2 `modal_host`·`default_button`,
  D3 popup 메뉴(정책 `on_wheel`의 메뉴 검사 줄 유지), D5 점진 검색. 지금 상태에서
  카드 body·dialog scrim은 luil 기본으로 Tab 자리다.
- **Skia 백업**: 삭제한 `third_party/skia`·`skia-externals` 작업 트리(빌드 산출물
  포함)는 지우지 않고 `E:\Programming\Lusain\gitman-skia-backup\`으로 옮겼다.

## 3. 자동 검증

| 항목 | 결과 |
|---|---|
| `cmake --preset vs2026` / `vs2026-tests` | 성공. `Skia:` 줄이 luil 패키지 하나뿐 |
| `cmake --build --preset vs2026-debug` / `vs2026-release` (`gitman`) | 성공, 오류 0·경고 0 |
| `cmake --build --preset vs2026-tests-debug` | 성공, 오류 0·경고 0 |
| `ctest --preset vs2026-tests-debug` 전체 | **774 / 774 통과**, 실패 0, skip 3 (실제 GitHub·SVN·자격 증명 원격 — 기존과 같음). 73초. 첫 실행의 실패 7건은 test 쪽 문제였다: 임시 액션 목록을 가리키는 포인터 8곳(dangling)과 stress test의 sink가 가득 찬 채널을 버리던 것 — 고친 뒤 통과 |
| smoke 4종 × Debug·Release | 8건 종료 코드 0 |
| `scripts/check_source_style.ps1 -root .` | 통과 (425 files) |
| 기준선 대비 | C1 837 → 774. 삭제 11 파일·재작성 축소(기반 API 역학 사례는 luil test 몫)·신설 7 파일의 합이다 |

## 4. 수동 확인 (창 메시지로 입력을 넣고 client 영역을 캡처, DPI 150%)

| 시나리오 | 결과 |
|---|---|
| 명령행 문서 열기 (`check.version-list`, 카드 2개: 저장소 아님·git) | 카드 2개·상태 chip·경로가 예전과 같은 모양으로 그려진다. Debug·Release 모두 |
| 시작 페이지 (인자 없음) | 제목·설명·시작 열(문서 열기·새 문서 만들기)·최근 항목 |
| 카드 body 클릭 | 선택 테두리 + 하단 로그 pane(제목·필터·자동 스크롤·복사·지우기 버튼) |
| 카드 우클릭 → 컨텍스트 메뉴 | 6항목 메뉴가 포인터에 뜬다. ↓↓로 강조가 둘째 항목으로, Esc로 닫힘 |
| F5 | 카드에 spinner가 돌고 조회 뒤 chip이 갱신되며 로그 pane에 결과 줄이 남는다 |
| 도구 막대 환경설정 버튼 | 환경설정 dialog(탭 4개·범위 배지·찾아보기·제한 시간 칸·저장/취소). Esc로 닫힘 |
| 도구 막대 탐색 버튼 | Win32 폴더 선택 dialog가 **문서 폴더에서** 열린다 (`app_ui_command::argument` 경로) |
| 도구 막대 경로 표시 토글 | 상대/절대 경로가 바뀌고 문서에 저장된다 (다음 실행에 유지됨) |
| Esc (dialog 없음) | 카드 선택 해제 |
| 창 닫기 (WM_CLOSE) | 종료 코드 0, hang·fail-fast 없음 (Debug·Release) |
| `--renderer=cpu` · `direct3d` · `auto` · 실패 주입 | smoke 8건 통과 |

하지 못한 것: 파일 dialog로 문서 열기·드롭·카드 drag 순서 변경·로그 복사·
switch Git/SVN·글꼴 dialog·IME 조합·붙여넣기·창 배치 복원·DPI 변경·Alt+F4는
자동 입력으로 재현하지 않았다 (test가 intent 수준으로 덮고, 화면 확인은 C3·C4의
수동 시나리오에서 마저 한다).

## 5. 알려진 제한

- 카드 body·dialog scrim·스크롤 막대가 luil 기본 규칙으로 Tab 자리다 (C3-1에서 정리).
- 컨텍스트 메뉴는 아직 tree 안 overlay다 (C3-3에서 popup으로).
- `docs/build.md`·`README.md`에 gitman 자체 Skia 절이 "C2 이전까지 필요"로 남아
  있다 (C4에서 삭제).
- smoke 모드에서도 driver를 만들어 worker pool·로그 writer thread가 잠깐 선다.

## 6. 다음 체크포인트

C3-1 초점 정책(D1-B) → C3-2 `modal_host`(D2-A) → C3-3 popup 메뉴(D3-A) →
C3-4 점진 검색(D5)·정리 → C4 문서·전역 검증.
