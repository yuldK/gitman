# 단계 5 구현 계획 - 탐색과 등록

## 1. 문서 상태

- 작성일: 2026-08-17
- 개정일: 2026-08-17 (계획 승인 반영: 단계 5부터 production code와 test code를 한 검수 구간으로 통합)
- 대상: 구현 단계 5
- 현재 상태: `S5-P0` 승인 완료, `S5-D1` 진행
- 현재 검수 게이트: `S5-D1` 구현 및 test 검수
- 관련 요구사항: REQ-001, REQ-004, REQ-010~REQ-013, REQ-016, NFR-005~NFR-006, NFR-009
- 상위 문서: `docs/plan.md` 3.6, 3.7, 8장 단계 5, `docs/requirements.md` 6장
- 선행 조건: 단계 4 최종 사용자 승인 (2026-08-17 사용자가 `S5-P0` 진행을 지시함)

사용자는 단계 2~4와 같은 진행 방식을 요구했다. 계획, 각 작업 구간과 각 테스트 구간이 끝날 때마다 무엇을 했고 다음에 무엇을 어떻게 처리할지 보고한 뒤 검수를 기다린다. 사용자는 각 검수 후 직접 커밋을 진행한다.

## 2. 목표

단계 5는 등록 경로의 **깊이 1 자식 디렉터리를 조사해 저장소 후보를 만들고, 사용자가 고른 항목만 문서에 추가**하는 탐색·등록 계층이다. REQ-004의 수용 기준(후보 미리보기와 선택 등록)을 데이터 계층에서 완성하고, 미리보기 dialog UI 자체는 단계 6~7이 담당한다.

- 지정한 스캔 루트의 바로 아래 자식 디렉터리만 열거한다. 깊이 2 이상은 내려가지 않는다.
- 각 자식에서 저장소 종류(Git/SVN)와 저장소 루트 여부를 표식 기반으로 판정한다.
- junction, 심볼릭 링크, Git worktree, bare repository, submodule의 세부 지원 범위를 확정한다. `docs/requirements.md` 6장이 단계 5로 이관한 항목이다.
- 접근 거부, 사라진 경로, 느린 경로가 전체 탐색을 중단시키지 않게 한다. 실패한 자식은 그 항목만 사유와 함께 표시한다.
- 이미 등록된 프로젝트와 중복인 후보를 정규화 경로 비교로 식별해 표시한다.
- 발견 즉시 저장하지 않는다. 후보 목록, 판정 종류, 제외 사유를 먼저 반환하고 사용자가 선택한 항목만 등록한다.
- 선택 항목을 `project_definition`으로 변환해 문서에 추가하고 단계 2의 원자적 저장으로 기록한다.
- 저장 시 단계 2의 revision token으로 다른 프로세스의 동시 수정을 감지하고, 충돌하면 저장하지 않고 구조화된 오류로 보고한다.
- 취소 토큰을 받아 자식 경계에서 협조적으로 중단할 수 있게 한다.
- 예외를 public API 밖으로 던지지 않고 구조화 진단으로 반환한다.

## 3. 단계 5에서 하지 않는 일

- 탐색 미리보기 dialog와 등록 UI (단계 6~7)
- 탐색과 등록의 스레드 배치, `task_scheduler`, 진행률 표시 (단계 6~7)
- ADR-004의 범용 message queue, dispatcher와 thread bridge (단계 6 별도 설계 승인 후)
- `.verison-list` Windows file association 등록 (단계 8)
- 깊이 2 이상의 재귀 탐색 (`docs/plan.md` 2.2의 제외 범위)
- 발견 후보의 자동 등록. 선택하지 않은 후보는 어떤 경로로도 파일에 기록되지 않는다.
- 탐색 중 Git/SVN 프로세스 실행. 판정은 표식 파일 기반이며 프로세스를 하나도 만들지 않는다 (4.2).
- 등록 직후의 저장소 상태 조회. 등록 후 카드 조회는 단계 4 provider와 단계 6~7 UI의 몫이다.

