# 단계 3 구현 계획 - 프로세스 실행 계층

## 1. 문서 상태

- 작성일: 2026-08-16
- 대상: 구현 단계 3
- 현재 상태: `S3-P0` 계획 사용자 검수 대기
- 현재 검수 게이트: 계획 승인
- 관련 요구사항: REQ-006, REQ-007, REQ-008, REQ-009~REQ-013, NFR-005~NFR-009
- 상위 문서: `docs/plan.md` 3.4, 3.9, 8장 단계 3, `docs/decisions/ADR-003-vcs-runtime-policy.md`, `docs/decisions/ADR-004-threading-and-state-ownership.md`
- 선행 조건: 단계 2 최종 사용자 승인 완료 (2026-08-16 사용자가 단계 3 진행을 지시함)

사용자는 이번 단계에서 계획, 작업과 테스트의 중간 지점마다 진행 상황 보고와 검수를 요구했다. 따라서 단계 2와 같은 `CODE` / `TEST` / `FIX` 3분할 체크포인트를 유지하고, 각 체크포인트 종료 시 보고 후 정지한다.

## 2. 목표

단계 3에서는 Git 및 SVN 명령을 실제로 실행할 때 사용할 **범용 외부 프로세스 실행 계층**만 구현한다. Git/SVN 고유 지식은 포함하지 않는다.

- 셸을 거치지 않고 실행 파일과 인자 배열로 자식 프로세스를 시작한다.
- stdout과 stderr를 교착 없이 비동기로 수집하고 카드 로그에 넣을 구조화 레코드로 변환한다.
- 종료 코드, 시작/종료 시각, 실행 시간, 취소 및 timeout 여부를 기록한다.
- 자식 프로세스가 대화형 입력을 기다려 멈추지 않게 stdin을 즉시 EOF로 만든다.
- 제한 시간과 취소를 제공하고 자식이 만든 손자 프로세스까지 함께 종료한다.
- 출력의 인코딩을 UTF-8 `std::u8string`으로 정규화하고 변환 실패를 표시한다.
- 자격 증명, token과 URL userinfo를 레코드와 기록된 명령줄에서 마스킹한다.
- 공백, 한글, emoji, 긴 경로와 대용량 출력에서 동작을 보장한다.
- 예외를 public API 밖으로 던지지 않고 구조화 진단으로 변환한다.

## 3. 단계 3에서 하지 않는 일

- Git/SVN 실행 파일 탐색, `--version` 확인과 버전 미달 UX (단계 4)
- Git/SVN 명령 인자 조립과 출력 파싱 (단계 4)
- 카드별 `operation_lane`, 저장소별 직렬화와 전체 동시 실행 상한 (`task_scheduler`, 단계 6~7)
- 카드별 로그 ring buffer, 로그 필터와 하단 터미널 UI (`per_project_log_store`, 단계 7)
- ADR-004의 범용 message queue, dispatcher와 thread bridge (단계 6 별도 설계 승인 후)
- 파일 로그 저장, 보존 기간과 진단 화면 (단계 8)
- 프로세스 우선순위, 리소스 제한, 샌드박싱

`task_scheduler`와 로그 저장소가 없으므로 단계 3의 실행 계층은 **호출한 스레드에서 완료까지 블로킹하는 동기 API**로 만들고, 스레드 배치와 병렬성 정책은 후속 단계가 결정한다. 이렇게 하면 ADR-004의 구현 차단 조건을 건드리지 않는다.

## 4. 사용자 검수에서 확정할 설계 제안

### 4.1 계층 경계와 실행 모델

- `domain`은 프로세스 실행의 값 type(스트림 종류, 완료 사유, 출력 레코드, 결과)만 정의하고 Win32, Skia, nlohmann/json을 참조하지 않는다.
- `application`은 `process_runner` 추상 인터페이스, 요청 값과 출력 sink 계약만 정의한다.
- `infrastructure`는 OS와 무관한 출력 파이프라인(줄 분할, UTF-8 정규화, 마스킹)을 구현한다.
- `platform/win32`만 `CreateProcessW`, pipe, job object와 handle 수명을 다룬다.
- 실행 API는 `run(request, sink, token)` 한 번 호출로 시작부터 종료까지 처리하는 동기 함수다. 호출자는 worker 스레드에서 호출한다고 가정한다.
- 이 계약은 단계 2의 `workspace_document_file_system` 선례와 같다. 추상 계약은 상위 계층에 두고 Win32 구현체는 `gitman_win32_platform`에 두어 링크 순환을 만들지 않는다.

