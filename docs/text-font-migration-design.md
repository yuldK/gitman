# 직접 그리는 글꼴을 luil `make_text_font`로 옮기는 설계

상태: **2차 검수 대기** (2026-11-02 작성, 구현 전). 1차 검수로 D1~D4가 정해졌고 범위가
6·7장으로 넓어졌다. 넓힌 범위의 D5·D6이 남았다.

2026-11-02 UI/UX 점검(`change_log.md`의 같은 날 절)에서 "남긴 것"으로 적은 일이다.
gitman의 커스텀 element는 글꼴을 `SkFont { sk_ref_sp(context.ui_typeface), 14.0f * scale }`
꼴로 손수 짓는다. luil 사용 규칙은 이것을 피하고 `luil::make_text_font`로 지으라고 한다.
이 문서는 무엇이 실제로 바뀌는지, 어떤 순서로 옮길지, 정할 것이 무엇인지를 적는다.

## 1. 먼저 바로잡을 것 — Windows에서는 크기가 바뀌지 않는다

점검 기록에는 "손수 지은 글꼴이 시스템 글자 크기를 따르지 않는다"고 적었다. luil 소스를
보면 **Windows에서는 사실이 아니다.**

- `make_text_font`는 크기를 `scaled_font_size(size, scale)`로 정한다
  (`third_party/luil/src/ui/text_block.cpp:213`).
- `scaled_font_size`는 플랫폼이 변환 표를 넣었을 때만 표를 쓰고, 없으면
  `logical_size * display_scale`이다 (`src/ui/font_scale.cpp:17`).
- 표를 넣는 곳(`set_system_font_sizes`)은 Android뿐이다 (`src/android/android_app.cpp:932`).
  Windows의 "텍스트 크기" 접근성 설정(UISettings `TextScaleFactor`)은 luil이 읽지 않는다.

그래서 옮겨도 **글자 크기·줄 폭·elide 자리는 픽셀 단위로 그대로**다. 고정 논리 치수
(`list_metrics.h`의 카드 높이·로그 줄 높이 등)와 글이 어긋날 일도 지금은 생기지 않는다.
`change_log.md`의 그 문장은 이 문서를 쓰면서 바로잡았다.

옮겨서 실제로 달라지는 것은 셋이다.

1. **굵은 글이 진짜 Bold face로 그려진다.** 지금은 `setEmbolden`(획 덧칠)이다. luil
   `docs/font-style-design.md`는 덧칠이 작은 글씨에서 획을 뭉치게 한다고 적는다. 기본 UI
   글꼴 Malgun Gothic은 `malgunbd.ttf`가 깔려 있어 진짜 Bold가 선다. 진짜 Bold는 덧칠과
   글자 폭이 달라 그 글의 elide 자리와 가운데 정렬이 몇 픽셀 움직인다.
2. **코드 글꼴의 물러섬이 한 곳으로 모인다.** 지금 두 곳이
   `context.code_typeface != nullptr ? context.code_typeface : context.ui_typeface`를 손으로 쓴다.
   `text_font::face = code`가 같은 규칙을 luil 안에서 한다.
3. **나중에 생길 크기 규칙을 거저 받는다.** luil이 Windows 글자 크기를 읽게 되면
   `make_text_font`를 쓰는 자리만 따라간다. 그때는 2장의 고정 치수 문제가 생긴다 (5장 D4).

## 2. 지금의 자리 (전수)

`src/presentation/ui/`의 손으로 지은 `SkFont`는 57곳이다. typeface로 나누면 다음과 같다.

| 묶음 | 수 | 자리 |
| --- | --- | --- |
| UI 글꼴, 보통 굵기 | 32 | 카드 제목·경로·조각, 목록 끌기 ghost, 시작 페이지 전부, dialog 제목·본문·행 |
| UI 글꼴, 덧칠 굵기 | 6 | notice 제목, 설정 제목·섹션 머리·항목 제목, 고른 설정 탭, 문서 설정 배지 |
| 코드 글꼴 (UI로 물러섬) | 2 | 로그 본문(10.5), 로컬 변경 diff(11) |
| 고른 가족의 글꼴 | 2 | 글꼴 선택 행(12.5), 미리 보기(14) — `context.fonts->family(...)`로 얻은 face |
| codicon | 15 | 카드·시작 페이지·dialog·설정의 아이콘 |

