# Gitman 후속 구현 세션 인수인계

## 1. 현재 상태

- 기준일: 2026-08-16
- 완료 단계: 단계 0, 단계 1 구현 및 자동 검증, 단계 2 전체 (2026-08-16 사용자 최종 승인)
- 현재 단계: 단계 3 프로세스 실행 계층
- 현재 체크포인트: `S3-P0` 계획 작성 완료, 계획 사용자 검수 대기
- 다음 허용 작업: 계획 승인 후 `S3-D1-CODE` 하나만 수행하고 다시 보고
- 실제 구현: CMake, vcpkg manifest, Win32/Skia smoke shell, renderer, custom caption skeleton, embedded Codicons, `.verison-list` 도메인 및 JSON 저장소, test와 install 구성
- 기준 문서: `docs/stage-3-plan.md`
- 직전 단계 기준 문서: `docs/stage-2-plan.md`
- 최근 검증 기록: `docs/verification/2026-08-16-stage-2.md`
- 사용자 진행 방식 지시: 계획, 작업과 테스트의 각 중간 지점에서 진행 내용과 처리 방침을 보고하고 검수를 받는다. 단계 2처럼 여러 체크포인트를 한 번에 자동 진행하지 않는다.

다음 작업은 이 문서와 `docs/stage-3-plan.md`를 먼저 읽어야 한다. 단계 3에서는 범용 외부 프로세스 실행 계층만 구현하고 Git/SVN 명령 지식, scheduler, 로그 UI와 ADR-004 message component는 구현하지 않는다.

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

- 단계 3 프로세스 실행 계층 구현 (`S3-P0` 계획 승인 대기)
- Git/SVN command 조립과 출력 파싱 (단계 4)
- 탐색 및 등록 (단계 5), 실제 카드 UI와 로그 UI (단계 6~7)
- thread message API 상세 설계와 구현 (단계 6 이전 별도 승인)

따라서 후속 작업은 단계 3 계획의 활성 체크포인트 하나만 진행하고 다시 검수를 요청해야 한다.

## 8. 단계 3 영속 세션 메모리

### 8.1 현재 체크포인트

| 항목 | 상태 |
| --- | --- |
| 계획 ID | `S3-P0` |
| 제출 내용 | `docs/stage-3-plan.md` 구현 계획과 설계 제안, 단계 2 승인 상태 문서 반영 |
| production code | 없음. 계획 승인 전에는 `src/` 아래 프로세스 관련 source를 추가하지 않는다. |
| test code 및 fixture | 없음. 테스트 도우미 실행 파일은 `S3-D2-TEST`에서만 추가한다. |
| 검증 | 문서 변경만 있으므로 UTF-8/CRLF 및 source style 검사로 확인 |
| 승인 대기 | `S3-P0` 계획 검수 (특히 계획 10장의 9개 항목) |
| 승인 뒤 다음 작업 | `S3-D1-CODE` 프로세스 값 model, 요청 검증, runner/sink 계약과 취소 primitive만 허용 |

### 8.2 단계 3 진행 원장

| 체크포인트 | 상태 | 비고 |
| --- | --- | --- |
| `S3-P0` 계획 | 검수 대기 | 동기 실행 API, job object 취소, 줄 단위 레코드, 마스킹 규칙과 테스트 도우미 target 승인 필요 |
| `S3-D1-CODE` ~ `S3-D5-FIX` | 시작 전 | 계획 7장 순서대로 하나씩 진행 |
| `S3-V1` 단계 3 최종 검증 | 시작 전 | 전체 build/test/analyze/install과 동시 실행 stress |

### 8.3 단계 2 진행 원장 (완료)

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

### 8.4 미해결 또는 보류 사항

- `.verison-list`는 user-owned 작업공간 문서이며 한 프로세스 및 창에서 하나를 활성화한다. 실제 Windows association 등록은 단계 8에 구현한다.
- unknown field 보존, 상대 path 기준, migration과 backup 정책은 계획대로 승인됐다.
- 기존 `utf8.cpp`, `win32_application.cpp`, `ui_theme.h`의 aggregate clang-format 기준선 위반은 `S2-V1`에서 프로젝트 formatter로만 정렬해 해소했다. 이후 모든 build, test, 분석과 aggregate format/style 검사를 다시 통과시켰다.
- 단계 1의 caption 수동 검수 체크리스트 한 항목은 문서상 미확인 상태이며 계속 보존한다.
- ADR-004의 범용 메시지 구조 구현 차단 조건은 그대로 유효하다. 단계 3의 reader 스레드는 실행 하나에 종속된 내부 구현이며 이 차단 조건에 해당하지 않는다.
- Git/SVN 실행 파일 탐색과 최소 버전 확인은 ADR-003에 따라 단계 4에서 구현한다. 현재 호스트에 SVN이 없어 단계 3 test는 실제 VCS 대신 전용 콘솔 도우미를 사용한다.
- 단계 3의 기본 timeout, 기본 캡처 상한과 로캘 강제 여부는 명령별로 결정할 사항이므로 단계 4까지 미정으로 둔다.
