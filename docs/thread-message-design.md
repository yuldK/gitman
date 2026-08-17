# 범용 스레드 메시지 component 설계안 (`MSG-P0`)

## 1. 문서 상태

- 작성일: 2026-08-17
- 대상: ADR-004가 요구한 단계 6 사전 설계 검수
- 현재 상태: `MSG-P0` 설계안 제출, 사용자 검수 대기
- 관련 요구사항: REQ-015, NFR-009, NFR-014
- 상위 문서: `docs/decisions/ADR-004-threading-and-state-ownership.md`, `docs/plan.md` 3.8
- 승인 후 처리: 승인 내용을 ADR-005로 기록한 뒤에만 단계 6 구현을 시작한다

이 문서는 ADR-004의 구현 차단 조건을 해소하기 위한 설계안이다. **사용자가 이 설계를 승인하기 전에는 message queue, dispatcher, thread bridge를 구현하지 않는다.**

## 2. 설계 목표와 비목표

### 2.1 목표

- Win32, Skia, Gitman 도메인과 무관한 **독립 재사용 core**. 표준 라이브러리만 사용한다.
- `input_thread` → `logic_thread` → `ui_thread` / `worker` 사이의 단방향 메시지 전달.
- UI와 input thread를 절대 블로킹하지 않는 계약 (NFR-009).
- 단일 소유 상태(ADR-004)와 맞는 단순한 동시성 모델: 추론 가능성이 성능 최적화보다 우선한다.
- 결정적 단위 test가 가능한 API.

### 2.2 비목표

- 프로세스 간 통신, 직렬화, 네트워크. 같은 binary 안의 C++ 값 전달만 다룬다.
- lock-free 자료구조. 카드 수백 개 규모의 GUI 앱에서 mutex 기반 queue로 충분하며, 필요해지면 내부 구현만 바꾼다.
- 범용 worker pool과 job scheduling. 카드별 lane 직렬화와 동시 실행 상한은 Gitman의 `task_scheduler`(단계 6~7)가 담당하는 **응용 정책**이다. core는 전달 장치만 제공한다.

## 3. 핵심 결정 요약

| 항목 | 결정 |
| --- | --- |
| 기본 단위 | consumer 스레드 하나가 소유하는 **MPSC FIFO `channel<T>`** 와 최신값만 유지하는 **`latest_slot<T>`** 두 가지 |
| 소비자 수 | channel마다 정확히 하나. MPMC는 제공하지 않는다 |
| producer 블로킹 | 없음. `post()`는 항상 즉시 반환하며 가득 참과 닫힘을 값으로 알린다 |
| overflow | channel 생성 시 `reject_newest`(기본) 또는 `drop_oldest` 선택. 두 경우 모두 통계로 관찰 가능 |
| snapshot 전달 | `latest_slot`이 덮어쓰기로 병합한다. view/layout snapshot은 최신 값만 의미가 있다 |
| envelope | `sequence`(채널별 단조 증가)와 `enqueued_at`만. correlation, generation, schema version은 넣지 않는다 (5.2) |
| 우선순위 | 없음. 단일 inbox FIFO와 `close()`의 별도 상태로 충분하다 (6.4) |
| wake-up | `set_signal_callback`. 비어 있다가 채워질 때와 close에서 호출된다. Win32 연동은 adapter가 `PostMessageW`로 구현한다 |
| 취소·timeout | 메시지 속성이 아니다. 취소는 payload로 전달되는 단계 3 token, 최신성은 응용층 generation 검사가 담당한다 (7장) |
| namespace / target | 최상위 `messaging`, 새 static library `gitman_messaging` (`src/messaging/`). gitman의 어떤 target에도 의존하지 않는다 |

## 4. queue topology