### 4.2 프로세스 시작 계약

| 항목 | 제안 |
| --- | --- |
| 실행 파일 | 절대 경로만 허용한다. `lpApplicationName`에 명시하고 PATH 탐색과 확장자 추론을 사용하지 않는다. 상대 경로와 빈 값은 요청 검증에서 거부한다. |
| 인자 | `std::vector<std::u8string>` argv로 받아 `CommandLineToArgvW` 규칙에 맞게 인용한다. 셸 metacharacter는 특별 처리하지 않는다. |
| 셸 | `cmd.exe`와 `CREATE_NO_WINDOW` 없는 콘솔 생성을 사용하지 않는다. `CREATE_NO_WINDOW`와 `CREATE_UNICODE_ENVIRONMENT`를 지정한다. |
| 작업 디렉터리 | 요청에 명시한 절대 경로만 사용하고 호출자 프로세스의 현재 디렉터리에 의존하지 않는다. |
| 환경 | 부모 환경을 복사한 뒤 요청의 override 목록만 덮어쓰거나 삭제한다. 실제 비대화형 변수 값은 단계 4의 provider가 요청에 넣는다. |
| stdin | 항상 `NUL` 장치 handle을 넘긴다. 자식이 프롬프트를 읽으면 즉시 EOF가 되어 무한 대기가 발생하지 않는다. |
| stdout/stderr | 서로 다른 익명 pipe 두 개를 사용한다. 부모가 가진 쓰기 handle 사본은 `CreateProcessW` 직후 닫아야 EOF를 관측할 수 있다. |
| handle 상속 | `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`로 명시한 handle만 상속시켜 다른 handle 누출을 막는다. |
| 프로세스 트리 | 자식을 `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job object에 넣어 timeout, 취소와 비정상 경로에서 손자 프로세스(예: Git이 실행한 `ssh`)까지 함께 종료한다. |
| 시작 실패 | `CreateProcessW` 실패는 `start_failed`와 Win32 error code로 반환한다. 예외를 던지지 않는다. |
| UTF-8 경계 | UTF-8 요청 값은 기존 `platform/win32/utf8.h`로만 UTF-16으로 변환하고, 변환 실패도 구조화 오류로 반환한다. |

인자 인용은 다음 규칙을 구현한다.

- 공백, 탭, 따옴표, 개행이 없으면 그대로 사용한다.
- 그렇지 않으면 큰따옴표로 감싸고 내부 `"`는 `\"`로, 닫는 따옴표 바로 앞의 backslash 연속은 두 배로 늘린다.
- 빈 문자열 인자는 `""`로 표현한다.
- 인자 인용 결과는 결과 객체에 마스킹된 형태로 보관해 로그와 재현에 사용한다.

### 4.3 출력 수집과 인코딩