단계 2~4와 같이 탐색·등록 API는 **호출한 스레드에서 완료까지 블로킹하는 동기 API**로 만든다. 스레드 배치는 단계 6~7이 결정하며 ADR-004의 구현 차단 조건을 건드리지 않는다.

## 4. 사용자 검수에서 확정할 설계 제안

### 4.1 계층 경계와 계약

- `domain`은 탐색 후보, 제외 사유, 탐색 결과 같은 값 type만 정의하고 Win32와 JSON 라이브러리를 참조하지 않는다.
- `application`은 깊이 1 열거 계약(`directory_enumerator`)과 탐색·등록 서비스를 담는다. 표식 파일 확인은 단계 4의 `vcs_file_probe` 계약을 재사용한다.
- `platform/win32`은 `directory_enumerator` 구현만 추가한다. 열거 항목의 reparse point 여부는 Win32 열거가 함께 돌려주는 파일 속성에서 얻는다.
- 경로 정규화와 중복 비교는 단계 2의 `project_path_resolver` 계약을 그대로 주입받아 사용한다. 새 정규화 규칙을 만들지 않는다.
- 등록 저장은 단계 2의 `project_store` 계약을 주입받아 사용한다. 탐색·등록 계층은 JSON을 직접 다루지 않는다.
- 따라서 탐색 판정과 등록 로직 전체를 실제 디스크 없이 fake enumerator, fake probe, fake resolver로 결정적으로 검증할 수 있다. 단계 4의 fake runner 선례와 같다.

의존성 방향은 다음으로 제한한다.

```text
domain <- application service <- (주입) directory_enumerator / vcs_file_probe / project_path_resolver / project_store
```

### 4.2 저장소 판정 규칙: 표식 기반, 프로세스 미사용

탐색은 **프로세스를 하나도 만들지 않는다**. 자식이 수십 개인 디렉터리에서 자식마다 `git.exe`를 띄우면 명시적 미리보기가 수 초 단위로 느려지고, 등록 후 정확한 상태 판정은 어차피 단계 4 provider가 수행한다. 탐색의 오판(예: 손상된 저장소)은 등록 후 카드의 `not_a_repository` 상태로 드러나므로 안전하다.

자식 디렉터리 하나의 판정 순서는 다음과 같다. 표식 확인은 `vcs_file_probe`를 사용한다.

| 순서 | 표식 | 판정 |
| --- | --- | --- |
| 1 | `.git` 디렉터리와 `.svn` 디렉터리가 동시에 존재 | 비정상. `conflicting_metadata`로 제외하고 warning 진단을 남긴다. |
| 2 | `.git` 디렉터리 | Git 저장소 루트 후보 |
| 3 | `.git` 파일 | Git 후보. linked worktree 또는 submodule이며 `via_git_file` 표시를 남긴다 (4.4). |
| 4 | `.svn` 디렉터리 | SVN 작업 복사본 루트 후보. 현대 SVN 형식은 루트에만 `.svn`을 두므로 깊이 1 판정과 맞는다. |
| 5 | `.git`도 `.svn`도 없고 `HEAD` 파일, `objects` 디렉터리, `refs` 디렉터리가 모두 존재 | bare 저장소로 보고 `bare_repository`로 제외한다 (4.4). |
| 6 | 어느 표식도 없음 | `not_a_repository`로 제외한다. 목록에는 남겨 사용자가 "왜 안 잡혔는지"를 볼 수 있게 한다. |

스캔 루트 자체에도 같은 판정을 적용해 `root_is_repository` 정보로 반환한다. 사용자가 저장소 내부를 스캔 루트로 지정한 실수를 UI가 알릴 수 있게 하기 위한 정보 값이며, 루트는 후보 목록에 넣지 않는다.

`.git` 파일의 `gitdir:` 내용은 읽지 않는다. worktree와 submodule을 구분하는 표시가 필요해지면 단계 6~7의 UI 결정에 따라 그때 추가한다. 이 결정으로 파일 내용 읽기 계약이 필요 없어진다.

