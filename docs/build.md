# 빌드 및 실행 안내

## 1. 필수 환경

- Windows 11 x64
- CMake 4.2.0 이상
- Visual Studio 2022 17.10 이상 또는 Visual Studio 2026
- Desktop development with C++ workload
- Windows SDK 10.0.22621.0 이상
- Git

Ninja와 GN은 필요하지 않다. Skia는 직접 빌드하지 않고 luil의 스크립트가 미리 빌드된 패키지를 설치한다 (2.2). Python은 앱 아이콘을 다시 만들 때만 쓴다 (5장).

## 2. 의존성 준비

의존성은 submodule과 해시가 고정된 패키지 아카이브로만 구성한다. 빌드 체계는
아무것도 내려받지 않는다. 근거는 [ADR-006](decisions/ADR-006-restricted-network-dependency-provisioning.md)과
[ADR-007](decisions/ADR-007-luil-ui-framework-adoption.md)에 있다.

### 2.1 submodule과 luil 패키지

UI 프레임워크 luil은 `third_party/luil` submodule이다. luil이 쓰는 Skia 패키지와
WebView2 SDK는 luil의 스크립트가 준비한다. 두 스크립트는 사람이 실행하며 핀 파일
(`third_party/luil/third_party/{skia-prep,webview2-prep}.json`)의 SHA-256과 맞는
아카이브만 설치한다.

```powershell
git submodule update --init third_party/nlohmann-json
git submodule update --init third_party/luil
git -C third_party/luil submodule update --init third_party/nlohmann_json
third_party\luil\scripts\fetch_skia.ps1 -Configuration Debug,Release
third_party\luil\scripts\fetch_webview2.ps1
```

제한된 환경에서는 브라우저로 아카이브를 받아
`third_party/luil/third_party/skia-prep-archives/`와 `webview2-prep-archives/`에
두고 같은 스크립트를 실행한다. 해시가 맞는 아카이브가 있으면 스크립트는 네트워크를
쓰지 않는다. 접근 대상은 `github.com/yuldK/skia-prep` 릴리스와 `api.nuget.org`다.

Catch2는 test를 빌드할 때만 필요하다. 앱만 빌드한다면 초기화하지 않아도 configure와
빌드가 성립한다.

```powershell
git submodule update --init third_party/catch2
```

사내 미러를 쓰는 환경은 `git config --global url."https://<사내미러>/".insteadOf
"https://github.com/"`로 원격을 치환한다.

### 2.2 Skia 패키지

Skia는 직접 빌드하지 않는다. `fetch_skia.ps1`이 설치하는 패키지
(`third_party/luil/third_party/skia-prep/`)는 luil이 정한 기능 집합으로 미리 빌드된
정적 라이브러리·헤더·`NOTICE.md`이며 Debug(`/MTd`)·Release(`/MT`) 산출물이 따로
있다. 패키지의 내용, 빌드 옵션과 갱신 절차는 luil 저장소의 `docs/skia-build.md`에
있다. gitman의 CMake는 패키지 위치를 바꾸는 변수를 두지 않으며, configure는
패키지의 존재만 검사한다.

## 3. Configure, build와 test

### 3.1 앱 빌드 (기본)

