# 의존성 구성 재편 설계

이 문서는 [ADR-006](decisions/ADR-006-restricted-network-dependency-provisioning.md)의 결정을 파일 단위 변경으로 옮긴 구현 설계다. 상태는 **초안 - 검수 대기**다. 승인 전에는 이 문서에 기술한 코드와 build file을 작성하지 않는다.

## 1. 설계 제약

의존성이 앱 코드에 닿는 지점은 세 target 이름뿐이다.

| 소비처 | target |
| --- | --- |
| `src/CMakeLists.txt:238` | `nlohmann_json::nlohmann_json` |
| `src/CMakeLists.txt:239` | `unofficial::skia::skia` |
| `tests/CMakeLists.txt:89`, `:103` | `Catch2::Catch2WithMain` |

세 이름과 `catch_discover_tests`를 동일하게 제공하는 것이 제약이다. **`src/`와 `tests/`의 CMake 파일과 C++ 소스는 이번 변경에서 수정하지 않는다.** 변경 범위는 최상위 `CMakeLists.txt`, `cmake/`, `CMakePresets.json`, `scripts/`, `third_party/`, `docs/`, `.gitmodules`다.

두 번째 제약은 **빌드는 네트워크를 시도하지 않는다**이다. CMake와 CTest가 실행하는 어떤 경로에도 취득이 없어야 한다. 취득은 사람이 실행하는 준비 단계에만 존재한다.

세 번째 제약은 **test 없이 빌드하는 경우가 기본**이라는 것이다. `GITMAN_BUILD_TESTS=OFF`에서는 Catch2 submodule이 초기화되어 있지 않아도 성립해야 한다.

## 2. submodule 구성

`.gitmodules`에 다음을 등록한다.

| 경로 | 원격 | 고정 |
| --- | --- | --- |
| `third_party/nlohmann-json` | `github.com/nlohmann/json` | `v3.12.0` |
| `third_party/catch2` | `github.com/catchorg/Catch2` | `v3.15.3` |
| `third_party/skia` | `github.com/google/skia` | `e7c90ecca9444fe09598f1630ab7cee2c0ee027a` |
| `third_party/skia-externals/d3d12allocator` | `github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator` | `169895d529dfce00390a20e69c2f516066fe7a3b` |
| `third_party/skia-externals/spirv-cross` | `github.com/KhronosGroup/SPIRV-Cross` | `b8fcf307f1f347089e3c46eb4451d27f32ebc8d3` |
| `third_party/skia-externals/spirv-headers` | `github.com/KhronosGroup/SPIRV-Headers` | `6dd7ba990830f7c15ac1345ff3b43ef6ffdad216` |
| `third_party/skia-externals/harfbuzz` | `github.com/harfbuzz/harfbuzz` | `9cb1fee51069b206effb4736e443b038d230789d` |
| `third_party/skia-externals/libgrapheme` | `github.com/FRIGN/libgrapheme` | `c0cab63c5300fa12284194fbef57aa2ed62a94c0` |
| `third_party/skia-externals/unicodetools` | `github.com/unicode-org/unicodetools` | `66a3fa9dbdca3b67053a483d130564eabc5fe095` |
| `third_party/skia-externals/icu` | `chromium.googlesource.com/chromium/deps/icu` | `364118a1d9da24bb5b770ac3d762ac144d6da5a4` |

아래 넷은 텍스트 처리 구성에만 필요하며 최소 구성에서는 초기화하지 않아도 된다. `unicodetools`는 작업 트리가 3.1 GB이므로 sparse-checkout(`unicodetools/data/ucd/15.0.0`, 52 MB) 적용을 권한다. ICU만 GitHub 대응물이 없다.

사내 미러를 쓰는 환경을 위해 원격 치환 방법을 문서에 남긴다.

```powershell
git config --global url."https://<사내미러>/".insteadOf "https://github.com/"
```

`gn`과 `ninja` 두 바이너리만 submodule 밖에 남는다. 브라우저로 받아 PATH에 두거나 `scripts/build_skia.ps1`에 경로로 전달한다.

## 3. Skia 준비 절차 (`docs/skia-build.md`)

빌드 전에 사람이 1회 수행한다.

### 3.1 external 배치

Skia는 `third_party/externals/<이름>`에서 external을 찾는다. Skia submodule을 수정하지 않기 위해 준비 스크립트가 junction으로 연결한다.