크기는 10~14 사이의 8단계(10·10.5·11·11.5·12·12.5·13·14)에 시작 페이지 제목 26 하나다.
아이콘을 포함해 11.0(20곳)과 13.0(14곳)이 대부분이다.

측정은 그리기와 같은 `SkFont`로만 한다 (`measure_text(text, font)` 4곳: 카드 조각 폭, 로그
경과 시간·시각 열, 설정 배지). 배치(`arrange`)에서 글 폭을 재는 자리는 없다. 그래서 글꼴을
바꾸면 측정도 저절로 따라온다.

## 3. 옮기는 모양

### 3.1 도우미 세 개 (권장)

cyella는 같은 일을 `ui_font`·`code_font`·`icon_font` 셋으로 묶는다
(`cyella/src/presentation/ui/ui_helpers.cpp:164`). 이름과 갈래를 따르되 몸통은 luil의
최신 길을 쓴다. `ui_common.h`에 둔다.

```cpp
// 직접 그리는 글의 글꼴이다. 크기는 논리 픽셀이고 배율은 luil이 곱한다.
[[nodiscard]] inline SkFont ui_font(const draw_context& context, const float size, const luil::font_weight weight = luil::font_weight::regular)
{
    return luil::make_text_font(context, { .size = size, .weight = weight });
}

[[nodiscard]] inline SkFont code_font(const draw_context& context, const float size)
{
    return luil::make_text_font(context, { .size = size, .face = luil::text_layout_face::code });
}

// 아이콘은 글자 크기 규칙을 따르지 않는다 (luil `font_scale.h`: 아이콘·여백에는 쓰지 않는다).
[[nodiscard]] inline SkFont icon_font(const draw_context& context, const float size)
{
    return SkFont { sk_ref_sp(context.codicon_typeface), size * context.scale };
}
```

- 굵기는 cyella의 `bool bold` 대신 `font_weight`다. 지금 덧칠 6곳은 `font_weight::bold`로
  옮긴다 (5장 D2).
- 고른 가족 2곳은 `make_text_font(context, { .size = 12.5f, .typeface = sk_ref_sp(...) })`로
  직접 부른다. 도우미를 하나 더 두지 않는다.
- 아이콘 15곳은 `icon_font`로 모으기만 한다. 크기 공식은 지금과 같다.
- `icon_font`가 `make_text_font`를 지나지 않는 것은 의도다. luil 자신의 단추
  (`button_element.cpp:109`)도 아이콘을 `size * scale`로 짓는다.

### 3.2 대안 — 부르는 자리마다 `make_text_font`

도우미 없이 글 42곳에서 `luil::make_text_font(context, { .size = 11.0f })`를 쓴다. luil 문서와
글자가 같아 찾기 쉽지만, 아이콘 15곳은 어차피 손으로 남고 같은 꼴이 줄마다 길어진다.

### 3.3 하지 않는 것

`draw_text_within`·`draw_text`를 `draw_text_block`(정렬·elide·여러 줄)으로 바꾸는 일은
이번 범위가 아니다. 그리기 위치 계산(`centered_text_baseline`)이 전부 바뀌어 화면 비교가
57곳 모두에서 필요해진다. 글꼴만 바꾸면 위치는 그대로다.

## 4. 순서와 검증

커밋 하나가 체크포인트 하나다. 각 체크포인트는 그 파일들의 test만 돌리고, 마지막에 전역
검증을 한다.

