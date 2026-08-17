# 변경 이력

## 2026-08-17 - 단계 5 `S5-D1` 탐색 계약·판정과 열거 구현 및 test

### 사용자 지시

- `S5-P0`을 승인하고 진행한다. 단계 5부터는 production code와 test code 작성을 한 검수 구간으로 같이 진행한다.

### 반영 내용

- `docs/stage-5-plan.md` 7장의 체크포인트를 `S5-P0`, `S5-D1`~`S5-D3`, `S5-V1`의 5개로 개정하고 10.0에 검수 결과를 기록했다. `CODE`/`TEST` 분리와 별도 `FIX` 체크포인트를 두지 않으며, 검수에서 발견된 결함은 해당 구간의 재제출로 처리한다.
- `domain/discovery.h/.cpp`를 추가했다. 후보·결과 값 type, 제외 사유 7종과 **표식 판정 순수 함수** `classify_discovery_markers`다. 판정 순서는 확인 실패 → 메타데이터 충돌 → `.git` 디렉터리 → `.git` 파일 → `.svn` → bare 휴리스틱 → 비저장소다.
- bare 휴리스틱은 `HEAD`+`objects`+`refs` 세 표식이 모두 있을 때만 인정하고 kind는 `git`으로 남긴다. 목록에 "Git bare 저장소라서 제외"라는 정보가 필요하기 때문이다.
- 후보 정렬 `discovery_candidate_before`를 추가했다. 이름 ASCII 대소문자 무시 → code unit → 절대 경로 순서로 filesystem 열거 순서와 무관하게 결정적이다.
- `application/directory_enumerator.h`를 추가했다. 깊이 1 열거 계약이며 항목마다 디렉터리·reparse point 여부를 담는다. UTF-8로 표현할 수 없는 이름은 조용히 버리지 않고 `unreadable_name_count`로 남긴다.
- `platform/win32/win32_directory_enumerator.h/.cpp`를 추가했다. `FindFirstFileExW` 기반이며 `\\?\` 확장 접두어로 `MAX_PATH` 초과 경로를 지원한다. 상대 경로는 OS 호출 없이 거부하고, 패턴 불일치(`ERROR_FILE_NOT_FOUND`)는 빈 목록으로, 반복 중간 실패는 실패로 보고한다.
- 새 static library `gitman_discovery`를 추가했다. `gitman_domain`·`gitman_vcs`·`gitman_workspace` PUBLIC, `gitman_win32_platform` PRIVATE이며 실행 파일에는 링크하지 않는다.
- `tests/discovery_domain_tests.cpp`(11개)와 `tests/directory_enumerator_tests.cpp`(8개), `tests/helpers/discovery_test_doubles.*`(fake enumerator)를 추가했다. bare 진부분집합 7종 전수, 충돌·우선순위, 정렬 동률의 양방향, 실제 filesystem의 비ASCII 이름과 실패 경로를 고정한다.
- 전체 CTest가 393에서 **412**로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 412/412 통과했다. `/analyze` 무경고, Debug 3회 반복 통과, `gitman_format_check` 238개 파일 통과.
- 개발 중 formatter 위반 1건(`find_handle` 생성자 초기화 목록)을 formatter 결과 수용으로 해소했다. production 결함은 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-5-d1.md`에 기록했다.

### 영향 요구사항

- REQ-004, REQ-009, REQ-010, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- `S5-D1`은 사용자 검수 대기 상태다.
- 승인 전에는 `S5-D2`의 `discovery_service` 탐색 실행과 junction 통합 test를 작성하지 않는다.

## 2026-08-17 - 단계 5 `S5-P0` 구현 계획 수립

### 사용자 지시

- `S5-P0`을 진행한다. 진행 원칙에 따라 이 지시로 `S4-V1`과 단계 4 전체를 최종 승인한 것으로 기록한다.

### 반영 내용

- `docs/stage-5-plan.md`를 작성했다. 단계 5 탐색과 등록의 범위, 설계 제안, 11개 체크포인트와 test 전략을 담는다.
- 탐색은 **프로세스를 만들지 않는 표식 기반 판정**으로 제안했다. `.git` 디렉터리/파일, `.svn` 디렉터리, bare 휴리스틱(`HEAD`+`objects`+`refs`)으로 종류를 정하고, 정확한 상태 판정은 등록 후 단계 4 provider가 담당한다.
- `docs/requirements.md` 6장이 단계 5로 이관한 링크·worktree·bare 세부 범위의 확정안을 제안했다. linked worktree와 submodule(`.git` 파일)은 후보 허용, bare는 표시 후 제외, reparse point는 판정 없이 제외하되 목록에 표시한다.
- 중복 판정은 단계 2 `project_path_resolver`, 저장은 단계 2 `project_store`와 revision token을 그대로 주입받아 재사용한다. 새 정규화·저장 장치를 만들지 않는다.
- 등록 규칙을 제안했다. id는 디렉터리 이름과 중복 시 숫자 접미사, 경로는 절대 경로, `vcs_hint`는 판정 종류, 부적격 후보가 섞인 선택 목록은 부분 등록 없이 전체 거부한다.
- 새 계약은 깊이 1 열거의 `directory_enumerator` 하나이며 Win32 구현은 새 static library `gitman_discovery`에 둔다. 표식 확인은 단계 4의 `vcs_file_probe`, 취소는 단계 3의 `process_cancellation_token`을 재사용한다.
- 판정이 표식 기반이므로 SVN 경로도 `svn.exe` 없이 통합 검증이 가능하다는 test 전략을 기록했다.
- 체크포인트는 `S5-P0`, `S5-D1`(계약·판정) / `S5-D2`(탐색 실행) / `S5-D3`(선택 등록)의 `CODE`/`TEST`/`FIX` 3분할과 `S5-V1`로 총 11개다.
- `S5-V1` 종료 보고에서 ADR-004 범용 메시지 구조의 단계 6 사전 설계 게이트를 다시 알리도록 계획에 명시했다.
- `docs/plan.md`의 지시 이력과 단계 상태, `docs/requirements.md`의 문서 상태, `docs/handoff.md`의 현재 체크포인트와 진행 원장을 갱신했다.
- production code와 test는 변경하지 않았다.

### 영향 요구사항

- REQ-001, REQ-004, REQ-012, REQ-016
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- `S5-P0`은 사용자 계획 검수 대기 상태다. 특히 `docs/stage-5-plan.md` 10.1의 확정 필요 사항 8개에 대한 결정이 필요하다.
- 승인 전에는 `S5-D1-CODE`의 도메인 모델, `directory_enumerator` 계약과 판정 규칙을 작성하지 않는다.
- ADR-004의 범용 메시지 구조는 단계 6 구현 전 별도 설계 승인을 받아야 한다. 이 차단 조건은 그대로 유효하다.

## 2026-08-17 - 단계 4 `S4-V1` 최종 검증

### 사용자 지시

- `S4-D6-TEST`를 승인하고 무결함 `S4-D6-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.

### 반영 내용

- `build/vs2022`를 삭제하고 preset으로 다시 configure한 뒤 전체 검증 matrix를 수행했다.
- VS2022 Debug/Release와 VS2026 Debug의 전체 CTest가 각각 **393/393** 통과했다. VS2022 `/analyze`는 무경고로 통과했다.
- aggregate `gitman_format_check`, `gitman_source_style`, `gitman_assets_checksum`, `git diff --check`와 `git diff --cached --check`가 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- Release install 결과가 `bin/gitman.exe` 단일 파일임을 확인했다. 크기 6,255,616 byte, PE 의존성은 Windows 시스템 DLL 9개뿐이며 VC runtime과 프로젝트 DLL이 없다. 설치본 renderer smoke test 4종이 모두 종료 코드 0이다.
- **7개 저장소 동시 조회 stress를 3회 수행했다.** 실제 `git.exe`로 만든 동기·behind·ahead·dirty·detached·remote 없음·없는 경로 저장소를 4개 스레드에서 조회했고, 회차마다 조회 56회에서 기대값 불일치 0, switch 후보 합계 88로 동일했다. 프로세스 handle은 71로 유지되어 실행별 누수가 없다.
- 계획 9장의 완료 조건 14개를 하나씩 대조해 검증 기록 7장에 결과를 남겼다.
- CTest 수가 단계 3 종료 및 감사 수정 시점의 139개에서 **393개**로 254개 늘었다. 태그별로 `[git]` 136개, `[svn]` 49개, `[switch]` 38개, `[update]` 21개, `[integration]` 29개다.
- 실제 `svn.exe` 실행 경로가 미검증으로 남는다는 사실과 실제 환경 연결 시 확인할 네 가지를 검증 기록 9장에 명시했다.
- 결과를 `docs/verification/2026-08-17-stage-4.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006~REQ-014, REQ-017
- NFR-005~NFR-009, NFR-011

### 다음 작업 제한

- 단계 4는 **사용자 최종 검수 대기** 상태다.
- 승인 전에는 단계 5의 탐색 및 등록을 시작하지 않는다.
- ADR-004의 범용 메시지 구조는 단계 6 구현 전 별도 설계 승인을 받아야 한다. 이 차단 조건은 그대로 유효하다.

## 2026-08-17 - 단계 4 `S4-D6-TEST` switch test 작성

### 사용자 지시

- `S4-D6-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/switch_validation_tests.cpp`를 추가했다. 검증 서비스의 규칙 test 13개다. 프로세스를 만들지 않는 순수 함수라 거부 사유와 그 우선순위를 실제 저장소 없이 전수 단정한다.
- `tests/git_switch_tests.cpp`를 추가했다. 후보 목록 조립과 provider의 후보 조회 및 전환 test 20개다.
- **명령 미생성을 요청 기록으로 직접 단정했다.** 빈 대상과 도구 부재는 0개, 저장소 아님은 1개, 검증 거부는 조회 5개에서 끝나며 어느 경로에도 `switch` 명령이 없다.
- 정상 경로의 명령 수와 순서를 고정했다. Git은 8개(재조회 2 → `remote` → `for-each-ref` → `worktree list` → `switch` → 재조회 2), SVN은 18개다. Git 전환 경로에 `fetch`가 **0개**인 것도 함께 단정한다.
- 후보 목록 규칙을 전수 고정했다. 정렬, 심볼릭 ref 제외, 여러 remote의 같은 이름, stale 표시, `/`가 든 remote 이름, 지워진 remote가 남긴 tracking ref다.
- **local 후보 중복 제거의 양쪽 경우를 모두 단정했다.** remote 후보로 도달할 수 있으면 넣지 않고, upstream이 다른 remote를 가리키면 남긴다. 뒤의 규칙이 없으면 그 branch로 전환할 방법이 사라진다.
- 검증 거부 사유 8종과 그 우선순위를 단정했다. 사유를 하나씩 없애면 다음 사유가 나오는 것으로 순서 자체를 고정했다.
- 작업 트리 위험 판정 5종(dirty, 충돌, **미상**, 진행 중 작업, `index.lock`)을 전수 확인했다.
- tracking branch는 확인 전 거부와 확인 후 승인을 쌍으로 단정하고, **확인했더라도 실제 차단 사유가 우선**하는 것을 함께 고정했다.
- SVN은 URL 형식 14종, 허용 목록, 형식 오류 메시지, 현재 위치, 작업 복사본 상태와 저장소 대조 7종을 다룬다. **대상 값을 못 읽은 것과 값이 다른 것을 구분**하고 현재 값을 모르면 통과시키지 않는 것도 단정했다.
- 전환 실패 분류가 로캘 독립 신호로만 이뤄지는 것을 timeout, 취소, 한국어 미분류 실패로 확인했다.
- `tests/git_integration_tests.cpp`에 실제 Git test 5개를 추가했다. **확인 후 실제로 tracking branch를 만들어 전환하고**, 확인 전에는 branch가 만들어지지 않으며, 실제 `git worktree`가 잡은 branch와 dirty 작업 트리를 거부하는 것을 확인한다.
- 명령 test와 파서 test를 각 파일에 추가했다. `for-each-ref` 형식, `worktree list`, `switch`의 금지 인자 6종, tracking 생성 인자 순서, `svn switch`의 금지 인자 3종과 원격 조회 한도다.
- 전체 CTest가 338에서 **393**으로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 393/393 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D6-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-4-d6-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013, REQ-014
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D6-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D6-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-V1`의 단계 4 최종 검증과 검증 문서를 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D6-CODE` switch 구현

### 사용자 지시

- `S4-D5-TEST`를 승인하고 무결함 `S4-D5-FIX` 생략을 확인한 뒤 `S4-D6-CODE`를 진행한다.

### 반영 내용

