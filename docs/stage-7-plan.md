# 단계 7 구현 계획 - 작업 UI와 로그

## 1. 문서 상태

- 작성일: 2026-08-18
- 대상: 구현 단계 7
- 현재 상태: `S7-P0` 수립. 2026-08-18 사용자가 단계 7 개시와 커밋까지의 자동 진행을
  위임했으므로 이 계획으로 곧바로 구현을 진행한다
- 관련 요구사항: REQ-006, REQ-007, REQ-008, REQ-009~REQ-015
- 상위 문서: `docs/plan.md` 3.3, 3.4, 3.5, 3.8, 3.9, 5.2, 5.3, 8장 단계 7,
  `docs/decisions/ADR-003`, `docs/decisions/ADR-004`, `docs/decisions/ADR-005`,
  `docs/ui-element-design.md`
- 선행 조건: 단계 6 최종 승인 (2026-08-18 사용자가 단계 7 개시를 지시하며 승인).
  단계 6 이후의 UI element 계층, drag & drop, `.version-list` 생성, 창 배치 저장,
  스크롤 UI, CMake 구성 정리도 같은 지시로 승인됐다
- 시작 기준선: `vs2022-tests` Debug 전체 CTest **544/544** 통과 (2026-08-18 실측,
  SVN 실측 1건은 도구 부재로 skip)

진행 방식은 단계 5~6과 같다. production code와 test code를 한 검수 구간에서 함께
작성하고, 구간 종료 시 검증 문서를 남기고 커밋한 뒤 다음 구간을 진행한다.

## 2. 목표

단계 7은 지금까지 비활성으로 남아 있던 **변경 작업(update·switch)을 화면에
연결하고**, 카드별 구조화 로그와 선택 카드 전용 하단 로그 뷰를 제공한다.

- 카드별 구조화 로그: 시간, 프로젝트 ID, 카드별 단조 증가 sequence, 작업 종류,
  심각도(스트림), 메시지를 담는 record를 카드별 ring buffer에 저장한다 (plan 3.9).
- 선택 카드 전용 하단 로그 뷰: 선택한 카드의 로그만 입력 기능 없이 표시하며,
  카드별 병렬 작업의 로그가 섞이지 않는다 (REQ-008). 필터, 복사, 지우기,
  자동 스크롤과 크기 제한을 제공한다.
- update 실행: 카드의 update 버튼을 활성화하고 Git 카드에는 `submodule 함께 갱신`
  option을 제공한다 (REQ-006, ADR-003). 진행 표시와 취소, 오류·결과 표시를
  연결한다.
- switch dialog: remote-first branch group과 SVN 허용 URL 후보를 표시하고,
  검증 실패 시 오류를 표시하며 명령을 실행하지 않는다 (REQ-007). tracking branch
  생성은 명시적 확인 후에만 진행한다.
- 명령 전후 자동 상태 갱신: 변경 작업 완료 후 provider가 재조회한 로컬 상태를
  카드에 반영하고, 이어서 remote-first refresh를 자동 실행한다 (plan 5.2/5.3).
- switch 검증 실패나 인증 프롬프트 때문에 앱이 멈추지 않는다 (완료 조건).
  비대화형 실행과 취소는 단계 3~4의 계약을 그대로 사용한다.

## 3. 단계 7에서 하지 않는 일

- 탐색 후보 미리보기·선택 등록 dialog. `.version-list` 생성 기능(2026-08-18)이
  발견 저장소 전량 등록을 이미 제공하므로, 후보 선택 UX는 별도 검수 항목으로
  남긴다.
- 환경설정 화면 (Git/SVN 실행 파일 경로 편집 UI). `settings` 값 자체는 단계 4
  경로로 이미 동작한다.
- Windows file association 등록 (단계 8).
- 실제 `svn.exe`·네트워크 원격·인증 필요 동작의 실측 검증 (단계 8).
- 파일 로그. 카드별 메모리 ring buffer만 제공하고 보존 기간·저장 위치는 필요할 때
  정한다 (plan 3.9).
- 정렬 전환 UI와 스크롤 막대 Page Up/Down (기존 후속 항목 유지).

## 4. 설계 확정 사항

사용자 위임에 따라 아래 제안을 확정값으로 채택한다. 근거를 함께 기록한다.

