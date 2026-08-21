# Gitman

Gitman은 여러 Git 및 SVN 작업 경로의 상태와 변경 작업을 한 화면에서 관리하는 Windows용 데스크톱 애플리케이션입니다. JSON 형식의 `.version-list` 작업공간 문서에 저장소를 등록하고, 각 저장소의 브랜치 또는 URL, 리비전, 작업 트리 상태와 원격 동기화 상태를 카드 형태로 표시합니다.

## 개요

- Windows 11 x64에서 실행되는 C++20 Win32 네이티브 애플리케이션입니다.
- Skia로 화면을 렌더링하며 Direct3D를 우선 사용하고, 초기화에 실패하면 CPU 렌더링으로 전환합니다.
- Git과 SVN 저장소를 하나의 `.version-list` 문서로 관리합니다.
- 전체 또는 개별 저장소의 상태 새로 고침, Git pull/SVN update, Git branch/SVN URL 전환을 제공합니다.
- 카드별 변경 로그와 선택 카드 전용 하단 로그 창을 제공합니다.
- 설정과 창 배치를 작업공간 문서에 저장하고, `.version-list` Windows 파일 연결을 현재 사용자 범위에서 관리할 수 있습니다.
- 배포 산출물은 의존성과 UI 자산을 포함한 단일 `gitman.exe` 파일입니다. Git과 SVN 명령줄 도구는 해당 저장소를 사용할 때 별도로 설치하면 됩니다.

최종 검증(`S8-V1`, 2026-08-19) 기준으로 VS2022·VS2026 빌드, CTest 626개, 정적 분석, 설치본 smoke test와 파일 연결 등록·제거를 확인했습니다. 실제 SVN CLI와 네트워크 드라이브는 아직 보증 범위에 포함하지 않습니다.

## 목적

Gitman은 여러 저장소를 오가며 각각의 상태를 반복해서 확인하고 변경 작업을 실행해야 하는 부담을 줄이는 것을 목적으로 합니다. Git과 SVN이 섞인 작업공간에서도 공통 카드와 로그 형식으로 상태를 비교할 수 있으며, 원격 조회는 명시적인 새로 고침이나 변경 작업 후에 실행합니다.

변경 작업은 비대화형 외부 프로세스로 실행하고, 실행 직전에도 작업 트리와 대상 브랜치를 다시 검증합니다. 자격 증명을 저장하지 않으며, 강제 전환·자동 stash·커밋·푸시·병합·리베이스·충돌 해결은 수행하지 않습니다. UI, 애플리케이션 로직과 외부 프로세스 실행을 분리하여 저장소가 많거나 조회가 느린 상황에서도 창의 응답성을 유지하도록 설계되어 있습니다.

## 사용 방법

### 1. 실행 환경

앱을 실행하려면 다음 환경이 필요합니다.

- Windows 11 x64
- Git 2.43.0 이상 CLI (Git 저장소를 사용할 때 선택 사항)
- SVN 1.14.5 이상 CLI (SVN 저장소를 사용할 때 선택 사항)

Git과 SVN 실행 파일은 `PATH`에서 자동 탐색합니다. 열린 `.version-list` 문서의 환경설정에서 파일 선택기로 절대 경로를 지정할 수도 있습니다. 경로를 지우면 자동 탐색으로 돌아갑니다.

### 2. 개발 환경과 의존성 준비

소스에서 빌드하려면 다음이 추가로 필요합니다.

- CMake 4.2.0 이상
- Visual Studio 2022 17.10 이상 또는 Visual Studio 2026
  - `Desktop development with C++` 워크로드
  - Windows SDK 10.0.22621.0 이상
- Python 3.9 이상
- Skia를 빌드할 GN과 Ninja 1.13 이상

CMake가 의존성을 자동으로 내려받지는 않습니다. submodule과 사용자가 직접 빌드한 Skia를 준비합니다.

```powershell
git submodule update --init `
    third_party/nlohmann-json `
    third_party/skia `
    third_party/skia-externals/d3d12allocator `
    third_party/skia-externals/spirv-cross `
    third_party/skia-externals/spirv-headers
