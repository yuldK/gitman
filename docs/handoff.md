# Gitman 후속 구현 세션 인수인계

## 1. 현재 상태

- 기준일: 2026-08-16
- 완료 단계: 단계 0, 단계 1 구현 및 자동 검증, 단계 2 전체, 단계 3 전체 (2026-08-16 사용자 최종 승인)
- 현재 단계: 단계 4 Git 및 SVN provider
- 현재 체크포인트: `S4-D1-CODE` 계약 production code 제출, 사용자 검수 대기
- 감사 결함 수정: 사용자 지시로 단계 4 진행 전에 단계 2·3을 독립 감사하고 발견 사항을 해소했다. 상세는 `docs/verification/2026-08-16-stage-2-3-audit-fix.md`
- 다음 허용 작업: `S4-D1-CODE` 승인 후 `S4-D1-TEST` 한 구간만 수행하고 보고 뒤 중지
- 실제 구현: CMake, vcpkg manifest, Win32/Skia smoke shell, renderer, custom caption skeleton, embedded Codicons, `.verison-list` 도메인 및 JSON 저장소, 범용 프로세스 실행 계층, test와 install 구성
- 기준 문서: `docs/stage-4-plan.md`
- 직전 단계 기준 문서: `docs/stage-3-plan.md`
- 현재 검증 기록: `docs/verification/2026-08-16-stage-4-d1-code.md`
- 직전 검증 기록: `docs/verification/2026-08-16-stage-2-3-audit-fix.md`
- 최근 검증 기록: `docs/verification/2026-08-16-stage-3.md`
- 사용자 진행 방식 지시: 계획, 작업과 테스트의 각 중간 지점에서 진행 내용과 처리 방침을 보고하고 검수를 받는다. 여러 체크포인트를 한 번에 자동 진행하지 않는다. 각 검수 후 사용자가 직접 커밋한다.

다음 작업은 이 문서와 `docs/stage-4-plan.md`를 먼저 읽어야 한다. 단계 4에서는 Git/SVN 도구 발견, 명령 조립, 기계 판독 파서, 공통 snapshot 변환, update와 switch 검증 및 실행만 구현하고 탐색·등록(단계 5), 카드와 로그 UI(단계 6~7), scheduler와 ADR-004 message component는 구현하지 않는다.

## 2. 확정된 기술 기준선

| 항목 | 결정 |
| --- | --- |
| 플랫폼 | Windows 11 x64 Win32 desktop |
| 배포 | Gitman 자체는 단일 `gitman.exe`; Git/SVN CLI는 외부 prerequisite |
| 언어 | C++20, 공백 4칸, `snake_case` |
| 문자열 및 경로 | 로직은 UTF-8 `std::u8string`, Win32 adapter만 UTF-16 변환 |
| 빌드 | CMake 4.2.0 이상, Visual Studio generator, CMake Preset |
| install | `CMAKE_INSTALL_PREFIX=${sourceDir}/bin`, 최종 `${workspaceRoot}/bin/gitman.exe` |
| dependency | vcpkg manifest, `x64-windows-static` |
| Skia | 148, `direct3d`, `harfbuzz`, `icu` |
| renderer | `auto` 기본: Direct3D 우선 및 CPU fallback; `cpu` 명시 선택 |
| 창 | Skia custom caption, Win32 non-client 동작 호환 |
| JSON | nlohmann/json 3.12.0#2 |
| test | Catch2 3.15.3, thread-safe assertions |
| icon | Codicons `v0.0.46-24`, 실행 파일에 embedded |
| 파일 형식 | UTF-8 무 BOM, CRLF |

vcpkg baseline은 `b9a5010d499952121b0f1a40eb98963c37da32dc`, Codicons tag commit은 `abd28d775fc5c40b437b8303807c17a6e63f6d6a`다.

## 3. Git 및 SVN 정책

- Git 최신 상태는 remote-first로 판단한다.
- upstream, `preferred_remote`, `origin`, 유일한 remote 순서로 target을 선택한다.
- remote가 없을 때만 `local_only`, remote는 있으나 비교 branch가 없으면 `remote_target_missing`이다.
- Git update는 선택 remote와 branch를 명시한 `pull --ff-only`다.
- `update_submodules`는 기본 off이며, on이면 dirty 검사 후 recursive update를 parent 카드 작업으로 실행한다.
- Git switch dialog는 remote branch를 먼저, local branch를 다음에 표시한다.
- remote branch에 local tracking branch가 없으면 명시적인 사용자 확인 후 생성한다.
- SVN switch는 JSON `svn_switch_targets` 허용 목록만 사용한다.
- 모든 명령은 비대화형이며 앱은 자격 증명을 저장하지 않는다.

