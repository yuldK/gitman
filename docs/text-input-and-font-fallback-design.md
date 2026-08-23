# 텍스트 입력(IME·BMP 밖 문자) · 글꼴 대체 · 설정 dialog 스크롤 설계

상태: **검수 반영** (2026-08-23. 아래 검수 결정 3건을 본문에 반영했다)

2026-08-23 사용자 지시 4건의 설계다.

| 항목 | 요구 |
|---|---|
| E1 | 클라이언트 높이가 설정 dialog보다 낮으면 스타일이 깨진다. 스크롤을 주어 스타일을 지킨다 |
| E2 | 한글 조합을 텍스트 박스 안에서 한다. BMP 밖 문자(이모지)도 칠 수 있게 한다. `WM_CHAR`보다 현대적인 방식을 쓴다 |
| E3 | 글꼴 dialog를 빠르게 내리면 abort한다 (`SkAutoToGlyphs` 콜스택) |
| E4 | `Cascadia Code`에서 한글이 제대로 나오지 않는다 |

| 검수 지점 | 결정 |
|---|---|
| 대체 해석기 전달 (E3.4) | **A · 한 번 등록.** `font_resolver`에 `fallback`을 더하고 platform이 시작 시 presentation에 등록한다 |
| IME 방식 (E2.1) | **TSF (`ITextStoreACP2`).** IMM32 인라인 조합이 아니다 |
| 설정 스크롤 범위 (E1.2) | **항목 열만.** rail·제목·메시지·버튼은 고정 |

---

## E0. 조사 결과

### E0.1 E3와 E4는 뿌리가 같다 — GDI font manager

`win32_fonts.cpp`는 `SkFontMgr_New_GDI()`로 가족을 열거·해석한다. 그런데 Skia는
`UNICODE` 매크로 **없이** 빌드된다. `SkFontHost_win.cpp`의 `TCHAR`가 `char`가 되어
ANSI GDI API로 컴파일된다는 뜻이다. 산출물에서 확인했다.

```
dumpbin /SYMBOLS third_party/skia/out/gitman-release/skia.lib
  __imp_EnumFontFamiliesExA
  __imp_CreateFontIndirectA
  __imp_GetTextFaceA
```

`tchar_to_skstring`은 ANSI 경로에서 `s->set(t)`로 **바이트를 그대로** SkString에
넣는다. 한국어 Windows(ACP 949)에서는 `맑은 고딕`·`굴림` 같은 이름이 **CP949
바이트열**로 오고, `installed_font_families()`는 그것을 UTF-8이라 믿고 담는다.

그 이름을 그리면 이렇게 된다.

```
SkFont::countText(CP949 바이트, kUTF8)  →  SkUTF::CountUTF8 실패  →  -1
SkAutoToGlyphs:  const size_t count = -1  →  SIZE_MAX
AutoSTArray<32, SkGlyphID>::reset(int count)  →  count == -1
  SkASSERT(count >= 0)  →  sk_abort_no_print
```

이 PC에서 두 관리자를 나란히 세어 확인했다.

| 관리자 | 가족 수 | UTF-8이 아닌 이름 |
|---|---|---|
| `SkFontMgr_New_GDI` | 289 | **22** |
| `SkFontMgr_New_DirectWrite` | 227 | 0 |

깨진 첫 이름의 바이트는 `B8 BC C0 BA 20 B0 ED B5 F1` — CP949로 읽으면 정확히
`맑은 고딕`이다.

첨부해 준 콜스택 그대로다. 그러므로 조건은 **스크롤 속도가 아니라 한글 이름 글꼴
행이 목록에 처음 들어오는 순간**이다. 천천히 내리면 그 전에 멈췄을 뿐이다. 같은
이유로 한글 이름 글꼴을 고르면 환경설정의 값 줄에서도 같은 abort가 난다.

그리고 `SkFontMgr_win_gdi`의 대체 진입점은 이렇게 되어 있다.