```

테스트까지 빌드하려면 Catch2 submodule도 초기화합니다.

```powershell
git submodule update --init third_party/catch2
```

Skia는 Debug와 Release를 각각 한 번씩 빌드합니다.

```powershell
.\scripts\build_skia.ps1 -Configuration Debug
.\scripts\build_skia.ps1 -Configuration Release
.\scripts\verify_skia_root.ps1
```

GN과 Ninja는 스크립트가 `third_party\skia-tools`, Skia 트리, Visual Studio 설치본, `PATH` 순으로 찾습니다. 어디에도 없다면 `-FetchTools`로 Skia의 `bin/fetch-gn`·`bin/fetch-ninja`를 한 번 실행해 받거나, `-GnPath`와 `-NinjaPath`로 실행 파일 경로를 지정합니다. 자세한 GN 옵션과 제한된 네트워크 환경에서의 준비 방법은 [Skia 준비 안내](docs/skia-build.md)를 참고합니다.

### 3. 빌드 및 설치

Visual Studio 2022 Release 구성을 생성하고 빌드한 뒤 설치합니다.

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-release
cmake --install build\vs2022 --config Release
```

Visual Studio 2026은 `vs2022`가 포함된 preset 이름을 각각 `vs2026`으로 바꿉니다. 설치 결과는 다음 단일 파일입니다.

```text
bin\gitman.exe
```

### 4. 작업공간 문서 만들기

Gitman을 인자 없이 실행합니다.

```powershell
.\bin\gitman.exe
```

오른쪽 위의 새 문서 버튼을 누르고 문서 이름, 저장소들을 담고 있는 폴더, 출력 위치를 선택합니다. 선택한 폴더의 바로 아래 하위 폴더만 스캔하며, 발견한 Git·SVN 저장소를 새 문서에 등록합니다. 기존 문서는 덮어쓰지 않습니다.

이미 열린 문서에 저장소를 추가하려면 상단의 하위 저장소 탐색·등록 버튼을 사용합니다. 스캔 폴더를 선택하면 깊이 1 후보와 제외 사유가 표시되고, 등록할 후보만 체크해 문서에 원자적으로 추가할 수 있습니다.

작업공간 문서를 직접 작성할 수도 있습니다. 최소 형식은 다음과 같습니다.

