# luil 채택 구현 계획

## 1. 문서 상태

- 작성일: 2026-09-05
- 현재 상태: **승인·진행 중** (2026-09-06). 사용자 지시("구현을 바로 하지 말고
  계획서부터 써")로 설계 승인 뒤 구현 전에 작성했고, 2026-09-06 사용자 지시("내
  판단이 필요한 일이 아니면 작업 끝날 때까지 묻지 말고 끝까지 진행")로 승인됐다.
- luil 갱신 (2026-09-06): 사용자가 부록 B의 글꼴 공개 API·drag 강조 opt-out을
  luil `c971f32`에 반영했다. 핀을 그 커밋으로 잡고 D4-B의 gitman 우회(자체 글꼴
  열거·frame 기본 글꼴 채움)는 두지 않는다. 남은 luil 요청은 설계 부록 B.
- 상위 문서: [luil-migration-design.md](luil-migration-design.md)(설계, 2026-09-05
  검수 승인 D1~D8), [ADR-007](decisions/ADR-007-luil-ui-framework-adoption.md)
- 진행 방식(D8-B): `feature/luil-migration` 브랜치에서 체크포인트마다 직접
  커밋하고, 마지막에 전역 검증을 한 번 보고한다. 검수는 커밋 단위로 사후에 한다.
- 시작 기준선: `main` `1f61c16`. 전체 CTest 통과 수는 C1 시작 시 현재 빌드로
  실측해 첫 검증 기록에 적는다.
- 검증 기록: 체크포인트마다 `docs/verification/2026-09-06-luil-migration-cN.md`.

## 2. 사용자 결정 사항 (2026-09-05)

| 지점 | 결정 | 계획에 미치는 영향 |
| --- | --- | --- |
| D1 Tab·초점 | B 선택 채택 | C3-1. 버튼·dialog 컨트롤·텍스트 칸만 Tab 자리. 카드 body·root·메뉴·목록 행은 `set_tab_stop(false)`. 카드 키보드는 policy가 지금 로직으로 |
| D2 dialog 뼈대 | A `modal_host_element` | C3-2. C2에서는 자체 scrim으로 이식하고 C3에서 재구성 |
| D3 컨텍스트 메뉴 | A luil popup | C3-3. C2에서는 overlay 그대로 이식(backdrop 유지) 후 C3에서 popup으로 |
| D4 luil 상류 변경 | B gitman 안에서 해결 → luil `c971f32`가 공개 API 제공 | C0 생략. `luil::installed_font_families`·`luil::set_ui_typeface_families`·`suppress_drop_highlight`를 쓴다. gitman 자체 글꼴 열거는 만들지 않는다 |
| D5 IME 점진 검색 | A 채택 | C3-4. 두 줄 규칙을 logic에 옮겨 적는다 |
| D6 nlohmann | A 둘 다 유지 | 준비 절차에 gitman submodule init 포함 |
| D7 자산 | A gitman 사본 삭제 | C2-1에서 삭제 |
| D8 진행 | B 브랜치 위임 | 이 문서의 커밋 단위 |

## 3. 목표와 완료 조건

목표는 설계 1장과 같다. 완료 조건은 다음 전부다.

1. gitman의 UI 단이 luil 공개 API만 쓰고, 2.1 표의 프레임워크 중복 파일이
   저장소에 없다.
2. `third_party/skia`·`skia-externals`·`skia-args`·`patches`·`build_skia.ps1`·
   `verify_skia_root.ps1`·`cmake/dependencies/skia.cmake`가 없다.
3. 새 clone에서 ADR-007 결정 4의 준비 절차만으로 configure·빌드·설치가 되고,
   빌드 로그에 네트워크 취득이 없다.
4. 전체 CTest가 Debug·Release에서 통과하고 smoke 4종이 통과한다.
5. `vs2026-analysis` 빌드와 `check_source_style.ps1`이 깨끗하다.
6. 수동 시나리오(설계 7장)가 검증 기록에 결과와 함께 있다.
7. 문서(설계 L10)가 갱신되어 있다.

## 4. 하지 않는 일

- luil 저장소 변경. 이 작업 중 luil에 추가로 요청할 것은 설계 부록 B에 적고
  없어도 진행한다.
- luil의 HTTP·WebView2·이미지·보조 창 사용.
- 새 UI 기능(dialog 캡션 끌기, 드롭다운, 탭, 토스트, `dialog_caption_element` 채택).
- 절대 위치 스크롤 intent(`scroll_to`)와 커스텀 element의 `accessibility()` 재정의.
- nlohmann submodule 통합.
- 모양이 다른 luil 요소로의 교체(토글·테마 세그먼트·시작 페이지 행·start action).

## 5. 체크포인트 개요

| 체크포인트 | 커밋 | 빌드 상태 | 검증 |
| --- | --- | --- | --- |
| C1 빌드 준비 | 1 | gitman은 자기 Skia로 빌드, luil은 자기 Skia로 빌드 (링크 없음) | configure + 앱 Debug/Release 빌드 + smoke 4종 + 전체 CTest(기준선 실측) |
| C2 기계적 이식 | 4 (C2-1~C2-4) | C2-1~C2-3은 단독으로 빌드되지 않을 수 있다. **C2-4에서 빌드·test 통과** | 영향 test 전부 + smoke 4종 + 앱 실행 수동 확인 |
| C3 luil 표준 요소 채택 | 4 (C3-1~C3-4) | 각 커밋 빌드·해당 test 통과 | dialog·메뉴·설정·상호작용 test + 수동 시나리오 |
| C4 문서·정리·전역 검증 | 1~2 | 빌드 통과 | 전역 검증 전체 + 새 clone 재현 |

C2-1~C2-3이 단독으로 빌드되지 않는 것은 검증 기록에 명시한다. 검수 시 하나로
합치는 것도 무방하다.

## 6. 체크포인트 세부

### C1. 빌드 준비 (커밋 1)

목표: luil을 저장소에 들이고 gitman 실행 파일이 luil의 리소스 조각을 쓰되,
**어떤 gitman target도 `luil::luil`을 링크하지 않는다.**

| ID | 작업 | 파일 |
| --- | --- | --- |
| C1-1 | `git submodule add https://github.com/yuldK/luil.git third_party/luil`, `c971f32`로 고정, luil의 `third_party/nlohmann_json` init | `.gitmodules`, `third_party/luil` |
| C1-2 | `fetch_skia.ps1 -Configuration Debug,Release`, `fetch_webview2.ps1` 실행 (산출물은 luil의 `.gitignore`가 막는다) | – |
| C1-3 | 최상위 `CMakeLists.txt`: `set(LUIL_BUILD_TESTS/EXAMPLES/TOOLING OFF)`, `add_subdirectory(third_party/luil SYSTEM)`, `LUIL_SOURCE_DIRECTORY`·`LUIL_ASSET_DIRECTORY`·`LUIL_NLOHMANN_JSON_ROOT` 재정의, `GITMAN_THIRD_PARTY_NOTICES`·`GITMAN_GENERATED_LUIL_RESOURCE` 정의, `luil_generate_third_party_notices`·`luil_configure_resource_script` 호출. 기존 `gitman_generate_third_party_notices` 호출과 `cmake/generate_notices.cmake` include 제거 | `CMakeLists.txt`, `src/CMakeLists.txt` |
| C1-4 | `.rc` 분리: `gitman.rc.in`에서 RCDATA 4줄 삭제, `resource_ids.h`는 `IDI_GITMAN 105`만. `gitman`·`gitman_tests`에 `${GITMAN_GENERATED_LUIL_RESOURCE}` 추가. gitman의 `embedded_assets.cpp`는 같은 ID로 luil 조각을 읽으므로 그대로 동작 | `src/platform/win32/resources/*`, `src/CMakeLists.txt`, `tests/CMakeLists.txt` |
| C1-5 | `.gitattributes`에 `/third_party/luil/** -text`; `scripts/normalize_line_endings.ps1` 제외 목록에 `third_party` | 두 파일 |
| C1-6 | `docs/build.md`·`README.md`의 준비 절차를 ADR-007 결정 4로 교체 (Skia 절은 C2에서 마저 정리) | 문서 |

검증: `cmake --preset vs2026` 로그에서 luil configure 메시지(Skia/WebView2 경로)
확인, `cmake --build --preset vs2026-debug`·`-release`, `ctest --preset
vs2026-tests-debug`(기준선 수치 기록), smoke 4종, 새 clone 흉내(임시 clone에서
C1-1·C1-2 절차 재현). 실행 파일 고지에 WebView2·Skia 항목이 이미 들어가는 것을
기록한다.

제안 커밋 메시지: `build: luil submodule을 더하고 실행 파일 리소스를 luil 조각으로 나눔`

### C2. 기계적 이식 (커밋 4)

원칙: 모양·동작 보존. luil 표준 요소는 동일 계약(버튼·라벨·스크롤 막대·캡션·
글자 버튼·텍스트 칸)만 바꾼다. dialog는 자체 scrim, 메뉴는 tree 안 overlay
그대로 (C3에서 재구성).

#### C2-1. 기반 교체와 공통 타입

| ID | 작업 |
| --- | --- |
| C2-1a | 삭제: `src/messaging/*.h`, `src/domain/{text_edit,utf8_text,appearance}.cpp`, `src/presentation/{ui_theme,renderer_policy,skia_smoke_view}.*`, `src/presentation/ui/{ui_element,ui_tree,ui_events,ui_interaction,draw_primitives,button_element,label_element,scrollbar_element,caption_element,caption_metrics,dialog_elements}.*`, `src/platform/win32/{win32_application,win32_app_runtime,caption_layout,skia_renderer,cpu_skia_renderer,direct3d_skia_renderer,embedded_assets,win32_fonts,win32_tsf_input,win32_clipboard}.*` |
| C2-1b | 전달 헤더: `domain/appearance.h`·`domain/text_edit.h`·`domain/utf8_text.h`를 luil include + `using` 선언으로 축소. `messaging/*` include를 `luil/messaging/*`로. `application_options.h`가 `luil/win32/renderer_policy.h`를 include |
| C2-1c | `app_messages.h`: `text_edit_command` 사본 삭제(`luil::text::text_edit_command`), `text_input_target`을 `: std::uint32_t { none = 0, settings_timeout = 1, ... }`로, `startup_intent`·`relay_drain_intent` 추가. `view_snapshot.h`의 `text_input_view` 삭제(`luil/ui/text_input_state.h`) |
| C2-1d | `presentation/ui/element_kinds.h` 신설(app kind 상수 98개 중 luil 요소가 맡는 것 제외), `presentation/ui/status_glyphs.{h,cpp}`(`codicon_for_name` 이전), `presentation/ui/gitman_caption.h` |
| C2-1e | `list_metrics`: `clamp_scroll_offset`→`luil::clamp_scroll`, scrollbar 상수 2개→luil 상수, `layout_caption_height`→`caption_element::height_for`. 스크롤 값은 논리 픽셀로 |
| C2-1f | CMake [C2] 항목: 코디콘·accent 생성·검증 제거, `unofficial::skia::skia`·시스템 라이브러리 10개 제거, `luil::luil`·`advapi32` 링크, `cmake/dependencies/skia.cmake`·`generate_*.cmake`·`verify_codicons.cmake` 삭제, `gitman_assets_checksum`·`gitman_messaging_asan_tests` 제거 |
| C2-1g | Skia 제거: submodule 8개 deinit·삭제, `third_party/{skia-args,patches}`, `scripts/{build_skia,verify_skia_root,fetch_codicons}.ps1`, `assets/codicons`, `assets/accents.json`, `.gitattributes`·`.gitignore`의 skia 줄, `docs/skia-build.md` |

#### C2-2. 앱 조립 신설

| ID | 작업 | 설계 |
| --- | --- | --- |
| C2-2a | `application/cancellation_registry.{h,cpp}`: mutex + `register(source&)`/`unregister`/`cancel_all()`. `logic_controller`가 참조로 받아 종료·변경 작업·탐색 source를 등록. `start()`의 제출은 `startup_intent` 처리로 이동 | L3 |
| C2-2b | `task_scheduler`: 생성자 인자를 `std::function<void(logic_message)>`로 | L3 |
| C2-2c | `platform/win32/operation_result_relay.{h,cpp}`: `post`/`bind`/`unbind`/`drain`, 깨우기 `relay_drain_intent` | L3 |
| C2-2d | `platform/win32/gitman_driver.{h,cpp}`: 멤버 순서(설계 L3), `handle`(drain → `shutting_down_`이면 close만), `make_frame`(마지막 frame 재사용, `ui_frame` 채움), `cancel`(registry), `make_close_message`, `shutdown_completed_` atomic, `stop_workers`(scheduler.shutdown → relay.unbind), `tick`(drain) | L3 |
| C2-2e | `application/app_commands.h`: `app_command` enum 12개 + `make_app_ui_command(command, argument)` | L7 |
| C2-2f | `platform/win32/gitman_delegate.{h,cpp}`: `on_started`(bind → startup → 글꼴 목록(`luil::installed_font_families`) → 명령행 문서 절대 경로), `execute_app_ui_command`(dialog 본체 이전, `GetActiveWindow()`), metrics/placement 메시지, `on_file_dropped`(절대 경로) | L3 |
| C2-2g | 글꼴: `main.cpp`가 `run_application_window` 전에 `luil::set_ui_typeface_families({ u8"Malgun Gothic" })`; delegate `on_started`가 `luil::installed_font_families()`로 `font_families_event` 게시. gitman 자체 열거 파일 없음 | L3 글꼴·drag |
| C2-2h | `presentation/ui/gitman_policy.{h,cpp}`: `text_target_of`, `make_text_edit_action`/`make_text_composition_action`(POD intent로 변환), `on_wheel`(dialog 존재 사슬 if-chain + `route_wheel` 표; 메뉴 검사 줄은 C2에서는 두고 C3-3에서 뺀다), `on_key`(F5·카드 ↑/↓·Enter·Esc 사슬은 C2에서는 지금 순서 그대로 policy에, C3-2에서 dialog Esc 제거), `on_click`, `menu()`(C2: 앱 kind `context_menu`/`context_menu_item`; C3-3: luil `menu`/`menu_item`), `close_menu` | L7 |
| C2-2i | `platform/win32/main.cpp` 재작성: DPI → `com_sta_scope` → 인자 → 파일 연결 모드 → `window_config`(`gitman_caption`, `IDI_GITMAN`, options 매핑, `accept_file_drop`) → `run_application_window` | L3 |

#### C2-3. element 이식 (28개 파일)

| ID | 파일 | 작업 |
| --- | --- | --- |
| C2-3a | `build_ui_tree` | `luil::root_element`, `gitman_caption`으로 `luil::caption_element`, 자식 순서 유지(`generate`·`font` 순서 교정은 C3-2), 배경 클릭 선택 해제, `make_logic_action` 도우미 |
| C2-3b | `toolbar_element` | luil `button_element`·`label_element`; 숨김 규칙 유지; `set_hit_opaque` |
| C2-3c | `card_element`, `card_list_element` | `dragged_owner`·`custom_visual = true`·`suppress_drop_highlight = true`·`grab_offset`, 버튼 3개 luil, `next_update`(spinner), 논리 픽셀 스크롤, luil `scrollbar_element`(`scrollbar_config` + `set_metrics`) |
| C2-3d | `log_view_element` | 헤더 버튼·막대 luil, 본문 커스텀, 복사 액션이 클릭 시점에 `clipboard_copy_request`, `next_update`(경과 시간 초 경계) |
| C2-3e | `start_page_element` | luil `ui_element` 위로, 커스텀 그리기 유지 |
| C2-3f | `context_menu_element` | overlay 유지, `set_hit_opaque`; 항목은 두 액션(닫기+실행) 유지 (C3-3에서 삭제) |
| C2-3g | `notice_dialog_element`, `switch_dialog_element`, `settings_dialog_element`, `discovery_dialog_element`, `font_dialog_element`, `generate_document_dialog_element`, `local_changes_dialog_element` | 자체 scrim 유지, `text_button_element`/`text_input_element`/`scrollbar_element`는 luil config 생성자로, 행·토글·배지·diff pane·tab rail은 커스텀, `hit_test` override → `set_clip_children`, `owner` 문자열, `next_update`(SVN 로딩 행) |
| C2-3h | 공통 | `luil::codicons::` 치환 81곳, `draw_hover_fill` 적용, `sk_sp` 필드, `interaction_snapshot` 필드명 확인 |

#### C2-4. test

| ID | 작업 |
| --- | --- |
| C2-4a | 삭제 11 (설계 L9) |
| C2-4b | 수정 9: `utf8_tests` 분리, `log_isolation_stress`, `application_options`, `app_settings`·`domain_model`·`json_project_store`·`project_schema`, `dependency_versions`(152), `task_scheduler`(sink) |
| C2-4c | 재작성 12: `ui_tree_build`, `ui_interaction`(policy·intent 매핑), `context_menu`, `discovery_dialog`, `notice_dialog`, `settings_dialog`, `switch_dialog`, `start_page`, `log_view`, `update_ui`, `local_changes`(UI 9사례; 파서 5사례는 `local_changes_domain_tests`로 분리), `app_runtime`→`gitman_driver_tests` |
| C2-4d | 신설: `operation_result_relay_tests`(순서·bind 배출·bind 먼저), `cancellation_registry_tests`, driver `shutting_down_` 동작, `element_kinds_tests`(중복 없음), `app_commands_tests`(매핑), `next_update` 값, `gitman_caption` 양쪽 동일, 경로 절대화, 카드 drag payload 플래그 |
| C2-4e | `tests/CMakeLists.txt` 정리, 매 tree마다 `duplicate_ids()`·`unarranged()` 빈 것 확인 도우미 |

검증(C2-4 끝): `ctest --preset vs2026-tests-debug` 전체(이 시점에는 전체를
돌린다 — 프레임워크 교체라 영향 범위가 곧 전부다), Release는 C4에서. smoke 4종.
앱 실행 수동 확인: 문서 열기(dialog·드롭·명령행 상대 경로), 카드 새로 고침·drag
순서 변경, 로그 복사, switch Git/SVN, 환경설정 4탭, 글꼴 dialog 미리 보기, IME
한글 조합 4칸, 붙여넣기, 컨텍스트 메뉴 키보드, 창 배치 저장/복원, DPI 변경,
`--renderer=cpu`, Alt+F4.

제안 커밋 메시지:
- C2-1 `refactor: UI 기반 계층을 luil로 바꾸고 Skia 직접 빌드를 제거함`
- C2-2 `feat: luil driver·policy·delegate로 앱을 조립함`
- C2-3 `refactor: 앱 element를 luil ui_element 위로 옮김`
- C2-4 `test: luil 이식에 맞춰 test를 정리함`

### C3. luil 표준 요소 채택 (커밋 4)

| ID | 작업 | 검증 |
| --- | --- | --- |
| C3-1 초점 정책 (D1-B) | 카드 body·root·메뉴·목록 행·행 안 아이콘에 `set_tab_stop(false)`; 버튼·dialog 컨트롤·텍스트 칸은 기본; `order_focus`는 기본; 텍스트 칸 초점 규칙 변화(배경 클릭 해제·지우기 버튼 유지) 확인 | `ui_interaction_tests`에 Tab 순서·Enter가 카드 refresh를 유지하는지 |
| C3-2 dialog 뼈대 (D2-A) | 7개 dialog를 `modal_host_element`(owner, scrim 0.45, outside 비움, dismiss=취소, focus_entry/return은 D1-B 범위에서 confirm/cancel 버튼)+`panel`+`stack`으로. `content_height`에 `dialog_available_height` 반영. `build_ui_tree`의 `generate`/`font` 추가 순서 교정. policy `on_key`의 dialog Esc 사슬 삭제. 확인 버튼 `default_button` | dialog test 7종 + Esc 순서 test(notice > local_changes > discovery > generate > font > settings > switch) |
| C3-3 컨텍스트 메뉴 (D3-A) | `ui_frame::popups`에 `ui_popup` + `menu_element`; `context_menu_element` 삭제; logic이 항목 선택 시 메뉴를 닫고 intent 처리; policy `menu()`를 `{menu, menu_item}`로, `on_wheel`의 메뉴 검사 줄 삭제; `context_menu_view`의 anchor는 물리 픽셀 그대로 두고 popup 빌더가 배율로 나눔 | `context_menu_tests` 재작성(popup tree) + 수동: 바깥 클릭·휠·활성 전환·창 이동 |
| C3-4 마무리 | D5 점진 검색(두 줄 규칙을 `filtered_switch_candidates`·`filtered_font_families`에), `draw_hover_fill`·`hit_opaque` 정리 잔여, `text_button` 남은 `accent` 플래그를 `visual`로 | `switch_dialog_tests`·`settings_dialog_tests`의 IME 조합 필터 사례 |

제안 커밋 메시지:
- C3-1 `feat: Tab 초점을 버튼·dialog 컨트롤·텍스트 칸에만 둠`
- C3-2 `refactor: dialog 뼈대를 luil modal_host로 재구성함`
- C3-3 `refactor: 컨텍스트 메뉴를 luil popup 창으로 옮김`
- C3-4 `feat: IME 조합 중 점진 검색과 hover 그리기 정리`

### C4. 문서·정리·전역 검증 (커밋 1~2)

| ID | 작업 |
| --- | --- |
| C4-1 | 설계 L10의 문서 전부: `build.md`(§1·2·3.2·5·7), `README.md`, `deployment.md`, `user-manual.md`(+`.html`), ADR-001/002/004/005/006 주석, `dependency-provisioning-design.md` 폐기 표시, `requirements.md` REQ-009, `plan.md`·`handoff.md` 날짜 항목, `ui-element-design.md` 머리말, `change_log.md` 구현 요약, 설계 문서 상태란 "구현 완료" |
| C4-2 | 전역 검증: `ctest` Debug·Release 전체, `cmake --preset vs2026-analysis` 빌드, `check_source_style.ps1`, `cmake --build --preset vs2026-debug`·`-release`, `cmake --install`로 `bin/gitman.exe`, 새 clone에서 준비 절차 재현(온라인·오프라인 각 1회), 빌드 로그 네트워크 취득 0건, 고지 파일 항목 확인, 실행 파일 크기 전후 비교 |
| C4-3 | 최종 검증 기록 `docs/verification/2026-09-06-luil-migration.md`와 보고 |

제안 커밋 메시지: `docs: luil 채택에 맞춰 빌드·배포 문서와 ADR을 갱신함`

## 7. 테스트 계획

- C1: 기존 전체 CTest(기준선), smoke 4종.
- C2-4: 전체 CTest Debug(프레임워크 교체라 영향 범위가 전부), smoke 4종, 수동
  시나리오.
- C3-1~C3-4: 각 커밋에서 추가·수정한 test만 (2026-08-20 지시).
- C4: 전체 CTest Debug·Release + analysis + style + 앱 빌드 + 새 clone 재현.
- 신설 test 목록은 C2-4d. 재작성 test의 공통 규칙: `ui_element_id { kind,
  u8string }`, `luil::interaction_controller { &policy, config }`, tree마다
  `duplicate_ids()`·`unarranged()` 빈 것 확인.

## 8. 검증 기록 형식

기존 형식(`docs/verification/2026-08-22-app-shell.md`)을 따른다: 변경 요약 표,
구현 경계, 실행한 test와 결과 수치, 수동 확인 항목과 결과, 알려진 제한, 다음
체크포인트. C2는 C2-1~C2-3이 단독 빌드되지 않음을 명시한다.

## 9. 위험과 되돌리기

- 브랜치에서 진행하므로 `main`은 영향이 없다. C2 도중 막히면 브랜치를 버리고
  설계로 돌아온다.
- Skia 152 API 차이는 C2-3 컴파일에서 드러난다. 그리기 API(SkCanvas·SkPaint·
  SkRRect·SkFont)만 쓰므로 작은 수정으로 예상한다.
- luil `/WX` 경고: submodule 핀을 고정하므로 현재 toolset에서는 없다. 새 toolset을
  올릴 때 부록 B의 옵션을 luil에 요청한다.
- 종료 hang·`__fastfail`: C2-2d의 `cancel`·`handle` 단축이 막는다. driver test와
  수동 Alt+F4·로그오프로 확인한다.
- 준비 절차 누락: C1과 C4에서 새 clone 재현으로 확인한다.

## 10. 승인

2026-09-06 승인. `feature/luil-migration` 브랜치를 만들어 C1부터 시작했다. 진행
기록은 `docs/verification/2026-09-06-luil-migration-cN.md`와 `change_log.md`에 있다.

2026-09-06 C1~C4 구현 완료. 커밋 12개, 전체 CTest 812건 Debug·Release 통과, 새
clone 재현(온라인·오프라인) 통과. 최종 검증 `docs/verification/2026-09-06-luil-migration.md`.