후보 목록은 디렉터리 이름의 대소문자 무시 오름차순으로 정렬해 반환한다. filesystem 열거 순서에 의존하지 않아 결과가 결정적이다.

### 4.3 링크 정책

reparse point(심볼릭 링크, junction, 기타 모든 reparse 종류)인 자식 디렉터리는 **판정하지 않고 `reparse_point` 사유로 제외하되 목록에는 표시한다.**

| 근거 | 설명 |
| --- | --- |
| 이중 등록 | 링크와 실제 경로가 같은 저장소를 가리키면 정규화 경로가 달라 중복 검사로 걸러지지 않고, 같은 작업 복사본에 대한 카드 두 개가 생겨 변경 명령 직렬화가 깨진다. |
| cycle 및 대상 불명 | 링크 대상이 스캔 루트 상위나 네트워크 경로일 수 있다. 자동 등록 후보에 올릴 만큼 안전하다고 판정하려면 대상 해석 비용이 판정 이득보다 크다. |
| 수동 등록은 열려 있음 | 이 정책은 탐색이 자동 후보로 올리지 않는다는 것뿐이다. 사용자가 원하면 그 경로를 문서에 직접 적어 등록할 수 있고, 단계 2의 경로 검증은 링크를 막지 않는다. |

### 4.4 worktree, bare, submodule 지원 범위 확정

`docs/requirements.md` 6장이 단계 5로 이관한 세부 범위를 다음과 같이 확정할 것을 제안한다.

| 배치 | 탐색 | 등록 | 근거 |
| --- | --- | --- | --- |
| 일반 작업 복사본 (`.git` 디렉터리, `.svn` 디렉터리) | 후보 | 허용 | 기본 대상 |
| Git linked worktree (`.git` 파일) | 후보 | 허용 | 단계 4 조회가 이미 추가 처리 없이 지원한다 (`S4-D2-CODE` 확정). |
| Git submodule (`.git` 파일) | 후보 | 허용 | 표식만으로는 worktree와 구분되지 않고(둘 다 `.git` 파일), 독립 카드로 조회·update·switch가 모두 동작한다. `via_git_file` 표시만 남긴다. |
| bare 저장소 | 목록에 표시 | 제외 | 카드 작업(작업 트리, update, switch)이 작업 트리를 전제한다. 단계 4가 `repository_availability::unsupported_layout`으로 보고하는 배치를 탐색이 후보로 올리면 등록 직후 쓸 수 없는 카드가 생긴다. |
| reparse point | 목록에 표시 | 제외 | 4.3의 링크 정책 |

### 4.5 중복 판정

- 후보의 정규화 경로는 주입받은 `project_path_resolver::resolve`로 만든다. 문서 경로 기준과 비교 규칙이 단계 2의 기존 항목과 완전히 같아진다.
- 문서에 이미 있는 프로젝트의 정규화 경로와 `normalized_equal`이면 `already_registered` 사유로 제외하고 목록에 표시한다. REQ-004의 "중복 없이 추가"를 등록 시점이 아니라 미리보기 시점에 보이게 하는 것이다.
- 비활성(`enabled: false`) 프로젝트와의 중복도 같은 사유로 제외한다. 문서에 있는 항목은 상태와 무관하게 이미 등록된 것이다.
- 같은 스캔 안에서 자식 이름은 filesystem이 유일성을 보장하므로 스캔 내 중복 검사는 두지 않는다.

### 4.6 견고성과 취소

