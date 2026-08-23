# 검증 기록 - 글꼴의 문서 override (후속 지시)

- 일자: 2026-08-23
- 대상: `feature/switch-dialog-query-bar` 브랜치 (이어서 진행)
- 기준 문서: `docs/fonts-and-in-app-dialogs-design.md` F1 (전제를 뒤집는다),
  `docs/settings-tabs-and-appearance-scope-design.md` S2 (따라가는 패턴)

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `ctest` Debug / Release | **829/829 통과** (양쪽, 신규 6 case) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| 앱 빌드 Debug / Release | 성공 |
| `check_source_style.ps1` | 466 files 통과 |

## 2. 확인한 것 - 버그가 아니라 설계였다

글꼴에는 문서 override 계층이 **처음부터 없었다.** 확인한 자리는 셋이다.

- `logic_controller::publish_snapshot`: `snapshot->fonts = app_settings_.fonts;` —
  문서를 아예 보지 않는다.
- `workspace_document`에 글꼴 필드가 없다. 문서 JSON에도 `fonts` 키가 없다.
- 환경설정 dialog의 글꼴 항목이 `override_field`를 갖지 않아 문서 모드에서 `덮어씀`
  배지가 붙지 않는다.

`app_settings.h`의 주석이 그 판단을 적어 두고 있었다.

> 테마와 달리 문서 override가 없어 `appearance_settings`가 아니라 앱 설정 바로 아래에 둔다.

2026-08-23 첫 지시가 "**앱 단위 설정**에서 UI 폰트와 코드 폰트를 고를 수 있게 한다"
였고 그대로 만든 것이다. 테마·키 컬러도 원래 앱 전용이었다가 나중에 문서 override를
얻었는데(S2.3) 글꼴은 그 흐름을 타지 않았다.

이번 지시로 글꼴을 테마·키 컬러와 같은 계층으로 올렸다.

## 3. 고친 내용

### 3.1 domain

`appearance.h`에 `font_settings`(app_settings.h에서 옮김)와 `font_overrides`를 나란히
두고 `apply_overrides`를 더했다. `appearance_settings`/`appearance_overrides`와 같은
모양이다.

**빈 문자열도 정의다.** `""`는 "이 문서는 내장 글꼴을 쓴다"는 뜻이라 "앱 설정을
따른다"(값 없음)와 구별해야 한다 — `optional`의 유무로 가른다.

`workspace_document`에 `font_overrides fonts`를 더했다.

### 3.2 저장소

문서 JSON에 `fonts` object가 생긴다. 키 이름은 앱 설정 파일과 같다(`ui`·`code`).

```json
"fonts": { "ui": "", "code": "Cascadia Code" }
```

`appearance`와 같은 규칙이다 — 정의된 키만 남기고, 정의를 거둔 키는 지우며, 알 수 없는
키는 보존한다. 값이 문자열이 아니면 경고 하나를 남기고 앱 설정을 따른다. 어떤 오류도
문서 열기를 막지 않는다.

### 3.3 logic

| 자리 | 내용 |
| --- | --- |
| `effective_fonts()` | 앱 글꼴 위에 문서 override를 얹은 유효 값 |
| `settings_dialog_state` | `ui_font_family`·`code_font_family` 초안과 `*_defined` 플래그 |
| `handle_open_settings` | 유효 값에서 초안을 시작하고, 문서 모드면 정의 여부를 싣는다 |
| `handle_confirm_font_dialog` | **앱 설정을 곧바로 고치던 것을 그만두고** 초안에 쓴다. 건드린 행은 문서 모드에서 "문서에 정의됨"이 된다 |
| `handle_confirm_settings` | 문서 모드면 `document_->fonts`, 전역 모드면 `app_settings_.fonts` |
| `handle_clear_settings_override` | `ui_font`·`code_font` 배지가 정의를 거둔다 |
| `publish_snapshot` | 유효 값을 싣고, 환경설정이 열려 있는 동안에는 초안을 미리 보여 준다 |

테마·키 컬러와 **똑같은** 흐름이다: 고르면 곧바로 미리 보이고, `저장`이 남기고,
`취소`가 되돌린다.

### 3.4 표시

글꼴 항목이 `settings_override_field`를 가져 문서 모드에서 `덮어씀` 배지가 붙고,
누르면 그 행이 앱 설정을 따르게 돌아간다.

## 4. 신규 test 6 case

`settings_dialog_tests` 4 case.

- `The font draft lands in the document on save` — 초안 미리 보기, 문서 저장에 실리는
  것, 건드리지 않은 행이 정의되지 않은 채 남는 것, 앱 설정이 그대로인 것.
- `The font override badge hands the row back to the app settings` — 배지가 정의를
  거두고 저장이 문서에서 지운다.
- `Cancelling the settings dialog rolls the font preview back` — `취소`가 미리 보기를
  되돌리고 저장이 나가지 않는다.
- `Without a document the font draft still edits the app settings` — 전역 모드는
  앱 설정 저장으로 나간다.

`json_project_store_tests` 1 case · `project_schema_tests` 1 case.

- 문서 JSON 왕복 — 빈 이름이 정의로 살아남고, 거둔 키는 지워지며, 알 수 없는 키는
  보존된다.
- 파서 — 정의한 키만 override, 빈 이름도 정의, 문자열이 아니면 경고 하나.

## 5. 곁들여 고친 것

`build_settings_dialog_view`가 한 줄 걸러 빈 줄이었다. 이 함수를 `publish_snapshot`
에서 뽑아낼 때(3단계) 이미 CRLF인 본문에 `\n` → `\r\n` 치환을 한 번 더 걸어 `\r\r\n`이
된 것이 원인이다. 원래 모양대로 되돌렸다. 같은 원인의 손상이 이전 세션에도 있어
(`handle_font_dialog_scroll` 등) 이번에 손댄 세 함수는 함께 정리했다.

## 6. 남은 확인 (수동)

- 문서를 연 상태에서 환경설정 → `테마` 탭 → 글꼴을 고르고 저장한 뒤, 다른 문서를 열면
  그 문서의 글꼴로 돌아오는 것.
- `덮어씀` 배지를 눌러 정의를 거두면 앱 설정 글꼴로 돌아오는 것.
- 기존 문서에는 `fonts` 키가 없으므로 지금까지와 똑같이 앱 설정을 따른다.