상세 계약은 `docs/decisions/ADR-003-vcs-runtime-policy.md`를 따른다.

## 4. 단계 1 구현 결과

1. [x] CMake 4.2.0 기준 `CMakeLists.txt`와 Visual Studio 2022/2026 preset을 만들었다.
2. [x] `vcpkg.json`에 고정 baseline과 Skia, JSON, Catch2 dependency를 기록했다.
3. [x] `x64-windows-static`과 정적 MSVC runtime으로 단일 exe 배포 기준을 구성했다.
4. [x] 최소 Win32 GUI target과 Skia renderer abstraction의 smoke test 범위를 만들었다.
5. [x] 기본 Direct3D 초기화, 명시적 CPU 선택과 실패 시 CPU fallback을 검증했다.
6. [x] system caption과 겹치지 않는 custom caption, Codicon caption button, 실제 창 명령과 hover feedback을 구현했다. 전체 카드 UI는 단계 6 범위다.
7. [x] Codicons font, mapping과 제3자 license text를 checksum과 함께 준비하고 실행 파일 resource로 embed했다.
8. [x] UTF-8, CRLF, 공백 4칸, `snake_case` 검사와 CTest를 구성했다.
9. [x] CMake install로 `${sourceDir}/bin/gitman.exe`를 만들고 generated tree를 `.gitignore`에 추가했다.
10. [x] 단계 1 검증 기록을 작성하고 사용자 검수를 요청한 뒤 멈춘다.

단계 1에서 Git/SVN provider, JSON 저장소, 실제 카드, 작업 dialog와 범용 thread message component를 구현하지 않는다.

## 5. 필수 검수 게이트

### 매 단계

- 각 단계의 코드, 문서와 자동 및 수동 검증 결과를 제시한다.
- 사용자 승인 전에는 다음 단계로 넘어가지 않는다.
- 단계 2 이후에는 production code, test code와 bug 수정을 서로 다른 체크포인트로 나눈다. 단계 3도 같은 `CODE` / `TEST` / `FIX` 분할을 사용한다.
- 계획 구간, 각 작업 구간과 각 테스트 구간이 끝날 때마다 무엇을 했고 다음에 무엇을 어떻게 처리할지 보고한 뒤 사용자 검수를 기다린다.
- 각 체크포인트 종료 시 이 문서의 진행 원장을 갱신하고 사용자 승인을 기다린다.

### 단계 6 이전의 메시지 구조

ADR-004의 재사용 가능한 메시지 구조는 구현 차단 조건이다. 단계 5 종료 또는 단계 6 시작 시 사용자에게 이 항목을 다시 알리고 다음 자료를 먼저 제시한다.

- 범용 public C++ API 초안
- message 및 envelope type 표
- queue topology와 ordering 및 backpressure 계약
- cancellation, timeout, late result와 shutdown scenario
- sequence diagram과 failure path
- deterministic test 및 sanitizer 전략

사용자가 이 설계를 승인하기 전에는 message queue, dispatcher, thread bridge를 작성하지 않는다.

## 6. 단계 1 검수 체크리스트

- [x] Visual Studio 2022 Debug/Release와 Visual Studio 2026 Debug build 및 CTest가 통과했다.
- [x] VS2022 `/analyze`와 clang-format 및 source style 검사가 통과했다.
- [x] Direct3D, CPU, auto와 강제 CPU fallback smoke test가 통과했다.
- [x] install tree가 `bin/gitman.exe` 단일 파일이며 VC runtime 및 프로젝트 DLL이 없음을 확인했다.
- [ ] 사용자가 custom caption의 실제 표시, Snap Layout, `Alt+Space`, DPI와 고대비 수동 검수 결과를 확인한다.
- [x] 사용자가 단계 2 진행을 지시해 다음 단계 진입을 승인했다.

## 7. 아직 하지 않은 작업

- `S4-P0` 계획 사용자 승인
- Git/SVN 실행 파일 탐색, command 조립과 출력 파싱, update 및 switch (단계 4)
- 탐색 및 등록 (단계 5), 실제 카드 UI와 로그 UI (단계 6~7)
- thread message API 상세 설계와 구현 (단계 6 이전 별도 승인)
- `gitman_process`와 단계 4의 `gitman_vcs`를 실행 파일에 링크하는 조립 작업. 두 library는 test로만 검증되며 exe 링크는 단계 6의 app 조립에서 이뤄진다.

