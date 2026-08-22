# 상단 배너 컨텍스트 메뉴 · Unix 경로 표시 · 테마와 키 컬러 설계

상태: **승인** (2026-08-22 작성·검수. 검수 결정은 각 장과 아래 "검수 결정"에 반영)

## 검수 결정 (2026-08-22)

| # | 질문 | 결정 |
|---|---|---|
| 1 | 키 컬러 목록의 출처 | **JSON 파일을 빌드 시점에 내장한다.** `assets/accents.json`을 CMake가 읽어 C++ 표(`gitman/generated/accents.h`)로 만든다. 런타임 JSON 파싱도, 실행 파일 옆 파일 읽기도 없다 (T4.3) |
| 2 | `accent_soft`·`accent_emphasis_fg` 역할 | T4.2 제안대로 (soft = 낮은 알파 옅은 바탕, emphasis_fg = 바탕 위 강조 글자) |
| 3 | 배너 `VSCode로 열기` 대상 | **문서가 있는 폴더** (카드 메뉴와 같은 규칙) |
| 4 | 테마 토글 아이콘 | `lightbulb` / `device-desktop` / `color-mode` |


2026-08-22 사용자 요구 3건(세부 4건)의 설계를 기록한다.

| 항목 | 요구 |
|---|---|
| T1 | 상단 배너(도구 막대)에 컨텍스트 메뉴 추가: `경로를 탐색기로 열기`, `VSCode로 열기` |
| T2 | 배너의 문서 주소와 저장소 카드의 경로를 unix 스타일(`/`)로 표시 |
| T3 | 환경설정에서 라이트/시스템/다크 테마를 codicon 아이콘 토글로 지정 |
| T4 | 키 컬러(현재 민트 하나)를 여러 색으로 확장. 제시된 JSON 스키마를 읽는다 |

---

## T1. 배너 컨텍스트 메뉴

### T1.1 현황

컨텍스트 메뉴는 카드 전용이다 (field-feedback-design 3장). 상태는 logic이
`context_menu_state { card, anchor_x, anchor_y }`로 들고 있고, snapshot의
`context_menu_view`가 `owner`(project_id)와 `repository_path` 하나를 담는다.
항목 종류(`context_menu_entry`)마다 element가 아이콘과 클릭 액션을 정한다.
`toolbar_element`는 우클릭 액션이 없어 배너 우클릭은 아무 일도 하지 않는다.

### T1.2 결정: 메뉴를 "대상"으로 일반화한다

메뉴 하나가 카드와 문서 두 대상을 모두 담도록 최소한으로 넓힌다. 새 overlay를
만들지 않아 바깥 클릭·Esc·키보드 이동(↑/↓/Enter)이 그대로 동작한다.

- `context_menu_view.owner`는 카드 메뉴에서만 채운다. 문서 메뉴는 비운다.
- 메뉴 수준의 `repository_path`를 **항목 수준 `target_path`**로 옮긴다
  (`context_menu_item_view`의 필드). 항목마다 대상이 다르기 때문이다 — 문서
  메뉴의 탐색기 항목은 문서 **파일**(`/select,`), VSCode 항목은 문서가 있는
  **폴더**를 받는다. 카드 메뉴의 항목은 모두 작업 복사본 경로다. 값은 Windows
  원형 그대로다(셸에 넘길 값이라 T2의 표시 변환을 적용하지 않는다).
- `context_menu_entry`에 `open_document_folder`(탐색기)와
  `open_document_in_vscode`를 추가한다. 두 항목은 `owner` 없이 `target_path`만
  쓴다.
- logic의 `context_menu_state`에 `kind { card, document }`를 더한다. 새 intent
  `open_document_context_menu_intent { anchor_x, anchor_y }`가 문서 메뉴를 연다
  (카드용 `open_context_menu_intent`는 그대로).
