# ADR-006: vcpkg 제거와 submodule 기반 의존성 구성

## 상태

초안 - 검수 대기 (2026-08-19, 3차 개정)

## 배경

Gitman은 GitHub에 소스만 제공하고 사용자가 자기 환경에서 직접 빌드해 사용하는 형태로 배포한다. 그 환경의 제약은 다음과 같다.

- 사람이 브라우저로 접근할 수 있는 곳은 모두 접근할 수 있다.
- 빌드 도구가 자동으로 수행하는 curl 방식의 취득은 통제되어 실패한다.
- 프록시 환경 변수 설정으로 우회되지 않는다.
- 네트워크가 되는 별도 머신이 없고 조직 외부에서 산출물을 반입할 수도 없다.
- 대상 환경의 MSVC toolset은 개발 머신과 다르며 개발 환경을 그대로 옮길 수 없다.

**사람이 손으로 하는 준비는 가능하고, 빌드 체계가 스스로 하는 취득만 불가능하다.** 소스를 GitHub에서 받아 빌드하는 경로가 성립하므로 git 자체는 동작한다.

현재 구성은 vcpkg manifest mode로 24개 패키지를 자동 취득해 빌드한다. 취득 지점은 vcpkg 부트스트랩 도구, 도구 port, port 원본 아카이브 약 15종, Skia port의 git external 10개, vcpkg registry checkout이다. 모두 자동 취득이므로 첫 configure에서 막힌다.

vcpkg의 존재 이유는 취득의 자동화다. 자동 취득이 성립하지 않는 환경에서 vcpkg를 유지하면 취득 지점 20여 곳을 우회하는 장치를 프로젝트가 계속 유지해야 하고, port를 갱신할 때마다 그 목록이 바뀐다.

## 조사 결과: Gitman이 현재 쓰는 Skia는 매우 작다

`src/` 전수 조사 결과 사용하는 Skia API는 다음이 전부다.

| 영역 | 사용 API |
| --- | --- |
| 그리기 | `SkCanvas` 기본 도형, `SkPaint`, `SkRect`, `SkRRect`, `SkPoint` |
| 문자 | `SkCanvas::drawSimpleText`, `SkFont::measureText`, `SkFontMetrics`, `SkTypeface` |
| 폰트 관리 | `SkFontMgr_New_DirectWrite` |
| Surface | `SkSurface` raster, Ganesh D3D |
| 기타 | `SkData`, `SkColorSpace`, `SkImageInfo`, `SkPixmap`, `SkRefCnt` |

`SkShaper`, `SkParagraph`, `SkUnicode`, `SkCodec`, `SkImages`, path ops, PDF, SVG는 한 곳도 쓰지 않는다. 한국어 문자열은 `drawSimpleText`가 DirectWrite typeface의 cmap으로 직접 처리하므로 현재 화면에는 shaping engine이 필요하지 않다.

다만 이는 **현재 구현 기준**이다. 문자열 줄바꿈, 텍스트 선택과 커서, 합자·커닝, 복합 문자 스크립트 중 하나라도 들어오면 `SkShaper`와 `SkParagraph`가 필요하고 그 둘은 harfbuzz와 ICU를 요구한다. 수동 빌드는 재빌드 비용이 크므로 이 결정은 **처음부터 harfbuzz와 ICU를 포함한 구성을 목표로 한다.** 세부 사항은 아래 "텍스트 처리 구성" 절에 둔다.

## 결정

vcpkg를 제거한다. 의존성은 모두 submodule로 두고, 취득은 `git submodule update --init`과 바이너리 도구 두 개로 끝낸다.

### submodule 구성