```text
third_party/skia/third_party/externals/d3d12allocator  ->  third_party/skia-externals/d3d12allocator
third_party/skia/third_party/externals/spirv-cross     ->  third_party/skia-externals/spirv-cross
third_party/skia/third_party/externals/spirv-headers   ->  third_party/skia-externals/spirv-headers
```

Skia 저장소의 `.gitignore`가 `third_party/externals`를 무시하므로 submodule이 dirty로 표시되지 않는다. junction 생성이 막힌 환경을 위해 복사 방식(`-CopyExternals`)도 제공한다.

### 3.2 패치

`third_party/patches/skia-148-direct3d-operator-equals.patch`를 적용한다. Skia 148은 `GrD3DTextureResourceInfo`와 `GrD3DBackendSurfaceInfo`의 `operator==`를 `GPU_TEST_UTILS` 가드 안에 두면서 `GrD3DBackendSurface.cpp`가 가드 밖에서 호출한다. 적용하지 않으면 Direct3D 빌드가 깨진다.

패치는 Skia submodule의 작업 트리를 수정하므로 submodule이 dirty가 된다. 준비 스크립트가 적용 전에 이미 적용되었는지 확인하고 중복 적용하지 않는다.

### 3.3 GN args

저장소가 파일로 고정 제공한다. 4장의 실측 단계마다 하나씩 확정한다.

| 파일 | 구성 |
| --- | --- |
| `third_party/skia-args/gitman-release.gn` | Release, 확정된 기능 집합 |
| `third_party/skia-args/gitman-debug.gn` | Debug |

최소 구성의 Release 기준선은 다음과 같다. harfbuzz·ICU 항목은 4장에서 바뀐다.

```text
target_cpu = "x64"
target_os = "win"
is_official_build = true
is_component_build = false

skia_use_direct3d = true
skia_use_gl = false
skia_use_vulkan = false
skia_use_metal = false

skia_use_harfbuzz = false
skia_use_system_harfbuzz = false
skia_use_icu = false
skia_use_system_icu = false
skia_use_icu4x = false
skia_use_libgrapheme = false
skia_use_client_icu = false
skia_use_freetype = false
skia_use_fontconfig = false
skia_use_expat = false
skia_use_zlib = false

skia_use_libpng_decode = false
skia_use_libpng_encode = false
skia_use_no_png_encode = true
skia_use_libjpeg_turbo_decode = false
skia_use_libjpeg_turbo_encode = false
skia_use_no_jpeg_encode = true
skia_use_libwebp_decode = false
skia_use_libwebp_encode = false
skia_use_no_webp_encode = true
skia_use_dng_sdk = false
skia_use_piex = false
skia_use_wuffs = false
skia_use_lua = false

skia_enable_pdf = false
skia_enable_svg = false
skia_enable_skottie = false
skia_enable_graphite = false
skia_enable_tools = false
skia_enable_android_utils = false
skia_enable_spirv_validation = false
skia_enable_gpu_debug_layers = false

extra_cflags = [ "/MT" ]
```

Debug는 `is_official_build = false`, `is_debug = true`, `extra_cflags = [ "/MTd" ]`로 바꾼다. 정적 CRT를 Gitman의 `CMAKE_MSVC_RUNTIME_LIBRARY`와 일치시키는 것이 핵심이며 어긋나면 LNK2038로 드러난다.

`skia_enable_fontmgr_win`은 Windows 기본값이 `true`이고 `SkFontMgr_New_DirectWrite`가 여기에 의존하므로 명시하지 않고 기본값을 쓴다.

### 3.4 `scripts/build_skia.ps1`

3.1~3.3을 한 명령으로 묶는다.

```powershell
scripts\build_skia.ps1 -Configuration Release [-GnPath <경로>] [-NinjaPath <경로>] [-CopyExternals]
```

**사용자가 손으로 실행하며 CMake와 CTest는 이 스크립트를 호출하지 않는다.** 이 구분을 스크립트 상단 주석과 문서에 명시한다. submodule이 초기화되지 않았거나 `gn`·`ninja`를 찾지 못하면 무엇을 어디에 두어야 하는지 보고하고 멈춘다. 산출물은 `third_party/skia/out/gitman-<구성>`이다.

## 4. Skia 기능 구성 실측 (완료)

