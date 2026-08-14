# ADR-003: Git 및 SVN 실행 정책

## 상태

승인됨 - 2026-08-14 사용자 검수 반영

## 배경

Git과 SVN은 외부 실행 파일이며 설치 버전, 인증, 저장소 상태에 따라 대화형 입력이나 위험한 변경을 요청할 수 있다. Gitman은 입력 없는 로그 뷰만 제공하므로 모든 명령이 예측 가능하게 종료되어야 한다.

## 결정

### 지원 버전과 발견

| 도구 | 최소 지원 버전 | 현재 환경 |
| --- | --- | --- |
| Git for Windows | 2.43.0 | 2.52.0.windows.1 |
| Apache Subversion CLI | 1.14.5 LTS | 설치되지 않음 |

시작 시 PATH와 사용자 설정 경로에서 실행 파일을 찾고 `--version`을 검사한다. 한 도구가 없거나 너무 오래되어도 앱 전체를 종료하지 않는다. 해당 VCS 카드에 설치 또는 버전 오류를 표시하고 관련 refresh, update, switch만 비활성화한다.

### refresh

- 시작과 JSON reload에서는 로컬 상태만 조회한다.
- 자동 주기 원격 조회는 최초 버전에 넣지 않는다.
- 전체 및 카드별 refresh에서만 Git fetch 또는 SVN 원격 상태 조회를 수행한다.
- Git의 최신 여부 기준은 원격 우선, 원격이 하나도 없을 때만 로컬로 한다.
- 현재 branch에 upstream이 있으면 해당 remote tracking branch를 최우선 비교 대상으로 사용한다.
- upstream이 없지만 remote가 있으면 JSON의 `preferred_remote`, `origin`, 유일한 remote 순서로 remote를 선택하고 현재 branch와 같은 이름의 remote branch를 비교 대상으로 찾는다.
- remote는 있지만 비교 가능한 branch가 없으면 로컬을 최신으로 오인하지 않고 `remote_target_missing`으로 표시한다.
- remote가 하나도 없으면 `local_only` 상태로 표시하고 현재 local HEAD와 작업 트리 상태를 최신 판단의 기준으로 사용한다.
- 선택된 remote를 `fetch --prune`한 후 local HEAD와 remote target의 ahead/behind를 계산한다.
- 카드에는 최신 상태뿐 아니라 판정 기준이 remote인지 local인지, 사용한 remote와 branch, 마지막 성공 확인 시각을 표시한다.
- SVN은 작업 복사본을 변경하지 않는 원격 상태 조회를 사용한다.
- 실패하면 마지막 성공 snapshot을 보존하고 `offline`, `authentication_required`, `error`를 구분한다.

### update

- Git update는 최신 판정에서 선택한 remote와 branch를 명시하고 기본적으로 `git pull --ff-only --recurse-submodules=no <remote> <branch>` 형태로 실행한다.
- update dialog 또는 카드 설정에 `update_submodules` option을 제공하며 기본값은 `false`다.
- `update_submodules=true`이면 parent repository의 fast-forward pull과 함께 `--recurse-submodules=on-demand`를 사용하고, 성공 후 `git submodule update --init --recursive`를 같은 카드 operation lane에서 실행한다.
- submodule 갱신 전에는 등록된 submodule의 dirty, conflict와 detached 상태를 수집하고, 사용자 변경을 덮을 가능성이 있으면 전체 갱신을 시작하지 않는다.
- submodule command, stdout, stderr와 실패 대상은 parent 카드 로그에 project-relative submodule 경로와 함께 기록한다.
- diverged 또는 유효한 remote update target 없음 상태에서는 pull을 실행하지 않는다.
- merge, rebase, squash, 자동 stash, 강제 reset을 수행하지 않는다.
- SVN update는 등록된 작업 복사본 root에서 비대화형으로 실행한다.
- 공통으로 conflict, 진행 중 작업, 보호 정책이 금지한 dirty 상태를 사전 검사한다.
- 성공과 실패 뒤 모두 로컬 상태를 다시 조회한다.

### switch