| 경로 | 대상 | 고정 | 필요 시점 |
| --- | --- | --- | --- |
| `third_party/nlohmann-json` | nlohmann/json | `v3.12.0` | 항상 |
| `third_party/catch2` | Catch2 | `v3.15.3` | `GITMAN_BUILD_TESTS=ON`일 때만 |
| `third_party/skia` | google/skia | `e7c90ecca9444fe09598f1630ab7cee2c0ee027a` | 항상 |
| `third_party/skia-externals/d3d12allocator` | D3D12MemoryAllocator | `169895d5` | 항상 |
| `third_party/skia-externals/spirv-cross` | SPIRV-Cross | `b8fcf307` | 항상 |
| `third_party/skia-externals/spirv-headers` | SPIRV-Headers | `6dd7ba99` | 항상 |
| `third_party/skia-externals/harfbuzz` | harfbuzz | `9cb1fee51069b206effb4736e443b038d230789d` | 텍스트 처리 구성 |
| `third_party/skia-externals/libgrapheme` | libgrapheme | `c0cab63c5300fa12284194fbef57aa2ed62a94c0` | 텍스트 처리 구성 |
| `third_party/skia-externals/unicodetools` | unicodetools | `66a3fa9dbdca3b67053a483d130564eabc5fe095` | 텍스트 처리 구성 (빌드 시점 표 생성) |
| `third_party/skia-externals/icu` | Chromium deps ICU | `364118a1d9da24bb5b770ac3d762ac144d6da5a4` | 텍스트 처리 구성 (BiDi 소스만) |

Skia의 external을 Skia submodule 안이 아니라 형제 경로에 두는 이유는 중첩 submodule을 Gitman이 관리할 수 없기 때문이다. Skia 저장소는 수정할 수 없으므로 `third_party/skia/third_party/externals/` 아래에 external을 **junction 또는 복사로** 배치하는 일은 준비 스크립트가 담당한다. Skia 저장소의 `.gitignore`가 이미 그 경로를 무시하므로 submodule이 dirty로 표시되지 않는다.

`tools/git-sync-deps`는 사용하지 않는다. 필요한 external만 명시적으로 고정한다.

### Catch2는 test 구성에서만 요구한다

기본 빌드(`GITMAN_BUILD_TESTS=OFF`)는 Catch2 submodule이 초기화되어 있지 않아도 configure와 빌드가 성립해야 한다. 실제 사용 환경은 앱만 빌드하며 test를 만들지 않는다.

- `GITMAN_BUILD_TESTS=OFF`: Catch2를 전혀 참조하지 않는다. 존재 검사도 하지 않는다.
- `GITMAN_BUILD_TESTS=ON`: submodule 존재를 검사하고, 없으면 초기화 명령을 안내하며 `FATAL_ERROR`로 멈춘다.

Catch2는 amalgamated가 아니라 submodule 전체 소스를 `add_subdirectory`로 빌드한다. 이 경우 `Catch2::Catch2WithMain`과 `catch_discover_tests`가 원본 그대로 제공되므로 `tests/CMakeLists.txt`와 test 소스의 include 문을 바꿀 필요가 없다.

### Skia는 사용자가 빌드하고 프로젝트는 연결만 한다

CMake는 네트워크도 GN도 건드리지 않는다. 이미 존재하는 빌드 산출물을 검사해 `unofficial::skia::skia` imported target으로 노출한다. `src/`와 `tests/`의 CMake 파일과 C++ 소스는 수정하지 않는다.

프로젝트는 대신 재현 가능한 빌드 절차를 고정할 책임을 진다.

- GN args를 저장소가 파일로 고정 제공한다. 정적 CRT를 Gitman의 `CMAKE_MSVC_RUNTIME_LIBRARY`와 일치시킨다.
- Skia 148의 Direct3D 백엔드는 `GrD3DBackendSurfaceInfo::operator==`를 `GPU_TEST_UTILS` 가드 안에 두면서 가드 밖에서 호출한다. 이를 고치는 패치 한 개를 저장소가 보관한다. vcpkg port의 `014-fix-direct3d.patch`와 같은 내용이다.
- `scripts/build_skia.ps1`이 external 배치, 패치 적용, `gn gen`, `ninja`를 한 명령으로 묶는다. **사용자가 손으로 실행하며 CMake와 CTest는 이 스크립트를 호출하지 않는다.**
- CMake는 `GITMAN_SKIA_ROOT`의 `args.gn`을 읽어 Gitman이 요구하는 기능 플래그가 켜져 있는지 검사한다. 잘못된 옵션으로 빌드된 Skia를 링크 오류가 아니라 명확한 메시지로 잡는다.