```cpp
sk_sp<SkTypeface> onMatchFamilyStyleCharacter(..., SkUnichar) const override {
    return nullptr;
}
```

**GDI 관리자로는 글꼴 대체를 할 수 없다.** 이것이 E4의 절반이다.

### E0.2 Cascadia Code에는 한글 글리프가 없다

이 PC의 글꼴을 직접 확인했다 (`PresentationCore`의 `CharacterToGlyphMap`).

| 가족 | 글리프 수 | `U+AC00`(가) | `U+D55C`(한) |
|---|---|---|---|
| Cascadia Code | 1483 | 없음 | 없음 |
| Cascadia Mono | 1483 | 없음 | 없음 |

VS Code·Windows Terminal에서 한글이 보이는 것은 그 앱들이 **글꼴 대체**를 하기
때문이다 (라틴은 Cascadia, 한글은 맑은 고딕 같은 다른 글꼴로 그린다). Gitman은
`drawSimpleText` 한 번으로 typeface **하나만** 쓰므로 없는 글자는 `.notdef`가 된다.

즉 E4는 "Cascadia Code가 한글을 지원하는데 이상하다"가 아니라 **Gitman에 글꼴 대체가
없다**가 정확한 진단이다.

### E0.3 E2는 Skia 버전 문제가 아니다

현재 Skia는 m148(submodule 최신 고정)이고 필요한 것은 전부 들어 있다. 지금 입력
경로가 아래와 같아서 생긴 일이다.

- `WM_IME_STARTCOMPOSITION`·`WM_IME_COMPOSITION`을 **조합 창 위치만 옮기고**
  `DefWindowProc`으로 흘린다 → 조합은 시스템이 별도 창에 그린다 (박스 밖).
- `WM_CHAR`에서 surrogate(`0xD800`~`0xDFFF`)를 **버린다** → BMP 밖 문자가 사라진다.
- `text_edit_encode_utf8`이 3바이트까지만 만든다 (주석에 그렇게 적혀 있다).

Skia 버전을 올릴 필요는 없다.

### E0.4 이미 갖춰져 있는 것

- `embedded_assets.cpp`는 **이미** `SkFontMgr_New_DirectWrite()`를 쓴다. 즉 앱이
  글꼴 관리자를 두 종류로 만들고 있고, 목록만 GDI 쪽이라 깨진다.
- `skia.lib`에 `SkFontMgr_New_DirectWrite` 심볼이 있다 (확인함). `dwrite`·`imm32`도
  이미 링크되어 있다. **Skia 재빌드는 필요 없다.**

---

## E1. 설정 dialog 스크롤

### E1.1 현황

`settings_dialog_element::arrange`는 panel 높이를 창에 맞춰 자른다.

```cpp
float height { panel_height_ * scale };
if (height > context.slot.height)
    height = context.slot.height;
```

그런데 항목 배치(`layout_for`)는 잘린 것을 모르고 `top + content_top`부터 **원래
높이만큼** 아래로 계속 쌓는다. 그래서 항목이 저장·취소 버튼 줄을 뚫고 panel 밖으로
넘친다 — 지시가 말한 "스타일이 깨진다"이다.

### E1.2 무엇이 스크롤되는가

**오른쪽 항목 열만** 스크롤한다. 왼쪽 탭 rail, 제목, 메시지 줄, 저장·취소 버튼은
제자리에 남는다. 탭을 못 옮기게 되거나 저장 버튼이 화면 밖으로 흘러가면 안 된다.

```
┌ 환경설정 ──────────────────────────────────────────┐
│                                                    │  content_top
│ ┌ 도구  ┐  ┌ 항목 열 (여기만 스크롤) ─────────┐ ▲ │
│ │ 동작  │  │ Git 실행 파일        [찾아보기]  │ █ │
│ │ 문서  │  │ SVN 실행 파일        [찾아보기]  │ ║ │  ← 스크롤 막대
│ └ 테마  ┘  └──────────────────────────────────┘ ▼ │
│ 메시지                              [저장] [취소]  │
└────────────────────────────────────────────────────┘
```

