# ADR-004: 스레드와 상태 소유권

## 상태

승인됨 - 기본 소유권 승인, 재사용 메시지 구조는 구현 전 별도 검수 필요

## 배경

여러 카드의 Git/SVN 작업이 병렬로 실행되는 동안 Win32 메시지 처리와 Skia 렌더링이 멈추지 않아야 한다. UI와 입력 코드에서 직접 상태를 변경하면 경쟁 조건과 수명 문제가 발생한다. 사용자가 확장하거나 제어할 코드는 logic command 경계에 집중되어야 한다.

## 결정

### 실행 단위

| 실행 단위 | 단일 소유 상태 | 책임 |
| --- | --- | --- |
| `ui_thread` | `HWND`, Win32 message pump, Skia surface, 현재 frame snapshot | 메시지 수신, 렌더링, present |
| `input_thread` | 입력 정규화 상태 | raw input을 hit test하여 `user_intent` 생성 |
| `logic_thread` | 유일한 mutable `app_state` | intent 처리, 검증, 선택 카드, dialog, 작업 예약, view snapshot 생성 |
| `worker_pool` | 실행 중 operation의 로컬 상태 | 파일 I/O, Git/SVN process, stdout/stderr 수집 |

### 개념적 메시지 방향

```text
ui_thread --raw_input_event--> input_thread --user_intent--> logic_thread
logic_thread --operation_request--> worker_pool --operation_event--> logic_thread
logic_thread --view_snapshot--> ui_thread
logic_thread --layout_snapshot--> input_thread
```

- 위 그림은 책임과 데이터 흐름만 나타내며 실제 queue 종류, envelope와 payload API를 확정하지 않는다.
- 스레드 사이에서 공유 mutable 업무 상태를 참조하지 않고 승인된 메시지 component를 통해서만 통신한다.
- mutable `app_state`는 logic thread에서만 읽고 쓴다.
- UI와 input thread는 repository provider, JSON store, process runner를 호출하지 않는다.
- worker는 UI 객체와 app state 포인터를 보유하지 않는다.
- Gitman adapter는 늦거나 중복된 결과를 구분할 수 있어야 하지만 `project_id`, `operation_id`, `generation`, `sequence`의 범용 envelope 포함 여부는 상세 설계 검수에서 정한다.
- 각 카드는 독립 operation lane과 로그 ring buffer를 가진다.
- 같은 카드의 변경 작업은 직렬화하고 서로 다른 카드는 제한된 worker pool에서 병렬 실행한다.

### 재사용 가능한 메시지 구조 검수 게이트

스레드 간 메시지 계층은 Gitman 내부 구현으로 고정하지 않고 다른 C++ 프로젝트에서도 사용할 수 있는 독립 component로 설계한다. 다만 이 세션에서는 상세 구조를 확정하거나 구현하지 않는다.

후속 구현 세션에서 단계 6을 시작하기 전에 다음 항목을 포함한 별도 설계안을 작성하고 사용자에게 검수를 요청해야 한다.

- platform 및 UI framework와 무관한 public API와 namespace
- type-safe message payload, envelope, message ID, correlation ID와 schema version
- queue ownership, single/multi producer 및 consumer 조합, ordering 보장 범위
- bounded capacity, backpressure, overflow, priority와 starvation 정책
- 값 전달, move-only payload, lifetime, allocator와 큰 payload 처리
- request/result/event 구분과 오류 전달 모델
- cancellation, deadline, timeout과 shutdown protocol
- late result, generation, deduplication과 idempotency 정책
- wake-up 방식, batching, fairness와 성능 계측 hook
- deterministic test adapter, fake clock와 thread sanitizer 검증 전략
- library target 분리 가능성, Gitman adapter와 범용 core 사이 의존성 방향

검수 절차는 다음과 같이 고정한다.

1. 단계 5 종료 또는 단계 6 시작 시 이 보류 항목을 사용자에게 다시 알린다.
2. sequence diagram, message type 표, queue topology, failure 및 shutdown scenario, public C++ API 초안을 문서로 제시한다.
3. 사용자가 설계를 승인하기 전에는 범용 message queue, dispatcher 또는 thread bridge 구현을 시작하지 않는다.
4. 승인 내용을 별도 ADR로 기록한 뒤에만 단계 6 구현을 진행한다.

이 항목은 인수인계 시 반드시 전달해야 하는 **구현 차단 조건**이다.

### 사용자 제어 경계

새 기능은 `user_intent`와 `logic_command` handler를 추가하는 방식으로 구현한다. UI callback에서 업무 상태를 직접 바꾸는 public API는 제공하지 않는다. 이 원칙으로 사용자는 logic 계층만 제어해도 제품 동작을 변경할 수 있다.

### 시작과 종료

1. UI thread가 Win32 창과 queue를 준비한다.
2. logic thread와 input thread를 시작한다.
3. logic thread가 worker pool을 사용할 수 있는 상태를 게시한다.
4. 종료 intent를 받은 logic thread가 새 작업 접수를 막고 worker 취소를 요청한다.
5. worker 결과 수집 후 input thread를 종료한다.
6. UI thread가 마지막 snapshot을 처리하고 Win32 및 Skia 자원을 해제한다.

실제 join 순서와 timeout은 단계 6 구현 전에 시퀀스 테스트로 확정한다.

## 고려한 대안

### UI thread에서 logic도 처리

작은 handler라도 상태 조회가 확장되면 메시지 pump를 막을 위험이 있어 채택하지 않는다.

### 모든 카드마다 전용 thread 생성

프로젝트 수에 따라 thread 수가 무제한 증가하고 종료가 복잡해져 bounded worker pool을 사용한다.

### 공유 app state와 mutex

lock 순서와 snapshot 일관성을 추론하기 어려워 단일 logic thread 소유권을 사용한다.

## 결과

- UI 업데이트는 즉시 mutable state를 참조하지 않고 snapshot 발행 지연을 가진다.
- input hit test에는 logic thread가 제공한 최신 layout snapshot을 사용한다.
- queue 종류, 용량, overflow 정책, message representation과 worker 수는 사전 설계 검수 및 단계 6 성능 검증에서 확정한다.
- 범용 메시지 component의 상세 API와 동시성 계약은 사용자 사전 검수 전까지 미확정 상태로 유지한다.

## 검증 방법

- debug build에서 thread ownership assertion을 제공한다.
- 100개 카드의 병렬 모의 작업 중 UI/input heartbeat가 유지되는지 확인한다.
- 카드 삭제 후 도착한 이전 generation 결과가 폐기되는지 확인한다.
- UI/input handler가 logic command 없이 app state를 바꾸는 경로가 없는지 정적 검토한다.
- 승인된 범용 메시지 ADR과 실제 public API 및 queue topology가 일치하는지 검사한다.
