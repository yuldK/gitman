# 단계 4 구현 계획 - Git 및 SVN provider

## 1. 문서 상태

- 작성일: 2026-08-16
- 개정일: 2026-08-16 (1차 검수 결정 반영: SVN XML 미사용, SVN 미설치, `authentication_required` 추가, 시스템 로캘 유지, 문서 `settings` 도입)
- 대상: 구현 단계 4
- 현재 상태: `S4-P0` 개정 계획 제출, 사용자 검수 대기
- 현재 검수 게이트: `S4-P0` 계획 승인
- 관련 요구사항: REQ-002, REQ-006, REQ-007, REQ-009~REQ-014, NFR-005~NFR-009
- 상위 문서: `docs/plan.md` 3.2, 3.3, 3.5, 5.1~5.3, 6장, 8장 단계 4, `docs/decisions/ADR-003-vcs-runtime-policy.md`
- 선행 조건: 단계 3 최종 사용자 승인 (2026-08-16 사용자가 단계 4 진행을 지시함)

사용자는 단계 3과 같은 진행 방식을 요구했다. 계획, 각 작업 구간과 각 테스트 구간이 끝날 때마다 무엇을 했고 다음에 무엇을 어떻게 처리할지 보고한 뒤 검수를 기다린다. 사용자는 각 검수 후 직접 커밋을 진행한다.

## 2. 목표

단계 4는 단계 3의 범용 프로세스 실행 계층 위에 **Git과 SVN 고유 지식**을 올린다. 즉 실행 파일 발견, 명령 인자 조립, 기계 판독 출력 파싱, 공통 `repository_snapshot` 변환, update와 switch의 안전 검사 및 실행이다.

- Git과 SVN 실행 파일을 찾고 `--version`으로 최소 지원 버전을 확인한다.
- 프로젝트 문서의 `settings`에서 Git 및 SVN 실행 파일 경로를 수동으로 지정할 수 있게 한다.
- 한 도구가 없거나 오래되어도 앱 전체를 막지 않고 해당 VCS 기능만 비활성화한다.
- 등록 경로에서 저장소 종류와 저장소 루트를 판정한다.
- 로컬 상태(현재 ref, 리비전, 작업 트리, 진행 중 작업)를 원격 접근 없이 조회한다.
- Git 최신 상태를 remote-first로 판정하고 `local_only`, `remote_target_missing`을 구분한다.
- SVN 로컬 및 원격 상태를 조회하고 mixed revision과 switched subtree를 표시한다.
- `git pull --ff-only`와 optional recursive submodule update, `svn update`를 실행한다.
- 변경 명령 전에 dirty, conflict, 진행 중 작업, diverged 등 보호 정책을 검사하고 위험하면 실행하지 않는다.
- Git switch 후보를 remote-first로 조회하고 SVN switch 후보를 JSON 허용 목록으로 제한한다.
- switch 대상을 검증하고 실패 시 process request를 만들지 않는다.
- 모든 실행을 비대화형으로 강제해 인증 프롬프트에서 멈추지 않고 `authentication_required`로 끝낸다.
- offline, 인증 필요, 저장소 오류를 구분해 공통 상태로 변환한다.
- 예외를 public API 밖으로 던지지 않고 구조화 진단으로 반환한다.

## 3. 단계 4에서 하지 않는 일

- 깊이 1 자식 탐색과 후보 등록 (`discovery_service`, 단계 5)
- 카드별 `operation_lane`, 저장소별 직렬화, 전체 동시 실행 상한 (`task_scheduler`, 단계 6~7)
- 카드 UI, switch dialog UI, 로그 뷰와 ring buffer (단계 6~7)
- ADR-004의 범용 message queue, dispatcher와 thread bridge (단계 6 별도 설계 승인 후)
- 자동 주기 원격 조회 (ADR-003에 따라 최초 버전 제외)
- 자격 증명 입력 및 저장, 대화형 인증
- commit, push, merge, rebase, 충돌 해결, 자동 stash, 강제 switch, cleanup, revert
- Windows file association 등록 (단계 8)

단계 3과 같이 provider API는 **호출한 스레드에서 완료까지 블로킹하는 동기 API**로 만든다. 스레드 배치와 병렬성 정책은 단계 6~7이 결정하며, 이렇게 하면 ADR-004의 구현 차단 조건을 건드리지 않는다.

## 4. 사용자 검수에서 확정할 설계 제안

### 4.1 계층 경계와 provider 계약

- `domain`은 VCS 도구 정보, 저장소 snapshot 확장, switch 후보와 검증 결과 같은 값 type만 정의하고 Win32, Skia, nlohmann/json, XML 라이브러리를 참조하지 않는다.
- `application`은 `repository_provider`, `vcs_tool_registry`, `switch_validation_service`와 파일 존재 확인용 `vcs_file_probe` 추상 계약만 정의한다.
- `infrastructure`는 Git/SVN 명령 조립, 출력 파서와 provider 구현을 담는다. OS API를 직접 호출하지 않고 주입받은 `process_runner`와 `vcs_file_probe`만 사용한다.
- `platform/win32`은 `vcs_file_probe` 구현과 PATH 탐색 구현만 추가한다.
- provider는 `process_runner`를 생성자 주입으로 받는다. 따라서 대부분의 provider 로직은 실제 Git/SVN 없이 fake runner로 결정적으로 검증할 수 있다.
- 이 구조는 단계 2의 `project_path_resolver`, 단계 3의 `text_transcoder` 선례와 같다. 추상 계약은 상위 계층에 두고 Win32 구현체만 platform target에 둔다.

의존성 방향은 다음으로 제한한다.

```text
domain <- application contract <- infrastructure provider <- (주입) process_runner / vcs_file_probe
```

### 4.2 도구 발견과 버전 정책

| 항목 | 제안 |
| --- | --- |
| 탐색 순서 | 프로젝트 문서 `settings`의 수동 지정 경로 → `PATH` 환경 변수 → 알려진 기본 설치 경로 |
| 수동 지정 | 4.11의 `settings`에서 절대 경로를 지정하면 그 경로만 사용한다. 지정 경로가 없거나 실행할 수 없으면 자동 탐색으로 조용히 물러서지 않고 오류로 보고한다. 사용자가 의도해서 지정한 값이기 때문이다. |
| 탐색 구현 | `PATH`를 직접 분해해 `git.exe` / `svn.exe`를 찾는다. `CreateProcessW`의 PATH 탐색은 사용하지 않는다. 단계 3 계약이 절대 경로만 허용하기 때문이다. |
| 버전 확인 | 발견한 절대 경로로 `--version`을 실행하고 첫 줄을 파싱한다. |
| Git 버전 형식 | `git version 2.52.0.windows.1` → `2.52.0`. 4번째 이후 구성 요소와 플랫폼 접미사는 비교에 쓰지 않는다. |
| SVN 버전 형식 | `svn, version 1.14.5 (r1922182)` → `1.14.5`. |
| 최소 버전 | ADR-003대로 Git 2.43.0, SVN 1.14.5 |
| 실패 처리 | `not_found`, `version_unreadable`, `too_old`, `available`의 네 가지 상태로 구분한다. 앱은 종료하지 않고 해당 VCS 카드 기능만 비활성화한다. |
| 캐시 | 결과는 registry 인스턴스가 보관하고 명시적 재조사 요청에서만 갱신한다. 매 명령마다 `--version`을 실행하지 않는다. |
| timeout | 도구 확인은 5초 timeout을 준다. 응답 없는 실행 파일이 시작을 막지 않게 한다. |

