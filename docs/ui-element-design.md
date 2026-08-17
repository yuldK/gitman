# UI element 계층 설계 초안

상태: **승인·구현 완료** (2026-08-17) — 검수 항목 4건 모두 제안안대로 확정.
tree는 logic thread가 빌드·게시, drag & drop은 API·상태 기계·시각 표시까지,
caption은 이번에 통합, `input_action`은 `ui_command`를 포함한 variant로
일반화한다. 구현 결과와 세부 조정은 8장과 `docs/change_log.md` 2026-08-17 항목을
참고한다. drag & drop의 첫 소비자(카드 순서 변경과 문서 저장)는 2026-08-18에
구현됐다 (`change_log.md` 해당 항목).

## 1. 목표와 범위

- caption, 카드, 버튼 등 모든 화면 요소가 하나의 최상위 추상 클래스 `ui_element`를
  상속해 일관된 API(배치, 그리기, hit test, 상호작용 등록)를 제공한다.
- 지금 없는 상호작용 기능을 추상 계층에서 표준으로 제공한다.
  - hover 강조 표시 (버튼·카드 공통)
  - tooltip (hover 지연 후 표시)
  - 마우스 왼쪽 클릭, 오른쪽 클릭, 더블 클릭 액션 등록
  - 활성/비활성 전환 (비활성 시 액션 차단 + 흐리게 그리기 일관 처리)
  - drag & drop 액션 등록과 재설정
- `presentation/ui/` 하위 모듈로 분리해 Win32, Skia 렌더러, 앱 조립부와
  의존성을 끊는다 (Skia canvas 타입만 그리기 계약에 사용).

비범위: 키보드 초점 이동 로직(현행 `input_controller`의 focused_ 유지),
cursor 모양 변경, 애니메이션. 필요 시 후속 단계에서 같은 계층 위에 얹는다.

## 2. 제약: ADR-004와의 정합

ADR-004는 다음을 요구한다.

- UI thread는 불변 snapshot만 읽어 그린다. input thread는 layout으로 hit test만
  한다. mutable 업무 상태는 logic thread 단독 소유다.
- "UI callback에서 업무 상태를 직접 바꾸는 public API는 제공하지 않는다."

따라서 고전적인 retained-mode 위젯(위젯이 상태를 소유하고 callback이 상태를
변경)은 채택할 수 없다. 대신 **snapshot마다 다시 빌드되는 불변 element tree**를
채택한다.

- `build_ui_tree(view_snapshot) -> shared_ptr<const ui_tree>`는 순수 함수다.
  같은 snapshot이면 어느 스레드에서 빌드해도 같은 tree가 나온다
  (현행 `compute_layout`과 같은 성질을 tree로 일반화한 것).
- tree는 빌드(조립) 중에만 mutable이다. `set_action`, `set_enabled`,
  `set_tooltip` 등은 빌드 함수 안에서 호출하고, 게시(publish) 후에는 불변이다.
- "재설정 가능"은 다음 snapshot의 tree 빌드에서 다른 액션을 등록하는 것으로
  달성한다. 상태에 따라 매 프레임 다른 액션·비활성화를 선언적으로 구성할 수 있다.
- 등록하는 액션은 상태를 직접 바꾸지 않는다. 액션은
  `logic_message` 또는 UI 명령(`show_open_document_dialog`, 창 최소화 등)을
  **반환**하고, 실행 주체(logic thread, UI thread)가 이를 소비한다.
  기존 `input_action` variant를 확장한 것이다.

## 3. 전체 데이터 흐름

```text
logic_thread:  view_snapshot ──build_ui_tree()──> ui_tree (불변, shared_ptr)
                     │                               │
                     ├── latest_slot ──> input_thread: hit test + 상호작용 해석
                     │                               │
                     │                               ├─ ui_action 실행 → logic_message → logic
                     │                               └─ interaction_snapshot 게시 ─┐
                     │                                                             │
                     └── latest_slot ──> ui_thread: tree.draw(canvas, interaction_snapshot)
```

- `ui_tree`는 현행 `layout_snapshot`을 대체한다 (hit 영역 + 그리기 + 액션을
  하나의 구조가 담당).
- `interaction_snapshot`은 새로 추가되는 **입력 정규화 상태의 불변 발행본**이다.
  hover 대상, 눌린 대상, tooltip 후보, drag 진행 상태를 담는다. ADR-004에서
  input thread가 소유하기로 한 "입력 정규화 상태"의 일부이며 업무 상태가 아니다.
- UI thread는 tree + interaction_snapshot만으로 hover 강조와 tooltip을 그린다.

## 4. 클래스 계층

