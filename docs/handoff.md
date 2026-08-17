# Gitman 후속 구현 세션 인수인계

## 1. 현재 상태

- 기준일: 2026-08-17
- 완료 단계: 단계 0, 단계 1 구현 및 자동 검증, 단계 2 전체, 단계 3 전체 (2026-08-16 사용자 최종 승인), 단계 4 전체 (2026-08-17 사용자가 `S5-P0` 진행을 지시하며 최종 승인)
- 현재 단계: 단계 5 탐색과 등록
- 현재 체크포인트: `S5-D2` 탐색 실행 완료. 2026-08-17 사용자 지시로 **단계 5 종료(`S5-V1`)까지 자동 진행하며 체크포인트마다 커밋**한다
- 감사 결함 수정: 사용자 지시로 단계 4 진행 전에 단계 2·3을 독립 감사하고 발견 사항을 해소했다. 상세는 `docs/verification/2026-08-16-stage-2-3-audit-fix.md`
- 다음 허용 작업: `S5-D3` 선택 등록, 이어서 `S5-V1` 최종 검증까지. 단계 5 전체에 대한 사용자 검수는 `S5-V1` 보고에서 받는다
- 실제 구현: CMake, vcpkg manifest, Win32/Skia smoke shell, renderer, custom caption skeleton, embedded Codicons, `.verison-list` 도메인 및 JSON 저장소, 범용 프로세스 실행 계층, Git/SVN 도구 발견과 조회·update·switch 전체, 탐색 도메인 모델과 표식 판정 및 깊이 1 열거, test와 install 구성
- 기준 문서: `docs/stage-5-plan.md`
- 직전 단계 기준 문서: `docs/stage-4-plan.md`
- 현재 검증 기록: `docs/verification/2026-08-17-stage-5-d1.md`
- 직전 검증 기록: `docs/verification/2026-08-17-stage-4.md` (단계 4 최종)
- 사용자 진행 방식 지시: 계획, 작업과 테스트의 각 중간 지점에서 진행 내용과 처리 방침을 보고하고 검수를 받는다. 여러 체크포인트를 한 번에 자동 진행하지 않는다. 각 검수 후 사용자가 직접 커밋한다. **단계 5부터 production code와 test code 작성은 한 검수 구간에서 함께 진행한다** (2026-08-17 지시).

다음 작업은 이 문서와 `docs/stage-5-plan.md`를 먼저 읽어야 한다. 단계 5에서는 깊이 1 자식 탐색, 표식 기반 저장소 판정, 링크·중복 정책, 후보 미리보기 데이터, 선택 등록과 저장 충돌 감지만 구현하고 미리보기 dialog UI(단계 6~7), 카드와 로그 UI(단계 6~7), scheduler와 ADR-004 message component는 구현하지 않는다. 탐색은 프로세스를 만들지 않는다.

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

- 탐색 및 등록 구현 (단계 5, `S5-P0` 계획 검수 대기), 미리보기 dialog와 실제 카드 UI 및 로그 UI (단계 6~7)
- thread message API 상세 설계와 구현 (단계 6 이전 별도 승인)
- `gitman_process`와 `gitman_vcs`를 실행 파일에 링크하는 조립 작업. 두 library는 test로만 검증되며 exe 링크는 단계 6의 app 조립에서 이뤄진다.
- 실제 `svn.exe` 실행 경로 검증 (단계 8). 단계 4는 명령 조립, 파서와 검증 규칙까지만 보장한다.
- 실제 네트워크 원격과 인증이 필요한 Git 동작 검증 (단계 8). 단계 4의 통합 test는 로컬 경로 원격만 사용한다.

따라서 후속 작업은 `S5-P0` 계획 승인 후 `S5-D1-CODE`부터 시작하며, 이후에도 체크포인트 하나씩 진행하고 다시 검수를 요청해야 한다.

## 8. 단계 5 영속 세션 메모리

### 8.1 현재 체크포인트