현재 호스트에는 SVN이 설치되어 있지 않다. 따라서 `not_found` 경로는 실제로 검증할 수 있고, SVN이 있는 경로의 통합 검증은 8.4의 정책을 따른다.

### 4.3 비대화형 실행 환경과 명령별 실행 한도

모든 실행은 단계 3의 `process_request`로 만들며 셸을 거치지 않는다. provider는 명령 종류마다 다음 값을 채운다.

Git 공통 환경 override:

| 변수 | 값 | 이유 |
| --- | --- | --- |
| `GIT_TERMINAL_PROMPT` | `0` | 터미널 자격 증명 프롬프트를 금지한다. |
| `GCM_INTERACTIVE` | `Never` | Git Credential Manager의 GUI 프롬프트를 금지한다. |
| `GIT_ASKPASS` | 삭제 | 외부 askpass 프로그램 실행을 막는다. |
| `SSH_ASKPASS` | 삭제 | 같은 이유다. |
| `DISPLAY` | 삭제 | askpass GUI 경로를 추가로 차단한다. |
| `GIT_SSH_COMMAND` | `ssh -oBatchMode=yes` | SSH 암호 및 호스트 키 확인 프롬프트 대신 즉시 실패하게 한다. |
| `GIT_OPTIONAL_LOCKS` | `0` | 조회 명령이 index를 갱신하려고 잠그지 않게 한다. |

로캘은 **강제하지 않고 시스템 설정을 그대로 따른다**. 2026-08-16 사용자 결정이다. 한국어 환경에서는 Git과 SVN의 오류 메시지가 한국어로 나오며, 로그 뷰를 이 앱이 직접 제공하므로 사용자가 읽을 수 있는 원문을 보여 주는 편이 낫다. 인코딩 문제는 단계 3의 `active_code_page_fallback`이 이미 처리한다. 대신 오류 분류가 메시지 언어에 의존하면 안 되므로 4.10의 분류 전략을 로캘 독립 신호 중심으로 바꾼다.

Git 공통 인자 접두어:

| 인자 | 이유 |
| --- | --- |
| `-c core.quotepath=false` | 비ASCII 경로를 8진 이스케이프 없이 UTF-8 원문으로 출력한다. |
| `-c gc.auto=0` | 조회 명령이 background `gc`를 띄우지 않게 한다. 단계 3 감사에서 확인한 손자 프로세스 drain 문제의 근본 예방이다. |
| `-c color.ui=false` | ANSI escape가 로그와 파서에 섞이지 않게 한다. |
| `--no-pager` | pager 실행을 막는다. |

SVN 공통:

| 항목 | 값 |
| --- | --- |
| 인자 | `--non-interactive` |
| 환경 | 로캘을 강제하지 않는다 |
| 인증서 | `--trust-server-cert` 계열을 사용하지 않는다. 인증서 오류는 실패로 처리한다. |

명령별 timeout과 캡처 상한(단계 3에서 미정으로 남긴 값의 확정 제안):

| 명령 부류 | timeout | 스트림당 캡처 상한 |
| --- | --- | --- |
| 도구 `--version` | 5초 | 64 KiB |
| 로컬 조회(`status`, `rev-parse`, `for-each-ref`, `svn info`, `svnversion`) | 30초 | 8 MiB |
| 원격 조회(`fetch`, `svn status -u`, 원격 `svn info`) | 120초 | 8 MiB |
| update(`pull`, `submodule update`, `svn update`) | 600초 | 32 MiB |
| switch(`switch`, `svn switch`) | 300초 | 8 MiB |

인코딩 모드는 **모든 명령에서 `active_code_page_fallback`**을 쓴다. 로캘을 강제하지 않기로 했으므로 Git과 SVN 모두 시스템 언어 메시지를 낼 수 있고, Windows에서 그 인코딩은 UTF-8일 수도 활성 code page일 수도 있다. fallback 모드는 유효한 UTF-8 레코드는 건드리지 않고 그렇지 않은 레코드만 변환하므로, `core.quotepath=false`로 얻는 UTF-8 경로 출력은 영향을 받지 않는다. 이 동작은 단계 3에서 이미 구현하고 검증했다.

위 값은 상수 한 곳에 모아 두고 후속 단계에서 설정으로 바꿀 수 있게 한다.

### 4.4 Git 로컬 상태 조회와 파서

로컬 조회는 네트워크에 접근하지 않는다. 다음 명령만 사용한다.

| 순서 | 명령 | 얻는 값 |
| --- | --- | --- |
| 1 | `rev-parse --absolute-git-dir --show-toplevel` | 저장소 루트와 git dir. 실패하면 Git 저장소가 아니다. |
| 2 | `status --porcelain=v2 --branch --untracked-files=normal -z` | `branch.oid`, `branch.head`, `branch.upstream`, `branch.ab`, 변경 항목 |
| 3 | `submodule status --recursive` (submodule이 있을 때만, D5) | submodule 경로와 dirty 및 detached 표시 |

`--porcelain=v2 -z`를 쓰는 이유는 경로에 개행과 공백이 있어도 레코드 경계가 흔들리지 않기 때문이다. 단계 3의 파이프라인은 줄 단위 레코드를 만들지만 provider는 원본 byte가 필요하므로, provider는 **레코드 텍스트를 그대로 이어 붙여 재조립**하는 대신 `-z` 출력을 받을 수 있는 수집 sink를 사용한다. 이 부분은 4.10에서 다룬다.

작업 트리 상태 매핑:

| porcelain v2 레코드 | 매핑 |
| --- | --- |
| `1` / `2` 항목의 XY 중 하나라도 비`.` | `modified_count` 증가 |
| `u` 항목 | `conflicted_count` 증가 |
| `?` 항목 | `untracked_count` 증가 |
| `!` 항목 | 무시(무시된 파일은 세지 않는다) |

`working_tree_state`는 conflict가 하나라도 있으면 `conflicted`, 아니면 modified가 있을 때 `modified`, 둘 다 없으면 `clean`이다. untracked만 있는 경우는 `modified`로 보되 개수를 구분해 카드에서 다르게 표시할 수 있게 한다.

진행 중 작업은 porcelain v2가 보고하지 않으므로 git dir 안의 표식 파일을 `vcs_file_probe`로 확인한다.

| 표식 | 상태 |
| --- | --- |
| `MERGE_HEAD` | merge 진행 중 |
| `rebase-merge/`, `rebase-apply/` | rebase 진행 중 |
| `CHERRY_PICK_HEAD` | cherry-pick 진행 중 |
| `REVERT_HEAD` | revert 진행 중 |
| `BISECT_LOG` | bisect 진행 중 |
| `index.lock` | 다른 Git 프로세스가 작업 중 |

detached HEAD는 `branch.head`가 `(detached)`인지로 판정한다. `current_reference`에는 detached일 때 짧은 커밋 ID와 detached 표시를 함께 담는다.

### 4.5 Git remote-first 최신 상태

ADR-003의 순서를 그대로 구현한다.

1. `remote` 명령으로 remote 목록을 얻는다. 하나도 없으면 `local_only`로 판정하고 fetch하지 않는다.
2. 현재 branch에 upstream이 있으면 그 remote tracking branch를 비교 대상으로 한다.
3. upstream이 없으면 `preferred_remote` → `origin` → 유일한 remote 순서로 remote를 고른다. 후보가 여럿이고 위 규칙으로 좁혀지지 않으면 자동 선택하지 않고 `remote_target_missing`으로 보고한다.
4. 고른 remote에서 현재 branch와 같은 이름의 remote branch를 찾는다. 없으면 `remote_target_missing`이며 local로 fallback하지 않는다.
5. `fetch --prune <remote>`를 실행한다.
6. `rev-list --left-right --count <local>...<remote-target>`으로 ahead/behind를 계산한다.
7. `comparison`은 `remote`, `comparison_target`은 사용한 remote branch 전체 이름, `remote_checked_at`은 fetch 성공 시각으로 채운다.