- `application/switch_validation_service.h/.cpp`를 추가했다. Git과 SVN의 전환 검증 규칙 전체를 프로세스도 filesystem도 쓰지 않는 순수 함수로 모았다. **검증 실패 시 명령을 만들지 않는다**는 REQ-007 수용 기준이 provider 한 곳에서만 지켜지면 되게 하려는 분리다.
- `infrastructure/git_command_builder.*`에 `for-each-ref`, `worktree list --porcelain`, `switch --no-guess`와 tracking branch 생성 요청을 추가했다. `--discard-changes`, `--merge`, `--force`는 쓰지 않는다.
- `for-each-ref` 형식에 `%(symref)`를 넣어 `refs/remotes/<remote>/HEAD` 같은 심볼릭 항목을 이름 규칙이 아니라 값으로 제외한다. 계획 4.8의 형식에 한 칸을 더한 것이다.
- `switch`와 tracking 생성 모두 `--`로 인자를 끊는다. 호스트 Git 2.52.0에서 `switch`가 `--`를 받아들이는 것과 `--track` 뒤의 완전한 ref가 옵션 값이 아니라 시작 지점으로 해석되는 것을 실측했다.
- `infrastructure/git_status_parser.*`에 `parse_git_reference_list`와 `parse_git_worktree_branches`를 추가했다. ref 이름에는 TAB이 들어갈 수 없어 TAB 구분 형식의 경계가 흔들리지 않는다.
- `build_git_switch_candidates`를 순수 함수로 추가했다. remote 후보를 먼저, local branch를 뒤에 두고, 같은 이름이 여러 remote에 있어도 합치지 않으며 자동으로 고르지 않는다.
- **remote 후보로 도달할 수 있는 local branch는 목록에 두 번 넣지 않는다.** 반대로 upstream이 그 remote와 다른 local branch는 remote 후보로 도달할 수 없으므로 그대로 남긴다. 계획 4.8의 "local-only"를 그대로 읽으면 이런 branch로 전환할 방법이 사라진다.
- 후보를 새로 고칠 remote는 `preferred_remote` → `origin` → 유일한 remote 순서로 고른다. 좁혀지지 않으면 **fetch하지 않고** 이미 받아 둔 tracking ref로 목록을 만든 뒤 `stale`로 알린다. upstream은 현재 branch에 종속된 값이라 후보 조회 기준으로 쓰지 않는다.
- fetch가 실패해도 목록 자체는 만든다. 원격을 새로 고치지 못한 것과 후보를 전혀 알 수 없는 것은 사용자가 할 일이 다르다.
- Git 검증 순서를 정했다. 대상 없음 → remote 미지정 → 저장소 조회 불가 → 이미 대상 → 다른 worktree 사용 중 → 작업 트리 위험 → tracking 충돌 → 확인 요구다. **확인 요구를 가장 뒤에 둔다.** 오류가 아니라 확인 요구이므로 실제 차단 사유가 있으면 그것을 먼저 알려야 한다.
- `switch_candidate`에 `tracking_branch_confirmed`를 추가했다. 후보 조회는 채우지 않고 dialog가 확인을 받은 뒤에만 켠다. 계약에 별도 인자가 없어 확인 여부를 실을 자리가 필요했고, 이 값이 없으면 "확인 후 생성"과 "무조건 생성" 중 하나만 구현할 수 있다.
- upstream이 **없는** local branch는 tracking 충돌로 보지 않는다. 이때 전환은 upstream을 건드리지 않고 그 branch로 옮기기만 한다.
- `switch_rejection::repository_unavailable`을 추가했다. 조회 자체가 안 되는 상태를 `working_tree_unsafe`로 보고하면 카드에 잘못된 사유가 뜬다.
- `switch_to`는 **재조회 → 재검증 → 실행 → 사후 재조회** 순서다. dialog 검증과 실행 사이의 상태 변경을 방어한다. 정상 경로의 명령 수는 Git이 8개, SVN이 16개다.
- 실행 직전에는 fetch하지 않는다. 전환은 이미 받아 둔 ref로만 하며 `--no-guess`가 목록에 없던 대상으로의 암묵 전환을 막는다.
- `infrastructure/svn_*`에 `svn switch`와 URL 대상 원격 `info --show-item`을 추가했다. `--ignore-ancestry`, `--force`, `--accept`는 쓰지 않는다.
- **SVN 후보 조회는 process request를 하나도 만들지 않는다.** 후보가 문서의 `svn_switch_targets`뿐이고 저장소 layout을 자동으로 가정하지 않기 때문이다. 형식을 해석할 수 없는 값은 후보에서 빼고 warning 진단으로 남긴다.
- SVN 검증은 허용 목록·형식·현재 위치·작업 트리를 **네트워크보다 먼저** 본다. 어차피 실패할 전환에 원격을 건드릴 이유가 없다. 계획 4.8의 나열 순서를 바꾼 것이며 판정 결과는 같다.
- 저장소 root와 UUID는 양쪽 값이 모두 있고 같을 때만 통과시킨다. 전환은 되돌리기 어려운 동작이라 확인하지 못한 것을 안전하다고 보지 않는다.
- `tests/`의 "아직 구현하지 않은 동작" test 2개를 **빈 대상은 조회 없이 거부한다**는 test로 좁혔다. 원래 단정은 더 이상 사실이 아니지만 REQ-007 수용 기준은 남겨야 한다. 새 test는 작성하지 않았다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 338/338 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 228개 항목을 확인하고 삭제했다. 실제 Git으로 **확인 후 tracking branch를 실제로 만들어 전환했고**, 확인 전 거부·기존 branch 복귀·재선택 거부·다른 worktree 점유 거부·dirty 거부를 모두 확인했다.
- 결과를 `docs/verification/2026-08-17-stage-4-d6-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013, REQ-014
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D6-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D6-TEST`의 후보 정렬과 거부 사유 test를 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D5-TEST` update test 작성

### 사용자 지시

- `S4-D5-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/git_update_tests.cpp`를 추가했다. 차단 사유 matrix, pull 성공·실패, submodule 경로 test 17개다.
- 차단 사유를 전수 확인했다. 저장소 아님, 충돌, 진행 중 작업, `index.lock`, detached, dirty, **미상**, diverged와 통과 3종(`behind`, `up_to_date`, 원격 미확인)이다.
- 사유가 여럿일 때 하나씩 사라질 때마다 다음 사유가 나오는 것으로 **우선순위 자체를 고정**했다.
- provider 층의 모든 차단 경로에서 **`pull` 명령이 0개**임을 요청 기록으로 직접 단정했다.
- 실행 경로는 명령 수와 순서로 고정했다. 기본 6개(조회 2 → remote → pull → 재조회 2), submodule 옵션 시 8개(조사와 갱신이 pull 앞뒤로 들어감)다.
- 실패해도 사후 재조회를 수행하는 것, timeout이 차단이 아니라 실패인 것, 인증 실패가 한국어 메시지에서도 분류되는 것을 단정했다.
- `tests/git_integration_tests.cpp`에 실제 Git test 4개를 추가했다. 뒤처진 저장소를 **실제로 fast-forward**하고, **원격 이력이 다시 쓰인 저장소에서 `pull --ff-only`가 merge를 만들지 않고 실패**하며, dirty·diverged·detached·remote 없음이 차단되고, 실제 submodule이 off/on에 따라 다르게 처리되는 것을 확인한다.
- 이력 재작성 test가 이 구간의 핵심이다. 사전 검사만으로 걸러지지 않는 경우에 `--ff-only`가 마지막 방어선으로 동작하는 것을 실제 Git으로 보여 준다.
- `tests/svn_repository_provider_tests.cpp`에 SVN 사전 검사와 update test 6개를 추가했다. 판정할 수 없는 switched·mixed가 update를 막지 않는 것도 함께 고정했다.
- 명령 test와 `submodule status` 파서 test를 각 파일에 추가했다.
- `git_repository_fixture`의 준비 명령에 `-c protocol.file.allow=always`를 추가했다. 임시 디렉터리의 로컬 경로를 submodule 원본으로 쓰기 위한 것이며 **production 명령은 이 설정을 만들지 않는다.**
- 전체 CTest가 306에서 **338**로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 338/338 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D5-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-4-d5-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D5-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D5-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D6-CODE`의 switch 후보와 검증을 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D5-CODE` update 구현

### 사용자 지시

- `S4-D4-TEST`를 승인하고 무결함 `S4-D4-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.

### 반영 내용

- `infrastructure/git_command_builder.*`에 `pull --ff-only`, `submodule status --recursive`, `submodule update --init --recursive` 요청을 추가했다. `--force`, `--rebase`, `--autostash`는 쓰지 않는다.
- pull은 remote와 branch를 명시하고 앞에 `--`를 둔다. 설정에 따라 다른 대상이 당겨지지 않고 `-`로 시작하는 이름이 인자로 해석되지 않는다. 호스트 Git 2.52.0으로 실측했다.
- `infrastructure/git_status_parser.*`에 `parse_git_submodule_status`를 추가했다. `<표시><커밋 ID> <경로> (<describe>)` 형식을 읽고 describe 접미사를 떼어 낸다.
- `evaluate_git_update_preflight`와 `evaluate_svn_update_preflight`를 순수 함수로 추가했다. 보호 정책 자체를 프로세스 없이 검증할 수 있다.
- **사전 검사는 지금 다시 조회한 상태로 한다.** 카드가 들고 있는 값으로 판단하면 그 사이에 바뀐 저장소에서 명령이 나갈 수 있다.
- 차단 사유 우선순위를 정했다. 도구 부재 → 저장소 아님 → 충돌 → 진행 중 작업 → `index.lock` → detached → dirty → diverged → 대상 없음이다. `working_tree_state::unknown`을 dirty와 함께 막는다.
- **SVN의 switched·mixed는 값이 있을 때만 차단한다.** `svnversion`이 없어 판정할 수 없다는 이유로 update를 영영 막으면 도구 구성 문제 하나로 기능이 사라진다. 조회가 이미 warning을 남긴다.
- submodule 옵션이 켜지면 pull 전에 `submodule status --recursive`로 조사하고 충돌(`U`)이나 커밋 불일치(`+`)가 하나라도 있으면 **parent pull을 시작하지 않는다.** 미초기화(`-`)는 `--init`이 처리하므로 위험으로 보지 않는다.
- `submodule update --init --recursive`는 parent pull이 **성공한 경우에만** 실행한다. 실패한 pull 뒤에 submodule을 옮기면 되돌리기 어려운 조합이 남는다.
- 계획 4.7의 "submodule dirty 검사"는 `git submodule status`가 내부 dirty를 보고하지 않아 충돌과 커밋 불일치로 좁혔다. 내부 dirty 검사는 단계 6~7에서 다시 본다.
- 성공과 실패 모두 실행 직후 로컬 상태를 다시 조회한다. 실행의 성공 여부와 조회 결과를 분리해 보고한다.
- `infrastructure/svn_*`에 `svn update`를 추가했다. `--accept`를 주지 않아 충돌을 자동으로 해결하지 않는다.
- `tests/`의 "아직 구현하지 않은 동작" test 2개에서 `update` 단정만 걷어냈다. 이번 구간이 구현한 동작이라 더 이상 사실이 아니다. 새 test는 작성하지 않았다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 306/306 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 109개 항목을 확인하고 삭제했다. 실제 Git으로 **원격이 앞선 저장소를 실제로 fast-forward했고** dirty·diverged·remote 없음·detached 저장소가 모두 차단되는 것을 확인했다.
- 결과를 `docs/verification/2026-08-17-stage-4-d5-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D5-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D5-TEST`의 차단 사유 matrix와 update 통합 test를 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D4-TEST` SVN 조회 test 작성

### 사용자 지시

- `S4-D4-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/svn_command_builder_tests.cpp`에 명령 test 6개를 추가했다. 네 명령의 인자와 한도, `--non-interactive` 위치, **`svnversion`에 공통 인자가 없음**, `--trust-server-cert`와 자격 증명 인자를 절대 만들지 않는 것을 단정한다.
- `tests/svn_output_parser_tests.cpp`에 파서 test 8개를 추가했다. 상태 문자 12종, switched·tree conflict 칸, 부가 설명 줄 무시, 무시·외부 항목 제외, `svnversion` 단일·범위·`M`/`S`/`P`·비작업복사본·잘못된 형식을 다룬다.
- 상태 칸 뒤 패딩이 하나 더 있어도 경로가 잘리지 않는 것을 test로 고정했다. 계획의 "고정 9칸" 대신 공백을 건너뛰는 구현의 근거다.
- `tests/fixtures/vcs/svn/`에 fixture 2개를 추가했다. 실제 출력을 캡처할 수 없어 **Apache Subversion 공식 문서의 출력 계약을 근거로 작성하고 출처와 미대조 사실을 파일 주석에 남겼다.** 주석은 `#`로 시작하며 test 도우미가 버린다.
- `tests/svn_repository_provider_tests.cpp`에 provider test 16개를 추가했다. 정상 조회의 명령 7개 순서, `svnversion` 부재와 해석 실패, `status` 실패, 원격 behind·up_to_date, 실패 분류 3종과 미구현 동작을 다루며 **명령 미생성을 요청 기록 수로 직접 단정한다.**
- 실패 분류 test는 모두 한국어 메시지에 SVN 오류 코드가 붙은 형태다. 번역된 메시지에도 코드가 남는다는 전제를 언어에 의존하지 않고 단정한다.
- `tests/svn_integration_tests.cpp`에 통합 test 2개를 추가했다. SVN이 없는 호스트에서 앱이 계속 동작하는 것은 **실제로 실행되고**, 실제 `svn.exe`를 쓰는 test는 skip된다. 두 test는 서로 배타적이라 SVN이 설치된 호스트에서는 반대로 동작한다.
- 실제 작업 복사본을 만들려면 `svnadmin`이 필요하다. 단계 4는 실행 파일 연결과 판정까지만 확인하고 실제 작업 복사본 통합 검증은 단계 8로 남긴다.
- 전체 CTest가 274에서 **306**으로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 306/306 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D4-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-4-d4-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-011, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D4-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D4-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D5-CODE`의 update 보호 정책과 실행을 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D4-CODE` SVN 조회 구현

### 사용자 지시

- `S4-D3-TEST`를 승인하고 무결함 `S4-D3-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.
- SVN은 실제 개발 시점에 쓰지 않는다. 나중에 프로덕션 환경에 **부품 끼워넣듯 최소 노력으로 적용할 수 있기만 하면 된다.**

### 반영 내용

- `infrastructure/svn_command_builder.*`에 `info --show-item`, 비verbose `status`, `svnversion`, 원격 리비전 요청을 추가했다.
- **`svnversion`에는 공통 인자를 붙이지 않는다.** `svn`과 다른 실행 파일이라 `--non-interactive`를 받지 않고, 그대로 붙이면 인자 오류로 실패한다. timeout과 인코딩 정책은 다른 명령과 같게 맞췄다.
- `infrastructure/svn_output_parser.*`에 값 한 줄 추출, 고정 칸 `status` 파서, `svnversion` 파서와 작업 트리 요약을 추가했다.
- `status` 경로는 앞 7칸(항목·속성·잠금·이력·switched·잠금 토큰·tree conflict)을 상태 칸으로 보고 그 뒤 공백을 모두 건너뛴 지점부터 읽는다. 계획의 "고정 9칸"보다 배포판별 패딩 차이에 강하다.
- 상태 칸이 모두 공백인 줄(`> moved from ...`)은 항목이 아니므로 건너뛴다. `I`(무시)와 `X`(외부 항목)는 어느 수에도 넣지 않는다.
- `infrastructure/svn_repository_provider.*`에 로컬 및 원격 조회를 구현했다. 구조와 실패 처리를 Git provider와 똑같이 맞춰 나중에 붙일 때 읽어야 할 새 개념이 없게 했다.
- mixed revision과 switched는 `svnversion`으로 판정한다. `svnversion`이 없거나 출력을 해석하지 못하면 조회를 막지 않고 `has_mixed_revision`을 비운 채 `status`의 switched 칸으로 보조 판정한다.
- 원격 조회는 `info --show-item url`로 현재 URL을 다시 물어본 뒤 원격 HEAD 리비전과 비교한다. SVN에는 `ahead`와 `diverged`가 없어 `behind`와 `up_to_date`만 나오고 `ahead_count`는 항상 0이다.
- 실패는 `S4-D1-CODE`의 분류기가 SVN `E<숫자>` 코드로 판정한다. 번역된 메시지에도 코드가 붙어 로캘에 의존하지 않는다.
- SVN이 없는 환경은 계속 정상 상태다. 도구 부재는 warning이고 어떤 명령도 만들지 않는다. 이 경로는 이 호스트에서 실제로 확인했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 274/274 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 115개 항목을 확인하고 삭제했다. 그중 실제 실행으로 확인한 SVN 경로는 미설치 감지뿐이다.
- 결과를 `docs/verification/2026-08-17-stage-4-d4-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-011, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D4-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D4-TEST`의 SVN fixture와 파서 test를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D3-TEST` remote-first 판정 test 작성