```text
ui_element (abstract)                     ── 최상위 추상 클래스
├── ui_container                          ── 자식 소유 + 배치 위임
│   ├── caption_element                   ── caption 막대 (아이콘, 제목, 창 버튼 3개 포함)
│   ├── toolbar_element                   ── 문서 경로 + 전체 새로 고침 + 문서 열기
│   ├── card_element                      ── 카드 1장 (상태 아이콘, 텍스트, 버튼 3개 포함)
│   └── card_list_element                 ── 스크롤 영역, 화면에 걸친 card_element만 생성
├── button_element                        ── 아이콘 버튼: hover 강조, tooltip, 클릭, 활성/비활성
├── label_element                         ── 텍스트 (제목, 경로, 빈 상태 안내, 진단)
└── (후속) 필요 시 checkbox, text_input 등

ui_tree                                   ── root 컨테이너 + element id 색인 + 순회/hit test 진입점
interaction_controller                    ── input thread 소유. 클릭·더블클릭·drag 상태 기계
```

- 창 버튼(minimize/maximize/close)도 `button_element`다. caption 전용 특수
  구현을 없애 "캡션 UI부터 버튼 UI까지 일관 API" 목표를 달성한다.
- `card_element`의 refresh/update/switch 버튼도 같은 `button_element`다.
  단계 7의 비활성 버튼은 `set_enabled(false)` + tooltip("단계 7에서 활성화")으로
  표현하고, 활성화 시점에는 빌드 함수에서 액션만 등록하면 된다.

## 5. 최상위 추상 클래스 API 초안

```cpp
namespace gitman::ui {
    // tree 안에서 element를 안정적으로 식별한다. 종류 + 소속 project로 구성해
    // snapshot이 바뀌어도 같은 대상(예: 특정 카드의 refresh 버튼)을 가리킨다.
    // hover·pressed·tooltip이 rebuild를 건너 이어지는 근거다.
    struct ui_element_id
    {
        ui_element_kind kind { ui_element_kind::none };
        project_id owner {};

        [[nodiscard]] bool operator==(const ui_element_id&) const = default;
    };

    enum class ui_trigger
    {
        left_click,
        right_click,
        double_click,
    };

    // 액션 실행 시점의 문맥이다. 좌표는 논리 픽셀, element는 대상 id다.
    struct ui_action_context
    {
        ui_element_id element {};
        float x { 0.0f };
        float y { 0.0f };
        bool control { false };
    };

    // 액션은 상태를 바꾸지 않고 후속 메시지를 반환한다 (ADR-004).
    // input_action = std::variant<std::monostate, logic_message, ui_command>
    using ui_action = std::function<std::vector<input_action>(const ui_action_context&)>;

    // drag & drop: 출발지는 payload를 만들고, 도착지는 수락 판정과 drop 액션을 가진다.
    struct drag_payload
    {
        ui_element_id source {};
        project_id dragged_project {};
    };

    struct drag_source
    {
        std::function<drag_payload(const ui_action_context&)> make_payload {};
    };

    struct drop_target
    {
        std::function<bool(const drag_payload&)> accepts {};
        std::function<std::vector<input_action>(const drag_payload&, const ui_action_context&)> on_drop {};
    };

    // 모든 화면 요소의 최상위 추상 클래스다. 빌드 중에만 mutable이고 게시 후에는
    // 불변으로 취급한다. 여러 스레드가 동시에 읽어도 안전한 이유다.
    class ui_element
    {
    public:
        explicit ui_element(ui_element_id id) noexcept;
        virtual ~ui_element() = default;
        ui_element(const ui_element&) = delete;
        ui_element& operator=(const ui_element&) = delete;

        // -- 공통 조회 --
        [[nodiscard]] ui_element_id id() const noexcept;
        [[nodiscard]] const rect_f& bounds() const noexcept;
        [[nodiscard]] bool enabled() const noexcept;
        [[nodiscard]] bool visible() const noexcept;
        [[nodiscard]] const std::u8string& tooltip() const noexcept;
        [[nodiscard]] const ui_action* action(ui_trigger trigger) const noexcept;
        [[nodiscard]] const drag_source* drag() const noexcept;
        [[nodiscard]] const drop_target* drop() const noexcept;

        // -- 빌드 시 구성 (게시 후 호출 금지) --
        void set_bounds(const rect_f& bounds) noexcept;
        void set_enabled(bool value) noexcept;
        void set_visible(bool value) noexcept;
        void set_tooltip(std::u8string text);
        void set_action(ui_trigger trigger, ui_action action);
        void clear_action(ui_trigger trigger) noexcept;
        void set_drag_source(std::optional<drag_source> source);
        void set_drop_target(std::optional<drop_target> target);

        // -- 파생 클래스 계약 --
        // 부모가 준 영역 안에서 자기 bounds와 자식 배치를 확정한다.
        virtual void arrange(const arrange_context& context) = 0;
        // 자신과 자식을 그린다. hover 강조·눌림 표시는 interaction으로 판정한다.
        virtual void draw(draw_context& context, const interaction_snapshot& interaction) const = 0;
        // 좌표를 포함하는 가장 안쪽 element를 돌려준다. 기본 구현은 bounds 검사
        // 후 자식을 역순(위에 그려진 것 먼저)으로 탐색한다. 비활성 element도
        // hit는 되므로(tooltip 표시용) 액션 실행 단계에서만 enabled를 검사한다.
        [[nodiscard]] virtual const ui_element* hit_test(float x, float y) const;

    protected:
        // 파생 컨테이너가 자식을 소유한다. 순회는 기본 구현이 제공한다.
        void add_child(std::unique_ptr<ui_element> child);
        [[nodiscard]] std::span<const std::unique_ptr<ui_element>> children() const noexcept;

    private:
        ui_element_id id_ {};
        rect_f bounds_ {};
        bool enabled_ { true };
        bool visible_ { true };
        std::u8string tooltip_ {};
        std::array<ui_action, 3> actions_ {};
        std::optional<drag_source> drag_source_ {};
        std::optional<drop_target> drop_target_ {};
        std::vector<std::unique_ptr<ui_element>> children_ {};
    };
}
```

