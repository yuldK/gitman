# 4단계 (d) — 명령·훅 편집 화면

날짜: 2026-09-10 · 설계: `docs/custom-commands-and-svn-hooks-design.md` (C5) ·
지시: 2026-09-10 사용자 요청 ("A로 전체 편집 UI") · 요구사항: REQ-020 · REQ-021

## 1. 변경 요약

| 항목 | 반영 |
| --- | --- |
| 목록 | 환경설정에 `명령` 탭. `사용자 명령`·`SVN 훅` 두 구역, 줄마다 ▲▼✎✕ |
| 추가 | 구역 끝의 `명령 추가`·`훅 추가` |
| 편집 | `command_editor_dialog_element` — 환경설정 위에 뜨는 modal |
| 스크립트 | 경로 고르기(`찾아보기…`)와 외부 편집기로 열기(`편집기`)까지. **내용은 다루지 않는다** |
| 아이콘 | codicon 이름 칸 + 그 자리에서 그리는 미리 보기. 목록 행 제목 앞에도 같은 글리프 |
| 범위 | 목록은 `custom`·`svn_hooks` **묶음 단위**로 문서가 덮어쓴다 |

## 2. 구현 경계

- **스크립트 내용은 이 창의 몫이 아니다** (2026-09-10 지시). 편집기가 훨씬 잘하는
  일이라 `편집기` 버튼이 기존 `app_command::open_in_vscode`로 넘긴다. 상대 경로는
  문서 폴더 기준으로 풀어 절대 경로로 넘기고, 풀 수 없으면 그 버튼이 비활성이다.
- **줄 번호는 owner가 나른다.** 버튼 넷은 kind가 넷뿐이고 `command-0`·`hook-1` 같은
  owner로 어느 줄인지를 담는다. 컨텍스트 메뉴가 enum 왕복 때문에 첫 줄로 몰렸던
  것과 같은 실수를 되풀이하지 않는다.
- **"만드는 중"은 뜻으로 나른다.** `open_command_editor_intent::creating`이다.
  "자리 번호가 목록 끝이면 새 항목"으로 두면, 한 프레임 안에서 앞줄이 지워졌을 때
  편집 클릭이 조용히 추가로 바뀐다.
- **목록을 건드리면 편집을 닫는다.** 지우기·순서 바꾸기·문서 override 되돌리기가
  모두 `command_editor_`를 버린다. 자리 번호가 다른 항목을 가리키게 되기 때문이다.
- **부모 없이 살아남지 않는다.** 문서 열기·바꾸기·닫기와 환경설정 다시 열기가
  편집 초안을 함께 버린다. 남겨 두면 다음에 환경설정을 열 때 **다른 문서의 목록
  위로** 되살아나 엉뚱한 항목을 덮어쓴다 (3.3의 결함 1).
- **묶음 단위 override다.** 항목 하나만 문서가 덮어쓰는 것은 표현할 수 없다 —
  "전역 항목 하나만 지우기"에 대응하는 값이 없다 (설계 C2).
- **`id`는 화면에 없다.** 새로 만들 때 이름에서 짓고(`make_command_id`), 이미 있던
  항목은 이름을 바꿔도 그대로 둔다. 다른 카드의 `projects`가 그 `id`를 가리키고
  있을 수 있다.

## 3. 검증

빌드·test 환경: Visual Studio Community 2026 (MSVC 19.51), CMake 4.4.2,
preset `vs2026-tests` / `vs2026-tests-release` / `vs2026-analysis`.

### 3.1 새 TEST_CASE

`tests/command_editor_tests.cpp` (25개)

| 묶음 | TEST_CASE |
| --- | --- |
| 만들기·검증 | Adding a command needs a name and a script before it can be confirmed |
| | Confirming an invalid command changes nothing |
| 고치기 | Editing an existing command replaces it in place and keeps its id |
| | Editing a row that no longer exists does nothing |
| 목록 | Command rows move within the list and stop at the ends |
| | Removing a command closes an editor open on the same list |
| | Reordering the list closes an open editor |
| 수명 | Closing or swapping the document drops the editor draft |
| | Reopening the settings dialog drops a stale editor draft |
| 값 | Cycling command choices walks every value and comes back |
| | Command flags toggle on the editor draft only |
| | The TortoiseSVN toggle lives on the tab, not the editor |
| | The timeout box keeps only digits |
| | Arguments, projects and the timeout round trip through their text boxes |
| | Arguments with quotes, spaces and tabs survive the round trip |
| 훅 | The hook editor shows the phase instead of a name |
| | The hook editor drops the command only rows |
| 스크립트 | A picked script path lands in the script box |
| | A relative script path is resolved against the document folder |
| | The editor hands the script off to Win32 and to the external editor |
| tree | The commands tab lists every row with its own move, edit and remove buttons |
| | The add buttons say they are creating rather than pointing past the end |
| | The editor dialog sits above the settings dialog with the right rows |
| | The editor buttons cancel and confirm |
| | Every editor text box routes to its own text target |

