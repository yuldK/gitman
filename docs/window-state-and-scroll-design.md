# 창 배치 저장 · 생성 위치 선택 · 스크롤 UI 설계

상태: **검수 대기** (2026-08-18 작성, 구현 완료 후 검수 요청)

이 문서는 2026-08-18 사용자 지시 3건의 설계와 구현 결정을 기록한다.

1. `.version-list`에 마지막 창 위치·크기를 저장하고 열 때 복원, 종료 이벤트에서 갱신
2. `.version-list` 생성 시 저장 위치 선택 (기존처럼 스캔 폴더에 만드는 선택지도 제공)
3. 카드 스크롤 시 상단 배너(notice)가 layout에 반영되지 않는 문제와 스크롤 UI 전반 점검

## 1. 창 배치 저장과 복원

### 1.1 문서 스키마

`.version-list`의 top-level에 optional `window` object를 추가한다. `settings`와 같은
규칙으로 **schema version은 올리지 않는다**. 이 필드를 모르는 이전 Gitman이 열어도
알 수 없는 top-level 필드로 보존되고, 이 필드가 없는 문서도 그대로 열린다.

```json
{
    "schema_version": 1,
    "window": {
        "x": 320,
        "y": 180,
        "width": 1280,
        "height": 720,
        "maximized": false
    },
    "projects": []
}
```

- 좌표는 `WINDOWPLACEMENT::rcNormalPosition`의 값 그대로다 (물리 픽셀, 작업 영역
  기준). `GetWindowPlacement`/`SetWindowPlacement`가 같은 좌표계를 쓰므로 왕복이
  일관된다. 최대화 상태에서도 `x/y/width/height`는 **복원 크기**이고 최대화 여부는
  `maximized`가 따로 담는다.
- 값이 깨졌거나(`width <= 0` 등) 타입이 다르면 **경고**(`invalid_window_placement`)만
  남기고 무시한다. 창 상태 때문에 문서 열기가 실패하면 안 된다.
- 저장 시 문서에 `window`가 없고 새 값도 없으면 필드를 만들지 않는다. 문서에 이미
  있던 `window`는 값이 없더라도 지우지 않는다(사용자 데이터 보존).

도메인은 `workspace_document::window`를 `std::optional<window_placement>`로 갖는다.

### 1.2 복원 (문서 열기)

`view_snapshot`에 다음 두 필드를 추가한다.

```cpp
std::optional<window_placement> window_placement_request {};
std::uint64_t window_placement_revision { 0 };
```

logic은 문서를 채택(`install_document`)할 때 배치가 있으면 revision을 1 올린다. UI
thread는 snapshot wake에서 **revision이 바뀐 경우에만** 한 번 `SetWindowPlacement`를
호출한다. snapshot마다 창을 다시 배치하는 사고를 revision 비교가 막는다.

적용 전 UI thread가 두 가지를 방어한다.

- 최소 크기(물리 240 × 160) 미만이면 적용하지 않는다.
- 저장된 사각형이 어떤 모니터와도 겹치지 않으면(모니터 구성이 바뀐 경우) 위치는
  버리고 크기만 현재 창 위치에 적용한다.

앱 시작 시 창은 기본 크기로 먼저 보이고, 문서를 읽은 뒤 배치가 적용된다. 명령줄로
문서를 받은 경우 짧은 재배치가 한 번 보인다(허용, 2.3 후속 항목).

### 1.3 갱신 (종료 이벤트)

`WM_CLOSE`에서 UI thread가 `GetWindowPlacement`로 현재 배치를 읽어
`window_placement_intent`를 logic inbox에 넣고, 그 뒤에 `app_runtime::shutdown()`을
호출한다. 같은 스레드에서 같은 FIFO 채널에 넣으므로 배치 intent가 close intent보다
반드시 먼저 처리된다.

logic은 배치가 현재 문서의 값과 다르면 `document_->window`를 갱신하고 dirty로
표시한다. `close_intent` 처리(`begin_shutdown`)에서 dirty면 **종료 중에도** 저장
요청을 한 번 내보낸다. 이 요청만 취소되지 않은 token을 쓴다(저장 자체는 token을
보지 않지만 의도를 코드에 남긴다).

