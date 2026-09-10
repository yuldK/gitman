# 사용자 명령과 SVN 훅 설계

날짜: 2026-09-10 · 지시: 2026-09-10 사용자 요청 · 기준 문서: [requirements.md](requirements.md),
[ADR-003](decisions/ADR-003-vcs-runtime-policy.md), [ADR-004](decisions/ADR-004-threading-and-state-ownership.md)

이 문서는 한 지시에서 나온 네 가지를 함께 다룬다. 크기가 서로 달라 단계를 나누고,
단계마다 빌드·test·검증 기록을 낸다 (2026-09-10 지시: "작은 것 먼저, 단계별 커밋").

| 단계 | 범위 | 절 |
| --- | --- | --- |
| 1 | 일괄 업데이트 버튼, 카드의 경과·마지막 완료 시각 | U, P |
| 2 | 스크립트 실행 기반과 설정 스키마 | X |
| 3 | 커스텀 명령 (컨텍스트 메뉴·카드 표면) | C |
| 4 | SVN 훅 (TortoiseSVN 훅 재사용 + 자체 훅 목록), 설정 UI | H, C5 |

## 0. 확정된 사용자 결정

2026-09-10 질의로 확정한 항목이다. 설계는 이 답을 전제한다.

| 항목 | 결정 |
| --- | --- |
| 설정 계층 | 전역 앱 설정에 기본 목록을 두고 `.version-list` 문서가 덮어쓴다 |
| 커스텀 명령 적용 범위 | VCS 종류 필터 + 카드별 켜고 끄기 + 경로·이름 패턴, 셋 다 제공 |
| SVN 훅 범위 | update·switch 전후. pre 계열 훅이 실패하면 본 작업을 실행하지 않는다 |
| 카드 시간 표시 | 실행 중이면 경과 시간, 끝났으면 마지막 완료 시각 |
| 문서 신뢰 게이트 | 두지 않는다. 문서가 정의한 명령·훅은 그대로 실행한다 |
| 아이콘 | codicon만 지원한다. 독자 이미지는 넣지 않는다 |

마지막 두 항목은 처음 요청에서 바뀐 것이라 근거를 남긴다.

- **문서 신뢰 게이트 없음**: `.version-list`는 Windows 파일 연결 대상이라 (REQ-016)
  남이 보낸 문서를 두 번 눌러 여는 것만으로 그 문서가 정의한 훅이 업데이트 시점에
  실행될 수 있다. 사용자가 위험을 알고 게이트를 두지 않기로 정했다. 대신 훅과
  커스텀 명령은 **실행할 때마다 실행할 명령줄 전체를 카드 로그에 남긴다** (X6).
- **codicon만**: luil `menu_item_config::icon`은 `char32_t` 하나라 컨텍스트 메뉴에
  이미지를 넣으려면 메뉴 element를 gitman이 다시 만들어야 한다. 키보드 탐색이
  luil의 menu kind에 묶여 있어 깨질 위험이 있고 이득이 아이콘 한 칸이라, 독자
  이미지를 범위에서 뺐다.

## U. 일괄 업데이트 버튼

### U1. 자리와 모양

도구 막대 왼쪽, `전체 새로 고침` 바로 오른쪽에 둔다. 두 버튼 모두 왼쪽 고정이며
문서 경로 라벨은 그 오른쪽에서 시작한다 (`label_left`가 버튼 두 개 폭만큼 밀린다).
오른쪽 정렬 묶음에 넣지 않는 이유는, 그쪽이 창이 좁아질 때 가장 먼저 사라지는
자리라 "위쪽에 제공"이라는 요청과 어긋나기 때문이다.

`kind_toolbar_update_all`을 새로 만든다. 글리프는 평소 `icon_cloud_download`,
변경 작업이 하나라도 돌고 있으면 `icon_stop_circle`이다.

### U2. 동작

- 평소: `update_all_intent`. logic이 활성 카드를 순서대로 돌며 `begin_change`를 부른다.
- 실행 중: `cancel_all_operations_intent`. 실행 중인 변경 작업을 전부 취소한다.
  지금까지 없던 "모두 중지"를 이 버튼이 겸한다.
- 활성 조건: 업데이트를 시작할 수 있는 카드가 하나라도 있거나, 실행 중인 작업이 있다.