### 텍스트 처리 구성

2026-08-19 실측으로 확정했다. 상세는 [검증 기록](../verification/2026-08-19-skia-manual-build.md)에 있다.

**full ICU를 쓰지 않고 `skia_use_libgrapheme=true`로 SkUnicode를 구성한다.** 근거는 Skia의 번들 full ICU가 Windows에서만 다르게 동작한다는 데 있다. 다른 플랫폼은 ICU 데이터를 바이너리에 넣지만 Windows는 `stubdata` + `SkLoadICU`를 쓰고 `icudtl.dat`를 런타임에 파일로 읽는다. 이는 ADR-002의 단일 `.exe` 원칙과 충돌한다. libgrapheme backend는 실측에서 **사이드카 데이터 파일을 하나도 만들지 않았다.**

세 가지 부수 사실을 함께 기록한다.

1. **ICU 소스 checkout은 여전히 필요하다.** libgrapheme backend도 BiDi는 `skia_icu_bidi_third_party_dir`을 통해 ICU 소스 20여 개 파일을 컴파일한다. 데이터 파일은 쓰지 않으며 산출물은 0.4 MB다.
2. **`is_official_build=true`가 시스템 라이브러리를 켠다.** `skia_use_system_harfbuzz`와 `skia_use_system_icu`의 기본값이 `is_official_build && !is_canvaskit`이라 Release 구성에서 자동으로 켜지고 Linux 경로를 참조해 실패한다. 둘 다 명시적으로 `false`로 꺼야 한다.
3. **libgrapheme는 빌드 시점에 Unicode 표를 생성한다.** `unicodetools`의 18개 데이터 파일(21 MB)만 읽지만 저장소 작업 트리는 3.1 GB다. sparse-checkout으로 `unicodetools/data/ucd/15.0.0`만 받으면 52 MB로 줄어든다.

남은 미지수는 하나다. ICU만 GitHub 대응물이 없고 `chromium.googlesource.com/chromium/deps/icu`에 있다. 개발 머신에서는 clone이 성공했으나 실사용 환경의 접근 가능 여부는 확인되지 않았다. 막히면 브라우저로 `+archive/<commit>.tar.gz`를 받거나, `skia_icu_bidi_third_party_dir`이 GN 인자라는 점을 이용해 `github.com/unicode-org/icu`를 가리키는 자체 `BUILD.gn`으로 대체한다.

텍스트 구성은 현재 기능에 필요하지 않다. 최소 구성으로 재편을 완료한 뒤 독립적으로 도입한다.

### 제3자 고지는 저장소가 관리한다

현재 `cmake/generate_notices.cmake`는 vcpkg installed 트리의 copyright 파일을 모은다. vcpkg가 사라지므로 submodule의 라이선스 파일과 `third_party/licenses/`를 출처로 바꾼다. ADR-002가 정한 "모든 제3자 고지를 실행 파일에 포함한다"는 결과는 유지되고, 고지 대상이 빌드 환경에 따라 달라지지 않게 된다.

### 단일 경로로 통일한다

vcpkg 경로를 선택지로 남기지 않는다. 두 경로를 유지하면 검증을 두 번 해야 하고, 실제 배포 대상 환경에서 동작하지 않는 쪽이 기본값으로 남는다. 개발 머신도 같은 절차로 Skia를 빌드한다.

## 고려한 대안

### 저장소 벤더링 (2차 초안)

nlohmann/json 단일 헤더와 Catch2 amalgamated를 저장소에 직접 넣는 방식이었다. GitHub ZIP 다운로드로 소스를 받는 경우에도 성립한다는 장점이 있으나, 제3자 소스가 프로젝트 이력에 섞이고 버전 갱신이 수동 복사가 된다. git이 동작하는 환경이므로 submodule로 대체한다.

### 반입 패키지 (1차 초안)

네트워크가 되는 머신에서 만든 패키지를 대상 환경으로 옮기는 방식이었다. 별도 머신이 없고 반입이 불가능하며 toolset이 달라 전제가 성립하지 않는다. 폐기한다.

### vcpkg를 유지하고 `downloads/`를 손으로 채우기