| 상황 | 처리 |
| --- | --- |
| 스캔 루트가 상대 경로, 존재하지 않음, 디렉터리 아님, 접근 불가 | 탐색을 시작하지 않고 후보 없는 결과에 구조화 오류 진단을 담는다. |
| 자식 하나의 표식 확인 실패 | 그 후보만 `inaccessible` 사유로 표시하고 다음 자식으로 계속한다. 전체 탐색을 중단하지 않는다. |
| 느린 경로 및 네트워크 드라이브 | 동기 API이므로 개별 Win32 호출 하나를 끊을 수는 없다. 자식 경계마다 취소 토큰을 확인해 다음 자식으로 넘어가지 않는 것까지를 보장하고, UI가 멈추지 않게 하는 스레드 배치는 단계 6~7이 담당한다. 시간 예산이나 자식 수 상한은 두지 않는다. |
| 취소 | 단계 3의 `process_cancellation_token`을 그대로 받는다. 이름은 process지만 이미 단계 4 provider 계약이 협조적 취소 신호로 쓰고 있어 같은 type을 재사용한다. 취소된 탐색은 `completed`를 끄고 그때까지의 후보를 담아 반환한다. |

접근 거부 경로의 실측에는 한계가 있다. 단계 2에서 확인했듯 로컬 NTFS는 deny ACE로도 속성 조회를 실패시킬 수 없으므로, `inaccessible` 분기는 fake enumerator/probe 단위 test로 검증하고 그 사실을 검증 기록에 남긴다.

### 4.7 선택 등록

등록 서비스는 사용자가 고른 후보 목록과 로드된 문서, revision token을 받아 다음을 수행한다.

1. 각 후보가 선택 가능한지 다시 검증한다. 제외 사유가 있는 후보, 문서와 중복인 후보가 섞여 들어오면 그 항목을 오류로 보고하고 **아무것도 저장하지 않는다.** 부분 등록은 하지 않는다. 사용자가 고른 목록 전체가 원자적으로 들어가거나 전체가 거부되는 편이 결과를 예측하기 쉽다.
2. 후보를 `project_definition`으로 변환한다 (아래 표).
3. 문서의 `projects` 끝에 추가하고 `project_store::save`에 revision token과 함께 넘긴다.
4. 저장 결과의 진단을 그대로 승격해 반환한다.

| 필드 | 값 | 근거 |
| --- | --- | --- |
| `id` | 디렉터리 이름을 그대로 쓰되, 문서 안에서 중복이면 `-2`, `-3` … 숫자 접미사로 유일하게 만든다 | 사람이 읽을 수 있고 재현 가능하며 난수 dependency가 없다. 한글 등 비ASCII 이름도 그대로 허용한다. id는 문서 내 유일 문자열이면 충분하다. |
| `path` | 자식의 절대 경로 | 스캔 루트가 절대 경로이므로 자연스럽고, 문서를 옮겨도 뜻이 변하지 않는다. 상대 경로 기록을 원하면 사용자가 문서를 직접 수정할 수 있다. |
| `display_name` | 디렉터리 이름 | 카드 기본 표시 |
| `vcs_hint` | 판정한 종류 (`git` 또는 `subversion`) | 탐색이 이미 종류를 확정했으므로 `automatic`으로 되돌려 provider가 다시 추측하게 할 이유가 없다. |
| `enabled` | `true` | 등록 즉시 카드로 쓰는 것이 사용자 의도다. |
| `preferred_remote` | 없음 | 기본값 |
| `svn_switch_targets` | 빈 목록 | SVN 허용 목록은 사용자가 직접 채우는 값이다 (ADR-003). |

### 4.8 저장 충돌 감지

- 단계 2의 `project_store`가 이미 revision token으로 원본 변경을 감지하고 원자적 교체를 수행한다. 단계 5는 새 감지 장치를 만들지 않고 **load에서 받은 token을 save까지 그대로 전달하는 경로**를 만들고 test로 고정한다.
- 다른 프로세스가 문서를 바꾼 뒤의 save는 저장하지 않고 충돌 진단을 반환해야 한다. 등록 서비스는 병합을 시도하지 않는다. 재탐색과 재시도는 단계 6~7 UI의 결정이다.
- 충돌로 거부된 경우 원본 문서와 backup이 훼손되지 않아야 한다.

### 4.9 오류 분류와 진단

