# ADR-005: 범용 스레드 메시지 component

## 상태

승인됨 - 2026-08-17 사용자가 `MSG-P0` 설계안(`docs/thread-message-design.md`)을 승인했다. ADR-004의 구현 차단 조건이 해소되어 단계 6을 진행할 수 있다.

## 배경

ADR-004는 input, UI, logic thread와 worker pool 사이의 통신을 재사용 가능한 독립 component로 설계하되, 상세 구조를 구현 전에 별도 검수받도록 차단 조건을 두었다. `docs/thread-message-design.md`가 그 설계안이며 이 ADR은 승인된 결정을 고정한다. 상세 계약과 근거, sequence diagram, 고려한 대안은 설계 문서를 기준으로 한다.

## 결정

| 항목 | 결정 |
| --- | --- |
| primitive | consumer 스레드 하나가 소유하는 MPSC FIFO `channel<T>`와 최신값 병합 `latest_slot<T>` 두 가지만 제공한다 |
| topology | 소비 스레드당 inbox 하나. logic thread는 intent·worker event·control을 하나의 `channel<variant>`로 받아 도착 순서대로 처리한다 |
| worker 분배 | 공유 MPMC job queue를 두지 않는다. `task_scheduler`(응용층)가 lane 정책에 따라 worker별 inbox로 request를 배정한다 |
| snapshot 전달 | `latest_slot` 덮어쓰기. view/layout snapshot은 최신 값만 의미가 있고 backpressure가 구조적으로 사라진다 |
| producer 계약 | `post()`는 절대 블로킹하지 않는다. 가득 참·닫힘은 `post_result` 값으로 알린다 (NFR-009) |
| overflow | 채널 생성 시 `reject_newest`(기본) 또는 `drop_oldest`. 두 경우 모두 통계로 관찰된다. 우선순위 레벨은 두지 않는다 |
| envelope | 채널별 단조 `sequence`와 `enqueued_at`만 담는다. message ID는 C++ type(variant tag)이고, correlation·`operation_id`·`generation`·schema version·deadline은 payload와 응용층 정책이다 |
| wake-up | `set_signal_callback`. 비어 있던 채널이 채워질 때와 close에서 호출된다. Win32 연동은 adapter가 callback에서 `PostMessageW`만 호출한다 |
| 취소·timeout | 메시지 속성이 아니다. 취소는 payload로 전달되는 단계 3 cancellation token, 명령 timeout은 단계 4 실행 정책이 담당한다 |
| late result | logic의 카드별 `generation` 검사로 폐기한다. 중복 refresh 병합은 scheduler lane 정책이다 |
| shutdown | `close()`는 멱등이고, 이후 post는 `channel_closed`, 남은 메시지는 drain 가능하다. 종료 순서는 설계 문서 7.3을 따르며 close 이후의 게시는 오류가 아니라 정상 신호다 |
| payload | 값 이동(move-only 허용). 큰 snapshot은 `std::shared_ptr<const T>` 불변 공유. allocator 주입은 제공하지 않는다 |
| 분리 | 최상위 namespace `messaging`, `src/messaging/` 신규 target. gitman의 어떤 target에도 의존하지 않으며 표준 라이브러리만 사용한다 |
| 채널 구성 초기값 | 설계 문서 4.1의 표(용량·정책)를 초기값으로 하고 상수 한 곳에 모은다. 단계 6 성능 검증에서 조정할 수 있다 |

## 고려한 대안

설계 문서 11장에 기록했다: 공유 MPMC job queue, lock-free ring buffer, 메시지 우선순위, producer 블로킹 post, envelope에 generation 내장, 전역 publish/subscribe bus. 모두 기각 사유와 함께 남겼다.

## 결과

- 단계 6은 이 결정과 일치하는 `messaging` component를 먼저 구현하고, Gitman의 message type(variant)과 Win32 wake 연동은 응용층에 둔다.
- `task_scheduler`, generation 검사, lane 병합은 메시지 계층 밖의 응용 정책으로 구현한다.
- ADR-004의 "승인된 범용 메시지 ADR과 실제 public API 및 queue topology가 일치하는지 검사한다"는 검증 항목이 이 ADR을 기준으로 동작한다.

## 검증 방법

- 설계 문서 9장의 전략을 따른다: 단일 스레드 계약 test(완전 결정적, 주입 시계), producer별 부분 순서만 단정하는 다중 스레드 test, stress와 `--repeat until-fail:3`, MSVC `/fsanitize=address` job.
- TSan은 MSVC가 지원하지 않으므로 단일 consumer 설계와 stress 반복으로 완화하고 그 한계를 검증 기록에 명시한다.
- debug build의 consumer thread ownership assert로 소유권 위반을 잡는다.