```json
{
    "schema_version": 1,
    "settings": {
        "git_executable": "",
        "svn_executable": "",
        "show_relative_paths": false
    },
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

`id`는 문서 안에서 고유해야 합니다. 상대 경로는 `.version-list` 문서가 있는 폴더를 기준으로 해석합니다. `display_name`, `vcs_hint`, `settings`는 생략할 수 있으며, `vcs_hint`는 `auto`, `git`, `svn` 중 하나입니다. 기존 문서의 `svn_switch_targets` 배열은 호환을 위해 보존되지만 현재 전환 후보에는 사용되지 않습니다.

### 5. 문서 열기와 상태 확인

문서를 열지 않은 상태에서는 시작 페이지가 표시됩니다. 왼쪽에는 `문서 열기…`와 `새 문서 만들기…`가 있고, 오른쪽에는 최근에 연 문서가 최대 10개까지 최신 순으로 표시됩니다. 항목을 누르면 그 문서를 열고, 오른쪽 `x` 아이콘은 목록에서만 제거합니다. 최근 목록은 `gitman.exe`와 같은 폴더의 `gitman.app-settings.json`에 저장되며, 실행 파일 폴더에 쓸 수 없으면 목록은 그 실행 동안에만 유지됩니다.

다음 방법 중 하나로 문서를 엽니다.

- 오른쪽 위의 폴더 버튼에서 `.version-list` 파일을 선택합니다.
- `.version-list` 파일을 Gitman 창으로 끌어다 놓습니다.
- 실행할 때 문서 경로를 인자로 전달합니다.
- 파일 연결을 등록한 뒤 Explorer에서 문서를 더블클릭합니다.

```powershell
.\bin\gitman.exe "D:\workspaces\team.version-list"
```

문서를 닫으려면 도구 막대 오른쪽 끝의 닫기 버튼을 누릅니다. 닫으면 시작 페이지로 돌아가며, 문서가 열려 있는 동안에는 새 문서 만들기 버튼이 보이지 않습니다.

문서를 열면 먼저 로컬 상태를 조회합니다. 도구 막대 왼쪽 끝의 전체 새로 고침 버튼이나 `F5`를 누르면 원격 상태까지 확인합니다. 카드의 새로 고침 버튼은 해당 저장소만 조회하며, 카드를 선택한 뒤 `Enter`를 눌러도 같은 동작을 실행합니다.

카드는 끌어다 놓아 순서를 바꿀 수 있으며 변경된 순서는 문서에 저장됩니다. 경로 표시 버튼으로 절대 경로와 문서 기준 상대 경로를 전환할 수 있습니다.

### 6. 업데이트와 전환

- Git 카드의 업데이트는 `git pull --ff-only`를 사용하며, 확인 창에서 recursive submodule 갱신 여부를 선택할 수 있습니다.
- SVN 카드는 `svn update`를 실행합니다.
- 실행 중 업데이트 버튼은 취소 버튼으로 바뀝니다. 성공·실패·취소 결과와 외부 명령 출력은 해당 카드 로그에 남고, 작업 후 상태를 자동으로 다시 조회합니다.
- Git 전환 창은 원격 branch를 우선 표시한 뒤 local branch를 표시합니다. local tracking branch가 새로 필요하면 명시적인 확인을 받습니다.
- SVN 전환 창은 저장소 루트부터 디렉터리 트리를 lazy 조회합니다. 현재 위치까지
  자동으로 펼치며 선택 대상은 실행 직전에 같은 저장소인지 다시 확인합니다.
- dirty 작업 트리, 이미 사용 중인 대상과 대상 불일치는 실행 전에 차단합니다. 외부 명령은 비대화형으로 실행해 인증 프롬프트 없이 실패를 로그에 남깁니다.

### 7. 로그와 환경설정

카드를 선택하면 하단에 해당 카드의 변경 로그가 표시됩니다. 로그 창에는 전체·출력·오류 스트림 필터, 자동 스크롤, 복사, 지우기, 스크롤 막대가 있으며 연속 progress 출력은 화면에서 접혀 표시됩니다. 화면 로그는 카드당 최대 1,000개 record의 메모리 ring buffer입니다.

같은 내용을 문서 폴더에 파일로도 남깁니다. 문서가 `D:\workspaces	eam.version-list`이면 `D:\workspaces	eam.version-list.log` 폴더가 만들어지고, 저장소마다 하위 폴더 하나에 `20260821-184012.log` 형태의 파일이 쌓입니다. 파일은 문서를 연 뒤 그 저장소에 첫 로그가 생길 때 하나 만들어 앱이 끝날 때까지 이어 씁니다. 저장소 폴더 이름은 작업 복사본의 마지막 폴더 이름이며, 문서 안에서 겹치면 상위 폴더를 앞에 붙이고(`a-b-c`) 그래도 겹치면 드라이브까지 붙입니다(`c-drive_a-b-c`). 오래된 로그는 지우지 않으므로 필요할 때 직접 정리합니다. 환경설정의 `로그를 문서 폴더에 파일로 남김`을 끄면 폴더를 만들지 않습니다.

문서가 열린 상태에서 환경설정 버튼을 누르면 Git·SVN 실행 파일 경로를 찾아보거나 지울 수 있습니다. 저장하면 설정을 문서에 원자적으로 기록하고 도구를 다시 탐색한 뒤 카드 전체를 새로 고칩니다.

### 8. 파일 연결

`.version-list`를 현재 Windows 사용자 계정에 연결하려면 관리자 권한 없이 다음을 실행합니다.

```powershell
.\bin\gitman.exe --register-file-association
```

연결 제거는 다음 명령으로 수행합니다.

```powershell
.\bin\gitman.exe --unregister-file-association
```

두 작업은 환경설정 창의 `연결 등록`·`연결 해제` 버튼으로도 수행할 수 있으며, 결과는 앱 스타일 알림 창으로 표시됩니다. 실행 파일을 다른 위치로 옮겼다면 연결을 다시 등록해야 새 경로가 반영됩니다. 등록은 `HKCU\Software\Classes`에만 적용되며, 다른 프로그램이 선택한 사용자 연결은 제거하지 않습니다.

### 9. 렌더러 선택

기본값인 `auto`는 Direct3D를 먼저 사용하고 실패하면 CPU 렌더링으로 전환합니다. 문제를 진단하거나 렌더러를 고정하려면 다음 옵션을 사용합니다.

```powershell
.\bin\gitman.exe --renderer=auto
.\bin\gitman.exe --renderer=direct3d
.\bin\gitman.exe --renderer=cpu
```

## 테스트

테스트는 앱 빌드와 분리된 전용 preset을 사용합니다. Skia Debug·Release와 Catch2 submodule이 준비되어 있어야 합니다.

```powershell
cmake --preset vs2022-tests
cmake --build --preset vs2022-tests-debug
ctest --preset vs2022-tests-debug
```

Release 구성도 같은 방식으로 실행합니다.

```powershell
cmake --build --preset vs2022-tests-release
ctest --preset vs2022-tests-release
```

테스트 모음은 작업공간 JSON, 경로와 schema, Git·SVN 명령과 출력 파서, 실제 Git update·switch 통합, 프로세스 실행·취소, 동시 작업과 로그 격리, UI 상호작용, 환경설정·탐색 dialog, 파일 연결, 렌더러 smoke test, 자산 checksum을 포함합니다. 최종 단계 8 검증에서는 전체 CTest 626개, VS2022 Debug/Release와 VS2026 구성, ASan, 3회 반복을 확인했습니다.

정적 분석과 포맷·소스 스타일 검사는 다음과 같이 실행합니다.

```powershell
cmake --preset vs2022-analysis
cmake --build --preset vs2022-analysis
cmake --build build\vs2022-tests --target gitman_format_check --config Debug
.\scripts\check_source_style.ps1 -root .
```

특정 테스트만 실행하려면 CTest의 정규식 필터를 사용합니다.

```powershell
ctest --preset vs2022-tests-debug -R Git
```

## 배포와 데이터

Gitman은 installer나 MSIX 없이 `bin\gitman.exe`를 원하는 위치에 복사해 배포합니다. Skia와 C/C++ runtime은 정적으로 연결되고, Codicons·앱 아이콘·제3자 라이선스 고지문은 실행 파일 resource에 포함됩니다. Git/SVN CLI는 외부 prerequisite입니다.

사용자 데이터는 다음 위치에 저장됩니다.

- 프로젝트 목록, 실행 파일 경로, 경로 표시 방식, 창 배치: 사용자가 만든 `.version-list` JSON 문서
- 최근에 연 문서 목록: `gitman.exe`와 같은 폴더의 `gitman.app-settings.json`
- 작업 로그: 화면은 메모리 전용(카드당 1,000 record), 파일은 `<이름>.version-list.log` 폴더의 저장소별 `타임스탬프.log`
- 파일 연결: 등록한 경우 `HKCU\Software\Classes`

업그레이드는 새 `gitman.exe`로 기존 파일을 교체하면 됩니다. 실행 파일 경로가 바뀌면 파일 연결을 다시 등록합니다. 문서 저장은 임시 파일과 원자적 교체를 사용하고, 동시에 다른 프로그램이 문서를 수정하면 revision 충돌로 덮어쓰기를 막습니다. 백업 파일(`.bak`)은 만들지 않습니다.

자세한 설치·업그레이드·제거·데이터 위치·라이선스·제한 사항은 [배포 안내](docs/deployment.md)를 참고합니다.

## 알려진 제한

- 실제 `svn.exe` 작업 복사본과 SVN 네트워크 동작은 아직 실측하지 않았습니다. 현재 보증 범위는 fixture와 fake runner 검증입니다.
- 네트워크 드라이브와 실제로 매우 느린 경로에서의 탐색·조회는 검증하지 않았습니다.
- 커밋, 푸시, 병합, 리베이스, 충돌 해결, 대화형 셸, 자격 증명 저장, 깊이 2 이상 자동 탐색은 지원하지 않습니다.
- 로그 파일은 자동으로 정리되지 않습니다(오래된 파일은 직접 지웁니다). Page Up/Page Down 키보드 스크롤은 제공하지 않습니다.
- 카드 목록은 항상 문서에 등록된 순서로 표시됩니다. 별도 정렬 기능은 없으며, 카드를 드래그해 순서를 바꾸면 문서에도 반영됩니다.

## 참고 문서

- [빌드 및 실행 안내](docs/build.md)
- [Skia 준비 안내](docs/skia-build.md)
- [배포 안내](docs/deployment.md)
- [요구사항 기준선](docs/requirements.md)
- [코드 스타일](docs/code_style.md)
- [단계 8 최종 검증 기록](docs/verification/2026-08-19-stage-8.md)