### E1.3 치수와 한계

`panel_height_for`가 이미 쓰는 값들을 그대로 쓴다.

| 값 | 크기 |
|---|---|
| `content_top` | 44 |
| `message_height` | 18 |
| `action_button_height` | 28 |
| `panel_padding` | 14 |
| **chrome 합계** | **104** |

```
panel_height = min(chrome + content_height, window_height)
viewport     = max(panel_height - chrome, item_title_height)   // 22 이하로는 안 줄인다
maximum      = max(content_height - viewport, 0)
```

logic과 element가 **같은 값**을 써야 하므로 계산 함수를 한 곳에 둔다. 항목 높이
계산은 이미 `settings_dialog_element.cpp`의 익명 namespace에 있고 Skia도 글자 폭도
쓰지 않는(순수 산술) 코드라, 파일을 옮기지 않고 헤더에 두 함수만 노출한다.

```cpp
// presentation/ui/settings_dialog_element.h
// 활성 탭이 아니라 **가장 높은 탭**의 내용 높이다. 탭을 옮길 때 창이 튀지 않는다 (S1.3).
[[nodiscard]] float settings_dialog_content_height(const settings_dialog_view& dialog);
// 항목 열이 실제로 보이는 높이다. 창이 낮으면 chrome을 뺀 나머지다.
[[nodiscard]] float settings_dialog_viewport_height(const settings_dialog_view& dialog, float window_height);
```

`logic_controller`는 이미 `presentation/list_metrics.h`를 쓰고 tree도 logic thread가
만들므로 계층 위반이 아니다.

### E1.4 배선

| 자리 | 추가 |
|---|---|
| `app_messages.h` | `struct settings_scroll_intent { float delta; };` + variant 등록 |
| `view_snapshot.h` | `settings_dialog_view::scroll_offset` (논리 픽셀, 이미 고정됨) |
| `logic_controller` | `handle_settings_scroll(delta)`. dialog를 열 때·탭을 옮길 때 0으로 되돌린다 |
| `ui_interaction.cpp` | 휠 라우팅에 `settings_dialog` 분기 추가. **글꼴 dialog보다 뒤**, switch dialog보다 앞이다 (글꼴 dialog가 환경설정 위에 뜬다) |
| `settings_dialog_element` | 항목 열을 `clipRect`로 자르고 `-scroll_offset`만큼 올려 배치. 내용이 넘칠 때만 `scrollbar_element`를 만든다 |
| `ui_element.h` | `settings_dialog_scrollbar` kind 추가 |

스크롤 막대는 다른 dialog와 같은 `scrollbar_element`를 쓰고, 보일 때 항목 열의 폭을
`layout_scrollbar_hit_width`만큼 줄인다.

### E1.5 test

- `settings_dialog_tests`: 창이 낮을 때 viewport·maximum 값, 스크롤 후 항목이
  chrome을 침범하지 않는 것, 탭 이동이 offset을 0으로 되돌리는 것.
- `ui_interaction_tests`: 환경설정이 열린 상태의 휠 → `settings_scroll_intent`,
  글꼴 dialog가 위에 뜨면 그쪽이 가져가는 것.

---

## E2. TSF 인라인 조합과 BMP 밖 문자

### E2.1 방식 (검수 결정: TSF)

검수에서 **TSF(`ITextStoreACP2`)**로 정했다. 문서를 OS에 노출하고 IME(TIP)가 직접
조회·수정하게 한다.

| | |
|---|---|
| 얻는 것 | 조합이 박스 안에서 이루어진다. IME의 재변환·자동 수정·필기·음성 입력 같은 TIP 기능이 그대로 붙는다. surrogate는 UTF-16 경로라 자연히 따라온다. 조합 밑줄 종류를 IME가 지정한 대로 그릴 수 있다 |
| 치르는 값 | `ITextStoreACP2` 25개 메서드 + 조합 sink + lock 규약. **`platform/win32`에 900~1300줄이 새로 생긴다.** E2.3의 그림자 문서가 ADR-004에 예외를 하나 더한다 |

