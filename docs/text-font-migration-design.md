# 직접 그리는 글꼴을 luil `make_text_font`로 옮기는 설계

상태: **검수 대기** (2026-11-02 작성, 구현 전)

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

## 검수 결정

(검수 뒤 채운다)