detached HEAD는 비교할 local branch 이름이 없으므로 upstream 규칙을 적용하지 않고 `remote_target_missing`으로 보고한다.

fetch 실패는 4.10의 분류기로 `offline`, `authentication_required`, `error`를 구분한다. 실패해도 로컬 snapshot과 마지막 성공 원격 확인 시각은 보존한다.

### 4.6 SVN 상태 조회

2026-08-16 사용자 결정에 따라 **XML을 사용하지 않고 XML 파서 dependency도 추가하지 않는다**. 대신 값 하나만 출력하는 SVN의 기계 판독 계약을 조합한다. `vcpkg.json`과 ADR-002는 그대로 둔다.

| 순서 | 명령 | 얻는 값 | 출력 형태 |
| --- | --- | --- | --- |
| 1 | `info --show-item <item> <path>` | `url`, `relative-url`, `repos-root-url`, `repos-uuid`, `revision`, `wc-root` | 값 한 줄 |
| 2 | `status <path>` (비verbose) | 변경, 충돌, 미추적 항목과 switched 표시 | 고정 9칸 상태 문자 + 경로 |
| 3 | `svnversion <path>` | 작업 복사본 리비전 범위와 mixed, switched, modified 요약 | `4123:4168MS` 형태 한 줄 |
| 4 | `info --show-item revision <url>` | 원격 HEAD 리비전 | 값 한 줄 (네트워크) |

이 조합을 고른 근거는 다음과 같다.

- `--show-item`은 사람이 읽는 `key: value` 목록이 아니라 값 하나만 출력하므로 로캘과 무관하고 파싱이 사실상 필요 없다. 항목마다 프로세스를 새로 띄우는 비용은 명시적 refresh에서만 발생하므로 수용한다.
- 비verbose `status`는 앞 9칸이 고정 상태 문자이고 그 뒤가 전부 경로다. 공백이 포함된 경로에도 경계가 모호하지 않다.
- `status --verbose`는 리비전과 **작성자** 컬럼을 끼워 넣어 작성자 이름에 공백이 있으면 경로 경계가 흔들린다. 따라서 사용하지 않는다.
- mixed revision 판정은 `--verbose` 대신 `svnversion`이 담당한다. 출력 문법이 `[저리비전:]고리비전[M][S][P]` 한 줄로 좁아 파서 위험이 가장 작다.
- 원격 대비 상태는 `status --show-updates`의 로캘 의존 요약 줄(`Status against revision:`) 대신 4번의 원격 HEAD 리비전과 작업 복사본 리비전 비교로 정한다.

상태 매핑은 다음과 같다.

| 판정 | 근거 |
| --- | --- |
| `has_mixed_revision` | `svnversion` 출력에 `:`가 있음 |
| `has_switched_subtree` | `svnversion` 출력에 `S`가 있거나 `status` 5번째 칸이 `S`인 항목이 있음 |
| `working_tree_state` | `status`의 `C` 및 tree conflict → `conflicted`, `M`/`A`/`D`/`!` → `modified`, `?` → untracked 개수, 항목 없음 → `clean` |
| `behind` | 원격 HEAD 리비전 > 작업 복사본 고리비전 |
| `up_to_date` | 두 리비전이 같음 |

SVN에는 Git의 `ahead`와 `diverged`에 해당하는 개념이 없다. 커밋하지 않은 로컬 변경은 `working_tree`로만 보고하고 `remote_sync_state`에는 반영하지 않는다.

`svnversion`은 `svn.exe`와 같은 디렉터리에 함께 배포되므로 SVN 실행 파일을 찾은 뒤 같은 디렉터리에서 찾는다. 없으면 mixed revision과 switched 판정만 포기하고(각각 `unknown`) 나머지 조회는 계속한다. 4번은 네트워크를 쓰므로 refresh에서만 실행하고 시작 시에는 1~3만 실행한다.

### 4.7 update 실행과 보호 정책

update는 항상 **사전 검사 → 실행 → 사후 재조회** 순서다.

사전 검사(preflight)에서 다음 중 하나라도 해당하면 실행하지 않고 구조화된 차단 사유를 반환한다.

| 차단 사유 | 대상 |
| --- | --- |
| 작업 트리 conflict | Git, SVN |
| merge, rebase, cherry-pick, revert, bisect 진행 중 | Git |
| `index.lock` 존재 | Git |
| dirty 작업 트리 | Git, SVN (보호 정책 기본값) |
| `diverged` 또는 유효한 remote update target 없음 | Git |
| detached HEAD | Git |
| SVN switched subtree 또는 mixed revision | SVN |
| 도구 미설치 또는 버전 미달 | Git, SVN |

Git update 명령은 ADR-003대로 다음과 같다.

```text
git -c ... pull --ff-only --recurse-submodules=no <remote> <branch>
```

`update_submodules`가 켜지면 다음을 추가로 수행한다.

1. `submodule status --recursive`로 등록 submodule을 수집한다.
2. 각 submodule의 dirty, conflict, detached를 사전 검사한다. 하나라도 위험하면 parent pull을 **시작하지 않는다**.
3. parent는 `--recurse-submodules=on-demand`로 pull한다.
4. 성공한 경우에만 `submodule update --init --recursive`를 같은 카드 operation으로 실행한다.
5. submodule 명령의 출력은 project-relative submodule 경로와 함께 parent 로그에 기록한다.

SVN update는 등록된 작업 복사본 루트에서 `update --non-interactive`로 실행한다.

성공과 실패 모두 실행 직후 로컬 상태를 다시 조회한다. update 자체의 성공 여부와 조회 결과는 분리해 보고한다.

### 4.8 switch 후보와 검증 및 실행

#### Git

후보 조회는 remote-first다.

1. `fetch --prune <remote>`를 시도한다. 실패하면 중단하지 않고 cache된 remote tracking ref를 `stale`로 표시한다.
2. `for-each-ref --format=%(refname)%09%(objectname)%09%(upstream)%09%(HEAD) refs/remotes refs/heads`로 한 번에 조회한다.
3. `refs/remotes/<remote>/HEAD` 같은 심볼릭 항목은 후보에서 제외한다.
4. remote branch 후보를 먼저, local-only branch 후보를 다음에 정렬한다.
5. 같은 branch 이름이 여러 remote에 있으면 하나로 합치지 않고 remote별 후보로 남긴다. 자동 선택은 하지 않는다.

검증은 다음을 확인하고 각 실패를 서로 다른 코드로 반환한다.

| 검증 | 실패 코드 |
| --- | --- |
| 후보 목록에 없는 대상 | `target_not_found` |
| 현재 ref와 동일 | `already_on_target` |
| 다른 worktree가 사용 중 (`worktree list --porcelain`) | `target_in_use` |
| dirty, conflict, 진행 중 작업 | `working_tree_unsafe` |
| remote branch 선택이며 같은 이름의 local branch가 없음 | `tracking_branch_confirmation_required` (오류가 아니라 확인 요구) |
| 같은 이름 local branch가 다른 upstream을 가짐 | `tracking_branch_conflict` |

실행 명령은 다음과 같다.

```text
로컬 branch:  git switch --no-guess <branch>
tracking 생성: git switch --no-guess --create <local> --track <remote>/<branch>
```

`--no-guess`로 선택하지 않은 remote branch로의 암묵 전환을 막는다. `--discard-changes`, `--merge`, `--force`는 사용하지 않는다.

#### SVN