기본 preset은 실제 실행 파일에 필요한 target만 만든다. 솔루션에는 `gitman`(실행
파일), `gitman_lib`(앱 코드 전체)와 Visual Studio 기본 target만 보인다.

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
cmake --build --preset vs2022-release
```

Visual Studio 2026은 `vs2026`, `vs2026-debug`, `vs2026-release`를 같은 방식으로
사용한다.

### 3.2 Test 빌드

test target과 CTest 등록은 `GITMAN_BUILD_TESTS`가 켜져 있을 때만 만들어진다. 전용
preset이 별도 binary directory(`build/vs2022-tests`)에 test 솔루션을 만들어 앱 전용
솔루션과 나란히 유지된다.

```powershell
cmake --preset vs2022-tests
cmake --build --preset vs2022-tests-debug
ctest --preset vs2022-tests-debug
cmake --build --preset vs2022-tests-release
ctest --preset vs2022-tests-release
```

기존 build directory에 직접 켜려면 `-DGITMAN_BUILD_TESTS=ON`을 준다. 개발 도구
target(`gitman_format`, `gitman_format_check`)은 `-DGITMAN_BUILD_TOOLING=ON`이며
test preset은 두 flag를 모두 켠다.

Skia 패키지와 애플리케이션은 같은 정적 CRT(`/MT`·`/MTd`)로 빌드돼야 한다. 패키지는 정적 CRT로 빌드되어 있고 최상위 `CMakeLists.txt`가 `CMAKE_MSVC_RUNTIME_LIBRARY`를 같은 값으로 고정한다. 어긋나면 LNK2038로 드러난다.

## 4. 정적 분석과 포맷 검사

```powershell
cmake --preset vs2022-analysis
cmake --build --preset vs2022-analysis
cmake --build build\vs2022-tests --target gitman_format_check --config Debug
```

포맷·style 검사는 CMake target 없이 스크립트로도 바로 실행할 수 있다.

```powershell
scripts\check_source_style.ps1 -root .
```

`gitman_format_check`는 [`docs/code_style.md`](code_style.md)의 자동화 가능한 항목을 검사한다. clang-format은 Allman 중괄호, namespace 내부 1단계 들여쓰기, 짧은 제어문 본문의 한 줄 배치 방지 및 가능한 중괄호 제거, 조건 연산자의 새 줄 시작, 생성자 초기화 목록의 새 줄 쉼표, `template<...>` 선언과 함수 signature 분리, 중괄호 초기화 형식을 적용한다. source style 검사는 UTF-8 무 BOM, CRLF, tab 및 줄 끝 공백, C++ type과 namespace의 `snake_case`, template/signature의 같은 줄 배치와 여러 줄 표현식의 마지막 닫는 기호가 독립된 줄에 있지 않은 경우를 실패로 보고한다. 부정 연산자 지양은 코드 리뷰 기준이다.

Visual Studio, MSBuild, CTest와 외부 도구 사이의 Windows code page 차이로 로그가
손상되지 않도록 CMake 및 검사 스크립트의 빌드 로그와 `OutputDebugStringW` 개발자
진단은 영문 ASCII로 출력한다. 한국어는 주석, 문서와 사용자에게 표시되는 UI
문구에 유지한다.

## 5. Install

```powershell
cmake --install build\vs2022 --config Release
```

결과는 다음 단일 파일이다.

```text
bin/gitman.exe
```

luil(Skia·WebView2 로더 포함)과 C/C++ runtime은 정적으로 연결되고 Codicons font, Codicons license, 앱 아이콘과 제3자 고지문은 실행 파일 resource에 들어간다. 코디콘 폰트·라이선스·고지문은 luil의 `.rc` 조각(ID 101~104)이 싣고, 고지문은 luil의 생성기가 Skia 패키지의 `NOTICE.md`, WebView2 SDK, nlohmann/json, Codicons 라이선스 원문에서 만든다. gitman의 `.rc`는 manifest·아이콘(ID 105)·버전 정보만 담는다. Windows 시스템 DLL과 향후 단계에서 사용할 외부 Git/SVN CLI는 단일 파일 범위에서 제외한다.

앱 아이콘은 현재 custom caption에서 사용하는 `source-control` Codicon을 기준으로 생성한다. Codicons 자산은 luil의 것(`third_party/luil/assets/codicons/`)을 쓰며, 그 자산이 갱신된 뒤 아이콘도 다시 만들려면 저장소 루트에서 다음을 실행한다 (Python 3.9 이상과 Pillow).

```powershell
python .\scripts\generate_app_icon.py `
    --font .\third_party\luil\assets\codicons\codicon.ttf `
    --mapping .\third_party\luil\assets\codicons\mapping.json `
    --output .\assets\gitman.ico
```

## 6. Renderer 선택

```powershell
bin\gitman.exe --renderer=auto
bin\gitman.exe --renderer=direct3d
bin\gitman.exe --renderer=cpu
```

- `auto`: 기본값이다. Direct3D를 먼저 초기화하고 실패하면 CPU로 전환한다.
- `direct3d`: Direct3D만 사용하며 자동 CPU 전환을 허용하지 않는다.
- `cpu`: GPU 초기화를 건너뛰고 CPU raster surface를 사용한다.

`--smoke-test`와 `--simulate-direct3d-failure`는 CTest용 진단 옵션이다. 실패 주입은 smoke test와 함께 사용할 때만 허용한다.

## 7. Codicons와 luil 갱신

Codicons 자산(font·mapping·license)은 luil의 것이다 (`third_party/luil/assets/codicons/`,
현재 `v0.0.46-24`). gitman은 자산을 따로 두지 않으며 갱신은 luil 저장소에서 한다.
luil을 올릴 때는 submodule 핀을 새 커밋으로 옮기고, Skia·WebView2 패키지 핀이 바뀌었으면
2.1의 스크립트를 다시 실행하며, `tests/dependency_versions_tests.cpp`의 Skia milestone과
[ADR-007](decisions/ADR-007-luil-ui-framework-adoption.md)의 핀 기록을 함께 맞춘다.