- pipe당 전용 reader 스레드 하나를 사용한다. 두 스레드는 runner 내부 구현이며 ADR-004의 범용 message component가 아니다.
- 한 pipe만 읽는 단일 스레드 구조는 다른 pipe 버퍼가 가득 차면 교착되므로 채택하지 않는다.
- 읽기 단위는 64 KiB이며 부분 UTF-8 sequence는 다음 읽기까지 보류하여 chunk 경계에서 문자가 깨지지 않게 한다.
- 레코드는 줄 단위로 만든다. `\r\n`과 `\n`은 줄 끝이고, 단독 `\r`도 줄 끝으로 처리하되 `progress` 표시를 세워 Git fetch 진행 출력이 단계 7에서 접힐 수 있게 한다.
- 줄 끝 없이 8 KiB를 넘기면 강제로 레코드를 끊고 `continued` 표시를 세운다.
- 프로세스 종료 후 남은 미완결 tail은 마지막 레코드로 flush한다.
- 유효하지 않은 byte sequence는 U+FFFD로 대체하고 레코드에 `replaced_invalid_bytes`를 세운다.
- 요청의 `text_encoding`이 `active_code_page_fallback`이면, UTF-8로 해석되지 않는 레코드만 주입된 transcoder로 활성 code page에서 변환한다. transcoder 인터페이스는 `application`에 두고 구현은 Win32 adapter에 둔다.
- 스트림별이 아닌 **실행 단위 단조 증가 sequence**를 부여한다. 두 스트림이 동시에 도착해도 한 실행 안에서는 순서가 고정되며, 같은 스트림 안의 상대 순서는 항상 보존된다.
- sink 호출은 runner 내부 mutex로 직렬화한다. 따라서 sink 구현은 스레드 안전을 스스로 보장하지 않아도 되지만, sink 안에서 blocking 작업을 하면 자식이 backpressure를 받는다는 계약을 문서화한다.
- 스트림별 `maximum_captured_bytes` 상한을 넘으면 이후 레코드를 버리고 결과에 `output_truncated`를 세운다. 상한 도달 뒤에도 프로세스는 계속 실행하고 pipe는 계속 비운다.

### 4.4 timeout, 취소와 종료 정책

- 요청은 `std::optional<std::chrono::milliseconds>` timeout을 가진다. 값이 없으면 무제한이다.
- 취소는 `process_cancellation_source`와 복사 가능한 `process_cancellation_token`으로 제공한다.
- token은 `is_cancelled()`와 콜백 등록을 제공한다. Win32 runner는 실행 시작 시 event를 `SetEvent`하는 콜백을 등록하고 반환 전에 해제한다. 이 방식은 polling 없이 대기하며 Win32 type을 상위 계층에 노출하지 않는다.
- 대기는 프로세스 handle과 취소 event를 함께 기다린다. timeout 또는 취소가 발생하면 job을 종료해 트리 전체를 죽인다.
- job 종료로 자식의 pipe 쓰기 handle이 모두 닫히므로 reader 스레드의 `ReadFile`이 반환한다. runner는 항상 두 스레드를 join한 뒤 결과를 반환한다.
- 취소 또는 timeout으로 끝난 실행은 자식이 우연히 exit code를 남겼더라도 완료 사유를 `cancelled` 또는 `timed_out`으로 보고한다.
- Windows에는 콘솔 없는 자식에게 보낼 안전한 graceful signal이 없으므로 유예 시간 없이 job 종료를 사용한다. 이 결정을 계획에 명시해 단계 7의 취소 UX가 즉시 종료를 전제하게 한다.
- 앱 종료 정책: 소유자가 모든 실행에 취소를 요청하고 각 `run` 호출이 반환할 때까지 join한다. reader 스레드를 detach하거나 handle을 누출한 채 종료하지 않는다.

### 4.5 비밀 마스킹

- 마스킹은 sink에 넘기기 전에 레코드 텍스트에, 그리고 결과에 저장하는 명령줄에 적용한다. 마스킹되지 않은 원문은 어떤 public 값에도 보관하지 않는다.
- `std::regex`는 대용량 출력에서 성능과 스택 위험이 있어 사용하지 않고 직접 작성한 스캐너로 구현한다.
- 초기 규칙:
    - URL userinfo: `scheme://user:secret@host` → `scheme://user:***@host`
    - 자격 증명 option의 값: `--password <value>`와 `--password=<value>` 형태를 모두 처리하고 option 이름은 남기고 값만 `***`로 치환한다.
    - `Authorization:`, `PRIVATE-TOKEN:`, `x-access-token:` 뒤의 값
    - 알려진 token prefix: `ghp_`, `github_pat_`, `gho_`, `glpat-`
    - Base64 Basic 자격 증명 형태 `Basic <base64>`
- 마스킹은 idempotent하며 이미 마스킹된 문자열을 다시 바꾸지 않는다.
- 마스킹 규칙 목록은 단계 4에서 provider별 인자를 확정할 때 확장할 수 있도록 한 곳에 모은다.
- 마스킹은 방어 계층이며 앱은 애초에 자격 증명을 인자로 만들지 않는다는 ADR-003 정책과 함께 유지한다.

### 4.6 오류와 진단