따라서 후속 작업은 `S4-P0` 승인 후 `S4-D1-CODE`부터 시작하며, 이후에도 체크포인트 하나씩 진행하고 다시 검수를 요청해야 한다.

## 8. 단계 4 영속 세션 메모리

### 8.1 현재 체크포인트

| 항목 | 상태 |
| --- | --- |
| 계획 ID | `S4-D1-CODE` |
| 제출 내용 | 도메인 확장, 문서 `settings` 스키마와 저장, provider/registry/probe 계약, 도구 탐색과 버전 비교, 실행 정책, 오류 분류기, `gitman_vcs` target |
| production code | 신규 24개 파일과 기존 7개 파일 수정. 상세는 `docs/verification/2026-08-16-stage-4-d1-code.md` 4장 |
| test code 및 fixture | 없음. 체크포인트 분리 규칙에 따라 `S4-D1-TEST`에서 작성한다. |
| bug 수정 | 없음 |
| 검증 | VS2022 Debug/Release, VS2026 Debug 전체 CTest 각각 139/139, `/analyze` 무경고, aggregate format/style 통과. 임시 프로그램 수동 확인 69/69 |
| 발견 결함 | 없음 |
| 승인 대기 | `S4-D1-CODE` 코드 검수 |
| 승인 뒤 다음 작업 | `S4-D1-TEST` 한 구간만 허용 |

### 8.2 단계 4 진행 원장

| 체크포인트 | 상태 | 비고 |
| --- | --- | --- |
| `S4-P0` 계획 | 승인 완료 | 1차 검수 결정을 반영해 개정. `docs/stage-4-plan.md` 10.1에 확정 사항 기록 |
| `S4-D1-CODE` 계약과 도구 발견 | 제출, 검수 대기 | `docs/verification/2026-08-16-stage-4-d1-code.md` |
| `S4-D1-TEST` | 시작 전 | 승인 전 착수 금지 |
| `S4-D1-FIX` | 시작 전 | |
| `S4-D2-CODE` Git 로컬 상태 | 시작 전 | |
| `S4-D2-TEST` | 시작 전 | |
| `S4-D2-FIX` | 시작 전 | |
| `S4-D3-CODE` Git remote-first 최신 상태 | 시작 전 | |
| `S4-D3-TEST` | 시작 전 | |
| `S4-D3-FIX` | 시작 전 | |
| `S4-D4-CODE` SVN 상태 | 시작 전 | |
| `S4-D4-TEST` | 시작 전 | |
| `S4-D4-FIX` | 시작 전 | |
| `S4-D5-CODE` update | 시작 전 | |
| `S4-D5-TEST` | 시작 전 | |
| `S4-D5-FIX` | 시작 전 | |
| `S4-D6-CODE` switch | 시작 전 | |
| `S4-D6-TEST` | 시작 전 | |
| `S4-D6-FIX` | 시작 전 | |
| `S4-V1` 단계 4 최종 검증 | 시작 전 | |

### 8.3 단계 3 진행 원장 (완료)