### 사용자 지시

- `S4-D3-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/git_remote_query_tests.cpp`를 추가했다. 대상 선택 matrix, 명령 순서, 실패 분류와 값 보존 test 16개다.
- 대상 선택은 upstream, `/`가 든 branch 이름, 가장 긴 remote 접두사, local branch upstream, preferred, preferred 부재, origin, 유일 remote, 모호, remote 없음, detached, branch 미상을 모두 단정한다.
- **네트워크를 쓰기 전에 끝나는 경로**를 요청 수로 직접 단정했다. 도구 부재·로컬 미준비·경로 소멸·detached는 0개, `local_only`와 모호는 1개, fetch 실패는 2개, ref 부재와 커밋 없음은 3개다.
- 실패 분류는 같은 실패의 영어 출력과 한국어 출력이 같은 분류를 내는지 쌍으로 단정한다. libcurl, OpenSSH, HTTP 상태와 미분류 실패, timeout, 취소를 다룬다.
- fetch가 실패해도 직전 로컬 비교, ahead 수, 작업 트리 상태와 이전에 성공한 `remote_checked_at`이 남는 것을 단정했다. 반대로 `remote_target_missing`에서는 비교 값을 지우는 것도 단정했다.
- `tests/git_integration_tests.cpp`에 실제 원격 비교 test 6개를 추가했다. 동기, ahead, behind, diverged, remote 없음, 원격 branch 부재, 도달 불가 URL과 비ASCII 왕복이다. 로컬 bare 저장소만 쓰며 네트워크에 접근하지 않는다.
- `behind`와 `diverged`는 원격을 건드리지 않고 clone을 `reset --hard HEAD~1`로 되돌려 만든다. 준비가 결정적이다.
- `tests/git_command_builder_tests.cpp`에 새 명령 4종의 인자와 한도 test를, `tests/git_status_parser_tests.cpp`에 remote 이름과 ahead/behind 파서 test를 추가했다.
- **한국어 Git 출력 인코딩을 실측해 기록했다.** 이 호스트의 시스템 ANSI code page는 949지만 Git for Windows 2.52.0에는 번역 catalog가 설치되어 있지 않아(`share/locale` 부재) `LANGUAGE`, `LC_ALL`, `LANG`을 어떻게 줘도 메시지가 영어다. Git이 되돌려 주는 비ASCII 내용은 UTF-8이라 `active_code_page_fallback`이 건드리지 않는다. 다른 호스트에는 번역본이 있을 수 있으므로 오류 분류는 계속 로캘 독립 신호만 쓴다.
- 전체 CTest가 246에서 **274**로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 274/274 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D3-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-4-d3-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009~REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D3-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D3-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D4-CODE`의 SVN 명령 조립과 파서를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D3-CODE` Git remote-first 판정 구현

### 사용자 지시

- `S4-D2-TEST`를 승인하고 무결함 `S4-D2-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.

### 반영 내용

- `infrastructure/git_command_builder.*`에 `remote`, `fetch --prune`, `rev-parse --verify --quiet`, `rev-list --left-right --count` 요청을 추가했다.
- `fetch`에는 `--`를 붙여 remote 이름이 옵션으로 해석되지 않게 했다. 반대로 `rev-parse`에는 `--`를 쓰지 않는다. 뒤의 값을 경로로 만들어 항상 실패하기 때문이며 호스트 Git 2.52.0으로 실측해 확정했다.
- `infrastructure/git_status_parser.*`에 `parse_git_remote_names`와 `parse_git_ahead_behind`를 추가했다.
- `infrastructure/git_repository_provider.*`에 `select_git_remote_target` 순수 함수와 `query_remote` 본문을 구현했다. 선택 순서는 ADR-003대로 upstream → `preferred_remote` → `origin` → 유일한 remote이며, 좁혀지지 않으면 **자동으로 고르지 않고** `remote_target_missing`으로 보고한다.
- upstream에서 remote 이름을 뗄 때 설정된 remote 중 가장 긴 접두사를 고른다. branch 이름에도 `/`가 들어갈 수 있어 첫 `/`로 자르면 `origin/feature/a/b`를 잘못 나눈다.
- `branch.<name>.remote = .`처럼 upstream이 local branch를 가리키면 원격 비교에 쓰지 않고 나머지 규칙으로 넘어간다.
- 지정한 `preferred_remote`가 저장소에 없으면 다음 규칙으로 진행하되 warning 진단을 남긴다. 지정한 값이 조용히 무시되면 사용자가 알 수 없다.
- detached HEAD와 remote가 없는 저장소에서는 **네트워크를 쓰지 않는다**. 각각 `remote_target_missing`과 `local_only`다.
- remote branch 존재 확인을 fetch **뒤**에 한다. 한 번도 fetch하지 않은 저장소에는 tracking ref가 없어 fetch 전에 확인하면 원격에 있는 branch를 없다고 오판한다. 계획 4.5의 4·5번 순서를 바꾼 것이며 판정 결과는 같다.
- fetch 실패는 로캘 독립 신호로 `offline`, `authentication_required`, `error`를 구분한다. 실패해도 작업 트리 상태, 마지막 성공 원격 확인 시각과 직전 로컬 비교 값을 지우지 않는다.
- 비교 대상 자체가 없다고 판정한 경우에는 이전 비교 값을 지운다. 유효하지 않은 비교를 남기면 카드가 잘못된 수를 계속 보여 준다.
- 커밋이 하나도 없는 저장소는 `HEAD`가 없어 대칭 차이를 계산할 수 없다. fetch와 ref 확인까지만 하고 `sync_state`를 `unknown`으로 두며 값을 추측하지 않는다.
- `tests/git_repository_provider_tests.cpp`의 "아직 구현하지 않은 동작" test에서 `query_remote` 단정만 걷어냈다. 이번 구간이 구현한 동작이라 더 이상 사실이 아니다. 새 test는 작성하지 않았다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 246/246 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 124개 항목을 확인하고 삭제했다. 그중 11개는 실제 `git.exe`와 임시 저장소 7종(동기, ahead, behind, diverged, remote 없음, 원격 branch 없음, 도달 불가 URL)을 사용했다.
- 도달 불가 URL의 실제 fetch 실패가 libcurl 영어 문장 덕분에 로캘과 무관하게 `offline`으로 분류되는 것을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-4-d3-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009~REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D3-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D3-TEST`의 대상 선택 matrix와 원격 통합 test를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D2-TEST` Git 로컬 조회 test 작성

### 사용자 지시

- `S4-D2-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/git_command_builder_tests.cpp`에 명령 조립 test 4개를 추가했다. `rev-parse` 인자 순서, `status` 인자, **어떤 인자에도 `-z`가 없음**, `status`만 큰 레코드 상한을 쓰는 것과 단계 3 요청 검증 통과를 단정한다.
- `tests/git_status_parser_tests.cpp`에 파서 test 15개를 추가했다. 인용 해제 이스케이프 전종, 배치 파서, 레코드 종류별 파싱, 알 수 없는 헤더 무시, 잘못된 `branch.ab` 4종, 해석 실패 시 `unknown` 상태를 단정한다.
- `tests/fixtures/vcs/git/`에 호스트 Git 2.52.0이 실제로 낸 `status --porcelain=v2 --branch` 출력 5종을 그대로 저장했다. rename fixture는 공백·한글·emoji가 든 경로와 TAB 구분자를 담는다.
- 개행이 든 경로가 인용 덕분에 한 레코드로 남는 것을 test로 고정했다. `-z`를 쓰지 않기로 한 결정의 근거다.
- `tests/git_repository_provider_tests.cpp`에 provider test 20개를 추가했다. 도구 부재, 상대 경로, 사라진 경로, 저장소 아님, bare, git dir 안, timeout, 취소, status 실패, 해석 실패와 미구현 동작에서 **요청 기록 수로 명령 미생성을 직접 단정**한다.
- 저장소 아님 판정 test에 한국어 Git 메시지를 넣어 분류가 메시지 본문이 아니라 구조적 신호로 이뤄지는 것을 언어 독립으로 단정했다.
- `tests/helpers/git_repository_fixture.h/.cpp`를 추가했다. 실제 `git.exe`로 임시 저장소를 만들고 소멸자에서 반드시 지운다. `HOME`, `GIT_CONFIG_GLOBAL`, `GIT_CONFIG_NOSYSTEM`과 커밋 저자·시각을 고정해 호스트 설정과 분리한다.
- `tests/git_integration_tests.cpp`에 통합 test 12개를 추가했다. 정상, ahead, dirty, 충돌, 중단된 rebase, detached, 커밋 없음, bare, linked worktree, 한글·emoji 경로, 비저장소와 없는 경로, 도달 불가 remote를 실제 Git으로 확인한다. Git이 없으면 skip한다.
- 도달 불가 remote를 등록한 저장소로 로컬 조회가 네트워크에 접근하지 않는 것을 실제 실행으로 확인했다.
- 전체 CTest가 195에서 **246**으로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 246/246 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- `--untracked-files=normal`이 미추적 디렉터리를 항목 하나로 접어 보고한다는 사실을 기록했다. `untracked_count`는 파일 수가 아니라 Git이 보고한 항목 수다.
- production source를 변경하지 않았고 `S4-D2-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-4-d2-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009, REQ-010, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D2-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D2-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D3-CODE`의 remote 열거와 fetch를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D2-CODE` Git 로컬 조회 구현

### 사용자 지시

- `S4-D2-CODE`를 진행한다.

### 반영 내용

- `infrastructure/git_command_builder.*`에 `rev-parse` 배치 조회와 `status --porcelain=v2` 요청 조립을 추가했다.
- `rev-parse` 인자 순서를 `--absolute-git-dir --is-bare-repository --is-inside-work-tree --show-toplevel`로 고정했다. 마지막 인자는 bare 저장소에서 실패하지만 앞의 값은 이미 출력되므로 실패한 경우에도 배치를 판정할 수 있다.
- **`status`에 `-z`를 쓰지 않기로 확정했다.** 단계 3 파이프라인이 줄 끝 문자를 남기지 않아 NUL 구분 출력은 경계 정보를 잃고, 개행이 든 경로가 오히려 손상된다. 줄 단위 출력에서는 Git이 그런 경로를 C 인용으로 감싸므로 경계가 흔들리지 않는다. 계획 4.10이 이 구간에서 정하라고 남긴 항목이다.
- `infrastructure/git_status_parser.*`에 배치 파서, porcelain v2 파서, 작업 트리 요약과 C 인용 경로 해제(`unquote_git_path`)를 추가했다.
- 해석하지 못한 레코드가 있거나 branch 헤더를 찾지 못하면 작업 트리 상태를 `unknown`으로 둔다. 출력을 다 읽지 못한 저장소를 깨끗하다고 보고하면 보호 정책이 무력해진다.
- `infrastructure/git_repository_provider.*`에 진행 중 작업 표식 probe와 `query_local`의 snapshot 변환을 추가했다. `index.lock`은 중단된 작업과 구분해 따로 보고한다.
- `domain/repository_snapshot.*`에 `repository_availability::unsupported_layout`을 추가했다. bare 저장소와 git dir 안의 경로를 `not_a_repository`로 보고하면 카드에 잘못된 사유가 뜬다. 계획 11장이 이 구간에서 정하라고 남긴 항목이다.
- linked worktree는 추가 처리 없이 조회된다. git dir이 worktree 전용 디렉터리이고 진행 중 작업 표식도 그곳에 있다.
- 저장소 아님 판정을 번역되는 `fatal: not a git repository` 문장 대신 "정상 종료했는데 출력이 없다"는 구조적 신호로 한다. 로캘 독립 원칙을 따른다.
- 명령을 만들기 전에 등록 경로의 절대 경로 여부와 디렉터리 존재를 확인해 `path_unavailable`을 먼저 판정한다.
- 로컬 조회가 `branch.ab`로 `sync_state`를 채우되 근거를 `comparison_source::local`로 남긴다. 원격을 실제로 확인하는 remote-first 판정은 `S4-D3`이 덮어쓴다.
- `make_vcs_process_request`에 `maximum_record_bytes` 기본 인자를 추가하고 `status`에만 64 KiB를 준다. rename 레코드가 기본 8 KiB를 넘겨 줄이 끊기면 파서가 다른 레코드로 오해한다.
- `git_repository_provider`가 `repository_provider` 계약을 구현한다. 아직 구현하지 않은 원격 조회, switch 후보, update와 switch는 어떤 process request도 만들지 않고 중립 값을 돌려준다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 195/195 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 177개 항목을 확인하고 삭제했다. 그중 19개는 실제 `git.exe`와 임시 저장소 6종(dirty, 충돌, bare, linked worktree, 커밋 없음, 비저장소)을 사용했다.
- 새 test source를 작성하지 않았다. 결과를 `docs/verification/2026-08-16-stage-4-d2-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009, REQ-010, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D2-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D2-TEST`의 파서 test와 Git 통합 fixture를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D1-TEST` 계약 test 작성

### 사용자 지시

- `S4-D1-CODE`를 승인하고 다음 단계를 진행한다.

### 반영 내용

- `tests/helpers/vcs_test_doubles.h/.cpp`에 요청을 기록하는 `fake_process_runner`와 등록한 경로만 존재하는 `fake_vcs_file_probe`를 추가했다. 계획의 `fake_process_runner.*` 대신 probe 대역까지 담는 이름으로 정했다.
- `tests/vcs_version_tests.cpp`에 Git과 SVN banner 파싱, patch 생략 표기, 첫 줄 계약과 최소 버전 경계 test 5개를 추가했다.
- `tests/vcs_tool_discovery_tests.cpp`에 `PATH` 분해, 후보 순서, 도구 조사, 버전 미달과 판독 실패, 지정 경로 정책과 registry test 13개를 추가했다.
- `tests/vcs_error_classifier_tests.cpp`에 로캘 독립 분류 test 12개를 추가했다. 같은 실패의 영어 출력과 한국어 출력이 같은 분류를 내는지 쌍으로 단정한다.
- HTTP 상태 오탐 방지를 `issue-403`, `branch 404`, `error: 4031`, `error: 1401`로 고정했다.
- 프로세스 결과가 stderr 검사보다 우선한다는 것과 취소가 마지막 판정을 오류로 덮지 않는다는 것을 확인했다.
- `tests/vcs_execution_policy_tests.cpp`에 명령 부류별 한도, 비대화형 환경 override, 공통 인자 순서와 단계 3 요청 검증 통과 test 8개를 추가했다.
- `LC_ALL`, `LANG`, `LANGUAGE` override가 없고 모든 명령이 `active_code_page_fallback`을 쓰는 것을 test로 고정했다.
- `tests/vcs_domain_tests.cpp`에 도구 값, 버전 비교, 작업 트리 안전성, snapshot 기본값과 switch 및 update 열거형 test 11개를 추가했다.
- **모든 VCS가 없는 환경**을 전용 test로 고정했다. 두 도구 모두 `not_found`, 프로세스를 하나도 만들지 않음, 진단이 모두 warning, `none_available()` 참을 단정한다.
- 한쪽만 없는 구성에서 나머지 도구가 계속 동작하는 것과, 조사 전 registry가 경고를 내지 않는 것도 확인했다.
- `tests/project_schema_tests.cpp`에 `settings` parse test 3개와 `workspace-settings.verison-list` fixture를 추가했다. 기존 fixture가 그대로 열리는 회귀도 함께 단정한다.
- `tests/json_project_store_tests.cpp`에 `settings` 저장 test 4개를 추가했다. 알 수 없는 키 보존, 기본값 문서에 필드를 만들지 않음, 값 생성 시 기록, 상대 경로 저장 거부를 확인한다.
- `tests/domain_model_tests.cpp`에 새 diagnostic code 11개, `authentication_required` 이름과 `workspace_settings` 기본값 단정을 추가했다.
- `gitman_tests`에 `gitman_vcs` 링크와 `${GITMAN_TEST_DIRECTORY}` include 경로를 추가했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 195/195 통과했고 `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D1-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-4-d1-test.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-002, REQ-006, REQ-007, REQ-009~REQ-013, REQ-017
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D1-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D1-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D2-CODE`의 Git 명령 조립과 출력 파서를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D1-CODE` 계약과 도구 발견 구현