`tests/settings_dialog_tests.cpp`의 "Every settings control stays inside the panel on
all tabs"에 `명령` 탭을 더했다. `tests/app_commands_tests.cpp`의 왕복 목록을 12개로
늘렸다.

### 3.2 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공, 경고 없음 |
| `ctest --preset vs2026-tests-debug` | **896 / 896 통과** |
| `cmake --build --preset vs2026-tests-release` | 성공, 경고 없음 |
| `ctest --preset vs2026-tests-release` | **896 / 896 통과** |
| `cmake --build --preset vs2026-analysis` | 성공, `/analyze` 경고 0개 |
| `scripts/check_source_style.ps1 -root .` | 461개 파일 통과 |

기준선 대비 test 수: 871 → **896** (+25).

### 3.3 검토에서 잡은 결함

이 단계의 검토는 서브 에이전트 셋으로 나눠 돌렸다 (계층·계약 / logic 경계 / UI 배치).

| # | 결함 | 고친 곳 |
| --- | --- | --- |
| 1 | 편집 초안이 문서를 바꿔도 살아남아, 다음에 환경설정을 열면 **다른 문서의 목록 위로** 되살아난다. 확인을 누르면 그 문서의 다른 항목을 덮어쓰고 `id`까지 옮겨 붙어 중복 `id`가 생긴다 (창을 가리는 modal을 파일 드롭이 지나친다) | `logic_controller.cpp` — `handle_open_document`·`install_document`·`handle_close_document`·`handle_open_settings`에서 함께 버린다 |
| 2 | `join_command_arguments`가 인자에 든 따옴표를 **삼키고**, 탭이 든 인자를 따옴표 없이 내보내 왕복에서 둘로 갈린다. 고치지 않고 열었다 확인만 해도 인자가 바뀐다 | `command_definition.cpp` — 따옴표 구간 안의 `""`를 글자 하나로 읽고, 탭도 따옴표 대상에 넣었다 |
| 3 | 제한 시간 칸에 숫자가 아닌 글자가 섞이면 `parse_seconds`가 통째로 0(제한 없음)으로 읽는데 검증이 오류로 보지 않아 원래 값이 조용히 사라진다 | `logic_controller.cpp` — 환경설정 제한 시간 칸과 같게 숫자만 남긴다 |
| 4 | 지우기와 편집이 같은 프레임에 들어오면 편집이 **한 칸 밀린 줄**을 연다 | `open_command_editor_intent::creating`을 두고, 없는 줄을 고치겠다는 클릭은 버린다 |
| 5 | 순서 바꾸기만 열린 편집을 닫지 않았다 (지금은 modal이 가려 닿지 않지만 규칙이 갈려 있었다) | `handle_move_command`도 닫는다 |

그 밖에 `handle_edit_text_input`이 새 칸을 처리한 뒤 switch dialog 갈래로 흘러 비어
있는 `switch_dialog_`를 건드리던 것을 test가 먼저 잡았다 (모든 키 입력이 abort).

### 3.4 검증하지 않은 것

- **Visual Studio 2022는 이 기기에 없다.** `cmake --preset vs2022-tests`가
  "could not find any instance of Visual Studio"로 configure에서 멎는다. 설치된
  것은 Visual Studio Community 2026 하나뿐이라 matrix의 2022 절반은 돌리지
  못했다.
- 실제 화면을 눈으로 보지 않았다. 배치는 `unarranged()`·`duplicate_ids()`와 값
  test로만 확인했다.
- 파일 선택 dialog(`choose_script_file`)는 UI thread의 Win32 호출이라 test가 없다.
  test는 "앱 명령이 올바른 인자와 함께 나가는가"까지 본다.

## 4. 남은 수동 검수

- 환경설정 `명령` 탭에서 명령을 하나 만들고, 카드 우클릭 메뉴와 카드 표면에 그
  이름·아이콘으로 나오는지 확인한다.
- `찾아보기…`가 문서 폴더에서 열리는지, 고른 경로가 칸에 들어오는지 확인한다.
- `편집기`가 그 스크립트를 편집기로 여는지 확인한다 (상대 경로 포함).
- 문서를 연 채로 목록을 고치고 `저장`한 뒤 `.version-list`의 `commands`가
  기대대로 쓰였는지, `덮어씀` 배지를 눌러 전역으로 되돌아가는지 확인한다.
- 줄이 많을 때 환경설정 창이 낮으면 항목 열만 스크롤되는지 확인한다.
- 아이콘 이름을 아무렇게나 적었을 때 기본 글리프로 물러서는지 확인한다.