- 탐색·등록 서비스는 예외를 던지지 않고 결과 값과 `diagnostic` 목록을 반환한다. 단계 2~4와 같은 계약이다.
- `diagnostic_code`에 탐색 루트 사용 불가, 자식 접근 불가, 탐색 취소, 등록 후보 거부 코드를 추가한다. 정확한 이름과 개수는 `S5-D1-CODE`에서 확정한다.
- 저장 충돌과 저장 실패는 단계 2 store의 기존 진단 코드를 그대로 승격한다.
- `directory_enumerator`는 실패 시 Win32 오류 번호를 함께 돌려주고, 서비스가 이를 진단의 상세로 담는다. 단계 2의 `project_path_resolution::native_error` 선례와 같다.

## 5. 코드 구조 제안

```text
src
├── domain
│   └── discovery.h/.cpp                 (탐색 후보, 제외 사유, 탐색 결과, 등록 요청·결과 값 type)
├── application
│   ├── directory_enumerator.h           (깊이 1 자식 열거 계약: 이름, 디렉터리 여부, reparse 여부, 오류)
│   ├── discovery_service.h/.cpp         (열거와 표식 판정, 중복 표시, 취소, 진단)
│   └── project_registration_service.h/.cpp  (선택 검증, id 생성, 문서 갱신과 저장)
└── platform/win32
    └── win32_directory_enumerator.h/.cpp
```

```text
tests
├── discovery_domain_tests.cpp
├── directory_enumerator_tests.cpp       (실제 임시 디렉터리의 Win32 열거 계약)
├── discovery_service_tests.cpp
├── project_registration_tests.cpp
├── discovery_integration_tests.cpp      (실제 임시 디렉터리, 실제 git.exe, junction)
└── helpers/discovery_test_doubles.h/.cpp (fake directory_enumerator)
```

CMake target 구성 제안:

- 새 static library `gitman_discovery`를 만들고 `domain/discovery.*`, `application/discovery_service.*`, `application/project_registration_service.*`, `platform/win32/win32_directory_enumerator.*`를 넣는다.
- `gitman_domain`과 `gitman_workspace`(store·resolver 계약), `gitman_vcs`(`vcs_file_probe` 계약과 `process_cancellation_token`)를 PUBLIC으로 링크한다. 새 외부 dependency는 없다.
- `win32_directory_enumerator`를 `gitman_win32_platform`이 아니라 `gitman_discovery`에 두는 것은 단계 3 `win32_process_runner`, 단계 4 `win32_vcs_file_probe`와 같은 계층 방향 유지 선택이다.
- `gitman_tests`는 `gitman_discovery`를 링크한다. 실행 파일 `gitman`은 이번 단계에서도 링크하지 않으며 실제 조립은 단계 6이다.

## 6. public API 초안

검수 편의를 위한 초안이며 세부 이름은 각 `CODE` 체크포인트에서 확정한다.

```cpp
enum class discovery_exclusion
{
    none,
    not_a_repository,
    bare_repository,
    conflicting_metadata,
    reparse_point,
    already_registered,
    inaccessible,
};

struct discovery_candidate
{
    std::u8string directory_name {};
    std::u8string absolute_path {};
    std::u8string normalized_path {};
    repository_kind kind { repository_kind::unknown };
    bool via_git_file { false };
    discovery_exclusion exclusion { discovery_exclusion::none };

    [[nodiscard]] bool selectable() const noexcept;   // exclusion == none
};

struct discovery_result
{
    bool completed { false };            // 루트 실패와 취소가 아니면 true
    bool root_is_repository { false };
    std::vector<discovery_candidate> candidates {};
    std::vector<diagnostic> diagnostics {};
};

class discovery_service
{
public:
    [[nodiscard]] discovery_result discover_children(
        std::u8string_view scan_root,
        const workspace_document& document,
        const process_cancellation_token& token) noexcept;
};

struct project_registration_result
{
    bool succeeded { false };
    std::optional<workspace_document> saved_document {};
    std::optional<workspace_revision_token> revision {};
    std::vector<diagnostic> diagnostics {};
};

class project_registration_service
{
public:
    [[nodiscard]] project_registration_result register_candidates(
        const workspace_document& document,
        const workspace_revision_token& expected_revision,
        std::span<const discovery_candidate> selected) noexcept;
};
```

