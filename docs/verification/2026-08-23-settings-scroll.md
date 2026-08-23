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