- `toolbar_element`에 `ui_trigger::right_click` 액션을 붙인다. 자식(버튼)이
  먼저 hit test에 걸리므로 **배너의 빈 곳과 문서 경로 글자** 우클릭만 메뉴를
  연다. 문서가 없으면(`문서 없음`) intent를 내지 않는다 — logic도 문서가 없으면
  메뉴를 열지 않는다.

메뉴 항목(문서 대상):

| 항목 | 아이콘 | 동작 |
|---|---|---|
| 경로를 탐색기로 열기 | `folder-opened` | `explorer /select,<문서 파일>` — 폴더를 열고 문서를 선택 상태로 둔다 |
| VSCode로 열기 | `vscode` | 문서가 있는 **폴더**를 workspace로 연다 |

두 동작 모두 기존 `open_external_request`(UI thread 셸 실행) 경로를 그대로
쓴다. `external_open_target::explorer`가 이미 `/select,` 규칙이라 새 target은
필요 없다. VSCode는 **문서가 있는 폴더**를 연다 (검수 결정 3) — 카드 메뉴가
저장소 폴더를 workspace로 여는 것과 같은 규칙이다. 대상 폴더는 logic이
`windows_parent_directory(document_path)`로 계산해 `target_path`에 담는다.

### T1.3 영향 범위

`view_snapshot.h`(entry·필드 이름), `context_menu_element.cpp`(아이콘·액션),
`app_messages.h`(새 intent), `logic_controller`(상태·핸들러·snapshot 조립),
`toolbar_element`(우클릭 액션). 기존 카드 메뉴의 동작은 바뀌지 않는다.

---

## T2. Unix 스타일 경로 표시

### T2.1 현황

- 배너: `view_snapshot.document_path`(Windows 원형)를 label이 그대로 그린다.
- 카드: logic의 `display_path()`가 절대 경로 원형 또는
  `relative_windows_path()` 결과(`..\a\b`)를 `card_view_model.path`에 넣는다.

### T2.2 결정: 표시 문자열만 `/`로 바꾼다

`path_syntax`에 순수 함수를 추가한다.

```cpp
// 표시용 경로다. 구분자를 `/`로 통일한다. 저장·실행에는 쓰지 않는다.
[[nodiscard]] std::u8string to_display_path(std::u8string_view path);
```

- **카드**: `display_path()`의 반환에 적용한다(절대·상대 모두). 결과는
  `C:/work/alpha`, `../alpha` 형태다.
- **배너**: `view_snapshot.document_path`는 **Windows 원형을 유지**하고, 표시
  문자열은 새 필드 `document_display_path`에 logic이 함께 담는다. 원형이 남아야
  하는 이유는 UI thread가 이 필드를 탐색 폴더 선택의 시작 폴더
  (`windows_parent_directory`)로도 쓰기 때문이다 — Win32 파일 dialog의 초기
  폴더는 `/`를 받지 못한다. T1의 메뉴 대상 경로도 원형이 필요하다. 표시 문자열을
  logic이 만드는 것은 카드의 `path`와 같은 규칙이라 element는 그대로 그리기만
  한다(구현 중 조정: 처음에는 `build_ui_tree`에서 변환하려 했으나 표시 문자열의
  주인을 한 곳으로 모으고 test에서 확인할 수 있게 했다).
- 문서 저장(JSON), 최근 목록, 로그, 프로세스 인자, 필터 대상은 그대로 원형이다.
  표시 계층만 바뀐다.

### T2.3 영향 범위

`path_syntax`(+함수·+test), `view_snapshot`(+`document_display_path`),
`logic_controller`(`display_path`·snapshot 조립), `build_ui_tree`(배너 label).
시작 페이지 최근 항목의 폴더 표시도 같은 규칙으로 맞춘다(일관성).

---

## T3. 테마 선택 (라이트 / 시스템 / 다크)

### T3.1 현황

`color_theme { dark, high_contrast }`의 두 팔레트가 `constexpr`로 박혀 있고,
UI thread가 프레임마다 고대비 여부만 조사해 고른다
(`win32_application::render_one_frame`). 라이트 팔레트는 없다.