두 서비스는 `directory_enumerator`, `vcs_file_probe`, `project_path_resolver`, `project_store`를 생성자 주입으로 받는다.

## 7. 세부 작업과 검수 게이트

2026-08-17 사용자 지시로 단계 5부터는 **production code와 test code 작성을 한 검수 구간에서 함께 수행한다.** 단계 2~4의 `CODE` / `TEST` 분리는 적용하지 않는다. 개발 중 test가 드러낸 결함은 같은 구간에서 바로 수정하고 보고에 기록하며, 검수에서 발견된 결함의 수정은 해당 구간의 재제출로 처리하므로 별도 `FIX` 체크포인트도 두지 않는다.

| 순서 | 체크포인트 | 이번 구간에서 하는 일 | 구간 종료 조건 |
| --- | --- | --- | --- |
| 1 | `S5-P0` | 본 구현 계획과 설계 제안 작성 | 사용자 계획 승인 전 중지 |
| 2 | `S5-D1` | 도메인 후보·결과 모델, `directory_enumerator` 계약과 Win32 구현, 표식 판정 규칙 순수 함수, `gitman_discovery` target. 판정 matrix, 열거 계약, 정렬과 도메인 값 test, fake enumerator 도우미 | build/test/style 결과와 diff 제시 후 중지 |
| 3 | `S5-D2` | `discovery_service` 탐색 실행: 루트 검증, 열거, 판정 적용, 중복 표시, 취소, 진단. fake 기반 탐색 시나리오와 실제 임시 디렉터리·실제 `git.exe`·junction 통합 test | build/test/style 결과와 diff 제시 후 중지 |
| 4 | `S5-D3` | `project_registration_service`: 선택 재검증, id 생성, 문서 갱신, revision 전달과 충돌 보고. 변환 규칙, 전체 거부, 미선택 무기록, round-trip, 동시 수정 충돌 test | build/test/style 결과와 diff 제시 후 중지 |
| 5 | `S5-V1` | 전체 build/test/analyze/install 검증과 단계 5 검증 문서 작성 | 최종 결과 제시 후 단계 5 승인 대기 |

각 체크포인트가 끝나면 반드시 `docs/handoff.md`에 마지막 체크포인트, 승인 대기 항목, 변경 파일과 검증 결과, 미수정 결함, 승인 후 허용되는 다음 작업 하나를 갱신한다. 사용자가 명시적으로 승인하기 전에는 다음 행으로 진행하지 않는다.

`S5-V1` 종료 보고에는 `docs/handoff.md` 5장의 게이트에 따라 **ADR-004 범용 메시지 구조의 사전 설계 검수가 단계 6 시작 전에 필요하다는 사실**을 다시 알린다. 설계 자료 작성 자체는 단계 5 범위가 아니며 사용자 지시에 따라 별도 구간으로 진행한다.

## 8. 테스트 계획

### 8.1 두 층 전략

| 층 | 대상 | 방식 | 결정성 |
| --- | --- | --- | --- |
| 단위 | 표식 판정, 정렬, 중복 표시, 취소, id 생성, 문서 변환, 충돌 보고 | fake enumerator·probe·resolver·store 주입 | 완전 결정적 |
| 통합 | 실제 filesystem 열거, 실제 저장소 표식, junction, 실제 저장·충돌 | 임시 디렉터리에 실제 배치를 만들고 실제 store를 쓴다 | Git 설치에 의존 (fixture 생성) |

판정이 표식 기반이므로 **SVN 경로는 `svn.exe` 없이도 통합 검증이 가능하다.** `.svn` 디렉터리를 직접 만들면 실제 열거와 판정 경로 전체가 실행된다. 실제 SVN 작업 복사본과의 대조는 단계 8의 실 환경 검증 항목에 남긴다.

