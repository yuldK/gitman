# 검증 기록 - Skia 수동 빌드 실측 (`S-1`~`S-3`)

## 1. 개요

- 날짜: 2026-08-19
- 대상: [ADR-006](../decisions/ADR-006-restricted-network-dependency-provisioning.md)의 전제 검증. vcpkg 없이 Skia를 직접 빌드해 Gitman이 요구하는 기능을 얻을 수 있는지 확인한다
- 배경: 실사용 환경이 조직 프록시로 자동 취득을 차단한다. 설계의 external 목록과 GN args는 vcpkg port의 feature 배선에서 도출한 잠정값이었고 실빌드로만 확정할 수 있었다
- 환경: Windows 11 Pro, VS2022 (MSVC 14.44.35207), Windows SDK 10.0.26100.0, Python 3.11.3
- 도구: `gn`과 `ninja`는 별도 취득 없이 로컬 vcpkg 캐시의 것을 사용했다 (`vcpkg-tool-gn`, `ninja-1.13.2-windows`)

## 2. 실측 결과 요약

| 단계 | 구성 | 결과 |
| --- | --- | --- |
| `S-1` | 최소 (harfbuzz·ICU off) | **성공** - 826 targets, `skia.lib` 52.4 MB |
| `S-2` | + harfbuzz (번들 빌드) | **성공** - `harfbuzz.lib`, `skshaper.lib` 산출 |
| `S-3` | + libgrapheme (ICU 대체) | **성공** - `skunicode_libgrapheme.lib` 산출, **사이드카 데이터 파일 없음** |

세 단계 모두 성립했다. Gitman이 요구하는 기능은 vcpkg 없이 전부 얻을 수 있다.

## 3. `S-1` 최소 구성

### 3.1 확정된 external

vcpkg port에서 도출한 잠정 목록 3개가 그대로 충분했다. `gn gen`이 89 targets를 생성했고 추가 요구가 없었다.

| external | 고정 commit | 출처 |
| --- | --- | --- |
| `d3d12allocator` | `169895d529dfce00390a20e69c2f516066fe7a3b` | GitHub |
| `spirv-cross` | `b8fcf307f1f347089e3c46eb4451d27f32ebc8d3` | GitHub |
| `spirv-headers` | `6dd7ba990830f7c15ac1345ff3b43ef6ffdad216` | GitHub |

`skia_use_zlib=false`, `skia_use_expat=false`, `skia_use_piex=false`, `skia_use_wuffs=false`가 모두 유효했다. vcpkg port가 `required_externals`에 항상 포함하던 `expat`, `piex`, `zlib`, `wuffs`는 upstream 직접 빌드에서는 필요하지 않다.

### 3.2 산출물

| 항목 | 값 |
| --- | --- |
| `skia.lib` | 54,930,292 byte (52.4 MB) |
| 함께 산출 | `spirv_cross.lib` 17.6 MB, `d3d12allocator.lib` 0.3 MB, `skcms.lib` 0.1 MB |
| CRT 지시자 | `/DEFAULTLIB:LIBCMT`, `/DEFAULTLIB:libcpmt` (정적 release CRT, Gitman의 `MultiThreaded`와 일치) |
| 기타 지시자 | `/DEFAULTLIB:OLDNAMES`, `/DEFAULTLIB:uuid.lib` |

vcpkg가 만든 같은 이름의 라이브러리는 **538.0 MB**였다. 사용하지 않는 기능을 끈 결과 **10분의 1 이하**로 줄었다.

### 3.3 심볼 확인

Gitman이 실제로 호출하는 진입점이 모두 존재한다.

| 심볼 | 결과 |
| --- | --- |
| `SkFontMgr_New_DirectWrite` | 존재 |
| `SkCanvas::drawSimpleText` | 존재 |
| `SkFont::measureText` | 존재 |
| `GrDirectContexts::MakeD3D` | 존재 |
| `GrBackendRenderTargets::MakeD3D` | 존재 |
| `SkSurfaces::WrapBackendRenderTarget` | 존재 |
| `SkSurfaces::Raster` | 존재 |