2026-08-19에 세 단계를 모두 실측했다. 전체 기록은 [검증 기록](verification/2026-08-19-skia-manual-build.md)에 있다.

| 단계 | 구성 | 결과 |
| --- | --- | --- |
| S-1 | 최소 (harfbuzz·ICU off) | 성공. external 3개로 충분, `skia.lib` 52.4 MB, 정적 CRT 일치, 필요한 심볼 전수 존재 |
| S-2 | + harfbuzz 번들 빌드 | 성공. `harfbuzz.lib` 16.1 MB, `skshaper.lib` 16.4 MB |
| S-3 | + libgrapheme | 성공. `skunicode_libgrapheme.lib` 1.0 MB, 사이드카 데이터 파일 없음 |

3장의 GN args와 2장의 external 목록은 이 실측으로 확정된 값이다. 실측에서 드러나 설계에 반영한 사항은 다음 세 가지다.

1. **`skia_use_system_harfbuzz`와 `skia_use_system_icu`를 명시적으로 꺼야 한다.** 기본값이 `is_official_build && !is_canvaskit`이라 Release 구성에서 자동으로 켜지고 Linux 경로(`/usr/include/harfbuzz`)를 참조해 실패한다.
2. **full ICU 대신 libgrapheme를 쓴다.** Skia의 번들 full ICU는 Windows에서만 `icudtl.dat`를 런타임 파일로 읽어 단일 `.exe` 원칙과 충돌한다. libgrapheme backend는 데이터 파일을 만들지 않는다.
3. **ICU 소스 checkout은 여전히 필요하다.** libgrapheme backend도 BiDi는 `skia_icu_bidi_third_party_dir`을 통해 ICU 소스 20여 개 파일을 컴파일한다. 데이터 파일은 쓰지 않으며 `icu_bidi.lib`는 0.4 MB다.

남은 미지수는 ICU 취득 경로 하나다. `chromium.googlesource.com/chromium/deps/icu`에만 있고 GitHub 대응물이 없다. 개발 머신에서는 clone이 성공했으나 실사용 환경은 확인되지 않았다. 막히면 브라우저 `+archive/<commit>.tar.gz` 또는 `skia_icu_bidi_third_party_dir`을 자체 `BUILD.gn`으로 대체하는 두 경로가 있다.

텍스트 구성은 현재 기능에 필요하지 않다. 최소 구성으로 재편을 완료한 뒤 독립적으로 도입한다.

## 5. 파일 단위 변경

### 5.1 신설: `cmake/dependencies/skia.cmake`

`GITMAN_SKIA_ROOT`(기본값 `${PROJECT_SOURCE_DIR}/third_party/skia`), `GITMAN_SKIA_BUILD_DEBUG`, `GITMAN_SKIA_BUILD_RELEASE`를 읽어 `unofficial::skia::skia`를 STATIC IMPORTED로 만든다.

- `IMPORTED_LOCATION_DEBUG` / `IMPORTED_LOCATION_RELEASE`를 구성별로 지정하고 `MinSizeRel`·`RelWithDebInfo`는 `CMAKE_MAP_IMPORTED_CONFIG_*`로 Release에 매핑한다.
- `INTERFACE_INCLUDE_DIRECTORIES`는 Skia 소스 루트 하나다. Gitman이 `#include "include/core/SkCanvas.h"` 형식으로 쓰고 있어 루트만 있으면 된다.
- `INTERFACE_LINK_LIBRARIES`에 Windows 시스템 라이브러리를 명시한다. 현재 vcpkg 구성이 요구하는 것은 `FontSub.lib`, `Usp10.lib`, `d3d12.lib`, `dxgi.lib`, `d3dcompiler.lib`다. 실제 목록은 4장의 링크 검증에서 확정한다.
- configure 검사 세 가지: 헤더 존재, 구성별 `skia.lib` 존재, `args.gn` 파싱으로 요구 기능 플래그 확인. 요구 목록은 현재 `skia_use_direct3d=true` 하나이며 향후 `SkShaper`를 쓰기 시작하면 `skia_use_harfbuzz=true`를 추가한다. 실패 시 `docs/skia-build.md`의 해당 절을 가리키며 `FATAL_ERROR`.

### 5.2 신설: `cmake/dependencies/nlohmann_json.cmake`

submodule을 `add_subdirectory`한다. `JSON_BuildTests=OFF`, `JSON_Install=OFF`를 설정해 Gitman 솔루션에 불필요한 target이 생기지 않게 한다. `nlohmann_json::nlohmann_json`은 upstream이 그대로 제공한다.

