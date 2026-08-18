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

## 4. 후속 작업

- 스크롤 thumb 끌기, Page Up/Down, Home/End
- 문서 전환 시점의 배치 저장 (1.4)
- 생성 dialog를 앱 내부 overlay로 옮기는 작업 (기존 change_log의 후속 항목)
