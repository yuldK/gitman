# 3단계 — 커스텀 명령 (컨텍스트 메뉴·카드 표면·실행)

날짜: 2026-09-10 · 설계: `docs/custom-commands-and-svn-hooks-design.md` (C) ·
지시: 2026-09-10 사용자 요청 · 요구사항: REQ-020

## 1. 변경 요약

| 항목 | 반영 |
| --- | --- |
| 컨텍스트 메뉴 | 내장 여섯 항목 뒤에 이 카드에 걸린 사용자 명령이 이어진다 |
| 카드 표면 | `show_on_card`인 명령이 기본 버튼 왼쪽에 버튼으로 나온다 |
| 아이콘 | mapping.json에서 만든 codicon 이름 표(별칭 포함)를 이분 탐색한다 |
| 실행 | `operation_kind::run_custom_command`가 카드 lane에서 스크립트를 돌린다 |
| 로그 | 실행할 명령줄 전체와 stdout·stderr가 카드 로그에 남는다 |

## 2. 구현 경계

- `context_menu_entry`에 `custom_command` 하나를 더했다. 하나의 값이 여러 항목을
  뜻하므로 **메뉴 key를 항목마다 만든다** (`custom:<id>`). 기존 select factory는
  종류로 첫 일치를 찾아 실행했는데, 그대로 두면 사용자 명령이 전부 첫 항목으로
  몰린다. 키로 찾도록 고쳤다.
- 카드 표면 버튼의 element id owner는 `<카드 id> + U+001F + <명령 id>`다. 카드 id는
  사용자가 JSON에 쓴 문자열이라 흔한 구분자는 충돌할 수 있고, 명령 id의 제어
  문자는 문서 파서가 이미 거른다. 중복 id는 `require_clean_tree`가 잡는다.
- `place` 순서상 사용자 명령이 **가장 먼저 숨는다**. 좁은 창에서 새로 고침·
  업데이트·전환이 살아남는 것이 맞다.
- codicon 이름 표는 빌드 때 `cmake/generate_codicon_names.cmake`가
  `third_party/luil/assets/codicons/mapping.json`에서 만든다. luil이 만드는
  `icon_<name>` 상수는 컴파일 시점에 이름을 아는 코드용이라 이 쓰임에 맞지 않는다.
  이름 순 정렬은 이분 탐색의 전제이므로 test가 `std::ranges::is_sorted`로 확인한다.
- 실행 조건은 update보다 느슨하다. 스크립트가 저장소를 읽는지 쓰는지 앱이 모르므로
  로컬 조회 완료를 요구하지 않고 **바쁘지만 않으면** 허용한다.
- 끝난 뒤 조회는 명령의 `refresh_after`가 참일 때만 잇는다. `handle_change_completed`가
  무조건 `request_refresh`를 부르는 것과 다른데, 스크립트가 무엇을 바꿨는지 알 수
  없어 매번 원격까지 다시 보는 것이 비싸기 때문이다.
- **환경 변수 두 개를 넣지 않았다.** 설계 X5의 `GITMAN_REFERENCE`·`GITMAN_REVISION`은
  조회 없이 알 수 없고, 명령마다 조회를 앞세우면 비싸다. 스크립트는 저장소 폴더에서
  실행되므로 직접 물어보면 된다. 4단계의 훅은 provider가 이미 snapshot을 들고
  있으므로 그때 넣는다. 설계 문서를 그렇게 고쳤다.
- 카드 하나에 붙는 명령은 12개로 자른다. 넘치면 **문서를 열 때** 카드 로그로
  알린다 — view 구성은 매 프레임 도는 자리라 알릴 곳이 아니다.

## 3. 검증

빌드·test 환경: Visual Studio Community 2026 (18.9.12120.119, MSVC 19.51),
CMake 4.4.2, preset `vs2026-tests` / `vs2026-tests-release`.

### 새 TEST_CASE

| 파일 | TEST_CASE |
| --- | --- |
| `tests/custom_command_tests.cpp` | The generated Codicon table is sorted and finds names and aliases |
| `tests/custom_command_tests.cpp` | A card carries the commands that pass its filters |
| `tests/custom_command_tests.cpp` | Running a card command submits the definition to the worker |
| `tests/custom_command_tests.cpp` | A command that no longer matches the card is refused |
| `tests/custom_command_tests.cpp` | A finished command reports its exit code and follows refresh_after |
| `tests/custom_command_tests.cpp` | A successful command with refresh_after queries the card again |
| `tests/custom_command_tests.cpp` | The card face shows only the commands marked for it |
| `tests/custom_command_tests.cpp` | Clicking a card command button runs that command |
| `tests/custom_command_tests.cpp` | A disabled card command produces nothing |
| `tests/custom_command_tests.cpp` | The executor runs the script through its interpreter |
| `tests/custom_command_tests.cpp` | A missing interpreter fails the command without running anything |
| `tests/context_menu_tests.cpp` | Custom commands follow the built in items and keep separate keys |
| `tests/context_menu_tests.cpp` | The card menu lists the commands that match the card |

### 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-release` | 성공, 경고 없음 |
| `ctest --preset vs2026-tests-release` | **860 / 860 통과** (76.1초) |
| `scripts/check_source_style.ps1 -root .` | 447개 파일 통과 |

기준선 대비 test 수: 812 → 1단계 822 → 2단계 847 → **3단계 860** (+13).

### 검증하지 않은 것

- 실제 `python.exe`·`cmd.exe`를 띄우지 않았다. executor test는 `fake_process_runner`로
  "무엇을 실행하려 했는가"를 본다. 실제 실행은 수동 검수 항목이다.
- 설정 UI(명령 탭)는 4단계다. 지금은 `.version-list`나 전역 앱 설정 JSON을 직접
  고쳐야 명령이 생긴다.
- Visual Studio 2022 toolchain, Debug 구성, `/analyze`는 최종 단계로 미뤘다.

## 4. 남은 수동 검수

- `.version-list`에 `commands.custom`을 써 넣고 카드를 우클릭해 명령이 내장 항목
  뒤에 뜨는지, 아이콘이 지정한 codicon으로 나오는지 확인한다.
- `show_on_card`를 켠 명령이 카드 표면 버튼으로 나오고 tooltip이 이름인지 확인한다.
- 실제 `.py`·`.bat`·`.ps1` 스크립트를 각각 실행해 출력이 하단 로그에 흐르고
  첫 줄에 `실행: <해석기> <스크립트> …`가 남는지 확인한다.
- 0이 아닌 코드로 끝나는 스크립트에서 로그에 종료 코드가 남는지 확인한다.
- 실행 중 카드의 업데이트 버튼(중지)으로 취소했을 때 스크립트가 실제로 끝나는지,
  `cmd.exe`가 만든 손자 프로세스까지 사라지는지 확인한다.
- 창을 좁혀 사용자 명령 버튼이 기본 버튼보다 먼저 사라지는지 확인한다.