### T3.2 결정: 설정에 선호를 저장하고 UI thread가 해석한다

- 도메인에 `enum class theme_preference { system, light, dark }`를 둔다.
- `app_settings`에 **`appearance`**를 더한다. 문서가 덮어쓰지 않는다 — 외양은
  사용자·기기 단위이지 문서 단위가 아니다(따라서 `workspace_settings`가 아니라
  `app_settings` 직속이다).

```json
{
    "schema_version": 1,
    "appearance": { "theme": "system", "accent": "mint" }
}
```

  알 수 없는 값은 경고 진단 하나를 남기고 기본값(`system`, `mint`)을 쓴다 — 앱
  설정의 기존 규칙(어떤 오류도 시작을 막지 않는다)과 같다.
- `view_snapshot`에 `appearance_view { theme_preference theme; std::u8string accent_id; }`를
  싣는다. logic은 팔레트를 만들지 않는다(색은 표시 계층의 것).
- UI thread가 해석한다:
  `고대비 → high_contrast` > `light/dark → 그대로` > `system → 레지스트리`
  (`HKCU\...\Themes\Personalize\AppsUseLightTheme`). 레지스트리 값은 캐시하고
  `WM_SETTINGCHANGE`/`WM_THEMECHANGED`에서만 다시 읽는다(이미 두 메시지가
  `InvalidateRect`를 부른다).
- `color_theme`에 `light`를 추가하고 라이트 팔레트를 정의한다. 값은 VSCode
  Light Modern에 맞춘다:

| 역할 | dark | light |
|---|---|---|
| `window_background` | `#1e1e1e` | `#f8f8f8` |
| `surface_background` | `#2d2d30` | `#ffffff` |
| `primary_foreground` | `#ffffff` | `#1f1f1f` |
| `caption.background` | `#252526` | `#f0f0f0` |
| `notice_background` | `#422427` | `#fde7e9` |
| `tooltip_background` / `border` | `#252526` / `#5a5a5c` | `#ffffff` / `#c8c8c8` |
| `button_hover/pressed_background` | 흰색 10%/18% | 검정 8%/14% |
| `content_shadow` | 검정 | 검정 |
| `warning_accent` / `error_accent` | `#dcaa2d` / `#e06c75` | `#9a6700` / `#c0303a` |

  낮은 알파로 `primary_foreground`를 겹쳐 쓰는 기존 코드(`with_alpha(..., 0.08f)`
  류)는 전경색이 뒤집히므로 라이트에서도 그대로 성립한다.
- `.version-list` 생성 dialog(`version_list_generation_dialog`)는 지금
  `color_palette_for(color_theme::dark)`와 `DWMWA_USE_IMMERSIVE_DARK_MODE=1`을
  박아 쓴다. 해석된 테마를 인자로 받아 팔레트와 dark mode 속성을 함께 맞춘다.

### T3.3 환경설정 UI

환경설정 dialog 맨 아래에 **외양 2행**을 더한다(행 6: 테마, 행 7: 키 컬러).
panel 높이는 440 → 544다. 두 행은 문서 모드에서도 **항상 앱 설정을 편집**하며
`덮어씀` 배지가 없다(행 제목 뒤에 `(전역)` 표기). 문서를 연 채로 여는 경우가
대부분이라 문서 모드에서 숨기면 사실상 접근할 수 없다.

테마 행은 세그먼트 토글 하나다 — 아이콘 3개가 붙어 있고 현재 값만 강조된다.

```
테마 (전역)                            [ 라이트 | 시스템 | 다크 ]
시스템 설정을 따릅니다 (현재: 다크)
```

| 값 | codicon | 근거 |
|---|---|---|
| 라이트 | `lightbulb` | 밝음 |
| 시스템 | `device-desktop` | OS 설정을 따름 |
| 다크 | `color-mode` | 반쯤 채운 원 |