### 3.4 필요한 패치

vcpkg port의 `014-fix-direct3d.patch`가 그대로 적용되었고 필요했다. Skia 148은 `GrD3DTextureResourceInfo`와 `GrD3DBackendSurfaceInfo`의 `operator==`를 `GPU_TEST_UTILS` 가드 안에 두면서 가드 밖에서 호출한다. `third_party/patches/skia-148-direct3d-operator-equals.patch`로 보관한다.

## 4. `S-2`·`S-3` 텍스트 구성

### 4.1 `is_official_build`가 시스템 라이브러리를 켠다

가장 먼저 드러난 함정이다. `skia_use_system_harfbuzz`와 `skia_use_system_icu`의 기본값이 `is_official_build && !is_canvaskit`이라, `is_official_build=true`인 Release 구성에서 자동으로 켜진다. 그 결과 Linux 경로(`/usr/include/harfbuzz`)를 참조해 컴파일이 실패한다.

**Windows에서 번들 소스로 빌드하려면 두 값을 명시적으로 꺼야 한다.**

```text
skia_use_system_harfbuzz = false
skia_use_system_icu = false
```

vcpkg port가 `skia_use_system_icu=true`를 쓰고 icu·harfbuzz를 vcpkg 패키지로 끌어왔던 이유가 여기에 있다.

### 4.2 ICU 데이터 파일 문제와 그 해소

`third_party/icu/BUILD.gn`을 읽은 결과, Skia의 번들 full ICU는 Windows에서만 다르게 동작한다. 다른 플랫폼은 ICU 데이터를 어셈블리로 바이너리에 넣지만, **Windows는 `stubdata.cpp` + `SkLoadICU.cpp`를 쓰고 `icudtl.dat`를 출력 디렉터리로 복사해 런타임에 읽는다.** 이는 단일 `.exe` 원칙(ADR-002)과 충돌한다.

`skia_use_libgrapheme=true`로 SkUnicode backend를 바꾸면 이 문제가 사라진다. 실측 결과 **출력 디렉터리에 `.dat` 파일이 하나도 생성되지 않았다.**

다만 libgrapheme backend도 BiDi는 ICU에 의존한다. `modules/skunicode/BUILD.gn`의 `skunicode_libgrapheme`가 `skia_icu_bidi_third_party_dir`을 참조하며, 이는 ICU **소스 20여 개 파일**(`ubidi.cpp`, `uchar.cpp`, `utrie2.cpp` 등)만 컴파일한다. 데이터 파일은 쓰지 않는다. 산출된 `icu_bidi.lib`는 0.4 MB다.

정리하면 **ICU 소스 checkout은 필요하고, ICU 런타임 데이터 파일은 필요하지 않다.**

### 4.3 libgrapheme의 표 생성 비용

libgrapheme는 빌드 시점에 Unicode 표를 직접 생성하며 `unicodetools` 저장소의 데이터를 읽는다. 필요한 것은 `unicodetools/data/ucd/15.0.0` 아래 **18개 `.txt` 파일(합계 21 MB)**뿐이지만, 저장소 전체 작업 트리는 **3.1 GB**다.

| 범위 | 크기 |
| --- | --- |
| `unicodetools` 작업 트리 전체 | 3.1 GB |
| `--filter=blob:none` 적용 시 `.git` | 246 MB |
| 실제 필요한 `data/ucd/15.0.0` | 52 MB |
| 실제로 읽는 18개 파일 | 21 MB |

sparse-checkout으로 `unicodetools/data/ucd/15.0.0`만 받으면 3.1 GB를 52 MB로 줄일 수 있다. 이 설정은 submodule에 적용 가능하다.

### 4.4 산출물