### 8.2 단위 test 항목

- 표식 판정 matrix: `.git` 디렉터리, `.git` 파일, `.svn`, 동시 존재, bare 3표식의 전체 부분집합(하나라도 빠지면 비저장소), 아무 표식 없음
- reparse point 자식의 판정 생략과 사유 표시
- 스캔 루트 자체의 저장소 판정(`root_is_repository`)과 루트 미존재·상대 경로·디렉터리 아님·접근 불가
- 자식 하나의 probe 실패가 다른 자식 판정을 막지 않는 것
- 대소문자 무시 이름 정렬과 열거 순서 비의존
- 이미 등록된 프로젝트(활성·비활성)와의 중복 표시, 정규화 비교 사용
- 자식 경계 취소: 중단 지점 이후 probe 호출이 없고 `completed`가 꺼진 결과
- id 생성: 그대로 사용, 문서 내 중복 시 접미사, 접미사끼리의 중복, 한글·공백 이름
- 변환 규칙: 절대 경로, `display_name`, 판정 종류의 `vcs_hint` 기록, `enabled`, 빈 `svn_switch_targets`
- 전체 거부: 선택 목록에 제외 후보나 중복 후보가 섞이면 저장 호출이 없음
- revision 전달: load token이 save까지 그대로 가는 것, 충돌 진단 승격
- 미선택 무기록: 등록 후 문서에 선택 항목만 추가되고 다른 필드(`settings`, unknown key)가 보존되는 round-trip

### 8.3 통합 test 항목

- 실제 임시 디렉터리에 실제 `git.exe` 저장소(단계 4의 `git_repository_fixture` 재사용), `.git` 파일을 쓰는 linked worktree, `git init --bare` 저장소, 수동 `.svn` 표식, 일반 디렉터리를 섞어 놓고 전체 판정
- PowerShell `New-Item -ItemType Junction`으로 만든 junction 자식의 제외 (symlink는 관리자 권한이 필요할 수 있어 junction으로 검증)
- 한글, 공백, emoji가 포함된 자식 이름
- 실제 store로 등록 → 문서 다시 load → 항목 확인 round-trip
- 등록 사이에 문서를 외부에서 수정한 뒤 save가 충돌로 거부되고 원본이 보존되는 것
- 자식 수백 개 디렉터리의 탐색 완주

### 8.4 검증 한계

- 로컬 NTFS에서 deny ACE로 속성 조회를 실패시킬 수 없어(단계 2 실측) `inaccessible` 분기는 fake 기반 단위 test로만 검증하고 이 사실을 검증 기록에 남긴다.
- 네트워크 드라이브와 실제 느린 경로는 이 호스트에서 재현 수단이 없어 검증하지 않는다. 취소 경계 동작으로 대신 보장하고 단계 8 실 환경 검증 항목에 남긴다.

### 8.5 stress 및 회귀

- `S5-V1`에서 전체 suite를 `--repeat until-fail:3`으로 반복해 flakiness를 확인한다.
- 임시 디렉터리는 test 종료 시 RAII로 정리하고 junction도 함께 지운다.
- 기존 393개 test의 회귀를 각 구간에서 확인한다.

## 9. 단계 5 완료 조건

