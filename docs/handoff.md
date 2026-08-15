# Gitman 후속 구현 세션 인수인계

## 1. 현재 상태

- 기준일: 2026-08-15
- 완료 단계: 단계 0, 단계 1 구현 및 자동 검증
- 현재 단계: 단계 2 도메인과 설정 저장소
- 현재 체크포인트: `S2-D4-CODE` save/recovery production 구현 및 양 toolchain 검증 완료, 사용자 코드 검수 대기
- 다음 허용 작업: 사용자 승인 전에는 상태 문서 보정과 read-only 검토만 허용
- 실제 구현: CMake, vcpkg manifest, Win32/Skia smoke shell, renderer, custom caption skeleton, embedded Codicons, test와 install 구성
- 단계 2 production 구현: `S2-D1-CODE`, `S2-D2-CODE`, `S2-D3-CODE` 승인 완료, `S2-D4-CODE` 구현 완료 및 검수 대기
- 단계 2 test source 및 fixture: `S2-D1-TEST`, `S2-D2-TEST`, `S2-D3-TEST` 승인 완료
- 기준 문서: `docs/stage-2-plan.md`
- 현재 검증 기록: `docs/verification/2026-08-15-stage-2-d4-code.md`
- 직전 test 검증 기록: `docs/verification/2026-08-15-stage-2-d3-test.md`
- 최근 검증 기록: `docs/verification/2026-08-14-stage-1.md`

다음 작업은 이 문서, 단계 2 구현 계획과 `S2-D4-CODE` 검증 기록을 먼저 읽어야 한다. 현재 production code는 사용자 검수 대기 상태다. 승인되면 `S2-D4-TEST`의 동시 수정 및 write/flush/replace 실패 주입 test만 작성한다.

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
- 단계 2에서는 production code, test code와 bug 수정을 서로 다른 체크포인트로 나눈다.
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

- Git/SVN command 구현
- 단계 2의 저장 및 launch path 계약
- 단계 3 이후의 프로세스 실행, provider, 탐색 및 실제 카드 UI
- thread message API 상세 설계와 구현

따라서 후속 작업은 단계 2 계획의 활성 체크포인트 하나만 진행하고 다시 검수를 요청해야 한다.

## 8. 단계 2 영속 세션 메모리

### 8.1 현재 체크포인트

| 항목 | 상태 |
| --- | --- |
| 계획 ID | `S2-D4-CODE` |
| 제출 내용 | revision token, 원자적 save, backup 및 명시적 recovery production code 구현 완료 |
| production code | project store 계약, JSON load/save pipeline과 Win32 atomic file adapter 구현 완료 |
| test code 및 fixture | 이번 체크포인트에서 변경하지 않음 |
| bug 수정 | `S2-D3-FIX`는 발견 production 결함이 없어 사용자 지시에 따라 생략 완료 |
| 변경 파일 | `src/application/project_store.*`, `src/application/workspace_document_file_system.h`, `src/infrastructure/json_project_store.*`, `src/platform/win32/workspace_document_file_system.*`, `src/CMakeLists.txt` 및 상태 문서 |
| 검증 | VS2022/VS2026 Debug build 및 전체 CTest 각각 41/41, format/style/diff 검사 통과 |
| 승인 대기 | `S2-D4-CODE` 코드 검수 |
| 승인 뒤 다음 작업 | `S2-D4-TEST` save/recovery test 작성만 허용 |

### 8.2 단계 2 진행 원장

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
| `S2-D4-CODE` save/recovery production code | 사용자 검수 대기 | revision token, atomic save, backup 및 explicit recovery 구현, 양 toolchain 전체 41/41 통과 |
| `S2-D4-TEST` save/recovery test | 시작 전 | 앞 체크포인트 승인 필요 |
| `S2-D4-FIX` save/recovery bug 수정 | 시작 전 | 앞 체크포인트 승인 필요 |
| `S2-D5-CODE` launch path production code | 시작 전 | 앞 체크포인트 승인 필요 |
| `S2-D5-TEST` launch path test | 시작 전 | 앞 체크포인트 승인 필요 |
| `S2-D5-FIX` launch contract bug 수정 | 시작 전 | 앞 체크포인트 승인 필요 |
| `S2-V1` 단계 2 최종 검증 | 시작 전 | 전체 체크포인트 승인 필요 |

### 8.3 미해결 또는 보류 사항

- `.verison-list`는 user-owned 작업공간 문서이며 한 프로세스 및 창에서 하나를 활성화한다. 실제 Windows association 등록은 단계 8에 구현한다.
- unknown field 보존, 상대 path 기준, migration과 backup 정책은 계획대로 승인됐다.
- 단계 1의 caption 수동 검수 체크리스트 한 항목은 문서상 미확인 상태이며 단계 2 진행 승인과 별도로 보존한다.
- ADR-004의 범용 메시지 구조 구현 차단 조건은 그대로 유효하다.