### 사용자 지시

- 개정한 `S4-P0` 계획을 승인하고 코드 작성을 진행한다.
- 환경에 따라 Git이 없을 수도, SVN이 없을 수도 있다. 통합 사용 환경을 제공하는 것이 목표이므로 **모든 VCS가 없는 경우도 상정**해야 한다.

### 반영 내용

- `domain/vcs_tool.*`에 도구 가용성, 버전 값과 `vcs_tool_set`을 추가했다. `none_available()`로 Git과 SVN이 모두 없는 환경을 1급 상태로 표현한다.
- `domain/repository_snapshot.*`에 `repository_availability`를 추가해 도구 부재가 오류가 아니라 상태로 표현되게 했다. 카드는 목록에 남고 동작만 비활성화된다.
- 도구 부재 진단의 severity를 warning으로 정했다. 앱을 멈추지 않고 다른 VCS와 프로젝트 목록은 계속 사용할 수 있다.
- `repository_provider` 계약에 `available()`을 넣어 호출자가 조회와 변경을 시도하기 전에 걸러낼 수 있게 했다.
- `remote_sync_state::authentication_required`와 작업 트리의 진행 중 작업, `index.lock`, detached 표시를 추가했다.
- `working_tree_summary::is_safe_for_change()`를 추가했다. `unknown` 상태도 안전으로 보지 않는다.
- `submodule_status`, SVN 저장소 root 및 UUID, `std::optional<bool>` switched 및 mixed revision 필드를 추가했다.
- `domain/vcs_operation.*`에 switch 후보, 거부 사유 12종, update 차단 사유 13종과 각각의 한국어 메시지를 추가했다.
- `domain/diagnostic.*`에 VCS 관련 code 11개와 이름 매핑을 추가했다.
- `is_absolute_windows_path`를 `application/process_request`에서 `domain/path_syntax`로 옮겼다. 문서 `settings` 검증과 프로세스 요청 검증이 같은 규칙을 쓰게 하려는 이동이며, `process_request.h`가 새 헤더를 include해 기존 호출자는 변경 없이 컴파일된다.
- `.verison-list`에 optional `settings` object를 추가했다. 스키마 버전 1을 유지하고, 없으면 진단 없이 기본값이며, 절대 경로가 아닌 값은 `vcs_tool_path_invalid` 오류로 보고한다.
- 저장 시 기존 `settings` object를 template으로 삼아 알 수 없는 키를 보존하고, 문서에 없었고 값도 기본값이면 필드를 만들지 않는다.
- `application/vcs_tool_registry.*`를 값 container로 두고 탐색 로직은 infrastructure에 두어 application이 infrastructure를 참조하지 않게 했다.
- `application/vcs_file_probe.h`로 git dir 표식 파일 확인을 계약화했다. Git에 진행 중 작업을 알려 주는 기계 판독 명령이 없기 때문이다.
- `infrastructure/vcs_tool_discovery.*`에 `settings` → `PATH` → 기본 설치 위치 순서의 탐색을 구현했다. 후보 생성은 filesystem을 보지 않는 순수 함수다.
- `PATH` 분해에서 따옴표를 벗기고 빈 항목과 상대 경로 항목을 버리며 ASCII 대소문자를 무시하고 중복을 제거한다.
- 지정 경로가 상대 경로거나 없거나 `--version`이 실패하면 `path_invalid`로 보고하고 자동 탐색으로 물러서지 않게 했다.
- `svnversion.exe`를 `svn.exe`와 같은 디렉터리에서 찾아 보조 도구로 담고, 없어도 조회를 막지 않게 했다.
- `infrastructure/vcs_version.*`에 접두어에 의존하지 않는 버전 파서를 구현했다. `git version 2.52.0.windows.1`과 `svn, version 1.14.5 (r1922182)`를 모두 처리한다.
- `infrastructure/vcs_execution_policy.*`에 명령 부류별 timeout 및 캡처 상한과 Git 비대화형 환경 override 7종, 공통 인자 4종, SVN `--non-interactive`를 모았다.
- 로캘을 강제하지 않고 모든 명령에서 `active_code_page_fallback` 인코딩을 쓰도록 했다.
- `infrastructure/vcs_error_classifier.*`를 로캘 독립 신호(SVN 오류 코드, libcurl 및 OpenSSH 원문, HTTP 상태 번호)만으로 분류하도록 구현했다. HTTP 상태는 독립 토큰이면서 `http` 문맥이 있을 때만 인정해 오탐을 막는다.
- 어느 신호에도 맞지 않는 실패는 추측하지 않고 `command_failed`로 보고하게 했다.
- `infrastructure/vcs_command_runner.*`로 stdout과 stderr를 분리 수집하면서 호출자 sink에도 전달하게 했다.
- `platform/win32/win32_vcs_file_probe.*`에 파일 존재 확인과 환경 변수 읽기를 구현했다. 계층 방향을 지키려고 target은 `gitman_vcs`에 두었다.
- 새 static library `gitman_vcs`를 추가하고 `gitman_domain`과 `gitman_process`를 PUBLIC, `gitman_win32_platform`을 PRIVATE으로 링크했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 139/139 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 69개 항목을 확인하고 삭제했다. 한국어 Git 출력에서도 libcurl 문자열 덕분에 `offline` 분류가 유지되는 것과, 빈 `PATH`에서 두 도구 모두 `not_found`이면서 진단이 warning뿐인 것을 확인했다.
- 임시 프로그램의 기대값 오류 2건을 발견해 고쳤다. production 결함은 아니며 `S4-D1-TEST`에서 test로 고정한다.
- 결과를 `docs/verification/2026-08-16-stage-4-d1-code.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-002, REQ-006, REQ-007, REQ-009~REQ-013, REQ-017
- NFR-005, NFR-007, NFR-008, NFR-009

### 다음 작업 제한

- `S4-D1-CODE` 검수 전에는 `S4-D1-TEST`의 test source, fixture와 `gitman_tests` 링크를 추가하지 않는다.
- Git 명령 조립과 출력 파서는 `S4-D2-CODE` 승인 후에만 작성한다.
- test에서 production 결함이 발견되어도 `S4-D1-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-16 - 단계 4 `S4-P0` 1차 검수 결정 반영

### 사용자 지시

- `remote_sync_state`에 `authentication_required`를 제안대로 추가한다.
- 로캘은 강제하지 말고 시스템 로캘에 맞춘다. 이 프로젝트는 한국어 기반이므로 한국어 출력이 나오며, stderr를 표시하는 로그 뷰를 앱이 직접 제공하므로 인코딩 문제는 앱이 감당한다.
- SVN CLI는 설치하지 않는다. CLI가 있다고 가정하고 SVN 명령만 연결한다. XML 처리가 꼭 필요한지 재검토한다.
- Git 및 SVN 경로의 수동 입력을 추후 환경설정에서 제어할 수 있게 한다. 환경설정 값은 프로젝트 파일의 `settings` 속성에 둔다.

### 반영 내용

- SVN에서 XML을 사용하지 않기로 확정했다. `info --show-item`(값 한 줄), 비verbose `status`(고정 9칸 + 경로), `svnversion`(`4123:4168MS` 형태) 조합으로 필요한 값을 모두 얻는다.
- `status --verbose`는 작성자 컬럼 때문에 공백 포함 경로에서 경계가 모호해지므로 사용하지 않기로 하고, mixed revision 판정을 `svnversion`으로 옮겼다.
- 원격 대비 상태를 로캘 의존 요약 줄 대신 원격 URL의 `--show-item revision`과 작업 복사본 리비전 비교로 판정하게 했다.
- XML 파서 dependency를 추가하지 않으므로 `vcpkg.json`과 ADR-002는 변경하지 않는다.
- `LC_ALL=C` 강제를 계획에서 제거하고 시스템 로캘을 따르도록 바꿨다.
- 로캘을 강제하지 않으면 번역된 메시지로 오류를 분류할 수 없으므로, 분류 근거를 SVN `E<숫자>` 코드, libcurl 및 OpenSSH 원문 문자열, HTTP 상태 번호, 프로세스 완료 사유 같은 로캘 독립 신호로 다시 설계했다.
- 어떤 신호에도 맞지 않는 실패는 추측하지 않고 `error`로 보고하도록 정했다.
- 오류 분류 test가 같은 오류의 영어 출력과 한국어 출력에서 같은 분류를 내는지 단정하도록 test 계획을 보강했다.
- 인코딩 모드를 Git 포함 모든 명령에서 `active_code_page_fallback`으로 통일했다. 단계 3에서 이미 구현하고 검증한 경로다.
- `remote_sync_state`에 `authentication_required`를 추가하기로 확정하고 `docs/plan.md` 3.2의 상태 표와 Codicon 표에 `key` 아이콘 및 “인증 필요”를 반영했다. `offline` 설명에서 인증 실패를 분리했다.
- 프로젝트 문서에 optional `settings` 속성을 도입하는 설계를 계획 4.11에 추가했다. 스키마 버전은 1을 유지하고, 없으면 자동 탐색 기본값이며, 알 수 없는 키까지 round-trip 보존한다.
- 도구 탐색 순서를 `settings` 수동 지정 → PATH → 기본 설치 경로로 바꾸고, 지정 경로가 잘못되면 자동 탐색으로 물러서지 않고 `vcs_tool_path_invalid`로 보고하도록 정했다.
- `docs/plan.md` 3.7의 스키마 예시와 `docs/requirements.md`에 REQ-017을 추가했다.
- `S4-D1` 구간 범위에 문서 `settings` 스키마 확장과 기존 fixture 6종 회귀를 포함했다.
- SVN 통합 검증 정책을 미설치 확정 기준으로 다시 썼다. 도구 미설치 감지는 이 호스트에서 실제 검증 가능한 유일한 SVN 경로이므로 유지한다.

### 영향 요구사항

- REQ-001, REQ-002, REQ-006, REQ-007, REQ-008, REQ-011, REQ-012, REQ-017
- NFR-005, NFR-007, NFR-008

### 다음 작업 제한

- 개정한 `S4-P0` 계획 승인 전에는 `src/`와 `tests/`에 VCS provider 관련 source를 추가하지 않는다.
- 승인 후에도 `S4-D1-CODE` 한 구간만 수행하고 보고 뒤 중지한다.
- 환경설정 화면은 단계 6~7 범위이며 단계 4에서 UI를 만들지 않는다.

## 2026-08-16 - 단계 3 승인과 단계 4 `S4-P0` 계획 작성

### 사용자 지시

- 단계 4를 진행한다.
- 이전 단계와 같이 작업 단위의 변경마다 검수를 받고, 검수 후 사용자가 직접 커밋한다.

### 반영 내용