### 4.1 로그 소유권과 전달 경로

- **카드별 ring buffer는 logic thread가 소유한다** (ADR-004: 유일한 mutable app
  state). worker는 로그를 직접 쓰지 않고 `operation_log_event`로 logic inbox에
  보낸다. 같은 카드의 변경 작업은 lane에서 직렬화되므로 카드 안에서 record 순서가
  보장된다.
- record의 sequence는 logic이 append 시점에 카드별로 부여한다. 표시 record는
  `시각, 종류(작업 수명 주기/stdout/stderr), 심각도, 메시지`를 담는다.
- worker의 process 출력은 provider의 `log` sink 자리에 **배치 sink**를 꽂아
  전달한다. 16 record마다, 그리고 명령 종료 시 flush해 채널 부담을 줄인다.
- ring buffer 상한은 **카드당 1,000 record**다. 초과 시 오래된 record부터
  제거하고, 제거가 일어났다는 사실을 뷰가 알 수 있게 유지 개수와 총 발행 수를
  분리해 둔다. 상수는 한 곳(`presentation/list_metrics.h`와 같은 방식)에 모은다.
- view snapshot에는 **선택 카드의 로그만** 담는다 (REQ-008). 필터 적용 후의
  record 목록과 스크롤 상태를 함께 담아 렌더러가 그대로 그린다.

### 4.2 로그 대상

- 변경 작업(update, switch)의 수명 주기(시작, 차단, 실패, 취소, 성공)와 process
  출력(stdout/stderr)을 로그에 남긴다.
- 조회(query_local, refresh)는 로그에 남기지 않는다. 결과가 카드 상태로 이미
  표시되고, 주기적 조회가 로그를 밀어내면 정작 변경 작업 진단이 사라진다.
- switch 후보 조회는 dialog 상태로 표시하고 로그에는 실행된 switch만 남긴다.

### 4.3 하단 로그 뷰

- 카드를 선택하면 목록 아래에 로그 pane이 나타난다. 높이는 논리 160px 고정이며
  `compute_list_layout`이 pane 유무를 반영해 목록 viewport를 계산한다.
- 헤더에 카드 이름, 스트림 필터 토글(전체/출력/오류), 자동 스크롤 토글, 복사,
  지우기 버튼을 둔다. 모든 버튼은 `button_element`이며 액션은 intent 또는
  `ui_command`를 반환한다.
- **복사**는 UI thread 전용 `ui_command::copy_selected_log`다. UI thread가 현재
  view snapshot의 표시 중 로그를 CRLF로 이어 Win32 클립보드에 넣는다 (클립보드는
  platform adapter에 격리).
- **지우기**는 `clear_log_intent { id }`로 logic이 해당 카드 buffer를 비운다.
- 자동 스크롤은 기본 켜짐이다. 로그 영역에서 휠로 위로 올리면 꺼지고, 토글이나
  맨 아래 도달로 다시 켜진다. 스크롤 상태는 logic이 소유한다 (선택 카드 전환 시
  초기화).
- 로그가 없으면 pane에 한국어 empty 안내를 표시한다. 진행 표시(`\r` progress
  record)는 접지 않고 그대로 줄로 표시하되 `progress` 표식을 유지해 후속 개선
  여지를 남긴다.

### 4.4 update 실행

- update 버튼은 카드가 `ready`·`warning`(위험 아님) 상태이고 busy가 아닐 때
  활성화한다. 실제 차단 판정은 provider의 preflight가 실행 직전에 다시 한다
  (plan 3.5). 버튼 활성화는 편의일 뿐 보호 정책의 근거가 아니다.
- **Git 카드**의 update 버튼은 in-app 확인 overlay를 연다: `submodule 함께 갱신`
  체크(기본 off, ADR-003), 실행·취소 버튼. overlay 상태는 logic이 소유하고 view
  snapshot에 담긴다 (Win32 모달을 쓰지 않아 UI thread가 멈추지 않고, 불변 tree
  구조와 test 전략을 유지한다).