`begin_change`는 이미 busy 카드를 거부하고 사유를 카드 로그에 남긴다. 일괄
업데이트는 그 거부를 세어 배너 notice로 요약한다 — "N개 카드의 업데이트를
시작했습니다 (M개는 이미 작업 중이라 건너뜀)". 전체 새로 고침이 제출 실패를 조용히
버리는 현재 동작을 그대로 물려받지 않기 위한 것이다.

### U3. view_snapshot

`view_snapshot`에 두 값을 더한다. 도구 막대는 view를 들지 않으므로 `build_ui_tree`가
생성자 인자로 넘긴다.

- `bool can_update_all` — 업데이트를 시작할 수 있는 카드가 하나 이상이다.
- `bool any_change_running` — 변경 작업이 하나 이상 실행 중이다.

## P. 카드의 경과 시간과 마지막 완료 시각

### P1. 문제

지금 경과 시간은 하단 로그 pane 헤더에만 있다. 카드만 봐서는 그 카드가 도는 중인지
멎었는지 알 수 없다 (2026-09-10 지시). 상태 아이콘의 회전이 유일한 단서인데 카드가
여럿이면 눈에 잘 들어오지 않는다.

### P2. 표시 규칙

카드 세 번째 줄(상태 조각 줄)의 **오른쪽 끝**에 오른쪽 정렬로 그린다. 조각과 달리
먼저 잘리지 않도록 `arrange`가 폭을 미리 예약한다 (`layout_card_elapsed_width`,
64 논리 픽셀). 로그 헤더의 `elapsed_right_` 관용구를 그대로 따르며, 자리가 없으면
0을 넣어 "그리지 않음"을 뜻하게 한다.

| 상태 | 글 | 색 |
| --- | --- | --- |
| 작업 실행 중 | `MM:SS` 경과 (1초마다 증가) | 강조색 |
| 실행한 적 있음 | `방금 전` · `3분 전` · `2시간 전` · `09-08 14:32` | 흐린 전경색 |
| 아직 아무것도 안 함 | 그리지 않는다 | — |

경과 시간은 조회(refresh)와 변경(update·switch·커스텀 명령)을 **모두** 센다. 지금은
변경 작업만 `change_started_at`을 가지므로 조회에도 시작 시각이 필요하다.

### P3. 상태

`card_state`에 두 값을 더한다.

- `std::optional<std::chrono::steady_clock::time_point> operation_started_at`
  — 조회·변경 어느 쪽이든 작업이 시작될 때 찍고 끝나면 지운다.
- `std::optional<std::chrono::system_clock::time_point> last_finished_at`
  — 마지막으로 끝난 작업의 완료 시각. 실패해도 남긴다.

기존 `change_started_at`·`change_elapsed`는 로그 헤더가 쓰므로 그대로 둔다.
`card_view_model`이 위 두 값을 그대로 싣는다.

### P4. 새 포맷터

`presentation/log_presentation`에 `format_relative_time(now, then)`을 더한다. 순수
함수라 test가 쉽다.

| 차이 | 결과 |
| --- | --- |
| 10초 미만 | `방금 전` |
| 60초 미만 | `N초 전` |
| 60분 미만 | `N분 전` |
| 24시간 미만 | `N시간 전` |
| 그 이상 | `MM-DD HH:MM` (지역 시각) |

음수 차이(시계가 뒤로 간 경우)는 `방금 전`이다.

### P5. 다시 그리기

`card_element::next_update`는 지금 busy일 때 `context.now`를 돌려주어 33ms마다 창
전체를 다시 그린다. 여기에 규칙 하나를 더한다.

- 작업 중: 지금처럼 `context.now` (회전 아이콘이 이미 요구한다).
- 마지막 완료 시각만 그릴 때: **다음 분 경계**. 1분에 한 번이면 `N분 전`이 제때
  바뀐다. `context.now`를 돌려주면 멎은 카드가 영원히 30fps로 창을 깨운다.

## X. 스크립트 실행 기반

커스텀 명령(C)과 SVN 훅(H)이 함께 쓰는 계층이다.

### X1. 왜 새 계층이 필요한가

`win32_process_runner`는 `CreateProcessW(lpApplicationName, …)`를 쓰고
`validate_process_request`가 상대 경로를 거부한다. 그래서 `.py`·`.js`·`.bat`·`.ps1`은
그대로 실행할 수 없다 — 부르는 쪽이 해석기를 절대 경로로 정해 스크립트를 인자로
넘겨야 한다. `make_vcs_process_request`는 git·svn 공통 인자를 무조건 앞에 붙이므로
재사용할 수 없다.