(codicon에는 sun/moon이 없어 의미가 가장 가까운 3종을 골랐다 — 검수 결정 4.)

키 컬러 행은 색 동그라미(swatch) 목록이다. 현재 색만 테두리 링으로 표시하고
tooltip이 `label`을 보여 준다.

**즉시 적용**: 외양 2행은 클릭 즉시 반영·저장한다(도구 막대의 경로 표시
토글과 같은 규칙). `저장`/`취소` 초안에 넣지 않는다 — 색은 눌러 보고 고르는
항목이라 미리 보기가 곧 값이어야 하고, `취소`로 되돌릴 대상도 아니다.
전용 intent `set_theme_preference_intent { theme }`,
`set_accent_intent { id }`가 앱 설정을 갱신하고 저장을 요청한다.

### T3.4 영향 범위

`domain/app_settings`(+`appearance_settings`), `json_app_settings_store`
(+읽기·쓰기), `view_snapshot`, `logic_controller`(+intent 2개, snapshot 조립),
`ui_theme`(+라이트 팔레트), `skia_smoke_view`/`win32_application`(해석·전달),
`settings_dialog_element`(+2행·+세그먼트·+스와치), `version_list_generation_dialog`.

---

## T4. 키 컬러 팔레트

### T4.1 현황

키 컬러는 `positive_accent` 한 필드이며 다크 팔레트에 민트 `#4ec9b0`으로 박혀
있다. 카드 상태, 테두리, 선택 표시, 설정 행 제목, 토글 트랙, 강조 버튼 등
35곳이 이 값을 쓴다.

### T4.2 결정: 4역할 accent set + id로 고르는 목록

요구가 제시한 JSON을 그대로 읽는다.

```json
{
    "id": "blue",
    "label": "블루",
    "swatch": "#a2cfff",
    "dark":  { "accent": "#3f7ebb", "accentHover": "#5392d1", "accentSoft": "#a2cfff", "accentEmphasisFg": "#c6e1ff" },
    "light": { "accent": "#0062ab", "accentHover": "#005391", "accentSoft": "#00487f", "accentEmphasisFg": "#00487f" }
}
```

- `swatch`: 설정의 색 동그라미. 테마와 무관한 대표색이다.
- 팔레트 필드: `positive_accent` → **`accent`**로 이름을 바꾸고(기계적 치환
  35곳) `accent_hover`, `accent_soft`, `accent_emphasis_fg`를 더한다.

역할 배정(제안):

| 역할 | 쓰이는 곳 |
|---|---|
| `accent` | 카드 상태 아이콘·선택 테두리·드롭 표시·강조 버튼 채움 |
| `accent_hover` | 위 요소의 hover (지금은 흰색 overlay로 대신하고 있다) |
| `accent_soft` | 낮은 알파로 겹치는 옅은 강조 바탕 — `덮어씀` 배지, 토글 트랙, 선택 행 |
| `accent_emphasis_fg` | 바탕 위 강조 글자 — 설정 행 제목, 강조 버튼 라벨 |

고대비 팔레트는 지금처럼 accent 4역할을 모두 흰색으로 둔다(키 컬러 무시).

### T4.3 색 목록의 출처: 빌드 시점 내장 (검수 결정 1)

색 정의의 원본은 `assets/accents.json`(위 스키마의 배열)이고, CMake가 빌드
시점에 읽어 C++ 표로 만든다. Codicons 매핑과 같은 구조다.

```
assets/accents.json
  → cmake/generate_accents.cmake  (string(JSON ...)으로 읽어 #rrggbb → 0xAARRGGBB)
  → build/.../generated/include/gitman/generated/accents.h
```

생성 header는 의존성이 없는 값 표 하나다(`<cstdint>`만 포함).