```text
                                  ┌────────────────────────────────────────────┐
                                  │                 logic_thread               │
 ui_thread (Win32 pump)           │  소유: app_state, logic inbox               │
 ┌──────────────────┐  raw_input  │                                            │
 │ WndProc          │──channel──▶ input_thread ──┐                             │
 │                  │             (input inbox)  │ user_intent                 │
 │ view_snapshot    │                            ▼                             │
 │   latest_slot ◀──┼──────────── logic inbox: channel<logic_message>          │
 └──────────────────┘             logic_message = variant<user_intent,         │
        ▲                                          operation_event,            │
        │ signal(PostMessageW)                     control_message>            │
        │                         ▲                        │ operation_request │
 input_thread ◀── layout_snapshot │                        ▼ (+cancel token)   │
   (latest_slot) ─────────────────┘             worker_1..N inbox              │
                                  │             (channel<operation_request>)   │
                                  │  operation_event ──▶ logic inbox           │
                                  └────────────────────────────────────────────┘
```

- **inbox 원칙**: 소비 스레드 하나당 inbox 하나다. logic thread는 intent와 worker event를 같은 `channel<logic_message>`로 받아 도착 순서 그대로 처리한다. 여러 큐를 기다리는 select 장치가 필요 없어지고, "logic thread가 모든 입력을 순서대로 처리한다"는 ADR-004 원칙이 구조가 된다.
- **worker 배정**: `task_scheduler`(응용층)가 lane 정책에 따라 특정 worker의 inbox로 request를 넣는다. 공유 MPMC job queue를 두지 않으므로 같은 카드의 직렬화가 "같은 lane은 같은 worker inbox"로 단순해진다.
- **snapshot**: `latest_slot`은 queue가 아니다. logic이 몇 번을 게시하든 UI는 다음 렌더링에서 최신 하나만 읽는다. backpressure 문제가 구조적으로 사라진다.
- ui_thread는 condition variable로 대기할 수 없으므로(Win32 pump) signal callback이 `WM_APP` 계열 메시지를 게시한다. input/logic/worker는 `receive_wait`로 대기한다.

### 4.1 Gitman 채널 구성표

| 경로 | 종류 | payload | 용량 | overflow | 근거 |
| --- | --- | --- | --- | --- | --- |
| ui → input | `channel<raw_input_event>` | 정규화 전 입력 | 4096 | `drop_oldest` | 입력 폭주 시 가장 오래된 이동 이벤트부터 버리는 편이 최신 입력 반영에 낫다. 버려진 수는 통계로 남는다 |
| input → logic, worker → logic | `channel<logic_message>` (공유 inbox) | variant | 1024 | `reject_newest` | 의도와 결과는 버리면 안 된다. 가득 참은 logic 정체를 뜻하므로 게시자가 실패를 보고 진단을 남긴다 |
| logic → worker_i | `channel<operation_request>` | request + token | 64 | `reject_newest` | scheduler가 lane당 동시 1개만 넣으므로 사실상 도달하지 않는 방어 상한이다 |
| logic → ui | `latest_slot<std::shared_ptr<const view_snapshot>>` | 불변 snapshot | 1 | 덮어쓰기 | 최신만 의미 있음 |
| logic → input | `latest_slot<std::shared_ptr<const layout_snapshot>>` | 불변 snapshot | 1 | 덮어쓰기 | hit test는 최신 layout만 필요 |

용량 값은 단계 6 성능 검증에서 조정할 수 있는 초기값이며 상수 한 곳에 모은다.

## 5. message와 envelope

### 5.1 envelope

```cpp
template<typename payload_type>
struct envelope
{
    // 채널별 단조 증가 접수 번호. drop_oldest에서 건너뜀이 관찰된다.
    std::uint64_t sequence { 0 };
    std::chrono::steady_clock::time_point enqueued_at {};
    payload_type payload {};
};
```

### 5.2 envelope에 넣지 않는 것과 그 근거

