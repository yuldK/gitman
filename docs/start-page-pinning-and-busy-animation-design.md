# 시작 페이지 고정과 진행 표시 회전 — 설계 초안

2026-08-23 지시. **2026-08-23 검수 승인 후 구현 완료.** 검수에서 정해진 것은 아래
`검수 결정`에 적었고, 초안과 달라진 부분은 그 자리에 표시했다.

| 지점 | 지시 |
|---|---|
| P1 | 시작 화면 최근 항목 왼쪽에 pin 버튼을 두어 고정할 수 있게 한다 |
| P2 | 같은 행 오른쪽 끝에 `$(close)` 삭제 버튼을 둔다 |
| P3 | SVN 첫 조회의 로딩 표시가 멈춰 있다. 회전 등으로 살아 있음을 보인다 |

---

## P2. 삭제 버튼 — 이미 있다

`recent_remove_element`가 이미 행 오른쪽 끝에 `codicons::icon_close`를 그리고
`remove_recent_document_intent`를 낸다 (`start_page_element.cpp:92`). 평소 alpha
0.45, hover에서 1.0이다.

**초안은 여기서 틀렸다.** 만들어져 있기는 했지만 **한 번도 그려지지 않았다** —
`recent_item_element::draw`가 `draw_children`을 부르지 않았다. 폴더 이름을 그릴 자리가
없을 때 `return`하는 가지가 있어 더 눈에 띄지 않았다. hover 배경만 보이고 아이콘이
없던 것은 이 때문이다.

고친 내용은 둘이다.

- `draw_children(context, interaction)`을 draw 끝에서 반드시 부른다. 폴더를 못 그리는
  경우에도 자식은 그린다.
- 아이콘 색을 **글과 같게** 한다 (검수 결정 1). alpha 0.45로 흐리게 그리던 것을
  그만둔다 — 아이콘이 없는 것으로 읽힌다.

---

## P1. 최근 항목 고정

### P1.1 도메인

```cpp
struct recent_document
{
    std::u8string path {};
    std::u8string display_name {};
    std::u8string opened_at {};
    // 사용자가 고정한 항목이다. 상한에 걸려도 버리지 않고 목록 앞에 모인다.
    bool pinned { false };
};
```

**순서**: 고정 항목이 항상 앞이고, 각 무리 안에서는 최근에 연 것이 먼저다. 지금의
"맨 앞으로 올린다"는 그 무리 안에서의 맨 앞이 된다.

**상한**: 지금 `recent_document_capacity`(10)는 목록 전체의 상한이다. 고정 항목까지
세면 고정을 10개 하는 순간 새로 연 문서가 목록에 남지 않는다. 그래서 **상한은
비고정 항목에만** 적용한다. 고정은 사용자가 명시적으로 고른 것이라 버리지 않는다.
총 개수는 `고정 수 + 10`이다.

`touch_recent_document`는 기존 항목의 `pinned`를 **보존**한다 — 고정한 문서를 다시
열었다고 고정이 풀리면 놀랍다.

### P1.2 저장

`gitman.app-settings.json`의 `recent_documents` 항목에 `"pinned": true`를 **참일
때만** 기록한다. 거짓이면 키를 생략해 기존 파일과 diff가 나지 않는다. 읽을 때 키가
없거나 boolean이 아니면 거짓이다. `schema_version`은 올리지 않는다 — 옛 파일이 그대로
읽히고 새 파일도 옛 버전이 무시하고 읽을 수 있다.

### P1.3 UI

고정한 항목은 **`고정됨` 무리로 따로 모아** 보여 준다 (2026-08-23 후속 지시).
목록 자체가 이미 고정을 앞에 모으고 있으므로, 표시는 그 경계에 머리글을 하나 더
넣는 것으로 끝난다.

```
┌ 고정됨 ─────────────────────────────────────────┐
│ 📌  work          C:\work                    ✕ │
├ 최근 항목 ──────────────────────────────────────┤
│ 📍  other         C:\other                   ✕ │
└─────────────────────────────────────────────────┘
```

- `고정됨` 머리글은 고정한 항목이 하나라도 있을 때만 그린다.
- `최근 항목` 머리글은 **언제나** 있다 — 목록이 비었다는 안내가 붙을 자리이기도 하다.
  다 고정해서 최근 무리가 비면 그 아래에 `최근에 연 문서가 없습니다.`가 붙는다.
- 머리글도 높이를 먹으므로 창이 낮을 때 들어가는 행 수가 한 줄 줄어든다. 넘치는
  개수를 마지막 줄로 알리는 규칙은 그대로다.

- 행 왼쪽 20px에 pin 버튼, 오른쪽 20px에 기존 삭제 버튼. 글은 그만큼 안으로 들어온다.
- 고정이면 `icon_pinned`, 아니면 `icon_pin`이다. 두 아이콘 모두 **글과 같은 색**으로
  그린다 (검수 결정 1). 행 강조는 hover 배경이 맡는다.
- tooltip: 고정이면 `고정 해제`, 아니면 `목록에 고정`.
- 두 버튼 모두 행보다 먼저 hit되어 문서를 열지 않는다 (기존 `recent_remove_element`와
  같다).

### P1.4 메시지와 view

- 신규 intent `toggle_recent_document_pin_intent { std::u8string path }`.
- `logic_controller`가 항목을 찾아 `pinned`를 뒤집고 순서를 다시 세운 뒤 앱 설정을
  저장한다 (`request_app_settings_save`).
