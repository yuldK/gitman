# 단계 6 구현 계획 - GUI와 상태 연결

## 1. 문서 상태

- 작성일: 2026-08-17
- 대상: 구현 단계 6
- 현재 상태: `S6-P0` 계획 제출, 사용자 검수 대기
- 현재 검수 게이트: `S6-P0` 계획 승인
- 관련 요구사항: REQ-002, REQ-003, REQ-005, REQ-009~REQ-016, NFR-005~NFR-006, NFR-009, NFR-012~NFR-014
- 상위 문서: `docs/plan.md` 3.1, 3.2, 3.8, 3.10, 5.1, 8장 단계 6, `docs/decisions/ADR-004`, `docs/decisions/ADR-005`, `docs/thread-message-design.md`
- 선행 조건: 단계 5 최종 승인과 `MSG-P0` 설계 승인 (2026-08-17, ADR-005로 기록)

진행 방식은 단계 5와 같다. production code와 test code를 한 검수 구간에서 함께 작성하고, 구간 종료 시 보고와 커밋 후 다음 구간을 진행한다.

## 2. 목표

단계 6은 지금까지 test로만 검증된 계층들을 **실행되는 앱**으로 조립한다. ADR-005의 메시지 component를 구현하고, input·UI·logic thread와 worker pool을 연결하며, 실제 `.version-list` 문서의 카드 목록이 Skia 창에 표시되고 refresh가 동작하는 것까지가 범위다.

- ADR-005와 일치하는 `messaging` component(`channel`, `latest_slot`)를 구현한다.
- logic thread가 유일한 mutable `app_state`를 소유하고 view/layout snapshot을 게시한다.
- `task_scheduler`가 카드별 lane 직렬화, 전체 동시 실행 상한, 중복 refresh 병합, generation 발급을 담당한다.
- worker pool이 단계 4 provider를 동기 호출하고 `operation_event`로 결과를 돌려준다.
- input thread가 raw input을 layout snapshot으로 hit test해 `user_intent`를 만든다.
- 가로형 카드 목록을 Skia로 렌더링한다: Codicon 상태, 브랜치/URL, 리비전, 작업 트리 요약, 마지막 확인 시각, 진행 표시.
- 전체 및 카드별 refresh 버튼이 로컬 → remote-first 순서로 실제 조회를 실행한다.
- 이름 검색 필터와 정렬, 빈 상태와 오류 상태 표시를 제공한다.
- 명령행 인자의 `.version-list` 경로로 활성 문서를 열고, 없으면 빈 상태에서 Win32 파일 dialog로 연다.
- 실행 파일에 `gitman_messaging`, `gitman_workspace`, `gitman_process`, `gitman_vcs`, `gitman_discovery`를 링크하고 Win32 구현체(resolver, probe, enumerator, runner)를 주입 조립한다.
- 시작·종료 시퀀스를 ADR-005의 shutdown protocol대로 구현한다.
- 100개 이상 모의 카드의 병렬 모의 작업에서 UI/input thread가 멈추지 않는 것을 자동 test로 고정한다.

## 3. 단계 6에서 하지 않는 일

- update·switch 실행 UI와 dialog, submodule option UI (단계 7). 카드의 해당 버튼은 표시하되 비활성으로 둔다.
- 탐색 미리보기 dialog와 선택 등록 UI (단계 7). 단계 5의 service는 조립만 해 둔다.
- 카드별 로그 ring buffer와 하단 로그 뷰 (단계 7). 로그 ring buffer 크기 확정도 단계 7이다.
- 환경설정 화면 (단계 7). `settings` 값은 단계 4 경로로 이미 반영된다.
- Windows file association 등록 (단계 8). 단계 6은 명령행 인자와 파일 dialog로만 문서를 연다.
- 자동 주기 원격 조회 (ADR-003, 최초 버전 제외).
- messaging component의 성능 최적화. 계약과 정확성이 우선이며 조정은 `S6-V1` 계측 후 판단한다.

## 4. 사용자 검수에서 확정할 설계 제안

### 4.1 체크포인트 분할

작업량이 단계 4에 준하므로 구현 5구간으로 나눈다. 각 구간은 코드와 test를 함께 담는다.