- public API는 예외를 던지지 않는다. 모든 실패를 결과 값과 `diagnostic` 목록으로 반환한다.
- 완료 사유는 `exited`, `start_failed`, `timed_out`, `cancelled`, `invalid_request`다.
- 기존 `diagnostic_code`에 다음 값을 추가한다: `process_start_failed`, `process_timed_out`, `process_cancelled`, `process_pipe_failed`, `process_output_truncated`, `invalid_process_request`.
- `diagnostic_source`는 문서 경로 중심이므로 프로세스 실행에서는 사용하지 않고, 실행 식별과 실행 파일 정보는 결과 값에 둔다. `diagnostic_source`의 확장 필요 여부는 D1에서 확인한다.
- 종료 코드는 `std::optional<std::int32_t>`이며 `exited`일 때만 값이 있다.
- 시각은 `std::chrono::system_clock::time_point`(표시용)과 `std::chrono::milliseconds` 실행 시간(측정용)으로 둘 다 기록한다.

### 4.7 스레드 안전과 ADR-004 경계

- 하나의 `process_runner` 인스턴스는 여러 스레드에서 동시에 `run`을 호출할 수 있어야 한다. 인스턴스는 실행별 상태를 공유하지 않는다.
- runner가 만드는 reader 스레드는 실행 하나에 종속되며 실행 반환 시 반드시 join된다.
- 이 구조는 단계 6의 범용 message component와 무관하다. 프로세스 실행 계층은 queue, dispatcher, envelope를 정의하지 않는다.
- 후속 단계가 결과를 logic thread로 전달하는 방식은 승인된 message 설계에서 정한다.

## 5. 코드 구조 제안

```text
src
├── domain
│   ├── diagnostic.h/.cpp          (진단 code 추가)
│   └── process_execution.h/.cpp   (스트림, 완료 사유, 레코드, 결과 값)
├── application
│   ├── process_request.h/.cpp     (요청 값과 요청 검증)
│   ├── process_runner.h           (추상 인터페이스와 sink 계약)
│   └── process_cancellation.h/.cpp(취소 source 및 token)
├── infrastructure
│   ├── process_output_pipeline.h/.cpp (줄 분할, UTF-8 정규화, 인코딩 fallback 연결)
│   ├── command_line_builder.h/.cpp    (Windows 인자 인용)
│   └── secret_masking.h/.cpp          (마스킹 규칙)
└── platform/win32
    ├── win32_process_runner.h/.cpp    (CreateProcessW, pipe, job, 대기)
    └── win32_text_transcoder.h/.cpp   (활성 code page fallback)

tests
├── process_request_tests.cpp
├── command_line_builder_tests.cpp
├── process_output_pipeline_tests.cpp
├── secret_masking_tests.cpp
├── win32_process_runner_tests.cpp
└── helpers/process_test_child.cpp
```

CMake target 구성 제안은 다음과 같다.

- `gitman_domain`에 `domain/process_execution.*`를 추가한다.
- 새 static library `gitman_process`를 만들고 `application/`과 `infrastructure/`의 프로세스 source를 넣는다. `gitman_domain`을 PUBLIC, `gitman_win32_platform`을 PRIVATE로 링크한다.
- `platform/win32/win32_process_runner.*`와 `win32_text_transcoder.*`는 기존 `gitman_win32_platform`에 추가한다.
- 테스트 도우미는 콘솔 subsystem 실행 파일 target `gitman_process_test_child`로 만들고 `install` rule을 주지 않아 단일 exe 배포에 영향이 없게 한다.
- `gitman_tests`는 `gitman_process`를 링크하고, 도우미 실행 파일 경로를 compile definition으로 받는다.

의존성 방향은 다음과 같이 제한한다.

```text
domain <- application contract <- infrastructure pipeline <- Win32 process adapter
```

## 6. public API 초안

검수 편의를 위한 초안이며 세부 이름은 `S3-D1-CODE`에서 확정한다.