`ITextStoreACP2`를 쓴다 — `ITextStoreACP`에서 `GetWnd`·`AdviseMouseSink`·
`UnadviseMouseSink`가 빠진 판이다 (SDK 10.0.26100에서 확인). COM STA는 `main.cpp`가
이미 `CoInitializeEx(COINIT_APARTMENTTHREADED)`로 잡아 둔다.

### E2.2 구성

```
main.cpp          CoInitializeEx (이미 있음)
 └ application_window
    └ tsf_input_session          ← 신규: platform/win32/win32_tsf_input.{h,cpp}
       ├ ITfThreadMgr2           CLSID_TF_ThreadMgr → ActivateEx
       ├ ITfDocumentMgr          창에 하나. 초점 이동은 문서 갈이가 아니라 그림자 갈이다
       ├ ITfContext              CreateContext(client_id, 0, text_store, …)
       ├ text_store              ITextStoreACP2 구현. 그림자 문서를 본다
       └ composition_sink        ITfContextOwnerCompositionSink. 조합 구간을 알아낸다
```

`ITfDocumentMgr`는 창에 **하나만** 둔다. 텍스트 박스는 4종이지만 동시에 초점을 갖는
것은 하나뿐이므로, 초점이 옮겨 가면 문서를 바꾸는 대신 **그림자 내용을 갈아 끼우고**
`ITextStoreACPSink`의 `OnTextChange`·`OnSelectionChange`·`OnLayoutChange`로 알린다.
박스마다 문서 관리자를 만들면 dialog가 열릴 때마다 COM 객체가 늘어난다.

초점이 어느 박스에도 없으면 `ITfThreadMgr2::SetFocus(nullptr)`로 TIP을 떼어 놓는다 —
목록만 보고 있을 때 IME가 키를 먹으면 안 된다.

### E2.3 ADR-004와의 접점 — UI thread의 그림자 문서

**TSF의 진짜 비용이고, 이 설계에서 새로 결정해야 하는 지점이다.**

TSF는 텍스트 저장소에 **동기로** 묻는다: `GetText`·`GetSelection`·`GetEndACP`·
`GetTextExt`. lock을 잡고 `SetText`·`InsertTextAtSelection`으로 **즉시** 쓰기도 하며,
바로 다음 `GetText`가 그 결과를 반영해야 한다.

그런데 Gitman의 글 상태는 logic thread 소유이고 UI thread에는 snapshot으로만 온다.
왕복이 비동기라 TSF의 동기 규약과 그대로는 맞지 않는다.

**해결**: 초점을 가진 박스 **하나**에 대해 UI thread가 그림자 문서(글·caret·anchor)를
갖는다. `interaction_snapshot`이 이미 그런 것처럼 업무 상태가 아니라 **입력 정규화
상태**다 — 확정된 글의 진실은 언제나 logic이 갖는다.

| 시점 | 동작 |
|---|---|
| 초점이 박스로 들어옴 | view snapshot의 `text`·`caret`·`anchor`로 그림자를 채우고 `SetFocus(document_mgr)` |
| TSF가 씀 (`SetText`·`InsertTextAtSelection`) | 그림자를 먼저 바꾸고, 같은 편집을 `edit_text_input_intent`로 logic에 보낸다 |
| logic snapshot 도착 | **조합이 없고** 그림자와 다르면 그림자를 snapshot에 맞추고 sink에 알린다. logic이 거른 결과가 여기로 온다 (제한 시간 칸의 숫자 필터) |
| 조합 중 snapshot 도착 | **미룬다.** 조합 도중 글이 밑에서 바뀌면 TIP이 혼란에 빠진다 |
| 조합 중 외부 갱신 (SVN 노드 선택) | `ITfContextOwnerCompositionServices::TerminateComposition`으로 조합을 끝내고 그림자를 새 글로 맞춘다 |
| 초점 상실·dialog 닫힘 | 조합 종료 → `SetFocus(nullptr)` |