- **SVN 카드**는 option이 없으므로 확인 없이 곧바로 실행한다.
- 실행 중에는 카드 update 버튼이 **중지 버튼**(codicon `stop-circle`)으로 바뀌어
  `cancel_operation_intent { id }`를 보낸다. logic은 변경 작업마다 별도
  `process_cancellation_source`를 만들어 요청에 싣고, 취소 시 그 source만
  신호한다. 앱 종료는 진행 중인 모든 변경 작업 source를 함께 취소한다.
- 변경 작업 결과는 `change_completed_event`로 돌아온다. logic은
  `repository_change_result.snapshot`(실행 직후 재조회)을 카드에 반영하고,
  성공·실패와 관계없이 **자동 refresh**(remote-first)를 이어 실행한다 (plan
  5.2의 8). 차단·거부 사유는 한국어 메시지로 로그와 카드 진단에 남긴다.
- 변경 작업 중 같은 카드의 refresh·update·switch 요청은 시작하지 않는다 (카드
  lane 직렬화와 별개로 logic이 busy로 차단해 대기열 폭주를 막는다).

### 4.5 switch dialog

- switch 버튼은 in-app overlay dialog를 연다. 열면 곧바로
  `query_switch_candidates` 작업을 제출하고 "후보 조회 중"을 표시한다.
- 후보 목록은 **원격 브랜치 group을 위에, local branch를 아래에** 표시한다
  (REQ-007, plan 3.3). SVN은 문서의 `svn_switch_targets` 허용 목록만 표시한다.
  `stale` 후보는 "이번에 갱신되지 않음" 표식을 단다.
- 후보를 선택하면 dialog가 즉시 판정 가능한 검증(현재 참조와 동일, tracking
  branch 필요 여부)을 표시한다. tracking branch가 필요한 후보는 확인 문구와 함께
  **생성 확인 단계**를 거쳐 `tracking_branch_confirmed`를 켠 뒤에만 실행을
  허용한다 (단계 4 계약).
- 확인 버튼은 후보가 선택되고 검증 메시지가 없을 때만 활성화된다. 실행하면
  provider `switch_to`가 실행 직전 재검증을 수행하고, 거부 시 어떤 process
  request도 만들지 않으며 (`executed == false`, 단계 4 보장) dialog가 거부
  사유를 한국어로 표시한다. 이것이 REQ-007의 "검사와 실행 사이 상태 변경 방어"다.
- dialog가 열린 동안 후보 조회·전환 실행이 진행되어도 UI·input thread는 멈추지
  않는다. Esc 또는 취소 버튼이 dialog를 닫고, 진행 중 전환 실행은 카드 로그에서
  계속 추적된다.
- dialog 상태(`switch_dialog_view_model`)는 logic이 소유하고 view snapshot에
  담긴다: 대상 카드, 조회 중 여부, 후보 목록, 선택 index, 검증·거부 메시지,
  tracking 확인 대기 여부, 실행 중 여부.

### 4.6 메시지·작업 확장

| 추가 | 내용 |
| --- | --- |
| `operation_kind::update` | preflight 재조회 포함 update 실행. 카드 lane에서 직렬화 |
| `operation_kind::query_switch_candidates` | remote-first 후보 조회 |
| `operation_kind::switch_to` | 실행 직전 재검증 포함 전환 실행 |
| `operation_log_event` | worker → logic 로그 record 배치 |
| `change_completed_event` | update/switch 결과 (`repository_change_result`) |
| `switch_candidates_event` | 후보 조회 결과 (`switch_candidate_result`) |
| intent | `request_update_intent`(option 포함), `begin_switch_intent`, `select_switch_candidate_intent`, `confirm_switch_intent`, `cancel_switch_dialog_intent`, `confirm_tracking_branch_intent`, `cancel_operation_intent`, `clear_log_intent`, `set_log_filter_intent`, `set_log_auto_scroll_intent`, `log_scroll_intent`, update overlay용 open/option/confirm/cancel |

- `operation_request`에 `update_options`와 `switch_candidate`(실행 대상)를
  optional로 싣는다. late event 폐기는 기존 generation·operation id 정책을
  그대로 쓴다.
- 변경 작업 event는 조회 event와 마찬가지로 어떤 실패에서도 final event를 보내는
  executor 규칙을 따른다.

### 4.7 UI element 확장