### X2. 확장자에서 해석기로 (`domain/script_interpreter`)

순수 표다. OS를 모르고 파일을 읽지 않는다.

| 확장자 | 해석기 | 스크립트 앞에 붙는 인자 |
| --- | --- | --- |
| `.py` `.pyw` | `python.exe` | 없음 |
| `.js` `.mjs` `.cjs` | `node.exe` | 없음 |
| `.ps1` | `pwsh.exe`, 없으면 `powershell.exe` | `-NoProfile -NonInteractive -ExecutionPolicy Bypass -File` |
| `.bat` `.cmd` | `cmd.exe` | `/d /c` |
| `.exe` `.com` | 그 파일 자신 | 없음 |
| 그 밖 | 없음 (거부) | — |

`script_interpreter_kind`와 `script_interpreter_spec { executable_name,
alternate_executable_name, leading_arguments, prefer_system_directory }`를 돌려준다.

### X3. 해석기 찾기 (`infrastructure/script_interpreter_discovery`)

`vcs_tool_discovery`의 `split_search_path`를 그대로 쓴다. 찾는 순서는

1. `prefer_system_directory`인 해석기(`cmd.exe`·`powershell.exe`)는
   `%SystemRoot%\System32`를 먼저 본다. 셸을 PATH에서 찾으면 앞에 놓인 동명 파일이
   가로챌 수 있다.
2. PATH의 각 디렉터리.
3. 못 찾으면 진단 `script_interpreter_unavailable`.

`%SystemRoot%\System32`를 알기 위해 `vcs_tool_environment`에 `system_directory`를
더한다. 이 구조체는 이미 "시작할 때 한 번 모으는 host 사실"의 자리다
(`PATH`·program files). 채우는 곳은 `win32_vcs_file_probe.cpp`의
`current_vcs_tool_environment()` 하나뿐이라 infrastructure는 계속 환경 변수도
레지스트리도 모른다.

결과는 `vcs_operation_executor`가 캐시한다. 해석기 경로는 앱이 사는 동안 바뀌지
않는다고 본다.

### X4. 요청 만들기 (`infrastructure/script_command_builder`)

`make_script_process_request(…)`를 `make_vcs_process_request` 옆에 새로 만든다.
git·svn 공통 인자를 붙이지 않는 것이 유일한 차이다.

- `text_encoding`은 `active_code_page_fallback`. Windows 스크립트의 출력은 보통
  콘솔 코드 페이지(한국어 Windows에서 CP949)다.
- `vcs_command_class::user_script`를 더하고 `vcs_limits_for`에 제한을 넣는다. 기본
  제한 시간은 **없음**(취소로만 끝난다), capture 상한은 update와 같은 32 MiB.
  명령·훅마다 `timeout_seconds`로 덮어쓸 수 있다.

### X5. 환경 변수

스크립트가 인자 파싱 없이 맥락을 읽을 수 있도록 아래를 넣는다. 값이 없으면 넣지
않는다.

| 이름 | 값 |
| --- | --- |
| `GITMAN_PROJECT_ID` | 카드 id |
| `GITMAN_PROJECT_NAME` | 카드 표시 이름 |
| `GITMAN_REPOSITORY_PATH` | 저장소 작업 트리 경로 (Windows 원형) |
| `GITMAN_DOCUMENT_PATH` | 열린 `.version-list` 절대 경로 |
| `GITMAN_VCS` | `git` 또는 `subversion` |
| `GITMAN_REFERENCE` | 현재 branch 또는 URL |
| `GITMAN_REVISION` | 현재 리비전 |

`process_request`가 이름 중복(대소문자 무시)을 오류로 보므로 만드는 쪽에서 한 번
정리한다.

### X6. 로그

`operation_log_forwarder`를 그대로 sink로 쓴다. 스크립트의 stdout·stderr가 카드
로그와 로그 파일에 그대로 들어간다. 그 앞뒤로 lifecycle 항목을 남긴다.

- 시작: `사용자 명령 "<이름>" 실행: <해석기 파일 이름> <스크립트> <인자…>`
  — 실제로 무엇이 실행되는지 남긴다. 문서 신뢰 게이트를 두지 않기로 한 대신
  사후에 추적할 수 있게 하는 장치다 (0절).
- 끝: `사용자 명령 "<이름>"이 끝났습니다 (종료 코드 N, MM:SS)` 또는 실패·취소 문구.