submodule이 비어 있으면 초기화 명령을 안내하며 `FATAL_ERROR`.

### 5.3 신설: `cmake/dependencies/catch2.cmake`

`GITMAN_BUILD_TESTS=ON`일 때만 include된다. submodule을 `add_subdirectory`하고 `CATCH_CONFIG_THREAD_SAFE_ASSERTIONS`를 정의로 준다. `Catch2::Catch2WithMain`과 `catch_discover_tests`(`third_party/catch2/extras/Catch.cmake`)를 upstream 그대로 사용하므로 `tests/`는 수정이 없다.

`gitman_set_ide_folder`로 Catch2 target을 `Tests` 폴더 아래에 넣어 솔루션 최상위를 어지럽히지 않는다.

### 5.4 신설: `cmake/dependencies.cmake`

```cmake
gitman_find_dependencies(BUILD_TESTS <bool>)
```

Skia와 nlohmann/json을 항상, Catch2를 `BUILD_TESTS`일 때만 구성한다. 최상위 `CMakeLists.txt`는 이 파일 하나만 include한다.

### 5.5 수정: `CMakeLists.txt`

- `find_package(unofficial-skia ...)`, `find_package(nlohmann_json ...)`, `find_package(Catch2 ...)` 제거.
- `VCPKG_TARGET_TRIPLET` 검증 블록과 `GITMAN_VCPKG_TARGET_TRIPLET` 제거.
- `GITMAN_CATCH2_VERSION`은 submodule 고정으로 대체되므로 제거한다.
- `GITMAN_SKIA_ROOT` 등 새 변수 정의와 `include(dependencies.cmake)` 추가.

### 5.6 수정: `cmake/generate_notices.cmake`

vcpkg installed 트리 대신 `third_party/licenses/`의 목록 파일을 읽는다. 목록은 라이선스 원문 경로와 표시 이름을 짝지으며, submodule 안의 원문(`third_party/skia/LICENSE` 등)을 직접 가리켜 사본이 어긋나지 않게 한다. 목록에 있는 파일이 하나라도 없으면 `FATAL_ERROR`로 보고해 의존성을 추가하고 고지를 빠뜨리는 일을 막는다.

초기 목록은 Skia(BSD-3-Clause), nlohmann/json(MIT), Catch2(BSL-1.0), Codicons, Skia에 정적으로 포함되는 external들이다. Catch2는 test 구성에서만 링크되므로 앱 고지에 넣을지 여부를 구현에서 정한다.

### 5.7 수정: `CMakePresets.json`

- `toolchainFile`, `VCPKG_HOST_TRIPLET`, `VCPKG_TARGET_TRIPLET`, `VCPKG_OVERLAY_TRIPLETS` 제거.
- preset 개수와 이름은 유지한다. 사용자가 익힌 명령이 바뀌지 않는다.
- `GITMAN_SKIA_ROOT`는 기본값이 저장소 안(`third_party/skia`)이므로 preset에 넣지 않아도 된다. 외부 경로를 쓰려는 사용자를 위해 `CMakeUserPresets.json` 예시를 문서에 둔다.

### 5.8 삭제

- `vcpkg.json`
- `cmake/vcpkg_toolchain.cmake`
- `cmake/triplets/` 전체
- `build/vcpkg-baseline` 관련 문서 서술

### 5.9 신설: `scripts/verify_skia_root.ps1`

헤더 존재, 구성별 `skia.lib` 존재, `skia.lib`의 정적 CRT 여부(`dumpbin /directives`의 `/DEFAULTLIB` 확인), `args.gn`의 기능 플래그, 패치 적용 여부를 표로 보고한다. CMake configure 검사와 같은 판정을 사람이 먼저 돌려볼 수 있게 한다.

### 5.10 수정: 문서

- 신설 `docs/skia-build.md`: 2~3장.
- `docs/build.md`: 2장 "vcpkg 준비"를 "의존성 준비"로 교체하고 submodule 초기화와 `docs/skia-build.md` 연결을 넣는다. 3장 이하 명령은 유지.
- `README.md`: 개발 환경 준비 절의 vcpkg 문단 교체.
- `docs/decisions/ADR-002-dependencies-and-assets.md`: 취득 수단이 ADR-006으로 대체되었음을 상태 절에 명시. 버전 고정 내용은 유지.
- `docs/change_log.md`, `docs/requirements.md`, `docs/plan.md`: 기존 형식으로 항목 추가.

