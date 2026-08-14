# 단계 2 구현 계획 - 도메인과 설정 저장소

## 1. 문서 상태

- 작성일: 2026-08-14
- 대상: 구현 단계 2
- 현재 상태: `S2-D1-CODE` 구현 및 기존 회귀 검증 완료, 사용자 코드 검수 대기
- 현재 검수 게이트: `S2-D1-CODE`
- 관련 요구사항: REQ-001, REQ-002, REQ-004, REQ-009~REQ-013, REQ-016, NFR-005~NFR-006, NFR-009

사용자는 `S2-P0` 계획을 승인하면서 설정 파일을 solution 및 `.code-workspace`와 같은 작업공간 문서로 변경했다. 확장자는 사용자 지시의 철자를 그대로 적용한 `.verison-list`다. 이후 작업은 7장의 체크포인트 순서를 따른다.

## 2. 목표

단계 2에서는 UI 및 Git/SVN command와 무관한 도메인 모델과 JSON 프로젝트 저장소를 구현한다.

- `std::u8string` 기반 프로젝트, 저장소 snapshot, 상태, 작업과 구조화 오류 모델을 정의한다.
- schema version 1의 프로젝트 JSON을 읽고 항목별 오류를 반환한다.
- 원문 경로를 보존하면서 비교용 Windows 정규화 경로를 만든다.
- 중복 ID와 중복 경로를 판정한다.
- 일부 프로젝트 항목이 잘못되어도 유효한 항목은 반환한다.
- 동시 수정을 감지하고 원본을 보존하는 원자적 저장, backup과 명시적 복구 경로를 제공한다.
- 후속 단계가 Skia, Win32 UI 또는 nlohmann/json type에 의존하지 않고 도메인 값을 사용할 수 있게 한다.

## 3. 단계 2에서 하지 않는 일

- Git/SVN 실행 파일 탐색, command 실행과 출력 파싱
- 저장소 종류 자동 판별과 깊이 1 자식 디렉터리 탐색
- 프로젝트 ID 자동 생성 UI와 프로젝트 등록 UI
- 카드, refresh, update, switch dialog와 로그 UI
- input/UI/logic thread, message queue, dispatcher와 worker pool
- symlink, junction, Git worktree와 bare repository의 실체 동일성 판정

Git/SVN provider는 단계 4, 탐색과 등록은 단계 5, 메시지 구조와 GUI 연결은 별도 사전 설계 검수를 거친 단계 6에서 진행한다.

## 4. 사용자 검수 시 확정할 설계 제안

### 4.1 작업공간 문서와 연결 프로그램

- 고정된 전역 `projects.json`은 사용하지 않는다.
- 프로젝트 목록은 JSON 내용 형식을 사용하는 `.verison-list` 작업공간 문서다.
- 사용자는 서로 다른 위치에 여러 문서를 둘 수 있으며, 한 프로세스 및 창은 한 번에 하나의 활성 문서를 연다.
- 연결 프로그램으로 실행하면 Windows shell이 넘긴 문서 경로를 첫 positional argument로 받아 연다.
- 문서 경로가 없는 시작 흐름과 열기 및 새 문서 UI는 단계 6에서 연결한다.
- `.verison-list` file association의 등록 및 제거는 단계 8의 배포 검증에서 구현한다.
- backup은 원본과 같은 디렉터리의 `<document-name>.verison-list.bak`으로 한다.
- 저장소 API는 항상 명시적 문서 경로를 받는다.

### 4.2 schema version 1

