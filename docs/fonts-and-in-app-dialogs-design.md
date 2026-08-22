# 글꼴 설정 · 글꼴 선택 dialog · 새 문서 dialog의 앱 UI 통일

상태: **구현 중** — 2026-08-23 사용자 지시 2건의 설계다. 같은 브랜치에서 이어서
진행한다.

| 항목 | 요구 |
|---|---|
| F1 | 앱 단위 설정에서 **UI 글꼴**과 **코드 글꼴**을 고를 수 있게 한다 |
| F2 | 시스템에 설치된 글꼴을 보여 주고 **미리 보기**도 함께 보여 주는 선택 dialog를 만든다 |
| F3 | `새 문서 만들기` dialog를 앱 UI dialog로 바꿔 스타일을 통일한다 |

---

## F1. 설정 값

글꼴은 문서가 아니라 **앱에만** 남는다 (지시: "앱 단위 설정"). 테마·키 컬러가 문서
override를 갖는 것과 달리 override 계층을 두지 않으므로 `appearance_settings`가
아니라 `app_settings` 바로 아래에 둔다.

```cpp
struct font_settings
{
    // 비어 있으면 내장 글꼴이다. 시스템 글꼴 가족 이름을 그대로 담는다.
    std::u8string ui_family {};
    std::u8string code_family {};
};
```

- **UI 글꼴**: 창의 모든 글자다 (카드·dialog·도구 막대).
- **코드 글꼴**: 로그 뷰 본문과 로컬 변경 dialog의 diff 본문이다. 외부 명령 출력과
  diff는 열이 맞아야 읽히므로 UI와 다른 글꼴을 쓸 수 있어야 한다.
- 목록에 없는 이름이 저장돼 있으면 조용히 내장 글꼴로 되돌아간다 — 설정 파일은
  건드리지 않는다 (다른 PC에서 열었다가 돌아올 수 있다).

## F2. 글꼴 해석과 선택 dialog

### F2.1 해석은 platform이 한다

Skia의 `SkFontMgr_New_GDI()`가 이 build에 링크되어 있어 가족 열거와
`matchFamilyStyle`을 쓸 수 있다 (DirectWrite fontmgr은 빌드에 없다).

`platform/win32/win32_fonts.{h,cpp}`에 **글꼴 registry**를 둔다.

```cpp
// 시스템에 설치된 가족 이름을 정렬해 돌려준다. UI thread에서 한 번 부른다.
[[nodiscard]] std::vector<std::u8string> installed_font_families();

// 현재 설정을 반영한다 (UI thread가 매 frame 부른다. 값이 같으면 아무 일도 없다).
void set_configured_fonts(std::u8string_view ui_family, std::u8string_view code_family);
[[nodiscard]] sk_sp<SkTypeface> configured_ui_typeface();
[[nodiscard]] sk_sp<SkTypeface> configured_code_typeface();
// 미리 보기용이다. 못 찾으면 nullptr다.
[[nodiscard]] sk_sp<SkTypeface> family_typeface(std::u8string_view family);
```

registry는 mutex로 보호한 이름 → typeface cache 하나를 process 단위로 갖는다.
업무 상태가 아니라 **OS 자원의 cache**라 ADR-004의 단일 소유 규칙 밖이다. 그리기
thread와 입력 thread가 같은 값을 봐야 하므로(아래) 공유가 필요하다.

### F2.2 그리기와 hit test가 같은 글꼴을 써야 한다

입력 thread의 측정 함수가 내장 글꼴을 계속 쓰면 사용자가 UI 글꼴을 바꾼 순간
caret 자리가 어긋난다. 그래서 측정 함수도 `configured_ui_typeface()`를 부른다 —
registry가 공유되는 이유다.

### F2.3 element가 가족 이름으로 글꼴을 얻는 길

글꼴 선택 dialog의 각 행은 **그 글꼴로** 이름을 그려야 미리 보기가 된다.
element는 `presentation` 계층이라 `platform/win32`를 직접 부를 수 없으므로
`draw_context`에 해석기 interface를 실어 보낸다.

```cpp
// 가족 이름을 typeface로 옮긴다. 없으면 nullptr다.
struct font_resolver
{
    virtual ~font_resolver() = default;
    [[nodiscard]] virtual SkTypeface* family(std::u8string_view name) const = 0;
};

struct draw_context { …; SkTypeface* code_typeface {}; const font_resolver* fonts {}; };
```

### F2.4 목록은 UI thread가 logic에 알린다

열거는 OS 호출이라 logic이 할 수 없다. 창을 만든 직후 UI thread가 한 번 열거해
`font_families_event`로 logic에 보내고, logic이 그대로 들고 있다가 dialog를 열 때
쓴다. 실패하면 빈 목록이고 dialog는 안내를 띄운다.

