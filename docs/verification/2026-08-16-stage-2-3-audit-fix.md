# 단계 2·3 독립 감사 결함 수정 검증 기록

## 1. 배경과 결론

사용자가 단계 4 진행 전에 단계 2와 단계 3의 독립 감사를 지시했다. 감사는 계획 문서 대비 코드·테스트 전수 대조와 현재 HEAD의 build/test 재현으로 수행했고, 두 단계 모두 완료 조건 충족으로 판정됐다. 감사에서 발견된 결함과 테스트 갭을 이 기록의 범위에서 해소했다.

수정 후 VS2022 Debug/Release와 VS2026 Debug의 전체 CTest가 각각 139/139 통과했다(수정 전 135개에서 test 4개 증가). VS2022 `/analyze`와 aggregate `gitman_format_check`도 통과했다.

## 2. 감사 결과 요약

- 단계 2: 계획된 산출물(S2-D1~D5)이 모두 실재하고 대응 테스트가 있으며, 검증 기록의 수치 주장이 실측과 일치했다. 원본 보존 계약은 3중으로 구현·검증되어 있었다.
- 단계 3: 계획된 산출물(S3-D1~D5)이 모두 실재하고, 인용 규칙·pipe 데드락·취소 race·CP949 청크 경계가 모두 올바르게 구현되어 있었다. 검증 기록의 test 개수 주장(24+29+11+7+10=81, Catch2 총 129)이 실측과 정확히 일치했다.
- 검증 문서의 주장에서 과장이나 허위는 발견되지 않았다.

## 3. 수정한 발견 사항

### 3.1 단계 3 프로세스 실행 계층

| 발견 | 심각도 | 수정 |
| --- | --- | --- |
| 자식이 정상 종료해도 출력 pipe 쓰기 handle을 상속한 손자가 살아 있으면 reader의 `ReadFile`이 반환하지 않아 `run()`이 무기한 블록된다. Git의 `gc --auto` 같은 background 프로세스에서 단계 4에 실제로 노출될 수 있는 경로다. | 중간(잠재) | reader join에 2초 drain 유예를 도입했다. 유예를 넘기면 트리를 종료해 pipe를 닫고, job이 없어 손자가 살아남는 경우까지 대비해 `CancelSynchronousIo`로 동기 `ReadFile`을 직접 취소한다. 어떤 경로로든 join은 반드시 끝나며, 강제 마감 시 warning 진단을 남긴다. `spawn-detached`/`hold-handles` 도우미 명령과 회귀 test를 추가했다. |
| job 생성/배정 실패 fallback에서 `TerminateProcess`는 자식만 죽이므로 손자가 pipe를 잡으면 같은 join hang이 발생한다. | 낮음 | 위 `CancelSynchronousIo` 최후 수단이 job 없는 경로도 함께 해소한다. |
| reader 스레드 시작 후 취소 콜백 등록(`register_callback`)이 예외를 던지면 joinable한 `std::thread`가 unwinding으로 파괴되며 `std::terminate`가 일어난다. | 낮음(OOM 한정) | reader 생성 이후 구간 전체를 try로 감싸고, 예외 시 트리 종료와 join을 거쳐 `internal_error` 결과로 변환하도록 했다. |
| reader의 catch-all이 주석("절단 표시로만 남기고")과 달리 아무 표시도 남기지 않고 pipe drain도 중단해, timeout 없는 요청이면 자식이 backpressure로 블록될 수 있다. | 낮음(OOM 한정) | 실패 flag를 세우고 할당 없는 stack buffer로 pipe를 계속 비우도록 했다. runner는 flag를 보고 `output_truncated`와 `process_pipe_failed` 진단을 남긴다. |
| CP949 fallback 실행에서 8 KiB 강제 분할 경계가 UTF-8 규칙만 알아 2 byte 문자 가운데를 가르면 해당 문자가 양쪽 레코드에서 U+FFFD로 훼손된다. | 낮음 | `text_transcoder`에 `safe_split_position` 계약을 추가하고 Win32 구현이 `IsDBCSLeadByteEx`로 문자 경계를 계산하게 했다. fallback 모드에서 유효 UTF-8이 아닌 레코드는 이 경계로 분할한다. 대역 transcoder로 분할 위치를 고정하는 파이프라인 test를 추가했다. |
| URL password에 percent-encoding 없는 `@`가 있으면 첫 `@`를 구분자로 보아 뒷부분이 부분 누출된다(`https://u:p@ss@h` → `ss` 노출). | 정보 | authority가 끝나기 전의 마지막 `@`를 userinfo 구분자로 삼도록 scanner를 수정하고 회귀 test를 추가했다. idempotency는 유지된다. |
| 테스트 갭: 실행 파일 자체의 공백·한글·emoji 경로 미검증, 대용량 test가 계획(8 MiB) 대비 4 MB, 통합 test에 CTest timeout 없음. | 정보 | 도우미 사본을 `도우미 사본 🚀.exe`로 복사해 실행하는 test를 추가하고, 대용량 test를 8 MiB로 올렸으며, `catch_discover_tests`에 TIMEOUT 120을 부여해 hang 회귀가 실패로 드러나게 했다. |

### 3.2 단계 2 도메인과 설정 저장소

