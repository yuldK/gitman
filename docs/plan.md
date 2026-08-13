# Gitman 프로젝트 설계 및 구현 계획

## 1. 문서 목적

이 문서는 JSON에 등록된 Git 및 Subversion 저장소를 하나의 Skia 기반 GUI에서 조회하고 관리하는 Gitman의 요구사항, 설계 보완점, 구현 순서, 검증 기준을 기록한다.

- 모든 구현 지시는 요구사항 식별자로 추적한다.
- 설계 결정과 변경 사유는 `docs/` 아래에 계속 기록한다.
- 이 문서는 최초 계획의 기준선이며, 구현 중 확정되는 세부 사항은 관련 문서에 반영한다.

### 1.1 지시 반영 이력

| 날짜 | 사용자 지시 | 반영 위치 | 관련 요구사항 |
| --- | --- | --- | --- |
| 2026-08-14 | Windows 11 전용 Win32 네이티브 앱 및 CMake 사용 | 3.1, 단계 0~1 | REQ-003, REQ-013 |
| 2026-08-14 | 상태와 동작을 VS Code Codicons로 표시 | 3.2, 단계 1·6, 테스트 전략 | REQ-005 |
| 2026-08-14 | Git/SVN switch dialog와 유효성 검사, 실패 시 미실행 | 3.3, 5.3, 단계 4·7 | REQ-007 |
| 2026-08-14 | 선택 카드 전용 하단 터미널과 카드별 병렬 실행 | 3.4, 3.8, 3.9, 단계 7 | REQ-006, REQ-008 |
| 2026-08-14 | 명시적 refresh 버튼 제공 | 3.2, 5.1, 단계 6 | REQ-014 |
| 2026-08-14 | input, UI, logic thread 분리 및 logic 중심 제어 | 3.8, 권장 아키텍처, 단계 6 | REQ-015 |

## 2. 목표와 범위

### 2.1 핵심 목표

| 식별자 | 요구사항 | 완료 기준 |
| --- | --- | --- |
| REQ-001 | JSON 형식의 프로젝트 경로 목록을 읽는다. | 유효한 파일, 일부 잘못된 항목, 존재하지 않는 경로를 구분하여 표시한다. |
| REQ-002 | 각 경로의 Git 또는 SVN 상태를 표시한다. | 저장소 종류, 현재 브랜치 또는 SVN URL, 로컬 리비전, 원격 대비 상태, 변경 파일 유무를 표시한다. |
| REQ-003 | Windows 11 Win32 네이티브 앱에서 Skia로 GUI를 렌더링한다. | Win32 창의 크기 변경 및 고해상도 배율에서 카드와 로그 영역이 정상 렌더링된다. |
| REQ-004 | 등록 경로의 바로 아래 자식 디렉터리를 조사한다. | 깊이 1만 검사하고 발견 결과를 미리 보여 준 뒤 선택 항목을 JSON에 중복 없이 추가한다. |
| REQ-005 | 프로젝트별 가로형 카드를 제공한다. | 카드에 경로, 저장소 종류, Codicon 최신 상태, 브랜치 또는 URL, 리비전, 작업 트리 상태, 작업 버튼이 배치된다. |
| REQ-006 | Git pull 및 SVN update를 실행한다. | 비대화형 실행, 진행 상태, 종료 코드, 표준 출력과 오류 출력이 로그에 남는다. |
| REQ-007 | Git switch 및 SVN switch를 실행한다. | switch dialog에서 전환 후보를 검증하고, 실패 시 오류를 표시하며 명령을 실행하지 않는다. |
| REQ-008 | 선택 카드 전용 하단 로그 뷰를 제공한다. | 선택한 카드의 로그만 입력 기능 없이 표시하며, 카드별 병렬 작업의 로그가 섞이지 않는다. |
| REQ-009 | C++과 Skia를 중심으로 구현한다. | 애플리케이션 코드는 C++로 작성하고 UI 렌더링에는 Skia를 사용한다. 운영체제 API와 필수 라이브러리 사용 범위는 ADR로 기록한다. |
| REQ-010 | 코드 스타일과 파일 형식을 준수한다. | 들여쓰기 4칸, `snake_case`, UTF-8, CRLF를 자동 검사한다. |
| REQ-011 | 설명과 주석을 한국어로 작성한다. | 사용자 문서와 설명성 주석은 한국어이며, 식별자와 외부 API 고유 명칭은 원문을 허용한다. |
| REQ-012 | 지시와 검증 결과를 문서화한다. | 요구사항 변경, 설계 결정, 검증 결과가 `docs/`의 추적 가능한 문서에 남는다. |
| REQ-013 | CMake로 프로젝트를 구성하고 빌드한다. | Windows 11의 지원 컴파일러에서 CMake configure, build, test 명령이 재현된다. |
| REQ-014 | 명시적인 상태 새로 고침 기능을 제공한다. | 전체 및 카드별 refresh 버튼으로 최신 조회를 요청하고 진행 및 완료 상태를 확인할 수 있다. |
| REQ-015 | 입력, UI, 로직 스레드를 분리한다. | 세 스레드의 상태 소유권과 메시지 경계가 지켜지며, 사용자 동작은 로직 명령으로만 상태를 변경한다. |