- `recent_document_view`에 `bool pinned` 추가.
- `ui_element_kind`에 `start_page_recent_pin` 추가.

### P1.5 영향 범위

| 파일 | 변경 |
|---|---|
| `domain/app_settings.{h,cpp}` | `pinned` 필드, 순서·상한 규칙, `set_recent_document_pinned` |
| `infrastructure/json_app_settings_store.cpp` | 읽기·쓰기 한 키 |
| `application/app_messages.h` | intent 1개, variant |
| `application/logic_controller.{h,cpp}` | handler 1개, view 채우기 |
| `presentation/view_snapshot.h` | `recent_document_view.pinned` |
| `presentation/ui/ui_element.h` | kind 1개 |
| `presentation/ui/start_page_element.{h,cpp}` | pin element, 행 배치 |

test: 도메인 순서·상한 4 case, JSON 왕복 1 case, logic handler 1 case, tree 빌드 1 case.

---

## P3. 진행 표시 회전

### P3.1 지금

`svn_status_row_element`가 `codicons::icon_loading`을 **정지 상태로** 그린다
(`switch_dialog_element.cpp:301`). 카드의 busy 표시(`icon_sync`)도 같다. 글리프는
회전하는 그림이 아니라 정지 화살표라 "멈췄다"로 읽힌다.

### P3.2 그리기

`draw_primitives`에 회전판을 더한다.

```cpp
// 글리프를 target 중앙을 축으로 돌려 그린다. 각도는 now에서만 나오므로 어느 frame에
// 그려도 위상이 같다.
void draw_spinning_glyph(SkCanvas&, char32_t, const rect_f& target, const SkFont&, const SkPaint&,
                         std::chrono::steady_clock::time_point now);
```

- 주기 1초, 등속 회전. `canvas.save()` → `rotate(angle, cx, cy)` → 기존
  `draw_centered_glyph` → `restore()`.
- 적용 지점: SVN 브라우저의 `조회 중…` 행, 카드의 busy 표시(`icon_sync`). 둘 다 같은
  함수를 쓴다 (검수 결정 2). 오류 행의 `icon_warning`은 정지 글리프다.

### P3.3 다시 그리기

정지 화면을 돌리려면 frame이 계속 와야 한다. 이 앱은 이벤트가 있을 때만 그리고,
caret 깜빡임·tooltip·경과 시간은 각각 `SetTimer`로 다음 시점 하나를 예약한다
(`win32_application.cpp:1381`~). 회전도 **같은 방식**으로 붙인다.

```cpp
// ui_element
[[nodiscard]] virtual bool animating() const noexcept { return false; }
// ui_tree — 평탄 색인을 훑어 하나라도 애니메이션 중이면 true다.
[[nodiscard]] bool animating() const noexcept;
```

- 회전 글리프를 그리는 element만 `animating()`을 재정의한다.
- `schedule_spinner_repaint(tree)`: `tree->animating()`이면 `SetTimer(33ms)`, 아니면
  `KillTimer`. `WM_TIMER` 분기에 id를 하나 더한다.
- 조회가 끝나면 그 행이 사라져 `animating()`이 거짓이 되고 timer가 죽는다. **회전이
  보이는 동안에만 30fps**이고 평소 비용은 0이다.

### P3.4 영향 범위

| 파일 | 변경 |
|---|---|
| `presentation/ui/draw_primitives.{h,cpp}` | `draw_spinning_glyph` |
| `presentation/ui/ui_element.h` | `animating()` 기본 구현 |
| `presentation/ui/ui_tree.{h,cpp}` | `animating()` |
| `presentation/ui/switch_dialog_element.cpp` | 로딩 행이 회전판을 쓴다 |
| `presentation/ui/card_element.cpp` | (결정 2에 따라) busy 표시 |
| `platform/win32/win32_application.cpp` | timer id 1개, scheduler 1개 |

test: `ui_tree::animating()` 2 case (회전 element 있음·없음), 회전 각도가 `now`만의
함수임을 확인하는 1 case.

---

## 검수 결정

| 결정 | 정해진 것 |
|---|---|
| 1 · 아이콘 색 | **글과 같은 색**으로 그린다. 흐리게 그리면 없는 것으로 읽힌다 |
| 2 · 회전 적용 범위 | SVN 로딩 행과 **카드 busy 표시까지** |
| 3 · 고정 상한 | 초안대로 — 상한은 비고정 10개에만 적용한다 |
| 4 · 고정 해제 자리 | **가장 최근에 연 문서로 쳐서** 비고정 무리의 맨 앞에 세운다 |

## 구현 결과

- `set_recent_document_pinned`가 두 방향 모두 새 무리의 맨 앞에 세운다 (결정 4).
  이미 그 상태면 자리도 건드리지 않고 `false`를 돌려 저장을 부르지 않는다.
- 앱 설정을 읽을 때 `stable_partition`으로 고정을 앞으로 모은다. 손으로 고친 파일이
  두 무리를 섞어 두어도 목록의 불변이 지켜진다.
- `spinner_angle`은 `steady_clock`의 절대 시각만의 함수다. 시작 시각을 들고 다니지
  않아도 되고 같은 frame의 여러 표시가 함께 돈다.
- 신규 test 5 case — 도메인 고정 순서·상한 1, JSON 왕복 1, 시작 페이지 pin 1,
  `ui_tree::animating` 1, 회전 위상 1.