| 체크포인트 | 범위 | 화면 변화 | 검증 |
| --- | --- | --- | --- |
| T1 | `ui_common.h` 도우미와 test (도우미가 만든 글꼴의 크기·face·굵기) | 없음 | 새 test |
| T2 | 보통 굵기·코드·아이콘 자리 전부 (카드, 목록, 로그, 시작 페이지, dialog 7개) | 없어야 한다 | 관련 test, 격리 데스크톱 스크린샷을 이전 판과 픽셀 비교 |
| T3 | 덧칠 6곳을 `font_weight::bold`로 | 굵은 글의 획과 폭 | 설정·notice dialog 스크린샷 눈 비교, 배지(62 논리 px) 안에 "문서 설정"이 드는지 |
| T4 | 고른 가족 2곳 (글꼴 선택 행·미리 보기) | 없어야 한다 | 글꼴 dialog 스크린샷 비교 |
| V | 전역 | — | Debug·Release 전체 CTest, `vs2026-analysis`, 소스 스타일, 앱 두 구성 빌드 |

T2가 "화면 변화 없음"을 지키는지가 이 설계의 핵심 검증이다. 크기 공식이 같으니 픽셀이 같아야
하고, 다르면 옮기다 크기나 face를 틀린 것이다. 비교는 같은 창 크기·같은 문서로 격리
데스크톱에서 찍은 두 PNG를 픽셀 단위로 뺀다 (luil-uia 스킬의 `Save-LuilShot`).

## 5. 열린 결정

- **D1. 묶는 모양**
  - A (권장): 3.1의 도우미 세 개. cyella와 이름·갈래가 같다.
  - B: 3.2처럼 자리마다 `make_text_font`.
- **D2. 덧칠 굵기를 진짜 Bold로**
  - A (권장): T3에서 `font_weight::bold`로 바꾼다. 획이 또렷해지고 굵은 글의 폭이 조금 바뀐다.
  - B: 덧칠을 유지한다 (`apply_font_style` 없이 `setEmbolden`). 화면은 지금 그대로다.
  - 중간 굵기(`semibold`)는 고르지 않는다. Malgun Gothic은 Regular·Bold 두 face뿐이라 어느 쪽이
    설지는 DirectWrite의 가까운 face 고르기에 달렸고, 확인하지 않았다.
- **D3. 범위**
  - A (권장): 글꼴만 옮긴다 (3.3).
  - B: 이참에 `draw_text_block`까지 옮긴다. 화면 검수가 훨씬 커진다.
- **D4. Windows 글자 크기 설정**
  - A (권장): 이번에는 다루지 않는다. luil이 Windows `TextScaleFactor`를 읽는 것은 luil 쪽
    요청으로 남긴다. 그때 `list_metrics.h`의 고정 높이(카드 높이, 로그 줄 높이, dialog 행
    높이)를 글자 크기에서 정하도록 바꾸는 설계가 따로 필요하다.
  - B: 지금 함께 설계한다.

## 6. 넓힌 범위 1 — `draw_text_block`으로 옮기기 (D3-B)

### 6.1 지금

글은 `draw_text`(12곳)와 `draw_text_within`(45곳)로 그린다. 둘 다 기준선 y를 손으로 셈한다
(`box.y + centered_text_baseline(font, box.height)`). `draw_text_within`은 넘치면 **끝**을
줄인다. 그래서 경로가 잘리면 저장소 폴더 이름이 먼저 사라진다 — 카드 경로
`E:/Programming/Lusain/gitman`이 좁은 창에서 `E:/Programming/Lu…`가 된다.

### 6.2 옮긴 모양

`luil::draw_text_block(context, text, box, style, paint)`는 상자와 규칙(`text_block_style`:
가로 정렬, 세로 정렬, 줄이는 자리, 줄 바꿈, 최대 줄 수)을 받는다. 기준선 계산이 사라지고
상자만 남는다. 도우미는 3.1의 셋에 하나를 더한다.

```cpp
// 한 줄 글을 상자에 놓는다. 세로 가운데, 넘치면 `elide` 자리를 줄인다.
rect_f draw_label_text(draw_context& context, std::u8string_view text, const rect_f& box, const luil::text_font& font, const SkPaint& paint,
    luil::text_elide elide = luil::text_elide::end, luil::text_align align = luil::text_align::start);
```