### 2.2 초기 버전에서 제외할 범위

- 커밋, 푸시, 병합, 리베이스, 충돌 해결 기능
- 터미널 입력 및 대화형 셸
- Git 및 SVN 자격 증명 저장 기능
- 저장소 호스팅 서비스 전용 기능과 이슈 관리
- 깊이 2 이상의 자동 디렉터리 재귀 탐색

범위 밖 기능이 필요해지면 요구사항 식별자를 추가하고 우선순위를 다시 정한다.

## 3. 현재 설계에서 빠졌거나 보강할 부분

### 3.1 지원 운영체제와 Skia 백엔드

지원 환경을 **Windows 11 전용 Win32 네이티브 데스크톱 앱**으로 확정한다. 빌드 시스템은 CMake를 사용한다. 별도 크로스 플랫폼 창 라이브러리를 두지 않고 다음 기능을 Win32 경계에 구현한다.

- `wWinMain`, 창 클래스, `HWND`, 메시지 루프와 생명 주기
- Per-Monitor V2 DPI 인식과 논리 좌표 및 물리 픽셀 변환
- Win32 클립보드, 파일 선택 대화상자, 오류 알림
- Win32 경계의 UTF-16과 애플리케이션 내부 UTF-8 사이 변환
- Skia 글꼴 관리와 한글 폴백

Skia는 창과 입력을 제공하지 않으므로 `platform/win32` 뒤에 네이티브 처리를 격리한다. 최초 렌더링 기준선은 CPU 래스터 surface와 Win32 표시 경로로 두고, 기능 기준선을 통과한 뒤 Direct3D 기반 GPU surface를 선택적으로 추가한다. GPU 초기화 또는 장치 손실 시 CPU 기준선으로 복귀할 수 있어야 한다. 정확한 Skia 빌드 옵션과 GPU 백엔드는 ADR로 고정한다.

CMake는 애플리케이션, 테스트, 자산 생성 대상을 관리한다. `WIN32` 실행 파일 대상, 유니코드 컴파일 정의, Windows SDK 링크, Skia와 테스트 프레임워크 버전을 명시하고 CMake Preset으로 개발 및 배포 구성을 재현한다.

### 3.2 “최신” 상태의 정확한 정의

로컬 정보만으로 원격 저장소의 최신 상태를 확정할 수 없다. 상태를 단순한 불리언이 아닌 다음 열거형으로 표현한다.

| 상태 | 의미 |
| --- | --- |
| `up_to_date` | 추적 대상과 로컬이 동일하다. |
| `behind` | 로컬이 원격보다 뒤처져 있다. |
| `ahead` | 로컬에만 커밋이 있다. |
| `diverged` | 로컬과 원격 양쪽에 고유 변경이 있다. |
| `unknown` | 업스트림이 없거나 최신 조회를 아직 수행하지 않았다. |
| `offline` | 네트워크 또는 인증 문제로 원격 확인에 실패했다. |
| `error` | 저장소 손상 또는 명령 실행 오류로 판정할 수 없다. |

실제 앱에서는 열거형 이름을 그대로 노출하지 않고 Microsoft VS Code Codicons의 아이콘 글리프로 표시한다. 아이콘만으로 의미를 판단하게 하지 않고 색상, 한국어 툴팁, 접근성 이름을 함께 제공한다.

| 상태 | Codicon 이름 | 보조 표시 |
| --- | --- | --- |
| `up_to_date` | `pass` | “최신 상태” |
| `behind` | `arrow-down` | “원격보다 뒤처짐”과 커밋 또는 리비전 수 |
| `ahead` | `arrow-up` | “로컬이 앞섬”과 커밋 수 |
| `diverged` | `git-compare` | “분기됨”과 양방향 커밋 수 |
| `unknown` | `question` | “확인되지 않음” |
| `offline` | `debug-disconnect` | “오프라인 또는 인증 실패” |
| `error` | `error` | 오류 요약과 상세 보기 |

Codicons의 버전을 고정하고 아이콘 폰트와 공식 `mapping.json`에서 필요한 코드포인트만 C++ 헤더로 생성한다. 런타임 웹 또는 npm 의존 없이 폰트를 앱 자산으로 패키징하고 Skia font manager로 로드한다. `refresh`, `loading`, `repo-pull`, `git-compare`, `terminal` 등 동작 아이콘도 같은 자산을 사용한다. 아이콘 콘텐츠와 코드의 라이선스 및 attribution 파일을 배포물에 포함한다.

