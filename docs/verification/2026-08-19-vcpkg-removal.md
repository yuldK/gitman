# 검증 기록 - vcpkg 제거와 submodule 전환 (`N1`~`N5`)

## 1. 개요

- 날짜: 2026-08-19
- 대상: [ADR-006](../decisions/ADR-006-restricted-network-dependency-provisioning.md)의 구현. vcpkg를 제거하고 의존성을 submodule과 사용자가 빌드한 Skia로 대체한다
- 선행: [Skia 수동 빌드 실측](2026-08-19-skia-manual-build.md)에서 external 목록과 GN args를 확정했다
- 환경: Windows 11 Pro, VS2022 (MSVC 14.44.35207), Windows SDK 10.0.26100.0

## 2. 결과 요약

| 항목 | 결과 |
| --- | --- |
| `cmake --preset vs2022` configure | **성공** (vcpkg 없이 3.1초) |
| Release 빌드와 install | **성공**, `bin/gitman.exe` 6,510,080 byte |
| smoke test (`cpu`, `direct3d`, `auto`) | **3종 모두 통과** |
| Debug 빌드 | **성공** |
| `cmake --preset vs2022-tests` configure | **성공** |
| CTest Debug | **585/585 통과** |
| CTest Release (ASan 17개 실계측 포함) | **585/585 통과** |
| `check_source_style.ps1` | 350 파일 통과 |
| `verify_skia_root.ps1` | Debug·Release 전 항목 통과 |
| VS2026 Release 빌드와 smoke 3종 | **통과** |
| VS2026 CTest Debug | **585/585 통과** |
| `/analyze` (`vs2022-analysis`) | **무경고** |
| 렌더링 픽셀 비교 (변경 전 기준선 대비) | **409,440 px 전부 일치, 차이 0** |

실행 파일 크기는 vcpkg 시절 7,185,920 byte에서 **6,510,080 byte**로 675,840 byte 줄었다. 사용하지 않는 Skia 기능을 끈 결과다.

## 3. 변경 내용

### 3.1 삭제

- `vcpkg.json`
- `cmake/vcpkg_toolchain.cmake`
- `cmake/triplets/` (vs2022, vs2026 overlay triplet)
- `CMakePresets.json`의 `toolchainFile`, `VCPKG_HOST_TRIPLET`, `VCPKG_TARGET_TRIPLET`, `VCPKG_OVERLAY_TRIPLETS`
- `CMakeLists.txt`의 `VCPKG_TARGET_TRIPLET` 검증 블록과 `find_package` 세 개

preset의 이름과 개수는 그대로 두었다. 사용자가 익힌 명령이 바뀌지 않는다.

### 3.2 신설

| 파일 | 역할 |
| --- | --- |
| `cmake/dependencies.cmake` | 의존성 구성 진입점. `gitman_find_dependencies(BUILD_TESTS ...)` |
| `cmake/dependencies/skia.cmake` | 사용자가 빌드한 산출물을 검사해 `unofficial::skia::skia` imported target 합성 |
| `cmake/dependencies/nlohmann_json.cmake` | submodule `add_subdirectory` |
| `cmake/dependencies/catch2.cmake` | submodule `add_subdirectory`, test 구성에서만 호출 |
| `scripts/build_skia.ps1` | 사용자가 손으로 실행하는 Skia 빌드 보조 |
| `scripts/verify_skia_root.ps1` | Skia 산출물 검사 |
| `docs/skia-build.md` | Skia 준비 절차 |

submodule 두 개를 추가했다. `third_party/nlohmann-json`(v3.12.0), `third_party/catch2`(v3.15.3).

### 3.3 수정

- `cmake/generate_notices.cmake`: vcpkg installed 트리의 copyright 파일 대신 submodule과 자산 디렉터리의 라이선스 원문을 읽는다. 목록에 있는 파일이 없으면 `FATAL_ERROR`로 보고해 의존성 추가 시 고지 누락을 막는다.
- `scripts/check_source_style.ps1`: 제외 디렉터리에 `third_party`를 추가했다. submodule의 제3자 소스까지 검사해 실패했다.
- `.gitattributes`: `*.patch`는 `-text`로 두어 LF를 유지한다. CRLF로 정규화되면 `git apply`가 성립하지 않는다. submodule 경로도 정규화 대상에서 제외했다.

## 4. `src`와 `tests` 수정 범위

설계는 `src/`와 `tests/`를 수정하지 않는 것을 제약으로 두었다. **`src/`는 한 줄도 수정하지 않았다.** target 이름(`unofficial::skia::skia`, `nlohmann_json::nlohmann_json`, `Catch2::Catch2WithMain`)과 `catch_discover_tests`를 동일하게 제공했기 때문이다.

