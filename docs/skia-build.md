# Skia 준비 안내

Gitman은 Skia를 자동으로 내려받거나 빌드하지 않는다. 사용자가 1회 직접 빌드하고, CMake는 그 산출물을 검사해 연결만 한다. 근거는 [ADR-006](decisions/ADR-006-restricted-network-dependency-provisioning.md)에 있다.

이 문서의 절차는 개발 머신에서 실측으로 확인했다. 결과는 [검증 기록](verification/2026-08-19-skia-manual-build.md)에 있다.

## 1. 필요한 것

| 항목 | 비고 |
| --- | --- |
| submodule | `git submodule update --init` 한 번으로 Skia와 external을 모두 받는다 |
| `gn` | Skia 빌드 생성기. 브라우저로 받아 PATH에 두거나 경로를 인자로 준다 |
| `ninja` | 1.13 이상 |
| Python 3 | 3.9 이상. Skia의 GN 스크립트가 사용한다 |

`gn`과 `ninja`만 submodule 밖에 있다. Skia의 `bin/fetch-gn`과 `bin/fetch-ninja`는 자동 다운로드를 수행하므로 **사용하지 않는다.** 두 실행 파일을 직접 받아 둔다.

- `ninja`: `github.com/ninja-build/ninja`의 releases에서 `ninja-win.zip`
- `gn`: CIPD 패키지 페이지에서 `gn/gn/windows-amd64` 최신본

## 2. submodule 초기화

최소 구성에 필요한 것은 넷이다.

```powershell
git submodule update --init third_party/skia
git submodule update --init third_party/skia-externals/d3d12allocator
git submodule update --init third_party/skia-externals/spirv-cross
git submodule update --init third_party/skia-externals/spirv-headers
```

앱 빌드에 필요한 나머지 하나도 함께 받는다.

```powershell
git submodule update --init third_party/nlohmann-json
```

test를 빌드할 때만 Catch2가 추가로 필요하다. 앱만 빌드한다면 초기화하지 않아도 된다.

```powershell
git submodule update --init third_party/catch2
```

사내 미러를 쓰는 환경은 원격을 치환한다.

```powershell
git config --global url."https://<사내미러>/".insteadOf "https://github.com/"
```

## 3. 빌드

```powershell
scripts\build_skia.ps1 -Configuration Release
scripts\build_skia.ps1 -Configuration Debug
```

이 스크립트는 external 배치, 패치 적용, `gn gen`, `ninja`를 순서대로 수행한다. **CMake와 CTest는 이 스크립트를 호출하지 않는다.** 빌드 체계가 자동으로 취득하는 경로를 만들지 않기 위한 구분이다.

`gn`이나 `ninja`가 PATH에 없으면 경로를 준다.

```powershell
scripts\build_skia.ps1 -Configuration Release -GnPath D:\tools\gn.exe -NinjaPath D:\tools\ninja.exe
```

산출물은 `third_party/skia/out/gitman-release`와 `third_party/skia/out/gitman-debug`에 생긴다. 두 구성을 모두 빌드해야 Gitman의 Debug와 Release가 각각 링크된다.

빌드가 끝나면 검사할 수 있다.

```powershell
scripts\verify_skia_root.ps1
```

## 4. 스크립트가 하는 일

손으로 수행하거나 다른 환경에 옮길 때를 위해 내용을 남긴다.

### 4.1 external 배치

Skia는 `third_party/externals/<이름>`에서 external을 찾는다. Skia submodule을 수정하지 않기 위해 junction으로 연결한다.

```text
third_party/skia/third_party/externals/d3d12allocator  ->  third_party/skia-externals/d3d12allocator
third_party/skia/third_party/externals/spirv-cross     ->  third_party/skia-externals/spirv-cross
third_party/skia/third_party/externals/spirv-headers   ->  third_party/skia-externals/spirv-headers
```

Skia 저장소의 `.gitignore`가 `third_party/externals`를 무시하므로 submodule이 dirty로 표시되지 않는다. junction을 만들 수 없는 환경은 `-CopyExternals`로 복사한다.

`tools/git-sync-deps`는 사용하지 않는다. DEPS의 나머지 external은 아래 GN args에서 전부 꺼져 있어 필요하지 않다.

### 4.2 패치

```powershell
git -C third_party\skia apply ..\patches\skia-148-direct3d-operator-equals.patch
```

