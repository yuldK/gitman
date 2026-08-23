# 검증 기록 - 설정 dialog 스크롤 (3단계)

- 일자: 2026-08-23
- 대상: `feature/switch-dialog-query-bar` 브랜치 (이어서 진행)
- 기준 문서: `docs/text-input-and-font-fallback-design.md` (E1)
- 앞 단계: `docs/verification/2026-08-23-font-fallback.md`

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2026-tests-debug` | **805/805 통과** (신규 5 case) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| `cmake --build build/vs2026 --config Debug` (앱) | 성공 |
| `scripts/check_source_style.ps1 -root .` | 460 files 통과 |

## 2. 현황과 원인

`settings_dialog_element::arrange`는 panel 높이를 창에 맞춰 잘랐지만, 항목 배치
(`layout_for`)는 잘린 것을 모르고 원래 높이만큼 아래로 계속 쌓았다. 그래서 항목이
저장·취소 버튼 줄을 뚫고 panel 밖으로 넘쳤다 — 지시가 말한 "스타일이 깨진다"이다.

## 3. 고친 내용

### 3.1 항목 열만 스크롤한다 (검수 결정)

왼쪽 탭 rail, 제목, 메시지 줄, 저장·취소 버튼은 제자리에 남는다. 창이 낮아도 탭을
옮길 수 있고 저장 버튼이 화면 밖으로 흘러가지 않는다.

panel에 `content_viewport`를 두고 그 안에서만 항목과 컨트롤을 그린다. 탭 rail과 항목
컨트롤을 dialog가 아니라 **panel의 자식**으로 옮겨 clip 하나가 둘 다 덮게 했다.
`settings_panel_element::hit_test`가 같은 경계를 보므로 **잘려서 보이지 않는 컨트롤은
눌리지도 않는다**.

### 3.2 치수는 한 함수가 정한다

```cpp
// presentation/ui/settings_dialog_element.h
[[nodiscard]] settings_dialog_scroll_metrics settings_dialog_metrics(const settings_dialog_view&, float window_height);
```

logic의 스크롤 한계와 element의 배치가 이 함수 하나를 함께 쓴다. 다르면 휠이 헛돌거나
마지막 항목이 잘린다.

chrome(제목 44 + 메시지 18 + 버튼 28 + 여백 14 = 104)을 창 높이에서 뺀 것이 항목 열의
viewport다. 한 줄도 못 보일 만큼 낮으면 그 한 줄(22)만큼은 남긴다.

**두 번 센다.** 막대가 보이면 항목 열이 좁아지고(16), 좁아지면 색 격자가 한 줄 더 늘
수 있다. 막대가 필요한지 먼저 판정한 뒤 그 폭으로 다시 세어 한계와 배치가 어긋나지
않게 했다.

logic이 표시 모델을 두 곳(스냅샷 게시, 스크롤 한계 계산)에서 만들지 않도록
`build_settings_dialog_view()`로 뽑았다.

### 3.3 배선

| 자리 | 추가 |
| --- | --- |
| `app_messages.h` | `settings_scroll_intent` |
| `view_snapshot.h` | `settings_dialog_view::scroll_offset` (논리 픽셀, 이미 고정됨) |
| `logic_controller` | `handle_settings_scroll`. 탭을 옮기면 0으로 되돌린다 |
| `ui_interaction.cpp` | 휠 라우팅. 글꼴 dialog보다 뒤, switch dialog보다 앞이다 |
| `ui_element.h` | `settings_dialog_scrollbar` kind |

막대는 다른 dialog와 같은 `scrollbar_element`이고 내용이 넘칠 때만 보인다.

## 4. 신규 test (`settings_dialog_tests` 5 case)

- `A short window scrolls the settings items instead of overflowing` — 창 320px에서
  한계가 0보다 크고 막대가 생기며, 저장 버튼이 panel 안에 남는다.
- `Settings scrolling stops at both ends and resets when the tab changes` — 위로
  굴려도 0 아래로 가지 않고, 아래로 아무리 굴려도 한계에서 멈추며, 탭을 옮기면 0이다.
- `A tall window keeps the settings dialog unscrolled` — 창 1000px에서는 한계가 0,
  막대가 없고, 휠이 아무 일도 하지 않는다.
- `Scrolled settings controls leave the viewport and stop responding` — 항목은 위로
  흐르고 **탭 rail은 제자리**이며, viewport 밖으로 밀려난 자리는 눌리지 않는다.
- `The wheel scrolls the settings dialog and the font dialog wins on top` — 라우팅
  우선순위.

## 5. 남은 확인 (수동)

- 창 높이를 설정 dialog보다 낮게 줄이고 열어 스타일이 유지되는 것, 휠·막대 끌기로
  끝까지 볼 수 있는 것.
- 창이 충분히 높을 때는 지금과 똑같이 보이는 것 (막대 없음).

## 6. 알려진 한계

가로는 다루지 않았다. 창 **폭**이 panel(600 논리 픽셀)보다 좁으면 항목이 여전히
가로로 넘친다. 이번 지시는 높이에 대한 것이라 범위를 넓히지 않았다.

## 7. 다음 단계

4단계는 BMP 밖 문자(`WM_CHAR` surrogate 병합 + 4바이트 인코딩)다. 그다음이 TSF다.

---

## 8. 후속 - 창 가장자리 여백 (2026-08-23 추가 지시)

### 8.1 지시와 원인

"높이가 짧을 때 스크롤 나오는 것까지는 잘 되는데 위아래에 여백 공간이 적어도 어느
정도 있어야 하지 않겠어? 클라이언트랑 너무 딱 붙어 있는 것 같아. 내가 미세 조정할 수
있게 잘 고려해서 만들어줘."

panel 높이를 창 높이로 그대로 잘랐기 때문에 잘리는 순간 위아래 여백이 0이 되었다.
모든 dialog가 같은 방식이었다.

### 8.2 미세 조정 지점

`presentation/list_metrics.h`에 knob 하나와 함수 둘을 뒀다.

```cpp
inline constexpr float layout_dialog_edge_margin { 24.0f };        // ← 이 값만 고치면 된다
inline constexpr float layout_dialog_edge_margin_ratio { 0.12f };  // 아주 낮은 창에서 여백을 줄이는 한도