| 후보 | 결정 | 근거 |
| --- | --- | --- |
| message ID 문자열 | 제외 | 같은 binary 안에서는 C++ type이 곧 식별자다. `std::variant` tag가 message ID 역할을 한다 |
| correlation / `operation_id` / `project_id` / `generation` | payload로 | 도메인 개념이다. Gitman의 `operation_event`가 세 값을 직접 담고, logic이 발급·검사한다. core가 이를 알면 재사용성이 깨진다 |
| schema version | 제외 | 직렬화 경계가 없다. 프로세스 밖으로 나가는 순간 이 component의 범위가 아니다 |
| deadline | 제외 | 소비 시점에만 검사할 수 있어 실효가 없다. 최신성 판정은 응용층 generation 검사가 한다 |

### 5.3 Gitman message type 표

| type | 방향 | 주요 필드 | 비고 |
| --- | --- | --- | --- |
| `raw_input_event` | ui → input | 종류(포인터/키/포커스), 좌표, 키 코드, modifier, 시각 | Win32 메시지의 최소 복사. `HWND`는 담지 않는다 |
| `user_intent` | input → logic | 의도 종류, 대상 `project_id`, dialog 항목 색인 등 | hit test 결과. 좌표가 아니라 의미를 담는다 |
| `logic_command` | logic 내부 | intent 해석 결과 | 사용자 확장 경계 (ADR-004) |
| `operation_request` | logic → worker | 작업 종류(조회/원격/update/switch), `project_definition`, option, `operation_id`, `generation`, cancellation token | worker는 단계 4 provider를 동기 호출 |
| `operation_event` | worker → logic | `project_id`, `operation_id`, `generation`, 종류(시작/로그 레코드/완료), snapshot 또는 change result, diagnostics | 로그 레코드는 카드별 sequence를 보존 |
| `control_message` | any → logic | 종료 요청, worker 준비 완료 | 별도 우선순위 없이 같은 inbox |
| `view_snapshot` | logic → ui | 카드 목록, 선택, dialog, 로그 뷰 상태의 불변 사본 | `shared_ptr<const>`로 게시 |
| `layout_snapshot` | logic → input | hit test 영역, 포커스 순서의 불변 사본 | 〃 |

payload는 **불변 값 또는 move-only 값**이다. 큰 snapshot은 `std::shared_ptr<const T>`로 공유해 복사 비용을 없애되, 수신 측이 mutable 참조를 얻을 방법은 없다.

## 6. public API 초안

세부 이름은 구현 검수에서 확정한다. 표준 라이브러리 외 의존성이 없고 예외를 경계 밖으로 던지지 않는다.

