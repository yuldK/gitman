# ADR-001: Windows 플랫폼과 C++ 도구체인

## 상태

승인됨 - 2026-08-14 사용자 검수 반영

## 배경

Gitman은 Skia를 사용하는 C++ GUI 애플리케이션이며 지원 플랫폼을 Windows 11로 제한할 수 있다. 창, 입력, DPI, 클립보드, 프로세스 실행을 제공할 네이티브 경계와 재현 가능한 빌드 기준선이 필요하다.

## 결정

- Windows 11 x64 데스크톱만 최초 지원한다.
- Win32 API로 `wWinMain`, `HWND`, 메시지 pump, DPI, 클립보드, 파일 dialog, 프로세스 기능을 구현한다.
- 애플리케이션 언어 표준은 C++20으로 고정하고 compiler extension은 기본적으로 끈다.
- 최소 컴파일러는 Visual Studio 2022 17.10의 MSVC 19.40으로 한다.
- 최소 Windows SDK는 10.0.22621로 한다.
- 최소 CMake는 4.2.0으로 한다.
- 프로젝트 공용 설정은 `CMakePresets.json`, 개발자 로컬 설정은 추적하지 않는 `CMakeUserPresets.json`에 둔다.
- `Visual Studio 17 2022`와 `Visual Studio 18 2026` generator preset을 분리하고, 공통 cache 설정을 상속한다.
- CPU 래스터 surface를 최소 호환 기준선으로 유지하되 기본 renderer는 Direct3D GPU surface로 한다.
- renderer 설정은 `auto`, `direct3d`, `cpu`를 제공한다. 기본값 `auto`는 Direct3D를 먼저 초기화하고 장치 또는 초기화 실패 시 CPU로 fallback한다. `cpu`는 사용자가 명시적으로 GPU를 우회한다.
- Win32 API는 `src/platform/win32`와 전용 adapter 뒤에 최대한 격리한다. domain, application, presentation의 업무 모델은 Win32 type과 UTF-16 문자열을 노출하지 않는다.
- 로직의 문자열과 경로 표현은 UTF-8 기반 `std::u8string`을 사용한다. Win32의 wide API가 필요한 지점에서만 `std::u8string`과 UTF-16을 엄격하게 변환하고 변환 오류를 구조화된 오류로 반환한다.
- 기본 Windows caption을 그대로 사용하지 않고 Skia client area가 title bar와 caption을 포함하는 일체형 UI를 렌더링한다.
- custom caption은 이동, 가장자리 resize, 최소화, 최대화 및 복원, 닫기, 더블 클릭 최대화, `Alt+Space` system menu, Windows 11 Snap Layout, DPI, 고대비와 키보드 동작을 유지한다.
- UWP와 app package는 지원하지 않는다. 애플리케이션 dependency와 Codicons 자산을 정적으로 연결하거나 실행 파일 resource에 포함하여 Gitman 자체는 단일 `.exe`로 배포한다.
- Git과 SVN CLI는 시스템에 별도로 설치되는 외부 도구이며 단일 실행 파일 안에 번들하지 않는다.
- CMake `install()` 규칙을 제공하고 공용 preset의 `CMAKE_INSTALL_PREFIX`를 `${sourceDir}/bin`으로 둔다. `install(TARGETS gitman RUNTIME DESTINATION .)`에 해당하는 규칙으로 `${workspaceRoot}/bin/gitman.exe`를 만든다.
- build tree와 install tree를 분리하고 배포 검증은 build 산출물이 아니라 `cmake --install` 결과를 대상으로 한다.

## 현재 검증 환경

| 항목 | 확인 결과 |
| --- | --- |
| 운영체제 대상 | Windows 11 x64, 검증 호스트 build 26200 |
| Visual Studio | 2022 17.14.31 Community, 2026 18.8.0 Community |
| MSVC | 19.44.35226, 19.51.36248 |
| Windows SDK | target baseline 10.0.22621.0, 실제 선택 10.0.26100.0 |
| CMake | 4.2.0 |
| vcpkg | baseline `b9a5010d499952121b0f1a40eb98963c37da32dc` |
| configure와 build | VS2022 Debug/Release, VS2026 Debug 통과 |
| renderer | Direct3D, CPU, auto fallback smoke test 통과 |
| install | `bin/gitman.exe` 단일 파일 생성 |

단계 1에서 실제 configure, compile, GPU 초기화, CPU fallback과 install을 검증했다. 전체 명령과 결과는 `docs/verification/2026-08-14-stage-1.md`에 기록한다.

## 고려한 대안

### 크로스 플랫폼 창 라이브러리

지원 플랫폼이 Windows 11로 고정되어 있어 창 수명 주기와 입력을 Win32로 직접 관리하는 편이 요구사항과 일치한다. 추가 창 라이브러리는 채택하지 않는다.

### CPU surface만 제공

호환성은 높지만 기본 사용자 경험과 대량 카드 렌더링 성능 목표에 맞지 않는다. CPU는 fallback과 명시적 선택 경로로 유지하고 Direct3D를 기본으로 사용한다.

### 기본 Windows caption 유지

구현은 단순하지만 앱의 카드 및 도구 모음과 시각적으로 분리된다. Skia로 전체 caption을 렌더링하되 Windows의 비클라이언트 동작과 접근성 계약을 adapter에서 재현한다.

### Ninja 전용 빌드

현재 환경에 Ninja가 없고 Visual Studio generator만으로 요구사항을 충족할 수 있으므로 필수 도구로 두지 않는다.

## 결과

- 플랫폼 계층은 `src/platform/win32`에 격리하고 로직은 `std::u8string`만 사용한다.
- x86, ARM64, UWP, MSIX는 지원 범위가 아니다.
- Direct3D와 CPU renderer가 한 실행 파일에 포함된다.
- 단계 1은 Visual Studio generator용 preset, GPU 기본 및 CPU 강제 Skia smoke test, `${sourceDir}/bin` install을 제공해야 한다.
- 실행에 필요한 Codicons와 애플리케이션 자산은 실행 파일에 포함한다.

## 검증 방법

- CMake 최소 버전 미만에서 명확히 실패하는지 확인한다.
- Visual Studio 2022 이상에서 x64 configure와 build를 실행한다.
- 기본 실행에서 Direct3D surface를 사용하고 GPU 초기화 실패 및 명시적 CPU 설정에서 CPU surface를 사용하는지 확인한다.
- 로직 및 공개 업무 interface에 `HWND`, `wchar_t`, `std::wstring`이 노출되지 않는지 검사한다.
- 공백, 한글, 확장 Unicode가 포함된 `std::u8string` 경로의 Win32 왕복 변환을 검사한다.
- Per-Monitor V2 DPI에서 custom caption, drag, resize, caption button, system menu와 Snap Layout을 확인한다.
- `cmake --install` 후 `${sourceDir}/bin`에 외부 asset 또는 프로젝트 DLL이 필요 없는 `gitman.exe`가 생성되는지 확인한다.

## 근거 자료

- [CMake Presets 공식 문서](https://cmake.org/cmake/help/latest/manual/cmake-presets.7.html)
- [MSVC 언어 표준 옵션](https://learn.microsoft.com/cpp/build/reference/std-specify-language-standard-version)
- [Win32 데스크톱 애플리케이션](https://learn.microsoft.com/windows/win32/desktop-programming)
- [DWM custom window frame](https://learn.microsoft.com/windows/win32/dwm/customframe)
- [CMake install 명령](https://cmake.org/cmake/help/latest/command/install.html)