| 발견 | 심각도 | 수정 |
| --- | --- | --- |
| 계획의 계층 규칙("wide API는 platform/win32에 격리")과 달리 `infrastructure/workspace_document_paths.cpp`가 Win32 경로 어댑터를 직접 include하고 `gitman_workspace`가 `gitman_win32_platform`에 링크했다. fake file system 단위 test 중에도 실제 디스크를 조회해 테스트 결정성이 떨어졌다. | 중간 | `application/project_path_resolver.h`에 경로 해석 계약을 추가하고 Win32 구현(`win32::make_project_path_resolver`)을 분리했다. `resolve_workspace_document_paths`와 `json_project_store`는 주입받은 resolver만 사용하며, `gitman_workspace`는 더 이상 Win32 platform에 링크하지 않는다. 단위 test는 lexical fake resolver를, Win32 통합 test는 실제 구현을 주입한다. |
| `ReplaceFileW`가 `ERROR_UNABLE_TO_MOVE_REPLACEMENT_2`로 실패한 뒤 원본 복원(`MoveFileExW`)의 성공 여부를 확인하지 않아, 복원까지 실패하면 원본이 `.bak` 위치에만 남는데 호출자가 구분할 수 없었다. | 낮음 | 복원 실패를 `workspace_file_commit_failure::restore`로 구분해 보고하고, store가 `.bak` 복구 안내가 포함된 별도 메시지로 변환하게 했다. |
| `inaccessible` 경로 상태의 직접 테스트가 없었다. | 낮음 | 수정 중 확인한 사실: 로컬 NTFS는 속성 조회를 부모 디렉터리 메타데이터로 처리하므로 대상 deny ACE로도 `GetFileAttributesW`를 실패시킬 수 없다(호스트에서 실측). 접근 거부는 네트워크 공유·잠긴 파일에서만 실제 발생하므로, 오류→상태 매핑을 `project_path_state_from_error`로 공개하고 `ERROR_ACCESS_DENIED`/`ERROR_SHARING_VIOLATION` → `inaccessible`을 포함한 매핑 test로 검증했다. |
| `default_display_name`이 parser와 store 두 파일에 중복 정의되어 향후 불일치 위험이 있었다. | 정보 | `default_project_display_name`으로 `json_workspace_document.h`에 공개하고 양쪽이 공유하게 했다. |

## 4. 문서화로 해소한 발견 사항 (설계상 수용)

- **저장 시 byte 비교와 교체 사이의 TOCTOU 창**: 계획 4.6이 승인한 exact-byte 비교 방식의 본질적 한계다. 파일 잠금 없이 감지 창을 완전히 없앨 수 없으며, 실패 방향이 안전(저장 거부 또는 `.bak` 보존)하므로 수용한다.
- **8 KiB를 넘는 한 줄에 걸친 비밀의 레코드 분할 마스킹 우회**: 마스킹은 레코드 단위 방어 계층이고, 앱이 자격 증명을 인자로 만들지 않는 ADR-003 정책이 1차 방어다. 분할 지점에 걸린 8 KiB 초과 비밀은 현실적으로 드물어 수용한다.
- **unknown field 보존의 project ID 매칭 의존**: ID가 바뀐 project는 shadow template 매칭이 끊겨 해당 항목의 unknown field가 유실될 수 있다. ID 변경 기능 자체가 후속 단계 범위이므로 그 시점에 정책을 정한다.
- **revision token의 문서 경로 exact 비교**: 같은 파일을 다른 표기(대소문자 등)로 저장하려 하면 실패한다. 실패 방향이 안전(저장 거부)하므로 수용한다.
- **stem 없는 `.version-list` 파일명 허용**: 파일명이 정확히 `.version-list`인 문서도 유효한 경로로 통과한다. Windows에서 만들 수 있는 정상 파일명이므로 수용한다.
- **취소 event와 프로세스 종료가 동시에 신호되면 `exited` 우선**: `WaitForMultipleObjects`의 낮은 index 우선 규칙에 따른 것으로, 자식이 실제로 스스로 끝난 경우이므로 결과가 더 정확하다. 강제 종료가 일어난 경우에는 계획대로 `cancelled`로 보고된다.
- **동시 실행 stress의 재현 프로그램 부재**: `S3-V1`의 100 프로세스 stress는 저장소 밖 임시 프로그램으로 수행되어 기록상 수치만 남아 있다. 저장소 안에는 4스레드 동시 실행 test가 있으며, 더 큰 규모의 stress가 필요하면 단계 4 계획에서 재현 가능한 형태로 정한다.

## 5. 검증 matrix

| 검증 | 결과 |
| --- | --- |
| VS2022 Debug build | 통과 |
| `ctest` (vs2022 Debug) | 139/139 통과 |
| VS2022 Release build | 통과 |
| `ctest` (vs2022 Release) | 139/139 통과 |
| VS2022 `/analyze` build | 경고 없이 통과 |
| VS2026 Debug build | 통과 |
| `ctest` (vs2026 Debug) | 139/139 통과 |
| aggregate `gitman_format_check` | clang-format 및 source style 154개 파일 통과 |

CTest는 단계 3 종료 시점의 135개에서 139개로 늘었다. 추가된 test 4개는 손자 pipe 점유 drain 회귀, emoji 실행 파일 경로, fallback 강제 분할 경계, 속성 오류 상태 매핑이다. 기존 test는 대용량 출력 8 MiB 상향, `--password` raw `@` URL, `restore` 실패 유형 매핑이 보강됐다.

## 6. 후속 단계 참고

- drain 유예(2초)는 runner 상수다. 단계 4에서 Git background 프로세스(`gc --auto`, credential helper 등)의 실제 동작을 보고 명령별 정책(`gc.autoDetach` 대응 등)과 함께 재검토한다.
- `gitman_workspace`를 사용할 후속 조립 코드(단계 6)는 `win32::make_project_path_resolver()`로 실제 구현을 주입해야 한다.
- Catch2 test 전체에 CTest TIMEOUT 120초가 걸려 있으므로, 향후 120초를 넘는 통합 test를 추가할 때는 개별 상향이 필요하다.