- 후보는 프로젝트 정의의 `svn_switch_targets`만 사용한다. 저장소 layout을 자동 가정하지 않는다.
- 검증 순서: URL 형식 → `info --xml <url>` 접근 가능 여부 → repository root 일치 → UUID 일치 → 현재 URL과 다름 → 작업 트리 안전 상태.
- 실행은 `switch --non-interactive <url> <wc-path>`다. `--ignore-ancestry`와 `--force`는 사용하지 않는다.

#### 공통

- 검증 실패 시 `process_request`를 만들지 않는다. 이것이 REQ-007의 핵심 수용 기준이다.
- 검증 결과에는 사람이 읽는 한국어 메시지와 기계 판정용 코드를 함께 담는다.
- 실행 직전에 같은 검증을 다시 수행한다. dialog 검증과 실행 사이의 상태 변경을 방어한다. 재검증 실패도 같은 코드로 보고한다.

### 4.9 도메인 모델 확장

기존 `repository_snapshot`은 단계 2에서 정의했다. 단계 4에서 다음을 추가한다.

| 위치 | 추가 |
| --- | --- |
| `remote_sync_state` | `authentication_required` |
| `working_tree_summary` | `operation_in_progress`, `has_index_lock`, `is_detached` |
| `repository_snapshot` | `svn_repository_root`, `svn_repository_uuid`, `has_switched_subtree`, `has_mixed_revision`, `submodules` |
| 신규 `vcs_tool_info` | 도구 종류, 실행 파일 경로, 버전 문자열, 파싱한 버전 3요소, 가용성 상태 |
| 신규 `switch_candidate` | 종류(remote/local/svn URL), 표시 이름, 완전한 ref 또는 URL, remote 이름, tracking 여부, `stale` 표시 |
| 신규 `switch_validation_result` | 통과 여부, 실패 코드, 한국어 메시지, tracking branch 생성 필요 여부 |
| 신규 `update_block_reason` | 4.7 차단 사유 열거 |
| 신규 `workspace_settings` | 4.11의 문서 수준 설정 값 |
| `workspace_document` | `settings` 필드 |
| `diagnostic_code` | `vcs_tool_not_found`, `vcs_tool_too_old`, `vcs_tool_path_invalid`, `vcs_command_failed`, `vcs_output_unparsable`, `authentication_required`, `remote_unreachable`, `repository_not_found`, `update_blocked`, `switch_target_rejected` |

`remote_sync_state`에 `authentication_required`를 추가하는 안은 2026-08-16 사용자가 승인했다. `docs/plan.md` 3.2의 Codicon 표에 Codicon `key`와 “인증 필요” 툴팁을 함께 반영한다.

### 4.10 오류 분류와 진단

- provider는 예외를 던지지 않고 결과 값과 `diagnostic` 목록을 반환한다.
- 로캘을 강제하지 않기로 했으므로 **번역되는 메시지 본문으로 분류하지 않는다**. 분류는 언어와 무관하게 남는 신호만 사용한다.

로캘 독립 신호는 다음과 같다.

| 신호 | 근거 |
| --- | --- |
| SVN 오류 코드 `E<숫자>` | SVN은 번역된 메시지에도 `svn: E170013:` 형태의 코드를 그대로 붙인다. SVN 분류의 1차 근거다. |
| libcurl 원문 문자열 | Git의 HTTP 전송 오류는 libcurl이 만들며 libcurl에는 번역 catalog가 없다. `Could not resolve host`, `Failed to connect`, `Connection timed out`은 한국어 환경에서도 영어로 남는다. |
| OpenSSH 원문 문자열 | `Permission denied (publickey)`, `Host key verification failed`도 같은 이유로 번역되지 않는다. |
| HTTP 상태 번호 | Git이 감싸는 문장은 번역되어도 `401`, `403`, `404` 숫자는 남는다. |
| 프로세스 완료 사유 | 단계 3의 `timed_out`, `cancelled`, `start_failed`는 메시지와 무관하다. |

| 분류 | Git 신호 | SVN 신호 |
| --- | --- | --- |
| `authentication_required` | `Permission denied (publickey)`, HTTP `401`, HTTP `403` | `E170001`, `E215004`, `E170013`과 인증 관련 하위 코드 |
| `offline` | `Could not resolve host`, `Failed to connect`, `Connection timed out`, `Connection refused` | `E170013`(연결 실패 문맥), `E175002` |
| `repository_not_found` | HTTP `404`, `rev-parse` 실패 종료 코드 | `E155007`, `E155010` |
| `error` | 위에 해당하지 않는 실패 | 같음 |

- 어떤 신호에도 맞지 않으면 추측하지 않고 `error`로 보고하며 마스킹된 원문 메시지를 진단에 담는다. 잘못된 `offline` 판정보다 안전하다.
- Git의 `terminal prompts disabled` 계열 메시지는 번역 대상이라 1차 근거로 쓰지 않는다. 다만 보조 신호로는 남겨 두고, 언어에 상관없이 동작하는 위 신호가 먼저 판정한다.
- SVN 코드와 HTTP 상태의 정확한 매핑 목록은 `S4-D1-CODE`에서 확정하고 fixture로 고정한다.
- 분류기는 별도 `vcs_error_classifier`로 분리해 규칙을 한 곳에 모으고 fixture로 검증한다. fixture에는 영어 출력과 한국어 출력을 모두 넣어 **언어가 달라져도 같은 분류가 나오는지**를 단정한다.
- 프로세스 실행 자체의 실패(`start_failed`, `timed_out`, `cancelled`)는 단계 3의 `process_completion`을 그대로 승격해 보고한다.
- `-z` 출력을 다루는 명령은 줄 단위 레코드 sink 대신 **원시 byte 수집 sink**를 사용한다. 단계 3의 sink 계약은 그대로 두고, provider가 자신의 sink 구현에서 레코드를 이어 붙이는 대신 `process_request`의 `maximum_record_bytes`를 크게 잡아 NUL 구분 출력을 손상 없이 받는 방법을 `S4-D2-CODE`에서 확정한다. 두 방법 모두 단계 3 코드를 바꾸지 않는다.
- 로그로 남기는 명령줄은 단계 3의 마스킹을 그대로 사용한다. provider는 애초에 자격 증명을 인자로 만들지 않는다.

### 4.11 프로젝트 문서의 `settings`

2026-08-16 사용자 지시로 Git과 SVN 실행 파일 경로를 수동으로 지정할 수 있게 한다. 저장 위치는 별도 설정 파일이 아니라 **프로젝트 문서(`.verison-list`)의 `settings` 속성**이다. 이후 환경설정 화면이 이 값을 읽고 쓴다.

```json
{
    "schema_version": 1,
    "settings": {
        "git_executable": "C:/Program Files/Git/cmd/git.exe",
        "svn_executable": ""
    },
    "projects": []
}
```

| 규칙 | 내용 |
| --- | --- |
| 스키마 버전 | 1을 유지한다. `settings`는 optional이며 없으면 기본값(전부 자동 탐색)이다. 기존 문서는 그대로 열린다. |
| 값 형식 | 빈 문자열 또는 절대 경로만 허용한다. 빈 문자열은 “지정하지 않음”이며 자동 탐색으로 간다. 상대 경로는 항목 오류로 보고한다. |
| 검증 시점 | 문서 parse 단계에서는 값 형식만 본다. 실제 실행 가능 여부는 `vcs_tool_registry`가 판정한다. |
| 지정 실패 | 지정한 경로가 없거나 `--version`이 실패하면 `vcs_tool_path_invalid`로 보고하고 자동 탐색으로 물러서지 않는다. |
| round-trip | 저장 시 값을 보존한다. 단계 2의 unknown field 보존 정책과 같은 방식으로 `settings` 안의 알 수 없는 키도 보존한다. |
| 확장 | 이후 단계가 동시 실행 상한, 로그 크기 같은 값을 같은 `settings`에 추가한다. 단계 4는 실행 파일 경로 두 개만 넣는다. |