그림자와 logic의 글이 어긋나는 구간은 **조합 중**뿐이고, 그동안 그리기는 그림자를
쓴다. 조합이 끝나면 확정 글이 intent로 logic에 가고 다음 snapshot에서 다시 하나가
된다.

ACP offset은 **UTF-16 단위**다. 그림자는 UTF-8(`std::u8string`)로 두고 경계 변환
함수를 한 쌍 두어 단위가 섞이지 않게 한다.

```cpp
// 단위가 섞이면 조합이 엉뚱한 자리에 들어간다.
[[nodiscard]] long acp_from_utf8(std::u8string_view text, std::size_t byte_offset) noexcept;
[[nodiscard]] std::size_t utf8_from_acp(std::u8string_view text, long acp) noexcept;
```

### E2.4 구현할 메서드

| 묶음 | 메서드 | 처리 |
|---|---|---|
| sink | `AdviseSink`·`UnadviseSink` | `ITextStoreACPSink` 하나를 보관한다 |
| lock | `RequestLock` | 동기 요청은 그 자리에서 준다. 비동기 요청은 창 메시지로 미뤄 재진입을 막는다 |
| 상태 | `GetStatus`·`QueryInsert`·`GetEndACP` | 한 줄 평문이다 |
| 선택 | `GetSelection`·`SetSelection` | 그림자의 caret·anchor다 |
| 글 | `GetText`·`SetText`·`InsertTextAtSelection` | 그림자 수정 + logic intent |
| 배치 | `GetActiveView`·`GetACPFromPoint`·`GetTextExt`·`GetScreenExt` | ui_tree의 element bounds와 `draw_primitives`의 측정으로 계산해 화면 좌표로 준다. 후보 창이 조합 글자 밑에 붙는 근거다 |
| 속성 | `RequestSupportedAttrs`·`RequestAttrsAtPosition`·`RequestAttrsTransitioningAtPosition`·`FindNextAttrTransition`·`RetrieveRequestedAttrs` | 지원 속성 없음으로 최소 구현한다 |
| 서식·임베드 | `GetFormattedText`·`GetEmbedded`·`QueryInsertEmbedded`·`InsertEmbedded`·`InsertEmbeddedAtSelection` | `E_NOTIMPL` / 거부. 한 줄 평문 박스다 |

조합 구간은 `ITfContextOwnerCompositionSink`(`OnStartComposition`·
`OnUpdateComposition`·`OnEndComposition`)에서 `ITfRange`를 ACP로 바꿔 얻는다.
밑줄은 1단계에서 한 종류(강조색 실선)로 그리고, `GUID_PROP_ATTRIBUTE` +
`ITfDisplayAttributeMgr`로 IME가 지정한 종류를 읽는 것은 동작 확인 뒤에 붙인다.

### E2.5 키 입력과 메시지 루프

TSF에서는 키를 TIP이 먼저 볼 기회를 주어야 한다.

- `ITfThreadMgr2`에서 `ITfKeystrokeMgr`를 얻어 `WM_KEYDOWN`·`WM_KEYUP`에서
  `TestKeyDown` → `KeyDown`을 먼저 부른다. TIP이 먹었다고 하면 그 자리에서 끝낸다.
- `WM_CHAR`는 **조합 중이 아니고 TIP이 먹지 않은** 글자만 지금 경로로 흘린다.
  surrogate 병합은 그대로 필요하다 — 이모지 패널은 TSF를 거치지 않고 `WM_CHAR`로 온다.
- 메시지 루프(`GetMessageW`)는 그대로 둔다. `ITfMessagePump`는 TIP이 자체 창을 가질
  때 필요한데 지금 범위에서는 `ITfKeystrokeMgr` 경유로 충분하다. 실제 IME에서 확인해
  부족하면 그때 바꾸고 검증 기록에 남긴다.