```cpp
enum class process_stream
{
    standard_output,
    standard_error,
};

enum class process_completion
{
    exited,
    start_failed,
    timed_out,
    cancelled,
    invalid_request,
};

struct process_output_record
{
    std::uint64_t sequence {};
    process_stream stream { process_stream::standard_output };
    std::u8string text {};
    bool progress { false };
    bool continued { false };
    bool replaced_invalid_bytes { false };
};

struct process_result
{
    process_completion completion { process_completion::invalid_request };
    std::optional<std::int32_t> exit_code {};
    std::chrono::system_clock::time_point started_at {};
    std::chrono::system_clock::time_point finished_at {};
    std::chrono::milliseconds duration {};
    std::uint64_t record_count {};
    bool output_truncated { false };
    std::optional<std::uint32_t> native_error {};
    std::u8string masked_command_line {};
    std::vector<diagnostic> diagnostics {};
};

class process_output_sink
{
public:
    virtual void on_record(const process_output_record& record) = 0;
};

class process_runner
{
public:
    [[nodiscard]] virtual process_result run(
        const process_request& request,
        process_output_sink* sink,
        const process_cancellation_token& token) noexcept
        = 0;
};
```

## 7. 세부 작업과 검수 게이트

production 구현, test 작성과 bug 수정은 같은 검수 구간에서 함께 수행하지 않는다. 기존 test 실행과 compile 확인은 어느 구간에서나 허용하지만 새 test source와 도우미는 test 구간에서만 작성한다.

| 순서 | 체크포인트 | 이번 구간에서 하는 일 | 구간 종료 조건 |
| --- | --- | --- | --- |
| 1 | `S3-P0` | 본 구현 계획과 설계 제안 작성 | 사용자 계획 승인 전 중지 |
| 2 | `S3-D1-CODE` | 프로세스 값 model, 요청 검증, runner/sink 계약, 취소 primitive와 CMake target만 구현 | build/style 결과와 diff 제시 후 중지 |
| 3 | `S3-D1-TEST` | 값 model, 요청 검증과 취소 primitive test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 4 | `S3-D1-FIX` | 승인된 계약 결함만 수정 | 회귀 결과 제시 후 중지. 결함이 없으면 사용자 확인으로 생략 |
| 5 | `S3-D2-CODE` | 인자 인용, `CreateProcessW` 시작, 작업 디렉터리, 환경, stdin `NUL`, 종료 코드와 시작 실패 production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 6 | `S3-D2-TEST` | 콘솔 도우미 실행 파일과 인용, 종료 코드, cwd, 환경, 시작 실패, 공백 및 한글 경로 test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 7 | `S3-D2-FIX` | 승인된 시작 계약 결함만 수정 | 회귀 결과 제시 후 중지 |
| 8 | `S3-D3-CODE` | reader 스레드, 줄 분할, UTF-8 경계 처리, 인코딩 fallback, sequence와 캡처 상한 production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 9 | `S3-D3-TEST` | chunk 경계, 대용량 출력, 두 스트림 순서, 상한 초과와 잘못된 byte test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 10 | `S3-D3-FIX` | 승인된 출력 파이프라인 결함만 수정 | 회귀 결과 제시 후 중지 |
| 11 | `S3-D4-CODE` | timeout, 취소, job object 트리 종료와 스레드 정리 production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 12 | `S3-D4-TEST` | timeout, 실행 중 취소, 손자 프로세스 종료, stdin EOF와 반복 실행 test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 13 | `S3-D4-FIX` | 승인된 timeout 및 취소 결함만 수정 | 회귀 결과 제시 후 중지 |
| 14 | `S3-D5-CODE` | 비밀 마스킹 규칙과 명령줄 기록 마스킹 production code만 구현 | build/style 결과와 diff 제시 후 중지 |
| 15 | `S3-D5-TEST` | URL userinfo, password option, token prefix, idempotency와 출력 마스킹 test만 추가 | test 결과와 발견 결함 제시 후 중지 |
| 16 | `S3-D5-FIX` | 승인된 마스킹 결함만 수정 | 회귀 결과 제시 후 중지 |
| 17 | `S3-V1` | 전체 build/test/analyze/install 검증, 동시 실행 stress와 단계 3 검증 문서 작성 | 최종 결과 제시 후 단계 3 승인 대기 |

각 체크포인트가 끝나면 반드시 `docs/handoff.md`에 다음을 갱신한다.