이 변경은 단계 2에서 만든 `workspace_document`, schema parser와 `json_project_store`를 건드린다. 단계 4의 첫 구간에서 함께 처리하며, 기존 fixture 6종이 그대로 통과하는지도 회귀로 확인한다.

환경설정 **화면**은 단계 6~7 범위다. 단계 4는 값이 문서에서 provider까지 도달하는 경로만 만든다.

## 5. 코드 구조 제안

기존 `src/infrastructure`는 평면 구조이므로 하위 디렉터리를 만들지 않고 접두어로 구분한다.

```text
src
├── domain
│   ├── diagnostic.h/.cpp            (진단 code 추가)
│   ├── project.h/.cpp               (`workspace_settings`와 문서 필드 추가)
│   ├── repository_snapshot.h/.cpp   (상태 확장)
│   ├── vcs_tool.h/.cpp              (도구 종류, 버전, 가용성)
│   └── vcs_operation.h/.cpp         (switch 후보, 검증 결과, update 차단 사유)
├── application
│   ├── repository_provider.h        (조회, update, switch 추상 계약)
│   ├── vcs_tool_registry.h/.cpp     (탐색 결과 보관과 조회)
│   ├── vcs_file_probe.h             (git dir 표식 파일 확인 계약)
│   └── switch_validation_service.h/.cpp
├── infrastructure
│   ├── vcs_command_runner.h/.cpp    (request 조립과 출력 수집 공통 helper)
│   ├── vcs_error_classifier.h/.cpp
│   ├── vcs_version.h/.cpp           (`--version` 파싱과 최소 버전 비교)
│   ├── vcs_tool_discovery.h/.cpp    (PATH 분해와 후보 생성, 순수 함수)
│   ├── git_command_builder.h/.cpp
│   ├── git_status_parser.h/.cpp     (porcelain v2, rev-parse, for-each-ref, submodule status)
│   ├── git_repository_provider.h/.cpp
│   ├── svn_command_builder.h/.cpp
│   ├── svn_output_parser.h/.cpp     (`--show-item`, `status`, `svnversion` 출력)
│   └── svn_repository_provider.h/.cpp
└── platform/win32
    └── win32_vcs_file_probe.h/.cpp
```

```text
tests
├── vcs_version_tests.cpp
├── vcs_tool_discovery_tests.cpp
├── vcs_error_classifier_tests.cpp
├── git_command_builder_tests.cpp
├── git_status_parser_tests.cpp
├── git_repository_provider_tests.cpp
├── svn_command_builder_tests.cpp
├── svn_output_parser_tests.cpp
├── svn_repository_provider_tests.cpp
├── switch_validation_tests.cpp
├── git_integration_tests.cpp
├── helpers/fake_process_runner.h/.cpp
├── helpers/git_repository_fixture.h/.cpp
└── fixtures/vcs/**  (고정 출력 텍스트와 XML)
```

CMake target 구성 제안:

- `gitman_domain`에 `domain/vcs_tool.*`와 `domain/vcs_operation.*`를 추가하고 `domain/project.*`에 `workspace_settings`를 넣는다.
- 새 static library `gitman_vcs`를 만들고 `application/`과 `infrastructure/`의 VCS source를 넣는다. `gitman_domain`과 `gitman_process`를 PUBLIC으로 링크한다. XML 파서를 쓰지 않으므로 새 외부 dependency는 없다.
- `settings` 직렬화와 parse는 기존 `gitman_workspace`에 들어간다. `gitman_vcs`는 값만 읽으므로 두 library 사이에 새 의존이 생기지 않는다.
- `platform/win32/win32_vcs_file_probe.*`는 기존 `gitman_win32_platform`에 추가한다.
- `gitman_tests`는 `gitman_vcs`를 링크하고 fixture 디렉터리를 compile definition으로 받는다.
- 실행 파일 `gitman`은 이번 단계에서도 provider를 링크하지 않는다. 실제 조립은 단계 6이며, 그때 `gitman_process`와 함께 들어간다.

## 6. public API 초안

검수 편의를 위한 초안이며 세부 이름은 각 `CODE` 체크포인트에서 확정한다.

```cpp
enum class vcs_tool_availability
{
    unknown,
    not_found,
    version_unreadable,
    too_old,
    available,
};

struct vcs_tool_info
{
    repository_kind kind { repository_kind::unknown };
    vcs_tool_availability availability { vcs_tool_availability::unknown };
    std::u8string executable {};
    std::u8string reported_version {};
    std::array<std::uint32_t, 3> version {};
    std::vector<diagnostic> diagnostics {};
};

struct repository_query_result
{
    repository_snapshot snapshot {};
    std::vector<diagnostic> diagnostics {};
};

struct repository_change_result
{
    bool executed { false };
    std::optional<update_block_reason> blocked_by {};
    repository_snapshot snapshot {};
    std::vector<diagnostic> diagnostics {};
};

class repository_provider
{
public:
    [[nodiscard]] virtual repository_kind kind() const noexcept = 0;

    [[nodiscard]] virtual repository_query_result query_local(
        const project_definition& project,
        const process_cancellation_token& token) noexcept
        = 0;

    [[nodiscard]] virtual repository_query_result query_remote(
        const project_definition& project,
        const repository_snapshot& local,
        const process_cancellation_token& token) noexcept
        = 0;

    [[nodiscard]] virtual std::vector<switch_candidate> query_switch_candidates(
        const project_definition& project,
        const process_cancellation_token& token) noexcept
        = 0;

    [[nodiscard]] virtual repository_change_result update(
        const project_definition& project,
        const update_options& options,
        const process_cancellation_token& token) noexcept
        = 0;

    [[nodiscard]] virtual repository_change_result switch_to(
        const project_definition& project,
        const switch_candidate& target,
        const process_cancellation_token& token) noexcept
        = 0;
};
```

로그 전달은 provider가 만든 `process_output_sink`를 호출자가 주입하는 형태로 확정한다. 단계 4에서는 test용 수집 sink만 사용하고 카드별 로그 저장소 연결은 단계 7이 담당한다.

## 7. 세부 작업과 검수 게이트

production 구현, test 작성과 bug 수정은 같은 검수 구간에서 함께 수행하지 않는다. 기존 test 실행과 compile 확인은 어느 구간에서나 허용하지만 새 test source와 fixture는 test 구간에서만 작성한다.