### F2.5 dialog 모양

환경설정 `테마` 탭에 `글꼴` 섹션을 더한다 — 항목 둘(`UI 글꼴`, `코드 글꼴`)이고
각각 현재 이름과 `선택…` 버튼이다. 버튼이 글꼴 선택 dialog를 연다.

```
┌ UI 글꼴 선택 ───────────────────────────────┐
│ [⟳] [ 이름으로 거르기            ] [×]      │  ← 질의 막대 (기존 element 재사용)
│ ┌─────────────────────────────────────────┐ │
│ │ (기본 글꼴)                             │ │
│ │ Consolas            ← 그 글꼴로 그린다  │ │
│ │ 맑은 고딕                               │ │
│ └─────────────────────────────────────────┘ │
│ ┌ 미리 보기 ────────────────────────────┐   │
│ │ 다람쥐 헌 쳇바퀴에 타고파 0123 {}();  │   │
│ └───────────────────────────────────────┘   │
│                            [선택] [취소]    │
└─────────────────────────────────────────────┘
```

- 목록의 첫 항목은 `(기본 글꼴)`이고 고르면 빈 이름 = 내장 글꼴이다.
- 필터 칸은 **switch dialog의 질의 막대와 같은 규칙**이다 — `text_input_element`와
  `text_input_target::font_filter`를 그대로 쓴다. 편집 기능(선택·실행 취소 등)이
  따라온다.
- 미리 보기 문구는 한글·영문·숫자·기호를 한 줄에 담아 코드 글꼴 판단에 쓴다.
- 고른 값은 **`선택`을 눌러야** 설정에 반영된다. 환경설정의 다른 항목과 달리
  글꼴은 앱 설정을 곧바로 고치고 저장을 예약한다 (문서 override가 없어 초안 계층이
  필요 없다).

## F3. 새 문서 dialog의 앱 UI 통일

지금은 `platform/win32/version_list_generation_dialog.cpp`(753줄)가 Win32 control로
그린 모달 팝업이다. 창 스타일·글꼴·색이 앱과 따로 논다.

앱 UI dialog로 바꾼다.

| 요소 | 값 |
|---|---|
| 스캔 폴더 | 읽기 전용 경로 + `찾아보기…` 버튼 (Win32 폴더 dialog는 그대로 쓴다) |
| 문서 경로 | 읽기 전용 경로 + `찾아보기…` 버튼 (Win32 저장 dialog) |
| 메시지 | 검증 실패 사유 (빈 값·상대 경로·확장자 등) |
| 버튼 | `만들기` · `취소` |

- 경로 자체는 여전히 Win32 파일 dialog가 고른다 — 앱이 파일 탐색기를 다시 만들
  이유가 없고, 탐색 등록의 폴더 선택도 이미 같은 방식이다.
- 검증은 logic이 한다. 지금 Win32 dialog가 하던 어휘 규칙(절대 경로,
  `.version-list` 확장자, 스캔 폴더 존재)을 그대로 옮긴다. 최종 검증과 원자적
  생성은 지금처럼 worker의 생성 service가 한다.
- `ui_command`에 폴더·저장 경로 선택 둘을 더하고, 고른 값은 새 intent로 logic에
  돌아온다. 취소하면 아무 일도 없다.
- Win32 dialog 파일과 그 resource는 지운다.

## 영향 범위

| 파일 | 변경 |
|---|---|
| `domain/app_settings.{h,cpp}` | `font_settings` |
| `infrastructure/json_app_settings_store.cpp` | 글꼴 읽기·쓰기 |
| `platform/win32/win32_fonts.{h,cpp}` | **신규** registry·열거·해석 |
| `presentation/ui/ui_element.h` | `font_resolver`, `draw_context.code_typeface`·`fonts` |
| `presentation/ui/log_view_element.cpp`, `local_changes_dialog_element.cpp` | 코드 글꼴 사용 |
| `presentation/ui/font_dialog_element.{h,cpp}` | **신규** 글꼴 선택 dialog |
| `presentation/ui/generate_document_dialog_element.{h,cpp}` | **신규** 새 문서 dialog |
| `presentation/view_snapshot.h` | `font_dialog_view`, `generate_document_dialog_view`, 설정 view의 글꼴 항목 |
| `application/app_messages.h` | intent·event 추가, `text_input_target::font_filter` |
| `application/logic_controller.{h,cpp}` | 두 dialog 상태와 handler, 글꼴 설정 저장 |
| `platform/win32/win32_application.cpp` | 글꼴 열거·registry 갱신·저장 경로 선택, Win32 생성 dialog 제거 |
| `platform/win32/version_list_generation_dialog.*` | **삭제** |
