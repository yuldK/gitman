# 2단계 — 스크립트 실행 기반과 명령·훅 설정 스키마

날짜: 2026-09-10 · 설계: `docs/custom-commands-and-svn-hooks-design.md` (X, C1~C2) ·
지시: 2026-09-10 사용자 요청 · ADR: `ADR-003` 사용자 스크립트 실행 절 추가

## 1. 변경 요약

| 항목 | 반영 |
| --- | --- |
| 스크립트를 확장자로 실행 | `domain/script_interpreter`가 확장자 → 해석기 표를 든다. `.py` `.js` `.ps1` `.bat` `.cmd` `.exe`를 다룬다 |
| 해석기 찾기 | `infrastructure/script_interpreter_discovery`가 `PATH`(셸은 `%SystemRoot%\System32` 먼저)에서 절대 경로를 찾는다 |
| 요청 만들기 | `infrastructure/script_command_builder`가 git·svn 공통 인자 없이 `process_request`를 만든다 |
| 명령·훅 설정 | `domain/command_definition` 값 타입과 `commands` JSON 구역 (전역 + 문서 override) |
| 정책 문서 | ADR-003에 사용자 스크립트 실행 절을 더하고 "shell 미사용" 문구를 정정 |

이 단계까지는 **아무것도 실행하지 않는다**. 설정을 읽고 쓰고, 실행에 필요한 값을
만드는 데까지다.

## 2. 구현 경계

- `win32_process_runner`는 `CreateProcessW`에 `lpApplicationName`을 넘기므로
  `.py`·`.bat`을 직접 실행할 수 없다. 해석기를 고르는 것이 이 단계의 핵심이다.
- 해석기 탐색은 **이름 우선**이다. `pwsh.exe`를 모든 디렉터리에서 먼저 찾고 없을
  때 `powershell.exe`를 찾는다. 디렉터리를 우선하면 `PATH` 앞쪽의 옛 판이 새 판을
  가린다.
- 셸(`cmd.exe`·`powershell.exe`)만 `%SystemRoot%\System32`를 먼저 본다. 나머지
  해석기는 사용자가 고른 판을 쓰는 것이 맞으므로 `PATH` 순서를 그대로 따른다.
- `commands`는 문서의 **새 최상위 키**다. `workspace_settings_overrides`가 스칼라
  단위 `std::optional` 사슬이라 목록을 담을 수 없어서다. **스키마 판번은 1
  그대로** — 판번을 올리면 이 키를 모르는 예전 gitman이 문서를 아예 못 연다.
- 문서 override는 묶음(`custom` / `svn_hooks`) 단위 대체다. 항목별 병합은 "전역의
  이 항목만 지우기"를 표현할 방법이 없어 두지 않았다.
- 성립하지 않는 정의의 진단 심각도는 **경고**다. 설계 문서는 심각도를 정하지
  않았고, 오류로 두면 오타 하나가 문서 저장 전체를 막는다 (`json_project_store`는
  자기 출력을 다시 파싱해 오류가 있으면 commit하지 않는다).
- 문서와 앱 설정이 같은 codec(`infrastructure/json_command_catalog`)을 쓴다. 문서
  파서는 `nlohmann::json`, 저장소는 `ordered_json`이라 이 구역만 옮겨 담는다.
  파싱 규칙이 두 벌이 되는 것보다 사본 하나가 싸다.

## 3. 검증

빌드·test 환경: Visual Studio Community 2026 (18.9.12120.119, MSVC 19.51),
CMake 4.4.2, preset `vs2026-tests` / `vs2026-tests-release`.

### 새 TEST_CASE

| 파일 | TEST_CASE |
| --- | --- |
| `tests/command_definition_tests.cpp` | A command pattern matches with wildcards, mixed separators and case |
| `tests/command_definition_tests.cpp` | Command filters combine with AND |
| `tests/command_definition_tests.cpp` | A command pattern also matches the display name |
| `tests/command_definition_tests.cpp` | A malformed command never matches |
| `tests/command_definition_tests.cpp` | A document catalog replaces the global one item group at a time |
| `tests/command_definition_tests.cpp` | An SVN hook is skipped when disabled, malformed or out of path |
| `tests/command_definition_tests.cpp` | The interpreter table follows the script extension |
| `tests/script_command_tests.cpp` | Interpreter discovery finds the tool on PATH |
| `tests/script_command_tests.cpp` | A shell is taken from the system directory before PATH |
| `tests/script_command_tests.cpp` | PowerShell falls back to the Windows edition |
| `tests/script_command_tests.cpp` | An executable script is run directly |
| `tests/script_command_tests.cpp` | An unknown extension and a missing interpreter both report why |
| `tests/script_command_tests.cpp` | A script request carries the interpreter, the script and the user arguments |
| `tests/script_command_tests.cpp` | An executable script is not repeated as an argument |
| `tests/script_command_tests.cpp` | A per command timeout overrides the unlimited default |
| `tests/script_command_tests.cpp` | An unusable interpreter yields no request at all |
| `tests/script_command_tests.cpp` | The script environment drops empty values and folds duplicate names |
| `tests/script_command_tests.cpp` | A command line description names the interpreter and every argument |
| `tests/project_schema_tests.cpp` | The document reads user commands and SVN hooks |
| `tests/project_schema_tests.cpp` | A document without commands follows the global list |
| `tests/project_schema_tests.cpp` | A malformed command is dropped with a warning and the document still opens |
| `tests/json_project_store_tests.cpp` | Project store round-trips the document commands |
| `tests/json_project_store_tests.cpp` | A document that defines no commands writes no commands object |
| `tests/app_settings_tests.cpp` | App settings round-trip the global command catalog |
| `tests/app_settings_tests.cpp` | App settings without a command catalog keep the defaults |

### 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-release` | 성공, 경고 없음 |
| `ctest --preset vs2026-tests-release` | **847 / 847 통과** (76.8초) |
| `scripts/check_source_style.ps1 -root .` | 444개 파일 통과 |

기준선 대비 test 수: 812 → 1단계 822 → **2단계 847** (+25).

### 검증하지 않은 것

- 실제 해석기(`python.exe` 등)를 실행하지 않았다. 이 단계는 요청을 **만드는**
  데까지이고, 실행 배선은 3단계다. 탐색은 `fake_vcs_file_probe`로 검증했다.
- Visual Studio 2022 toolchain, Debug 구성, `/analyze`는 최종 단계로 미뤘다.

## 4. 남은 수동 검수

- `gitman.app-settings.json`을 열어 `commands` 구역이 기본값으로 기록되는지
  확인한다 (`custom: []`, `svn_hooks.use_tortoise_svn: true`).
- `.version-list`에 `commands`를 손으로 써 넣고 문서를 열었다 저장했을 때 값이
  그대로 남는지, 모르는 키가 보존되는지 확인한다.
- 잘못된 명령 정의를 넣었을 때 문서가 그대로 열리고 배너에 경고가 뜨는지 확인한다.