- 단계 4 진행 지시를 단계 3 최종 승인으로 처리하고 `docs/plan.md`와 `docs/handoff.md`의 승인 대기 상태를 완료로 갱신했다.
- `docs/stage-4-plan.md`에 Git 및 SVN provider 구현 계획을 작성했다.
- provider가 `process_runner`를 주입받아 Win32 API를 직접 호출하지 않는 계층 경계와 의존성 방향을 정의했다.
- PATH 직접 분해 기반 도구 탐색, `--version` 파싱과 `not_found` / `version_unreadable` / `too_old` / `available` 상태를 제안했다.
- Git 비대화형 환경 override 7종과 공통 인자 `-c core.quotepath=false`, `-c gc.auto=0`, `-c color.ui=false`, `--no-pager`를 제안했다.
- 단계 3이 미정으로 남긴 명령별 timeout과 스트림당 캡처 상한을 부류별 값으로 확정 제안했다.
- `rev-parse`와 `status --porcelain=v2 --branch -z` 기반 로컬 상태 조회, 진행 중 작업 표식 파일 판정을 제안했다.
- upstream → `preferred_remote` → `origin` → 유일한 remote 순서의 remote target 선택과 `fetch --prune`, `rev-list --left-right --count` 기반 ahead/behind 판정을 제안했다.
- SVN `info --xml`, `status --verbose --xml`, `status --show-updates --xml` 기반 조회와 mixed revision 및 switched subtree 판정을 제안했다.
- SVN XML 처리 방식으로 pugixml 추가를 권장하고 자체 reader 및 `--show-item` 대체안과 함께 검수 항목으로 올렸다.
- update의 사전 차단 사유 8종, `pull --ff-only`, submodule dirty 사전 검사와 recursive update 순서를 제안했다.
- switch 후보의 remote-first 정렬, ambiguous remote 자동 선택 금지, tracking branch 확인 요구와 `--no-guess` 실행을 제안했다.
- 검증 실패 시 `process_request`를 만들지 않는 REQ-007 수용 기준을 fake runner 기록으로 직접 검증하는 test 전략을 정의했다.
- `remote_sync_state`의 `authentication_required` 추가와 `docs/plan.md` 3.2 Codicon 표 갱신을 검수 항목으로 올렸다.
- stderr 패턴 기반 `authentication_required` / `offline` / `repository_not_found` / `error` 분류기를 제안했다.
- fake runner 단위 test와 실제 임시 Git 저장소 통합 test의 두 층 전략, fixture 12종과 SVN 미설치 대응 정책을 정의했다.
- `CODE` / `TEST` / `FIX` 6분할과 `S4-V1`로 구성한 20개 체크포인트 및 검수 게이트를 정의했다.
- `docs/handoff.md`의 현재 단계, 진행 원장과 미해결 항목을 단계 4 기준으로 갱신했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-009~REQ-014
- NFR-005~NFR-009

### 다음 작업 제한

- `S4-P0` 계획 승인 전에는 `src/`와 `tests/`에 VCS provider 관련 source를 추가하지 않는다.
- 승인 후에도 `S4-D1-CODE` 한 구간만 수행하고 보고 뒤 중지한다.
- pugixml 추가와 ADR-002 개정은 계획 검수에서 승인된 뒤에만 수행한다.
- ADR-004의 범용 메시지 구조는 단계 6 이전 별도 승인 없이 구현하지 않는다.

## 2026-08-16 - 단계 2·3 독립 감사 및 발견 사항 해소

### 사용자 지시

- 단계 4 진행 전에 단계 2·3의 진행 상황을 독립적으로 감사하고 보고한다.
- 감사에서 확인한 발견 사항들을 해소한 뒤 단계 4로 넘어간다.

### 감사 결과

- 계획 문서 대비 코드·테스트 전수 대조와 현재 HEAD의 build/test 재현으로 두 단계 모두 완료 조건 충족을 확인했다.
- 검증 기록의 test 개수 등 수치 주장이 실측과 일치했고 과장이나 허위는 발견되지 않았다.

### 반영 내용

- runner에 reader join drain 유예(2초)와 `CancelSynchronousIo` 최후 수단을 추가해, 자식이 정상 종료해도 출력 pipe를 상속한 손자 때문에 `run()`이 무기한 블록되는 경로를 없앴다. 강제 마감은 warning 진단으로 보고한다.
- reader 생성 이후 구간을 예외 안전하게 만들어 joinable 스레드 unwinding에 의한 `std::terminate` 경로를 제거하고, reader catch-all이 pipe를 계속 비우며 `process_pipe_failed` 진단을 남기게 했다.
- `text_transcoder`에 `safe_split_position`을 추가하고 fallback 강제 분할이 활성 code page 문자 경계를 따르게 해 CP949 2 byte 문자 훼손을 막았다.
- URL userinfo 마스킹이 authority 안의 마지막 `@`를 구분자로 삼아 percent-encoding 없는 password `@`가 부분 누출되지 않게 했다.
- `application/project_path_resolver.h` 계약을 추가하고 `gitman_workspace`의 Win32 platform 링크를 제거했다. store와 경로 해석은 주입받은 resolver만 사용하며 단위 test는 lexical fake를 쓴다.
- `ReplaceFileW` 실패 후 원본 복원까지 실패한 경우를 `workspace_file_commit_failure::restore`로 구분하고 `.bak` 복구 안내 메시지를 추가했다.
- `default_project_display_name`을 공개해 parser와 store의 중복 정의를 통합했다.
- test 보강: 손자 pipe 점유 drain 회귀(`spawn-detached`/`hold-handles` 도우미), emoji 실행 파일 경로, 8 MiB 대용량 상향, fallback 강제 분할 경계, raw `@` URL 마스킹, `restore` 매핑, `project_path_state_from_error` 매핑. 전체 Catch2 test에 CTest TIMEOUT 120초를 부여했다.
- 로컬 NTFS에서 deny ACE로 `GetFileAttributesW`를 실패시킬 수 없음을 실측으로 확인하고 `inaccessible` 검증을 오류 매핑 방식으로 확정했다.
- TOCTOU 창, 레코드 분할 마스킹 우회, unknown field의 ID 매칭 의존 등은 설계상 수용으로 문서화했다.
- VS2022 Debug/Release, VS2026 Debug 전체 CTest 각각 139/139, `/analyze` 무경고, aggregate format/style 154개 파일 통과.
- 결과를 `docs/verification/2026-08-16-stage-2-3-audit-fix.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-006, REQ-008~REQ-013
- NFR-005~NFR-009

### 다음 작업 제한

- 단계 3 최종 사용자 승인 대기 상태는 유지된다. 승인 전에는 단계 4를 시작하지 않는다.
- drain 유예 상수와 Git background 프로세스 대응 정책은 단계 4 계획(`S4-P0`)에서 재검토한다.

## 2026-08-16 - 단계 3 최종 자동 검증

### 사용자 지시

- `S3-D5-TEST`를 승인하고 무결함 `S3-D5-FIX` 생략을 확인한 뒤 `S3-V1`을 진행한다.

### 반영 내용

- `build/vs2022`를 삭제하고 재configure한 뒤 Debug build와 전체 CTest 135/135가 통과했다.
- VS2022 Release build와 CTest 135/135, VS2026 Debug build와 CTest 135/135가 통과했다.
- VS2022 `/analyze` build가 경고 없이 통과했고 aggregate `gitman_format_check`가 152개 파일에서 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- 4스레드 × 25회, 합계 100개 프로세스 동시 실행 stress를 3회 수행했다. 실패 0건, sequence 역전 0건이며 레코드 수 112,525가 매번 같았다.
- handle 증가폭이 매번 고정된 6개이고 반복해도 누적되지 않아 실행별 누수가 없음을 확인했다.
- Release install 결과가 `bin/gitman.exe` 한 파일(6,255,616 byte)이며 Windows 시스템 DLL 외 의존성이 없음을 확인했다.
- 설치본의 CPU, auto, 강제 fallback과 Direct3D smoke test가 모두 종료 코드 0으로 통과했다.
- test 전용 도우미 target이 install tree에 포함되지 않음을 확인했다.
- 실행 파일 크기가 단계 2와 같은 이유가 `gitman_process`를 아직 exe가 링크하지 않기 때문임을 기록했다.
- 결과를 `docs/verification/2026-08-16-stage-3.md`에 기록했다.

### 영향 요구사항

- REQ-006~REQ-013
- NFR-005~NFR-009, NFR-011

### 다음 작업 제한

- `S3-V1` 자동 완료 조건은 충족했으며 단계 3 최종 사용자 검수 대기 상태다.
- 사용자 최종 승인 전에는 단계 4 Git 및 SVN provider를 시작하지 않는다.

## 2026-08-16 - 단계 3 `S3-D5-TEST` 마스킹 test 작성

### 사용자 지시

- `S3-D5-CODE`를 승인하고 `S3-D5-TEST`를 진행한다.

### 반영 내용

- `tests/secret_masking_tests.cpp`에 URL userinfo, 자격 증명 option, 헤더와 `Basic`, token 접두어, 일반 출력 불변, 복수 비밀과 보수적 경계 test 9개를 추가했다.
- 모든 단정이 기대값 비교와 함께 결과를 다시 마스킹해도 같은지 확인하는 idempotency 검사를 거치게 했다.
- `://`가 없는 SSH 축약 형태(`git@github.com:owner/repo.git`)와 `--password-from-stdin` 같은 접두어 공유 이름이 변형되지 않는 것을 고정했다.
- 자격 증명에 붙은 구두점이 함께 가려지는 문서화된 동작을 test로 고정해 의도치 않은 변경을 잡을 수 있게 했다.
- `tests/win32_process_runner_tests.cpp`에 end-to-end 적용 test를 추가해 기록된 명령줄과 출력 레코드 양쪽에서 비밀이 사라지는 것을 확인했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 135/135 통과했고 `/analyze`도 무경고로 통과했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d5-test.md`에 기록했다.

### 영향 요구사항

- REQ-008, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-006, NFR-008

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D5-FIX`는 사용자 확인 후 생략한다.
- 남은 작업은 `S3-V1` 단계 3 최종 검증뿐이며 승인 후에만 시작한다.

## 2026-08-16 - 단계 3 `S3-D5-CODE` 비밀 마스킹 구현

### 사용자 지시

- `S3-D4-TEST`를 승인하고 무결함 `S3-D4-FIX` 생략을 확인한 뒤 `S3-D5-CODE`를 진행한다.

### 반영 내용

- `infrastructure/secret_masking.*`에 `std::regex`를 쓰지 않는 단일 통과 scanner를 구현했다.
- URL userinfo는 사용자 이름을 남기고 비밀만 가리며, 사용자 이름 없는 값은 token으로 보고 전체를 가린다.
- 자격 증명 option 6종의 값을 `=` 형태와 공백 구분 형태 모두에서 가리고, 명령줄 인용이 남아 있으면 따옴표 안쪽만 가린다.
- `Authorization:`, `PRIVATE-TOKEN:`, `x-access-token:` 값을 줄 끝까지 가리고 단독 `Basic` 자격 증명도 처리한다.
- `ghp_`, `gho_`, `ghu_`, `ghs_`, `ghr_`, `github_pat_`, `glpat-` 접두어가 붙은 token을 가린다.
- option과 token 이름은 단어의 처음에서만 인식하고, 이름 뒤에 `=`나 공백이 오는지로 접두어 충돌을 판정해 목록 순서에 의존하지 않게 했다.
- 마스킹을 인코딩 확정 이후 단계에 두어 UTF-8과 code page fallback 경로 모두에서 sink 도달 전에 적용되게 했다.
- 기록용 `masked_command_line`에만 마스킹을 적용하고 자식에게 넘기는 실제 명령줄은 원본을 유지했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 125/125 통과했고 `/analyze`도 무경고로 통과했다.
- 임시 프로그램으로 26개 항목과 각 항목의 idempotency를 확인했고, 4 MB 출력 마스킹이 863 ms로 실용적임을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d5-code.md`에 기록했다.

### 영향 요구사항

- REQ-008, REQ-009, REQ-011, REQ-012, REQ-013
- NFR-007, NFR-008

### 다음 작업 제한

- `S3-D5-CODE` 검수 전에는 마스킹 test source를 추가하지 않는다.
- 단계 3 최종 검증 `S3-V1`은 `S3-D5-TEST` 승인 후에만 시작한다.

## 2026-08-16 - 단계 3 `S3-D4-TEST` timeout과 취소 test 작성

### 사용자 지시

- `S3-D4-CODE`를 승인하고 `S3-D4-TEST`를 진행한다.

### 반영 내용

- 도우미에 `sleep`, `write-marker`, `spawn-child` 명령을 추가했다. `sleep`은 대기 전에 한 줄을 출력해 timeout 이전 레코드 전달을 확인할 수 있게 했다.
- timeout 종료, 실행 중 취소, 사전 취소, timeout 이전 정상 종료, 미사용 취소 source와 handle 누수 test를 추가했다.
- 손자 종료 test에 대조군을 넣어 "파일이 없다"는 단정이 종료 동작 때문임을 보장했다. 손자가 200 ms 뒤 marker를 만들면 파일이 생기고, 2초 뒤 만들도록 하고 300 ms에 종료하면 3.5초를 기다려도 생기지 않는다.
- handle 누수 test는 20회 반복 실행 전후의 프로세스 handle 수 차이를 확인한다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 125/125 통과했고 `/analyze`도 무경고로 통과했다.
- 타이밍에 의존하는 test 14개를 `--repeat until-fail:3`으로 반복 실행해 flakiness가 없음을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d4-test.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-012, REQ-013
- NFR-007, NFR-009

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D4-FIX`는 사용자 확인 후 생략한다.
- `S3-D5-CODE` 승인 전에는 마스킹 구현을 시작하지 않는다.

## 2026-08-16 - 단계 3 `S3-D4-CODE` timeout과 취소 구현

### 사용자 지시

- `S3-D3-TEST`를 승인하고 무결함 `S3-D3-FIX` 생략을 확인한 뒤 `S3-D4-CODE`를 진행한다.

### 반영 내용