vcpkg는 `downloads/`에 기대한 파일명과 SHA512가 맞는 파일이 있으면 curl을 호출하지 않는다. 그러나 필요한 파일 목록·URL·해시가 port 갱신마다 바뀌고 목록을 얻으려면 실패를 반복하며 로그에서 수집해야 한다. 취득 지점을 없애는 대신 그 목록을 사람이 관리하게 만든다. 채택하지 않는다.

### CMake가 GN 빌드를 구동

`tools/git-sync-deps`와 `bin/fetch-gn`이 자동 취득을 다시 도입하고, Skia 빌드 시간이 Gitman의 configure에 들어와 개발 반복이 느려진다. 채택하지 않는다.

### Skia 대신 Direct2D/DirectWrite 직접 사용

의존성 문제는 사라지지만 렌더링 계층 전체를 다시 쓰는 일이고 단계 1~7의 UI 구현이 무효가 된다. 범위 밖이다.

## 결과

- 대상 환경의 취득이 `git submodule update --init --recursive`와 `gn`·`ninja` 바이너리 두 개로 끝난다. 두 바이너리는 브라우저로 받을 수 있다.
- 준비 절차는 Skia를 1회 빌드하고 경로를 지정하는 것이다. 그 뒤의 `cmake --preset` 이하 명령은 현재와 동일하다.
- Skia 버전 고정과 빌드 옵션 관리 책임이 vcpkg port에서 프로젝트로 이동한다. submodule 고정, GN args 파일, 패치, 검증 스크립트로 감당한다.
- ADR-002의 버전 고정 정책은 유지되고 취득 수단만 바뀐다. `vcpkg.json`, `cmake/triplets/`, `cmake/vcpkg_toolchain.cmake`는 제거 대상이다.
- Catch2를 소스로 빌드하게 되므로 현재 ASan test가 vcpkg 이진과 ABI를 맞추려고 쓰던 `_DISABLE_STRING_ANNOTATION`·`_DISABLE_VECTOR_ANNOTATION` 회피를 없앨 수 있다. 부수 효과이며 별도 검증 대상이다.
- clone 용량이 늘어난다. Skia 저장소가 가장 크다. `--depth`를 쓰는 submodule 설정 여부는 구현에서 정한다.

## 검증 방법

Skia 빌드 실측은 2026-08-19에 완료했다. 결과는 [검증 기록](../verification/2026-08-19-skia-manual-build.md)에 있다.

| 단계 | 구성 | 결과 |
| --- | --- | --- |
| S-1 | 최소 (harfbuzz·ICU off) | 성공. external 3개로 충분, `skia.lib` 52.4 MB, 정적 CRT 일치, 필요한 심볼 전수 존재 |
| S-2 | + harfbuzz 번들 빌드 | 성공 |
| S-3 | + libgrapheme | 성공. 사이드카 데이터 파일 없음 |

vcpkg가 만들던 같은 라이브러리는 538.0 MB였고 최소 구성은 52.4 MB다.

남은 검증은 다음과 같다.

- 빌드 로그에 네트워크 취득이 한 건도 없는지 확인한다.
- `GITMAN_BUILD_TESTS=OFF`에서 Catch2 submodule을 초기화하지 않은 상태로 configure·빌드·설치가 성공하는지 확인한다.
- `GITMAN_SKIA_ROOT`가 없거나, 산출물이 없거나, CRT가 어긋나거나, 요구 기능 플래그가 꺼진 Skia를 지정한 경우 configure가 원인과 조치를 보고하며 실패하는지 확인한다.
- 실행 파일 resource의 제3자 고지가 새 생성 경로에서도 모든 항목을 포함하는지 확인한다.
- CTest 전량이 통과하는지 확인한다.

## 근거 자료

- [Skia: Building Skia](https://skia.org/docs/user/build/)
- [Catch2 CMake integration](https://github.com/catchorg/Catch2/blob/devel/docs/cmake-integration.md)
- [ADR-001: Windows toolchain](ADR-001-windows-toolchain.md)
- [ADR-002: C++ 의존성과 Codicons 자산 고정](ADR-002-dependencies-and-assets.md)