- `ui_element_kind`에 로그 pane(본문·헤더 버튼들), update overlay, switch dialog
  (배경, 항목, 버튼) 항목을 추가한다.
- overlay·dialog는 root의 마지막 자식으로 추가해 카드 위에 그려지고, 배경 클릭은
  닫기다. 카드 목록의 hit test와 겹치지 않도록 dialog가 보일 때는 전체 화면을
  덮는 배경 element가 클릭을 흡수한다.
- update/switch 버튼 활성화는 `build_ui_tree`에서 액션 등록으로 구현한다
  (docs/ui-element-design.md의 예정 경로).

## 5. 체크포인트

| 구간 | 내용 | 의존 |
| --- | --- | --- |
| `S7-D1` | 로그 model(record·ring buffer)과 변경 작업 배관: app_messages 확장, executor의 update·switch·후보 조회 실행과 로그 sink, logic의 로그 소유·변경 작업 상태 기계·per-operation 취소·자동 refresh. UI 없음 | 없음 |
| `S7-D2` | 하단 로그 뷰: layout 확장, log pane element, 필터·복사·지우기·자동 스크롤, 클립보드 adapter | D1 |
| `S7-D3` | update 실행 UI: 카드 버튼 활성화·중지 버튼 전환, Git 확인 overlay(submodule option), 진행·결과 표시 | D1, D2 |
| `S7-D4` | switch dialog: 후보 group 표시, 선택·검증·tracking 확인, 실행과 거부 표시 | D1, D2 |
| `S7-V1` | 단계 7 최종 검증: 전체 matrix, 병렬 로그 격리 stress, 완료 조건 대조, 문서 갱신 | 전부 |

각 구간은 코드와 test를 함께 담고, 검증 문서(`docs/verification/2026-08-18-stage-7-d*.md`)를
남긴 뒤 커밋한다.

## 6. 테스트 계획

- `S7-D1`: ring buffer 상한·sequence·지우기, 로그 배치 sink flush 규칙,
  executor의 update/switch/후보 경로(fake runner로 명령 조립과 final event 보장),
  logic의 변경 작업 상태 전이(busy 차단, 취소, late event 폐기, 자동 refresh
  연쇄), **서로 다른 두 카드의 병렬 변경 작업 로그가 섞이지 않는 것**.
- `S7-D2`: layout(선택 시 pane 자리, 목록 viewport 축소), 필터·자동 스크롤 규칙,
  복사 명령의 대상 텍스트 구성, tree 빌드(pane 표시·버튼 액션·비활성).
- `S7-D3`: update 버튼 활성 조건, Git overlay 열기·option·확인·취소, SVN 즉시
  실행, 실행 중 중지 버튼 전환과 취소 intent, 실제 임시 Git 저장소의 executor
  update end-to-end (단계 4 fixture 재사용).
- `S7-D4`: dialog 상태 기계(조회 중, 후보 group 순서, 선택·검증, tracking 확인,
  실행·거부 표시), 검증 실패 시 process request 0개(fake runner 기록으로 단정),
  실제 임시 Git 저장소의 switch end-to-end.
- `S7-V1`: 전체 CTest matrix (VS2022 Debug/Release, VS2026 Debug), `/analyze`,
  ASan, format/style, 단일 exe install과 설치본 smoke, `--repeat until-fail:3`.

## 7. 단계 7 완료 조건

- 서로 다른 카드의 병렬 작업 로그가 섞이지 않고, 선택 카드의 실행 내용과 결과만
  하단 로그 뷰에서 추적할 수 있다 (REQ-008).
- update가 비대화형으로 실행되고 진행, 종료 코드, 출력이 로그에 남는다 (REQ-006).
  submodule option이 동작한다.
- switch dialog가 remote-first 후보를 표시하고, 검증 실패 시 오류를 표시하며
  명령을 실행하지 않는다 (REQ-007). tracking branch는 명시적 확인 후에만
  만들어진다.
- 변경 작업 전후에 카드 상태가 자동으로 갱신된다.
- switch 검증 실패나 인증 프롬프트 때문에 앱이 멈추지 않는다. 취소가 동작한다.
- logic thread만 app state(로그 buffer 포함)를 변경한다 (REQ-015).
- 전체 build/test/analyze/format/install matrix가 통과한다.