제안 schema는 다음과 같다.

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
            "preferred_remote": null,
            "svn_switch_targets": []
        }
    ]
}
```

필드 계약은 다음과 같다.

| 필드 | 계약 |
| --- | --- |
| `schema_version` | 필수 정수이며 현재 값은 `1`이다. |
| `projects` | 필수 배열이다. 빈 배열을 허용한다. |
| `id` | 필수 비어 있지 않은 UTF-8 문자열이며 문서 안에서 유일해야 한다. 단계 5에서 생성 정책을 연결한다. |
| `path` | 필수 비어 있지 않은 UTF-8 문자열이다. 원문과 비교용 정규화 값을 함께 유지한다. |
| `display_name` | 선택 문자열이다. 생략 시 경로 마지막 구성 요소를 메모리 기본값으로 사용하되 원본 JSON에는 임의로 쓰지 않는다. |
| `vcs_hint` | 선택 문자열 `auto`, `git`, `svn`이며 기본값은 `auto`다. |
| `enabled` | 선택 boolean이며 기본값은 `true`다. |
| `preferred_remote` | 선택 string 또는 `null`이다. 실제 remote 존재 검증은 단계 4에서 한다. |
| `svn_switch_targets` | 선택 문자열 배열이며 기본값은 빈 배열이다. URL 접근성과 UUID 검증은 단계 4에서 한다. |

알 수 없는 top-level 및 project 필드는 warning을 반환하되 원문 JSON shadow에 보존하여 다시 저장할 때 유실하지 않는다. 도메인 public type에는 `nlohmann::json`을 노출하지 않는다.

### 4.3 오류와 부분 성공

- root JSON 파싱 실패, root type 오류, `schema_version` 오류와 `projects` type 오류는 문서 수준 실패다.
- project 하나의 필수 필드 누락, type 오류, 잘못된 enum과 잘못된 path syntax는 해당 항목 오류다. 다른 유효 항목은 계속 반환한다.
- 중복 ID와 중복 정규화 경로는 먼저 나온 유효 항목을 유지하고 이후 항목을 오류로 제외한다.
- 존재하지 않는 디렉터리는 schema 오류로 제외하지 않고 `missing` 상태와 warning을 반환한다.
- 접근 거부는 `inaccessible`, 일반 파일은 `not_directory`로 구분한다. 이 상태는 저장소 종류 판정과 별개다.
- 모든 진단은 안정적인 error code, severity, JSON pointer, project index와 선택적 project ID를 가진다.
- JSON 및 file I/O exception은 public API 밖으로 전달하지 않고 구조화 오류로 변환한다.

### 4.4 경로 의미와 중복 판정

- JSON의 상대 경로는 process working directory가 아니라 설정 파일이 있는 디렉터리를 기준으로 해석한다.
- 사용자가 입력한 `path` 문자열은 그대로 보존하고, 별도의 절대 비교 키를 만든다.
- 비교 키는 `.` 및 `..`, slash 방향, 중복 separator와 root 이외의 trailing separator를 정리한다.
- Windows 비교는 대소문자를 구분하지 않는다. 변환과 비교에 필요한 wide API는 Win32 adapter에 격리한다.
- 한글, emoji, 공백, drive path, UNC path와 long path를 처리한다.
- symlink와 junction이 같은 실제 위치를 가리키는지는 단계 5까지 판정하지 않는다.

### 4.5 schema migration 정책

schema version 1이 최초 배포 schema이므로 존재하지 않았던 version 0 형식을 임의로 만들지 않는다.

- `schema_version` 누락, `0`과 음수는 지원하지 않는 이전 schema 오류로 반환한다.
- `2` 이상은 지원하지 않는 미래 schema 오류로 반환하고 원본을 변경하지 않는다.
- version dispatch 경계와 migration 결과 type은 마련하되 실제 변환기는 후속 schema가 생길 때 추가한다.
- 단계 2의 migration fixture는 이전/미래 version을 안전하게 거부하고 원본을 보존하는 compatibility fixture로 정의한다.

### 4.6 동시 수정, 원자적 저장과 복구

- load 결과에는 호출자가 해석하지 않는 revision token을 포함한다.
- save 직전에 현재 파일 byte를 load 당시 byte와 비교한다. 다르면 `concurrent_modification`으로 실패하고 아무 파일도 교체하지 않는다.
- 저장 후보 전체를 먼저 serialize 및 재검증한 뒤 대상과 같은 디렉터리에 임시 파일을 만든다.
- UTF-8 무 BOM, 공백 4칸과 CRLF로 쓰고 `FlushFileBuffers` 후 교체한다.
- 기존 파일은 `ReplaceFileW`로 교체하면서 `.bak`을 만들고, 최초 생성은 write-through 이동을 사용한다.
- write, flush 또는 replace 실패 시 기존 파일을 보존하고 임시 파일을 정리한다.
- primary load 실패 시 유효한 `.bak`의 존재 여부와 진단을 반환하지만 자동 복구하지 않는다.
- 복구는 backup을 명시적으로 load한 뒤 별도 승인된 save 요청으로만 수행한다.

## 5. 코드 구조 제안

```text
src
├── domain
│   ├── project.h/.cpp
│   ├── repository_snapshot.h/.cpp
│   ├── operation.h/.cpp
│   └── diagnostic.h/.cpp
├── application
│   └── project_store.h
├── infrastructure
│   ├── json_project_document.h/.cpp
│   └── json_project_store.h/.cpp
└── platform/win32
    ├── workspace_document_path.h/.cpp
    └── project_file_system.h/.cpp

