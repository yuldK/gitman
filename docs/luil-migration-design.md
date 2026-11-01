# luil 채택 설계 — gitman UI 계층을 luil로 교체

상태: **구현 완료** — 2026-09-06 `feature/luil-migration` 브랜치, 최종 검증 [verification/2026-09-06-luil-migration.md](verification/2026-09-06-luil-migration.md). 검수 승인 (2026-09-05 작성. 같은 날 코드 대조 검토 3종 — 정확성·
완결성·실현성 — 의 지적을 반영한 2차 초안을 사용자가 검수해 D1~D8을 결정했다:
D1-B, D2-A, D3-A, D4-B(luil 공개 API는 후속 개선), D5-A, D6-A, D7-A, D8-B).
구현은 [luil-migration-plan.md](luil-migration-plan.md)의 순서를 따르며, 사용자
지시로 계획서 승인 뒤에 시작한다.

관련 문서: [ADR-007 초안](decisions/ADR-007-luil-ui-framework-adoption.md),
[ADR-006](decisions/ADR-006-restricted-network-dependency-provisioning.md),
[ui-element-design.md](ui-element-design.md), luil 쪽
`third_party/luil/docs/concepts/consumer-contract.md`(채택 후 경로).

## 0. 요약

- gitman의 UI 프레임워크(`presentation/ui` 기반 계층, `ui_theme`, `messaging`,
  `domain/{text_edit,utf8_text,appearance}`, `platform/win32`의 창·렌더러·TSF·
  클립보드·글꼴 코드)는 luil(https://github.com/yuldK/luil)의 **직계 조상**이다.
  luil은 같은 코드를 라이브러리로 일반화한 것이라 기반 계층은 삭제하고 luil을
  쓰면 되고, gitman에 남는 것은 앱 고유 element(카드·로그·시작 페이지·dialog
  7종·도구 막대)와 앱 조립(driver·policy·delegate)이다.
- Skia는 gitman이 직접 관리하지 않는다. submodule 8개(skia + externals 7),
  GN args, 패치, `build_skia.ps1`·`verify_skia_root.ps1`을 지우고, luil이
  `scripts/fetch_skia.ps1`로 받는 사전 빌드 패키지(skia-prep, Skia 152)를 쓴다.
  WebView2 SDK 패키지도 luil의 모든 구성에서 요구하므로 함께 받는다.
  CMake는 여전히 네트워크를 쓰지 않으며 취득은 사람이 실행하는 스크립트이므로
  ADR-006의 원칙은 유지된다 (ADR-007이 Skia 취득 방식만 대체한다).
- 규모(실측): 삭제되는 프레임워크 중복 코드 약 8,800줄(전달 헤더로 남는 domain
  헤더 셋을 빼면 약 8,500줄), luil API 위로 다시 쓰는 앱 element 28개 파일
  약 5,650줄. test는 75개 중 11개 삭제·12개 재작성·9개 수정·43개 유지
  (재작성 대상 약 6,350줄).
- 새로 쓰는 것: `logic_driver` 구현(logic_controller + task_scheduler 포장),
  `interaction_policy` 구현, `window_delegate` 구현, `app_ui_command` 표,
  worker 결과 중계(relay), 취소 registry, app kind 표, caption 설정 한 곳, main.cpp.
- 결정 8건은 4장에 기록했다 (2026-09-05 승인). 요지: Tab 초점은 선택 채택,
  dialog 뼈대는 `modal_host`, 컨텍스트 메뉴는 popup 창, luil 공개 API 추가는
  후속 개선(지금은 gitman 안에서 해결), IME 점진 검색 채택, nlohmann은 유지,
  자산은 gitman 사본 삭제, 진행은 별도 브랜치 위임.

## 1. 목표와 범위

목표

- gitman의 UI 단 코드가 luil의 공개 API(`include/luil/**`)만 쓰게 한다.
- gitman 저장소에서 Skia 소스·external·빌드 스크립트·패치·GN args를 없앤다.
- luil을 `third_party/luil` submodule로 두고 커밋을 고정한다.
- 화면 모양과 동작은 **가능한 한 지금과 같게** 옮긴다. luil이 더 잘하는 것
  (키보드 초점, 접근성, 팝업 닫힘 계기, 세션 종료 저장)은 5장의 "수용할 변화"로
  명시해 승인받고 들인다.
- UI와 무관한 계층(domain·application·infrastructure)은 luil의 **UI 헤더**
  (`luil/ui/**`, `luil/win32/{app_host,win32_window}.h`)를 include하지 않는다.
  값 타입만 담은 `luil/text/*`, `luil/theme/*`, `luil/messaging/*`,
  `luil/win32/renderer_policy.h`는 허용한다 (REQ-009의 검증 기준을 이렇게 고쳐
  적는다).

비범위

- luil의 HTTP client·WebView2·이미지·보조 창은 쓰지 않는다 (링크는 되지만 호출하지
  않는다).
- 새 기능 추가(드롭다운·탭·토스트 등)는 하지 않는다. 다만 luil 표준 요소가 기존
  요소와 **모양이 같으면** 교체한다 (L5).
- luil 자체는 바꾸지 않는다 (D4-B). 소폭 공개 API 추가는 후속 개선이다 (부록 B).
- 접근성(UIA)은 luil 표준 컨트롤(캡션 버튼·툴팁 있는 버튼·텍스트 칸)에 한해
  생긴다. gitman 커스텀 element의 `accessibility()` 재정의는 후속이다 (5장·부록 B).

## 2. 조사 결과

### 2.1 gitman의 UI 프레임워크와 luil의 대응

diff로 실측한 대응이다 (같은 이름·같은 계약, namespace만 다른 것은 "동일").

| gitman | luil | 차이 |
| --- | --- | --- |
| `messaging/{channel,envelope,latest_slot}.h` | `luil/messaging/*.h` (namespace `messaging` 그대로) | `latest_slot::set_signal_callback`이 `bool()`을 받는다 (전달 실패 시 다음 게시에서 재신호) |
| `domain/text_edit.*`, `domain/utf8_text.*` | `luil/text/*.h` (`luil::text`) | 동일. `text_edit_command` enum이 luil로 옮겨 감 (gitman `app_messages.h`의 사본과 이름·순서 동일) |
| `domain/appearance.*` | `luil/theme/appearance.h` | 동일 |
| `presentation/ui_theme.*` | `luil/theme/ui_theme.h` | 팔레트 역할 5개 추가(`secondary_foreground`·`disabled_foreground`·`divider`·`input_background`·`input_border`), `accent_for`가 값 반환, OS 고대비 색·시스템 accent 합성 추가. gitman이 읽는 역할 22개(최상위 16 + caption 6)는 전부 있음 (luil은 27개) |
| `presentation/renderer_policy.*` | `luil/win32/renderer_policy.h` | 동일 |
| `presentation/ui/ui_element.*` | `luil/ui/ui_element.h`, `ui_element_id.h` | `ui_element_id::owner`가 `project_id`→`std::u8string`; kind는 라이브러리 예약 0~63 + `application_element_kind(index)`; `animating()`→`next_update()`; `drag_payload::dragged_project`→`dragged_owner`; 초점·Tab·가둠·기본 버튼·커서·접근성 API 추가 |
| `presentation/ui/ui_tree.*` | `luil/ui/ui_tree.h` | `root()`가 포인터; `duplicate_ids()`·`unarranged()` 진단; `make_arranged_tree` |
| `presentation/ui/ui_events.h` | `luil/ui/ui_events.h` | 모든 이벤트에 `surface`; `key_code` 확장(+tab·space·page·F1~F12·platform 대역); `ui_command`는 창 명령 3개만 남고 앱 명령은 `app_ui_command{uint32, u8string}`; `input_action`의 `logic_message` 자리가 `app_message`(불투명 상자) |
| `presentation/ui/ui_interaction.*` | `luil/ui/ui_interaction.h` | 상태 기계는 라이브러리가 소유. 앱은 `interaction_policy` 파생 클래스만 쓴다 |
| `presentation/ui/draw_primitives.*` | `luil/ui/draw_primitives.h` | `font_fallback::for_codepoint`가 `sk_sp` 반환; `draw_hover_fill` 추가. `codicon_for_name`은 gitman 전용이라 남긴다 |
| `button/label/scrollbar/caption_element`, `caption_metrics.h`, `dialog_elements.*` | 같은 이름의 luil 요소 | config 구조체로 바뀜(`caption_config`·`scrollbar_config`·`text_button_config`·`text_input_config`). `dialog_caption_element`는 새것 |
| `platform/win32/{win32_application,win32_app_runtime,caption_layout,skia_renderer,cpu_skia_renderer,direct3d_skia_renderer,embedded_assets,win32_fonts,win32_tsf_input,win32_clipboard}.*` | luil `src/win32/*` (비공개) + `luil/win32/{app_host,win32_window}.h` | 창·렌더러·입력·TSF·클립보드·글꼴 registry·타이머·DPI·테마 감지가 전부 라이브러리 안이다 |
| `presentation/skia_smoke_view.*` | `window_config::smoke_test` | luil이 caption만 그리는 smoke 화면을 낸다. 종료 코드 77 계약 동일 |
| `assets/codicons/*`, `assets/accents.json`, `cmake/generate_{codicons,accents}.cmake`, `verify_codicons.cmake` | luil의 같은 파일 | 자산은 **byte 단위로 동일**. 생성 헤더 namespace만 `luil::codicons`·`luil::generated` |

`platform/win32/utf8.{h,cpp}`(UTF-8↔UTF-16)는 luil에서 비공개라 **gitman이 그대로
유지한다** — 프로세스 실행·파일 시스템·파일 연결 등 UI가 아닌 platform 코드 8개
파일이 쓴다.

### 2.2 luil이 라이브러리 안에서 맡는 것

gitman의 `win32_application.cpp`(1,553줄)와 `win32_app_runtime.cpp`(328줄)가
하던 일 가운데 다음은 luil의 `run_application_window`·`app_host`가 그대로 한다.
gitman은 코드를 쓰지 않는다.

- 창 class·생성·custom caption·비클라이언트 hit test·시스템 메뉴·최소 크기·DWM
  프레임·DPI 변경·최대화 전환 보고.
- 렌더러 선택(auto/direct3d/cpu)·fallback·smoke test(종료 코드 77)·런타임 장치
  손실 후 CPU 복귀(gitman에 없던 것).
- 포인터·휠·키·문자·surrogate 병합·TSF 인라인 조합·클립보드 복사/붙여넣기
  (붙여넣기는 `policy::make_text_edit_action`으로 앱 메시지가 된다).
- 글꼴 registry(`ui_frame::fonts` → `set_configured_fonts`), 글리프 대체 해석기
  등록, `WM_FONTCHANGE` cache 무효화(gitman에 없던 것), `draw_context::fonts`
  (글꼴 dialog의 행별 미리 보기가 그대로 동작).
- 테마: 밝은 모드 레지스트리, 고대비 시스템 색, OS accent 색 추적, 글꼴 다듬기
  품질.
- 타이머: element의 `next_update()` 최솟값에 timer 하나. render 뒤마다 다시
  계산해 걸므로 "다음 초 경계"처럼 매번 달라지는 예고도 동작한다 (gitman의
  tooltip·caret·경과 시간·spinner 타이머 4개를 대체).
- 창 배치 저장·복원(revision 비교), 파일 드롭(`IDropTarget` + delegate fallback),
  `WM_QUERYENDSESSION`/`WM_ENDSESSION` 종료 저장(gitman에 없던 것), UIA 접근성.
- input thread·logic thread·채널·slot 조립과 종료 순서(`app_host::shutdown`).

### 2.3 luil 소비자 계약 요점

`third_party/luil/docs/concepts/consumer-contract.md`와 `examples/CMakeLists.txt`에서
확인한 것이다.

- `add_subdirectory(third_party/luil)` 뒤 `luil::luil`을 링크한다. Skia·nlohmann
  (single_include)·WebView2 로더·Windows 시스템 라이브러리 전부가 PUBLIC으로
  따라온다. gitman의 `unofficial::skia::skia`와 시스템 라이브러리 목록은 사라진다.
- 실행 파일마다 luil의 `.rc` 조각(코디콘 폰트·라이선스·제3자 고지, ID 101~104)이
  필요하다. `luil_generate_third_party_notices`·`luil_configure_resource_script`를
  부르기 전에 호출 scope에 `LUIL_SOURCE_DIRECTORY`·`LUIL_ASSET_DIRECTORY`·
  `LUIL_NLOHMANN_JSON_ROOT`를 다시 정해야 한다 (하위 디렉터리의 일반 변수는
  부모로 올라오지 않는다). `LUIL_SKIA_ROOT`·`LUIL_WEBVIEW2_ROOT`·
  `LUIL_ACCENT_CATALOG`는 cache라 보인다.
- 부모가 `project(... LANGUAGES CXX RC)`와 정적 CRT(`/MT`·`/MTd`)를 써야 한다.
  gitman은 이미 둘 다 만족하고, luil은 하위 프로젝트일 때 CRT를 건드리지 않는다
  (`if(NOT DEFINED PROJECT_NAME)` 가드).
- Skia 패키지: `scripts/fetch_skia.ps1 -Configuration Debug,Release` (Release zip
  25.7 MB, Debug zip 204 MB, 풀면 약 1 GB). WebView2: `scripts/fetch_webview2.ps1`
  (8.8 MB). 둘 다 SHA-256 고정, `-ArchiveDirectory`로 브라우저로 받은 zip을
  오프라인 설치할 수 있다. CMake configure는 파일 존재만 검사한다.
- luil의 중첩 submodule 중 `third_party/nlohmann_json`은 모든 구성에서 필수
  (`luil_find_nlohmann_json`이 무조건 실행), `catch2`는 luil test 전용이라 받지
  않는다 (`--recursive` 대신 개별 init).

### 2.4 확인된 공백 — luil 공개 API에 없는 것

| 항목 | 상태 | 영향 |
| --- | --- | --- |
| `installed_font_families()` | 처음엔 `src/win32/win32_fonts.h` 비공개였다. luil `c971f32`(2026-09-06)가 `luil/text/fonts.h`로 공개했다 | gitman은 `luil::installed_font_families()`를 부른다 (L3 글꼴) |
| 기본 UI 글꼴 우선순위 | luil 기본은 `Segoe UI`. gitman은 `Malgun Gothic`→`Segoe UI` (`embedded_assets.cpp:47-60`). luil `c971f32`가 `luil::set_ui_typeface_families`를 공개했다 | `main.cpp`가 앱 시작 전에 `{ u8"Malgun Gothic" }`을 지정한다. 설치되지 않았으면 luil이 `Segoe UI`로 물러선다 (L3 글꼴) |
| drag 중 drop 대상 강조 | 처음엔 `plan_drag_overlay`가 `highlight_target`을 항상 켰다. luil `c971f32`가 `drag_payload::suppress_drop_highlight`를 더했다 | 카드 drag payload에 `suppress_drop_highlight = true`를 넣어 지금처럼 강조 없이 그린다 (L3 글꼴·drag) |
| `verify_embedded_resources` 등 | `src/win32/embedded_assets.h` 비공개 | gitman의 `embedded_assets_tests`를 같은 모양으로 다시 쓸 수 없다 (L9) |
| `scrollbar_config::scroll_to` 없이는 UIA `SetValue` 불가 | 절대 위치 intent가 gitman에 없다 (모두 delta) | 이번 이식에서는 비워 둔다 (5장·부록 B) |
| `/WX` 끄는 옵션 | 없음 | 새 toolset의 luil 경고가 gitman 빌드를 막을 수 있다 (부록 B) |
| fetch 스크립트 `-Offline` | 없음. 아카이브가 없으면 HTTP를 한 번 시도한 뒤 안내 | ADR-006 취지에는 맞지만 시도 자체는 일어난다 (부록 B) |
| 시작 오류 메시지 | `luil startup error` 제목에 원문 오류 | gitman의 한국어 브랜딩 메시지가 사라진다 (5장) |

### 2.5 스레드와 종료 (luil 소스로 확인)

- `app_host` 생성자가 logic thread를 **바로** 시작하고(`app_host.cpp:173`) 그
  thread가 첫 줄에서 `driver.start()`를 부른다(`:435`). `window_delegate::
  on_started(host)`는 그 뒤 UI thread의 `run()`에서 불린다
  (`win32_window.cpp:597-599`). 따라서 `start()`가 제출한 worker 작업의 결과는
  host를 알기 전에 도착할 수 있다.
- `app_host::post_app_message`는 어느 thread에서나 안전하지만 **inbox(1024,
  reject_newest)가 차면 조용히 버린다** (`void`, 통계로만 관찰). gitman의
  `task_scheduler::emit_to_logic`은 지금 무한 재시도라(`task_scheduler.cpp:155-167`)
  그대로 옮기면 폭주 시 결과 유실이 생긴다.
- logic loop는 `next_tick()`을 **frame을 게시한 뒤에만** 다시 묻고, 예고 시각이
  되면 `handle`이 아니라 `tick(now)`을 부른다 (`app_host.cpp:437, 474-484`).
  닫힌 inbox도 남은 메시지를 전부 `received`로 내주며 배치 64개마다
  `handle`을 부른다 (`channel.h:139-150`, `app_host.cpp:462-470`).
- 종료 순서는 gitman과 같은 골격이다: cancel → close 메시지(재시도) → `shutdown_
  completed` 대기(3초) → `stop_workers` → inbox 닫고 logic join(1초 예산, 초과면
  `__fastfail`) → input join. `stop_workers`는 시간 제한이 없으므로 취소가 먼저
  전파되어야 한다. `cancel()`은 close 메시지를 넣기 **전에** 불린다
  (`app_host.cpp:227` vs `:237`).
- 시작 메시지 순서가 바뀐다: luil은 `post_window_metrics()`를 `on_started`보다
  먼저 보내므로 `window_metrics_intent`가 `font_families_event`·
  `open_document_intent`보다 앞선다. `logic_controller`는 창 크기만 받아 두므로
  무해하다.

## 3. 결정 사항 (제안)

### L1. 의존성과 취득

| 항목 | 결정 |
| --- | --- |
| luil | `third_party/luil` submodule, `https://github.com/yuldK/luil.git`, 커밋 `c971f32` (2026-09-06, 0.1.0 + 글꼴 공개 API·drag 강조 opt-out·`LUIL_DEFAULT_ACCENT_ID`) 고정. 설계 초안의 핀 `a15aaa18`은 사용자가 D4 후속 항목을 luil에 반영하면서 올렸다 |
| Skia | gitman의 `third_party/skia`, `skia-externals/*`(7개), `skia-args/`, `patches/`, `scripts/build_skia.ps1`, `scripts/verify_skia_root.ps1`, `cmake/dependencies/skia.cmake`, `GITMAN_SKIA_*` 변수 전부 삭제(C2). Skia는 luil의 skia-prep 패키지(milestone 152, rust png 코덱)로만 온다 |
| WebView2 SDK | luil이 모든 구성에서 요구하므로 `fetch_webview2.ps1`도 준비 절차에 넣는다. gitman은 호출하지 않지만 정적 로더(수백 KB)가 실행 파일에 들어가고 고지에 항목이 는다 |
| nlohmann/json | gitman의 `third_party/nlohmann-json` submodule과 `nlohmann_json::nlohmann_json`을 **이번에는 유지**한다 (모든 구성에서 필수 — 준비 절차에 init이 들어간다). 같은 커밋(v3.12.0)이고 include guard·ABI 태그가 같아 luil의 사본과 충돌이 없다. 합치는 것은 별도 후속 정리로 미룬다 (D6) |
| Catch2 | gitman의 submodule과 `cmake/dependencies/catch2.cmake` 유지. `LUIL_BUILD_TESTS`는 절대 켜지 않는다 (같은 `${CMAKE_BINARY_DIR}/third_party/catch2` 바이너리 디렉터리로 add_subdirectory가 충돌한다) |
| 준비 절차 | 아래 코드 블록. 오프라인이면 zip 3개를 브라우저로 받아 `third_party/luil/third_party/{skia-prep,webview2-prep}-archives/`에 둔다 |
| ADR | ADR-007 신설(초안 첨부). ADR-006은 원칙을 재확인하되 Skia 취득 절은 "ADR-007로 대체"로 표시. `docs/dependency-provisioning-design.md`는 폐기 표시. ADR-002의 Skia 행은 152·패키지 출처로 갱신. ADR-005의 ASan 조항은 luil 저장소 CI로 위임한다고 명시 (L9) |

```powershell
git submodule update --init third_party/nlohmann-json          # 모든 구성 필수
git submodule update --init third_party/luil
git -C third_party/luil submodule update --init third_party/nlohmann_json
third_party\luil\scripts\fetch_skia.ps1 -Configuration Debug,Release
third_party\luil\scripts\fetch_webview2.ps1
git submodule update --init third_party/catch2                 # test 구성일 때만
cmake --preset vs2026
```

취득 지점은 submodule 4개(gitman의 nlohmann-json·catch2, luil, luil의
nlohmann_json)와 해시 고정 아카이브 2개다. 새로 확인해야 하는 접근 대상은
`github.com/yuldK/skia-prep` 릴리스와 `api.nuget.org` 둘이다. 대신 ADR-006이
미확인으로 남겼던 `chromium.googlesource.com`(ICU)과 해시 없는 `gn`·`ninja`
바이너리가 없어진다.

### L2. 빌드

각 항목 뒤의 [C1]/[C2]는 6장의 체크포인트다. **C1에서는 어떤 gitman target도
`luil::luil`을 링크하지 않는다** — gitman은 자기 Skia(148)로 빌드되고 luil은
자기 Skia(152)로 빌드되어 공존한다. 두 Skia가 한 TU나 한 링크 줄에 함께 서는
것은 C2에서 gitman의 Skia를 지우는 순간뿐이다.

최상위 `CMakeLists.txt` [C1]

```cmake
# project() 앞의 정적 CRT 설정은 그대로 둔다 (luil이 존중한다).
set(LUIL_BUILD_TESTS OFF)      # 반드시 OFF (Catch2 충돌)
set(LUIL_BUILD_EXAMPLES OFF)
set(LUIL_BUILD_TOOLING OFF)
# LUIL_INSTALL은 PROJECT_IS_TOP_LEVEL=FALSE라 이미 OFF
add_subdirectory(third_party/luil SYSTEM)   # SYSTEM: luil 헤더를 /W4·/analyze 밖에 둔다
# luil의 리소스 함수가 호출 scope에서 읽는 세 변수다. 하위 디렉터리의 일반
# 변수는 부모로 올라오지 않으므로 여기서 다시 정한다.
set(LUIL_SOURCE_DIRECTORY   "${PROJECT_SOURCE_DIR}/third_party/luil/src")
set(LUIL_ASSET_DIRECTORY    "${PROJECT_SOURCE_DIR}/third_party/luil/assets/codicons")
set(LUIL_NLOHMANN_JSON_ROOT "${PROJECT_SOURCE_DIR}/third_party/luil/third_party/nlohmann_json")
# src/와 tests/가 함께 쓰므로 최상위에서 한 번 만든다.
set(GITMAN_THIRD_PARTY_NOTICES "${GITMAN_GENERATED_DIRECTORY}/third_party_notices.txt")
set(GITMAN_GENERATED_LUIL_RESOURCE "${GITMAN_GENERATED_DIRECTORY}/luil_resources.rc")
luil_generate_third_party_notices("${GITMAN_THIRD_PARTY_NOTICES}")
luil_configure_resource_script("${GITMAN_GENERATED_LUIL_RESOURCE}" "${GITMAN_THIRD_PARTY_NOTICES}")
add_subdirectory(src)
```

- `add_subdirectory(... SYSTEM)`이 없으면 `vs2026-analysis` preset이 luil 공개
  헤더에 `/analyze /WX`를 돌린다. luil은 그 조건으로 검증된 적이 없다.
- `gitman/share/luil` 디렉터리를 만들면 안 된다. luil의 리소스 함수가 그 경로를
  설치본 배치로 오인한다.
- `LUIL_ACCENT_CATALOG`는 덮어쓰지 않는다 (자산이 동일하므로 gitman 사본 삭제).
- C1에서 gitman의 자체 고지 생성(`gitman_generate_third_party_notices`)은 luil 고지
  생성으로 바꾼다 — 그 시점에 luil 고지는 gitman.exe가 아직 링크하지 않는
  WebView2·Skia 152를 담지만, C2가 곧 따라오므로 검증 기록에 적고 넘어간다.

`src/CMakeLists.txt`

- [C1] `.rc` 분리: `gitman.rc.in`은 manifest·`IDI_GITMAN` 아이콘·VERSIONINFO만
  남기고 RCDATA 4줄을 지운다. `resource_ids.h`는 `IDI_GITMAN 105`만 남긴다.
  `add_executable(gitman WIN32 "${GITMAN_GENERATED_LUIL_RESOURCE}"
  "${GITMAN_GENERATED_RESOURCE}" platform/win32/main.cpp ...)`. 규칙: 101~104는
  luil, 105는 아이콘, 이후 gitman 리소스는 106부터. gitman의 `embedded_assets.cpp`는
  C2까지 같은 ID(101~104)로 luil 조각의 리소스를 읽으므로 C1에서도 동작한다.
- [C2] 삭제: 코디콘·accent `add_custom_command`, `verify_codicons` PRE_BUILD,
  2.1 표의 프레임워크 파일 목록, `GITMAN_ASSET_*`·`GITMAN_ACCENT_*`·
  `GITMAN_GENERATED_CODICONS/ACCENTS`.
- [C2] 링크: `target_link_libraries(gitman_lib PUBLIC gitman_project_options
  nlohmann_json::nlohmann_json luil::luil advapi32)`. `unofficial::skia::skia`와
  시스템 라이브러리 10개를 지운다. `advapi32`는 `win32_file_association.cpp`의
  `Reg*` 때문에 명시한다.
- [C1] `tests/CMakeLists.txt`: `gitman_tests`에도 `${GITMAN_GENERATED_LUIL_RESOURCE}`를
  넣는다 (luil test와 같은 방식). [C2] `gitman_assets_checksum` 삭제(자산이
  luil로 감), `gitman_messaging_asan_tests` 삭제(gitman이 messaging 헤더를
  소유하지 않는다. 같은 검사는 luil 저장소의 `luil_messaging_asan_tests`가 자기
  CI에서 돈다 — gitman 빌드에서는 돌지 않으므로 ADR-005에 위임을 적는다).
  smoke test 4개와 `gitman_source_style`은 그대로.
- [C2] `cmake/dependencies.cmake`: skia include·호출 제거. `cmake/{generate_notices,
  generate_codicons,generate_accents,verify_codicons}.cmake` 삭제.
- `CMakePresets.json`: 변경 없음 (luil의 `LUIL_SKIA_ROOT` 기본값이 submodule 안을
  가리킨다).
- [C1] `.gitmodules`: luil 추가. [C2] skia 8개 제거. [C1] `.gitattributes`:
  `/third_party/luil/** -text` 추가. [C2] `*.gn` 줄과 skia 두 줄 제거.
  [C2] `.gitignore`: `/third_party/skia-tools/` 제거 (skia-prep 등은 luil 자체
  `.gitignore`가 막는다).
- scripts: [C2] `build_skia.ps1`·`verify_skia_root.ps1`·`fetch_codicons.ps1` 삭제.
  [C1] `normalize_line_endings.ps1`의 제외 목록에 `third_party` 추가(luil 사본과
  같게). [C2] `generate_app_icon.py` 사용법의 폰트·매핑 경로를
  `third_party/luil/assets/codicons`로.
- [C2] assets: `assets/codicons/`·`assets/accents.json` 삭제, `assets/gitman.ico`만
  남긴다.

### L3. 앱 조립

새 파일(제안 경로)과 역할이다.

| 파일 | 역할 |
| --- | --- |
| `platform/win32/gitman_driver.{h,cpp}` | `luil::win32::logic_driver` 구현. 지금 `app_runtime::assembly`가 소유하던 그래프를 멤버로 소유한다. UI thread에서 생성, `start/handle/make_frame/tick`만 logic thread |
| `platform/win32/operation_result_relay.{h,cpp}` | worker → logic 중계 큐. mutex 하나로 `post(m)`·`bind(host)`·`unbind()`·`drain(sink)` |
| `application/cancellation_registry.{h,cpp}` | 종료·변경 작업·탐색의 `process_cancellation_source`를 등록하는 thread-safe 보관소. `logic_controller`가 참조로 받아 등록하고 driver가 `cancel()`에서 전부 취소한다 |
| `presentation/ui/gitman_policy.{h,cpp}` | `luil::interaction_policy` 구현 (L7) |
| `platform/win32/gitman_delegate.{h,cpp}` | `luil::win32::window_delegate` 구현. 파일/폴더 dialog·파일 연결·외부 열기·창 배치·창 크기 메시지·드롭 |
| `application/app_commands.h` | `enum class app_command : std::uint32_t` (L7 표)와 `app_ui_command` 만들기 도우미 |
| `presentation/ui/element_kinds.h` | app kind 상수 표 (L4) |
| `presentation/ui/gitman_caption.h` | `inline const luil::caption_config gitman_caption` 한 곳. `window_config::caption`(비클라이언트 hit test·창 제목)과 tree의 `caption_element`(그리기)가 같은 값을 읽는다 |
| `platform/win32/main.cpp` | 다시 씀 (아래) |

메시지 운반

- `luil::app_message`에 **`logic_message` variant를 통째로** 담는다:
  `app_message { logic_message { intent } }`, 받는 쪽은
  `message.get<logic_message>()` 뒤 지금처럼 `std::visit`. variant의 exhaustiveness가
  그대로 남는다. element 쪽 도우미 `make_logic_action(intent)`가
  `luil::make_app_action(logic_message { intent })`를 감싼다.
- `task_scheduler`의 생성자 인자 `messaging::channel<logic_message>&`를
  `std::function<void(logic_message)>` (relay의 `post`)로 바꾼다. worker 코드는
  그대로다. `task_scheduler_tests`의 생성 5곳이 기록용 sink를 넘기도록 고친다.
- relay가 큐의 정본이다. `post(m)`은 lock 안에서 큐에 넣고, host가 묶여 있으면
  `post_app_message(logic_message { relay_drain_intent {} })`로 **깨우기만** 한다
  (내용은 큐에 남는다). `handle`과 `tick`이 **둘 다** 첫 줄에서 `drain`으로 큐를
  비워 `logic_controller::handle`에 순서대로 넘긴다. 깨우기 메시지가 inbox 포화로
  버려지는 경우는 이미 1,024개 이상의 메시지가 대기 중이라는 뜻이라 곧 `handle`이
  불리고 그때 비워진다 — `next_tick`에 기대지 않는다 (luil은 frame 게시 뒤에만
  `next_tick`을 다시 묻고 예고 시각에는 `tick`을 부르므로 그 경로로는 보장할 수
  없다). `bind(host)`는 한 lock 안에서 host를 세우고 깨우기 하나를 보낸다.

driver 계약

- 생성 순서(=파괴 역순): file_system → resolver·runner·probe·enumerator → store →
  app_settings_store → log_files → log_writer → executor → cancellation_registry →
  relay → scheduler → **logic_controller 마지막** (scheduler와 log_writer를 참조로
  든다). `logic_controller`는 UI thread에서 생성되지만 `start/handle/make_frame/
  tick`만 건드린다.
- `start()`는 **비워 둔다**. 지금 `logic_controller::start()`가 하던 앱 설정 읽기
  제출은 `on_started(host)`가 보내는 `startup_intent`(신설)를 `handle`이 받아 한다.
  이러면 relay가 host를 알기 전에 결과가 오는 창이 원천적으로 없어진다.
- `on_started(host)`의 **첫 문장은 `driver.bind_host(host)`** 다. 그다음
  `startup_intent`, `font_families_event`(gitman 자체 열거 — 아래 글꼴), 명령행 문서
  `open_document_intent`(`absolute_workspace_document_path`를 거친다) 순으로 게시.
- `handle(app_message)`: relay `drain` → `get<logic_message>()` →
  `shutting_down_`이면 `close_intent`만 `logic_controller::handle`에 넘기고 나머지는
  버린다 (`cancel()`이 close 게시보다 먼저 불리므로 close 도착 시점에 이미
  `shutting_down_`이다). `close_intent` 처리 직후 `std::atomic<bool>
  shutdown_completed_`를 세운다 (`controller.shutdown_requested()`를 UI thread에서
  읽으면 data race).
- `make_frame()`: `shutting_down_`이면 마지막 frame을 그대로 돌려준다. 아니면
  `make_view_snapshot()` → `build_ui_tree(view)` → `ui_frame { tree, popups(컨텍스트
  메뉴, L6), appearance, fonts, window_placement_request/revision }`. UI thread가
  view를 읽는 경로는 두지 않는다 — 로그 복사는 element 액션이, discovery 시작
  폴더는 `app_ui_command::argument`가 나른다 (L7).
- `cancel() noexcept` (UI thread): `shutting_down_`을 세우고 `try { registry.
  cancel_all(); } catch (...) {}` (`request_cancellation`은 noexcept가 아니다).
  `begin_shutdown()`의 기존 취소 호출은 멱등이라 그대로 둔다. 이것이 close 메시지가
  유실됐을 때 `stop_workers`의 join이 무한 대기하는 유일한 hang을 막고, 닫힌
  inbox를 소진하는 동안 `handle`이 무거운 일을 하지 않게 해 1초 예산 안에 logic
  thread가 빠져나오게 한다.
- `make_close_message()`: `app_message { logic_message { close_intent {} } }`.
- `stop_workers()`: `scheduler.shutdown()` 뒤 `relay.unbind()`.
- `next_tick()`·`tick()`: 기본(nullopt)이다. `tick`은 relay `drain`만 한다.

`logic_controller` 변경(부록 A 재작성 목록): `cancellation_source_`와 card별
`change_cancellation`, 탐색 취소를 `cancellation_registry`에 등록하도록 바꾼다.
`start()`의 제출은 `startup_intent` 처리로 옮긴다.

main.cpp

- `enable_per_monitor_dpi_awareness()` → `com_sta_scope`(OleInitialize; 드롭·UIA에
  필요) → `command_line_arguments()` → 기존 `parse_application_options`.
- `--register/--unregister-file-association`은 지금처럼 창 없이 처리한다.
- `window_config`: `class_name = L"Gitman.Window"`, `caption = gitman_caption`
  (제목 `Gitman`, `application_icon = icon_source_control`, 툴팁 3개),
  `initial_width/height 1280×720`, `minimum_client 480×320`, `resize_border 4/
  corner 10`, `icon_resource = IDI_GITMAN`, `renderer/smoke_test/
  simulate_direct3d_failure` ← options, `accept_file_drop = true`.
- `window_environment { &driver, &policy, &delegate }` → `run_application_window`.

delegate

- `execute_app_ui_command`: `app_command`로 switch. dialog 본체(IFileOpenDialog
  코드)는 지금 것을 옮긴다. 소유 창은 `GetActiveWindow()` (luil 예제와 같다).
  문서 열기 dialog·드롭·명령행 셋 모두 `absolute_workspace_document_path()`를
  거쳐 `open_document_intent`를 만든다.
- `make_window_metrics_message` → `window_metrics_intent`,
  `make_window_placement_message` → `window_placement_intent`,
  `on_file_dropped` → `.version-list` 확장자면 절대 경로로 `open_document_intent`, true.

글꼴·drag (D4-B → luil `c971f32`)

D4-B는 gitman 안에서 우회하는 결정이었으나, 사용자가 2026-09-06에 luil 공개 API를
더해(`c971f32`) 우회가 필요 없어졌다. 이 커밋으로 핀을 올리고 다음처럼 쓴다.

- 글꼴 목록: delegate의 `on_started`가 `luil::installed_font_families()`
  (`luil/text/fonts.h`)를 한 번 불러 `font_families_event`를 게시한다. luil이
  DirectWrite로 열거하고 끝 NUL 제거·UTF-8 검사·이름순 정렬을 한다. gitman의
  자체 열거 파일은 두지 않는다.
- 기본 UI 글꼴: `main.cpp`가 `run_application_window` 전에
  `luil::set_ui_typeface_families({ u8"Malgun Gothic" })`를 부른다. luil registry는
  설치되지 않은 이름을 내장 글꼴(`Segoe UI`)로 되돌리므로 지금의
  `Malgun Gothic`→`Segoe UI` 순서가 재현된다. frame의 `ui_family`는 유효 설정
  값을 그대로 넣는다 (비어 있으면 빈 채로 — luil이 기본 글꼴을 쓴다). 저장된
  설정과 환경설정 화면의 `(기본 글꼴)` 표시는 그대로다.
- drag 강조: 카드 drag payload에 `suppress_drop_highlight = true`와
  `custom_visual = true`를 함께 넣는다. luil은 drop 대상 테두리와 목록·탭 배경을
  그리지 않고 hit test와 drop 액션만 유지하므로 카드 목록의 삽입 틈·ghost는 지금
  코드가 그린다.
- 기본 accent: gitman의 기본 `mint`가 luil의 `LUIL_DEFAULT_ACCENT_ID` 기본값과
  같으므로 덮어쓰지 않는다.

### L4. 공통 타입 전환

- 전달 헤더: `domain/appearance.h`, `domain/text_edit.h`, `domain/utf8_text.h`는
  본문을 지우고 luil 헤더를 include한 뒤 `using luil::theme_preference; ...`,
  `using luil::text::text_edit_state; ...` 선언만 둔다. `app_settings`·`project`·
  `logic_controller`·JSON 저장소·그 test들이 `gitman::` 한정자를 그대로 쓴다.
  `domain_model_tests`의 `gitman::apply_overrides` 두 overload도 그대로 컴파일된다.
  `messaging/`는 헤더 3개를 지우고 `#include "luil/messaging/..."`로 바꾼다
  (namespace `messaging` 동일). `presentation/renderer_policy.*`는 삭제하고
  `application_options.h`가 `luil/win32/renderer_policy.h`를 include한다 (값
  타입 헤더라 1장의 허용 범위다).
- application 계층은 luil **UI** 헤더를 include하지 않는다. 그래서
  `edit_text_input_intent`·`set_text_composition_intent`·`text_input_target`은
  지금처럼 gitman의 POD로 남기고(command 필드만 `luil::text::text_edit_command`),
  변환은 `gitman_policy`(`text_edit_request`→intent, `text_composition_event`→intent)
  한 방향에서만 한다. logic의 편집 적용은 지금 코드(`text_edit_*` 함수 switch)를
  그대로 쓴다 — luil의 `apply_text_edit`는 `luil/ui/ui_interaction.h`에 있어 쓰지
  않는다. 제한 시간 칸의 숫자 4자리 규칙(전체 글 정규화)은 지금 코드 그대로다
  (luil의 `text_insert_filter`는 들어오는 조각만 보므로 표현할 수 없다).
- `text_input_target`: `enum class : std::uint32_t { none = 0, settings_timeout = 1, switch_query, font_filter, generate_document_name }`.
  0을 쓰면 luil의 `none`과 겹친다. `static_cast`는 policy 한 곳에서만.
- `text_edit_command`·`text_input_view`: gitman 사본을 지우고 luil의 것을 쓴다
  (`view_snapshot.h`는 `luil/ui/text_input_state.h`를 include — element 어휘가
  없는 값 헤더라 허용한다).
- 코디콘: `#include "luil/generated/codicons.h"`, 호출부 81곳을 `luil::codicons::`로
  명시 치환. `ui_theme`은 luil의 것을 그대로 쓴다.
- app kind 표(`element_kinds.h`): 현재 enum 105개 중 앞 7개(`none`~`caption_close`)는
  luil 예약 kind와 이름·값이 같아 그대로 쓰고, 나머지 98개에서 luil 요소가 맡는
  것(scrollbar 6종→luil `scrollbar_element`의 앱 kind 하나 + owner,
  `context_menu*`→`menu`/`menu_item`(D3), dialog 7종의 overlay/panel→
  `modal_host`/`modal_scrim`+owner(D2))을 빼고
  `inline constexpr luil::ui_element_kind kind_toolbar { luil::application_element_kind(0) };`
  꼴로 나열한다. 토글 3종은 앱 kind로 남는다 (L5). index 중복을 막는
  static_assert(또는 test)를 둔다.
- `ui_element_id::owner`: `project_id`→`std::u8string`. 카드류는 `id.value`,
  index 기반 행은 지금의 문자열 규칙 그대로. `drag_payload::dragged_owner`도 같다.
- 스크롤 단위: luil 컨테이너·막대에 넘기는 값은 **논리 픽셀**이다. gitman의
  카드 목록·로그 pane이 `scroll_offset * scale`로 미리 곱하던 것을 없앤다.
  `list_metrics.h`의 `clamp_scroll_offset`은 `luil::clamp_scroll`(인자 순서 다름)로
  대체하고 `scroll_offset_showing_card`(여백 규칙 포함)는 남긴다.
  `layout_scrollbar_width`·`layout_scrollbar_minimum_thumb`는 luil 상수를 쓴다.
  `scrollbar_config::scroll_to`(절대 위치)는 이번 이식에서 **비워 둔다** — gitman의
  스크롤 intent 8종이 전부 delta라 절대 intent를 새로 두는 일은 후속이다
  (5장·부록 B).
- `list_metrics.{h,cpp}`의 나머지(카드·dialog 치수, `compute_list_layout`, drag 슬롯
  계산)는 gitman 소유로 남는다. `layout_caption_height`는
  `luil::caption_element::height_for`로 바꿔 수동 동기화를 없앤다.

### L5. element 이식 방침

원칙: **모양 보존 우선**. luil 표준 요소는 지금 요소와 모양·동작이 같을 때만
바꾸고, 다르면 luil의 `ui_element` 위에 지금 그리기 코드를 그대로 얹는다
(palette 역할 22개와 `draw_context::fonts`가 동일하므로 draw 본문은 namespace와
`sk_sp` 필드 정도만 바뀐다). D2·D3에서 승인되면 dialog 뼈대와 메뉴는 luil
요소로 재구성한다.

| gitman 파일 | 처분 | 근거 |
| --- | --- | --- |
| `button_element`, `label_element`, `scrollbar_element`, `caption_element`, `caption_metrics.h`, `dialog_elements`(text_button·text_input), `draw_primitives`, `ui_element`, `ui_tree`, `ui_events.h`, `ui_interaction` | **삭제**, luil 사용 | 동일 계약. `codicon_for_name`만 `presentation/ui/status_glyphs.{h,cpp}`로 옮긴다 |
| `build_ui_tree` | luil 위로 재작성 | root는 `luil::root_element`. 자식 순서·배경 클릭(선택 해제)·caption 마지막 배치 유지. caption은 `gitman_caption` 하나를 읽는다. dialog 뼈대는 D2 |
| `toolbar_element` | 커스텀 유지, 버튼·라벨만 luil | 좁을 때 오른쪽부터 숨기는 규칙은 luil stack에 없다 |
| `card_element` | 커스텀 유지, 버튼 3개만 luil | 3줄 텍스트·상태 조각·spinner·drag 출발 |
| `card_list_element` | 커스텀 유지 | 가상화·삽입 틈·들린 카드 ghost. `luil::list_element`는 전 항목을 tree에 담아 가상화가 없고 행 내용이 label+icon뿐 |
| `log_view_element` | 커스텀 유지, 헤더 버튼·막대는 luil | 로그 본문 렌더링에 대응 요소 없음. 복사 버튼은 `clipboard_copy_request`를 클릭 시점에 만든다 |
| `start_page_element` | 커스텀 유지 | `text_button_element`는 항상 채워진 상자·가운데 정렬이라 모양이 다르다. pin/제거 아이콘은 행 hover 규칙 때문에 자체 그리기 |
| `context_menu_element` | D3 | popup이면 삭제하고 `menu_element`; overlay면 backdrop만 남기고 panel을 `menu_element`로 |
| `notice_dialog_element` | D2 뼈대 + 본문 커스텀, 확인 버튼 `text_button(accent, default_button)` | |
| `switch_dialog_element` | D2 뼈대 + 행 커스텀(Git 후보·SVN tree·상태 sub-row·현재 위치 표식), 질의 막대는 `button+text_input+button` 형제 유지 | luil `text_input`의 지우기 버튼은 `icon_close` 고정이라 search/close/arrow 3단 전환을 못 한다 |
| `settings_dialog_element` | D2 뼈대 + tab rail·항목 배치·**토글**·테마 세그먼트·accent 격자·덮어씀 배지 커스텀 유지. 텍스트 버튼·제한 시간 칸은 luil | luil `check_element(toggle_switch)`는 30×16 고정·줄 전체가 hit·꺼짐이 외곽선이라 gitman의 38×20 채움 토글과 다르다. `choice_group(toggle)`은 글자 버튼이라 아이콘 3단과 다르고, `wrap_element`는 정사각 격자 규칙이 없고, `badge_element`는 클릭을 받지 않는다 |
| `discovery_dialog_element` | D2 뼈대 + 체크박스 행 커스텀 | 행 안 체크 표시에 맞는 luil 요소 없음 |
| `font_dialog_element` | D2 뼈대 + 행·미리 보기 커스텀. 질의 막대는 형제 구조 유지 | 행별 typeface는 `draw_context::fonts`로 지금처럼 |
| `generate_document_dialog_element` | D2 뼈대 + luil `text_input`·`text_button`. 같은 폴더 토글은 지금처럼 accent 텍스트 버튼 | 읽기 전용 경로 칸은 작은 그리기 도우미 유지 |
| `local_changes_dialog_element` | D2 뼈대 + 행·diff pane 커스텀, 막대 2개는 luil | 배지에 아이콘이 들어가 `badge_element`와 다르다 |

공통 변경: hit 흡수용 빈 `left_click` 액션은 `set_hit_opaque(true)`로, 부분 영역
hit 제한을 위한 `hit_test` override는 `set_clip_children(true)`로, hover 채움은
`draw_hover_fill`로 바꾼다. `animating()`은 L8의 `next_update()`로.
`accessibility()` 재정의는 이번 범위 밖이다 (1장).

### L6. dialog와 메뉴 구조 (D2·D3 승인 시)

- 각 dialog는 `modal_host_element { owner, scrim_opacity 0.45, outside = 비움
  (흡수만), dismiss = 취소(Esc), focus_entry/focus_return(D1에 따라),
  content_width/height }` → `panel_element(surface_background, radius)` →
  `stack(column)` [내용]. dialog 캡션은 `dialog_caption_element`로 바꿀 수 있으나
  제목 줄의 모양이 바뀌므로 **1차 이식에서는 제목을 지금처럼 그린다** (후속 후보).
- scrim은 luil이 팔레트 `content_shadow`로 칠한다 (지금은 검정 0.45). 고대비에서
  차이가 생길 수 있다.
- 창이 낮을 때 dialog를 줄이는 `dialog_available_height` 계산은 `modal_host`가
  하지 않으므로 tree를 짓는 쪽이 `content_height`에 넣어 준다.
- Esc 우선순위: luil은 "그리기 순서상 마지막 가둠"이 Esc를 받는다.
  `build_ui_tree`의 추가 순서에서 `generate`와 `font`가 지금 Esc 사슬(generate
  먼저)과 반대이므로 두 `add` 순서를 바꿔 맞춘다. 나머지(notice > local_changes
  > discovery > … > switch)는 이미 일치한다.
- 컨텍스트 메뉴(D3-A popup): `ui_frame::popups`에 `ui_popup { id "card-menu"|
  "document-menu", x/y = anchor / scale, width 190, height = menu_element::
  height_for, tree = make_arranged_tree(menu_element), dismiss = close_context_menu_intent }`.
  `menu_config::select`는 key 하나만 받으므로 항목 key에 `entry` 이름을 쓰고,
  대상 경로·owner는 `context_menu_view`에서 logic이 이미 아는 값으로 되돌린다
  (logic이 메뉴를 닫고 항목 intent를 처리한다 — 지금 element가 두 액션을 내던
  것을 logic 한 곳으로 옮긴다). policy의 `menu()`는 `{menu, menu_item}`을 돌려주고
  `close_menu()`는 `close_context_menu_intent`다.
- D3-B(overlay 유지)면 backdrop element(투명, 클릭 흡수+닫기)를 남기고 panel만
  `menu_element`로 바꾼다. 키보드 탐색은 `find_menu`가 주 tree도 보므로 동일하게
  동작한다.

### L7. 입력 정책 (`gitman_policy`)

| 메서드 | 내용 |
| --- | --- |
| `text_target_of(kind)` | 4개 텍스트 칸 kind → `text_input_target` (L4) |
| `make_text_edit_action` | `text_edit_request` → gitman `edit_text_input_intent` |
| `make_text_composition_action` | `text_composition_event` → `set_text_composition_intent` (필드 동일) |
| `menu()` / `close_menu()` | L6 |
| `on_wheel` | D3-A 확정: 메뉴 검사 줄을 두지 않는다 — 메뉴는 popup tree에 있어 `on_wheel`이 받는 주 tree에 없고 닫힘은 `ui_popup::dismiss(wheel_scrolled)`가 맡는다 (5장). 그다음 dialog **존재** 기준 사슬(font > settings > switch > discovery: 포인터 위치와 무관하게 그 dialog 목록을 흘린다 — luil `route_wheel`은 좌표 기준이라 그대로 못 옮긴다)을 if-chain으로, local_changes(diff pane vs 목록)·log pane·카드 목록은 `route_wheel` 표로 |
| `on_key` | F5(refresh_all, `repeat` 무시), 카드 ↑/↓(D1-B면 지금 코드 그대로 policy 안에서 `ids_of_kind(card_body)` 순회), Enter(초점 카드 refresh; switch 질의 칸의 Enter는 `default_button`으로), Esc 최종 fallback(선택 해제). dialog Esc는 `modal_host.dismiss`가, 메뉴 Esc는 `close_menu`가 받는다 |
| `on_click` | 카드 body 클릭 → policy가 든 `focused_card_` 갱신(지금 `process_release`의 역할) |
| `order_focus` | D1에 따라 필터 |
| `on_focus_moved` | D1-C가 아니면 기본 |

`app_command` 표 (`app_ui_command::command`; `argument`는 경로):

| 값 | 인자 | 실행 |
| --- | --- | --- |
| `show_open_document_dialog` | – | 문서 열기 dialog → 절대 경로 → `open_document_intent` |
| `show_generate_scan_folder_picker` / `show_generate_location_picker` | – | 폴더 dialog → `set_generate_*_intent` |
| `show_discovery_folder_picker` | 시작 폴더(문서 폴더; tree를 지을 때 view에서 넣는다) | 폴더 dialog → `begin_discovery_intent` |
| `show_git_executable_picker` / `show_svn_executable_picker` | – | 파일 dialog → `set_settings_executable_intent` |
| `register_file_association` / `unregister_file_association` | – | 레지스트리 → `show_notice_intent` |
| `open_in_vscode` / `open_in_explorer_select` / `open_in_explorer_folder` | 절대 경로 | `ShellExecuteW` (지금 `execute_open_external` 본문) |

`copy_selected_log`는 명령이 아니라 로그 pane 복사 버튼의 액션이
`clipboard_copy_request { format_log_copy_text(log_) }`를 클릭 시점에 만든다
(element가 든 `log_`를 참조하므로 frame마다 문자열을 만들지 않는다). 창 명령
3개(최소화·최대화·닫기)는 luil `ui_command`로 캡션 요소가 낸다.

키 어휘가 넓어진다: luil은 `space`·`tab`·`page_up/down`·F1~F12·`platform_key_code`
(Ctrl/Alt와 함께 눌린 모든 키)를 보낸다. policy는 모르는 키를 무시한다.

### L8. 시간 갱신 (`next_update`)

| 대상 | 구현 |
| --- | --- |
| 카드 spinner (`card_.busy`) | `next_update`가 `context.now` 반환 (연속) |
| SVN 로딩 sub-row | 같다 |
| 로그 헤더 경과 시간 (MM:SS) | `log_.change_started_at` 기준 다음 초 경계 반환 (element가 이미 값을 든다) |
| caret 깜빡임·tooltip 지연 | luil `text_input_element`·`ui_tree`가 한다 |

### L9. test 재편

| 처분 | 파일 |
| --- | --- |
| 삭제 11 (luil test가 동일·상위 집합이거나 대상이 luil 비공개) | `caption_layout`, `font_fallback`, `win32_fonts`, `renderer_policy`, `ui_theme`, `ui_element`, `tsf_input`, `text_edit`, `messaging_channel`, `messaging_latest_slot`, `embedded_assets`(luil 검증 함수가 비공개라 luil_tests에 맡긴다; gitman 리소스 ID 105 존재만 `gitman_driver_tests`에서 `FindResourceW`로 확인) + `gitman_messaging_asan_tests` target(ADR-005 ASan 조항은 luil CI로 위임) |
| 수정 9 (include/namespace/생성자) | `utf8_tests`(utf8_text 사례 삭제, win32 utf8 사례 유지), `log_isolation_stress`, `application_options`(`luil::renderer_mode`), `app_settings`·`domain_model`·`json_project_store`·`project_schema`(전달 헤더 덕에 최소), `dependency_versions`(Skia 검사는 luil 핀을 관찰하는 값 152로 재고정), `task_scheduler`(생성자에 sink 함수) |
| 재작성 12 (luil API 위) | `ui_tree_build`, `ui_interaction`(gitman policy·intent 매핑만 남긴다), `context_menu`, `discovery_dialog`, `notice_dialog`, `settings_dialog`, `switch_dialog`, `start_page`, `log_view`, `update_ui`, `local_changes`(UI 9사례; 파서 5사례는 `local_changes_domain_tests`로 분리해 유지), `app_runtime`→`gitman_driver_tests`(headless `app_host { config{}, driver, policy }`) |
| 신설 | relay 순서·bind 시 배출·bind가 첫 post보다 먼저, `cancel()`이 registry의 token을 전부 세우는지, `shutting_down_` 중 `handle`이 close만 통과시키는지, kind 표 중복 없음, `app_command` 매핑, `next_update` 값(spinner·경과 시간), `gitman_caption`이 window_config와 tree 양쪽에 같은 값인지, 드롭·명령행 경로 절대화, 카드 drag payload의 `suppress_drop_highlight`·`custom_visual` 값 |
| 유지 43 | 나머지(`stabilization_tests` 포함) + helpers + fixtures |

재작성 test의 공통 변경: `ui_element_id { kind, u8string }`, `luil::interaction_controller
{ &policy, config }`(policy 첫 인자), `luil::ui_tree` 진단 `duplicate_ids()`·
`unarranged()`가 비어 있음을 매 tree마다 확인.

### L10. 문서 갱신

| 문서 | 변경 |
| --- | --- |
| `docs/build.md` | §1 GN/Ninja 문장 삭제, §2 submodule·준비 절차 교체(L1 코드 블록), §3.2 CRT 문장의 검사 도구 교체, §5 고지 목록에 WebView2, 아이콘 생성 경로, §7 코디콘 갱신 절 삭제(luil 몫) |
| `docs/skia-build.md` | 삭제, `build.md`에서 `third_party/luil/docs/skia-build.md`로 안내 |
| `README.md` | 개발 환경 준비·빌드 절 교체, 참고 문서 링크 |
| `docs/deployment.md`, `docs/user-manual.md`(+`.html`) | 정적 연결 목록·고지 목록에 WebView2 |
| ADR-001 / ADR-002 / ADR-004 / ADR-005 / ADR-006 / ADR-007 / `dependency-provisioning-design.md` | ADR-001의 vcpkg baseline·Skia 문장, ADR-004의 스레드 소유·종료 순서(luil 소유로 주석), ADR-005의 ASan 위임, ADR-002·006은 L1 |
| `docs/requirements.md` | REQ-009 "Skia 148"→"luil(내부 Skia)"; 검증 기준을 1장의 허용 헤더 범위로 |
| `docs/plan.md`, `docs/handoff.md` | 날짜 있는 추가 항목으로 기록 (역사 편집 않음) |
| `docs/change_log.md` | 승인 결정과 구현 요약 |
| `docs/ui-element-design.md` | 머리말에 "기반 계층은 luil로 이관(2026-09)" 한 줄 |

## 4. 검수 질문 (열린 결정)

### D1. Tab·초점 정책

**결정 (2026-09-05): B.**

luil은 `left_click` 액션이 있는 모든 element를 **기본으로 Tab 자리**로 삼고
(`tab_stop` 기본값), 클릭한 element가 `focused`가 되어 Enter/Space가 그 액션을
다시 실행한다. gitman의 `left_click` 등록은 16개 파일에 66곳이다. 그대로 두면
카드를 클릭한 뒤 Enter가 `refresh`가 아니라 `select_card`를 낸다.

- A. 전부 억제: 도우미로 모든 element에 `set_tab_stop(false)` — 지금과 동일한
  마우스 전용 UI.
- B. **선택 채택 (추천)**: 버튼·dialog 컨트롤·텍스트 칸은 Tab 자리로 두고
  카드 body·root·메뉴 backdrop·목록 행은 `set_tab_stop(false)`. 카드 ↑/↓·Enter·
  Esc는 지금 코드 그대로 policy에서 처리한다. 초점 테는 키보드로 옮길 때만
  그려진다.
- C. 전면 채택: 카드 목록을 `focus_group`으로, 모든 dialog에 `focus_entry/
  return`. 카드 목록 가상화를 포기해야 한다 (luil 묶음 키보드는 tree의 전 항목을
  본다).

### D2. dialog 뼈대를 `modal_host_element`로 재구성

**결정 (2026-09-05): A.**

- A. **채택 (추천)**: scrim·초점 가둠·Esc·바깥 클릭이 선언적으로 된다. Esc
  사슬 코드 삭제. generate/font 추가 순서 교정. 바깥 클릭이 dialog를 닫는 것은
  `outside`를 비워 지금처럼 흡수만 하게 둔다.
- B. 유지: 각 dialog가 지금처럼 scrim을 그리고 Esc 사슬은 policy `on_key`로
  옮긴다. luil의 초점 가둠·기본 버튼을 못 쓴다.

### D3. 컨텍스트 메뉴

**결정 (2026-09-05): A.**

- A. **popup 창 (추천)**: luil 표준. 메뉴가 창 밖으로 나갈 수 있고 활성 전환·창
  이동·크기 변경에 닫힌다. 다만 **메뉴 밖 클릭이 아래 요소에도 전달**되고(카드
  선택·다른 카드 우클릭 즉시 새 메뉴), **휠이 메뉴를 닫으면서 목록도 흘린다**.
  gitman 코드(앵커 다듬기·backdrop·그림자·height 계산) 삭제.
- B. tree 안 overlay 유지 + panel만 `menu_element`: 동작 변화 0, 키보드 탐색은
  luil이 준다. popup 배관은 쓰지 않는다.

### D4. luil 상류 소폭 변경 허용

**결정 (2026-09-05): B.** 일단 gitman 안에서 해결하고 luil 공개 API 추가는 후속
개선 항목으로 둔다 (L3 글꼴, 부록 B).

**갱신 (2026-09-06):** 사용자가 luil에 A의 공개 API(`luil::installed_font_families`·
`luil::set_ui_typeface_families`·`drag_payload::suppress_drop_highlight`)를 더했다
(`c971f32`). 핀을 그 커밋으로 올리고 gitman 우회는 두지 않는다 (L3 글꼴·drag).

gitman만 고치면 다음 셋이 남는다. luil도 같은 저자의 저장소이므로 작은 공개 API를
더하고 submodule을 올리는 안이 있다.

- A. **luil에 추가 (추천)**: `include/luil/win32/fonts.h`에
  `installed_font_families()`와 `set_ui_typeface_families()` 공개(구현은 이미
  있다), `drag_payload`에 drop 대상 강조를 끄는 플래그(또는 `plan_drag_overlay`가
  `custom_visual`을 강조에도 적용). 이 둘로 글꼴 목록·한글 기본 글꼴(Malgun Gothic
  우선)·카드 drag 모양이 지금과 같아진다. 선택: `LUIL_WARNINGS_AS_ERRORS` 옵션.
- B. gitman 안에서 해결: `SkFontMgr_New_DirectWrite` 열거 약 35줄을 gitman이
  갖고(DirectWrite 관리자가 프로세스에 둘), 기본 글꼴이 `Segoe UI`로 바뀌는 것과
  drag 중 목록 테두리를 수용한다.

### D5. IME 조합 중 점진 검색

**결정 (2026-09-05): A.**

luil의 `search_query`는 조합 중인 글자(예: "토ㅅ")로 먼저 거르고 아무것도 맞지
않으면 확정 글로 물러선다. gitman은 지금 확정 글로만 거른다 (조합 글은 그리기만).
- A. **채택 (추천)**: switch·글꼴 dialog 필터가 조합 중에도 좁혀진다. 규칙은
  두 줄이라 logic에 그대로 옮겨 적는다 (luil UI 헤더를 application에 들이지 않는다).
- B. 지금처럼 확정 글만.

### D6. nlohmann/json submodule

**결정 (2026-09-05): A.**

- A. **이번엔 둘 다 유지 (추천)**: 충돌 없음이 확인됐고 UI 이식과 무관하다. 후속
  정리 항목으로 남긴다.
- B. 지금 gitman 사본을 지우고 luil의 `single_include`를 쓴다 (submodule 하나
  감소, luil 핀에 JSON 버전이 묶인다. `cmake/dependencies/nlohmann_json.cmake`
  삭제와 링크 줄 변경이 따른다).

### D7. 자산(코디콘·accents) 소유

**결정 (2026-09-05): A.**

- A. **gitman 사본 삭제 (추천)**: 동일하므로 luil 것을 쓴다. 이후 아이콘 추가는
  luil 변경 + submodule 갱신.
- B. `assets/accents.json`만 남기고 `LUIL_ACCENT_CATALOG`로 덮어쓴다 (cache
  형태로 미리 정의). 브랜드 색을 gitman이 독립적으로 바꿀 수 있다.

### D8. 진행 방식

**결정 (2026-09-05): B.** 단, 사용자 지시로 계획서(`luil-migration-plan.md`)를
먼저 쓰고 승인받은 뒤 시작한다.

- A. 체크포인트마다 검증 문서 + 제안 커밋 메시지로 검수 요청, 커밋은 사용자.
- B. **별도 브랜치 위임 (추천)**: `feature/luil-migration`에서 체크포인트마다 직접
  커밋, 끝에 전역 검증(전체 CTest Debug·Release + `vs2026-analysis` +
  `check_source_style` + 앱 빌드 Debug·Release) 후 한 번 보고. C2 하나가 삭제
  약 8,800줄·이식 약 5,650줄·test 약 6,350줄이라 중간 검수 단위가 매우 커진다.

## 5. 수용해야 하는 동작 변화

| 변화 | 원인 | 성격 |
| --- | --- | --- |
| Tab으로 컨트롤을 순회하고 키보드로 옮긴 초점에 2px accent 테가 그려진다 | luil 초점 모델 | 새 기능 (D1-B: 버튼·dialog 컨트롤·텍스트 칸만) |
| 빈 배경을 클릭하면 텍스트 칸 초점(caret)이 풀린다; 텍스트 칸 안의 버튼(지우기)을 눌러도 초점이 유지된다 | luil `process_press` | 지금과 반대 두 가지, 개선 |
| 초점 컨트롤에서 Space·Enter가 클릭을 실행한다; dialog의 기본 버튼이 Enter를 받는다 | `process_activation_key`·`default_button` | 새 기능 |
| 메뉴 밖 클릭 전달, 휠 시 메뉴 닫힘+스크롤, 메뉴가 창 밖으로 나감 | popup 표면 | 수용 (D3-A) |
| (없어짐) 카드 drag 중 카드 목록 강조 | `drag_payload::suppress_drop_highlight = true` | luil `c971f32`로 지금과 같다 |
| scrim 색이 팔레트 `content_shadow` | `modal_host` | 고대비에서만 차이 |
| 로그오프·재부팅 때 종료 저장이 실행된다 | `WM_ENDSESSION` | 개선 |
| 첫 실행 창 크기가 모니터 DPI로 배율된다 (1280×720 논리) | luil 창 생성 | HiDPI에서 더 큰 창 |
| 시작 실패 메시지 상자 제목이 `luil startup error`, 원문 오류 노출 | luil `show_startup_error` | 브랜딩 손실, 훅 없음 |
| 파일 drag-over 시 수락 element 강조 | `IDropTarget` | delegate fallback 경로(문서 열기)는 강조 없음 |
| IME 후보 창 위치를 luil 그림자 문서가 정한다 | TSF 이관 | 통합 검증 항목 |
| 스크린 리더가 캡션 버튼·툴팁 있는 버튼·텍스트 칸을 읽는다. 카드·로그·dialog 행 등 커스텀 내용은 `accessibility()` 재정의 전까지 읽히지 않는다 | UIA provider | 부분적 새 기능 (부록 B) |
| 스크롤 막대의 UIA `SetValue`는 읽기 전용 | `scroll_to` 비움 | 후속 (부록 B) |
| 시작 메시지 순서: metrics → font list → open document | `post_window_metrics` 위치 | 무해 |
| 텍스트 칸 초점 유지 규칙 등 세부 | luil 상태 기계 | 통합 검증에서 확인 |

## 6. 구현 순서

각 체크포인트는 빌드되고 해당 test가 통과한다. D8-B면 체크포인트마다 커밋한다.

| 체크포인트 | 내용 | 검증 |
| --- | --- | --- |
| C0 (생략) | 이 작업은 luil을 바꾸지 않는다. 글꼴 공개 API·drag 강조 플래그는 사용자가 luil `c971f32`에 반영했고 핀을 그 커밋으로 잡는다. 나머지 요청은 부록 B | – |
| C1 빌드 준비 | L2의 [C1] 항목만: luil submodule·패키지 취득 절차·`add_subdirectory(SYSTEM)`·세 변수·luil 고지/`.rc` 조각 생성·`.rc` 분리·`.gitattributes`·`normalize_line_endings`. **gitman target은 `luil::luil`을 링크하지 않고** 자기 Skia로 빌드된다 (luil은 자기 Skia로 빌드되어 공존 — `luil_skia_*` imported target·per-target `MAP_IMPORTED_CONFIG_*`라 전역 변수 충돌이 없다) | configure(luil 포함) + 앱 빌드 Debug/Release + smoke 4종 |
| C2 기계적 이식 | L2의 [C2] 항목 전부; 2.1 표의 프레임워크 파일 삭제·전달 헤더·kind 표·`text_input_target`·codicons 치환; driver/relay/registry/policy/delegate/caption/main 신설; `logic_controller` 취소 registry 연결; element 28개 파일을 luil `ui_element` 위로 이식(모양 보존, dialog는 지금처럼 자체 scrim); `next_update`; Skia submodule·스크립트·`skia.cmake`·자산·생성기 삭제; test 삭제·수정·재작성(L9) | 영향 test 전부 + smoke 4종 + 앱 실행 수동 확인 |
| C3 luil 표준 요소 채택 | D1 초점 정책 적용, D2 `modal_host`, D3 메뉴, `draw_hover_fill`·`hit_opaque` 정리, D5 점진 검색 | dialog·메뉴·설정 test + 수동 시나리오 |
| C4 문서·정리 | L10 문서, ADR-007 승인 반영, change_log, handoff | 전역 검증: 전체 CTest Debug·Release, `vs2026-analysis`, `check_source_style.ps1`, 앱 빌드 Debug·Release, 새 clone에서 준비 절차 재현 |

C2가 가장 크다. 중간에 빌드가 깨지는 상태로 커밋하지 않기 위해 C2는 한 커밋
또는 "기반 교체 → element 이식 → test" 세 커밋으로 나누되 세 번째까지 마쳐야
test가 돈다는 것을 검증 기록에 적는다.

## 7. 검증 계획

- 체크포인트 중간: 그 단계에서 추가·수정한 test만 실행 (2026-08-20 지시).
- 최종(C4): 전체 CTest Debug·Release, `cmake --preset vs2026-analysis` 빌드,
  `scripts\check_source_style.ps1 -root .`, `cmake --build --preset vs2026-debug`·
  `vs2026-release`, `cmake --install`로 `bin/gitman.exe` 생성, 빌드 로그에 네트워크
  취득 0건.
- 수동 시나리오(검증 문서에 기록): 문서 열기(dialog·드롭·명령행 상대 경로)·카드
  새로 고침·카드 drag 순서 변경·로그 pane 복사·switch(Git/SVN)·환경설정 4탭·
  글꼴 dialog 미리 보기·IME 한글 조합(각 텍스트 칸)·클립보드 붙여넣기·컨텍스트
  메뉴 키보드·창 배치 저장/복원·DPI 변경·고대비·Alt+F4·로그오프 저장·
  `--renderer=cpu`.
- 새 clone 재현: `git clone` → L1 준비 절차 → configure/build. 오프라인
  경로(zip을 archives에 두기)도 한 번 재현한다.

## 8. 위험과 완화

| 위험 | 완화 |
| --- | --- |
| worker 결과 유실 (`post_app_message`는 가득 차면 버림) | relay가 큐의 정본이고 `handle`·`tick`이 비운다; 깨우기 유실은 inbox 포화를 뜻해 곧 `handle`이 온다 (L3). 종료 시 `app_inbox_statistics().rejected`를 로그 |
| 종료 hang (`stop_workers` join 무한 대기) | `cancel()`에서 `cancellation_registry`로 취소 전파 (L3) |
| `__fastfail` (inbox 닫은 뒤 1초 안에 logic이 못 나옴) | `shutting_down_`이면 `handle`이 close만 처리하고 `make_frame`이 tree를 다시 짓지 않음 |
| Skia 152 API 차이 | gitman이 Skia를 직접 쓰는 곳은 element draw 본문(SkCanvas·SkPaint·SkRRect·SkFont)뿐이며 152에서 변하지 않은 API다. C2 빌드에서 드러난다 |
| skia-prep 릴리스 가용성 (개인 저장소 릴리스 자산) | 무결성은 SHA-256, 가용성은 없음. `LUIL_SKIA_ROOT`로 직접 빌드 트리를 가리키는 경로가 열려 있음을 ADR-007에 기록 |
| luil `/WX` 아래 새 toolset 경고 | submodule 핀 유지. `LUIL_WARNINGS_AS_ERRORS` 옵션은 후속 |
| test 재작성 규모(약 6,350줄) | 재작성 대상은 gitman 의미(intent 매핑) 검증만 남기고 일반 상호작용 역학은 luil test에 맡긴다 |
| 두 `.rc`의 리소스 ID 충돌 | 규칙 문서화 (101~104 luil, 105 아이콘, 106+ gitman) |
| `add_subdirectory` 변수 scope 실수 | L2의 순서를 CMake 주석으로 고정 |
| C1에서 두 Skia 혼선 | C1은 어떤 gitman target도 `luil::luil`을 링크하지 않는다 (L2·6장) |

## 부록 A. gitman 파일 처분 표

삭제 (luil 사용): `src/messaging/{channel,envelope,latest_slot}.h`,
`src/domain/{text_edit,utf8_text,appearance}.cpp`(헤더는 전달 헤더로 축소),
`src/presentation/{ui_theme,renderer_policy,skia_smoke_view}.*`,
`src/presentation/ui/{ui_element,ui_tree,ui_events,ui_interaction,draw_primitives,button_element,label_element,scrollbar_element,caption_element,caption_metrics,dialog_elements}.*`,
`src/platform/win32/{win32_application,win32_app_runtime,caption_layout,skia_renderer,cpu_skia_renderer,direct3d_skia_renderer,embedded_assets,win32_fonts,win32_tsf_input,win32_clipboard}.*`,
`src/platform/win32/resources/resource_ids.h`의 IDR 4개,
`cmake/dependencies/skia.cmake`, `cmake/{generate_notices,generate_codicons,generate_accents,verify_codicons}.cmake`,
`scripts/{build_skia,verify_skia_root,fetch_codicons}.ps1`,
`third_party/{skia,skia-externals,skia-args,patches}`, `assets/codicons/`, `assets/accents.json`,
`docs/skia-build.md`.

유지 (UI 무관): `src/domain/*`(위 셋 제외), `src/infrastructure/*`,
`src/application/*`(아래 셋 제외),
`src/platform/win32/{utf8,project_file_system,win32_app_settings_path,win32_directory_enumerator,win32_file_association,win32_log_file_system,win32_process_runner,win32_text_transcoder,win32_vcs_file_probe,workspace_document_file_system}.*`,
`src/presentation/{diff_presentation,list_metrics,log_presentation,status_presentation,view_snapshot}.*`,
`cmake/{macro,dependencies,run_style_check}.cmake`, `cmake/compiler/msvc.cmake`,
`cmake/platform/windows.cmake`, `cmake/dependencies/{nlohmann_json,catch2}.cmake`,
`assets/gitman.ico`, `CMakePresets.json`.

재작성/수정: `src/application/{logic_controller,task_scheduler,app_messages}.*`
(취소 registry·sink 함수·`startup_intent`·`relay_drain_intent`·`text_input_target` 값),
L3 신설 파일, L5 표의 element 28개 파일, L9 표의 test.

## 부록 B. luil 쪽 후속 후보 (이번 범위 밖)

반영됨 (luil `c971f32`, 2026-09-06):

- luil 공개 글꼴 API(`installed_font_families`·`set_ui_typeface_families`) —
  `luil/text/fonts.h`.
- `plan_drag_overlay` 강조 opt-out — `drag_payload::suppress_drop_highlight`.
- `LUIL_DEFAULT_ACCENT_ID` (gitman은 기본값 `mint`를 그대로 쓴다).

반영됨 (luil `d87096b`, 2026-11-02 확인): 아래 요청 중 `LUIL_ENABLE_WEBVIEW`,
fetch `-Offline`, 시작 오류 상자(`window_config::startup_error`),
`LUIL_WARNINGS_AS_ERRORS`, 고지 머리글의 `;`(`string(CONCAT …)`). gitman은 앞의 셋을
채택했다 (ADR-007 갱신). 지우기 버튼 글리프는 아직 형제 구조로 둔다.

luil에 추가로 요청하는 것 (2026-09-06, 이번 이식은 없어도 진행한다):

- `LUIL_ENABLE_WEBVIEW=OFF` 옵션 (WebView2 SDK 준비·정적 로더 링크·고지 항목을
  뺀다). gitman은 WebView2를 쓰지 않는데 준비 절차와 실행 파일 크기를 진다.
- fetch 스크립트 `-Offline` 스위치 (아카이브가 없으면 네트워크를 시도하지 않고
  바로 안내). ADR-006의 "빌드 체계는 네트워크를 쓰지 않는다"를 준비 단계까지
  넓힌다.
- 시작 실패 메시지 상자(`luil startup error`)의 제목·본문 훅 (`window_config`에
  제목 문자열 또는 delegate 콜백). gitman의 한국어 브랜딩 메시지가 사라진다.
- `LUIL_WARNINGS_AS_ERRORS` 옵션 (`/WX` 끄기). 새 toolset에서 luil 경고가
  gitman 빌드를 막는 것을 소비자가 풀 수 있게 한다.
- (낮음) `text_input_config`의 지우기 버튼 글리프 지정 (switch 질의 막대의
  search/close/arrow 3단 전환). 지금은 형제 구조로 우회하므로 급하지 않다.
- (낮음) `cmake/generate_notices.cmake`의 `set(notice_text "a" "b" …)`가 CMake list를
  만들어 고지 머리글에 `;`가 섞인다. `string(CONCAT …)`로 바꾸면 없어진다 (C1
  검증에서 발견).

gitman 쪽 후속: 절대 위치 스크롤 intent와 `scroll_to` 연결, 커스텀 element의
`accessibility()` 재정의, `dialog_caption_element` 채택, nlohmann submodule 통합.