자리마다 줄이는 자리를 정한다 (5장 D6).

| 글 | 지금 | 옮긴 뒤 |
| --- | --- | --- |
| 카드 경로, 시작 페이지 최근 폴더, 로컬 변경 경로, 탐색 dialog의 대상 폴더, 설정의 실행 파일 경로 | 끝을 줄임 | **앞을 줄임** (`start`) — 폴더 이름이 남는다 |
| 문서 경로(도구 막대 label), 전환 후보 이름, 글꼴 이름 | 끝 / luil label | 가운데 (`middle`) 또는 그대로 |
| 제목·본문·배지 | 끝 | 끝 (그대로) |
| 로그 본문·diff 줄 | 끝 | 끝. 줄 높이가 고정된 목록이라 줄 바꿈하지 않는다 |
| notice dialog 본문 | logic이 나눈 줄(`lines`)을 줄마다 끝을 줄여 그림 | `wrap = true` (D6에서 함께 정한다) |

### 6.3 화면 변화와 검증

- `draw_text_block`의 세로 가운데는 줄 높이(`text_line_height`) 안의 기준선이라 지금의
  `centered_text_baseline`과 1~2 px 다를 수 있다. 픽셀 같음은 더 기대하지 않는다.
  그래서 T2를 "픽셀 비교"에서 **화면별 전후 스크린샷 눈 비교**로 바꾼다.
- 줄이는 자리를 바꾼 자리는 좁은 창(최소 480 논리 px)에서 따로 찍어 확인한다.
- 측정은 `measure_text_block`이 그리기와 같은 규칙으로 하므로, 카드 상태 조각처럼 폭을 재
  잇는 자리도 같은 함수로 잰다.

## 7. 넓힌 범위 2 — Windows 텍스트 크기 (D4-B)

### 7.1 무엇을 따라야 하나

Windows 설정 > 접근성 > 텍스트 크기(100~225%)는 **글자만** 키운다. 앱은 글이 든 칸을
그만큼 키워야 한다. 값은 WinRT `UISettings.TextScaleFactor`로 읽고 `TextScaleFactorChanged`로
바뀜을 듣는다. (Win32 앱이 같은 값을 `HKCU\Software\Microsoft\Accessibility\TextScaleFactor`
DWORD로 읽을 수 있는지, 바뀜을 `WM_SETTINGCHANGE`로 받는지는 확인하지 않았다 — T5의 첫 일이다.)

luil에는 이미 받는 자리가 있다. `set_system_font_sizes(sizes)`에 "n 논리 px 글자의 실제 크기"
표를 넣으면 `scaled_font_size`·`text_line_height`·`make_text_font`가 모두 따른다. 지금은
Android만 넣는다. Windows는 배율 하나라 표가 선형이다 (`sizes[n] = n * factor`).

### 7.2 걸림돌 — luil 컴포넌트의 고정 높이

표를 넣는 순간 gitman 글만이 아니라 **luil 컴포넌트의 글도 커진다.** 그런데 luil 컴포넌트는
글이 든 칸의 높이를 상수로 둔다: `menu_item_height` 26, `text_button_height` 28,
`dropdown_height` 28, `check_row_height` 24, `group_header_height` 28 등. 그 상수를 쓰는
gitman 자리(컨텍스트 메뉴, dialog 버튼 줄, 질의 막대 26, 도구 막대 단추 28)는 150% 근처부터
글이 칸 위아래로 넘친다. gitman만 고쳐서는 끝나지 않는다.

### 7.3 gitman 쪽에서 할 일

gitman의 고정 치수 중 **글이 드는 칸**을 글 크기에서 정한다. 아이콘·여백·창 크기는 그대로다.