- 모든 체크포인트가 개별 사용자 검수를 통과한다.
- 깊이 1 자식만 조사하고 깊이 2 이상으로 내려가지 않는다.
- Git 저장소(.git 디렉터리와 .git 파일), SVN 작업 복사본, bare 저장소, 비저장소가 표식 기반으로 구분된다.
- 접근 거부나 느린 경로가 전체 탐색을 중단시키지 않고 해당 항목만 사유와 함께 표시된다.
- reparse point 자식이 후보로 올라가지 않고 사유와 함께 표시된다.
- 이미 등록된 경로와 중복인 후보가 미리보기에서 식별된다.
- 취소 토큰이 자식 경계에서 동작한다.
- 선택하지 않은 후보는 어떤 경로로도 파일에 기록되지 않는다.
- 선택 항목이 유일한 id로 문서에 추가되고 `settings`와 unknown field가 보존된다.
- 동시 수정 충돌 시 저장하지 않고 구조화된 오류를 반환하며 원본과 backup이 보존된다.
- 탐색과 등록이 프로세스를 만들지 않는 것이 test로 고정된다.
- 한글, 공백, emoji가 포함된 자식 이름에서 탐색과 등록이 동작한다.
- VS2022 Debug/Release, VS2022 `/analyze`, VS2026 Debug, aggregate format/style과 전체 CTest가 통과한다.
- 단일 exe install 결과가 유지된다.
- `docs/verification/`의 단계 5 기록, `docs/change_log.md`와 `docs/handoff.md`가 최종 상태를 기록한다.

## 10. 계획 검수 항목

### 10.0 2026-08-17 검수 결과

- 사용자가 `S5-P0`을 승인했다. 10.1의 확정 필요 사항 8개와 10.2의 진행 사항 5개는 별도 이견 없이 제안대로 확정됐다.
- 사용자 지시로 단계 5부터 production code와 test code 작성을 한 검수 구간으로 통합했다. 7장의 체크포인트 표를 `S5-P0`, `S5-D1`~`S5-D3`, `S5-V1`의 5개로 개정했다.

### 10.1 사용자 확정이 필요한 사항

1. 탐색 중 프로세스 미사용과 표식 기반 판정 (4.2). 판정 정확도보다 미리보기 반응성과 단순성을 우선한 선택이다.
2. reparse point 자식의 기본 제외 정책 (4.3). 반대로 "판정은 하되 표시만 남기고 선택 가능하게" 바꿀 수도 있다.
3. bare 저장소의 등록 제외 (4.4). 단계 4의 `unsupported_layout` 보고와 일관된다.
4. submodule을 worktree와 구분하지 않고 `via_git_file` 표시로만 남기는 것 (4.4). 구분하려면 `.git` 파일 내용 읽기 계약이 추가로 필요하다.
5. 등록 항목의 `vcs_hint`에 판정 종류를 기록하는 것 (4.7). `automatic`으로 두는 대안이 있다.
6. 등록 경로를 절대 경로로 기록하는 것 (4.7).
7. id 생성 규칙: 디렉터리 이름 + 중복 시 숫자 접미사 (4.7).
8. 선택 목록에 부적격 후보가 섞이면 부분 등록 없이 전체를 거부하는 것 (4.7).

### 10.2 별도 이견이 없으면 계획대로 진행하는 사항

1. `CODE` / `TEST` / `FIX` 3분할과 `S5-V1`로 구성한 11개 체크포인트 순서.
2. 새 `gitman_discovery` target 구성과 파일 배치 (5장).
3. 취소 토큰으로 단계 3의 `process_cancellation_token` type 재사용 (4.6).
4. 후보 정렬 기준: 이름 대소문자 무시 오름차순 (4.2).
5. 스캔 루트 자체의 저장소 여부를 정보 값으로만 반환하는 것 (4.2).

## 11. 미결정 항목

- 미리보기 dialog의 표시 구성, 선택 UX, 재탐색과 진행 표시는 단계 6~7에서 정한다. 단계 5는 후보 데이터와 사유만 제공한다.
- 탐색·등록의 스레드 배치와 취소 UI는 단계 6~7의 `task_scheduler` 및 ADR-004 설계에 따른다.
- 저장 충돌 후의 재시도·재탐색 흐름은 단계 6~7 UI의 결정이다.
- 실제 SVN 작업 복사본, 네트워크 드라이브와 실 환경 접근 거부 경로의 검증은 단계 8이다.
- `.git` 파일 내용(gitdir)을 읽어 worktree와 submodule을 구분하는 표시는 단계 6~7의 UI 요구가 생길 때 추가한다.