```cpp
namespace messaging {
    enum class overflow_policy
    {
        reject_newest,
        drop_oldest,
    };

    enum class post_result
    {
        posted,
        // drop_oldest 채널이 가장 오래된 항목을 버리고 받았다.
        posted_after_drop,
        channel_full,
        channel_closed,
    };

    enum class receive_status
    {
        received,
        empty,
        timed_out,
        closed,
    };

    struct channel_options
    {
        std::size_t capacity { 1024 };
        overflow_policy policy { overflow_policy::reject_newest };
        // envelope 시각의 주입점. test가 고정 시계를 넣는다.
        std::function<std::chrono::steady_clock::time_point()> clock {};
    };

    struct channel_statistics
    {
        std::uint64_t posted { 0 };
        std::uint64_t dropped_oldest { 0 };
        std::uint64_t rejected { 0 };
        std::size_t peak_depth { 0 };
    };

    template<typename payload_type>
    class channel
    {
    public:
        explicit channel(channel_options options);
        // 복사와 이동 금지. 채널은 조립 시점에 만들어져 참조로 주입된다.

        // 임의 스레드. 절대 블로킹하지 않는다.
        [[nodiscard]] post_result post(payload_type payload);

        // 아래 세 함수는 소비 스레드 전용이다. debug build는 첫 호출의 thread id를
        // 기록하고 이후 불일치를 assert한다.
        [[nodiscard]] receive_status try_receive(envelope<payload_type>& out);
        [[nodiscard]] receive_status receive_wait(envelope<payload_type>& out, std::chrono::milliseconds wait_limit);
        // 한 번의 잠금으로 최대 max_count개를 꺼낸다. 반환값은 꺼낸 수다.
        [[nodiscard]] std::size_t drain(std::vector<envelope<payload_type>>& out, std::size_t max_count);

        // 이후 post는 channel_closed를 돌려주고, 남은 메시지는 계속 소비할 수 있다.
        // 비어 있고 닫힌 채널의 receive는 closed를 돌려준다. 멱등이다.
        void close() noexcept;
        [[nodiscard]] bool closed() const noexcept;

        // 비어 있던 채널이 채워질 때와 close에서 post/close 호출 스레드로 호출된다.
        // callback은 짧아야 하고 예외를 던지면 안 되며 채널 API를 재호출하면 안 된다.
        // Gitman ui adapter는 여기서 PostMessageW 하나만 호출한다.
        void set_signal_callback(std::function<void()> callback);

        [[nodiscard]] channel_statistics statistics() const;
    };

    template<typename value_type>
    struct versioned
    {
        std::uint64_t version { 0 };
        value_type value {};
    };

    // 최신 값 하나만 유지하는 상태 mailbox다. 게시가 이전 값을 덮어쓰므로 소비자는
    // 항상 최신만 본다. view/layout snapshot 전달용이다.
    template<typename value_type>
    class latest_slot
    {
    public:
        // 임의 스레드(Gitman에서는 logic 하나). 새 version을 돌려준다.
        std::uint64_t publish(value_type value);
        // 마지막으로 본 version보다 새 값이 있을 때만 돌려준다.
        [[nodiscard]] std::optional<versioned<value_type>> take_newer(std::uint64_t last_seen_version);
        void close() noexcept;
        [[nodiscard]] bool closed() const noexcept;
        void set_signal_callback(std::function<void()> callback);
    };
} // namespace messaging
```

### 6.1 ordering 계약

- 한 producer가 한 채널에 게시한 순서는 소비 순서에서 보존된다.
- 서로 다른 producer 사이의 전역 순서는 접수 순서(내부 잠금 획득 순서)이며, `sequence`가 그 순서를 그대로 드러낸다.
- 소비자가 하나뿐이므로 소비 순서는 접수 순서와 같다. `drop_oldest`에서만 `sequence`에 구멍이 생긴다.
- `latest_slot`은 순서가 아니라 최신성만 보장한다. `version`은 단조 증가한다.

### 6.2 수명과 소유권

- 채널과 slot은 조립 code(단계 6 app 조립)가 생성해 스레드들보다 오래 살게 유지한다. 참조로만 주입하고 소멸 전에 모든 스레드를 join한다.
- payload는 값으로 이동한다. 게시 후 producer에 남는 참조가 없고, `shared_ptr<const>` snapshot은 불변이라 공유가 안전하다.
- allocator 주입은 제공하지 않는다. 필요가 입증되면 내부 구현만 바꾼다.

### 6.3 request / result / event 구분

core는 세 가지를 구분하지 않는다. 모두 "채널에 게시되는 값"이다. Gitman에서 request는 `operation_request`, 그에 대한 result와 진행 event는 `operation_event`이며, 대응은 logic이 발급한 `operation_id`의 echo로 이뤄진다. 오류는 `operation_event`의 diagnostics 값으로 전달되고, core 자체의 실패는 `post_result`/`receive_status` 열거뿐이다.

### 6.4 우선순위와 starvation

우선순위 큐는 두지 않는다. 종료 신호는 `close()`라는 별도 상태라 대기열을 앞지를 필요가 없고, logic inbox는 FIFO이므로 어떤 producer도 굶지 않는다. `drain(max_count)`의 상한이 한 wake에서 무한히 소비하는 것을 막아 렌더링 기회를 보존한다.

## 7. cancellation, timeout, late result, shutdown

### 7.1 취소와 timeout