```cpp
namespace gitman::generated {
    struct accent_entry
    {
        const char8_t* id;
        const char8_t* label;
        std::uint32_t swatch;
        // accent, accent_hover, accent_soft, accent_emphasis_fg 순이다.
        std::uint32_t dark[4];
        std::uint32_t light[4];
    };
    inline constexpr accent_entry accents[] { /* ... */ };
}
```

- 런타임 JSON 파싱이 없다 — 계층 문제(logic·element·UI thread가 모두 목록을
  본다)와 시작 시 실패 경로가 함께 사라진다. `ui_theme`이 이 표를 감싸
  `accent_definition` 조회를 제공한다.
- **검증은 생성 시점에** 한다. 형식이 틀린 색(`#rrggbb` 아님), 빈 `id`,
  중복 `id`, 4역할 누락, `mint` 없음은 `message(FATAL_ERROR)`로 빌드를 세운다.
  잘못된 색이 조용히 화면에 나가는 것보다 낫다.
- 앱 설정의 `appearance.accent`가 표에 없는 `id`면 기본값(`mint`)으로 물러서고
  경고 진단 하나를 남긴다. 저장된 값은 지우지 않는다(다음 빌드에서 그 색이
  돌아오면 복구된다).

`assets/accents.json` 초안(수치는 구현 시 대비를 실측해 확정한다):

| id | label | swatch | dark accent | light accent |
|---|---|---|---|---|
| `mint` | 민트 | `#4ec9b0` | `#4ec9b0` | `#0f8a72` |
| `blue` | 블루 | `#a2cfff` | `#3f7ebb` | `#0062ab` |
| `purple` | 퍼플 | `#c8a2ff` | `#8a63d2` | `#6c3fbb` |
| `amber` | 앰버 | `#f0c674` | `#c99b3f` | `#8a6100` |
| `rose` | 로즈 | `#ff9fb0` | `#c0576e` | `#a63d55` |

### T4.4 영향 범위

`assets/accents.json`(신규), `cmake/generate_accents.cmake`(신규),
`src/CMakeLists.txt`(생성 규칙), `ui_theme`(구조·조회·팔레트 합성), accent를
쓰는 element 35곳(이름 변경), `app_settings`·저장소(선택 id),
`settings_dialog_element`(스와치 행).

---

## 검증 계획

- 순수 함수 test: `to_display_path` 4종(절대·상대·혼합 구분자·빈 값),
  accent 표 검사 3종(id 중복 없음·`mint` 존재·모든 역할이 불투명),
  `find_accent`의 fallback 2종, `appearance` 읽기·쓰기 왕복 3종,
  테마 해석 표(고대비·명시·시스템) 3종.
- logic test: 배너 메뉴 열기(문서 있음/없음), 항목 실행이 내는 액션 2종,
  테마·키 컬러 intent가 앱 설정을 갱신하고 저장을 요청하는지 2종.
- 표시 test: 카드 경로가 `/`로 나오는지, 배너 label이 `/`로 나오는데
  snapshot의 `document_path`는 원형인지.
- 눈으로 확인: 라이트·다크·고대비 각각에서 카드 목록·환경설정·로그 pane·
  switch dialog·생성 dialog.

## 체크포인트 구획 (구현 순서)

| # | 범위 | 커밋 단위 |
|---|---|---|
| C1 | T2 unix 경로 표시 (+test) | 작음 |
| C2 | T1 배너 컨텍스트 메뉴 (+test) | 작음 |
| C3 | T4 accent 4역할 · 색 목록 · 팔레트 합성 (화면 반영 없음, +test) | 중간 |
| C4 | T3 라이트 팔레트 · 테마 해석 · 앱 설정 저장 (+test) | 중간 |
| C5 | 환경설정 외양 2행(테마 세그먼트 · 색 스와치) + 생성 dialog 테마 적용 | 중간 |

각 체크포인트마다 검증 문서와 제안 커밋 메시지를 첨부해 검수를 요청한다.

## 열린 결정

없다. 4건 모두 2026-08-22 검수에서 결정되어 위 표와 각 장에 반영했다.