- 마지막 완료 또는 제출 체크포인트
- 사용자 승인 대기 항목
- 변경 파일과 검증 명령 및 결과
- 발견했지만 아직 수정하지 않은 결함
- 승인 후 허용되는 다음 작업 하나

사용자가 명시적으로 승인하기 전에는 다음 행으로 진행하지 않는다. test 구간에서 결함이 발견되어도 같은 구간에서 production code를 수정하지 않는다.

### 7.1 현재 진행 상태

- `S3-P0`: 2026-08-16 사용자 승인 완료. 체크포인트 17개 유지와 활성 code page fallback의 단계 3 포함을 함께 승인했다.
- `S3-D1-CODE`: 값 model, 요청 검증, runner/sink 계약, 취소 primitive와 `gitman_process` target 구현 및 사용자 승인 완료. 결과는 `docs/verification/2026-08-16-stage-3-d1-code.md`에 기록했다.
- `S3-D1-TEST`: 계약 test 24개 작성 및 사용자 승인 완료. 결과는 `docs/verification/2026-08-16-stage-3-d1-test.md`에 기록했다.
- `S3-D1-FIX`: 발견 production 결함이 없어 사용자 확인에 따라 생략 완료
- `S3-D2-CODE`: 인자 인용, `CreateProcessW` 시작, 작업 디렉터리와 환경, stdin `NUL`, 종료 코드와 시작 실패 구현 후 1차 검수에서 두 가지 수정 지시를 받았다. wait 실패 시 자식 정리와 `internal_error` 도입, 출력 pipe 및 reader 스레드 포함을 반영해 재제출했고 사용자 검수 대기 중이다. 결과는 `docs/verification/2026-08-16-stage-3-d2-code.md`에 기록했다.
- `S3-D2-TEST`: argv 기반 도우미 target과 test 29개 작성 완료, 양 toolchain 전체 CTest 107/107 통과, 발견 production 결함 없음, 사용자 검수 대기. 결과는 `docs/verification/2026-08-16-stage-3-d2-test.md`에 기록했다.
- `S3-D2-FIX`: 발견 production 결함이 없어 사용자 확인에 따라 생략 완료
- `S3-D3-CODE`: 활성 code page fallback transcoder 구현과 파이프라인 연결 및 사용자 승인 완료. 결과는 `docs/verification/2026-08-16-stage-3-d3-code.md`에 기록했다.
- `S3-D3-TEST`: fallback과 UTF-8 유효성 test 11개 작성 및 사용자 승인 완료. 결과는 `docs/verification/2026-08-16-stage-3-d3-test.md`에 기록했다.
- `S3-D3-FIX`: 발견 production 결함이 없어 사용자 확인에 따라 생략 완료
- `S3-D4-CODE`: timeout, 취소, job object 트리 종료와 스레드 정리 구현 및 사용자 승인 완료. 결과는 `docs/verification/2026-08-16-stage-3-d4-code.md`에 기록했다.
- `S3-D4-TEST`: 도우미 명령 3개와 timeout, 취소, 손자 종료, handle 누수 test 7개 작성 및 사용자 승인 완료. 결과는 `docs/verification/2026-08-16-stage-3-d4-test.md`에 기록했다.
- `S3-D4-FIX`: 발견 production 결함이 없어 사용자 확인에 따라 생략 완료
- `S3-D5-CODE`: 비밀 마스킹 규칙과 출력 및 명령줄 적용 구현 완료, 사용자 검수 대기. 결과는 `docs/verification/2026-08-16-stage-3-d5-code.md`에 기록했다.
- 범위 이동: 사용자 지시로 출력 pipe와 줄 단위 레코드가 `S3-D2`로 옮겨졌다. `S3-D3`에는 활성 code page fallback transcoder 구현과 연결이 남는다. 체크포인트 수는 17개를 유지한다.

## 8. 테스트 계획

### 8.1 테스트 도우미 실행 파일

실제 Git/SVN에 의존하면 결정적 검증이 불가능하고 현재 호스트에는 SVN이 없다. 따라서 `S3-D2-TEST`에서 콘솔 subsystem 도우미 `gitman_process_test_child`를 추가한다.

