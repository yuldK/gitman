# Gitman

Gitman은 여러 Git 및 SVN 작업 경로의 상태를 한 화면에서 확인하는 Windows용 데스크톱 애플리케이션입니다. JSON 형식의 `.version-list` 문서에 저장소를 등록하고, 각 저장소의 브랜치 또는 URL, 리비전, 작업 트리 상태와 원격 동기화 상태를 카드 형태로 표시합니다.

## 개요

- Windows 11 x64에서 실행되는 C++20 Win32 네이티브 애플리케이션입니다.
- Skia로 화면을 렌더링하며 Direct3D를 우선 사용하고, 초기화에 실패하면 CPU 렌더링으로 전환합니다.
- Git과 SVN 저장소를 하나의 `.version-list` 작업공간 문서로 관리합니다.
- 저장소가 모인 폴더를 스캔하여 새 작업공간 문서를 만들거나 기존 문서를 열 수 있습니다.
- 전체 저장소 또는 개별 저장소의 상태를 명시적으로 새로 고칠 수 있습니다.
- 빌드 결과는 의존성과 UI 자산을 포함한 단일 `gitman.exe` 파일입니다. Git과 SVN 명령줄 도구는 별도로 설치되어 있어야 합니다.

현재 버전의 GUI는 작업공간 관리, 상태 조회와 새로 고침을 중심으로 제공합니다. 업데이트와 전환 버튼은 아직 비활성화되어 있습니다.

## 목적

Gitman은 여러 저장소를 오가며 각각의 상태를 반복해서 확인해야 하는 작업을 줄이는 것을 목적으로 합니다. Git과 SVN이 섞인 작업공간에서도 같은 형식으로 상태를 살펴볼 수 있으며, 원격 조회는 명시적인 새로 고침에서만 실행하여 예상하지 않은 네트워크 작업을 피합니다.

또한 자격 증명을 저장하거나 강제 전환, 자동 stash, 충돌 해결과 같은 위험한 변경을 수행하지 않는 것을 기본 원칙으로 삼습니다. UI, 애플리케이션 로직과 외부 프로세스 실행을 분리하여 저장소가 많거나 조회가 느린 상황에서도 창이 응답성을 유지하도록 설계되어 있습니다.

## 사용 방법

### 1. 개발 환경 준비

다음 환경이 필요합니다.

- Windows 11 x64
- CMake 4.2.0 이상
- Visual Studio 2022 17.10 이상
  - `Desktop development with C++` 워크로드
  - Windows SDK 10.0.22621.0 이상
- Git
- SVN 저장소를 사용할 경우 SVN 명령줄 도구

의존성은 submodule과 직접 빌드한 Skia로 구성합니다. 빌드 체계가 외부에서 무엇인가를 내려받지 않으므로 네트워크가 통제된 환경에서도 빌드할 수 있습니다.

```powershell
git submodule update --init third_party/nlohmann-json
git submodule update --init third_party/skia
git submodule update --init third_party/skia-externals/d3d12allocator
git submodule update --init third_party/skia-externals/spirv-cross
git submodule update --init third_party/skia-externals/spirv-headers
```

Skia는 한 번 직접 빌드합니다. `gn`과 `ninja` 실행 파일이 필요합니다.

```powershell
.\scriptsuild_skia.ps1 -Configuration Release
.\scriptsuild_skia.ps1 -Configuration Debug
```

절차와 GN 옵션의 세부는 [Skia 준비 안내](docs/skia-build.md), 전체 빌드 안내는 [빌드 및 실행 안내](docs/build.md)를 참고합니다. 테스트를 빌드할 때만 Catch2 submodule이 추가로 필요합니다.

### 2. 빌드 및 설치