- 기존 `WM_IME_*` 처리와 `place_composition_window`는 **지운다.** TSF와 IMM32를 함께
  두면 조합이 두 번 그려진다.

### E2.6 그리기

view는 방식과 무관하다 — TSF는 **어떻게 채우는지**만 바꾼다.

```cpp
// view_snapshot.h — text_input_view
// 조합 중인 글이다 (E2). 비어 있으면 조합이 없다. 확정 전이라 `text`에는 없다.
std::u8string composition {};
std::size_t composition_caret { 0 };
```

```
표시 글 = text[0, caret) + composition + text[caret, end)
caret   = caret + composition_caret 자리
밑줄    = composition 구간 아래 1px (강조색)
```

가로 스크롤(`scroll_for`)도 같은 표시 글 기준이어야 조합 중에 caret이 칸 밖으로
나가지 않는다.

**필터·검증은 확정된 `text`만 본다.** 조합 중인 글로 목록을 다시 거르지 않는다 —
`ㅎ`·`하`·`한`마다 목록이 튀는 것보다 확정 시점에 거르는 편이 읽기 쉽다.

### E2.7 BMP 밖 문자

- `text_edit_encode_utf8`에 4바이트 분기를 넣는다. surrogate 코드포인트와 `0x10FFFF`
  초과는 빈 문자열로 거른다 (잘못된 UTF-8을 만들지 않는다).
- `WM_CHAR`의 high/low surrogate를 창에서 병합해 `char32_t` 하나로 보낸다 — 이모지
  패널(`Win`+`.`)이 이 경로다.
- TSF 경로의 글은 UTF-16 → UTF-8 변환이 처리하므로 별도 처리가 필요 없다.
- caret 이동·지우기·낱말 판정은 이미 UTF-8 경계 기준이라 4바이트에서도 맞는다.
- **표시**는 E4의 글꼴 대체가 있어야 실제로 보인다 (Segoe UI Emoji). 컬러 이모지가
  Skia DirectWrite scaler에서 색으로 나오는지는 구현 중 확인해 검증 기록에 남긴다.
  흑백으로 나오더라도 입력·저장·복사는 정상이다.

### E2.8 test

TSF 자체는 COM·OS 의존이라 단위 test로 덮이지 않는다. **순수한 조각을 갈라내어**
그것을 test한다.

- `text_edit_tests`: 4바이트 인코딩, surrogate·범위 초과 거부, 이모지가 든 글에서
  backspace·낱말 이동.
- 신규 `text_composition_tests`: 그림자 문서 모델의 순수 부분 — ACP ↔ UTF-8 offset
  변환, 조합 삽입 후 표시 글·caret 계산, snapshot 화해 규칙(조합 중이면 미룸).
- `utf8_tests`: surrogate 쌍 병합.
- TSF 배선 자체는 **수동 검증**이다 (한글 IME 조합·후보 창 자리·이모지 패널·
  초점 이동·조합 중 dialog 닫기). 절차와 결과를 검증 기록에 남긴다.

---


## E3·E4. 글꼴 관리자 교체와 글꼴 대체

### E3.1 DirectWrite로 바꾼다

`win32_fonts.cpp`의 `SkFontMgr_New_GDI()` → `SkFontMgr_New_DirectWrite()`.
`embedded_assets.cpp`가 이미 쓰는 것과 같은 관리자가 되어 앱에 하나만 남는다.

| 얻는 것 | 설명 |
|---|---|
| 이름이 제대로 온다 | `sk_get_locale_string`이 UTF-16 → UTF-8로 옮긴다. CP949 바이트가 사라지고 **E3의 abort가 사라진다** |
| 사용자 로캘 이름 | 한글 이름을 가진 글꼴은 한글 이름으로 보인다 |
| 대체가 가능해진다 | `onMatchFamilyStyleCharacter`가 실제로 구현되어 있다 (E4) |