| 치수 | 지금 | 옮긴 뒤 |
| --- | --- | --- |
| 카드 높이 | 64 | 여백 + 제목 줄(14) + 경로 줄(11) + 상태 조각 줄(10.5)의 `text_line_height` 합 |
| 로그 줄·헤더 | 15·26 | `text_line_height(10.5, …)`·`text_line_height(11.5, …)` |
| 시작 페이지 행·섹션·제목 | 26·26·40 | 각 글 크기의 `text_line_height` |
| 전환·로컬 변경·탐색·글꼴 dialog 행 | 22 | `text_line_height(11.5, …)` |
| 배너 | 22 | `text_line_height(label 크기, …)` |
| dialog panel 높이와 chrome 높이 | 상수 | 위 줄 높이의 합 (스크롤 한계와 배치가 같은 함수를 쓰는 지금 구조는 그대로) |

- `list_metrics.h`의 `inline constexpr`가 같은 이름의 `[[nodiscard]] inline float` 함수가 된다.
  logic thread(스크롤 한계)와 UI thread(배치)가 같은 함수를 부르는 지금 규칙은 그대로다.
  `scaled_font_size`는 어느 thread에서나 부를 수 있다 (luil `font_scale.h`).
- 배율이 100%면 새 함수가 지금 상수와 **같은 값**을 내도록 여백을 정한다. 그래야 T5 이전의
  화면이 바뀌지 않는다.
- test는 `set_system_font_sizes`로 150%·225% 표를 넣고 카드·행 높이와 스크롤 한계가 함께
  커지는지 본다. 끝나면 표를 비운다.

### 7.4 누가 Windows 값을 읽나 (5장 D5)

- 앱이 읽는다: gitman이 시작할 때와 바뀔 때 값을 읽어 `set_system_font_sizes`를 부르고 창을
  다시 배치한다. 7.2의 luil 컴포넌트 넘침은 남는다.
- luil이 읽는다: Android처럼 플랫폼 계층이 읽고 컴포넌트 높이도 글에서 정한다. luil 저장소의
  일이다. gitman은 7.3만 해 두고 luil이 켜는 날 그대로 따른다.

## 8. 넓힌 범위의 순서

| 체크포인트 | 범위 |
| --- | --- |
| T1~T4 | 4장 그대로 (글꼴) |
| T5 | 7.1의 값 읽기 확인(작은 시험 코드, 커밋하지 않음)과 D5 결론 기록 |
| T6 | 7.3 — `list_metrics.h` 치수를 함수로, 100%에서 값이 같음을 test로 고정 |
| T7 | 6장 — 화면 묶음(카드·목록 → 시작 페이지 → dialog 일곱 → 로그)마다 커밋 하나 |
| T8 | D5가 "앱이 읽는다"면 그 연결. 아니면 luil 요청 문서 |
| V | 전역 검증 + 100%·150%(test 표) 스크린샷 |

## 검수 결정

2026-11-02 1차 검수:

- D1 → A: 도우미 세 개 (`ui_font`·`code_font`·`icon_font`).
- D2 → A: 덧칠 6곳을 진짜 Bold(`font_weight::bold`)로.
- D3 → B: `draw_text_block`까지 옮긴다 → 6장.
- D4 → B: Windows 텍스트 크기를 지금 함께 설계한다 → 7장.

넓힌 범위에서 새로 정할 것:

- **D5. Windows 텍스트 크기를 누가 읽나** (7.4)
  - A (권장): luil이 읽는다. gitman은 7.3(치수 함수화)까지만 하고, luil에 플랫폼 읽기와
    컴포넌트 높이를 요청한다. luil 컴포넌트가 커지지 않는 채로 켜면 메뉴·단추 글이 넘친다.
  - B: gitman이 읽어 `set_system_font_sizes`를 부른다. 지금 바로 커지지만 7.2의 넘침을 안고 간다.
- **D6. 줄이는 자리** (6.2 표)
  - A (권장): 경로는 앞을 줄이고(`start`), 나머지는 지금처럼 끝을 줄인다. notice 본문만 `wrap`.
  - B: 표의 "옮긴 뒤"를 자리마다 다시 고른다.
