# 변경 이력

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