도우미는 반드시 표준 `wmain` argv를 사용하는 실행 파일이어야 한다. `cmd.exe`는 `CommandLineToArgvW` 규칙이 아닌 자체 따옴표 처리를 하므로 test 자식으로 쓰지 않는다. 근거는 `docs/verification/2026-08-16-stage-3-d2-code.md` 6장에 있다.

| 도우미 명령 | 동작 |
| --- | --- |
| `echo-args` | 받은 argv를 한 줄씩 그대로 출력해 인용 왕복을 검증한다. |
| `echo-cwd` | 현재 작업 디렉터리를 UTF-8로 출력한다. |
| `echo-env <name>` | 지정한 환경 변수 값을 출력한다. |
| `emit <bytes> <stream>` | 지정 스트림에 지정 크기의 결정적 pattern을 출력한다. |
| `interleave <count>` | stdout과 stderr에 번갈아 출력해 스트림 순서를 검증한다. |
| `emit-bytes <hex>` | 잘못된 UTF-8과 활성 code page byte를 그대로 출력한다. |
| `read-stdin` | stdin을 읽어 EOF 여부와 읽은 byte 수를 출력한다. |
| `sleep <ms>` | 지정 시간 대기해 timeout과 취소를 검증한다. |
| `spawn-child <ms>` | 손자 프로세스를 만들고 대기해 트리 종료를 검증한다. |
| `exit <code>` | 지정 종료 코드로 끝난다. |

도우미는 테스트 전용 target이며 `install` 대상이 아니다. 공백과 한글이 포함된 임시 디렉터리로 복사해 경로 처리도 검증한다.

### 8.2 단위 테스트 (자식 프로세스 없음)

- 인자 인용: 공백, 탭, 따옴표, 연속 backslash, 빈 인자, 한글과 emoji, 셸 metacharacter 무처리
- 요청 검증: 빈 실행 파일, 상대 경로, 존재하지 않는 작업 디렉터리 형식, 0 이하 timeout, 0 상한
- 출력 파이프라인: chunk 경계로 쪼갠 multibyte 문자, `\r\n` / `\n` / 단독 `\r`, 8 KiB 초과 줄, 미완결 tail flush, 잘못된 byte 대체, code page fallback
- sequence: 단일 실행 안의 단조 증가와 스트림 내 상대 순서 보존
- 마스킹: URL userinfo, `--password` 두 형태, token prefix, `Basic`, idempotency, 마스킹 대상이 없는 문자열 불변
- 취소 primitive: 취소 전후 상태, 콜백 등록 및 해제, 여러 token 복사본

### 8.3 통합 테스트 (도우미 실행)

- 종료 코드 0과 비정상 종료 코드
- stdout, stderr와 두 스트림 동시 출력의 순서
- 8 MiB 출력에서 교착 없이 전량 수집
- 캡처 상한 초과 시 절단 표시와 프로세스 정상 완료
- timeout 초과 시 `timed_out`과 트리 종료
- 실행 중 취소 시 `cancelled`와 즉시 종료
- 손자 프로세스가 job 종료로 함께 사라지는지 확인
- `read-stdin`이 프롬프트 대기 없이 즉시 EOF를 보는지 확인
- 존재하지 않는 실행 파일의 `start_failed`와 Win32 error code
- 공백, 한글, emoji가 포함된 실행 파일 경로 및 작업 디렉터리
- 환경 override 적용과 삭제
- 같은 runner 인스턴스로 여러 스레드에서 동시에 실행하는 스레드 안전 확인
- 반복 실행 후 handle 및 스레드 누수 확인

### 8.4 stress 및 회귀

- `S3-V1`에서 동시 실행 stress(예: 4개 스레드 × 다수 실행)와 `ctest --repeat until-pass:1` 계열 재실행으로 flakiness를 확인한다.
- 실행 시간이 긴 통합 test에는 CTest timeout을 명시한다.

## 9. 단계 3 완료 조건