| 구간 | 내용 | 의존 |
| --- | --- | --- |
| `S6-D1` | `messaging` component: `channel`, `latest_slot`, envelope, 통계, thread ownership assert. ADR-005 계약 test 전부와 stress, ASan 구성 | 없음 |
| `S6-D2` | 표시·상태 모델과 logic: `app_state`, 카드 상태, `view_snapshot`/`layout_snapshot` 값 type, Codicon 매핑, `user_intent`/`logic_command` 처리, generation 정책. 스레드 없이 순수 test | D1 |
| `S6-D3` | `task_scheduler`와 worker pool: lane 직렬화, 동시 상한, refresh 병합, worker thread의 provider 호출과 event 생성. fake provider test와 실제 스레드 test | D1, D2 |
| `S6-D4` | input thread: raw event 정규화, layout snapshot hit test, intent 변환, 키보드 포커스 이동. 순수 test | D2 |
| `S6-D5` | Skia 카드 렌더링과 앱 조립: 카드 목록·필터·정렬·빈 상태 그리기, caption 통합, Win32 wake 연동, 문서 열기, exe 링크와 시작·종료 시퀀스 | 전부 |
| `S6-V1` | 100+ 모의 카드 stress, 전체 matrix, 수동 검증 checklist 제시 | 전부 |

### 4.2 단계 6의 카드 동작 범위

| 카드 요소 | 단계 6 | 근거 |
| --- | --- | --- |
| 상태 표시 (Codicon, 브랜치/URL, 리비전, 작업 트리, 확인 시각) | 동작 | REQ-002, REQ-005 |
| 전체/카드별 refresh | 동작 (로컬 → remote-first) | REQ-014 |
| update, switch 버튼 | 표시만, 비활성 | 실행 UI는 단계 7 |
| 로그 보기 | 없음 | 단계 7 |
| 카드 선택 | 동작 (선택 상태만, 하단 뷰는 단계 7) | 단계 7 준비 |

### 4.3 문서 열기

- 명령행 인자에 `.version-list` 경로가 있으면 그 문서를 활성화한다. 잘못된 경로나 손상 문서는 빈 상태 화면에 진단을 표시한다.
- 인자가 없으면 빈 상태 화면의 "문서 열기" 버튼이 Win32 파일 dialog(`IFileOpenDialog`)를 연다. dialog 호출은 platform adapter에 격리한다.
- association 등록과 double-click 실행 검증은 단계 8이다 (REQ-016의 나머지).

### 4.4 동시 실행 상한 초기값

`docs/plan.md` 10장이 단계 6~7로 미룬 값의 확정 제안이다.

| 항목 | 제안 | 근거 |
| --- | --- | --- |
| worker thread 수 | `min(4, hardware_concurrency)` | 원격 조회는 대기 중심이라 소수로 충분하고, 프로세스 폭주를 막는다 |
| 전체 동시 작업 상한 | worker 수와 동일 | worker당 동시 1작업. lane 배정이 곧 상한이다 |
| 카드별 동시 작업 | 1 (lane 직렬화) | ADR-003·ADR-004 기존 결정 |
| 채널 용량 | ADR-005 설계 문서 4.1의 표 | 승인된 초기값 |

값은 상수 한 곳에 모으고 이후 `settings` 확장은 단계 7 이후에 정한다.

### 4.5 렌더링과 wake 방식

- UI thread는 Win32 message pump를 유지한다. `latest_slot` signal callback이 `PostMessageW(WM_APP_SNAPSHOT)`를 게시하고, handler가 `InvalidateRect`로 다시 그린다. 연속 게시는 pump에서 자연히 병합된다.
- 진행 애니메이션(loading spinner)은 `WM_TIMER` 기반이며 진행 중 카드가 있을 때만 켠다.
- 카드 목록은 세로 스크롤과 보이는 영역 중심 그리기를 적용한다. 완전한 가상화는 성능 계측 후 필요 시 확장한다 (3.10).
- 필터는 이름 부분 문자열 검색, 정렬은 이름/상태 두 기준으로 시작한다. UI 상세(입력창 위치 등)는 `S6-D5`에서 정한다.

### 4.6 조립과 계층

- 새 static library `gitman_messaging`(namespace `messaging`)은 어떤 gitman target에도 의존하지 않는다 (ADR-005).
- Gitman 응용층의 message type(`logic_message` variant, intent·event 정의)과 스레드 조립은 `presentation`/`application` 계층에 둔다. Win32 wake와 파일 dialog는 `platform/win32`다.
- 실행 파일 `gitman`이 처음으로 `gitman_workspace`, `gitman_process`, `gitman_vcs`, `gitman_discovery`, `gitman_messaging`을 링크한다. Win32 구현체 주입(경로 resolver, vcs probe, 열거자, process runner, 문서 file system)은 조립 code 한 곳에 모은다.
- debug build에 thread ownership assert(ADR-005)와 "logic thread만 `app_state`를 만진다"는 구조를 유지한다.