- switch는 dialog의 검증과 logic thread의 실행 직전 재검증을 모두 통과해야 한다.
- Git switch dialog는 remote와 remote branch를 먼저 조회하고 `원격 브랜치` 그룹을 local branch보다 위에 표시한다.
- dialog를 열 때 선택 remote를 fetch한다. fetch가 실패하면 cache된 remote tracking ref를 stale 상태로 표시하고 local branch 조회를 계속한다.
- remote가 하나도 없거나 유효한 remote candidate가 없으면 기존 local branch를 `로컬 브랜치` 그룹에 표시한다.
- remote branch와 같은 이름의 local tracking branch가 있으면 해당 local branch로 전환한다.
- remote branch에 대응하는 local branch가 없으면 사용자가 확인한 local 이름으로 명시적인 tracking branch를 만든 뒤 전환한다.
- 여러 remote에 같은 branch가 있으면 자동 선택하지 않고 dialog에서 remote를 명시적으로 선택하게 한다.
- Git 실행에는 `--no-guess`와 완전한 ref를 사용하여 선택하지 않은 remote branch로 암묵 전환하지 않는다.
- detached HEAD, `--discard-changes`, `--merge`, 기존 local branch의 강제 reset은 지원하지 않는다.
- SVN 최초 버전은 프로젝트 JSON의 `svn_switch_targets`에 등록된 URL만 선택할 수 있다.
- SVN URL은 형식, 접근 가능 여부, repository UUID와 root 일치를 검증한다.
- 검증 실패 시 dialog 오류만 갱신하고 process request를 생성하지 않는다.

### 인증과 프로세스

- Git은 기존 Git Credential Manager 또는 credential helper에만 위임한다.
- SVN은 기존 사용자 config와 인증 cache에만 위임한다.
- `GIT_TERMINAL_PROMPT=0`, SVN `--non-interactive` 등 비대화형 설정을 강제한다.
- 앱은 비밀번호, token, SSH key를 저장하거나 입력받지 않는다.
- 실행 파일과 인자 배열을 Win32 process API에 전달하고 shell을 사용하지 않는다.
- stdout과 stderr를 비동기로 수집하고 timeout 및 취소를 제공한다.

## 고려한 대안

### merge 또는 rebase pull

충돌 해결 UI와 터미널 입력이 없는 앱에서 자동 이력 변경은 복구가 어렵다. fast-forward가 불가능하면 오류로 종료한다.

### SVN 서버 레이아웃 자동 탐색

모든 저장소가 `trunk/branches/tags`를 사용하지 않으므로 JSON 허용 목록을 먼저 채택한다.

### 앱 내 자격 증명 입력

보안 저장소와 상호작용 설계가 별도로 필요하므로 초기 범위에서 제외한다.

## 결과

- remote 상태는 refresh 전까지 `unknown` 또는 마지막 확인 시각이 있는 stale 상태일 수 있다.
- remote가 없는 Git 저장소는 `local_only`로 정상 지원하지만 remote 기반 저장소와 시각적으로 구분한다.
- Git switch candidate는 remote-first로 정렬되며 remote branch 선택은 명시적인 tracking local branch 생성으로 이어질 수 있다.
- submodule update option은 기본적으로 꺼져 있으며 활성화 시 parent 카드 작업의 일부로 추적된다.
- 안전하지 않은 작업은 편의 기능으로 우회하지 않고 명시적 오류로 종료한다.
- SVN이 없는 현재 환경에서는 SVN 카드 기능이 비활성화되는 경로를 단계 4에서 검증해야 한다.

## 검증 방법

- Git/SVN 미설치와 최소 버전 미만 fixture를 검사한다.
- diverged Git 저장소에서 pull process가 시작되지 않는지 확인한다.
- upstream, same-name remote branch, preferred remote, origin, local-only 순서의 최신 기준 선택을 fixture로 검사한다.
- remote가 있지만 비교 branch가 없을 때 local 상태로 fallback하지 않는지 확인한다.
- remote branch를 local보다 먼저 표시하고 remote branch 선택 시 정확한 tracking branch command를 생성하는지 확인한다.
- 존재하지 않는 branch, ambiguous remote와 허용되지 않은 SVN URL에서 switch request가 생성되지 않는지 확인한다.
- submodule option이 꺼지면 submodule command를 실행하지 않고, 켜지면 dirty preflight와 recursive update를 수행하는지 확인한다.
- 인증 cache가 없는 환경에서 prompt 없이 제한 시간 안에 오류로 종료되는지 확인한다.

## 근거 자료

- [git pull 공식 문서](https://git-scm.com/docs/git-pull)
- [git switch 공식 문서](https://git-scm.com/docs/git-switch)
- [Apache Subversion 다운로드 및 LTS 정보](https://subversion.apache.org/download/)
- [Apache Subversion Windows 패키지 안내](https://subversion.apache.org/packages.html#windows)