tests
├── domain_model_tests.cpp
├── project_schema_tests.cpp
├── project_path_tests.cpp
├── json_project_store_tests.cpp
└── fixtures/projects
```

의존성 방향은 다음과 같이 제한한다.

```text
domain <- application contract <- infrastructure JSON store <- Win32 file adapter
```

- `domain`은 Skia, Win32와 nlohmann/json을 참조하지 않는다.
- `application`의 `project_store` 계약은 명시적 경로, load/save result와 opaque revision token만 노출한다.
- `infrastructure`만 nlohmann/json을 사용한다.
- UTF-16, known folder, file flush와 replace는 `platform/win32`에만 둔다.
- 단계 2의 file I/O는 향후 worker pool에서 호출될 동기 API다. UI 또는 thread 연결은 하지 않는다.

## 6. 도메인 모델 범위

단계 2에서 다음 값 type을 확정한다.

- `project_id`, `project_definition`, 원문 및 정규화 `project_path`
- `vcs_hint`, `repository_kind`
- `remote_sync_state`: `unknown`, `up_to_date`, `behind`, `ahead`, `diverged`, `local_only`, `remote_target_missing`, `offline`, `error`
- `working_tree_state`: `unknown`, `clean`, `modified`, `conflicted`
- `repository_snapshot`: 현재 ref 또는 URL, revision, 비교 target, ahead/behind 수, path 상태와 마지막 확인 시각
- `operation_kind`, `operation_state`와 opaque `operation_id`
- config 및 storage diagnostic code, severity와 source location

Git 및 SVN 전용 parser 결과를 미리 가정하지 않고, 단계 4에서 필요한 필드는 별도 검수 후 확장한다.

## 7. 세부 작업과 검수 게이트

production 구현, test 작성과 bug 수정은 같은 검수 구간에서 함께 수행하지 않는다. 기존 test 실행과 compile 확인은 어느 구간에서나 허용하지만 새 test source는 test 구간에서만 작성한다.

| 순서 | 체크포인트 | 이번 구간에서 하는 일 | 구간 종료 조건 |
| --- | --- | --- | --- |
| 1 | `S2-P0` | 본 구현 계획과 설계 제안 작성 | 사용자 계획 승인 전 중지 |
| 2 | `S2-D1-CODE` | 도메인 값 type과 구조화 diagnostic production code, CMake target만 구현 | build/style 결과와 diff 제시 후 중지 |
| 3 | `S2-D1-TEST` | 도메인 model test만 추가하고 실행 | test 결과와 발견 결함 제시 후 중지 |
| 4 | `S2-D1-FIX` | 승인된 도메인 결함만 수정 | 회귀 결과 제시 후 중지. 결함이 없으면 사용자 확인으로 생략 |
| 5 | `S2-D2-CODE` | schema v1 parser, 부분 성공과 unknown-field 보존 production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 6 | `S2-D2-TEST` | 정상, 손상, 부분 오류와 version fixture 및 test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 7 | `S2-D2-FIX` | 승인된 schema/parser 결함만 수정 | 회귀 결과 제시 후 중지 |
| 8 | `S2-D3-CODE` | 경로 정규화, path 상태와 중복 판정 production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 9 | `S2-D3-TEST` | drive, UNC, 상대, 한글, 공백, long/missing path test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 10 | `S2-D3-FIX` | 승인된 경로 결함만 수정 | 회귀 결과 제시 후 중지 |
| 11 | `S2-D4-CODE` | revision token, 원자적 save, backup과 recovery production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 12 | `S2-D4-TEST` | 동시 수정과 write/flush/replace 실패 주입 test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 13 | `S2-D4-FIX` | 승인된 저장 및 복구 결함만 수정 | 회귀 결과 제시 후 중지 |
| 14 | `S2-D5-CODE` | `.verison-list` positional launch path parsing과 store 진입 계약 production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 15 | `S2-D5-TEST` | launch path 없음, 정상, 중복 및 잘못된 확장자 test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 16 | `S2-D5-FIX` | 승인된 launch contract 결함만 수정 | 회귀 결과 제시 후 중지 |
| 17 | `S2-V1` | 전체 build/test/analyze/install 검증과 단계 2 검증 문서 작성 | 최종 결과 제시 후 단계 2 승인 대기 |

각 체크포인트가 끝나면 반드시 `docs/handoff.md`에 다음을 갱신한다.

- 마지막 완료 또는 제출 체크포인트
- 사용자 승인 대기 항목
- 변경 파일과 검증 명령 및 결과
- 발견했지만 아직 수정하지 않은 결함
- 승인 후 허용되는 다음 작업 하나

사용자가 명시적으로 승인하기 전에는 다음 행으로 진행하지 않는다. test 구간에서 결함이 발견되어도 같은 구간에서 production code를 수정하지 않는다.

### 7.1 현재 진행 상태

- `S2-P0`: `.verison-list` 작업공간 문서 수정 후 승인 완료
- `S2-D1-CODE`: 구현 완료, `docs/verification/2026-08-14-stage-2-d1-code.md` 기준 사용자 검수 대기
- `S2-D1-TEST`: 시작 전이며 새 test source와 fixture 없음

## 8. 테스트 계획

### 8.1 도메인 model

- enum 고정 값과 기본 상태
- project 원문/정규화 path 분리
- repository snapshot의 Git/SVN 공통 표현
- diagnostic의 code, severity와 source 위치
- UI, Win32, Skia와 JSON type이 public domain header에 노출되지 않는지 compile 확인

### 8.2 schema와 부분 성공

- 정상 최소/전체 document와 빈 projects
- 잘못된 JSON, root type, schema version과 projects type
- 필수 필드 누락, optional 기본값과 잘못된 field type
- 중복 ID, unknown enum, 잘못된 UTF-8
- 하나의 잘못된 project와 여러 유효 project가 섞인 문서
- unknown top-level/project field의 warning 및 round-trip 보존
- version 0 및 미래 version의 무수정 거부

### 8.3 경로

- absolute drive, UNC와 상대 경로
- `.`, `..`, slash, trailing separator와 대소문자 중복
- 설정 파일 디렉터리 기준 상대 경로
- 한글, emoji, 공백과 long path
- existing, missing, inaccessible과 not-directory
- 같은 lexical path의 중복과 symlink/junction 실체 판정 보류

### 8.4 저장과 복구

- 최초 파일 생성과 기존 파일 교체
- UTF-8 무 BOM, CRLF와 안정적인 4칸 JSON 출력
- load 뒤 외부 변경이 있는 경우 무수정 실패
- temp write, flush, backup과 replace 실패 주입
- 실패 뒤 기존 파일 byte 보존과 temp 정리
- valid backup 검색과 명시적 backup load
- unknown field와 사용자가 입력한 path 원문 보존

실패 주입은 작은 file adapter fake로 결정적으로 검증하고, 실제 임시 디렉터리를 사용하는 Windows 통합 test를 별도로 둔다.

## 9. 단계 2 완료 조건

- 모든 체크포인트가 개별 사용자 검수를 통과한다.
- UI 없이 schema v1 프로젝트 파일의 load와 save가 동작한다.
- 일부 project 오류가 있어도 유효 project와 항목별 diagnostic이 반환된다.
- 원문 path를 보존하면서 Windows 비교용 정규화와 중복 판정이 동작한다.
- 동시 수정과 저장 실패에서 원본 byte가 보존된다.
- backup을 자동 적용하지 않고 명시적 복구 후보로 제공한다.
- 실행 파일이 하나의 `.verison-list` positional path를 작업공간 문서 진입 값으로 인식한다.
- VS2022 Debug/Release, VS2026 Debug, source style과 관련 test가 통과한다.
- `docs/verification/2026-08-14-stage-2.md`, `docs/change_log.md`와 `docs/handoff.md`가 최종 상태를 기록한다.

## 10. 계획 검수 항목

사용자는 `S2-P0`에서 특히 다음 제안을 승인하거나 수정한다.

1. `.verison-list` 작업공간 문서, 한 창당 하나의 활성 문서와 단계 8 file association
2. schema v1의 optional 기본값 및 `preferred_remote` 필드
3. unknown field warning 및 round-trip 보존
4. 상대 path의 설정 파일 디렉터리 기준 해석과 lexical duplicate 정책
5. 실제 version 0 migration을 만들지 않고 이전/미래 schema를 안전하게 거부하는 정책
6. exact source byte 비교, `.bak`과 명시적 복구를 사용하는 저장 정책
7. production code, test 작성과 bug 수정을 분리한 체크포인트 순서