| 체크포인트 | 상태 | 비고 |
| --- | --- | --- |
| `S3-P0` 계획 | 승인 완료 | 체크포인트 17개 유지와 활성 code page fallback의 단계 3 포함을 함께 승인 |
| `S3-D1-CODE` 계약 production code | 승인 완료 | `docs/verification/2026-08-16-stage-3-d1-code.md` |
| `S3-D1-TEST` 계약 test | 승인 완료 | test 24개, 양 toolchain 전체 78/78, production 결함 없음 |
| `S3-D1-FIX` 계약 bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S3-D2-CODE` 시작 계약과 출력 수집 production code | 승인 완료 | 1차 검수 지시(wait 실패 정리, 출력 pipe 포함) 반영 후 승인 |
| `S3-D2-TEST` 시작 계약과 출력 test | 승인 완료 | 도우미 target과 test 29개, 양 toolchain 107/107 |
| `S3-D2-FIX` bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S3-D3-CODE` code page fallback production code | 승인 완료 | `docs/verification/2026-08-16-stage-3-d3-code.md` |
| `S3-D3-TEST` fallback test | 승인 완료 | test 11개, 양 toolchain 118/118 |
| `S3-D3-FIX` bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S3-D4-CODE` timeout과 취소 production code | 승인 완료 | `docs/verification/2026-08-16-stage-3-d4-code.md` |
| `S3-D4-TEST` timeout과 취소 test | 승인 완료 | 도우미 명령 3개와 test 7개, 양 toolchain 125/125 |
| `S3-D4-FIX` bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S3-D5-CODE` 마스킹 production code | 승인 완료 | `docs/verification/2026-08-16-stage-3-d5-code.md` |
| `S3-D5-TEST` 마스킹 test | 승인 완료 | test 10개, 양 toolchain 135/135 |
| `S3-D5-FIX` bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S3-V1` 단계 3 최종 검증 | 승인 완료 | 전체 matrix 통과 후 2026-08-16 사용자 최종 승인 |
| 단계 2·3 독립 감사 및 결함 수정 | 완료 | 사용자 지시. `docs/verification/2026-08-16-stage-2-3-audit-fix.md`, 양 toolchain CTest 139/139 |

### 8.4 단계 2 진행 원장 (완료)

| 체크포인트 | 상태 | 비고 |
| --- | --- | --- |
| `S2-P0` 계획 | 승인 완료 | `.verison-list` 작업공간 문서 방식으로 수정 승인 |
| `S2-D1-CODE` 도메인 production code | 승인 완료 | 사용자가 test 작성을 지시함 |
| `S2-D1-TEST` 도메인 test | 승인 완료 | 사용자가 구현 진행을 지시함 |
| `S2-D1-FIX` 도메인 bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S2-D2-CODE` schema/parser production code | 승인 완료 | 사용자가 구현 진행을 지시함 |
| `S2-D2-TEST` schema/parser test | 승인 완료 | 사용자가 구현 진행을 지시함 |
| `S2-D2-FIX` schema/parser bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S2-D3-CODE` path production code | 승인 완료 | 사용자가 작성을 지시함 |
| `S2-D3-TEST` path test | 승인 완료 | test 7개 및 runtime fixture 완료, 양 toolchain 전체 41/41 통과 |
| `S2-D3-FIX` path bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S2-D4-CODE` save/recovery production code | 승인 완료 | 사용자가 다음 작업 진행을 지시함 |
| `S2-D4-TEST` save/recovery test | 승인 완료 | 사용자가 진행을 지시함 |
| `S2-D4-FIX` save/recovery bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S2-D5-CODE` launch path production code | 승인 완료 | 사용자가 계속 진행을 지시함 |
| `S2-D5-TEST` launch path test | 승인 완료 | test 4개, 양 toolchain 전체 54/54 통과, production 결함 없음 |
| `S2-D5-FIX` launch contract bug 수정 | 생략 완료 | 발견 production 결함 없음 |
| `S2-V1` 단계 2 최종 검증 | 승인 완료 | 전체 matrix 통과 후 2026-08-16 사용자 최종 승인 |

### 8.5 미해결 또는 보류 사항

- `.verison-list`는 user-owned 작업공간 문서이며 한 프로세스 및 창에서 하나를 활성화한다. 실제 Windows association 등록은 단계 8에 구현한다.
- unknown field 보존, 상대 path 기준, migration과 backup 정책은 계획대로 승인됐다.
- 기존 `utf8.cpp`, `win32_application.cpp`, `ui_theme.h`의 aggregate clang-format 위반은 `S3-D1-TEST`에서 formatter 결과 수용으로 해소했다. 원인은 수동 줄바꿈 규칙과 `ColumnLimit` 200의 충돌이며, `docs/code_style.md` 2장에 formatter 우선 규칙을 명시했다.
- 단계 1의 caption 수동 검수 체크리스트 한 항목은 문서상 미확인 상태이며 계속 보존한다.
- ADR-004의 범용 메시지 구조 구현 차단 조건은 그대로 유효하다. 단계 3의 reader 스레드는 실행 하나에 종속된 내부 구현이며 이 차단 조건에 해당하지 않는다.
- Git/SVN 실행 파일 탐색과 최소 버전 확인은 ADR-003에 따라 단계 4에서 구현한다. 현재 호스트에 SVN이 없어 단계 3 test는 실제 VCS 대신 전용 콘솔 도우미를 사용한다.
- 단계 3이 미정으로 남긴 명령별 timeout, 캡처 상한과 로캘 강제 여부는 `docs/stage-4-plan.md` 4.3에 값으로 제안했고 `S4-P0` 검수에서 확정한다.
- `S3-D5-CODE`까지 적용한 runner에는 계획에 명시된 미구현 항목이 없다. 남은 구간은 마스킹 test와 단계 3 최종 검증이다.
- 마스킹은 값의 끝을 공백과 줄 끝으로 판정하므로 자격 증명 뒤에 붙은 구두점이 함께 가려질 수 있다. 덜 가리는 것보다 안전한 방향으로 의도한 동작이다.
- job object를 만들거나 배정하지 못하는 환경에서는 warning 진단과 함께 종료 범위가 자식 하나로 줄어든다. 실행 자체를 막지 않는 선택이며 손자 정리는 보장되지 않는다. 감사 수정 이후에는 그런 환경에서도 손자가 pipe를 잡아 `run()`이 무기한 대기하는 일은 없다(drain 유예와 `CancelSynchronousIo` 최후 수단).
- reader join의 drain 유예는 2초 상수다. 단계 4 계획은 이를 바꾸는 대신 모든 Git 명령에 `-c gc.auto=0`을 붙여 background 유지보수 프로세스를 원천 차단하는 방향을 제안했다. `S4-P0` 검수 항목 6번이다.
- 사용자가 SVN CLI를 설치하지 않기로 했다. 단계 4의 SVN 실행 경로는 fixture와 fake runner 검증까지만 보장되며 실제 `svn.exe` 동작은 미검증으로 남는다. 단계 8에서 다시 다룬다.
- SVN은 XML을 쓰지 않는다. `info --show-item`, 비verbose `status`, `svnversion` 조합으로 값을 얻으므로 XML 파서 dependency가 없고 ADR-002와 `vcpkg.json`은 변경하지 않는다.
- 로캘을 강제하지 않기로 했으므로 Git/SVN 오류 메시지가 시스템 언어로 나온다. 오류 분류는 SVN `E<숫자>` 코드, libcurl 및 OpenSSH 원문 문자열, HTTP 상태 번호 같은 로캘 독립 신호만 사용해야 한다. 번역되는 문장으로 분류하면 한국어 환경에서 오분류가 발생한다.
- 모든 Git/SVN 실행은 `active_code_page_fallback` 인코딩 모드를 쓴다. 한국어 Git 출력의 실제 인코딩 실측은 `S4-D3-TEST`에서 기록한다.
- 프로젝트 문서에 `settings`를 추가했다. 스키마 버전은 1을 유지하고 optional이며, `S4-D1-CODE`가 단계 2의 parser와 store를 함께 수정했다. 기존 fixture 6종 회귀 test는 `S4-D1-TEST`에서 추가한다.
- REQ-017을 추가했다. 환경설정 화면 자체는 단계 6~7 범위다.
- `is_absolute_windows_path`가 `application/process_request`에서 `domain/path_syntax`로 옮겨졌다. `process_request.h`가 새 헤더를 include하므로 기존 호출자는 그대로 동작한다.
- `platform/win32/win32_vcs_file_probe.*`는 파일 위치는 계획대로지만 CMake target은 `gitman_win32_platform`이 아니라 `gitman_vcs`다. 단계 3의 `win32_process_runner`와 같은 이유로 계층 방향을 지키기 위한 선택이다.
- 단계 4의 provider는 아직 없다. `S4-D1-CODE`는 계약과 도구 조사까지이며 Git/SVN 명령 조립은 `S4-D2-CODE` 이후다.
- 로컬 NTFS는 속성 조회를 부모 디렉터리 메타데이터로 처리해 deny ACE로도 `GetFileAttributesW`를 실패시킬 수 없다(호스트 실측). `inaccessible` 분기는 `project_path_state_from_error` 매핑 test로 검증한다.
- `gitman_workspace`는 Win32 platform에 링크하지 않으며 경로 해석은 `project_path_resolver` 주입으로만 사용한다. 단계 6 조립 시 `win32::make_project_path_resolver()`를 주입해야 한다.
- Catch2 test 전체에 CTest TIMEOUT 120초가 걸려 있다. 이를 넘는 통합 test는 개별 상향이 필요하다.
- 이 호스트의 활성 code page는 949다. 실행 파일 manifest에 `activeCodePage` 설정이 없으므로 시스템 설정을 따르며, UTF-8 code page 환경에서는 fallback이 무해한 no-op이 된다.
- 사용자 지시로 출력 pipe와 줄 단위 레코드가 `S3-D3`에서 `S3-D2`로 이동했다. 체크포인트 수는 17개를 유지하고 `S3-D3`은 code page fallback transcoder와 파이프라인 단위 test 보강만 담당한다.
- test 자식으로 `cmd.exe`를 사용하면 `CommandLineToArgvW`와 다른 자체 따옴표 처리 때문에 인용 및 인자 검증이 잘못 실패한다. 도우미는 표준 `wmain` argv 실행 파일로 만든다.