Visual Studio 2022 Release 구성을 생성하고 빌드한 뒤 설치합니다.

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-release
cmake --install build\vs2022 --config Release
```

설치가 끝나면 다음 실행 파일이 생성됩니다.

```text
bin\gitman.exe
```

Visual Studio 2026을 사용하는 경우 `vs2022`가 포함된 preset 이름을 각각 `vs2026`으로 바꿉니다.

### 3. 작업공간 문서 만들기

Gitman을 인자 없이 실행합니다.

```powershell
.\bin\gitman.exe
```

오른쪽 위의 새 문서 버튼을 누르고 다음 값을 입력합니다.

1. 새 `.version-list` 문서의 이름을 입력합니다.
2. Git 또는 SVN 저장소들을 담고 있는 폴더를 선택합니다.
3. 문서를 저장할 위치를 선택한 뒤 생성합니다.

선택한 폴더의 바로 아래 하위 폴더만 스캔하며, 발견한 Git 및 SVN 저장소를 새 문서에 자동으로 등록합니다. 같은 경로에 있는 기존 문서는 덮어쓰지 않습니다.

작업공간 문서를 직접 작성할 수도 있습니다. 최소 형식은 다음과 같습니다.

```json
{
    "schema_version": 1,
    "projects": [
        {
            "id": "frontend",
            "path": "repositories/frontend",
            "display_name": "Frontend",
            "vcs_hint": "git"
        },
        {
            "id": "legacy-server",
            "path": "D:/repositories/legacy-server",
            "display_name": "Legacy Server",
            "vcs_hint": "svn"
        }
    ]
}
```

`id`는 문서 안에서 고유해야 합니다. 상대 경로는 `.version-list` 문서가 있는 폴더를 기준으로 해석하며, `display_name`과 `vcs_hint`는 생략할 수 있습니다.

### 4. 작업공간 열기와 상태 확인

다음 방법 중 하나로 문서를 엽니다.

- 오른쪽 위의 폴더 버튼에서 `.version-list` 파일을 선택합니다.
- `.version-list` 파일을 Gitman 창으로 끌어다 놓습니다.
- 실행할 때 문서 경로를 인자로 전달합니다.

```powershell
.\bin\gitman.exe "D:\workspaces\team.version-list"
```

문서를 열면 먼저 로컬 상태를 조회합니다. 원격 상태까지 확인하려면 툴바의 전체 새로 고침 버튼이나 `F5`를 사용합니다. 카드의 새로 고침 버튼은 해당 저장소만 조회하며, 키보드로 카드를 선택한 뒤 `Enter`를 눌러도 같은 동작을 실행합니다.

카드는 끌어다 놓아 순서를 바꿀 수 있으며 변경된 순서는 문서에 저장됩니다. 툴바의 경로 표시 버튼으로 절대 경로와 문서 기준 상대 경로를 전환할 수 있습니다.

### 5. 렌더러 선택

기본값인 `auto`는 Direct3D를 먼저 사용하고 실패하면 CPU 렌더링으로 전환합니다. 문제를 진단하거나 렌더러를 고정하려면 다음 옵션을 사용합니다.

```powershell
.\bin\gitman.exe --renderer=auto
.\bin\gitman.exe --renderer=direct3d
.\bin\gitman.exe --renderer=cpu
```

## 테스트

테스트는 앱 빌드와 분리된 전용 preset을 사용합니다.

```powershell
cmake --preset vs2022-tests
cmake --build --preset vs2022-tests-debug
ctest --preset vs2022-tests-debug
```

Release 구성도 같은 방식으로 실행할 수 있습니다.

```powershell
cmake --build --preset vs2022-tests-release
ctest --preset vs2022-tests-release
```

테스트 모음은 작업공간 JSON 처리, Git·SVN 명령 생성과 출력 해석, 프로세스 실행과 취소, 동시 작업 스케줄링, UI 상호작용, 렌더러 smoke test, 자산 checksum과 소스 스타일을 포함합니다. 특정 테스트만 실행하려면 CTest의 정규식 필터를 사용합니다.

```powershell
ctest --preset vs2022-tests-debug -R git_status
```

포맷과 소스 스타일만 별도로 검사할 수도 있습니다.

```powershell
.\scripts\check_source_style.ps1 -root .
cmake --build build\vs2022-tests --target gitman_format_check --config Debug
```

프로젝트의 상세 요구사항과 구현 기준은 [요구사항 기준선](docs/requirements.md), 코드 규칙은 [코드 스타일](docs/code_style.md)에서 확인할 수 있습니다.