| 순서 | 체크포인트 | 이번 구간에서 하는 일 | 구간 종료 조건 |
| --- | --- | --- | --- |
| 1 | `S4-P0` | 본 구현 계획과 설계 제안 작성 | 사용자 계획 승인 전 중지 |
| 2 | `S4-D1-CODE` | 도메인 확장, 문서 `settings` 스키마와 저장, provider/registry/probe 계약, 도구 탐색과 버전 비교, 실행 환경 정책, 오류 분류기, `gitman_vcs` target | build/style 결과와 diff 제시 후 중지 |
| 3 | `S4-D1-TEST` | 버전 파싱, PATH 탐색, `settings` parse 및 round-trip과 기존 fixture 회귀, 오류 분류, 도메인 확장 test와 fake runner 도우미 | test 결과와 발견 결함 제시 후 중지 |
| 4 | `S4-D1-FIX` | 승인된 계약 결함만 수정 | 회귀 결과 제시 후 중지. 결함이 없으면 사용자 확인으로 생략 |
| 5 | `S4-D2-CODE` | Git 명령 조립, `rev-parse` 및 porcelain v2 파서, 진행 중 작업 probe, 로컬 snapshot 변환 | build/style 결과와 diff 제시 후 중지 |
| 6 | `S4-D2-TEST` | 고정 출력 파서 test와 실제 임시 Git 저장소 통합 test | test 결과와 발견 결함 제시 후 중지 |
| 7 | `S4-D2-FIX` | 승인된 로컬 조회 결함만 수정 | 회귀 결과 제시 후 중지 |
| 8 | `S4-D3-CODE` | remote 열거와 target 선택, `fetch --prune`, ahead/behind, offline 및 인증 상태 판정 | build/style 결과와 diff 제시 후 중지 |
| 9 | `S4-D3-TEST` | target 선택 matrix, bare 원격 통합 test, 실패 분류 test | test 결과와 발견 결함 제시 후 중지 |
| 10 | `S4-D3-FIX` | 승인된 최신 판정 결함만 수정 | 회귀 결과 제시 후 중지 |
| 11 | `S4-D4-CODE` | SVN 명령 조립, `--show-item` 및 `status` 및 `svnversion` 파서, 로컬 및 원격 상태, mixed revision과 switched 판정 | build/style 결과와 diff 제시 후 중지 |
| 12 | `S4-D4-TEST` | SVN 고정 출력 fixture 파서 test와 미설치 경로 test | test 결과와 발견 결함 제시 후 중지 |
| 13 | `S4-D4-FIX` | 승인된 SVN 조회 결함만 수정 | 회귀 결과 제시 후 중지 |
| 14 | `S4-D5-CODE` | update preflight 보호 정책, `pull --ff-only`, submodule option, `svn update`, 사후 재조회 | build/style 결과와 diff 제시 후 중지 |
| 15 | `S4-D5-TEST` | 차단 사유 matrix, ff-only 성공 및 실패, submodule off/on, dirty submodule 차단 test | test 결과와 발견 결함 제시 후 중지 |
| 16 | `S4-D5-FIX` | 승인된 update 결함만 수정 | 회귀 결과 제시 후 중지 |
| 17 | `S4-D6-CODE` | switch 후보 조회, tracking branch 정책, SVN 허용 URL 검증, 검증 서비스와 실행 | build/style 결과와 diff 제시 후 중지 |
| 18 | `S4-D6-TEST` | 후보 정렬, ambiguous remote 거부, 미존재 ref 및 잘못된 URL 거부, 명령 미생성 test | test 결과와 발견 결함 제시 후 중지 |
| 19 | `S4-D6-FIX` | 승인된 switch 결함만 수정 | 회귀 결과 제시 후 중지 |
| 20 | `S4-V1` | 전체 build/test/analyze/install 검증과 단계 4 검증 문서 작성 | 최종 결과 제시 후 단계 4 승인 대기 |

각 체크포인트가 끝나면 반드시 `docs/handoff.md`에 다음을 갱신한다.

- 마지막 완료 또는 제출 체크포인트
- 사용자 승인 대기 항목
- 변경 파일과 검증 명령 및 결과
- 발견했지만 아직 수정하지 않은 결함
- 승인 후 허용되는 다음 작업 하나

사용자가 명시적으로 승인하기 전에는 다음 행으로 진행하지 않는다. test 구간에서 결함이 발견되어도 같은 구간에서 production code를 수정하지 않는다.

## 8. 테스트 계획

### 8.1 두 층 전략

provider가 `process_runner`를 주입받으므로 test를 두 층으로 나눈다.

| 층 | 대상 | 방식 | 결정성 |
| --- | --- | --- | --- |
| 단위 | 명령 조립, 출력 파서, target 선택, 검증 규칙, 오류 분류 | `fake_process_runner`에 고정 출력을 등록하고 provider를 그대로 돌린다 | 완전 결정적 |
| 통합 | 실제 Git 실행, 저장소 상태 변화 | 임시 디렉터리에 실제 저장소를 만들고 실제 `git.exe`를 실행한다 | Git 설치에 의존 |

`fake_process_runner`는 요청받은 실행 파일과 인자 배열을 기록하고 미리 등록한 응답을 돌려준다. 따라서 "이 조건에서 어떤 명령을 만들었는가"와 "이 출력에서 어떤 상태로 변환했는가"를 모두 한 test에서 고정할 수 있다. switch 검증 실패 시 명령을 만들지 않는다는 REQ-007 수용 기준도 이 기록으로 직접 검증한다.

### 8.2 Git 통합 test fixture

`git_repository_fixture` 도우미가 임시 디렉터리에 다음을 만든다. 모든 저장소는 로컬 파일 경로만 사용하고 네트워크에 접근하지 않는다.

| fixture | 구성 |
| --- | --- |
| `clean` | 커밋 하나, upstream 있음, 동기 상태 |
| `dirty` | 추적 파일 수정과 untracked 파일 |
| `conflicted` | merge 충돌을 남긴 상태 |
| `ahead` / `behind` / `diverged` | 로컬 bare 저장소를 원격으로 두고 양쪽 커밋을 조작 |
| `no_upstream` | branch에 upstream 미설정, remote는 존재 |
| `no_remote` | remote 없음 (`local_only` 확인) |
| `remote_target_missing` | remote는 있으나 같은 이름 branch 없음 |
| `detached` | detached HEAD |
| `in_progress` | rebase 중단 상태 |
| `submodule_clean` / `submodule_dirty` | 로컬 submodule 등록 |
| `unicode_path` | 한글, 공백, emoji가 포함된 경로 |

fixture는 실제 `git.exe`로 만들며 커밋 저자와 시각을 환경 변수로 고정해 재현성을 확보한다. Git이 없으면 통합 test를 skip하고 이유를 보고한다.

offline과 인증 필요 사례는 네트워크 없이 결정적으로 만든다. 존재하지 않는 로컬 경로를 remote URL로 등록하면 fetch가 실패하고, 도달 불가 주소를 쓰면 연결 실패 경로를 만들 수 있다. stderr 패턴 분류 자체는 fixture 기반 단위 test가 담당한다.

### 8.3 단위 test 항목

- 버전 파싱: Git과 SVN의 정상 형식, 접미사 포함, 잘못된 형식, 최소 버전 경계와 미달
- PATH 탐색: 빈 PATH, 따옴표 포함 항목, 중복 항목, 상대 경로 항목 무시, 확장자 처리
- `settings`: 없는 문서의 기본값, 빈 문자열, 절대 경로, 상대 경로 거부, 알 수 없는 키 보존, 저장 후 round-trip, 기존 fixture 6종 회귀
- 수동 지정 경로 우선순위: 지정값이 있으면 PATH를 보지 않음, 지정값이 잘못되면 자동 탐색으로 물러서지 않고 `vcs_tool_path_invalid`
- Git 명령 조립: 공통 `-c` 접두어와 환경 override 전부, 명령별 인자, 경로 인자 위치
- porcelain v2 파서: `1`/`2`/`u`/`?`/`!` 항목, `branch.*` 헤더, `branch.ab` 없음, detached, 개행 포함 경로, 빈 출력
- 진행 중 작업 판정: 표식 파일 조합
- remote target 선택: upstream 있음, `preferred_remote`, `origin`, 유일한 remote, remote 없음, 같은 이름 branch 없음, ambiguous
- ahead/behind 파싱과 상태 매핑: `up_to_date`, `behind`, `ahead`, `diverged`
- SVN 출력 파서: `--show-item` 값 한 줄과 빈 값, `status`의 각 상태 문자와 switched 칸, 공백 포함 경로, `svnversion`의 단일 리비전 및 범위와 `M`/`S`/`P` 조합, 비작업복사본 출력
- 오류 분류: 4.10 표의 각 신호와 어디에도 맞지 않는 실패. **같은 오류의 영어 출력과 한국어 출력이 같은 분류를 내는지** 확인
- update 차단 사유: 4.7 표의 각 조건과 조건 여러 개 동시 발생
- switch 검증: 미존재 대상, 동일 대상, worktree 사용 중, 위험한 작업 트리, tracking 확인 필요, ambiguous remote, 허용되지 않은 SVN URL, 다른 UUID
- 명령 미생성: 검증 실패 시 `fake_process_runner`에 switch 명령 기록이 없음