## 6. 구현 순서

각 단계 끝에서 검수를 요청한다.

| 단계 | 내용 | 완료 판정 |
| --- | --- | --- |
| N0 | 4장 S-1~S-3 실측 | **완료 (2026-08-19)**. external·GN args 확정, ICU 대체 경로 확정 |
| N1 | submodule 등록, `build_skia.ps1`, 패치, GN args 파일 | 새 clone에서 submodule 초기화 후 스크립트만으로 Skia 재현 |
| N2 | `cmake/dependencies/skia.cmake`, `CMakeLists.txt`·preset에서 vcpkg 제거 | `gitman.exe` 빌드·설치 성공, 한국어·Codicon 육안 검증 통과 |
| N3 | nlohmann/json submodule 연결 | 앱 빌드 유지, 워크스페이스 JSON test 통과 |
| N4 | Catch2 submodule 연결, test 구성 분리 | `GITMAN_BUILD_TESTS=OFF`에서 Catch2 미초기화로 빌드 성공, `ON`에서 CTest 585개 전량 통과 |
| N5 | 고지 생성 경로 교체, `verify_skia_root.ps1` | 고지 전수 포함, 오류 경로 메시지 확인 |
| N6 | 텍스트 구성(harfbuzz + libgrapheme) 도입 | `skshaper`·`skparagraph` 링크와 ICU 취득 경로 확정 |
| N7 | 문서·계획·요구사항 갱신 | `check_source_style.ps1` 통과 |

N0은 완료했다. N6은 현재 기능에 영향이 없으므로 N5까지 완료한 뒤 독립적으로 진행할 수 있다.

## 7. 위험과 대응

| 위험 | 근거 | 대응 |
| --- | --- | --- |
| ICU 데이터가 단일 `.exe` 원칙과 충돌 | Skia + full ICU는 `icudtl.dat`를 요구할 수 있다 | 4장 S-3의 세 갈래. 최악의 경우 S-1 구성으로 재편을 완료하고 텍스트 기능 도입 시 재검토 |
| ICU 소스가 googlesource에만 있음 | Skia DEPS가 Chromium deps 저장소를 참조한다 | S-3에서 접근 확인. 막히면 대체 backend 또는 미러 등록 |
| external 3개 외에 더 필요 | vcpkg port 배선에서 도출한 잠정값이다 | N0 실빌드로 확정. 2장 표는 잠정값이다 |
| `skia_use_zlib=false`에서 빌드가 깨짐 | PDF를 끄면 불필요하나 미검증 | N0에서 확인, 필요하면 external 추가 |
| 정적 CRT 불일치 | Skia와 Gitman이 각각 빌드된다 | GN args 고정, `verify_skia_root.ps1`과 configure 검사 |
| Skia 패치로 submodule이 dirty | 작업 트리를 수정한다 | 준비 스크립트가 중복 적용을 막고 상태를 보고. `git submodule status`의 `+` 표시가 정상임을 문서에 명시 |
| clone 용량 증가 | Skia 저장소가 크다 | submodule의 `shallow = true` 적용 여부를 N1에서 판단 |
| vcpkg 제거로 개발 머신의 기존 빌드가 무효화 | 4.5 GB binary cache가 쓸모없어진다 | N0~N7을 분리 commit해 문제 시 이전 commit으로 복귀 |
| Skia 버전 갱신 부담이 프로젝트로 이동 | vcpkg port가 하던 일이다 | 갱신을 기능 변경과 분리하고 submodule commit·GN args·패치를 한 변경으로 다룬다 |

## 8. 검수에서 확인이 필요한 항목

1. 실사용 환경에서 `chromium.googlesource.com` 접근이 되는지. ICU만 GitHub 대응물이 없어 텍스트 구성의 유일한 미지수다.
2. Skia submodule에 `shallow`를 적용할지. clone 용량과 이력 접근성의 교환이다.
3. Catch2 라이선스 고지를 앱 실행 파일에 포함할지. test에서만 링크된다.
4. `gn`·`ninja` 바이너리를 어떻게 안내할지. 문서 링크만 둘지, `scripts/build_skia.ps1`이 경로 인자를 받아 검사만 할지.