[[nodiscard]] float dialog_edge_margin(float window_height) noexcept;
[[nodiscard]] float dialog_available_height(float window_height) noexcept;
```

배치(`arrange`)와 스크롤 한계(`settings_dialog_metrics`·`switch_dialog_list_height`)가
**모두 `dialog_available_height`를 거친다.** 한쪽만 고치면 휠이 헛돌거나 마지막
항목이 잘리므로 이 지점을 하나로 묶어 두었다.

### 8.3 적용 범위

창 높이에 맞춰 내용이 줄어드는 dialog에만 넣었다.

| dialog | 적용 | 이유 |
| --- | --- | --- |
| 환경설정 | ○ | 항목 열이 스크롤한다 |
| switch (SVN 브라우저 포함) | ○ | 목록 높이가 창 높이의 함수라 자동으로 따른다 |
| 글꼴 · 탐색 · 로컬 변경 · 새 문서 · 알림 | ✕ | 내용이 창을 따라 줄지 않아, panel만 짧게 하면 **더 일찍 깨진다** |

예를 들어 글꼴 dialog(설계 428)는 지금 창 400에서도 버튼과 미리 보기가 겹치지 않지만
여백을 넣으면 352로 줄어 겹친다. 그 dialog들의 짧은 창 대응은 환경설정과 같은 작업이
각각 필요하며 남은 과제다.

### 8.4 대가

여백만큼 스크롤이 일찍 시작된다. 환경설정은 이제 `설계 높이 + 48`보다 낮은 창에서
막대가 생긴다. 실제로 **문서 모드가 600px 창에서 막대를 갖게 되었다** — 전에는 딱
맞았다. 의도한 맞바꿈이고, `layout_dialog_edge_margin`을 줄이면 그만큼 되돌아간다.

### 8.5 test

신규 3 case (`settings_dialog_tests`).

- `A clamped dialog keeps a margin from the client edges` — 창 320에서 panel이
  위아래 여백만큼 떨어져 있고, 그래도 잘린 상태이며 사라지지 않는다.
- `The scroll limit accounts for the dialog margin` — panel 높이 = chrome + viewport다.
  배치와 한계가 같은 여백을 보지 않으면 이 등식이 깨진다. 끝까지 스크롤한 값도 확인한다.
- `A very short window shrinks the margin instead of the panel to nothing` — 창이
  낮으면 여백부터 줄고, 어떤 높이에서도 여백 둘이 창을 넘지 않는다.

고쳐 쓴 test 1 case.

- `The scope badge sits above the item title without moving the controls` — 문서 모드와
  전역 모드를 600px 창에서 나란히 비교하고 있었는데, 여백이 들어가면서 문서 모드에만
  막대가 생겨 항목 열 폭이 16 달라졌다. **동작이 아니라 test의 전제가 틀렸다** —
  두 모드 모두 스크롤이 필요 없는 높이(1000)로 올려 원래 의도(배지가 컨트롤을 밀지
  않는다)만 검사하게 했다.

### 8.6 재검증

| 항목 | 결과 |
| --- | --- |
| `ctest` Debug / Release | **820/820 통과** (양쪽) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| 앱 빌드 Debug / Release | 성공 |
| `check_source_style.ps1` | 466 files 통과 |

---

## 9. 후속 - 탭별 스크롤 재계산 (2026-08-23 추가 지시)

### 9.1 지시와 원인

"각 세션별로 스크롤 계산을 새로 하기. 세션을 눌러서 표시하는 이벤트가 있기 때문에
해당 시점에 재계산 처리."

`settings_dialog_metrics`가 스크롤 한계를 **가장 높은 탭**의 내용으로 세고 있었다.
panel 높이를 가장 높은 탭에 맞추는 규칙(S1.3)을 한계 계산까지 그대로 쓴 것이 원인이다.
그래서 짧은 탭에서도 가장 높은 탭만큼 스크롤되어 **빈 공간이 보였다.**

### 9.2 고친 내용

두 값을 갈랐다.

| 값 | 기준 | 이유 |
| --- | --- | --- |
| `panel_content_height` | **가장 높은 탭** | 탭을 옮길 때 dialog가 커졌다 작아졌다 하지 않는다 (S1.3) |
| `content_height` | **지금 보고 있는 탭** | 스크롤 한계와 막대 표시가 이 값으로 정해진다 |

`settings_dialog_metrics`가 `dialog.active_tab`을 보므로, 탭을 누르면 다음 snapshot이
이 함수를 다시 거치며 그 시점에 새로 계산된다. offset은 이미 0으로 되돌아간다.

측정값 (창 520, 논리 픽셀. 10장의 여백이 들어간 뒤의 값이다):

| 탭 | 내용 높이 | viewport | 스크롤 한계 (고친 뒤) | 고치기 전 |
| --- | --- | --- | --- | --- |
| 도구 | 238 | 368 | **0** | 112 |
| 동작 | 266 | 368 | **0** | 112 |
| 외양 | 480 | 368 | 112 | 112 |
| 시스템 | 116 | 368 | **0** | 112 |

panel 크기(480)와 viewport(368)는 탭이 바뀌어도 그대로다.

### 9.3 신규 test 2 case

- `Each settings tab gets its own scroll extent` — 창 520에서 외양 탭만 막대를 갖고
  나머지 셋은 한계 0에 막대도 없으며 휠이 아무 일도 하지 않는다. panel 크기와
  viewport는 네 탭에서 같다.
- `Switching tabs recalculates the scroll limit at that moment` — 긴 탭에서 끝까지
  내린 뒤 짧은 탭으로 옮기면 0이 되고 더 굴러가지 않으며, 돌아오면 그 탭의 한계까지
  다시 내려간다.

test를 쓰면서 `build_ui_tree(...)->find(...)` 결과를 변수에 담아 다음 문장에서 쓰는
**dangling pointer**를 하나 만들었다가 잡았다 — tree가 그 문장 끝에서 사라진다.
기존 test에는 같은 형태가 없다.

### 9.4 재검증

| 항목 | 결과 |
| --- | --- |
| `ctest` Debug / Release | **822/822 통과** (양쪽) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| 앱 빌드 Debug / Release | 성공 |
| `check_source_style.ps1` | 466 files 통과 |

---

## 10. 후속 - 항목 블록 안쪽 여백 (2026-08-23 추가 지시)

### 10.1 지시와 원인

"`문서 설정`을 렌더링할 때 배경에 옅은 키 컬러를 깔잖아. 그 때 옵션들이 여백 없이
빡빡하게 배치되어있는 게 티가 나. 버튼이나 `문서 설정` 배지 등 바깥쪽과 닿는 UI들에
여백을 좀 줘."

문서 모드에서 덮어쓴 항목은 블록에 옅은 키 컬러를 깐다(`accent_soft` 0.10). 그런데
배치가 이랬다.

| UI | 자리 | 결과 |
| --- | --- | --- |
| 배지 | `block.x`, `block.y` | 블록 **왼쪽 위 모서리에 정확히** 붙음 |
| 제목·값 글 | `block.x` | 왼쪽 끝에 붙음 |
| 행 버튼·토글 | `controls_right = block 오른쪽 끝` | 오른쪽 끝에 붙음 |

즉 위·왼쪽·오른쪽 여백이 **정확히 0**이었다.

### 10.2 고친 내용

항목 블록에 안쪽 여백을 뒀다. 강조 바탕이 깔리는 `block` 사각형은 그대로 두고, 글과
컨트롤만 안으로 들여 앉힌다.

```cpp
constexpr float item_top_margin { 6.0f };      // 블록 위쪽 여백
constexpr float item_bottom_margin { 10.0f };  // 8 → 10 (블록 아래쪽 여백 + 블록 간격)
constexpr float item_inner_padding { 8.0f };   // 블록 좌우 여백
constexpr float item_block_gap { 4.0f };       // 강조 블록끼리 붙지 않게 덜 칠하는 높이
```

여백 = 위 6 · 아래 6(`bottom_margin` − `block_gap`) · 좌우 8, 블록 사이 간격 4다.

**모든 항목에 같은 값을 준다.** 덮어쓴 항목만 들여쓰면 행끼리 좌우가 어긋난다.
문서 모드가 아니어도 항목 간 간격이 조금 넉넉해진다.

색 격자가 겹치지 않도록 `item_content_width()`를 두어 **높이 계산과 배치가 같은 폭**을
쓰게 했다 — 폭이 좁아지면 색 동그라미가 한 줄 더 늘 수 있다.

### 10.3 대가

항목 하나가 8 높아진다(위 6 + 아래 2). 창 520 기준 탭 내용 높이는
214 → 238 · 242 → 266 · 448 → 480 · 108 → 116이 되었다. 그만큼 스크롤이 조금 일찍
시작된다. 조정은 위 네 상수로 한다.

### 10.4 신규 test 1 case

- `An overridden item keeps its badge and controls off the highlight edges` — 배지
  왼쪽이 블록 왼쪽보다 안쪽이고, 버튼 오른쪽이 블록 오른쪽보다 안쪽이다. 전에는 둘 다
  정확히 같았다.

기존 `Each settings tab gets its own scroll extent`·`Switching tabs recalculates…`는
창 높이를 420 → 520으로 올렸다. 항목이 높아지면서 `동작` 탭(266)이 viewport(268)에
2 차이로 겨우 들어와 test가 아슬아슬해졌기 때문이다 — 여유 있는 높이로 옮겨 앞으로의
치수 조정에 흔들리지 않게 했다.

### 10.5 재검증

| 항목 | 결과 |
| --- | --- |
| `ctest` Debug / Release | **823/823 통과** (양쪽) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| 앱 빌드 Debug / Release | 성공 |
| `check_source_style.ps1` | 466 files 통과 |