- 이미 취소된 요청은 프로세스를 만들지 않고 `cancelled`로 반환하도록 했다.
- `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job을 만들고 자식을 `CREATE_SUSPENDED`로 시작한 뒤 배정하고 재개하도록 했다. 배정 전에 손자가 만들어지는 경쟁을 없앤다.
- job을 만들거나 배정하지 못하면 warning 진단과 함께 단일 프로세스 종료로 물러서도록 했다. 제한된 환경에서 실행 자체가 막히지 않게 한 선택이다.
- 취소 token 콜백이 event 하나를 신호하고 `WaitForMultipleObjects`가 프로세스와 취소 event를 함께 기다리도록 했다. polling이 없고 Win32 type도 상위 계층에 노출되지 않는다.
- event handle을 registration보다 먼저 선언해 콜백이 닫힌 event를 신호하지 않도록 수명 순서를 고정했다.
- timeout 초과와 취소에서 트리를 종료하고, 종료로 pipe가 닫혀 reader 스레드가 EOF를 보고 끝나도록 했다.
- 강제 종료한 실행은 종료 코드를 채우지 않고 `timed_out` 또는 `cancelled`로 보고하며 그때까지 수집한 출력은 유지한다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 118/118 통과했고 `/analyze`도 무경고로 통과했다.
- 임시 프로그램으로 19개 항목을 확인했다. 400 ms timeout이 483 ms에 반환되고, 300 ms 취소가 314 ms에 반환되며, 손자 프로세스가 job과 함께 사라지고, 30회 반복 실행 후 handle 수가 늘지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-3-d4-code.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-007, REQ-009, REQ-012, REQ-013
- NFR-007, NFR-009

### 다음 작업 제한

- `S3-D4-CODE` 검수 전에는 도우미 명령과 test source를 추가하지 않는다.
- 마스킹은 `S3-D5-CODE` 승인 후에만 구현한다.

## 2026-08-16 - 단계 3 `S3-D3-TEST` code page fallback test 작성

### 사용자 지시

- `S3-D3-CODE`를 승인하고 `S3-D3-TEST`를 진행한다.

### 반영 내용

- `tests/process_output_pipeline_tests.cpp`에 대역 transcoder를 넣어 판정 순서 test 6개를 추가했다. 유효하지 않은 레코드만 변환, 유효 UTF-8 보존, 변환 실패 시 U+FFFD 복귀, `utf8` 모드의 미호출과 transcoder 없음 경로를 확인한다.
- `is_valid_utf8_text`가 `normalize_utf8_text`와 같은 기준으로 overlong, surrogate, 범위 초과와 미완결 sequence를 거부하는지 확인했다.
- `tests/win32_text_transcoder_tests.cpp`를 추가해 빈 입력, ASCII, 해석 불가 byte와 한국어 호스트의 CP949 복원을 확인했다. 활성 code page가 949가 아니면 해당 단정을 `WARN`으로 건너뛴다.
- `tests/win32_process_runner_tests.cpp`에 end-to-end fallback test 3개를 추가했다. 확인 문자열을 `WideCharToMultiByte(CP_ACP, ...)`로 runtime에 인코딩해 code page 949와 UTF-8 호스트 모두에서 유효한 검증이 되게 했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 118/118 통과했고 `/analyze`도 무경고로 통과했다.
- test 작성 중 UTF-8 한글 byte를 CP949로 해석하면 다른 문자가 된다는 기대값이 틀렸음을 확인했다. 엄격 변환이 실패하는 것이 실제 계약이며 production 수정 없이 기대값을 고쳤다.
- 결과를 `docs/verification/2026-08-16-stage-3-d3-test.md`에 기록했다.

### 영향 요구사항

- REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-006

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D3-FIX`는 사용자 확인 후 생략한다.
- `S3-D4-CODE` 승인 전에는 timeout과 취소 구현을 시작하지 않는다.
- 도우미의 `sleep`과 `spawn-child` 명령은 `S3-D4-TEST`에서만 추가한다.

## 2026-08-16 - 단계 3 `S3-D3-CODE` 활성 code page fallback 구현

### 사용자 지시

- `S3-D2-TEST`를 승인하고 무결함 `S3-D2-FIX` 생략을 확인한 뒤 `S3-D3-CODE`를 진행한다.

### 반영 내용

- `application/text_transcoder.h`에 변환 실패를 값으로 보고하는 `noexcept` transcoder 계약을 추가했다.
- `platform/win32/win32_text_transcoder.*`에 `CP_ACP`와 `MB_ERR_INVALID_CHARS` 기반 엄격 변환을 구현했다. 해석할 수 없는 byte는 실패로 보고해 호출자가 U+FFFD 대체로 되돌릴 수 있게 했다.
- `process_output_pipeline`이 인코딩 모드와 transcoder를 받도록 확장했다. 기본값이 있어 기존 호출과 test는 변경 없이 동작한다.
- 레코드가 유효한 UTF-8이면 그대로 두고, 아닐 때만 활성 code page로 변환한 뒤 `transcoded_from_active_code_page`를 세우도록 했다. 변환이 실패하면 U+FFFD 대체로 되돌린다.
- 판단 단위를 레코드 하나로 두어 한 실행에서 UTF-8 줄과 code page 줄이 섞여도 각각 알맞게 처리된다.
- `is_valid_utf8_text`를 공개해 `normalize_utf8_text`와 같은 기준으로 유효성을 판정하게 했다.
- runner는 fallback을 요청한 실행에서만 transcoder를 만들고 두 스트림 파이프라인이 공유하게 했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 107/107 통과했고 `/analyze`도 무경고로 통과했다.
- 활성 code page 949 호스트에서 임시 프로그램으로 15개 항목을 확인했다. CP949 byte가 `한글`로 복원되고, 유효한 UTF-8은 변형되지 않으며, CP949에서도 해석할 수 없는 byte는 U+FFFD로 대체된다.
- 결과를 `docs/verification/2026-08-16-stage-3-d3-code.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-007

### 다음 작업 제한

- `S3-D3-CODE` 검수 전에는 fallback test source를 추가하지 않는다.
- timeout, 취소와 마스킹은 각각 `S3-D4-CODE`, `S3-D5-CODE` 승인 후에만 구현한다.

## 2026-08-16 - 단계 3 `S3-D2-TEST` 도우미 target과 실행 계층 test 작성

### 사용자 지시

- 수정한 `S3-D2-CODE`를 승인하고 `S3-D2-TEST`를 진행한다.

### 반영 내용

- `tests/helpers/process_test_child.cpp`와 `gitman_process_test_child` 콘솔 target을 추가했다. 표준 `wmain` argv를 쓰고 raw byte로 출력하며 install 대상이 아니다.
- test는 `GetModuleFileNameW`로 도우미 경로를 runtime에 찾는다. compile definition으로 넘기면 Windows 경로의 backslash가 문자열 이스케이프로 해석되는 문제를 피했다.
- `tests/command_line_builder_tests.cpp`에 인용 규칙, backslash 처리, 셸 metacharacter 보존과 명령줄 조립 test 5개를 추가했다.
- `tests/process_output_pipeline_tests.cpp`에 UTF-8 정규화, 줄 분할, 진행 표시, chunk 경계, 강제 분할, 캡처 상한과 flush test 10개를 추가했다.
- `tests/win32_process_runner_tests.cpp`에 종료 코드, 인자 왕복, 한글 및 공백 작업 디렉터리, 환경 override, 4 MB 출력, 절단, 두 스트림 순서, 혼합 출력, 읽기 경계, null sink, stdin EOF, 시작 실패, 잘못된 요청과 동시 실행 test 14개를 추가했다.
- `tests/process_execution_tests.cpp`에 `internal_error` 이름 매핑과 성공 아님 판정을 추가했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 107/107 통과했고 `/analyze`도 무경고로 통과했다.
- Release install 결과가 `bin/gitman.exe` 한 파일임을 다시 확인해 도우미 target이 배포에 포함되지 않음을 검증했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d2-test.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-007, NFR-011

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D2-FIX`는 사용자 확인 후 생략한다.
- `S3-D3-CODE` 승인 전에는 code page fallback transcoder를 구현하지 않는다.
- `sleep`과 `spawn-child` 도우미 명령은 `S3-D4-TEST`에서만 추가한다.

## 2026-08-16 - 단계 3 `S3-D2-CODE` 수정 재제출

### 사용자 지시

- `S3-D2-CODE` 1차 제출을 수정 후 재검수로 판정하고 두 항목을 지시했다.
- `WaitForSingleObject` 실패 시 자식을 정리한다.
- `S3-D3`의 출력 pipe와 reader 스레드를 `S3-D2`에 포함한다.

### 반영 내용

- `process_completion::internal_error`를 추가해 프로세스는 시작했지만 결과를 신뢰할 수 없는 경로를 구분했다.
- wait 실패, reader 스레드 생성 실패, 종료 코드 확인 실패에서 자식을 `TerminateProcess`로 정리하고 `internal_error`를 반환하도록 했다.
- stdout과 stderr에 익명 pipe를 연결하고 쓰기 end만 상속시키며 시작 직후 부모 사본을 닫아 EOF를 관측하게 했다.
- pipe별 전용 reader 스레드를 만들고 `run` 반환 전에 항상 join하도록 했다.
- `infrastructure/process_output_pipeline.*`에 줄 단위 레코드, `\r\n` 및 단독 `\r` 처리, 강제 분할, UTF-8 경계 보정, 잘못된 byte의 U+FFFD 대체, 스트림별 캡처 상한과 절단 표시를 구현했다.
- `output_collector`가 mutex 아래에서 실행 단위 sequence를 부여하고 sink 예외를 흡수한 뒤 진단으로 보고하게 했다.
- 절단 발생 시 warning 진단을 남기고 실행 자체는 실패로 보지 않도록 했다.
- 저장소 밖 임시 프로그램으로 출력 34개 항목을 확인했다. 8,000,028 byte 출력이 교착 없이 131,148 레코드로 수집되고, chunk 경계에 걸친 한글 문자가 온전하며, 8 byte 상한에서 `continued` 분할이 동작한다.
- 구현 중 강제 분할 경계 계산 오류를 발견해 같은 구간에서 고쳤다.
- 범위 이동에 따라 `S3-D3`은 활성 code page fallback transcoder와 파이프라인 단위 test 보강만 담당하도록 계획을 갱신했다.

### 이전 제출 내용

- `infrastructure/command_line_builder.*`에 `CommandLineToArgvW` 규칙 인자 인용과 명령줄 조립을 구현했다.
- `platform/win32/win32_process_runner.*`에 `lpApplicationName` 기반 절대 경로 실행, 셸 미사용 시작, 작업 디렉터리 적용을 구현했다.
- 부모 환경 상속과 override 설정 및 삭제, 대소문자 무시 이름 비교, 정렬된 환경 block 생성을 구현했다.
- stdin을 항상 `NUL`에 연결해 대화형 프롬프트가 즉시 EOF가 되게 했다.
- `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`로 표준 handle 세 개만 상속시키고 시작 직후 부모 사본을 닫았다.
- 종료 코드를 bit 값 그대로 보존하고 시작 실패를 `start_failed`와 Win32 error code, 구조화 진단으로 반환했다.
- 실행용 명령줄과 기록용 `masked_command_line`을 분리해 `S3-D5-CODE`의 마스킹이 실행 인자에 영향을 주지 않게 했다.
- `gitman_process` target에 두 source를 넣고 `gitman_win32_platform`을 PRIVATE으로 링크했다. static library 순환 참조를 피하려고 runner의 target만 계획과 다르게 정했고 파일 위치는 계획대로 유지했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest 78/78, `/analyze` 무경고, aggregate format/style이 통과했다.
- 저장소 밖 임시 프로그램으로 21개 시작 계약 항목을 수동 확인하고 삭제했다. 결과는 `docs/verification/2026-08-16-stage-3-d2-code.md`에 기록했다.
- `cmd.exe`가 argv 규칙을 쓰지 않아 test 자식으로 부적합함을 확인하고 계획 8.1에 도우미 요건을 명시했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-007, NFR-008

### 다음 작업 제한

- `S3-D2-CODE` 검수 전에는 도우미 실행 파일 target과 test source를 추가하지 않는다.
- 출력 수집, timeout 및 취소, 마스킹은 각각 `S3-D3-CODE`, `S3-D4-CODE`, `S3-D5-CODE` 승인 후에만 구현한다.

## 2026-08-16 - 단계 3 `S3-D1-TEST` 계약 test 작성과 format 기준선 정렬

### 사용자 지시

- `S3-D1-CODE`를 승인하고 `S3-D1-TEST`를 진행한다.
- clang-format이 수동 줄바꿈을 되돌리는 3개 파일은 formatter 결과를 수용한다.

### 반영 내용

- `tests/process_execution_tests.cpp`에 출력 레코드 및 실행 결과 기본값, 성공 판정, 비정상 완료와 이름 매핑 test 5개를 추가했다.
- `tests/process_request_tests.cpp`에 기본값, 어휘적 절대 경로 판정, 실행 파일과 작업 디렉터리, 인자 NUL, 환경 override, timeout 및 상한 경계와 복합 오류 test 9개를 추가했다.
- `tests/process_cancellation_tests.cpp`에 기본 token, 통지 횟수, 취소 후 등록, 해제, registration 이동, 이동한 source, source 소멸, 동시 취소와 등록 경합 test 10개를 추가했다.
- `tests/domain_model_tests.cpp`의 diagnostic 이름 표에 프로세스 code 6개를 추가했다.
- `gitman_tests`에 `gitman_process`를 링크했다.
- `utf8.cpp`, `win32_application.cpp`, `ui_theme.h`를 clang-format 19.1.5 결과로 정렬해 aggregate `gitman_format_check`를 다시 통과시켰다. 의미 변경은 없다.
- `docs/code_style.md` 2장에 `ColumnLimit` 200 안의 표현식은 formatter 결과가 기준이라는 규칙을 명시했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 78/78 통과했고 `/analyze`도 무경고로 통과했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d1-test.md`에 기록했다.

### 영향 요구사항

- REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-007

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D1-FIX`는 사용자 확인 후 생략한다.
- `S3-D2-CODE` 승인 전에는 `CreateProcessW` 실행 코드를 작성하지 않는다.
- 자식 프로세스를 실행하는 test와 콘솔 도우미 실행 파일은 `S3-D2-TEST`에서만 추가한다.

## 2026-08-16 - 단계 3 `S3-D1-CODE` 계약 production code 구현

### 사용자 지시

- `S3-P0` 계획을 승인하고 `S3-D1-CODE`를 진행한다.
- 체크포인트는 17개를 유지하고 활성 code page fallback은 단계 3에 포함한다.

### 반영 내용

- `domain/process_execution.*`에 스트림, 완료 사유, 출력 레코드와 실행 결과 값을 추가했다.
- `domain/diagnostic.*`에 프로세스 실행 관련 diagnostic code 6개와 이름 매핑을 추가했다.
- `application/process_request.*`에 요청 값과 filesystem 조회 없는 요청 검증, 어휘적 절대 경로 판정을 추가했다.
- 요청 검증에 NUL 문자, 중복 환경 override, 0 이하 timeout, 0 캡처 상한과 4 byte 미만 레코드 상한 규칙을 넣었다.
- `application/process_runner.h`에 sink 직렬화와 동기 `run` 계약을 정의했다.
- `application/process_cancellation.*`에 콜백 등록과 RAII 해제를 지원하는 취소 primitive를 추가했다.
- `gitman_process` static library target을 추가하고 `gitman_domain`에 프로세스 값 model을 넣었다.
- VS2022/VS2026 Debug build, 양 toolchain 전체 CTest 54/54, VS2022 `/analyze` 무경고, source style과 `git diff --check`를 통과했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d1-code.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-007, NFR-009