### 8.4 SVN 통합 검증 정책

2026-08-16 사용자는 SVN CLI를 설치하지 않기로 결정했다. SVN은 CLI가 설치되어 있다는 가정 아래 명령 연결만 해 둔다. 따라서 다음 정책을 적용한다.

1. SVN 파서와 명령 조립, 상태 변환, 검증 규칙은 고정 fixture와 fake runner로 완전히 검증한다. 이 범위로 "SVN switched 및 mixed-revision 사례가 공통 상태로 변환된다"는 완료 조건을 만족시킨다.
2. 실제 `svn.exe`를 쓰는 통합 test는 작성하되 도구가 없으면 skip하고 그 사실을 검증 기록에 남긴다. 현재 호스트에서는 항상 skip된다.
3. 도구 미설치 감지(`not_found`)는 오히려 이 호스트에서 실제로 검증 가능한 유일한 SVN 경로이므로 그대로 유지하고 test로 고정한다.
4. 실제 SVN 실행 경로는 단계 4에서 미검증으로 남기고 `docs/handoff.md`의 미해결 항목과 단계 4 검증 기록에 명시한다.

fixture로 쓸 출력은 Apache Subversion 공식 문서의 출력 계약을 근거로 작성하고 출처를 fixture 파일 주석에 남긴다.

### 8.5 stress 및 회귀

- `S4-V1`에서 여러 저장소 동시 조회 stress와 `--repeat until-fail:3` 재실행으로 flakiness를 확인한다.
- Git 통합 test는 프로세스를 여러 개 띄우므로 개별 CTest timeout을 기본 120초보다 높게 잡을 수 있다. 필요한 항목만 명시적으로 올린다.
- 임시 디렉터리는 test 종료 시 반드시 정리하고, 실패해도 남지 않도록 RAII로 관리한다.

## 9. 단계 4 완료 조건

- 모든 체크포인트가 개별 사용자 검수를 통과한다.
- Git과 SVN 실행 파일 발견, 버전 확인과 미설치 및 버전 미달 경로가 동작한다.
- Git 로컬 상태(정상, dirty, conflict, detached, 진행 중 작업)가 공통 snapshot으로 변환된다.
- Git 최신 상태가 remote-first로 판정되고 `local_only`, `remote_target_missing`, `offline`, `authentication_required`, `error`가 구분된다.
- SVN 정상, switched, mixed revision과 원격 대비 상태가 공통 snapshot으로 변환된다.
- 보호 정책이 위험 상태에서 update와 switch 명령 생성을 차단한다.
- `pull --ff-only`, optional recursive submodule update와 `svn update`가 동작하고 사후 재조회가 수행된다.
- switch 후보가 remote-first로 정렬되고 ambiguous remote를 자동 선택하지 않는다.
- switch 검증 실패 시 `process_request`가 생성되지 않는다.
- 인증이 필요한 실행이 프롬프트 없이 제한 시간 안에 오류로 끝난다.
- 한글, 공백, emoji가 포함된 저장소 경로에서 조회와 실행이 동작한다.
- VS2022 Debug/Release, VS2022 `/analyze`, VS2026 Debug, aggregate format/style과 전체 CTest가 통과한다.
- 단일 exe install 결과가 유지된다.
- `docs/verification/`의 단계 4 기록, `docs/change_log.md`와 `docs/handoff.md`가 최종 상태를 기록한다.

## 10. 계획 검수 결과

### 10.1 2026-08-16 확정된 사항

| 항목 | 결정 |
| --- | --- |
| SVN XML 처리 | 사용하지 않는다. `--show-item`, 비verbose `status`, `svnversion` 조합으로 대체하고 XML 파서 dependency를 추가하지 않는다. `vcpkg.json`과 ADR-002는 변경하지 않는다. |
| SVN CLI 설치 | 설치하지 않는다. CLI가 있다는 가정으로 명령만 연결하고 실제 실행 경로는 미검증으로 남긴다. 2026-08-17 사용자가 방침을 다시 확인했다. SVN은 개발 시점에 쓰지 않으며 나중에 프로덕션에 **부품 끼워넣듯 최소 노력으로 적용**할 수 있으면 충분하다. |
| `authentication_required` | `remote_sync_state`에 추가하고 `docs/plan.md` 3.2 Codicon 표에 `key`와 “인증 필요”를 반영한다. |
| 로캘 | 강제하지 않고 시스템 로캘을 따른다. 한국어 메시지를 그대로 보여 주고, 인코딩은 `active_code_page_fallback`으로 처리한다. 오류 분류는 4.10의 로캘 독립 신호만 사용한다. |
| 실행 파일 경로 수동 지정 | 프로젝트 문서의 `settings` 속성으로 제공한다. 환경설정 화면은 후속 단계에서 이 값을 읽고 쓴다. |

### 10.2 별도 이견이 없으면 계획대로 진행하는 사항

1. 4.3의 명령별 timeout과 캡처 상한 값. 단계 3에서 미정으로 남긴 항목의 확정이다.
2. Git 공통 인자에 `-c gc.auto=0`을 넣어 background 유지보수 프로세스를 원천 차단하는 안. 단계 3 감사에서 남긴 drain 유예 재검토 항목의 대응이다.
3. dirty 작업 트리에서 update를 기본 차단하는 정책의 유지. ADR-003과 `docs/plan.md` 3.5의 기본값을 그대로 따른다.
4. `CODE` / `TEST` / `FIX` 6분할과 `S4-V1`로 구성한 20개 체크포인트 순서.
5. 제안한 파일 배치(평면 `infrastructure`, 접두어 구분)와 새 `gitman_vcs` target 구성.
6. `settings`의 JSON 키 이름을 복수형 `settings`로 두는 것. 값이 여러 개이고 이후 단계가 항목을 계속 추가하기 때문이다.

`S4-D5`와 `S4-D6`은 각각 update와 switch로 크기가 크다. 반대로 체크포인트 수를 줄이길 원하면 `S4-D3`을 `S4-D2`에 합쳐 17개로 줄일 수 있다. 다만 remote 판정은 단계 4에서 가장 규칙이 많은 부분이라 분리를 권장한다.

## 10.3 현재 진행 상태