| 항목 | 상태 |
| --- | --- |
| 계획 ID | `S5-D2` |
| 제출 내용 | `discovery_service` 탐색 실행(루트 검증, 열거, 판정 적용, 중복 표시, 취소, 진단)과 fake 시나리오 11개·실제 환경 통합 5개 |
| production code | `application/discovery_service.*`, `domain/diagnostic.*`(탐색 진단 code 3종), `src/CMakeLists.txt` |
| test code 및 fixture | test 16개, `scoped_scan_directory` 공용 도우미. 전체 CTest 412 → **428** |
| bug 수정 | production 결함 없음. style 위반 1건(여러 줄 중괄호 초기화)을 같은 구간에서 해소 |
| 검증 | VS2022 Debug/Release, VS2026 Debug 각각 428/428, Debug 3회 반복 통과, `/analyze` 무경고, format/style 통과 |
| 발견 결함 | 없음 |
| 승인 대기 | 사용자 위임으로 `S5-V1`까지 자동 진행 중. 최종 검수는 단계 5 전체에 대해 수행 |
| 승인 뒤 다음 작업 | `S5-D3` |

### 8.2 단계 5 진행 원장

| 체크포인트 | 상태 | 비고 |
| --- | --- | --- |
| `S5-P0` 계획 | 승인 완료 | 10.1의 확정 필요 사항 8개와 10.2의 5개를 제안대로 확정. 코드·test 통합 진행 지시를 반영해 체크포인트를 5개로 개정 |
| `S5-D1` 계약과 판정, 열거 | 완료, 커밋됨 | test 19개, 전체 412/412. 2026-08-17 사용자가 커밋과 단계 5 종료까지의 자동 진행을 지시. `docs/verification/2026-08-17-stage-5-d1.md` |
| `S5-D2` 탐색 실행 | 완료 | test 16개, 전체 428/428. junction 포함 실제 환경 검증. `docs/verification/2026-08-17-stage-5-d2.md` |

### 8.3 단계 4 진행 원장 (완료)

| 체크포인트 | 상태 | 비고 |
| --- | --- | --- |
| `S4-P0` 계획 | 승인 완료 | 1차 검수 결정을 반영해 개정. `docs/stage-4-plan.md` 10.1에 확정 사항 기록 |
| `S4-D1-CODE` 계약과 도구 발견 | 승인 완료 | `docs/verification/2026-08-16-stage-4-d1-code.md` |
| `S4-D1-TEST` | 승인 완료 | test 56개, 전체 195/195. `docs/verification/2026-08-16-stage-4-d1-test.md` |
| `S4-D1-FIX` | 생략 완료 | 발견 production 결함 없음 |
| `S4-D2-CODE` Git 로컬 상태 | 승인 완료 | `docs/verification/2026-08-16-stage-4-d2-code.md` |
| `S4-D2-TEST` | 승인 완료 | test 51개, 전체 246/246. `docs/verification/2026-08-16-stage-4-d2-test.md` |
| `S4-D2-FIX` | 생략 완료 | 발견 production 결함 없음 |
| `S4-D3-CODE` Git remote-first 최신 상태 | 승인 완료 | `docs/verification/2026-08-16-stage-4-d3-code.md` |
| `S4-D3-TEST` | 승인 완료 | test 28개, 전체 274/274. `docs/verification/2026-08-16-stage-4-d3-test.md` |
| `S4-D3-FIX` | 생략 완료 | 발견 production 결함 없음 |
| `S4-D4-CODE` SVN 상태 | 승인 완료 | `docs/verification/2026-08-17-stage-4-d4-code.md` |
| `S4-D4-TEST` | 승인 완료 | test 32개, 전체 306/306. `docs/verification/2026-08-17-stage-4-d4-test.md` |
| `S4-D4-FIX` | 생략 완료 | 발견 production 결함 없음 |
| `S4-D5-CODE` update | 승인 완료 | `docs/verification/2026-08-17-stage-4-d5-code.md` |
| `S4-D5-TEST` | 승인 완료 | test 32개, 전체 338/338. `docs/verification/2026-08-17-stage-4-d5-test.md` |
| `S4-D5-FIX` | 생략 완료 | 발견 production 결함 없음 |
| `S4-D6-CODE` switch | 승인 완료 | 신규 검증 서비스와 두 provider의 switch. `docs/verification/2026-08-17-stage-4-d6-code.md` |
| `S4-D6-TEST` | 승인 완료 | test 55개, 전체 393/393. `docs/verification/2026-08-17-stage-4-d6-test.md` |
| `S4-D6-FIX` | 생략 완료 | 발견 production 결함 없음 |
| `S4-V1` 단계 4 최종 검증 | 승인 완료 | 전체 matrix와 동시 조회 stress 통과. 2026-08-17 사용자가 `S5-P0` 진행을 지시하며 단계 4를 최종 승인. `docs/verification/2026-08-17-stage-4.md` |