보조 계약:

```cpp
    // 배치 문맥: 부모가 준 영역과 DPI 배율, 스크롤 값.
    struct arrange_context
    {
        rect_f slot {};
        float scale { 1.0f };
        float scroll_offset { 0.0f };
    };

    // 그리기 문맥: canvas와 공유 자원. draw 호출 동안만 유효하다.
    struct draw_context
    {
        SkCanvas& canvas;
        SkTypeface* codicon_typeface { nullptr };
        SkTypeface* ui_typeface { nullptr };
        const ui_color_palette& palette;
        float scale { 1.0f };
    };

    // input thread가 게시하고 UI thread가 읽는 상호작용 발행본이다.
    struct interaction_snapshot
    {
        ui_element_id hovered {};
        ui_element_id pressed {};
        // hover가 시작된 시각. tooltip 표시 여부(지연 경과)는 그리는 쪽이 판정한다.
        std::optional<std::chrono::steady_clock::time_point> hover_started_at {};
        std::optional<drag_visual> drag {};
    };

    struct drag_visual
    {
        drag_payload payload {};
        float x { 0.0f };
        float y { 0.0f };
        // 현재 위치의 drop 대상이 수락하는지. 수락 표시(강조 테두리)에 쓴다.
        ui_element_id hovered_drop_target {};
    };
```

`ui_tree`:

```cpp
    class ui_tree
    {
    public:
        explicit ui_tree(std::unique_ptr<ui_element> root);

        [[nodiscard]] const ui_element& root() const noexcept;
        [[nodiscard]] const ui_element* hit_test(float x, float y) const;
        [[nodiscard]] const ui_element* find(ui_element_id id) const noexcept;
        [[nodiscard]] float content_height() const noexcept;

        void draw(draw_context& context, const interaction_snapshot& interaction) const;

    private:
        std::unique_ptr<ui_element> root_ {};
        // id → element 색인. interaction_snapshot의 id를 빠르게 되찾는다.
        std::vector<std::pair<ui_element_id, const ui_element*>> index_ {};
    };

    // 순수 빌드 함수: 같은 view_snapshot이면 같은 tree다. 액션 등록도 여기서 한다.
    [[nodiscard]] std::shared_ptr<const ui_tree> build_ui_tree(const view_snapshot& view);
```

## 6. 상호작용 모델

### 6.1 interaction_controller (input thread 소유)

현행 `input_controller`의 hit test 역할을 흡수·확장한다. raw input과 최신
`ui_tree`를 받아 다음 상태 기계를 돌린다.

| 입력 | 처리 |
| --- | --- |
| pointer_moved | hover 대상 갱신. 바뀌면 `interaction_snapshot` 게시 + UI wake. drag 중이면 drag 위치·drop 대상 갱신 |
| pointer_pressed | pressed 대상 기록. 시각·좌표 기록(더블 클릭 판정용) |
| pointer_released | 같은 element 위에서 떼면 클릭 확정. 직전 클릭과 시간·거리 임계값 이내면 `double_click`, 아니면 버튼에 따라 `left_click`/`right_click`. drag 중이면 drop 판정 |
| mouse_wheel | 현행대로 `scroll_intent`를 logic으로 (element 경유 없음) |
| key_pressed | 현행 `input_controller`의 키보드 경로 유지 |