- 작업 취소는 `operation_request`에 담긴 단계 3 `process_cancellation_token`으로 전달된다. logic이 source를 보관하고, worker와 그 아래 provider·process가 같은 token을 관찰한다. 메시지 계층에는 취소 개념이 없다.
- 명령별 timeout은 단계 4의 실행 정책이 이미 처리한다.
- `receive_wait`의 timeout은 "대기를 끊고 주기 작업을 할 기회"이지 메시지의 수명이 아니다.

### 7.2 late result와 dedup

- logic은 카드마다 `generation`을 유지하고 refresh 요청 시 증가시킨다. `operation_event.generation`이 현재보다 오래됐으면 **버린다**. 카드 삭제 후 도착한 결과도 같은 검사로 소멸한다 (ADR-004 검증 항목).
- 같은 카드의 중복 refresh 요청은 scheduler가 "실행 중 + 대기 1"로 병합한다(`docs/plan.md` 5.1). dedup은 메시지 계층이 아니라 lane 정책이다.

### 7.3 shutdown 절차

ADR-004의 시작·종료 절차를 큐 동작으로 구체화한 것이다. 조립체(단계 6)가 스레드를 소유하고 join한다.

1. 종료 intent가 logic inbox에 도착한다 (다른 메시지와 같은 경로).
2. logic: 새 작업 접수를 중단하고, 모든 실행 중 작업의 cancellation을 요청하며, 각 worker inbox를 `close()`한다.
3. worker: 현재 작업을 취소 완료하고 마지막 `operation_event`를 게시한 뒤, 자기 inbox가 `closed`를 돌려주면 종료한다. logic이 worker를 join한다.
4. logic: "종료 중" view_snapshot을 게시하고 input inbox 방향 layout_slot을 close, logic inbox를 close한 뒤 남은 메시지를 drain하고 종료한다.
5. input: raw input 채널이 close되면 종료한다. ui thread가 input과 logic을 join한 뒤 Win32/Skia 자원을 해제한다.
6. close 이후의 게시(`channel_closed`)는 오류가 아니라 정상 신호다. worker는 결과를 버리고 종료한다. 앱이 끝나는 중이므로 메시지 유실이 허용되며, 문서 저장 같은 유실 불가 작업은 메시지가 아니라 logic의 동기 호출로 수행된다.

join 대기에는 상한을 두고, 초과 시 진단 로그를 남긴 뒤 process 종료에 맡긴다. 취소 불가능한 자식 프로세스 정리는 단계 3의 job object가 이미 담당한다.

## 8. sequence diagram과 failure path

### 8.1 정상 refresh

```text
ui(WndProc)      input           logic                worker_i          ui(render)
    │ raw(click)   │               │                     │                │
    ├─channel─────▶│ hit test      │                     │                │
    │              ├─intent(refresh, project A)─▶        │                │
    │              │               │ generation++        │                │
    │              │               ├─request(A, gen42, token)─▶           │
    │              │               │ snapshot(A: loading)│                │
    │              │               ├─latest_slot.publish──────────────────▶ 그리기
    │              │               │                     │ query_local    │
    │              │               │◀─event(A, gen42, 완료, snapshot)──── │
    │              │               │ gen 검사 → 반영      │                │
    │              │               ├─latest_slot.publish──────────────────▶ 그리기
```

### 8.2 late result 폐기

```text
logic: refresh A (gen42) ─▶ worker_1 (실행 중)
사용자: refresh A 다시 ─▶ logic: gen43, lane 정책상 대기로 병합
worker_1: event(A, gen42, 완료) ─▶ logic: 42 < 43 → 폐기, 대기 중이던 gen43 요청 제출
```

### 8.3 switch 확인과 재검증