Skia 148은 `GrD3DTextureResourceInfo`와 `GrD3DBackendSurfaceInfo`의 `operator==`를 `GPU_TEST_UTILS` 가드 안에 두면서 `GrD3DBackendSurface.cpp`가 가드 밖에서 호출한다. 적용하지 않으면 Direct3D 빌드가 깨진다.

이 패치는 Skia 작업 트리를 수정하므로 `git submodule status`에 `+`가 표시된다. 정상이다.

### 4.3 `gn gen`과 `ninja`

```powershell
gn gen third_party\skia\out\gitman-release `
    --script-executable=<python 경로> `
    --args="<third_party\skia-args\gitman-release.gn 내용을 한 줄로>"
ninja -C third_party\skia\out\gitman-release skia
```

`--script-executable`이 필요한 이유는 Skia의 `.gn`이 `script_executable = "python3"`로 되어 있는데 Windows의 `python3.exe`가 Microsoft Store 스텁인 경우가 많기 때문이다.

## 5. GN args에서 주의할 점

전체 값은 `third_party/skia-args/gitman-release.gn`과 `gitman-debug.gn`에 있다. 실측에서 걸린 항목만 기록한다.

| 항목 | 이유 |
| --- | --- |
| `skia_use_system_harfbuzz = false`<br>`skia_use_system_icu = false` | 기본값이 `is_official_build && !is_canvaskit`이라 Release에서 자동으로 켜지고 Linux 경로(`/usr/include/harfbuzz`)를 참조해 실패한다 |
| `skia_use_jpeg_gainmaps = false` | 기본값이 `is_skia_dev_build`라 Debug 구성에서 켜지고, `optional("xml")`을 통해 expat external을 끌어온다 |
| `extra_cflags = [ "/MT" ]` / `[ "/MTd" ]` | Gitman의 `CMAKE_MSVC_RUNTIME_LIBRARY`와 맞춰야 한다. 어긋나면 LNK2038로 드러난다 |
| `skia_enable_fontmgr_win` | 명시하지 않는다. Windows 기본값 `true`이며 `SkFontMgr_New_DirectWrite`가 여기에 의존한다 |

## 6. 텍스트 처리 구성 (선택)

현재 Gitman은 `drawSimpleText`와 `measureText`만 사용해 shaping engine이 필요하지 않다. `SkShaper`나 `SkParagraph`를 도입할 때 이 구성을 쓴다.

`third_party/skia-args/gitman-release-text.gn`이 `skia_use_harfbuzz = true`와 `skia_use_libgrapheme = true`를 켠 변형이다. submodule 셋을 추가로 받는다.

```powershell
git submodule update --init third_party/skia-externals/harfbuzz
git submodule update --init third_party/skia-externals/libgrapheme
git submodule update --init third_party/skia-externals/unicodetools
git submodule update --init third_party/skia-externals/icu
```

full ICU(`skia_use_icu = true`)는 쓰지 않는다. Skia의 번들 ICU는 Windows에서만 `icudtl.dat`를 실행 파일 옆에서 런타임에 읽어, 단일 `.exe` 원칙과 충돌한다. `libgrapheme` backend는 데이터 파일을 만들지 않는다.

다만 libgrapheme backend도 BiDi는 ICU **소스** 20여 개 파일을 컴파일하므로 `icu` submodule은 필요하다. 데이터 파일은 쓰지 않는다.

`unicodetools`는 libgrapheme의 표 생성에만 쓰이며 작업 트리가 3.1 GB다. 실제로 읽는 것은 `unicodetools/data/ucd/15.0.0`(52 MB)뿐이므로 sparse-checkout을 권한다.

```powershell
git -C third_party\skia-externals\unicodetools sparse-checkout set unicodetools/data/ucd/15.0.0
```

`icu`만 GitHub 대응물이 없어 `chromium.googlesource.com/chromium/deps/icu`를 원격으로 쓴다. 접근이 막히면 브라우저로 `+archive/<commit>.tar.gz`를 받아 풀거나, GN 인자인 `skia_icu_bidi_third_party_dir`을 `github.com/unicode-org/icu`를 가리키는 자체 `BUILD.gn`으로 바꾼다.

## 7. Skia 버전 갱신

버전 갱신은 기능 변경과 분리한다. 한 변경에서 다음을 함께 갱신한다.

- `third_party/skia` submodule commit
- external submodule commit (Skia의 `DEPS`와 대조)
- `third_party/patches/`의 패치가 여전히 필요하고 적용되는지
- `third_party/skia-args/`의 GN args

갱신 후에는 configure, 빌드, 전체 CTest, 한국어와 Codicon 렌더링 육안 검증을 모두 수행한다.
