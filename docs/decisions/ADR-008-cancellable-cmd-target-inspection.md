# ADR-008: 취소 가능한 배치 사전 조회

## 상태

구현·검증 후 검수 대상 — 2026-11-06. 사용자가 닿지 않는 UNC 배치 조회 취소 작업을 지시했다.
기존 신뢰·cmd 보정 정책은 유지하고 파일 I/O의 실행 경계만 바꾼다.

## 배경

logic은 5초 뒤 기다리는 실행을 버리지만 worker의 GetFileAttributesW·CreateFileW·ReadFile은
계속 남을 수 있었다. 같은 대화형 lane의 다음 조회와 worker join도 그 I/O를 기다렸다.

Windows의 I/O 취소는 파일시스템·드라이버 지원에 달려 있으며, thread 취소 요청만으로 완료를
보장할 수 없다. 별도 thread를 detach하거나 강제 종료하면 probe와 요청 데이터의 수명을
깨뜨릴 수 있어 쓰지 않는다. 근거는 Microsoft의
[I/O 취소 설명](https://learn.microsoft.com/en-us/windows/win32/fileio/canceling-pending-i-o-operations)이다.

## 결정

1. `cmd_target_inspector`를 application의 취소 가능한 조회 경계로 둔다. executor는 이 경계를
   worker에서 부르고, 묶이지 않았으면 직접 파일 조회로 돌아가지 않고 실패한다.
2. Win32 구현은 같은 exe의 숨은 조회 모드를 실행한다. DPI·COM·창·설정·renderer를 만들기 전에
   분기하며 동기 파일 조회는 자식 안에서만 한다. 별도 배포 파일은 없다.
3. 경로·질의는 최대 1MiB 익명 공유 메모리로 나르고 작은 고정 결과와 event로 응답한다. argv는
   내부 모드와 상속 handle 두 개뿐이다. HANDLE_LIST로 이 둘만 상속하며 자식을 정지 상태로 만들어
   kill-on-close job에 넣은 뒤 시작한다. 부모에서 pipe reader thread나 동기 입력 쓰기를 만들지 않는다.
4. query 전용 취소 source를 registry에 등록한다. 제한 시간·문서 교체·닫기·종료는 이 source를
   취소한다. 기본 5초 제한은 기존과 같다. 취소·시간 초과·실패 뒤 사용자 프로그램은 실행하지 않는다.
5. 부모는 자식의 파일 I/O가 끝나기를 기다리지 않고 취소된 조회에서 돌아온다. 종료 요청 후 자식이
   남아 있으면 같은 inspector는 다음 자식을 늘리지 않고 조회 실패로 응답한다. 정상 회수 뒤 재사용한다.
6. 부모의 cwd는 조회 대상 UNC가 아닌 실행 파일 폴더다. 실행 파일은 로컬 설치가 전제이며 UNC 형식의
   exe 재실행은 거부한다. 일반 VCS 조회·실제 사용자 프로그램 실행은 이번 경계의 대상이 아니다.

## 결과와 한계

- 배치 파일 열기 자체가 멈춰도 부모 worker를 취소할 수 있다. 원래의 256KiB 읽기 상한·문자 검사와
  신뢰 전 UNC를 읽지 않는 `cmd_target_is_local` 정책은 바꾸지 않는다.
- 잘못된 보조 조회 응답은 실행으로 이어 가지 않는다. Windows process 종료도 pending I/O의 회수에
  의존하므로 OS 자원의 완전 회수까지 엄격한 시간 상한을 보장하지는 않는다. 이는
  [TerminateProcess 설명](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-terminateprocess)에 따른다.
- 공유 데이터에는 포인터가 없고 보조 모드는 읽기 전용 조회만 한다. 부모가 끝난 뒤의 주소 공간 수명을
  공유하지 않는다. 정상 NTFS 취소와 실제 앱 결과는 검증 기록에서 확인한다.

## 검증

[배치 조회 취소 검증](../verification/2026-11-06-cmd-inspection.md).