기존 설정 파일에 CP949로 깨진 이름이 저장돼 있으면 해석에 실패해 조용히 기본 글꼴로
돌아간다 (지금도 없는 이름은 그렇게 처리한다). 다시 고르면 된다.

### E3.2 잘못된 UTF-8은 Skia에 닿지 않게 한다

근본 원인을 고쳐도 관문은 남긴다. 외부에서 오는 글(프로세스 출력·경로·설정 파일·
클립보드)은 언제든 깨질 수 있고, 지금 구조에서는 그 한 글자가 **프로세스를 죽인다**.

`draw_primitives`의 `measure_text`·`draw_text`가 Skia로 가는 **유일한 통로**이므로
여기에서 검증한다.

```cpp
// 잘못된 바이트열을 U+FFFD로 바꾼 사본이다. 온전하면 복사하지 않는다.
[[nodiscard]] std::u8string_view sanitize_text(std::u8string_view text, std::u8string& storage);
```

바이트 스캔 한 번(O(n))이고 대부분의 문자열은 짧다. 위반이 없으면 할당도 복사도
하지 않는다.

### E3.3 글꼴 대체 (run 나누기)

`measure_text`·`draw_text`가 문자열을 **run**으로 나눈다.

```
"Cascadia 한글 😀"
 └ run 0: "Cascadia "  → Cascadia Code
 └ run 1: "한글 "      → 대체(맑은 고딕)
 └ run 2: "😀"         → 대체(Segoe UI Emoji)
```

- 기본 typeface에서 `unicharToGlyph(cp) != 0`인 동안 한 run이다.
- 0이면 대체를 찾는다: `SkFontMgr::matchFamilyStyleCharacter(nullptr, style, nullptr, 0, cp)`.
- 못 찾으면 기본 typeface로 그린다 (`.notdef`가 보이는 편이 침묵보다 낫다).
- 규칙은 하나뿐이다: **기본 글꼴이 그릴 수 있으면 기본 글꼴이다.** 그래서 한글 뒤의
  영문이 원래 글꼴로 돌아오고, 코드 글꼴에서 공백이 고정폭을 유지한다 (초안의
  "공백은 직전 run을 따라간다"는 규칙은 버렸다 — 공백을 이모지 글꼴로 그리면 열이
  어긋난다).
- **측정과 그리기가 같은 함수로 run을 나눈다.** 다르면 caret 자리와 글자가 어긋난다.

대체 결과는 `codepoint → typeface` cache로 남긴다. 매 frame `matchFamilyStyleCharacter`를
부르면 DirectWrite 조회 비용이 그대로 들어온다.

### E3.4 대체 해석기를 어떻게 전달하는가

`draw_primitives`의 함수들은 지금 `(canvas, text, x, y, font, paint)`만 받는다. 대체가
필요하면 해석기가 있어야 하는데, 호출부가 20곳이 넘는다.

| 안 | 방법 | 판단 |
|---|---|---|
| **A** | `font_resolver`에 `fallback(char32_t)`를 더하고, platform이 시작할 때 presentation에 **한 번 등록**한다. `draw_primitives`가 그 포인터를 본다 | **검수 채택.** `win32_fonts`의 registry와 성격이 같다 — 업무 상태가 아니라 OS 자원 cache라 ADR-004의 단일 소유 규칙 밖이고, 헤더에 이미 그렇게 적혀 있다. test는 등록하지 않으면 지금과 똑같이 동작한다 |
| B | 모든 그리기 함수에 `const font_resolver*`를 인자로 추가 | 명시적이지만 호출부 20여 곳을 기계적으로 고쳐야 하고, 한 곳이라도 빠뜨리면 **측정과 그리기가 어긋난다** (조용한 버그) |

**A로 확정했다.** 등록 창구는 이렇게 둔다.

