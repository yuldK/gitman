# Gitman 후속 구현 세션 인수인계

## 1. 현재 상태

- 기준일: 2026-08-14
- 완료 단계: 단계 0 결정 사항 확정 및 사용자 검수 의견 반영
- 다음 단계: 단계 1 빌드 및 품질 기준선
- 실제 구현: 시작하지 않음
- source, CMake, vcpkg manifest, asset, test: 아직 생성하지 않음

다음 세션은 이 문서와 `docs/requirements.md`, ADR-001~004, `docs/plan.md`를 먼저 읽어야 한다. 한 번에 여러 단계를 진행하지 않고 단계 1만 구현한 뒤 사용자에게 검수를 요청한다.

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

## 4. 단계 1에서만 수행할 작업

1. CMake 4.2.0 기준 `CMakeLists.txt`와 Visual Studio 2022/2026 preset을 만든다.
2. `vcpkg.json`에 고정 baseline과 Skia, JSON, Catch2 dependency를 기록한다.
3. `x64-windows-static`과 정적 MSVC runtime으로 단일 exe 배포 기준을 구성한다.
4. 최소 Win32 GUI target과 Skia renderer abstraction의 smoke test 범위만 만든다.
5. 기본 Direct3D 초기화, 명시적 CPU 선택과 실패 시 CPU fallback을 검증한다.
6. custom caption의 최소 skeleton과 Win32 adapter 경계를 만든다. 전체 카드 UI는 단계 6 범위다.
7. Codicons font, mapping과 제3자 license text를 checksum과 함께 준비하고 실행 파일 resource로 embed한다.
8. UTF-8, CRLF, 공백 4칸, `snake_case` 검사와 CTest를 구성한다.
9. CMake install rule로 `${sourceDir}/bin/gitman.exe`를 만들고 generated `bin/`과 build tree를 `.gitignore`에 추가한다.
10. 단계 1 검증 기록을 작성하고 사용자 검수를 요청한 뒤 멈춘다.

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

## 6. 다음 세션 시작 체크리스트

- [ ] `docs/handoff.md`와 ADR-001~004를 읽었다.
- [ ] 현재 Git 상태와 사용자 변경을 확인했다.
- [ ] 단계 1 이외의 작업을 하지 않기로 범위를 고정했다.
- [ ] dependency 다운로드와 장시간 Skia build가 필요할 수 있음을 사용자에게 알렸다.
- [ ] 단계 1 완료 후 검수 요청을 남기고 중단한다.

## 7. 이번 세션에서 하지 않은 작업

- dependency 설치 또는 다운로드
- CMake configure, build, test, install
- Codicons asset 취득
- Win32 또는 Skia 코드 작성
- Git/SVN command 구현
- thread message API 상세 설계와 구현

따라서 후속 세션은 문서에 적힌 결정과 실제 toolchain 및 dependency의 호환성을 단계 1에서 처음 검증해야 한다.