ADR-005 7.3의 종료 순서에 **1.5단계(logic의 close 처리 확인)** 를 끼운다.

1. `window_placement_intent` 게시 (UI thread, `WM_CLOSE`)
2. `close_intent` 게시
3. **logic이 close를 처리할 때까지 대기** (최대 3초, 1 ms 폴링)
   → 이 시점에는 종료 저장 요청이 이미 worker lane 0의 inbox에 들어가 있다
4. `scheduler->shutdown()`: worker inbox를 닫고 join한다. 채널은 닫은 뒤에도 남은
   메시지를 소비하므로 종료 저장은 join 안에서 끝까지 실행된다
5. 기존 순서대로 logic inbox close → join → input close → join → slot close

대기가 3초를 넘으면(logic이 멈춘 비정상 상황) 저장을 포기하고 기존 순서로 종료한다.
종료가 사용자 눈에 매달리는 것보다 배치 저장을 잃는 편이 낫다.

### 1.4 범위 밖으로 둔 것

- 문서 A를 열어 둔 채 문서 B를 열면 A의 배치는 갱신되지 않는다. 배치는 "닫을 때
  열려 있던 문서"에만 기록한다.
- 이동·크기 변경 중(`WM_EXITSIZEMOVE`)에는 저장하지 않는다. 종료 시 1회 저장이다.

## 2. 생성 dialog의 저장 위치 선택

`.version-list 만들기` 팝업에 행 하나를 추가한다.

```
이름   [ my-workspace                    ]
폴더   [ D:\repos                    ] [...]      ← 저장소를 담은 스캔 폴더
위치   [x] 스캔 폴더에 만들기
       [ D:\repos                    ] [...]      ← 체크 해제 시에만 활성
```

- 체크 상자가 기본 체크이며 기존 동작(스캔 폴더에 생성)과 같다. 체크 상태에서는
  위치 입력과 찾아보기 버튼이 비활성이고 스캔 폴더 경로를 그대로 보여 준다.
- 체크를 풀면 위치를 따로 고를 수 있다. 검증은 스캔 폴더와 같다(절대 경로, 실제
  존재하는 디렉터리). 최종 문서 경로는 `위치 + 이름 (+ .version-list)`다.
- 체크 상자도 owner-draw다. 표준 컨트롤은 dark theme에서 밝게 그려진다.
- 등록되는 프로젝트 경로는 이미 절대 경로이므로 문서를 스캔 폴더 밖에 두어도
  경로 해석이 달라지지 않는다.

## 3. 스크롤 UI 점검

### 3.1 발견한 문제

| # | 문제 | 결과 |
| - | ---- | ---- |
| S1 | notice 배너와 카드 목록이 같은 y에서 시작하고, 카드가 배너보다 나중에 그려진다 | 배너가 카드에 가려 보이지 않는다 (지시 사항) |
| S2 | 카드 목록이 자기 영역으로 clip하지 않는다 | 위로 스크롤된 카드가 toolbar 위에 그려지고 그 자리에서 클릭까지 먹는다 |
| S3 | logic의 스크롤 한계 계산이 필터 이전의 전체 카드 수를 쓴다 | 필터로 카드가 줄면 빈 공간까지 스크롤된다 |
| S4 | 창 크기·필터가 바뀔 때 저장된 스크롤 값을 다시 고정하지 않는다 | 창을 키운 뒤 휠을 한참 굴려야 목록이 움직인다 |
| S5 | 키보드 상/하 이동이 tree에 있는 "보이는 카드"만 대상으로 한다 | 화면 끝 카드에서 더 내려가지 않는다 |
| S6 | 스크롤 가능 여부를 나타내는 표시가 없다 | 카드가 더 있는지 알 수 없다 |

### 3.2 반영

- **S1**: notice가 layout에 자리를 차지한다. `list_metrics`에 `layout_notice_height`와
  `compute_list_layout(window_height, scale, has_notice)`를 두고, tree 빌드·카드 목록
  element·logic이 **같은 함수**로 목록 영역을 계산한다.
- **S2**: `card_list_element::draw`가 자기 slot으로 `clipRect`하고, `hit_test`는 slot
  밖 좌표를 먼저 걸러낸다.
