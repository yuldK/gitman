# Gitman 후속 구현 세션 인수인계

## 1. 현재 상태

- 기준일: 2026-08-14
- 완료 단계: 단계 0 결정 사항 확정 및 사용자 검수 의견 반영
- 현재 단계: 단계 1 구현 및 자동 검증 완료, 사용자 검수 대기
- 다음 단계: 단계 2 도메인과 설정 저장소, 단계 1 승인 전 시작 금지
- 실제 구현: CMake, vcpkg manifest, Win32/Skia smoke shell, renderer, custom caption skeleton, embedded Codicons, test와 install 구성
- 검증 기록: `docs/verification/2026-08-14-stage-1.md`

다음 작업은 이 문서와 단계 1 검증 기록을 먼저 읽어야 한다. 사용자가 단계 1을 승인하기 전에는 단계 2 구현을 시작하지 않는다.

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
- [ ] 사용자가 단계 1을 승인한다.

## 7. 아직 하지 않은 작업

- Git/SVN command 구현
- 단계 2의 도메인 모델 및 JSON 설정 저장소
- 단계 3 이후의 프로세스 실행, provider, 탐색 및 실제 카드 UI
- thread message API 상세 설계와 구현

따라서 후속 작업은 단계 1 사용자 승인 뒤 단계 2만 진행하고 다시 검수를 요청해야 한다.