공식 기준 자료는 [microsoft/vscode-codicons](https://github.com/microsoft/vscode-codicons), 저장소의 `src/template/mapping.json`, `LICENSE`, `LICENSE-CODE`로 고정한다. 버전 갱신 시 폰트, 매핑, 생성 헤더, 라이선스 파일을 하나의 변경으로 검증한다.

Git은 `fetch` 성공 후 업스트림과의 ahead/behind를 계산한다. SVN은 원격 조회가 가능한 상태에서 `status -u` 또는 동등한 기계 판독 출력을 이용한다. 자동 원격 조회와 명시적 refresh를 분리하고, 마지막 성공 확인 시각을 카드에 표시한다.

### 3.3 Git과 SVN 개념 차이

SVN의 `switch`는 Git 브랜치 이름 전환과 달리 작업 복사본의 저장소 URL을 변경한다. 잘못된 URL 입력은 작업 복사본 전체에 영향을 줄 수 있으므로 다음 정보가 필요하다.

- Git: 로컬 브랜치, 원격 추적 브랜치, detached HEAD 여부, 업스트림
- SVN: 현재 URL, 저장소 루트, 상대 URL, 작업 복사본 리비전, 혼합 리비전 여부
- SVN 전환 대상: 사용자가 JSON에 명시한 허용 URL 목록 또는 저장소 레이아웃에서 안전하게 발견한 후보

Git과 SVN 모두 전환은 switch dialog에서만 시작한다. dialog는 현재 대상, 후보 목록, 작업 트리 상태, 검증 진행 상태, 오류 영역, 확인 및 취소 버튼을 제공한다. 초기 버전에서는 SVN 임의 URL 입력 대신 등록된 후보만 선택하게 한다. `trunk/branches/tags` 구조를 가정할지는 ADR로 명시하며, 자동으로 가정하지 않는다.

Git 대상은 유효한 ref인지, 실제로 존재하는지, 현재 브랜치와 다른지, 다른 worktree에서 사용 중인지 확인한다. SVN 대상은 URL 형식, 접근 가능 여부, 동일 저장소 UUID 및 저장소 루트 여부를 확인한다. 공통으로 dirty, conflict, 진행 중 작업 등 보호 정책을 검증한다. 검증 실패 시 dialog에 한국어 오류 메시지를 표시하고 확인 버튼을 비활성화하며 `switch` 명령을 생성하지 않는다. 검증 통과 뒤에도 로직 스레드가 실행 직전에 다시 검사하여 검사와 실행 사이 상태 변경을 방어한다.

### 3.4 외부 명령 실행과 비대화형 보장

Git과 SVN 실행 파일의 존재 및 버전을 시작 시 확인한다. 명령 문자열을 셸로 전달하지 않고 실행 파일과 인자 배열을 분리하여 경로 공백, 인자 삽입, 따옴표 문제를 방지한다.

- 표준 출력과 표준 오류를 별도로 비동기 수집한다.
- 종료 코드, 시작 및 종료 시각, 실행 시간, 취소 여부를 기록한다.
- Git에는 터미널 프롬프트를 비활성화하고, SVN에는 비대화형 옵션을 적용한다.
- 인증이 필요하면 프롬프트를 기다리지 않고 `authentication_required` 오류로 종료한다.
- 프로세스별 제한 시간과 취소 기능을 제공한다.
- 같은 저장소의 변경 명령은 직렬화하고, 읽기 조회와 변경 명령의 충돌을 막는다.
- 서로 다른 저장소의 명령은 제한된 worker pool에서 병렬 실행한다.
- 모든 출력 레코드에 `project_id`와 카드별 단조 증가 `sequence`를 부여한다.
- 앱 종료 시 실행 중인 프로세스 처리 정책을 명시한다.

### 3.5 작업 트리 보호 정책

`pull`, `update`, `switch` 전에 저장소를 다시 조회한다. 수정 파일, 충돌, 진행 중인 병합 또는 리베이스, 잠금, SVN switched subtree 및 혼합 리비전 등을 확인한다.

- 위험 상태에서는 기본적으로 실행을 차단하고 사유를 보여 준다.
- `git pull`의 merge/rebase/ff-only 정책은 설정으로 명시하며 암묵적으로 정하지 않는다.
- 자동 stash, 강제 switch, cleanup, revert는 초기 버전에 포함하지 않는다.
- 명령 실행 후 상태를 즉시 다시 조회한다.

### 3.6 경로 탐색 규칙

“바로 자식”을 등록 경로의 깊이 1 디렉터리로 정의한다. 탐색 시 다음 예외를 처리한다.

- 접근 거부, 사라진 경로, 네트워크 드라이브, 느린 경로
- 심볼릭 링크와 Windows junction의 추적 여부
- Git worktree, bare repository, submodule
- SVN 작업 복사본 내부의 `.svn`이 루트에만 존재하는 현대 형식
- 대소문자, 구분자, 상대 경로, 중복 경로의 정규화
- 하나의 경로에서 Git과 SVN 메타데이터가 동시에 발견되는 비정상 상황

발견 즉시 저장하지 않고 후보 목록, 저장소 종류, 제외 사유를 먼저 보여 준다. 사용자가 선택한 항목만 원본 JSON을 원자적으로 교체하여 저장한다.

### 3.7 JSON 스키마와 복구

설정 파일에는 명시적인 스키마 버전을 둔다. 최소 권장 형태는 다음과 같다.

```json
{
    "schema_version": 1,
    "projects": [
        {
            "id": "stable-generated-id",
            "path": "D:/work/example",
            "display_name": "example",
            "vcs_hint": "auto",
            "enabled": true,
            "svn_switch_targets": []
        }
    ]
}
```

- `path`는 읽을 때 정규화하되 사용자가 입력한 의미를 훼손하지 않는다.
- 알 수 없는 필드의 보존 여부와 향후 마이그레이션 정책을 정한다.
- 중복 ID와 중복 경로, 잘못된 타입을 항목별 오류로 보고한다.
- 저장은 임시 파일 작성, flush, 교체 순으로 수행하고 백업 및 복구 정책을 둔다.
- 다른 프로세스의 동시 수정 감지를 위해 읽은 파일의 수정 시각 또는 해시를 비교한다.

### 3.8 입력, UI, 로직 스레드 모델

`input_thread`, `ui_thread`, `logic_thread`를 명시적으로 분리하고 공유 가변 상태를 두지 않는다. 스레드 간 통신은 소유권이 분명한 단방향 메시지 큐와 불변 스냅샷만 사용한다.

| 실행 단위 | 소유권과 책임 | 금지 사항 |
| --- | --- | --- |
| `ui_thread` | Win32 `HWND`, 메시지 pump, Skia surface, 렌더링, 현재 frame snapshot 소유 | 저장소 명령 실행, JSON 접근, 애플리케이션 상태 직접 변경 |
| `input_thread` | 전달받은 포인터 및 키보드 이벤트의 정규화, hit test, 단축키 해석, `user_intent` 생성 | Win32 UI 객체 접근, repository provider 호출, 상태 직접 변경 |
| `logic_thread` | 유일한 mutable `app_state`, 선택 카드, dialog 상태, 검증, 작업 스케줄링, view snapshot 생성 | 렌더링, blocking 파일 및 프로세스 I/O |
| `worker_pool` | Git/SVN 조회 및 변경, 파일 I/O, 프로세스 출력 수집 | UI와 로직 상태 직접 접근, 다른 카드 작업 결과 변경 |

Win32의 창 메시지는 `HWND`를 만든 `ui_thread`에서 받아야 한다. 따라서 `WndProc`는 입력 관련 메시지를 최소한의 `raw_input_event`로 복사해 input queue에 넣고 즉시 반환한다. `input_thread`는 불변 layout snapshot을 이용해 이벤트를 `user_intent`로 변환하고 logic queue로 보낸다. `logic_thread`는 intent를 `logic_command`로 처리하고 새 `view_snapshot`을 UI queue에 게시한다.

```text
Win32 WndProc / ui_thread
        │ raw_input_event
        ▼
input_thread ── user_intent ──▶ logic_thread
                                      │ operation_request
                                      ▼
                                  worker_pool
                                      │ operation_event
                                      ▼
input_thread ◀── layout_snapshot ── logic_thread ── view_snapshot ──▶ ui_thread
```

사용자 상호작용으로 상태를 바꾸는 공개 진입점은 logic queue에 넣는 `logic_command`로 제한한다. UI 또는 입력 코드는 새로 고침, switch, pull, update를 직접 실행하지 않는다. 이 경계를 통해 사용자는 로직 명령과 handler만 제어하면 되고 Win32 메시지 처리 및 Skia 렌더링 수명 주기를 건드리지 않아도 된다.

각 카드는 독립된 `operation_lane`과 로그 버퍼를 갖는다. 같은 카드의 조회 및 변경 작업은 안전 정책에 따라 직렬화하지만 서로 다른 카드는 worker pool에서 병렬 실행할 수 있다. 전체 동시 실행 수는 설정 가능한 상한으로 제한한다. 카드가 삭제되거나 새 refresh가 요청된 뒤 늦게 도착한 결과는 `project_id`와 `generation` 검사로 폐기한다.

각 카드는 최소한 `loading`, `ready`, `running`, `warning`, `failed`, `disabled` 상태를 가진다. UI thread는 전달받은 snapshot만 그리며 애니메이션 시각과 hover 같은 일시적인 렌더 상태 외에는 업무 상태를 소유하지 않는다.

### 3.9 로그, 개인정보, 진단 정보

로그 레코드는 시간, 프로젝트 ID, 카드별 sequence, 작업 종류, 심각도, 메시지를 구조화하여 카드별 ring buffer에 저장한다. 하단 터미널 영역은 `selected_project_id`에 해당하는 카드의 로그만 표시한다. 카드 선택을 변경하면 진행 중인 작업을 중단하지 않고 표시 대상만 즉시 교체한다. 선택 카드가 없으면 안내 empty state를 표시한다.

화면에서는 선택 카드의 작업 상태, 필터, 자동 스크롤 켜기/끄기, 복사, 지우기를 제공한다. stdout과 stderr가 동시에 들어와도 카드별 sequence 순으로 안정적으로 표시한다. 전체 앱 진단 로그가 필요하면 카드 터미널에 섞지 않고 별도의 진단 화면 또는 파일로 분리한다.

- URL에 포함된 사용자 정보, 토큰, 자격 증명 인자는 마스킹한다.
- 카드별 및 전체 메모리 로그의 최대 크기와 오래된 항목 제거 정책을 둔다.
- 파일 로그가 필요하면 보존 기간과 저장 위치를 설정한다.
- 원시 명령 출력의 인코딩을 UTF-8 내부 문자열로 정규화하고 변환 실패를 표시한다.

### 3.10 사용성 및 접근성

- 카드가 많을 때 가상화 또는 보이는 영역 중심 렌더링을 적용한다.
- 검색, 저장소 종류 및 상태 필터, 정렬 기준을 제공한다.
- 색상만으로 성공과 실패를 구분하지 않고 아이콘과 텍스트를 함께 사용한다.
- 키보드 탐색, 포커스 표시, 최소 클릭 영역, 한글 글꼴 폴백을 검증한다.
- 명령 실행 중 대상 카드의 중복 실행을 막고 진행 및 취소 상태를 보여 준다.

## 4. 권장 아키텍처

의존성 방향은 UI에서 운영체제나 Git/SVN 명령 세부 구현을 직접 참조하지 않도록 유지한다.

```text
app
├── domain
│   ├── project
│   ├── repository_snapshot
│   ├── operation
│   └── status enums
├── application
│   ├── project_service
│   ├── repository_refresh_service
│   ├── repository_operation_service
│   ├── switch_validation_service
│   ├── discovery_service
│   └── logic_controller
├── infrastructure
│   ├── json_project_store
│   ├── process_runner
│   ├── git_repository_provider
│   ├── svn_repository_provider
│   ├── file_system
│   ├── task_scheduler
│   └── per_project_log_store
├── presentation
│   ├── app_state
│   ├── card_view_model
│   ├── log_view_model
│   ├── switch_dialog_view_model
│   ├── codicon_registry
│   ├── input_controller
│   └── skia_renderer
└── platform
    └── win32
        ├── window
        ├── graphics_surface
        ├── font_manager
        ├── process
        └── clipboard_and_dialog
```

핵심 인터페이스는 다음 책임으로 나눈다.

- `repository_provider`: 조회, 업데이트, 전환 후보 조회, 전환 요청을 VCS별로 구현한다.
- `switch_validation_service`: Git ref와 SVN URL, 저장소 안전 상태를 검사하고 구조화된 오류를 반환한다.
- `process_runner`: 인자 배열 실행, 출력 스트리밍, 제한 시간, 취소를 담당한다.
- `project_store`: 스키마 검증, 중복 검사, 원자적 저장을 담당한다.
- `discovery_service`: 깊이 1 탐색과 저장소 루트 판정을 담당한다.
- `task_scheduler`: 카드별 operation lane, 저장소별 직렬화, 전체 동시 실행 상한을 담당한다.
- `logic_controller`: 모든 user intent와 worker 결과를 순서대로 처리하고 유일한 mutable app state를 소유한다.
- `per_project_log_store`: 프로젝트 ID별 로그 순서와 ring buffer를 관리한다.
- `codicon_registry`: 상태 및 동작 이름을 고정된 폰트 코드포인트와 접근성 문자열로 변환한다.
- `skia_renderer`: 불변 view snapshot만 읽어 카드, 선택 카드 로그, dialog를 그린다.
- `input_controller`: 불변 layout snapshot으로 hit test하고 키보드 및 포인터 이벤트를 user intent로 변환한다.

UI와 무관한 계층은 Skia 타입을 노출하지 않는다. Git과 SVN의 명령 출력은 provider 내부에서 파싱하고 공통 `repository_snapshot`으로 변환한다.

## 5. 상태 조회와 작업 흐름

### 5.1 시작 및 새로 고침

1. 설정 파일을 읽고 스키마를 검증한다.
2. Git 및 SVN 실행 가능 여부와 버전을 확인한다.
3. 활성 프로젝트마다 저장소 종류와 루트를 판정한다.
4. 로컬 상태를 먼저 표시한다.
5. 정책에 따라 원격 최신 상태를 병렬 조회한다.
6. 결과와 마지막 확인 시각을 카드에 반영한다.

로컬 상태와 원격 상태를 분리하면 네트워크가 느리거나 끊겨도 기본 정보는 즉시 표시할 수 있다.

상단 도구 모음에는 전체 프로젝트용 `refresh` Codicon 버튼을, 각 카드에는 해당 프로젝트용 `refresh` 버튼을 명시적으로 제공한다. 사용자가 버튼을 누르면 input thread가 `refresh_all_requested` 또는 `refresh_project_requested` intent를 만들고 logic thread가 작업을 스케줄링한다. 새로 고침은 로컬 상태를 먼저 갱신한 다음 원격 상태를 확인하며, 진행 중에는 `loading` Codicon과 마지막 성공 시각을 함께 표시한다. 같은 카드에 중복 요청이 들어오면 무제한 병렬 실행하지 않고 현재 작업 뒤 한 번만 다시 실행하도록 병합한다.

### 5.2 갱신 작업

1. 사용자가 카드의 갱신 버튼을 누른다.
2. 변경 작업 잠금을 획득하고 안전성 상태를 다시 검사한다.
3. 확인이 필요한 변경 예상 내용을 보여 준다.
4. `git pull` 또는 `svn update`를 비대화형으로 실행한다.
5. 로그를 실시간 전달하고 취소 요청을 처리한다.
6. 성공 여부와 관계없이 저장소 상태를 다시 조회한다.

### 5.3 전환 작업

1. 사용자가 카드의 switch 버튼을 눌러 switch dialog를 연다.
2. Git 브랜치 또는 허용된 SVN URL 후보를 비동기로 조회한다.
3. 선택 후보가 바뀔 때마다 대상 형식, 존재 여부, 저장소 일치 여부와 작업 트리 위험 상태를 검증한다.
4. 검증 중에는 확인 버튼을 비활성화하고, 실패하면 dialog에 오류 메시지를 표시하며 명령을 만들지 않는다.
5. 검증이 통과하면 선택 대상과 영향을 표시하고 확인 버튼을 활성화한다.
6. 확인 시 logic thread가 동일 조건을 다시 검증한다.
7. 재검증이 실패하면 오류 메시지를 표시하고 실행하지 않는다.
8. 재검증이 통과한 경우에만 `git switch` 또는 `svn switch`를 worker pool에 제출한다.
9. 결과를 선택 카드 로그에 기록하고 성공 여부와 관계없이 해당 카드 상태를 다시 조회한다.

## 6. 명령 파싱 원칙

사람이 읽는 기본 출력은 버전과 로캘에 따라 달라질 수 있으므로 가능한 한 안정적인 기계 판독 형식을 사용한다.

- Git 상태: porcelain v2 및 branch 정보를 이용한다.
- Git 최신 상태: fetch 후 업스트림과의 좌우 커밋 수를 계산한다.
- Git 브랜치: `for-each-ref`의 명시적 format을 사용한다.
- SVN 정보: XML 또는 `--show-item`처럼 계약이 명확한 출력을 사용한다.
- SVN 상태: XML 출력과 원격 조회 옵션을 사용한다.
- 모든 명령은 작업 디렉터리와 인자를 명시하고 사용자 셸 설정에 의존하지 않는다.

지원하는 Git/SVN 최소 버전을 결정하고 해당 버전의 명령 옵션으로 고정한다. 샘플 출력 fixture를 저장하여 파서 회귀 테스트에 사용한다.

## 7. 프로젝트 구조 및 문서화 계획

```text
gitman/
├── CMakeLists.txt
├── cmake/
├── config/
│   └── projects.example.json
├── docs/
│   ├── plan.md
│   ├── requirements.md
│   ├── architecture.md
│   ├── decisions/
│   ├── verification/
│   └── change_log.md
├── src/
│   ├── app/
│   ├── domain/
│   ├── application/
│   ├── infrastructure/
│   ├── presentation/
│   └── platform/
└── tests/
    ├── unit/
    ├── integration/
    ├── fixtures/
    └── manual/
```

문서별 책임은 다음과 같다.

- `requirements.md`: 요구사항 식별자, 수용 기준, 범위 변경을 기록한다.
- `architecture.md`: 모듈 관계, 스레딩 모델, 상태 모델을 기록한다.
- `decisions/ADR-NNN-*.md`: 운영체제, 창 계층, Skia 백엔드, JSON 라이브러리, 프로세스 API 등의 결정을 사유와 함께 기록한다.
- `verification/YYYY-MM-DD.md`: 실행한 자동 및 수동 검증, 환경, 결과, 미해결 문제를 기록한다.
- `change_log.md`: 사용자 지시와 이에 따른 코드 및 문서 변경의 요구사항 ID를 기록한다.

코드 주석은 코드가 무엇을 하는지 반복하기보다 한국어로 제약, 이유, 예외 처리 근거를 설명한다. 외부 도구 명령과 API 이름은 검색 가능성을 위해 원문을 유지한다.

## 8. 구현 단계

### 단계 0: 결정 사항 확정

- Windows 11, Win32 네이티브 창 및 입력 계층, CPU 기준 Skia surface를 확정한다.
- C++ 표준과 지원 컴파일러, CMake 최소 버전을 정한다.
- Skia, JSON, 테스트 프레임워크의 취득 및 버전 고정 방식을 정한다.
- Codicons 버전과 폰트 및 매핑 생성 방식, 배포 라이선스 고지를 정한다.
- Git/SVN 최소 버전과 설치되지 않은 경우의 UX를 정한다.
- `git pull` 정책, 자동 원격 조회 정책, SVN switch 후보 정책을 정한다.

완료 조건: 관련 ADR과 `requirements.md`가 승인 가능한 상태이며 미결정 항목이 명시되어 있다.

### 단계 1: 빌드 및 품질 기준선

- CMakeLists와 CMake Preset으로 Win32 GUI 빌드, 테스트 실행, 정적 분석, 포맷 검사를 구성한다.
- UTF-8, CRLF, 4칸 들여쓰기, `snake_case` 검사를 자동화한다.
- 가장 작은 Win32 Skia 창에서 한글과 Codicon 글리프 렌더링, DPI 배율을 확인한다.

완료 조건: 깨끗한 환경의 명령 한 세트로 빌드와 테스트가 재현되고 파일 규칙 위반이 실패로 보고된다.

### 단계 2: 도메인과 설정 저장소

- 프로젝트, 저장소 스냅샷, 최신 상태, 작업, 오류 모델을 구현한다.
- JSON 스키마 검증, 경로 정규화, 중복 검사, 원자적 저장을 구현한다.
- 정상, 손상, 부분 오류, 마이그레이션 fixture를 작성한다.

완료 조건: UI 없이 설정 파일 읽기 및 저장의 단위 테스트가 통과하고 손상 시 원본을 보존한다.

### 단계 3: 프로세스 실행 계층

- 셸을 거치지 않는 인자 배열 실행을 구현한다.
- 출력 스트리밍, 종료 코드, 제한 시간, 취소, 비밀 마스킹을 구현한다.
- 공백과 비ASCII 문자가 포함된 경로 및 대용량 출력을 시험한다.

완료 조건: 성공, 실패, 시간 초과, 취소, 출력 인코딩, 프로세스 시작 실패 테스트가 통과한다.

### 단계 4: Git 및 SVN provider

- 로컬 상태 조회와 기계 판독 파서를 먼저 구현한다.
- 원격 최신 상태, 갱신, 전환 후보, switch dialog용 유효성 검사, 전환을 순서대로 구현한다.
- 실제 임시 저장소를 사용하는 통합 테스트와 고정 출력 파서 테스트를 작성한다.

완료 조건: Git/SVN의 정상, dirty, conflict, no-upstream, detached, offline, 인증 필요, SVN switched 및 mixed-revision 사례가 공통 상태로 변환된다.

### 단계 5: 탐색과 등록

- 깊이 1 자식 탐색, 저장소 루트 판정, 중복 및 링크 정책을 구현한다.
- 후보 미리보기, 선택 등록, 저장 충돌 감지를 구현한다.

완료 조건: 접근 거부나 느린 경로가 전체 탐색을 중단하지 않고, 선택하지 않은 후보는 파일에 기록되지 않는다.

### 단계 6: GUI와 상태 연결

- Win32 창, CPU 기준 Skia surface, 입력 전달, 한글 및 Codicon 글꼴, Per-Monitor V2 DPI 기반을 구현한다.
- input thread, UI thread, logic thread와 worker pool의 단방향 큐 및 종료 순서를 구현한다.
- 가로형 카드, Codicon 상태 표시, 전체 및 카드별 refresh, 필터와 정렬, 빈 상태 및 오류 상태를 구현한다.
- logic command 경계를 연결하고 카드별 중복 명령을 차단한다.

완료 조건: 100개 이상의 모의 카드에서 UI 및 input thread가 저장소 조회로 정지하지 않고, logic thread만 app state를 변경하며, 크기 변경 및 스크롤이 정상 동작한다.

### 단계 7: 작업 UI와 로그

- 갱신과 switch dialog 유효성 검사, 진행, 취소, 오류 및 결과 표시를 연결한다.
- 카드별 구조화 로그와 선택 카드 전용 하단 터미널 뷰, 필터, 복사, 지우기, 크기 제한을 구현한다.
- 명령 전후 자동 상태 갱신과 오류 복구를 구현한다.

완료 조건: 서로 다른 카드의 병렬 작업 로그가 섞이지 않고 선택 카드의 실행 내용과 결과만 추적할 수 있으며, switch 검증 실패나 인증 프롬프트 때문에 앱이 멈추지 않는다.

### 단계 8: 안정화와 배포

- 장시간 실행, 종료 중 작업, 네트워크 단절, 대량 로그, 파일 변경 충돌을 시험한다.
- 의존성 라이선스, 바이너리 배포, 설정 위치, 로그 위치를 문서화한다.
- 설치 및 제거, 업그레이드, 설정 백업과 복구 절차를 검증한다.

완료 조건: 지원 환경별 검증 기록과 알려진 제한 사항이 `docs/verification/`에 남아 있다.

## 9. 테스트 전략

### 9.1 단위 테스트

- JSON 스키마와 마이그레이션
- 경로 정규화와 중복 판정
- Git/SVN 출력 파서
- 최신 상태 및 카드 상태 전이
- 상태 열거형과 Codicon 및 접근성 이름 매핑
- switch dialog 검증 상태와 명령 생성 차단
- input event, user intent, logic command 변환 및 app state 소유권
- 로그 마스킹과 크기 제한
- 레이아웃 계산과 히트 테스트

### 9.2 통합 테스트

- 임시 Git 저장소와 로컬 bare 원격을 이용한 ahead/behind/diverged 테스트
- 임시 SVN 저장소를 이용한 update/switch/mixed-revision 테스트
- Git의 존재하지 않는 ref 및 SVN의 잘못된 URL과 다른 저장소 UUID 거부 테스트
- 여러 카드 병렬 실행 중 카드별 로그 순서와 선택 로그 격리 테스트
- 프로세스 취소, 제한 시간, 대용량 stdout/stderr 테스트
- JSON 동시 수정과 원자적 교체 실패 테스트
- 공백, 한글, 긴 경로가 포함된 프로젝트 테스트

### 9.3 수동 및 시각 검증

- 100%, 150%, 200% DPI에서 카드와 로그 확인
- 창 최소 크기, 긴 경로, 긴 브랜치명, 오류 메시지 잘림 확인
- 키보드 포커스와 색상 외 상태 구분 확인
- Codicon 폰트 누락 폴백과 모든 상태 아이콘의 한국어 툴팁 확인
- switch dialog의 검증 중, 실패, 성공 상태와 실패 시 미실행 확인
- 카드 선택 중 병렬 로그가 선택 카드에 맞게 전환되는지 확인
- 전체 및 카드별 refresh 버튼의 진행, 중복 클릭, 오류 복구 확인
- 네트워크 단절 및 인증 만료 시 무한 대기 여부 확인
- 실행 중 종료 및 재시작 후 설정 손상 여부 확인

검증 결과에는 날짜, 커밋, 운영체제, Git/SVN 버전, 실행 명령, 통과 여부, 스크린샷 또는 로그 위치를 기록한다.

## 10. 우선 해결할 설계 질문

구현 전에 다음 항목을 확정해야 재작업을 줄일 수 있다.

확정된 결정은 다음과 같다.

- Windows 11 전용 Win32 네이티브 앱으로 개발한다.
- 빌드 시스템은 CMake를 사용한다.
- 상태와 동작 아이콘은 버전을 고정한 VS Code Codicons를 사용한다.
- Git/SVN 전환은 유효성 검사가 포함된 switch dialog에서만 수행한다.
- 하단 터미널은 선택 카드 전용 로그를 표시한다.
- 전체 및 카드별 명시적 refresh 버튼을 제공한다.
- input thread, UI thread, logic thread를 분리하고 app state는 logic thread만 변경한다.

아직 확정해야 할 항목은 다음과 같다.

1. C++ 표준, Visual Studio 및 MSVC 최소 버전, CMake 최소 버전은 무엇인가?
2. 기능 기준선 이후 Direct3D GPU surface를 최초 릴리스에 포함할 것인가?
3. Skia, JSON, 테스트 프레임워크의 의존성 취득 및 버전 고정 방식은 무엇인가?
4. 앱이 원격 `fetch` 또는 SVN 원격 조회를 자동으로 수행해도 되는가? 주기는 얼마인가?
5. `git pull`은 `ff-only`, merge, rebase 중 어떤 정책을 기본으로 할 것인가?
6. SVN switch 대상 URL은 JSON 명시 목록만 허용할 것인가, 서버 레이아웃 탐색도 허용할 것인가?
7. 자격 증명은 기존 Git/SVN credential helper에만 위임할 것인가?
8. 심볼릭 링크, junction, Git worktree, bare repository를 지원 범위에 포함할 것인가?
9. 프로젝트 JSON의 기본 위치와 다중 설정 파일 지원 여부는 무엇인가?
10. 카드별 및 전체 worker 동시 실행 상한과 로그 ring buffer 크기는 얼마인가?

## 11. 주요 위험과 대응

| 위험 | 영향 | 대응 |
| --- | --- | --- |
| 원격 조회 지연 및 인증 프롬프트 | UI 정지 또는 무한 대기 | 비동기 실행, 비대화형 옵션, 제한 시간, 취소 |
| Git/SVN 출력 형식 및 로캘 차이 | 잘못된 상태 표시 | 기계 판독 형식, 최소 버전 고정, fixture 테스트 |
| dirty 작업 트리에서 변경 명령 | 사용자 변경 손상 | 사전 검사, 기본 차단, 강제 작업 제외 |
| 설정 저장 중 중단 | JSON 손상 | 원자적 교체, 백업, 동시 수정 감지 |
| 많은 저장소와 로그 | 성능 및 메모리 저하 | 제한된 병렬성, 카드 가상화, 로그 상한 |
| Skia 및 플랫폼 결합 | 이식성과 테스트성 저하 | platform 추상화, UI 독립 도메인, CPU 기준선 |
| 스레드 간 상태 공유 또는 메시지 역전 | 경쟁 조건과 UI 불일치 | logic thread 단일 소유권, 단방향 큐, generation 검사 |
| Codicon 폰트 또는 매핑 불일치 | 잘못된 아이콘 표시 | 동일 버전 자산 고정, 생성 헤더 검증, 폴백 텍스트 |
| 병렬 카드 로그 혼합 | 잘못된 작업 진단 | project ID와 sequence, 카드별 ring buffer, 선택 필터 |
| SVN 레이아웃 오판 | 잘못된 URL 전환 | 허용 목록, 사전 확인, 레이아웃 자동 가정 금지 |
| 경로와 로그의 비밀 노출 | 보안 사고 | 인자 배열 실행, 민감 정보 마스킹, 진단 로그 검토 |

## 12. 최초 마일스톤 완료 정의

최초 사용 가능한 버전은 다음 조건을 모두 만족해야 한다.

- JSON에서 Git/SVN 프로젝트를 읽고 항목별 오류를 표시한다.
- 가로형 카드에서 로컬 상태와 마지막 원격 확인 상태를 구분하여 보여 준다.
- 상태와 동작을 Codicon, 색상, 한국어 툴팁으로 일관되게 보여 준다.
- 전체 및 카드별 refresh 버튼으로 명시적으로 상태를 갱신할 수 있다.
- 깊이 1의 자식 저장소를 미리 보고 선택 등록할 수 있다.
- 안전성 검사와 switch dialog 검증을 거친 update/pull 및 switch를 비대화형으로 실행할 수 있다.
- 선택 카드 명령의 출력과 결과만 하단 읽기 전용 로그에서 확인할 수 있다.
- 서로 다른 카드 작업은 제한된 범위에서 병렬 실행할 수 있다.
- input, UI, logic 스레드가 분리되고 app state는 logic thread만 변경한다.
- UI와 input thread는 파일, 네트워크, 외부 프로세스 작업으로 멈추지 않는다.
- 한글, 공백 경로, 고해상도 배율, 오프라인, 취소, 오류 복구를 검증한다.
- 코드 스타일과 UTF-8/CRLF 규칙이 자동 검사된다.
- 요구사항, ADR, 검증 결과, 알려진 제한 사항이 `docs/`에 남아 있다.