`tests/`에서 한 파일만 수정했다. `tests/embedded_assets_tests.cpp`가 제3자 고지 형식을 `"Package: skia"`, `"Package: catch2"`로 검사하고 있었는데, 이는 vcpkg의 copyright 디렉터리 이름에 기댄 형식이다. 고지 생성 경로가 바뀌면서 형식이 `"Component: Skia"`로 바뀌었고, Catch2는 test 실행 파일에만 링크되므로 앱 고지에서 제외했다. 검사 대상을 새 형식과 실제 포함 구성 요소로 바꾸었다.

이 수정은 의존성 교체 때문이 아니라 고지 형식이 의도적으로 바뀌었기 때문이다.

## 5. 구현 중 드러난 사항

### 5.1 Skia 산출물은 라이브러리 네 개다

vcpkg는 모든 external을 하나의 `skia.lib`(538 MB)로 합쳐 주었다. 직접 빌드는 `skia.lib`, `skcms.lib`, `spirv_cross.lib`, `d3d12allocator.lib`를 따로 낸다. 네 개를 모두 링크해야 하며 `GITMAN_SKIA_COMPONENTS`가 목록을 관리한다.

Windows 시스템 라이브러리 `d3dcompiler`, `FontSub`, `Usp10`도 imported target의 링크 인터페이스에 추가했다. Gitman이 직접 쓰는 것(`d3d12`, `dwrite`, `dxgi` 등)은 `src/CMakeLists.txt`에 그대로 남아 있다.

### 5.2 함수 안의 `CMAKE_CURRENT_LIST_DIR`

`cmake/dependencies.cmake`가 함수 본문에서 `CMAKE_CURRENT_LIST_DIR`로 하위 파일을 include하려다 실패했다. 이 변수는 함수 정의 위치가 아니라 **호출 시점의 파일 기준**으로 평가된다. include 시점의 경로를 `GITMAN_DEPENDENCIES_DIRECTORY`에 붙잡아 두고 세 파일을 파일 범위에서 include하도록 바꾸었다. 파일을 include하는 것 자체에는 부작용이 없고, Catch2를 실제로 구성할지는 `BUILD_TESTS`가 정한다.

### 5.4 하나의 Skia 빌드가 두 generator를 모두 지원한다

vcpkg를 쓰던 시절에는 generator마다 전용 triplet(v143·v145)으로 의존성을 각각
빌드했다. 지금은 `build_skia.ps1`을 실행한 셸의 MSVC로 한 번 빌드한 산출물을 두
generator가 공유한다.

VS2022(MSVC 14.44)로 빌드한 Skia를 VS2026(MSVC 14.51)이 링크하고, 실행과 CTest
585개가 모두 통과함을 확인했다. MSVC의 v14x 계열 이진 호환이 전제이며, 정적 CRT를
양쪽 모두 `/MT`·`/MTd`로 맞추는 것이 조건이다.

### 5.3 Debug 구성의 expat

Debug 구성(`is_official_build=false`)에서 `skia_use_jpeg_gainmaps`의 기본값 `is_skia_dev_build`가 참이 되어 `optional("xml")`이 켜지고 expat external을 요구했다. `skia_use_jpeg_gainmaps = false`를 세 args 파일에 모두 추가했다. vcpkg port도 같은 값을 끄고 있었다.

## 6. 렌더링 동등성 검증

`harfbuzz`와 `icu`를 끈 Skia가 기존과 같은 화면을 그리는지가 이번 변경의 유일한
기능적 위험이었다. 변경 전 커밋(`019afd4`)을 별도 worktree에 두고 vcpkg로 빌드한
기준선 실행 파일을 만든 뒤, 같은 조건에서 창을 캡처해 픽셀 단위로 비교했다.

| 항목 | 값 |
| --- | --- |
| 비교 대상 | `019afd4` (vcpkg 빌드) vs `4aacaf6` (직접 빌드 Skia) |
| 캡처 조건 | `--renderer=cpu`, 853 x 480, `PrintWindow` |
| 비교 픽셀 | 409,440 |
| 다른 픽셀 | **0** |
| 최대 채널 차이 | **0** |

한국어 문자열("열린 문서가 없습니다. 오른쪽 위 버튼으로 .version-list 문서를
여세요.")과 caption의 Codicon 글리프가 기준선과 완전히 같게 그려진다.
`drawSimpleText`가 shaping engine 없이 한국어를 처리한다는 조사 결과가 실측으로
확인되었다.

Direct3D 렌더러의 캡처도 CPU 렌더러와 동일했다.

## 7. 남은 작업

1. 텍스트 처리 구성(harfbuzz + libgrapheme) 도입은 별도 작업(`N6`)이다.
2. 실사용 환경의 `chromium.googlesource.com` 접근 가능 여부 확인. 텍스트 구성에만
   영향이 있다.
3. `unicodetools` submodule의 sparse-checkout 적용 여부 결정. 텍스트 구성에만
   영향이 있다.