### 다음 작업 제한

- `S3-D1-CODE` 검수 전에는 `S3-D1-TEST`의 test source와 `gitman_tests` 링크를 추가하지 않는다.
- 기존 aggregate `gitman_format_check` 실패 3개 파일은 사용자가 처리 방향을 정하기 전에는 수정하지 않는다.
- Win32 프로세스 실행 구현은 `S3-D2-CODE` 승인 후에만 작성한다.

## 2026-08-16 - 단계 2 승인과 단계 3 `S3-P0` 계획 작성

### 사용자 지시

- 단계 3을 진행한다.
- 단계 2처럼 모든 세션을 자동으로 처리하지 말고 계획, 작업과 테스트 각 과정의 중간에 진행 내용과 처리 방침을 보고하고 검수받는다.

### 반영 내용

- 단계 3 진행 지시를 단계 2 최종 승인으로 처리하고 `docs/plan.md`, `docs/stage-2-plan.md`, `docs/verification/2026-08-16-stage-2.md`의 승인 대기 상태를 완료로 갱신했다.
- `docs/stage-3-plan.md`에 프로세스 실행 계층 구현 계획을 작성했다.
- 실행 API를 동기 블로킹 `run` 하나로 두고 scheduler와 스레드 배치를 단계 6~7로 미루는 범위를 정의했다.
- 셸 미사용, 절대 경로 실행 파일, `CommandLineToArgvW` 규칙 인자 인용, stdin `NUL` 연결, handle 상속 제한과 job object 트리 종료 계약을 제안했다.
- pipe별 reader 스레드, 줄 단위 레코드, chunk 경계 UTF-8 보류, 단독 `\r` progress 표시, 캡처 상한과 실행 단위 단조 sequence를 제안했다.
- timeout 및 취소 primitive와 Win32 event 콜백 연결, 앱 종료 시 취소 후 join 정책을 제안했다.
- URL userinfo, 자격 증명 option, token prefix 마스킹 규칙과 `std::regex` 미사용 결정을 제안했다.
- 실제 Git/SVN 대신 결정적 검증에 사용할 테스트 전용 콘솔 도우미 실행 파일 target을 제안했다.
- `CODE` / `TEST` / `FIX` 5분할과 `S3-V1`로 구성한 17개 체크포인트 및 검수 게이트를 정의했다.
- `docs/handoff.md`의 현재 단계, 진행 원장과 보고 방식 지시를 단계 3 기준으로 갱신했다.

### 영향 요구사항

- REQ-006, REQ-007, REQ-008, REQ-009~REQ-013
- NFR-005~NFR-009

### 다음 작업 제한

- `S3-P0` 계획 승인 전에는 `src/`와 `tests/`에 프로세스 실행 관련 source를 추가하지 않는다.
- 승인 후에도 `S3-D1-CODE` 한 구간만 수행하고 보고 뒤 중지한다.
- ADR-004의 범용 메시지 구조는 단계 6 이전 별도 승인 없이 구현하지 않는다.

## 2026-08-16 - 단계 2 최종 자동 검증

### 사용자 지시

- `S2-D5-TEST` 결과 이후 다음 작업을 계속 진행한다.
- 이를 `S2-D5-TEST` 승인, 무결함 `S2-D5-FIX` 생략과 `S2-V1` 진행 지시로 처리했다.

### 반영 내용