```text
input ─intent(switch 확인, 후보 X)─▶ logic
logic: dialog 상태로 후보 X 재검증(순수 함수) → 통과
logic ─request(switch_to X, gen, token)─▶ worker
worker: provider.switch_to (재조회→재검증→실행→재조회, 단계 4 계약)
worker ─event(성공, 새 snapshot)─▶ logic ─view_snapshot─▶ ui
실패 시: event의 diagnostics가 카드 로그로, 카드 상태는 재조회 결과로 갱신
```

### 8.4 failure path

| 상황 | 동작 |
| --- | --- |
| logic inbox 가득 참 (`reject_newest`) | 게시자(input/worker)가 `channel_full`을 받고 짧게 재시도 후 실패를 자체 진단으로 남긴다. intent 하나가 사라져도 앱 상태는 일관되며, 통계의 `rejected`가 정체를 드러낸다 |
| raw input 폭주 (`drop_oldest`) | 가장 오래된 이벤트부터 버린다. `dropped_oldest` 통계와 `sequence` 구멍으로 관찰된다 |
| worker의 provider 호출 실패 | 단계 4 계약상 예외 없이 diagnostics로 돌아온다. worker는 그것을 `operation_event`에 실어 보낼 뿐 판단하지 않는다 |
| worker 스레드가 event를 못 보내고 종료(치명 결함) | lane이 "실행 중"으로 남는다. debug assert 대상이며, 방어 watchdog은 두지 않는다. 이런 상태는 버그이지 정책 대상이 아니다 |
| close 이후 게시 | `channel_closed` 정상 신호. 게시자는 조용히 버린다 |
| signal callback 누락(설정 전 게시) | 메시지는 쌓이고, callback 설정 후 첫 게시 또는 소비자의 다음 폴링에서 처리된다. 조립 순서는 "채널 생성 → callback 설정 → 스레드 시작"으로 고정한다 |

## 9. deterministic test와 sanitizer 전략

### 9.1 단일 스레드 계약 test (완전 결정적)

- post/try_receive의 FIFO와 `sequence` 연속성, 주입 시계로 고정한 `enqueued_at`
- `reject_newest`: 가득 찬 뒤 post가 `channel_full`, 기존 내용 불변, `rejected` 증가
- `drop_oldest`: 가장 오래된 항목 교체, `posted_after_drop`, `sequence` 구멍, `dropped_oldest` 증가
- close 의미론: close 후 post는 `channel_closed`, 남은 메시지 소비 가능, 빈 뒤 `closed`, 멱등 close
- signal callback: 비어 있던 채널의 첫 post에서 정확히 1회, 연속 post에서 미호출, close에서 호출
- `drain(max_count)` 상한과 순서
- `latest_slot`: 덮어쓰기, `take_newer`의 최신성 판정, version 단조 증가
- move-only payload(`std::unique_ptr`) 왕복
- debug thread ownership assert: 다른 스레드의 receive가 assert에 걸리는 것 (debug 전용 test)

### 9.2 다중 스레드 test (구조로 결정성 확보)

- producer N개가 각자 표식과 일련번호를 담아 M개씩 게시 → 소비 결과에서 **producer별 부분 순서 보존**과 총 개수 N×M을 단정한다. 전역 순서는 단정하지 않는다 (계약이 아니므로)
- 소비 중 close, close 중 게시의 경합을 barrier로 고정해 반복한다
- `receive_wait` timeout은 단계 3 timeout test 선례처럼 짧은 실측 시간으로 확인한다
- stress: producer 8 × 메시지 10만 게시·소비 후 통계 정합(=게시 수) 확인, 전체 suite `--repeat until-fail:3`

### 9.3 sanitizer와 정적 분석

- 기존 VS2022 `/analyze` 무경고 기준을 그대로 적용한다.
- **ASan job 추가 제안**: MSVC `/fsanitize=address`로 messaging test 실행 파일을 별도 빌드해 stress를 돌린다. 채널 수명·payload 이동 오류를 잡는다.
- **TSan은 MSVC가 지원하지 않는다.** 대신 (1) 단일 consumer + 단일 mutex 설계로 경합 표면을 최소화하고, (2) 모든 공유 상태를 한 클래스의 잠금 아래 두며, (3) 9.2의 stress 반복으로 보완한다. 이 한계는 검증 기록에 명시한다.
- 성능 계측은 `channel_statistics`(게시 수, 거부 수, 최대 깊이)로 시작하고, 단계 6 성능 검증에서 필요하면 항목을 늘린다.