### 8.4 단계 3 진행 원장 (완료)

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

### 8.5 단계 2 진행 원장 (완료)

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

### 8.6 미해결 또는 보류 사항

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
- 2026-08-17 사용자 방침: **SVN은 개발 시점에 쓰지 않으며 나중에 프로덕션에 부품 끼워넣듯 최소 노력으로 적용할 수 있기만 하면 된다.** 따라서 SVN 쪽은 Git provider와 같은 구조를 유지하고 계획 4.6의 명령 조합에서 벗어나지 않는 선까지만 구현한다.
- `svnversion`은 `svn`과 다른 실행 파일이라 `--non-interactive`를 받지 않는다. SVN 공통 인자를 붙이면 인자 오류로 실패하므로 이 명령만 요청을 직접 만든다.
- `svn status`의 경로는 앞 7칸(항목·속성·잠금·이력·switched·잠금 토큰·tree conflict)을 상태 칸으로 보고 그 뒤 공백을 모두 건너뛴 지점부터 읽는다. 계획의 "고정 9칸"보다 배포판별 패딩 차이에 강하다. switched는 색인 4, tree conflict는 색인 6이다.
- SVN 원격 조회는 현재 URL을 `info --show-item url`로 다시 물어본 뒤 원격 HEAD 리비전과 비교한다. `ahead`와 `diverged`는 SVN에 없어 `behind`와 `up_to_date`만 나오고 `ahead_count`는 항상 0이다.
- 실제 SVN 환경에 붙일 때 확인할 것은 네 가지다. 실제 실행 경로 전부, `status`의 실제 칸 패딩, 원격 인증·연결 실패 메시지의 오류 코드, `info --show-item`이 값 외의 줄을 내는 배포판 여부다. 어긋나면 파서 한 곳을 고친 뒤 `tests/fixtures/vcs/svn/`의 fixture를 실제 출력으로 교체하면 된다.
- SVN fixture는 실제 출력을 캡처할 수 없어 Apache Subversion 공식 문서의 출력 계약을 근거로 작성했다. 출처와 "실제 출력과 대조하지 않았다"는 사실을 파일 안 `#` 주석에 남겼고 test 도우미가 그 줄을 버린다. SVN은 상태 줄을 `#`로 시작하지 않는다.
- SVN 통합 test 2개는 서로 배타적이다. 이 호스트에서는 "SVN이 없어도 앱이 동작한다"가 실행되고 실제 `svn.exe` test는 skip된다. SVN이 설치된 호스트에서는 반대가 된다. 실제 작업 복사본을 만들려면 `svnadmin`이 필요해 단계 8로 남겼다.
- SVN은 XML을 쓰지 않는다. `info --show-item`, 비verbose `status`, `svnversion` 조합으로 값을 얻으므로 XML 파서 dependency가 없고 ADR-002와 `vcpkg.json`은 변경하지 않는다.
- 로캘을 강제하지 않기로 했으므로 Git/SVN 오류 메시지가 시스템 언어로 나온다. 오류 분류는 SVN `E<숫자>` 코드, libcurl 및 OpenSSH 원문 문자열, HTTP 상태 번호 같은 로캘 독립 신호만 사용해야 한다. 번역되는 문장으로 분류하면 한국어 환경에서 오분류가 발생한다.
- 모든 Git/SVN 실행은 `active_code_page_fallback` 인코딩 모드를 쓴다. `S4-D3-TEST`에서 실측한 결과, 이 호스트의 시스템 ANSI code page는 949지만 Git for Windows 2.52.0에 번역 catalog가 설치되어 있지 않아(`share/locale` 부재) `LANGUAGE`·`LC_ALL`·`LANG`을 어떻게 줘도 메시지가 영어다. Git이 되돌려 주는 비ASCII 내용은 UTF-8이라 fallback이 건드리지 않는다. 다른 호스트에는 번역본이 있을 수 있으므로 오류 분류는 계속 로캘 독립 신호만 사용한다.
- 오류 분류의 HTTP 상태 신호는 같은 텍스트에 `http`가 함께 있어야 동작한다. `S4-D1-CODE`가 `branch 404` 같은 출력의 오탐을 막으려고 넣은 조건이므로, URL을 뺀 인위적인 문장으로 test를 만들면 `error`로 분류된다. test 데이터는 실제 Git 메시지 형태를 써야 한다.
- 프로젝트 문서에 `settings`를 추가했다. 스키마 버전은 1을 유지하고 optional이며, `S4-D1-CODE`가 단계 2의 parser와 store를 함께 수정했다. 기존 fixture 6종 회귀 test는 `S4-D1-TEST`에서 추가한다.
- REQ-017을 추가했다. 환경설정 화면 자체는 단계 6~7 범위다.
- `is_absolute_windows_path`가 `application/process_request`에서 `domain/path_syntax`로 옮겨졌다. `process_request.h`가 새 헤더를 include하므로 기존 호출자는 그대로 동작한다.
- `platform/win32/win32_vcs_file_probe.*`는 파일 위치는 계획대로지만 CMake target은 `gitman_win32_platform`이 아니라 `gitman_vcs`다. 단계 3의 `win32_process_runner`와 같은 이유로 계층 방향을 지키기 위한 선택이다.
- `S4-D6-CODE`로 두 provider의 `repository_provider` 계약이 모두 채워졌다. Git은 로컬 조회(`S4-D2`), 원격 판정(`S4-D3`), update(`S4-D5`), switch(`S4-D6`)를, SVN은 조회(`S4-D4`), update(`S4-D5`), switch(`S4-D6`)를 구현했다.
- switch 검증 규칙은 `application/switch_validation_service`에 순수 함수로 모여 있다. provider는 승인 결과를 받은 뒤에만 전환 명령을 만든다. REQ-007 수용 기준을 한 곳에서만 지키면 되게 한 분리다.
- `switch_candidate::tracking_branch_confirmed`는 dialog가 사용자 확인을 받은 뒤에만 켜는 값이다. 후보 조회는 채우지 않는다. 이 값이 꺼져 있으면 tracking branch를 만들지 않고 `tracking_branch_confirmation_required`로 되돌려 보낸다. 단계 6~7의 dialog가 이 계약을 지켜야 한다.
- Git 후보 목록에서 **remote 후보로 도달할 수 있는 local branch는 중복해 넣지 않는다.** upstream이 그 remote와 다른 local branch만 local 후보로 남는다. 계획 4.8의 "local-only"를 그대로 읽으면 그런 branch로 전환할 방법이 사라진다.
- 후보를 새로 고칠 remote는 `preferred_remote` → `origin` → 유일한 remote 순서로만 고른다. 좁혀지지 않으면 fetch하지 않고 목록을 `stale`로 표시한다. upstream은 현재 branch에 종속된 값이라 쓰지 않는다.
- `switch_to`는 실행 직전에 fetch하지 않는다. 이미 받아 둔 ref로만 전환하며 `--no-guess`가 목록에 없던 대상으로의 암묵 전환을 막는다.
- SVN 후보 조회는 process request를 하나도 만들지 않는다. 후보가 문서의 `svn_switch_targets`뿐이기 때문이다.
- SVN 전환 검증은 허용 목록·형식·현재 위치·작업 트리를 네트워크보다 먼저 본다. 저장소 root와 UUID는 양쪽 값이 모두 있고 같을 때만 통과시킨다.
- `git switch`는 `--`를 받아들이고, `--track` 뒤의 완전한 ref는 옵션 값이 아니라 시작 지점으로 해석된다. 호스트 Git 2.52.0 실측이다. `--no-guess`는 목록에 없는 이름을 `fatal: invalid reference`로 실패시킨다.
- update는 사전 검사를 위해 **스스로 로컬 조회를 수행한다.** 계약에 snapshot 인자가 없고 오래된 값으로 보호 정책을 판단하면 안 되기 때문이다. 정상 경로의 명령 수는 Git이 6개(조회 2 + remote + pull + 재조회 2), submodule 옵션이 켜지면 8개다.
- update 차단 사유의 우선순위는 도구 부재 → 저장소 아님 → 충돌 → 진행 중 작업 → `index.lock` → detached → dirty → diverged → 대상 없음이다. `working_tree_state::unknown`은 dirty와 함께 막는다.
- SVN의 `switched_subtree`와 `mixed_revision`은 값이 있을 때만 차단한다. `svnversion`이 없어 판정할 수 없다는 이유로 update를 영영 막지 않는 선택이며, 더 안전한 쪽을 원하면 `value_or(false)`를 `value_or(true)`로 바꾸면 된다.
- submodule 사전 검사는 `git submodule status`가 보고하는 충돌(`U`)과 커밋 불일치(`+`)만 본다. 이 명령은 submodule 내부의 dirty를 보고하지 않으므로 계획 4.7의 "dirty 검사"는 범위에서 빠졌다. 단계 6~7에서 다시 본다.
- `submodule update --init --recursive`는 parent pull이 성공한 경우에만 실행한다. 실패한 pull 뒤에 submodule을 옮기면 되돌리기 어려운 조합이 남는다.
- `repository_change_result`에는 `has_errors()`가 없다. `repository_query_result`에만 있으며, 실행 결과는 `executed`와 `succeeded`로 성패를 나타낸다. 단계 6에서 카드가 두 결과를 함께 다룰 때 다시 볼 비대칭이다.
- Git은 기본 설정에서 submodule을 `file` 경로에서 받아 오지 않는다. 통합 test는 준비 단계에서만 `-c protocol.file.allow=always`를 쓰고 `clone --recurse-submodules`로 미리 초기화한다. **production 명령은 이 설정을 만들지 않는다.**
- `svnversion` 요청에는 인자가 하나도 없다. test에서 인자를 확인할 때 `arguments.back()`을 쓰면 빈 vector에 접근한다.
- `rev-parse`에는 `--`를 쓰면 안 된다. 뒤의 값을 경로로 해석해 ref 확인이 항상 실패한다. `fetch`는 `--`를 받아들이며 remote 이름을 옵션으로 오해하지 않게 붙여 두었다. 호스트 Git 2.52.0 실측이다.
- remote branch 존재 확인은 `fetch` **뒤에** 한다. 한 번도 fetch하지 않은 저장소에는 tracking ref가 없어 앞에서 확인하면 원격에 있는 branch를 없다고 오판한다. 계획 4.5의 4·5번 순서를 바꾼 것이며 판정 결과는 같다.
- `query_remote`는 로컬 상태를 다시 만들지 않는다. 받은 snapshot을 복사해 원격 값만 덮어쓰므로 fetch가 실패해도 작업 트리 상태, 마지막 성공 `remote_checked_at`, 직전 로컬 비교가 남는다. 반대로 `local_only`와 `remote_target_missing`으로 판정한 경우에는 이전 비교 값을 지운다.
- detached HEAD와 remote가 없는 저장소, 대상이 모호한 저장소에서는 fetch하지 않는다. 네트워크를 쓰기 전에 판정이 끝난다.
- 커밋이 없는 저장소는 `HEAD`가 없어 `rev-list`로 대칭 차이를 구할 수 없다. fetch와 ref 확인까지만 하고 `sync_state`는 `unknown`이다.
- `status --porcelain=v2`에 `-z`를 쓰지 않기로 확정했다. 단계 3 파이프라인이 줄 끝 문자를 남기지 않아 NUL 구분 출력은 경계 정보를 잃고, 개행이 든 경로가 오히려 손상된다. 줄 단위 출력에서는 Git이 그런 경로를 C 인용으로 감싸며 `unquote_git_path`가 해제한다. 계획 4.4와 4.10의 `-z` 서술은 이 결정으로 대체됐다.
- `status` 명령만 레코드 상한 64 KiB를 쓴다. rename 레코드는 한 줄에 경로 두 개를 담아 기본 8 KiB를 넘길 수 있고, 끊긴 줄은 파서가 다른 레코드로 오해한다.
- bare 저장소와 git dir 안을 가리키는 등록 경로는 `repository_availability::unsupported_layout`이다. 지원 범위 자체는 단계 5에서 정한다. linked worktree는 추가 처리 없이 조회된다.
- 저장소 아님 판정은 "정상 종료했는데 `rev-parse` 출력이 없다"는 구조적 신호로 한다. 번역되는 `fatal: not a git repository` 문장에 의존하지 않는다.
- 로컬 조회는 `branch.ab`로 `sync_state`를 채우되 근거를 `comparison_source::local`로 남긴다. 이미 받아 둔 remote tracking ref와의 비교이며 `S4-D3`의 remote-first 판정이 덮어쓴다. `local_only`와 `remote_target_missing`은 `git remote` 결과가 필요해 `S4-D3` 범위다.
- 작업 트리 상태는 해석하지 못한 레코드가 있거나 branch 헤더가 없으면 `unknown`이다. `is_safe_for_change()`가 `unknown`을 안전으로 보지 않으므로 보호 정책이 그대로 동작한다.
- provider는 명령을 만들기 전에 등록 경로가 절대 경로인지와 디렉터리인지 확인한다. 따라서 test 도우미의 file probe에는 표식 파일뿐 아니라 **작업 디렉터리도 등록**해야 한다.
- `--untracked-files=normal`은 미추적 디렉터리를 항목 하나로 접어 보고한다(`? sub dir/`). `untracked_count`는 파일 수가 아니라 Git이 보고한 항목 수다. 카드 표시 방식은 단계 6에서 정한다.
- `tests/helpers/git_repository_fixture.*`는 실제 `git.exe`로 임시 저장소를 만든다. `HOME`, `USERPROFILE`, `XDG_CONFIG_HOME`, `GIT_CONFIG_GLOBAL`, `GIT_CONFIG_NOSYSTEM`과 커밋 저자·시각을 고정해 호스트 설정과 분리한다. 이렇게 하지 않으면 사용자의 `core.autocrlf`나 서명 설정에 따라 결과가 달라진다. 단계 4의 남은 통합 test도 이 도우미를 쓴다.
- `tests/fixtures/vcs/git/*.txt`는 실제 Git 출력을 그대로 저장한 것이며 `.txt`는 style 검사 확장자 목록에 없다. porcelain v2 rename 레코드가 TAB을 포함하므로 tab 금지 검사를 적용하면 안 된다. SVN fixture도 같은 위치에 둔다.
- test source의 여러 줄 표현식은 `gitman_source_style`의 "닫는 중괄호는 자기 줄에" 규칙과 clang-format의 200자 줄 합치기가 충돌할 수 있다. 중간 값을 지역 변수로 빼면 두 검사를 모두 만족한다.
- test 도우미는 계획의 `helpers/fake_process_runner.*` 대신 `helpers/vcs_test_doubles.*`다. runner 대역과 file probe 대역을 함께 담기 때문이다. `gitman_tests`에 `${GITMAN_TEST_DIRECTORY}` include 경로가 추가됐다.
- `fake_process_runner`는 받은 요청을 그대로 기록한다. 이후 구간에서 "검증 실패 시 명령을 만들지 않는다"는 REQ-007 수용 기준을 이 기록으로 단정한다.
- 로컬 NTFS는 속성 조회를 부모 디렉터리 메타데이터로 처리해 deny ACE로도 `GetFileAttributesW`를 실패시킬 수 없다(호스트 실측). `inaccessible` 분기는 `project_path_state_from_error` 매핑 test로 검증한다.
- `gitman_workspace`는 Win32 platform에 링크하지 않으며 경로 해석은 `project_path_resolver` 주입으로만 사용한다. 단계 6 조립 시 `win32::make_project_path_resolver()`를 주입해야 한다.
- Catch2 test 전체에 CTest TIMEOUT 120초가 걸려 있다. 이를 넘는 통합 test는 개별 상향이 필요하다.
- 이 호스트의 활성 code page는 949다. 실행 파일 manifest에 `activeCodePage` 설정이 없으므로 시스템 설정을 따르며, UTF-8 code page 환경에서는 fallback이 무해한 no-op이 된다.
- 사용자 지시로 출력 pipe와 줄 단위 레코드가 `S3-D3`에서 `S3-D2`로 이동했다. 체크포인트 수는 17개를 유지하고 `S3-D3`은 code page fallback transcoder와 파이프라인 단위 test 보강만 담당한다.
- test 자식으로 `cmd.exe`를 사용하면 `CommandLineToArgvW`와 다른 자체 따옴표 처리 때문에 인용 및 인자 검증이 잘못 실패한다. 도우미는 표준 `wmain` argv 실행 파일로 만든다.