- VS2022 Debug/Release build와 각 전체 CTest 54/54가 통과했다.
- VS2022 `/analyze` build가 경고 없이 통과했고 VS2026 Debug build 및 전체 CTest 54/54도 통과했다.
- 기존 aggregate format 기준선 위반 3개 파일을 clang-format 19.1.5로만 정렬하고 모든 build, test와 분석을 다시 통과시켰다.
- aggregate `gitman_format_check`와 source style 검사가 통과했다.
- Release install tree에 6,255,616 byte의 `bin/gitman.exe` 한 파일만 생성되는 것을 확인했다.
- 설치본의 CPU, auto, 강제 fallback과 Direct3D smoke test가 모두 종료 코드 0으로 통과했다.
- `dumpbin /dependents`로 Windows 시스템 DLL 외의 VC runtime, 프로젝트 및 제3자 DLL이 없음을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-2.md`에 기록했다.

### 다음 작업 제한

- `S2-V1` 자동 완료 조건은 충족했으며 단계 2 최종 사용자 검수 대기 상태다.
- 사용자 최종 승인 전에는 단계 3 프로세스 실행 계층을 시작하지 않는다.

## 2026-08-16 - 단계 2 positional launch path test 작성

### 사용자 지시

- `S2-D5-CODE` 검수 뒤 계속 진행한다.
- 이를 `S2-D5-CODE` 승인과 다음 체크포인트 `S2-D5-TEST` 진행 지시로 처리한다.

### 반영 내용

- 문서 경로가 없는 기존 시작에서 optional launch path가 비어 있는지 검증했다.
- 한글과 공백을 포함한 `.verison-list` 원문, path와 renderer option의 전후 순서 및 대문자 확장자 허용을 검증했다.
- option이 사이에 있어도 두 번째 positional path를 거부하는지 검증했다.
- `.json`, `.verison-list.bak`과 trailing space suffix를 잘못된 확장자로 거부하는지 검증했다.
- 신규 Catch2 test 4개와 VS2022/VS2026 전체 CTest 54/54가 통과했다.
- production source를 변경하지 않았고 `S2-D5-FIX` 후보도 발견하지 않았다.

### 다음 작업 제한

- `S2-D5-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D5-FIX`를 생략하거나 `S2-V1` 단계 2 최종 검증을 시작하지 않는다.
- 승인되고 무결함 결과가 확인되면 `S2-D5-FIX`를 수정 없이 생략한 뒤 `S2-V1`만 진행한다.

## 2026-08-16 - 단계 2 positional launch path production code 구현

### 사용자 지시

- `S2-D4-TEST` 이후 남은 단계 2 작업을 확인하고 진행한다.
- 이를 `S2-D4-TEST` 승인, 무결함 `S2-D4-FIX` 생략과 다음 체크포인트 `S2-D5-CODE` 진행 지시로 처리한다.

### 반영 내용

- `application_options`에 선택적인 UTF-8 `workspace_document_path`를 추가해 기존 Win32 진입 경계를 통해 보존한다.
- 실행 파일 이름 뒤 하나의 positional `.verison-list` path를 허용하고, 두 번째 positional path는 한 창당 한 문서 계약에 따라 거부한다.
- 확장자는 Windows 사용 방식에 맞게 ASCII 대소문자를 구분하지 않고 판정하며 다른 확장자와 backup suffix는 거부한다.
- 미지원 `--` option은 문서 경로로 오인하지 않고 기존 command-line 오류로 유지한다.
- parser는 path 원문을 바꾸거나 filesystem을 조회하지 않으며 실제 load, schema, backup과 recovery 판단은 project store 경계에 남긴다.
- VS2022와 VS2026 Debug build 및 기존 전체 CTest 50/50을 통과했다.
- 이번 체크포인트에서는 test source와 fixture를 변경하지 않았다.

### 다음 작업 제한

- `S2-D5-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D5-TEST`의 launch path test를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D5-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-15 - 단계 2 원자적 저장과 복구 test 작성

### 사용자 지시

- 다음 작업 진행을 시작한다.
- 이를 `S2-D4-CODE` 승인과 다음 체크포인트 `S2-D4-TEST` 진행 지시로 처리한다.

### 반영 내용

- in-memory file adapter fake로 최초 생성, 기존 교체, exact-byte 동시 수정, 후보 재검증과 write/flush/replace 실패 주입을 검증했다.
- canonical JSON의 UTF-8 무 BOM, 공백 4칸, CRLF와 unknown field 및 원문 path 보존을 검증했다.
- valid/invalid backup 탐지, 자동 적용 금지와 명시적 backup load 뒤 save 복구를 검증했다.
- 실제 Win32 임시 디렉터리에서 최초 생성, `ReplaceFileW` 교체, 직전 원본 backup, 외부 변경 충돌과 replace 실패 뒤 임시 파일 정리를 검증했다.
- 신규 Catch2 test 9개와 VS2022/VS2026 전체 CTest 50/50이 통과했다.
- production source를 변경하지 않았고 `S2-D4-FIX` 후보도 발견하지 않았다.

### 다음 작업 제한

- `S2-D4-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D4-FIX`를 생략하거나 `S2-D5-CODE`를 시작하지 않는다.
- 승인되고 무결함 결과가 확인되면 `S2-D4-FIX`를 수정 없이 생략한 뒤 `S2-D5-CODE`만 진행한다.

## 2026-08-15 - 단계 2 원자적 저장과 복구 production code 구현

### 사용자 지시

- `S2-D3-TEST`를 검수 완료로 처리한다.
- 발견 production 결함이 없는 `S2-D3-FIX`를 생략한다.
- `S2-D4-CODE`의 원자적 저장, 동시 수정 감지, backup 및 명시적 recovery production 구현을 시작한다.

### 반영 내용

- 호출자가 해석하지 않는 revision token과 primary/backup load, save 결과를 제공하는 project store 계약을 추가했다.
- load 당시 primary 원문 byte와 save 직전 byte를 정확히 비교해 동시 수정이면 파일을 변경하지 않는다.
- unknown field와 원문 path를 보존하면서 UTF-8 무 BOM, 공백 4칸과 CRLF JSON을 serialize하고 저장 전 전체 후보를 재검증한다.
- 대상과 같은 디렉터리의 임시 파일에 write 및 `FlushFileBuffers`를 수행하고, 기존 문서는 `ReplaceFileW`와 `.bak`, 최초 문서는 write-through move로 교체한다.
- primary load 실패 시 유효한 backup을 진단으로만 알리고, 별도 `load_backup` 뒤 새 save 요청으로만 복구하도록 분리했다.
- write, flush와 replace 실패를 구분하는 file adapter 계약을 두어 다음 test 체크포인트의 결정적 실패 주입 경계를 마련했다.
- VS2022와 VS2026 Debug build 및 기존 전체 CTest 41/41을 통과했다.
- 이번 체크포인트에서는 test source와 fixture를 변경하지 않았다.

### 다음 작업 제한

- `S2-D4-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D4-TEST`의 save/recovery test와 fixture를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D4-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-15 - 단계 2 project path test 작성

### 사용자 지시

- `S2-D3-CODE`를 승인하고 test 작성을 진행한다.

### 반영 내용

- runtime 임시 디렉터리 fixture와 project path test 7개를 추가했다.
- document 기준 상대 path, drive, extended drive, UNC 및 extended UNC 정규화를 검증했다.
- 한글, emoji, 공백, long path와 directory, file, missing 및 invalid 상태를 검증했다.
- 부분 성공 문서에서 대소문자 중복 path를 제외하고 원래 JSON project index와 diagnostic 위치를 보존하는지 검증했다.
- VS2022와 VS2026 Debug build 및 전체 CTest 41/41을 통과했다.
- production source를 변경하지 않았고 path production 결함도 발견되지 않았다.

### 다음 작업 제한

- `S2-D3-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D3-FIX`를 생략하거나 `S2-D4-CODE`를 시작하지 않는다.

## 2026-08-15 - 단계 2 project path production code 구현

### 사용자 지시

- `S2-D2-TEST`를 승인하고 다음 구현을 진행한다.

### 반영 내용

- production 결함이 없었던 `S2-D2-FIX`를 생략하고 `S2-D3-CODE`를 진행했다.
- document 디렉터리 기준 상대 path와 absolute drive 및 UNC path의 lexical 정규화를 구현했다.
- UTF-8 원문을 보존하면서 별도의 absolute normalized path와 filesystem 상태를 계산한다.
- missing, inaccessible, not-directory와 invalid 상태 및 구조화 diagnostic을 구현했다.
- Windows case-insensitive ordinal 비교로 중복 normalized path의 첫 project만 유지한다.
- schema parse와 filesystem resolution을 분리하고 원래 JSON project index를 shadow metadata에 보존했다.
- Win32 path 및 UTF 구현을 UI/Skia와 독립적인 `gitman_win32_platform` target으로 분리했다.
- VS2022와 VS2026 Debug build 및 기존 CTest 34/34를 통과했다.
- 이번 체크포인트에서는 test source와 fixture를 변경하지 않았다.

### 다음 작업 제한

- `S2-D3-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D3-TEST` path test와 fixture를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D3-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-14 - 단계 2 schema parser test 작성

### 사용자 지시

- `S2-D2-CODE`를 승인하고 다음 작업을 진행한다.

### 반영 내용

- 정상, 손상, 부분 성공, 이전 및 미래 version과 unknown field fixture 6개를 추가했다.
- schema parser 계약 test 8개와 project field 오류 matrix 12종을 추가했다.
- unknown field의 JSON pointer escape와 입력 JSON byte의 정확한 shadow 보존을 검증했다.
- `.verison-list` test asset도 UTF-8 무 BOM 및 CRLF 검사를 받도록 품질 도구 범위를 확장했다.
- VS2022와 VS2026 Debug build 및 CTest 34/34를 통과했다.
- production source를 변경하지 않았고 parser production 결함도 발견되지 않았다.

### 다음 작업 제한

- `S2-D2-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D2-FIX`를 생략하거나 `S2-D3-CODE`를 시작하지 않는다.

## 2026-08-14 - 단계 2 schema parser production code 구현

### 사용자 지시

- `S2-D1-TEST`를 승인하고 다음 구현을 진행한다.

### 반영 내용

- production 결함이 없었던 `S2-D1-FIX`를 생략하고 `S2-D2-CODE`를 진행했다.
- schema version 1의 `.verison-list` JSON parser와 구조화 parse result를 추가했다.
- 문서 수준 실패, project별 부분 성공, optional 기본값, 중복 ID와 unknown field warning을 구현했다.
- unknown field의 후속 round-trip을 위해 입력 JSON byte를 shadow에 그대로 보존했다.
- public header에서 nlohmann/json, Win32와 Skia type을 노출하지 않았다.
- VS2022와 VS2026 Debug build 및 기존 CTest 26/26을 통과했다.
- 이번 체크포인트에서는 새 test source와 fixture를 작성하지 않았다.

### 다음 작업 제한

- `S2-D2-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D2-TEST`의 schema fixture와 test를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D2-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-14 - 단계 2 도메인 model test 작성

### 사용자 지시

- `S2-D1-CODE`를 승인하고 test 작성을 시작한다.

### 반영 내용

- workspace/project 기본값, VCS hint, path 상태, repository snapshot, operation과 diagnostic 계약 test 6개를 추가했다.
- VS2022와 VS2026 Debug build 및 CTest 26/26을 통과했다.
- test 작성 중 production source와 fixture를 변경하지 않았다.
- production 결함은 발견되지 않았다.

### 다음 작업 제한

- `S2-D1-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D2-CODE` schema/parser 구현을 시작하지 않는다.

## 2026-08-14 - `.verison-list` 작업공간 문서 결정 및 단계 2 계획 승인

### 사용자 지시

- 프로젝트 목록은 고정 `projects.json`이 아니라 solution 및 `.code-workspace`와 같은 문서로 취급한다.
- 확장자는 사용자 지시의 철자 그대로 `.verison-list`를 사용한다.
- Gitman을 해당 확장자의 Windows 연결 프로그램으로 동작하게 한다.
- 나머지 단계 2 계획을 승인하고 추가 승인 요청 없이 첫 production code 구현을 진행한다.

### 반영 내용

- 고정 기본 config 위치를 제거하고 한 창당 하나의 `.verison-list` 활성 문서로 변경했다.
- shell positional path로 문서를 여는 계약과 단계 8 file association 등록 및 제거 검증을 요구사항에 추가했다.
- `S2-P0`을 승인 완료로, 현재 체크포인트를 `S2-D1-CODE`로 갱신했다.
- UI, Win32와 JSON library에 의존하지 않는 `gitman_domain` target을 추가했다.
- project, repository snapshot, operation과 diagnostic production type 및 안정적인 이름 변환을 구현했다.
- VS2022와 VS2026 Debug build 및 기존 CTest 20/20, source style 80개를 통과했다.

### 다음 작업 제한

- `S2-D1-CODE`는 사용자 코드 검수 대기 상태다.
- 새 test source와 fixture는 사용자가 코드 검수 후 `S2-D1-TEST` 진행을 승인하기 전까지 작성하지 않는다.

## 2026-08-14 - 단계 2 구현 계획 및 세부 검수 게이트 작성

### 사용자 지시

- 단계 2를 진행하되 구현 계획, production code, test code와 bug 수정을 한 번에 수행하지 않는다.
- 일정 부분마다 사용자 검수를 받고 다음 작업으로 넘어간다.
- 짧은 세션과 context 압축에 대비해 영속 세션 메모리를 계속 갱신한다.

### 반영 내용

- `docs/stage-2-plan.md`에 도메인 model, schema v1, 부분 오류, path 정규화, 원자적 저장과 복구 계획을 작성했다.
- production code, test code와 bug 수정을 분리한 `S2-P0`~`S2-V1` 체크포인트를 정의했다.
- `docs/handoff.md`를 단계 2의 영속 진행 원장으로 갱신하고 현재 상태를 `S2-P0` 사용자 검수 대기로 기록했다.
- 이번 체크포인트에서는 source, test와 fixture를 변경하지 않았다.

### 다음 작업 제한

- 사용자가 `S2-P0` 계획을 승인하기 전에는 `S2-D1-CODE`를 시작하지 않는다.
- 승인 뒤에도 도메인 production code만 구현하고 새 test code는 작성하지 않은 채 다시 검수를 요청한다.

## 2026-08-14 - 캡션 UI 클래스 및 컬러 테마 팔레트 분리

### 사용자 수정 요청

- 캡션 커스터마이징을 전용 UI 클래스에서 통합 처리한다.
- 캡션 높이를 줄여 시각적 두께를 완화한다.
- 컬러 테마 확장을 위해 색상 값을 렌더링 코드에서 분리한다.

### 반영 내용

- `caption_ui`가 캡션 배경, 제목, 앱 아이콘, 창 버튼과 hover 렌더링을 통합하도록 분리했다.
- 캡션의 공용 논리 높이를 48px에서 40px로 줄이고 Win32 hit test도 같은 메트릭을 사용하도록 연결했다.
- 화면과 캡션 색상을 의미 기반 `ui_color_palette`로 분리하고 dark 및 high contrast 팔레트를 제공했다.
- 캡션 레이아웃과 테마 팔레트 선택을 단위 테스트로 검증한다.

## 2026-08-14 - 전역 공백 4칸 기본값 적용

### 사용자 수정 요청

- CMake와 기타 코드에도 공백 4칸을 기본 들여쓰기로 적용한다.

### 반영 내용

- `.editorconfig`에 C++, CMake, PowerShell, JSON, XML 및 Windows resource 코드의
  공백 4칸 기본값을 추가했다.
- `vcpkg.json`과 CMake continuation 들여쓰기를 공백 4칸으로 정렬했다.
- 줄 끝 정규화 및 source style 검사 대상에 `.editorconfig`를 추가했다.

## 2026-08-14 - 단일 문장 제어문 및 조건식 줄바꿈 규칙 반영

### 사용자 수정 요청

- 단일 문장 제어문은 가급적 중괄호를 생략한다.
- 여러 줄 조건식의 `&&`와 `||`는 새 줄의 처음에 둔다.

### 반영 내용

- `.clang-format`에 단일 문장 제어문 중괄호 제거와 이항 연산자의 줄 앞 배치를 추가했다.
- `src/` 전체를 새 규칙으로 재포맷했다.
- 코드 컨벤션 문서와 요구사항·빌드 안내를 갱신했다.

## 2026-08-14 - C++ 코드 컨벤션 갱신

### 사용자 수정 요청

- Allman 중괄호, namespace 들여쓰기, 제어문 본문 배치, 중괄호 초기화, 생성자
  초기화 목록, 긴 표현식의 끝 표시 규칙을 추가한다.

### 반영 내용

- `docs/code_style.md`에 C++ 코드 컨벤션을 단일 기준으로 정리했다.
- `.clang-format`에 Allman 중괄호, namespace 내부 1단계 들여쓰기, 짧은 제어문
  본문 줄바꿈, 생성자 초기화 목록의 새 줄 쉼표를 반영했다.
- 기존 C++ source와 test를 새 formatter 설정으로 정렬했다.
- `requirements.md`와 `build.md`가 새 컨벤션 문서를 참조하도록 갱신했다.

### 다음 작업 제한

- 부정 연산자 지양과 제어문 본문의 중괄호 생략 여부는 자동 실패 규칙이 아니라
  코드 리뷰 기준으로 둔다.

## 2026-08-14 - 단계 1 캡션 버튼 hover 동작 연결

### 사용자 수정 요청

- 커스텀 캡션 버튼에 포인터 hover 동작까지 구현한다.

### 반영 내용

- `WM_NCMOUSEMOVE`와 `TrackMouseEvent`의 non-client leave 추적으로 최소화, 최대화/복원과 닫기 버튼의 hover 상태를 관리한다.
- 최소화와 최대화/복원에는 중립 hover 배경을, 닫기에는 경고색 hover 배경을 표시한다.
- 포인터가 버튼 또는 non-client 영역을 벗어나거나 창이 비활성화 및 크기 변경되면 hover 상태를 즉시 해제한다.
- non-client mouse message는 기본 Win32 처리에도 전달해 maximize button hover의 Snap Layout 경로를 보존한다.
- 144 DPI 실제 창에서 세 버튼의 hover, 포인터 이탈 후 원상복귀와 최대화 hover의 Windows 11 Snap Layout 표시를 화면 캡처 및 픽셀 값으로 검증했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 캡션 버튼 Codicon 및 기능 연결

### 사용자 수정 요청

- 최소화, 복원, 최대화와 닫기 버튼을 각각 `chrome-minimize`, `chrome-restore`, `chrome-maximize`, `chrome-close` Codicon으로 표시한다.
- 커스텀 캡션 버튼에 실제 창 동작을 연결한다.

### 반영 내용

- 캡션 버튼의 수동 선 그리기를 제거하고 embedded Codicons glyph를 실제 bounds 기준으로 중앙 배치했다.
- 일반 상태에는 `chrome-maximize`, 최대화 상태에는 `chrome-restore`를 표시한다.
- non-client 버튼의 누름을 직접 처리해 최소화, 최대화/복원과 닫기 시스템 명령을 실행한다.
- embedded asset test가 캡션에서 사용하는 네 chrome glyph를 모두 검증하도록 확장했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 커스텀 캡션 검수 의견 반영

### 사용자 수정 요청

- 시스템 캡션과 커스텀 캡션이 겹쳐 그려지는 문제를 수정한다.
- 커스텀 캡션 아이콘에 Codicon을 자연스럽게 적용한다.

### 반영 내용

- 표준 overlapped window로 기본 배치를 받은 뒤 표시 전에 `WS_CAPTION`을 제거하고 frame을 재계산했다. resize, minimize, maximize와 system menu style은 유지했다.
- 커스텀 캡션 아이콘을 repository glyph에서 `source-control` Codicon으로 변경했다.
- Codicon을 UTF-8 text의 고정 좌표로 그리지 않고 실제 glyph bounds를 기준으로 아이콘 영역 중앙에 배치했다.
- embedded asset test가 실제 캡션에서 사용하는 `source-control` glyph의 존재를 검증하도록 변경했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 코드 스타일 검수 의견 반영

### 사용자 수정 요청

- template 선언과 함수 signature를 서로 다른 줄에 둔다.
- 중괄호 초기화 안쪽에 여백을 추가한다.
- 여러 줄 중괄호 초기화의 마지막 닫는 중괄호를 독립된 줄에 둔다.

### 반영 내용

- `.clang-format`에 `template<...>` 표기와 signature 줄 분리 및 중괄호 초기화 여백 규칙을 고정했다.
- 단계 1 C++ source와 test를 새 규칙으로 다시 포맷했다.
- 중첩된 여러 줄 초기화에는 후행 쉼표를 사용해 마지막 닫는 중괄호가 독립된 줄에 유지되도록 했다.
- source style 검사에 같은 줄의 template/signature와 여러 줄 닫는 중괄호 위반 검사를 추가했다.
- REQ-010, NFR-006과 빌드 안내에 검수된 스타일 기준을 기록했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 빌드 및 품질 기준선 구현

### 사용자 지시

- ADR-001 작업을 개시한다.
- 한 번에 한 단계씩 진행한다는 기존 검수 gate를 유지한다.

### 반영 내용

- CMake 4.2.0과 VS2022/VS2026 preset, v143/v145 정적 vcpkg triplet을 추가했다.
- Skia 148 Direct3D 및 CPU renderer, 자동 fallback, Win32 custom caption skeleton과 UTF 변환 경계를 구현했다.
- Codicons `v0.0.46-24` 자산, checksum, 생성 mapping, 라이선스와 vcpkg 고지문을 실행 파일 resource로 포함했다.
- Catch2 단위 테스트, renderer smoke test, source style, clang-format, `/analyze`와 install 검증을 구성했다.
- VS2022 Debug/Release, VS2026 Debug의 각 19개 CTest와 설치본 네 renderer smoke test를 통과했다.
- 결과와 남은 수동 검수 항목을 `docs/verification/2026-08-14-stage-1.md`에 기록했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2 도메인 및 설정 저장소 구현을 시작하지 않는다.
- ADR-004의 범용 메시지 구조는 단계 6 전 별도 설계 승인까지 구현하지 않는다.

## 2026-08-14 - 단계 0 검수 의견 반영 및 인계

### 사용자 지시

- 최소 CMake를 4.2.0으로 변경한다.
- CPU를 최소 surface 기준선으로 두고 Direct3D GPU surface를 기본으로 사용한다.
- Win32 API를 격리하고 로직은 UTF-8 `std::u8string`을 사용한다.
- 기본 Windows caption 대신 앱과 일체화된 caption UI를 제공한다.
- UWP를 제외하고 Gitman 자체를 단일 `.exe`로 배포한다.
- CMake install 결과를 `${workspaceRoot}/bin/`에 둔다.
- Git 최신 상태와 switch candidate는 remote-first, remote 없음은 local 기준으로 처리한다.
- submodule 동시 update option을 제공한다.
- 범용 스레드 메시지 구조는 구현 시점에 별도 설계 검수를 받는다.
- 이번 세션에서는 실제 구현하지 않고 후속 세션용 인수인계 문서를 작성한다.

### 반영 내용

- ADR-001, ADR-002, ADR-003을 `승인됨`으로 변경하고 검수 결정을 반영했다.
- ADR-004에 단계 6 이전의 범용 메시지 구조 설계 검수 gate를 추가했다.
- 요구사항, 계획과 단계 0 검증 기록을 같은 기준으로 갱신했다.
- `docs/handoff.md`에 후속 세션의 시작 조건, 금지 사항과 검수 지점을 기록했다.

### 다음 작업 제한

- 이번 세션에서는 코드와 build file을 추가하지 않는다.
- 후속 세션은 단계 1만 수행하고 사용자 검수 후 멈춘다.
- 단계 6의 메시지 관련 코드는 별도 설계 승인 전에는 작성하지 않는다.

## 2026-08-14 - 단계 0 결정 사항 확정

### 사용자 지시

- 모든 단계를 한 번에 수행하지 않고 한 단계씩 구현한 뒤 검수를 요청한다.
- 우선 단계 0만 진행한다.

### 반영 내용

- 단계 0을 `검수 대기`, 단계 1~8을 `시작 전`으로 기록했다.
- 플랫폼과 도구체인을 ADR-001로 기록했다.
- C++ dependency와 Codicons 고정 정책을 ADR-002로 기록했다.
- Git/SVN 실행 정책을 ADR-003으로 기록했다.
- 스레드와 상태 소유권을 ADR-004로 기록했다.
- 요구사항 기준선과 단계 0 검증 기록을 추가했다.

### 영향 요구사항

- REQ-003, REQ-005~REQ-015
- NFR-001~NFR-010

### 다음 작업 제한

사용자가 단계 0을 승인하기 전에는 단계 1의 CMake 및 source 파일을 추가하지 않는다.