- **S3, S4**: logic이 필터를 통과한 카드 수로 content 높이를 계산하고, 스크롤·창
  크기·필터·정렬 변경 뒤에 항상 다시 고정(clamp)한다.
- **S5**: 카드 목록이 위아래로 한 장씩 더(overscan) element를 만들되 화면 밖 카드는
  `set_visible(false)`로 둔다. 그리기·hit test·drop 대상에서는 빠지고 키보드 순회
  대상에는 남는다. 선택이 바뀌면 logic이 그 카드를 화면 안으로 스크롤한다.
- **S6**: 내용이 화면보다 길면 오른쪽 여백에 얇은 thumb를 그린다. 표시 전용이며
  끌기는 후속 작업이다.

## 4. 좁은 창에서 글자와 UI가 겹치는 문제

원인은 글자를 그리는 모든 자리가 폭 제한 없이 `draw_text`를 부른 것이다. 창을 좁히면
카드 이름·경로·상태 줄이 오른쪽 버튼 아래로 그대로 밀고 들어간다.

두 겹으로 막는다.

1. **글자를 자른다.** `draw_primitives`에 `measure_text`, `elide_text`,
   `draw_text_within`을 추가했다. 폭이 모자라면 UTF-8 문자 경계에서 자르고 `…`를
   붙이며, `…`조차 못 들어가면 아무것도 그리지 않는다. label(문서 경로·notice·빈
   상태), caption 제목, 카드의 세 줄이 모두 이 경로를 쓴다.
2. **자리가 없으면 버튼을 숨긴다.** toolbar와 카드가 오른쪽부터 버튼을 배치하다가
   최소 글자 폭(`layout_card_minimum_text`, 논리 72px)을 지킬 수 없으면 그 버튼부터
   `set_visible(false)`로 뺀다. 숨긴 버튼은 그리기·hit test에서 함께 빠진다.

글자가 쓸 수 있는 오른쪽 한계는 배치가 정하고(`card_element::text_limit_`) 그리기가
그 값을 쓴다. 버튼이 숨겨지면 그만큼 글자 폭이 넓어진다.

## 5. 카드 경로 표시 토글

- toolbar에 `root-folder` 아이콘 토글 버튼을 추가했다. 켜면 카드 경로가 **문서가 있는
  폴더 기준 상대 경로**로 바뀐다. 켜진 상태는 버튼 배경·아이콘 강조색으로 보인다
  (`button_config::active`).
- 표시 방식은 문서 속성이다. `settings.show_relative_paths`(boolean)에 남고, 토글 시
  순서 변경과 같은 저장 경로(`request_save`)로 문서에 기록된다.
- 상대 경로 계산은 도메인의 순수 어휘 함수다 (`relative_windows_path`). filesystem을
  조회하지 않고, 대소문자를 구분하지 않으며, drive·share가 다르면 전체 경로를 그대로
  둔다. 문서보다 위 폴더는 `..`로 올라간다. 같은 위치는 `.`이다.
- 문서의 경로 필드 자체는 바뀌지 않는다. 표시만 바뀐다.

## 6. 브랜치·상태 줄의 시각 강조

카드 세 번째 줄이 `branch @ rev · 상태 · 작업 트리`를 같은 크기·같은 색으로 이어
붙여 읽히지 않았다. 조각(chip) 네 개로 나눈다.

| 조각 | 아이콘 | 색 |
| --- | --- | --- |
| 참조(브랜치/URL) | `git-branch`, SVN은 `link` | 중립 배경 12%, 기본 글자색 |
| 리비전 | `git-commit` | 중립 배경 12%, 흐린 글자색 |
| 동기화 상태 | 상태 글리프 | 상태 강조색 18% 배경 + 같은 색 글자 |
| 작업 트리 | `edit` | 경고색 18% 배경 + 경고색 글자 |

조각은 왼쪽부터 채우고, 남은 폭에 글자가 한 자도 안 들어가면 그 조각부터 그리지
않는다. 조각 안의 글자도 `elide_text`로 잘린다.

## 7. 스크롤 막대 잡기와 창 크기 정책

### 7.1 막대를 잡고 끌기

표시 전용이던 thumb를 `scrollbar_element`로 올려 클릭·끌기를 직접 받는다.