- 클릭 확정 시 대상 element의 `action(trigger)`를 실행한다. `enabled() == false`
  이면 실행하지 않는다. 반환된 `input_action` 목록을 현행 규칙대로 배분한다
  (`logic_message` → logic 채널, UI 명령 → UI thread).
- **더블 클릭 판정**: `pointer_pressed_event`에 timestamp를 추가한다
  (UI thread가 게시 시점에 기록). 임계값은 `GetDoubleClickTime()`을 조립 시
  주입해 test에서는 fake 값을 쓴다. 더블 클릭이 등록되지 않은 element는
  두 번의 left_click으로 처리한다(지연 없는 단순 규칙).
- **drag 시작**: pressed 상태에서 임계 거리(예: 논리 6px) 이상 이동하고 대상에
  `drag_source`가 있으면 drag로 전환하고 클릭은 취소한다. escape 또는
  수락 대상 밖에서 release하면 취소한다.

### 6.2 hover 강조와 tooltip (UI thread에서 그리기)

- `button_element::draw`는 `interaction.hovered == id()`이면 hover 배경을,
  `pressed == id()`이면 눌림 배경을 그린다. 비활성이면 강조 없이 흐리게 그린다.
  색은 `ui_color_palette`에 hover/pressed 색을 추가해 쓴다.
- tooltip: `hover_started_at`에서 지연(기본 500ms) 경과했고 대상 element에
  tooltip 텍스트가 있으면 `ui_tree::draw` 마지막에 최상위로 그린다.
  경과 판정은 그리는 쪽(UI thread)이 하고, 지연 도달 시 재그리기는 UI thread가
  WM_TIMER 한 번으로 깨운다. input thread에는 timer가 필요 없다.
- caption의 기존 hover(`caption_button_hover`)는 이 경로로 통합한다.
  비클라이언트 메시지(WM_NCMOUSEMOVE 등)는 UI thread가 client 좌표로 변환해
  `pointer_moved_event`로 input thread에 넘긴다.

### 6.3 활성/비활성

- `set_enabled(false)`의 효과를 기반 클래스에서 일관 보장한다:
  액션 미실행, drag 시작 불가, hover 강조 없음, 파생 draw에 `enabled()` 제공.
  tooltip은 비활성 사유 안내를 위해 계속 표시한다.

### 6.4 caption과 창 명령

- `caption_element`는 제목·아이콘 label과 창 버튼 3개(`button_element`)를 담고,
  각 버튼의 left_click에 `ui_command`(minimize/toggle_maximize/close)를 등록한다.
- `input_action`을 `std::variant<std::monostate, logic_message, ui_command>`로
  일반화하고, `ui_command`는 `std::variant<show_open_document_dialog,
  window_minimize, window_toggle_maximize, window_close>`류의 창 명령 집합이다.
  UI thread가 자기 채널에서 받아 실행한다.
- `WM_NCHITTEST`(창 끌기 영역, Windows 11 snap flyout)는 동기 응답이 필요하므로
  현행 `caption_layout` 순수 함수 경로를 유지한다. tree와 값이 어긋나지 않도록
  `caption_element::arrange`가 같은 상수(`caption_ui_metrics`)를 쓰고 test로
  두 계산의 일치를 고정한다.

## 7. 스레드 소유권 정리 (ADR-004 대비 변경점)

| 자원 | 소유 | 비고 |
| --- | --- | --- |
| `ui_tree` (불변) | logic thread가 빌드·게시 | `layout_snapshot` slot을 대체. UI·input thread는 `shared_ptr<const>`로 읽기만 |
| `interaction_snapshot` (불변) | input thread가 게시 | 새 `latest_slot`. UI thread가 그리기에 사용 |
| 클릭·drag 상태 기계 | input thread | ADR-004의 "입력 정규화 상태" 확장 |
| tooltip 지연 판정, WM_TIMER | UI thread | 프레임 게시 권한이 UI thread에 있으므로 |
| 업무 상태 | logic thread | 변경 없음. 액션은 메시지 반환만 한다 |

view snapshot 게시 경로에 tree 빌드가 추가된다:
`publish_snapshots`가 `compute_layout` 대신 `build_ui_tree`를 호출한다.
스크롤·창 크기가 view snapshot에 이미 포함되므로 게시 주기는 그대로다.

## 8. 모듈 구조와 파일 배치