| 라이브러리 | 크기 |
| --- | --- |
| `skia.lib` | 52.4 MB |
| `spirv_cross.lib` | 17.6 MB |
| `skshaper.lib` | 16.4 MB |
| `harfbuzz.lib` | 16.1 MB |
| `skparagraph.lib` | 1.7 MB |
| `skunicode_libgrapheme.lib` | 1.0 MB |
| `libgrapheme.lib` | 0.4 MB |
| `icu_bidi.lib` | 0.4 MB |
| `bentleyottmann.lib` | 0.4 MB |
| `d3d12allocator.lib` | 0.3 MB |
| `skcms.lib` | 0.1 MB |
| `skunicode_core.lib` | 0.03 MB |
| **합계** | **106.8 MB** |

`skia.lib` 자체 크기는 최소 구성과 동일하다. 텍스트 기능은 별도 모듈 라이브러리로 나온다. 정적 링크이므로 Gitman이 호출하지 않는 모듈은 최종 실행 파일에 들어가지 않는다.

## 5. 취득 경로 확정

| 대상 | 원격 | 브라우저 접근 |
| --- | --- | --- |
| skia | `github.com/google/skia` | 가능 |
| d3d12allocator | `github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator` | 가능 |
| spirv-cross | `github.com/KhronosGroup/SPIRV-Cross` | 가능 |
| spirv-headers | `github.com/KhronosGroup/SPIRV-Headers` | 가능 |
| harfbuzz | `github.com/harfbuzz/harfbuzz` | 가능 |
| libgrapheme | `github.com/FRIGN/libgrapheme` | 가능 |
| unicodetools | `github.com/unicode-org/unicodetools` | 가능 |
| icu | `chromium.googlesource.com/chromium/deps/icu` | **확인 필요** |

Skia의 `DEPS`는 대부분 googlesource 미러를 가리키지만, ICU를 제외한 전부가 GitHub 원본을 가진다. **ICU만 Chromium의 deps 저장소이며 GitHub 대응물이 없다.** 이 개발 머신에서는 clone이 성공했고 크기는 315 MB(`--filter=blob:none`)였으나, 실사용 환경에서의 접근 가능 여부는 확인되지 않았다.

막힐 경우의 경로는 두 가지다.

1. 브라우저로 `+archive/<commit>.tar.gz`를 받아 해당 경로에 푼다.
2. `skia_icu_bidi_third_party_dir`이 GN 인자이므로, `github.com/unicode-org/icu`의 `icu4c/source/...`를 가리키는 자체 `BUILD.gn`을 두고 그 경로로 바꾼다.

## 6. 저장소에 반영한 내용

- submodule 8개를 고정 commit으로 등록했다 (`.gitmodules`).
- `third_party/patches/skia-148-direct3d-operator-equals.patch`
- `third_party/skia-args/gitman-release.gn` (최소 구성)
- `third_party/skia-args/gitman-release-text.gn` (harfbuzz + libgrapheme 구성)

external은 `third_party/skia/third_party/externals/` 아래에 junction으로 연결했다. Skia 저장소의 `.gitignore`가 그 경로를 무시하므로 submodule이 dirty로 표시되지 않는다. D3D 패치는 Skia 작업 트리를 수정하므로 submodule이 dirty가 되며, 이는 의도된 상태다.

## 7. 후속 확인이 필요한 항목

1. Gitman을 이 `skia.lib`에 실제로 링크해 `gitman.exe`가 빌드·실행되는지 (설계의 `N2`).
2. 한국어와 Codicon 글리프가 기존과 동일하게 그려지는지 육안 검증.
3. Debug 구성(`is_debug=true`, `/MTd`) 빌드.
4. VS2026 toolset에서의 재현.
5. 실사용 환경의 googlesource 접근 가능 여부 (5장).
6. `unicodetools` submodule에 sparse-checkout을 적용할지 (4.3).