- **element 계층에 `pointer_drag_target`을 추가했다.** 카드의 drag & drop(ghost와
  drop 대상)과 달리, 누른 채 움직이는 동안 포인터 이동을 그때그때 메시지로 바꾸는
  경계다. `on_press`(누른 순간 1회)와 `on_move`(직전 위치, 현재 위치)로 이루어진다.
- interaction controller가 누름에서 대상을 기억하고, 눌린 동안의 이동을 클릭·카드
  drag 대신 그 element로 보낸다. 놓으면 클릭으로 치지 않고 끌기만 끝낸다.
- **상대 변화량만 쓴다.** 스크롤이 바뀌면 snapshot과 tree가 다시 만들어지므로 절대
  좌표 기준으로 계산하면 끌던 기준이 사라진다. `on_move`는 픽셀 변화량을
  `scrollable / (track - thumb)` 비율로 곱해 `scroll_intent`(논리 픽셀)로 보낸다.
  logic의 스크롤 처리 경로는 휠과 완전히 같다.
- thumb 밖의 track을 누르면 그 자리로 한 번 이동하고 이어서 끌린다. thumb를 누르면
  잡기만 하고 움직이지 않는다.
- hover·누름에 따라 thumb가 진해진다(28% → 45% → 62%).

### 7.2 크기 조절 테두리와 막대가 겹치는 문제

- 창 가장자리의 크기 조절 판정이 시스템 기본값(`SM_CXSIZEFRAME + SM_CXPADDEDBORDER`,
  보통 8px)이라 오른쪽 끝의 막대를 잡기 어려웠다. 판정 두께를
  `resize_border_thickness`(논리 4px)로 좁히고, 대각선 조절이 어려워지지 않도록
  모서리만 `resize_corner_thickness`(논리 10px)로 넓게 유지한다.
- 막대도 창 가장자리에서 `layout_scrollbar_right_inset`(논리 6px)만큼 안으로
  들어온다. 보이는 막대 폭은 8px이지만 클릭·끌기를 받는 hit 영역은 16px이다.
- 막대가 보이면 카드 폭을 그만큼 줄여 카드 클릭 영역과 겹치지 않게 한다.

### 7.3 최소 창 크기

`WM_GETMINMAXINFO`에서 최소 크기를 강제한다. 값은 client 영역 기준 논리 픽셀이며
`AdjustWindowRectExForDpi`로 창 크기로 바꿔 넣는다. 문서에서 복원한 배치도 이 값보다
작으면 적용하지 않는다.

```cpp
// src/platform/win32/win32_application.cpp
constexpr int minimum_window_width { 480 };
constexpr int minimum_window_height { 320 };
constexpr int resize_border_thickness { 4 };
constexpr int resize_corner_thickness { 10 };
```

네 값은 한 블록에 모아 두었으니 숫자만 바꾸면 된다.

## 8. 배너·상단 막대와 카드의 구분

카드와 도구 막대가 같은 `surface_background`라 카드가 막대 아래로 스크롤되면 경계가
사라진다. 두 가지로 나눈다.

- **그림자**: 목록이 위로 밀려 있을 때(`scroll > 0`)만 목록 위쪽 경계에서 아래로
  옅어지는 그림자를 목록 clip 안에 그린다. 셰이더 대신 알파를 제곱으로 낮춘 띠 8개를
  쌓는다(`draw_downward_shadow`). 스크롤이 막 시작된 구간에서는 그림자도 옅게
  시작해 갑자기 나타나지 않는다. 색은 theme의 `content_shadow`다.
- **배너 바탕**: notice 배너가 창 폭을 가득 채우고 `notice_background`(어두운
  적갈색)를 깐다. 경고 메시지라는 성격도 함께 드러난다. 배너 글자는 좌우 여백
  안에서 잘린다.
- 스크롤 막대는 목록 위아래에서 `layout_scrollbar_vertical_inset`(논리 6px)만큼
  띄운다.

## 9. 후속 작업

- Page Up/Down, Home/End
- 문서 전환 시점의 배치 저장 (1.4)
- 생성 dialog를 앱 내부 overlay로 옮기는 작업 (기존 change_log의 후속 항목)
- 경로 표시 토글의 세 번째 모드(문서 폴더 기준 대신 스캔 루트 기준 등)는 필요해지면
  `settings`의 문자열 값으로 넓힌다