```text
src/presentation/ui/           # CMake target: gitman_ui
    ui_events.h                # raw input 이벤트(+timestamp), ui_command, input_action
    ui_element.h/.cpp          # 추상 클래스, ui_element_id, trigger/action, arrange/draw 문맥
    ui_tree.h/.cpp             # tree, hit test, index, draw + tooltip·drag 오버레이
    ui_interaction.h/.cpp      # interaction_snapshot, interaction_controller, run_ui_input_pump
    draw_primitives.h/.cpp     # element 공용 Skia 그리기 조각
    button_element.h/.cpp
    label_element.h/.cpp
    caption_metrics.h          # 의존성 없는 caption 치수. Win32 caption_layout과 공유
    caption_element.h/.cpp     # make_caption_tree(smoke 화면용 단독 tree) 포함
    toolbar_element.h/.cpp
    card_element.h/.cpp
    card_list_element.h/.cpp
    build_ui_tree.h/.cpp       # view_snapshot → tree 순수 빌드 (액션 등록 포함)
```

- `gitman_ui`는 gitman_app(view snapshot·intent), gitman_core(theme),
  gitman_messaging(입력 pump), Skia에 의존한다. Win32에는 의존하지 않는다.
  구현 중 조정: input pump가 이 모듈에 있으므로 messaging 의존은 허용했고,
  `ui_command`는 창 메시지로 나르기 쉽도록 variant 대신 enum으로 확정했다.
- 기존 파일 처리: `caption_ui`, `card_list_view`, `layout_model`,
  `input_controller`는 새 모듈로 대체 후 제거했다. 스크롤 상수·clamp 함수는
  logic도 쓰므로 `presentation/list_metrics.*`(gitman_app)로 이동했다.

## 9. 마이그레이션 계획 (각 단계마다 빌드·테스트 green 유지)

1. **core**: `ui_element`/`ui_tree`/문맥 타입 + 단위 테스트 (배치, hit test,
   액션 등록·비활성 규칙).
2. **interaction**: `interaction_controller` + fake clock 테스트 (클릭, 더블
   클릭, hover, drag 상태 기계). `input_action` variant 일반화.
3. **toolbar·card 이식**: `build_ui_tree`로 현행 화면 재구성,
   `draw_card_list`·`compute_layout`·`input_controller` 마우스 경로 대체.
   hover 강조·tooltip이 이 단계에서 켜진다.
4. **caption 이식**: `caption_element` + 창 명령 `ui_command` 경로,
   NC hit test 일치 테스트. `caption_ui` 제거.
5. **drag & drop**: 상태 기계 마무리 + 첫 소비자(카드 순서 변경 여부는 검수
   질문 참고) + drag visual 그리기.
6. **정리**: 잔여 legacy 제거, `docs/change_log.md`·검증 문서 갱신, ADR 추가
   (본 문서를 승인 내용으로 확정).

## 10. 테스트 전략

- element 단위: arrange 결과 bounds, hit test 우선순위(위에 그린 것 먼저),
  enabled=false 시 액션 미실행을 canvas 없이 검증한다.
- interaction: fake clock으로 더블 클릭 임계, tooltip 지연, drag 임계 거리를
  결정적으로 검증한다 (`task_scheduler_tests`와 같은 방식).
- 일관성: `build_ui_tree`가 같은 snapshot에서 같은 hit 영역을 내는지, caption
  NC hit test와 `caption_element` bounds가 일치하는지 고정한다.
- 조립: 기존 `app_runtime` heartbeat·카드 폭풍 테스트가 새 경로에서 통과해야
  한다.

## 11. 검수 요청 항목 (열린 질문)

1. **tree 빌드 위치**: logic thread가 빌드해 양쪽에 게시(제안)하는 방식과,
   현행처럼 각 스레드가 snapshot에서 재계산하는 방식 중 선택.
   제안안은 빌드 1회·항상 일치가 장점이고, 프레임마다 최신 창 크기를 반영하는
   재계산 방식보다 게시 지연이 한 단계 있다(현행 input thread와 동일한 수준).
2. **drag & drop 첫 소비자**: 카드 순서 변경(문서의 프로젝트 순서 변경, logic
   명령 추가 필요)까지 이번에 넣을지, 이번에는 API·상태 기계·시각 표시까지만
   구현하고 소비자는 후속 단계로 미룰지.
3. **caption 통합 범위**: 위 4단계(caption까지 `ui_element`로 통합)를 이번에
   진행할지, caption은 현행 유지하고 클라이언트 영역만 먼저 통합할지.
4. **`input_action` variant 일반화**: `ui_command` 도입으로 창 명령이 input
   thread를 경유할 수 있게 되는 구조 변경에 대한 승인.