`classify_vcs_failure`는 git·svn 어휘라 스크립트 결과에 쓰지 않는다. 판정은
`process_completion`과 종료 코드만 본다.

### X7. 취소와 종료

`begin_change`가 만드는 카드별 취소 source를 그대로 쓰고 `cancellation_registry`에
등록한다. 그래야 앱을 닫을 때 worker join이 스크립트를 기다리며 매달리지 않는다.
runner의 job object가 `cmd.exe`·`powershell`이 만든 손자 프로세스까지 함께 죽인다.

### X8. ADR-003 개정

ADR-003은 "shell을 사용하지 않는다"라고 적는다. `.bat`은 `cmd.exe`가, `.ps1`은
PowerShell이 해석하므로 문구를 고쳐야 한다. 개정 내용은

- gitman은 여전히 명령줄 문자열을 조립하지 않는다. 해석기 실행 파일과 인자 배열을
  Win32에 넘긴다.
- 사용자가 `.bat`·`.ps1`을 고르면 그 해석기가 셸인 것은 사용자의 선택이다.
- 스크립트는 비대화형이다 (stdin은 NUL, 창 없음). PowerShell에는 `-NonInteractive`를
  명시한다.
- 스크립트는 취소와 job 종료의 대상이다.

## C. 커스텀 명령

### C1. 정의

전역 앱 설정과 문서 어느 쪽에도 같은 모양으로 들어간다.

```json
{
    "id": "build-test",
    "label": "빌드 테스트",
    "script": "scripts/build_test.py",
    "arguments": ["--config", "Release"],
    "working_directory": "repository",
    "icon": "run-all",
    "show_on_card": true,
    "vcs": "any",
    "path_pattern": "",
    "projects": [],
    "timeout_seconds": 0,
    "refresh_after": true
}
```

| 필드 | 뜻 |
| --- | --- |
| `id` | 목록 안에서 유일하다. element id와 메뉴 key의 재료다 |
| `label` | 메뉴·tooltip에 뜨는 이름 |
| `script` | 절대 경로이거나 문서가 있는 폴더 기준 상대 경로 |
| `arguments` | 스크립트 뒤에 붙는 인자 |
| `working_directory` | `repository`(기본) · `document` · `script` |
| `icon` | codicon 이름. 모르는 이름은 `run`으로 물러선다 |
| `show_on_card` | 카드 표면에도 버튼으로 낸다 |
| `vcs` | `any`(기본) · `git` · `subversion` |
| `path_pattern` | 저장소 경로·표시 이름에 대한 glob. 빈 값이면 전체 |
| `projects` | 카드 id 목록. 비어 있으면 위 두 필터만 본다 |
| `timeout_seconds` | 0이면 제한 없음 |
| `refresh_after` | 끝난 뒤 카드 상태를 다시 조회한다 |

세 필터는 **모두** 통과해야 그 카드에 뜬다 (AND).

### C2. 계층 합성

`workspace_settings_overrides`는 스칼라마다 `std::optional`을 두는 구조라 목록이
들어가지 않는다. 그래서 문서에 **새 최상위 구역** `commands`를 만든다. 스키마 판번은
올리지 않는다 — `settings`·`appearance`·`fonts`가 그랬듯 판번 1에 optional 키를
더한다. 판번을 올리면 예전 gitman이 문서를 아예 못 연다.

```json
{
    "schema_version": 1,
    "commands": {
        "custom": [ … ],
        "svn_hooks": { … }
    },
    "projects": [ … ]
}
```

합성 규칙은 기존 override 의미를 그대로 따른다.

- 키가 **없으면** 전역 앱 설정을 따른다.
- 키가 **있으면** 빈 배열이라도 정의다 — 전역 목록을 대체한다.

즉 문서는 전역 목록에 더하는 것이 아니라 갈아 끼운다. 항목마다 병합하는 규칙은
"전역의 이 항목만 지우기"를 표현할 방법이 없어 두지 않는다.

### C3. 컨텍스트 메뉴

`context_menu_entry`는 닫힌 enum이라 사용자 정의 항목을 값 하나로 표현할 수 없다.
바꾸는 것은 셋이다.

- `context_menu_entry::custom_command`를 더한다.
- `context_menu_item_view`에 `std::u8string command_id`와 `char32_t glyph`를 더한다.
- 메뉴 key를 항목마다 만든다: 내장은 지금 이름, 사용자 정의는 `custom:<id>`.
  `build_context_menu_popup`의 select는 지금 enum으로 되찾아 **첫 일치**를 고르는데,
  사용자 명령이 여럿이면 전부 첫 항목으로 몰린다. key로 직접 찾도록 고친다.