- 모든 체크포인트가 개별 사용자 검수를 통과한다.
- 셸 없이 인자 배열로 프로세스를 실행하고 종료 코드와 실행 시간을 보고한다.
- 성공, 실패, timeout, 취소, 시작 실패와 잘못된 요청이 서로 구분되는 결과로 반환된다.
- 대용량 출력과 두 스트림 동시 출력에서 교착이 없다.
- 출력이 UTF-8로 정규화되고 변환 실패가 표시된다.
- 자격 증명과 token이 레코드 및 기록된 명령줄에서 마스킹된다.
- 공백, 한글, emoji 경로에서 실행과 작업 디렉터리가 동작한다.
- 자식과 손자 프로세스가 timeout 및 취소에서 함께 종료되고 스레드와 handle이 정리된다.
- VS2022 Debug/Release, VS2022 `/analyze`, VS2026 Debug, aggregate format/style과 전체 CTest가 통과한다.
- 단일 exe install 결과에 테스트 도우미가 포함되지 않는다.
- `docs/verification/`의 단계 3 기록, `docs/change_log.md`와 `docs/handoff.md`가 최종 상태를 기록한다.

## 10. 계획 검수 항목

사용자는 `S3-P0`에서 특히 다음 제안을 승인하거나 수정한다.

1. 단계 3의 실행 API를 동기 블로킹 `run` 하나로 두고 스레드 배치는 단계 6~7에 남기는 범위 분할
2. 절대 경로 실행 파일만 허용하고 PATH 탐색 및 `--version` 확인을 단계 4로 미루는 결정
3. stdin을 항상 `NUL`로 연결해 대화형 프롬프트를 EOF로 만드는 정책
4. job object로 트리를 즉시 종료하고 graceful 유예를 두지 않는 취소 정책
5. 줄 단위 레코드, 단독 `\r` progress 표시, 8 KiB 강제 분할과 실행 단위 단조 sequence
6. 활성 code page fallback transcoder를 단계 3에서 함께 구현하는 범위
7. 마스킹 규칙 초기 목록과 `std::regex` 미사용 결정
8. 테스트 전용 콘솔 도우미 실행 파일 target 추가
9. `CODE` / `TEST` / `FIX` 5분할과 `S3-V1`로 구성한 17개 체크포인트 순서

`S3-D5` 마스킹 구간은 다른 구간보다 작다. 사용자가 원하면 `S3-D3`의 출력 파이프라인과 하나의 구간으로 합쳐 체크포인트를 14개로 줄일 수 있다.

2026-08-16 사용자 검수 결과는 다음과 같다.

- 위 9개 항목을 포함한 계획을 그대로 승인했다.
- 체크포인트는 17개를 유지하고 마스킹을 독립 구간으로 둔다.
- 활성 code page fallback transcoder는 단계 3에서 함께 구현한다.

## 12. 계획 이후 확인된 사항

- 프로세스 진단은 `diagnostic_source`를 확장하지 않고 메시지와 `process_result::masked_command_line`으로 문맥을 제공한다. 11장의 확인 항목을 `S3-D1-CODE`에서 이렇게 정리했다.
- 활성 code page fallback transcoder 인터페이스는 출력 파이프라인과 응집도가 높아 `S3-D3-CODE`에서 정의한다. 요청 값의 `process_text_encoding`은 `S3-D1-CODE`에 포함했다.
- 강제 분할이 UTF-8 sequence를 쪼개지 않도록 레코드 크기 상한의 최소값 4 byte를 요청 검증에 추가했다.
- 기존 aggregate `gitman_format_check`가 `utf8.cpp`, `win32_application.cpp`, `ui_theme.h`에서 실패했다. 사용자가 clang-format 결과 수용을 선택해 `S3-D1-TEST`에서 정렬하고 `docs/code_style.md` 2장에 formatter 우선 규칙을 명시했다.

## 11. 미결정 항목

- 기본 timeout 값과 기본 캡처 상한. 실제 값은 단계 4에서 명령별로 정하는 것이 안전하므로 단계 3은 요청 필수 값으로만 두고 기본값을 강제하지 않을 것을 제안한다.
- `LC_ALL` 등 로캘 강제 여부. 기계 판독 출력을 쓰는 단계 4에서 명령별로 결정한다.
- 진행 출력(`\r`) 접힘 정책의 최종 UX는 단계 7에서 정한다.
- `diagnostic_source`에 실행 식별 정보를 추가할지 여부는 `S3-D1-CODE`에서 확인한다.