```cpp
// presentation/ui/draw_primitives.h
// 없는 글자를 대신 그릴 typeface를 찾는다 (E4). platform이 시작할 때 한 번 등록하고
// 그 뒤로 바뀌지 않는다. 등록하지 않으면 대체 없이 기본 typeface로만 그린다.
struct font_fallback
{
    …
    [[nodiscard]] virtual SkTypeface* for_codepoint(char32_t codepoint) const = 0;
};

void set_font_fallback(const font_fallback* fallback) noexcept;
```

`draw_context.fonts`(가족 이름 → typeface)는 글꼴 dialog 미리 보기용으로 그대로 둔다.
`win32`의 구현체가 두 interface를 함께 구현해 registry 하나만 남는다.

### E3.5 harfbuzz·SkShaper는 지금 쓰지 않는다

`third_party/skia/out/gitman-text`에 harfbuzz·libgrapheme·skshaper·skparagraph가
빌드된 채로 이미 있다 (Release만). 하지만 이번에는 쓰지 않는다.

- 한글은 완성형 음절이라 codepoint → glyph 매핑만으로 정확히 그려진다. 조합 중인
  글자도 IME가 완성형으로 준다.
- 얻는 것은 ZWJ 이모지 결합(`👨‍👩‍👧`)과 인도계 문자 정도인데, 이 앱의 글(브랜치
  이름·경로·명령 출력)에는 거의 없다.
- 치르는 값은 Debug 구성 추가 빌드, 산출물 35 MB 증가, 그리고 **측정·그리기 경로
  전체를 shaped run 기준으로 다시 쓰는 것**이다.

E3.3의 run 나누기는 나중에 SkShaper로 갈아탈 때 그대로 자리를 물려줄 수 있는 구조다.
`docs/skia-build.md`의 `N6` 항목은 열어 둔다.

### E3.6 그 밖에 함께 고칠 것

- `installed_font_families()`가 이름 끝의 `\0`을 떼고, 그래도 UTF-8이 아닌 이름은
  목록에서 뺀다 (관리자를 바꿔도 방어는 남긴다).
- 글꼴 dialog 미리 보기 예문에 이모지를 한 자 넣어 대체가 눈에 보이게 한다.

### E3.7 test

- `ui_element_tests`: 가짜 해석기로 run 나누기 — 대체가 없을 때 run 하나,
  섞였을 때 경계, 공백이 직전 run을 따라가는 것.
- `ui_element_tests` 또는 신규: `sanitize_text`가 CP949 바이트열을 `U+FFFD`로 바꾸고
  온전한 글은 복사하지 않는 것.
- `font_dialog` 관련 test에 한글 이름 가족을 넣어 회귀를 막는다.

---


## 진행 순서

각 단계마다 검수를 요청하고 제안 커밋 메시지를 붙인다. 커밋은 사용자가 한다.

| 단계 | 내용 | 이유 |
|---|---|---|
| 1 | E3.1 관리자 교체 + E3.2 관문 + E3.6 | **지금 앱이 죽는다.** 가장 먼저 멈춘다 |
| 2 | E3.3·E3.4 글꼴 대체 | E4를 끝내고 E2의 이모지 표시 바탕을 깐다 |
| 3 | E1 설정 dialog 스크롤 | 작고 독립이다. TSF 앞에 끝내 둔다 |
| 4 | E2.7 BMP 밖 문자 (`WM_CHAR` surrogate 병합 + 4바이트 인코딩) | TSF와 무관한 조각이라 먼저 떼어 낸다 |
| 5 | E2 TSF 배선 | 가장 크다. 그림자 문서 → text store → 조합 sink → 키 라우팅 순으로 나눈다 |

## 남은 결정

- **E2.3 그림자 문서.** TSF가 동기 규약을 요구하므로 UI thread가 초점 박스의 글
  사본을 갖는다. `interaction_snapshot`과 같은 "입력 정규화 상태"로 보고 ADR-004에
  한 줄 적을 계획이다. 5단계 착수 전에 확인해 주면 좋겠다.
- **E3.5** harfbuzz·SkShaper를 이번에 넣지 않는 판단.