메뉴 항목은 내장 여섯 개 뒤에 이어 붙인다. 항목이 많아지면 190 논리 픽셀 고정 폭에
스크롤이 없는 팝업이 문제가 되므로, 카드 하나에 붙는 사용자 명령을 **12개로 제한**
하고 넘치면 카드 로그에 경고를 남긴다.

실행은 새 intent `run_custom_command_intent { project_id, command_id }`다. 늦은
클릭 방어를 위해 handler가 필터와 busy를 다시 판정한다.

### C4. 카드 표면 버튼

`show_on_card`인 명령은 카드 오른쪽 버튼 줄에 들어간다. 지금 세 개(새로 고침·
업데이트·전환) 뒤에, 즉 **더 왼쪽**에 놓는다. `place` 람다는 오른쪽부터 채우고
자리가 없으면 숨기므로 사용자 명령이 먼저 사라진다 — 기본 기능이 살아남는 것이 맞다.

element id는 `{ kind_card_custom_command, <카드 id> + U+001F + <명령 id> }`다.
카드 id는 사용자가 JSON에 쓴 문자열이라 흔한 구분자는 충돌할 수 있어, JSON 문자열
안에 그대로 들어갈 수 없는 제어 문자를 쓴다. 중복 id는 `require_clean_tree`가 잡는다.

카드 표면 버튼도 12개로 제한한다.

### C5. 설정 UI

환경설정에 `명령` 탭을 더한다. 목록 한 줄이 명령 하나이고 줄마다 위·아래·편집·삭제
버튼이 붙는다 (`local_changes_dialog_element`의 줄별 버튼 관용구). 편집은 별도
dialog다.

이 탭은 4단계로 미룬다. 3단계에서는 JSON을 직접 고쳐도 동작하도록 파서·실행·표시를
먼저 완성한다.

## H. SVN 훅

### H1. 훅 지점

`svn_repository_provider`의 `update_impl`·`switch_to_impl` 안, 실제 `run_vcs_command`
앞뒤다. TortoiseSVN은 switch에도 update 훅을 쓰므로 단계 이름도 update 계열 하나로
둔다.

| 단계 | 자리 | 실패했을 때 |
| --- | --- | --- |
| `start_update` | 사전 검사(`query_local_impl`) **앞** | 중단 |
| `pre_update` | 사전 검사 통과 뒤, `svn update`·`svn switch` 직전 | 중단 |
| `post_update` | 본 명령 직후, 재조회 전 | 경고만 |

`start_update`가 사전 검사 앞인 것은 TortoiseSVN이 대화상자 전에 부르기 때문이다.
훅이 작업 트리를 건드리면 그 결과가 사전 검사에 반영되어야 한다.

중단하면 `update_block_reason::hook_rejected`(새 값)로 돌려보내고 훅의 출력 전체를
카드 로그에 남긴다.

### H2. 실행 순서

1. TortoiseSVN 훅 (설정에서 켜져 있고, 설치되어 있고, 경로가 맞을 때)
2. 문서·전역이 정의한 gitman 훅, 목록 순서대로

각 묶음 안에서 순차 실행하며 하나가 실패하면 그 뒤는 실행하지 않는다. 단계마다
취소 token을 확인한다.

### H3. TortoiseSVN 훅 읽기

TortoiseSVN은 클라이언트 훅을 **레지스트리**에 둔다. `HKCU\Software\TortoiseSVN`의
`hooks` (REG_SZ) 하나에 전부 들어 있고, 개행으로 나뉜 6줄이 훅 하나다.

1. 훅 종류 (`start_update_hook` · `pre_update_hook` · `post_update_hook` …)
2. 적용할 작업 복사본 경로 (`*`는 전체)
3. 명령줄 (스크립트 경로와 사용자 인자)
4. `true` · `false` — 끝날 때까지 기다린다
5. `show` · `hide` — 콘솔 창을 보인다
6. `enforce` · `ask` — 건너뛸 수 없다

옛 판은 6번째 줄이 없다. 6번째 토큰이 아는 훅 종류 이름이면 그 줄이 다음 record의
시작이라고 보고 5줄로 읽는다.

경로 맞추기는 작업 복사본 경로에서 부모로 올라가며 정확히 일치하는 항목을 찾고,
없으면 `*`를 쓴다. 한 종류당 하나만 고른다.