## 5. 테스트 계획

- `S6-D1`: ADR-005 설계 문서 9장의 전부. 단일 스레드 계약 test(주입 시계), producer별 부분 순서 다중 스레드 test, close 경합, stress(8 producer × 10만), `/fsanitize=address` 구성 추가.
- `S6-D2`: intent → 상태 전이 → snapshot 표를 순수 test로 고정한다. generation 폐기, 카드 삭제 후 late event 폐기(ADR-004 검증 항목), Codicon 매핑 전수.
- `S6-D3`: fake provider로 lane 직렬화(같은 카드 순차, 다른 카드 병렬), 상한 준수, 중복 refresh 병합, 취소 전파를 실제 worker thread로 확인한다.
- `S6-D4`: 고정 layout snapshot에 대한 hit test 좌표 표, 경계값, DPI 배율 좌표 변환, 키보드 포커스 순서.
- `S6-D5`: 조립 smoke(모의 문서로 창 없이 logic까지, 창 포함 smoke test 확장), 시작·종료 시퀀스의 join 완료, 명령행 문서 열기.
- `S6-V1`: **100개 이상 모의 카드의 병렬 모의 작업 중 UI/input heartbeat 유지**를 자동 test로 고정하고(ADR-004), 전체 matrix와 `--repeat until-fail:3`, 단일 exe install을 확인한다.
- 수동 검증(사용자): 100%/150%/200% DPI, custom caption의 Snap Layout·`Alt+Space`, 실제 저장소 문서로 카드 표시와 refresh, 긴 경로·긴 브랜치명 잘림. checklist를 `S6-V1`에서 제시한다.

## 6. 단계 6 완료 조건

- 모든 체크포인트가 검수를 통과한다.
- `messaging` 구현이 ADR-005의 계약과 일치한다 (API, topology, close 의미론, 통계).
- 실제 `.version-list` 문서를 열어 카드 목록이 표시되고 전체/카드별 refresh가 로컬과 remote-first 조회를 실행한다.
- 상태가 Codicon, 색상, 한국어 툴팁으로 표시된다 (REQ-005, 3.2 표).
- logic thread만 `app_state`를 변경하고 UI/input thread는 파일·프로세스 I/O를 하지 않는다 (REQ-015, NFR-009).
- 100개 이상 모의 카드의 병렬 모의 작업에서 custom caption과 UI/input thread가 멈추지 않는다.
- 카드 삭제·재요청 후 늦게 도착한 이전 generation 결과가 폐기된다.
- 종료 시 실행 중 작업이 취소되고 스레드가 정해진 순서로 join되며 프로세스가 남지 않는다.
- 전체 build/test/analyze/format/install matrix와 ASan job이 통과한다.
- `docs/verification/`의 단계 6 기록과 `docs/change_log.md`, `docs/handoff.md`가 최종 상태를 기록한다.

## 7. 계획 검수 항목

### 7.1 사용자 확정이 필요한 사항

1. 체크포인트 분할 6구간 (4.1)
2. 단계 6 카드 동작 범위: refresh까지만 동작, update/switch·로그·탐색 dialog는 단계 7 (4.2)
3. 문서 열기 방식: 명령행 인자 + 빈 상태의 파일 dialog (4.3)
4. worker 수 `min(4, hardware_concurrency)`와 상한 초기값 (4.4)
5. 렌더링 wake 방식과 필터·정렬 최소 구성 (4.5)

### 7.2 별도 이견이 없으면 계획대로 진행하는 사항

1. `gitman_messaging` target과 조립 계층 배치 (4.6)
2. ASan job을 messaging test에 추가 (5장)
3. 수동 검증 checklist를 `S6-V1`에서 제시 (5장)

## 8. 미결정 항목

- 카드별 로그 ring buffer 크기와 로그 뷰 구성 (단계 7)
- 탐색·등록·update·switch dialog의 UX (단계 7)
- `settings` 확장 항목(동시 상한, 로그 크기)의 편집 UI (단계 7)
- 카드 목록 완전 가상화 여부 (성능 계측 후)
- association 등록과 제거 (단계 8)