### 9.4 Gitman adapter 검증 (단계 6)

- 100개 이상 모의 카드의 병렬 모의 작업 중 UI/input heartbeat 유지 (ADR-004 검증 항목)
- 카드 삭제 후 이전 generation 결과 폐기
- 종료 시퀀스의 join 순서와 상한 timeout
- Win32 signal(PostMessageW) 연동의 재진입 안전성

## 10. library 구성과 의존성 방향

```text
src/messaging/                      (신규, 표준 라이브러리 외 의존 없음)
├── envelope.h
├── channel.h            (template, header-only 중심)
├── latest_slot.h
└── messaging_statistics.h

CMake: add_library(gitman_messaging INTERFACE 또는 STATIC)
의존성: gitman_messaging ← (어떤 gitman target도 참조하지 않음)
Gitman adapter(단계 6): presentation/platform 계층이 gitman_messaging을 링크하고
Win32 wake와 message type(variant)을 정의한다.
```

- namespace는 `gitman`이 아니라 **`messaging`**이다. 다른 프로젝트가 이 디렉터리만 복사해도 컴파일되는 것이 분리 기준이다.
- template 중심이라 header-only가 자연스럽다. 구현 검수에서 STATIC 여부(비 template 공통부)를 확정한다.
- Gitman의 `logic_message` variant, intent/event type 정의는 응용층(단계 6)이며 이 문서 5.3의 표를 따른다.

## 11. 고려한 대안

| 대안 | 기각 사유 |
| --- | --- |
| 공유 MPMC job queue로 worker 분배 | 같은 카드 직렬화를 위해 결국 lane 상태가 필요하다. MPMC의 소비 순서 비결정성만 추가되고 이득이 없다 |
| lock-free ring buffer | 이 규모에서 mutex 병목이 실측된 바 없다. 추론 가능성이 우선이며 내부 구현은 계약 변경 없이 교체 가능하다 |
| 메시지 우선순위 레벨 | 종료는 `close()` 상태로, 공정성은 FIFO로 이미 해결된다. 우선순위는 starvation 정책이라는 새 문제를 만든다 |
| producer 블로킹 post | UI/input thread 블로킹 금지(NFR-009)와 정면 충돌하고 deadlock 표면을 만든다 |
| envelope에 generation 내장 | 도메인 개념의 침투다. generic core의 재사용성이 깨지고, 어차피 판정은 앱 상태(현재 generation)가 있어야 한다 |
| 단일 전역 bus (publish/subscribe) | 구독 관계가 암묵화되고 소유권 원칙(스레드당 inbox)이 흐려진다. 명시적 채널 참조 주입이 단순하다 |

## 12. 검수 요청 항목

1. MPSC `channel` + `latest_slot` 두 primitive 구성과 "스레드당 inbox 하나" topology (3~4장)
2. envelope 최소화: correlation·generation·schema version을 payload와 응용층에 두는 분리 (5.2)
3. 채널별 용량·overflow 정책 초기값 표 (4.1)
4. 비블로킹 post와 signal callback 기반 wake-up, Win32 연동 방식 (6장)
5. shutdown 절차와 close 의미론 (7.3)
6. worker 분배를 MPMC 없이 scheduler 지정 inbox로 하는 방향 (4장, 11장)
7. test 전략과 ASan 추가, TSan 부재의 완화책 (9장)
8. `messaging` 독립 namespace와 `src/messaging/` 분리 (10장)

승인 시 이 내용을 ADR-005로 기록하고 단계 6 계획(`S6-P0`)에서 구현 체크포인트를 정한다.