- `S4-P0`: 2026-08-16 사용자 승인 완료. 1차 검수에서 SVN XML 미사용, SVN CLI 미설치, `authentication_required` 추가, 로캘 미강제, 문서 `settings` 도입을 확정하고 계획서에 반영했다. 이후 "모든 VCS가 없는 환경도 상정해야 한다"는 지시를 받아 `S4-D1-CODE`에 반영했다.
- `S4-D1-CODE`: 도메인 확장, 문서 `settings` 스키마와 저장, provider/registry/probe 계약, 도구 탐색과 버전 비교, 실행 정책, 오류 분류기와 `gitman_vcs` target 구현 및 사용자 승인 완료. 결과는 `docs/verification/2026-08-16-stage-4-d1-code.md`에 기록했다.
- `S4-D1-TEST`: 계약 계층 test 56개와 `vcs_test_doubles` 도우미 작성 완료, 사용자 승인 완료. 전체 CTest가 139에서 195로 늘었고 양 toolchain에서 195/195 통과했다. 발견 production 결함이 없어 `S4-D1-FIX`는 생략했다. 결과는 `docs/verification/2026-08-16-stage-4-d1-test.md`에 기록했다.
- `S4-D2-CODE`: Git 명령 조립, `rev-parse` 배치 파서, porcelain v2 파서, 진행 중 작업 표식 probe와 로컬 snapshot 변환 구현 및 사용자 승인 완료. `status`에 `-z`를 쓰지 않기로 확정했고 bare 저장소와 git dir 안의 경로를 `unsupported_layout`으로 보고하기로 정했다. 결과는 `docs/verification/2026-08-16-stage-4-d2-code.md`에 기록했다.
- `S4-D2-TEST`: 파서와 provider test 39개, 실제 Git 통합 test 12개, `git_repository_fixture` 도우미와 실제 출력 fixture 5종 작성 및 사용자 승인 완료. 전체 CTest가 195에서 246으로 늘었고 양 toolchain에서 246/246 통과했다. 발견 production 결함이 없어 `S4-D2-FIX`는 생략했다. 결과는 `docs/verification/2026-08-16-stage-4-d2-test.md`에 기록했다.
- `S4-D3-CODE`: remote 열거와 대상 선택, `fetch --prune`, ahead/behind 계산과 offline·인증 판정 구현 및 사용자 승인 완료. remote branch 존재 확인을 fetch 뒤로 옮겼고(4.5의 4·5번 순서) `preferred_remote`가 없으면 warning과 함께 다음 규칙으로 진행한다. 결과는 `docs/verification/2026-08-16-stage-4-d3-code.md`에 기록했다.
- `S4-D3-TEST`: 대상 선택 matrix, 명령 순서와 실패 분류 test 22개, 실제 원격 통합 test 6개 작성 및 사용자 승인 완료. 전체 CTest가 246에서 274로 늘었고 양 toolchain에서 274/274 통과했다. 한국어 Git 출력 인코딩 실측도 함께 기록했다(11장 항목 해소). 발견 production 결함이 없어 `S4-D3-FIX`는 생략했다. 결과는 `docs/verification/2026-08-16-stage-4-d3-test.md`에 기록했다.
- `S4-D4-CODE`: SVN 명령 조립, `--show-item`·`status`·`svnversion` 파서, 로컬 및 원격 상태와 mixed revision·switched 판정 구현 및 사용자 승인 완료. 2026-08-17 사용자 방침(SVN은 나중에 최소 노력으로 붙이기만 하면 된다)에 따라 Git provider와 같은 구조를 유지하고 4.6의 명령 조합에서 벗어나지 않았다. 결과는 `docs/verification/2026-08-17-stage-4-d4-code.md`에 기록했다.
- `S4-D4-TEST`: SVN 파서와 provider test 30개, 통합 test 2개, 문서 근거 fixture 2개 작성 및 사용자 승인 완료. 전체 CTest가 274에서 306으로 늘었고 양 toolchain에서 306/306 통과했다. 이 호스트에서는 "SVN이 없어도 앱이 동작한다" test가 실행되고 실제 `svn.exe` test는 skip된다. 발견 production 결함은 없다. 결과는 `docs/verification/2026-08-17-stage-4-d4-test.md`에 기록했다.
- `S4-D5-CODE`: update 사전 검사 보호 정책, `pull --ff-only`, submodule 옵션, `svn update`와 사후 재조회 구현 및 사용자 승인 완료. 4.7의 submodule dirty 검사는 `git submodule status`가 내부 dirty를 보고하지 않아 충돌과 커밋 불일치로 좁혔고, SVN의 switched·mixed는 판정 값이 있을 때만 차단한다. 결과는 `docs/verification/2026-08-17-stage-4-d5-code.md`에 기록했다.
- `S4-D5-TEST`: 차단 사유 matrix, pull 성공·실패, submodule off/on test 28개와 실제 Git update 통합 test 4개 작성 및 사용자 승인 완료. 전체 CTest가 306에서 338로 늘었고 양 toolchain에서 338/338 통과했다. 원격 이력을 다시 쓴 저장소에서 `--ff-only`가 merge 없이 실패하는 것을 실제 Git으로 확인했다. 발견 production 결함이 없어 `S4-D5-FIX`는 생략했다. 결과는 `docs/verification/2026-08-17-stage-4-d5-test.md`에 기록했다.
- `S4-D6-CODE`: switch 후보 조회, tracking branch 정책, SVN 허용 URL 검증, 검증 서비스와 두 provider의 실행 구현 및 사용자 승인 완료. 4.8의 `for-each-ref` 형식에 `%(symref)`를 더해 심볼릭 항목을 값으로 제외하고, "local-only branch"를 "remote 후보로 도달할 수 없는 local branch"로 해석했으며, SVN 검증에서 로컬 판정을 네트워크보다 먼저 수행한다. tracking branch 생성 확인을 실을 자리로 `switch_candidate::tracking_branch_confirmed`를 추가했다. 결과는 `docs/verification/2026-08-17-stage-4-d6-code.md`에 기록했다.
- `S4-D6-TEST`: 검증 규칙, 후보 목록, 명령 미생성과 전환 실행 test 50개와 실제 Git 통합 test 5개 작성 완료, 사용자 검수 대기. 전체 CTest가 338에서 393으로 늘었고 양 toolchain에서 393/393 통과했다. 확인 후 실제로 tracking branch를 만들어 전환하는 것과 실제 worktree 점유 및 dirty 거부를 실제 Git으로 확인했다. 발견 production 결함은 없다. 결과는 `docs/verification/2026-08-17-stage-4-d6-test.md`에 기록했다.

## 11. 미결정 항목

- 저장소 종류 자동 판정에서 Git과 SVN 메타데이터가 동시에 발견되는 비정상 상황의 표시 방식. `vcs_hint`가 `automatic`일 때만 문제가 되며 `S4-D1-CODE`에서 확정한다.
- Git worktree와 bare repository의 지원 범위는 `docs/requirements.md` 6장에 따라 단계 5에서 확정한다. 단계 4의 보고 방식은 `S4-D2-CODE`에서 정했다. linked worktree는 그대로 조회하고, bare 저장소와 git dir 안을 가리키는 경로는 새 `repository_availability::unsupported_layout`으로 보고한다.
- submodule 상태를 카드에 얼마나 자세히 표시할지는 단계 6~7의 UI 결정이다. 단계 4는 데이터만 제공한다.
- SVN의 `ahead` 개념 부재를 UI에서 어떻게 표현할지는 단계 6에서 정한다.
- 환경설정 화면의 구성과 `settings` 편집 UX는 단계 6~7에서 정한다. 단계 4는 문서에서 provider까지의 데이터 경로만 만든다.
- `settings`에 추가될 나머지 항목(동시 실행 상한, 로그 ring buffer 크기 등)은 해당 단계에서 정의한다.
- Git이 시스템 로캘로 내는 메시지의 실제 인코딩 실측은 `S4-D3-TEST`에서 끝냈다. 이 호스트의 ANSI code page는 949지만 Git for Windows 2.52.0에 번역 catalog가 없어 메시지가 항상 영어이며, 비ASCII 내용은 UTF-8이라 `active_code_page_fallback`이 건드리지 않는다. 다른 호스트에는 번역본이 설치될 수 있으므로 오류 분류는 계속 로캘 독립 신호만 사용한다.