설치 여부는 `HKLM\SOFTWARE\TortoiseSVN`의 `Directory` 값으로 본다. 32비트로 빌드해도
64비트 키를 보도록 `KEY_WOW64_64KEY`를 쓴다. 레지스트리 접근은 platform/win32에만
두고 읽은 값을 `vcs_tool_environment`에 실어 infrastructure로 넘긴다 — 그래야 훅
선택 규칙을 레지스트리 없이 test할 수 있다.

TortoiseSVN은 작업 복사본 속성(`tsvn:preupdatehook` 등)에도 훅을 둘 수 있는데,
그쪽은 TortoiseSVN 자신이 승인 대화상자로 막는다. gitman에는 승인 개념이 없으므로
**속성 훅은 읽지 않는다**. 사용 설명서에 명시한다.

### H4. TortoiseSVN 훅의 인자

TortoiseSVN이 명령줄 뒤에 따옴표로 붙이는 인자를 그대로 만든다.

| 종류 | 인자 |
| --- | --- |
| `start_update_hook` | PATH CWD |
| `pre_update_hook` | PATH DEPTH REVISION CWD |
| `post_update_hook` | PATH DEPTH REVISION ERROR CWD RESULTPATH |

- PATH·RESULTPATH·ERROR는 **UTF-8 임시 파일**이다. PATH에는 작업 복사본 루트 한 줄이
  들어간다. ERROR는 성공해도 빈 파일로 만든다.
- DEPTH는 `svn_depth_t` 정수다. gitman은 `svn update`에 `--depth`를 주지 않으므로
  항상 `3`(infinity)이다.
- REVISION은 `HEAD`다. gitman은 리비전을 지정해 update하지 않는다.
- RESULTPATH는 update가 실제로 건드린 경로 목록인데 gitman은 `svn update` 출력을
  경로 목록으로 파싱하지 않는다. **빈 파일**을 넘기고 한계로 적는다.
- CWD는 작업 복사본 루트다.

임시 파일을 만들려면 새 이음매가 필요하다. application에 `temp_file_writer`
인터페이스를 두고 platform/win32가 `GetTempPathW` 기반으로 구현한다. infrastructure는
계속 OS를 부르지 않는다.

### H5. 알려진 차이

runner가 `CREATE_NO_WINDOW`와 stdin=NUL을 고정하므로 TortoiseSVN의 두 깃발을 그대로
지킬 수 없다.

- `show` (콘솔 창 보이기): 지키지 않는다. 항상 숨긴다. 출력은 카드 로그로 간다.
- `false` (기다리지 않기): 프로세스는 그대로 기다리되 **종료 코드를 판정에 쓰지
  않는다**. TortoiseSVN에서도 기다리지 않는 훅은 오류를 보고하지 않으므로 결과는
  같다.

### H6. gitman 자체 훅

```json
"svn_hooks": {
    "use_tortoise_svn": true,
    "hooks": [
        {
            "phase": "pre_update",
            "script": "hooks/check.py",
            "arguments": [],
            "enabled": true,
            "path_pattern": "",
            "timeout_seconds": 0
        }
    ]
}
```

인자는 TortoiseSVN 규약을 쓰지 않는다. 대신 X5의 환경 변수에 아래를 더한다.

| 이름 | 값 |
| --- | --- |
| `GITMAN_HOOK_PHASE` | `start_update` · `pre_update` · `post_update` |
| `GITMAN_HOOK_OPERATION` | `update` · `switch` |
| `GITMAN_HOOK_TARGET` | switch일 때 대상 URL |
| `GITMAN_HOOK_ERROR` | post 단계에서 본 명령이 실패했으면 그 요약 |

`use_tortoise_svn`은 기본 켬이다. TortoiseSVN이 없으면 아무 일도 하지 않는다.

## 요구사항

새 REQ를 넷 더한다.

| 식별자 | 요구사항 |
| --- | --- |
| REQ-018 | 도구 막대에서 모든 카드의 업데이트를 한 번에 시작하고 실행 중인 작업을 한 번에 중지한다 |
| REQ-019 | 카드에 실행 중 경과 시간과 마지막 완료 시각을 표시한다 |
| REQ-020 | 사용자가 정의한 스크립트를 카드 컨텍스트 메뉴와 카드 표면에서 실행한다 |
| REQ-021 | SVN update·switch 전후에 TortoiseSVN 훅과 사용자 정의 훅을 순차 실행한다 |
