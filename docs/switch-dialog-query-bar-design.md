# 전환 dialog 질의 막대 · SVN lazy 탐색 설계

상태: **구현 완료** (2026-08-22. `feature/switch-dialog-query-bar` 브랜치에서 사용자
지시로 체크포인트 검수 없이 커밋까지 진행했다. 검증 기록은
`docs/verification/2026-08-22-switch-dialog-query-bar.md`)

2026-08-22 사용자 지시 5건의 설계다. 검수에서 아래 3건이 확정되어 본문에 반영했다.

| 검수 지점 | 결정 |
|---|---|
| Git 우측 버튼 | **`$(close)` 지우기**로 한다 (트리거가 아니다). 질의가 비어 있으면 `$(search)` 장식이며 비활성이다 |
| SVN 갱신 범위 | **선택 노드**를 다시 조회한다 (초안대로) |
| `현재 위치로` 버튼 | **두지 않는다.** 박스 pre-fill + 트리거로 대신한다 |

| 항목 | 요구 |
|---|---|
| Q1 | 전환 dialog 제목 아래에 검색 바 같은 텍스트 박스를 둔다 |
| Q2 | 막대 우측 끝에 `$(arrow-right)`·`$(search)` 같은 트리거 버튼, 좌측 끝에 갱신 버튼을 둔다 |
| Q3 | SVN은 미리 다 읽지 않는다. 최상단 폴더만 읽고 사용자가 연 경로만 동적으로 확장한다 |
| Q4 | Git에서는 필터로 쓰지만 SVN에서는 성격이 다르다 |
| Q5 | SVN에서는 repo-browser처럼 실제 경로를 보여 준다 (Git과 나열 방식이 달라 생긴 문제다) |

---

## Q0. 현황

`switch_dialog_element`는 제목 줄(34) 아래에 곧바로 목록 영역을 둔다. 입력 칸은
없고, 목록은 Git이면 `candidates`를, SVN이면 `svn_rows`를 그린다.

SVN 브라우저는 `make_svn_repository_browser`로 root 노드만 만든 뒤 root를 조회하고,
`complete_svn_browser_query` → `advance_current_path`가 **현재 위치(current_url)까지
조상을 한 단계씩 자동으로 펼치며 계속 조회를 낸다**. 깊은 작업 복사본이면 dialog를
열자마자 depth 만큼의 `svn ls`가 연쇄로 나간다 — Q3이 지적한 동작이다.

행에는 디렉터리 **이름만** 그려서 `trunk` 밑의 `source`인지 `branches/x` 밑의
`source`인지 행만 봐서는 구분되지 않는다 — Q5가 지적한 문제다 (tooltip에만 URL이 있다).

---

## Q1·Q2. 질의 막대 (공통 골격)

제목 줄과 목록 사이에 한 줄을 넣는다. Git·SVN이 같은 골격을 쓰고 **문구·아이콘·
동작만** 다르다.

```
┌ SVN 저장소 전환 - 프로젝트 ────────────────────────────┐
│ [⟳]  [ /branches/2026-08                    ]  [→]     │  ← 질의 막대
│ ┌───────────────────────────────────────────────────┐  │
│ │ ▾ 📁 repo                                         │  │
│ │   ▸ 📁 branches                                   │  │
│ └───────────────────────────────────────────────────┘  │
│ 메시지                                    [전환] [취소] │
└────────────────────────────────────────────────────────┘
```

### 치수 (논리 픽셀)

| 값 | 지금 | 바뀜 |
|---|---|---|
| 제목 줄 아래 목록 top offset | 34 | **66** (`layout_switch_dialog_list_top`) |
| 질의 막대 높이 / top | — | **26** / 제목 아래 32 |
| 막대와 목록 사이 간격 | — | 8 |
| 좌우 버튼 한 변 / 버튼·박스 간격 | — | 26 / 6 |
| `switch_dialog_list_height`의 chrome 상수 | 130 (literal) | **162** (`layout_switch_dialog_chrome_height`) |
| Git dialog 높이 | 320 | **352** |
| SVN dialog 높이 | 430 | **462** |

목록 높이(190 / 300)는 그대로다 — dialog만 막대 높이만큼 자란다. `130`이라는
literal은 이름 있는 상수로 뽑아 두 곳(logic의 스크롤 한계, element의 배치)이 계속
같은 값을 쓰게 한다.

### element 구성

`ui_element_kind`에 셋을 더한다.

| kind | 역할 |
|---|---|
| `switch_dialog_query_input` | 텍스트 박스. `settings_timeout_input`과 같은 초점·caret 규칙이다 |
| `switch_dialog_query_action` | 우측 버튼 (Git 지우기 · SVN 이동) |
| `switch_dialog_query_refresh` | 좌측 갱신 버튼 |

텍스트 박스는 `settings_dialog_element`의 `timeout_input_element`와 그리기가 같다
(바탕 8%, 초점 시 accent 테두리, 글 끝 caret, 빈 값이면 흐린 placeholder). 두 곳이
같은 그림이므로 **`dialog_elements`로 옮겨 공유한다** — 커서 이동은 여전히 지원하지
않고 caret은 항상 글 끝이다.

### 초점과 키

- `interaction_controller`의 초점 판정에서 `settings_timeout_input`만 보던 곳을
  `is_text_input_kind(kind)` 하나로 모은다. 두 kind가 이 판정을 통과한다.
- `character_typed_event`는 초점을 가진 kind로 갈라 보낸다.
  `settings_timeout_input` → `edit_settings_timeout_intent`,
  `switch_dialog_query_input` → `edit_switch_query_intent`.
- `update_input_focus`가 dialog가 닫힐 때 초점을 거두는 규칙은 지금 환경설정
  dialog만 본다. 전환 dialog도 같이 본다.
- **Enter**: 질의 박스가 초점을 가지고 있으면 `submit_switch_query_intent`다. 카드
  갱신(현재 Enter 동작)으로 흘러가지 않는다. 초점이 없으면 지금과 같다.
- **Esc**: 지금 그대로 dialog 취소다. 박스에 초점이 있어도 마찬가지다 (초안을
  지우는 단계를 두지 않는다 — 취소가 더 흔하다).
- 열릴 때 자동 초점은 주지 않는다 (환경설정에서 받은 검수 지시와 같은 규칙).

### 새 intent

```cpp
struct edit_switch_query_intent { char32_t character; };   // backspace는 U+0008
struct submit_switch_query_intent {};                      // Enter · SVN 이동 버튼
struct clear_switch_query_intent {};                       // Git 지우기 버튼
struct refresh_switch_dialog_intent {};                    // 갱신 버튼
```

### view 모델

`switch_dialog_view`에 다섯을 더한다.

```cpp
// 우측 버튼이 맡은 몫이다. element가 이 값으로 glyph와 intent를 정한다.
enum class switch_query_action { search, clear, navigate };

std::u8string query {};              // 박스에 그릴 글
std::u8string query_placeholder {};  // 비었을 때의 흐린 안내
bool query_enabled { false };        // 조회 중·실행 중이면 거짓
switch_query_action query_action { switch_query_action::search };
bool query_action_enabled { false };
// 목록이 비었거나 조회 중일 때 목록 자리에 그릴 흐린 안내다.
std::u8string list_note {};
```

| action | glyph | 클릭 | 언제 |
|---|---|---|---|
| `search` | `$(search)` | 없음 (비활성) | Git · 질의가 비었다 |
| `clear` | `$(close)` | `clear_switch_query_intent` | Git · 질의가 있다 |
| `navigate` | `$(arrow-right)` | `submit_switch_query_intent` | SVN (질의가 있을 때만 활성) |

문구(`query_placeholder`·`list_note`)와 action까지 logic이 정하는 이유는, 확인 버튼
label·메시지를 이미 logic이 한곳에서 정하고 있어 같은 규칙을 따르기 위해서다
(plan 5.3). element는 그리기만 한다.

---

## Q4. Git — 필터

- **입력 즉시 거른다.** logic이 `display_name`에 대해 `contains_ignoring_ascii_case`
  (카드 목록 필터가 쓰는 그 함수)로 후보를 걸러 view의 `candidates`에 담는다.
- 따라서 view의 `candidates`는 **거른 목록**이다. `select_switch_candidate_intent`의
  index도 거른 목록 기준이며, logic이 원본 index로 되돌린다
  (`filtered_candidate_indices()` 헬퍼 하나). element는 손대지 않는다.
- 질의가 바뀌면 **선택을 지운다.** 고른 후보가 걸러져 사라졌는데 확인이 살아 있는
  상태를 만들지 않는다.
- 결과가 없으면 목록 자리의 안내가 `전환할 수 있는 후보가 없습니다.` 대신
  `필터에 맞는 후보가 없습니다.`다.
- placeholder: `브랜치 이름으로 필터링`
- 우측 버튼: 질의가 비면 `$(search)` 장식(비활성), 질의가 있으면 `$(close)`
  **지우기**다 (검수 결정). 즉시 걸러지므로 "적용" 트리거가 필요하지 않다.
- Enter: **걸러진 첫 후보를 선택한다.** 치고 → 첫 결과 잡기를 한 손으로 끝낸다.
- 갱신(`$(refresh)`): `query_switch_candidates`를 다시 낸다 (fetch 포함).
  `loading`을 다시 켜고 선택을 지우며 **질의 글은 그대로 둔다.**

## Q4·Q5. SVN — 실제 경로 주소 막대

Git과 달리 SVN은 목록 자체가 나열이 아니라 트리라 "필터링"이 맞지 않는다. 같은 칸을
**repo-browser의 주소 막대**로 쓴다.

- 박스에는 선택 노드의 **저장소 root 기준 실제 경로**를 그린다. root는 `/`,
  그 아래는 `/branches/2026-08` 형태다. URL 전체는 길어 tooltip에 남긴다.
- **트리에서 노드를 고르면 박스 글을 그 경로로 덮어쓴다** — 주소 막대의 원형
  동작이다. (Git 필터는 선택으로 덮어쓰지 않는다. 이 차이가 Q4가 말한 "느낌"이다.)
- 트리거(`$(arrow-right)`·Enter): 박스의 경로로 **이동**한다. root 기준 상대 경로
  (`/branches/x`)와 root로 시작하는 전체 URL을 모두 받는다. 없는 경로면
  `그 경로를 찾을 수 없습니다: …`를 메시지 자리에 띄우고 트리는 그대로 둔다.
- placeholder: `이동할 경로 (예: /branches/2026-08)`
- 갱신(`$(refresh)`): **선택 노드를 다시 조회한다** (선택이 없으면 root). 그 노드의
  자식 cache와 그 아래 노드를 버리고 `not_loaded`로 되돌린 뒤 다시 조회한다.
  펼침 상태는 유지한다. 새로 생긴 branch를 dialog를 닫지 않고 볼 수 있다.

### 목록 행 자체는?

행에는 지금처럼 **이름만** 그린다. 깊이 들여쓰기가 이미 계층을 보이고, 전체 경로는
주소 막대와 tooltip이 맡는다. 행마다 전체 경로를 그리면 640px 폭에서 깊은 노드의
이름이 잘려 오히려 읽기 어렵다.

---

## Q3. lazy 탐색 — 자동 하강을 사용자 행동으로 바꾼다

`advance_current_path`(current_url까지 자동 하강)를 지운다. 대신 같은 기계를
**명시적 목표**로 다시 쓴다.

```cpp
struct svn_repository_browser_state {
    …
    // 사용자가 이동을 요청한 목표다. 도달하거나 실패하면 비운다.
    std::u8string reveal_url {};
};

// 목표를 세우고 cache된 만큼 펼친다. 조회가 필요한 첫 URL을 돌려준다.
[[nodiscard]] std::optional<std::u8string> begin_svn_browser_reveal(state&, std::u8string_view url);
```

- `complete_svn_browser_query`는 `reveal_url`이 있을 때만 root부터 목표까지 다시
  훑어 내려간다 (`descend_toward_reveal`). 조회가 필요한 첫 노드를 만나면 그 URL을
  돌려주고 멈춘다.
- 두 함수는 `optional<u8string>` 대신 진행 상태를 돌려준다. 어디까지 갔는지를 logic이
  알아야 `찾을 수 없습니다` 메시지를 낼 수 있다.

```cpp
struct svn_browser_reveal_progress
{
    std::optional<std::u8string> query_url {};  // 조회가 필요한 다음 URL
    bool reached { false };                     // 목표에 도달해 선택까지 끝났다
    bool not_found { false };                   // 조회 목록에 그 경로가 없었다
};
```

- dialog를 열면 **root 조회 하나로 끝난다.** `reveal_url`은 비어 있다.
- 목표에 도달하면 그 노드를 선택하고 `reveal_url`을 비운다. 목표 노드 **자신은
  펼치지 않는다** (그 아래는 사용자가 열 때 조회한다).
- 중간 단계 조회가 실패하면 `reveal_url`을 비우고 실패한 노드에 지금과 같은 오류
  행을 남긴다.

`reveal_url`을 세우는 곳은 둘뿐이다.

1. 주소 막대 트리거 (`submit_switch_query_intent`)
2. 그것뿐이다. dialog를 열면 박스에 **현재 작업 복사본의 경로가 미리 채워져**
   있으므로 트리거 한 번으로 현재 위치까지 펼칠 수 있다. `현재 위치로` 전용 버튼은
   두지 않는다 (검수 결정).

현재 위치 행의 `(현재 위치)` 표시와 accent 막대는 그대로다 — 그 노드가 보일 때만
보인다.

**한 걸음 내려갈 때마다** 그 깊이의 노드를 골라 목록 가운데로 옮긴다 (2026-08-23
지시). 조회 하나가 초 단위로 걸려서, 목표에 닿을 때까지 아무 데도 붙지 않으면 한참
동안 어디까지 갔는지 알 수 없다. `progress.query_url`이 곧 그 깊이라 따로 셀 것이
없다. 주소 막대는 **건드리지 않는다** — 사용자가 친 목표가 도착할 때까지 보여야
한다. 걸어 내려가는 동안(`reveal_url`이 있는 동안)의 선택은 지나가는 자리라
`전환`을 받지 않는다 — 그 사이 확인을 받으면 목표가 아니라 중간 폴더로 간다.

목표에 **도달하면 그 행을 목록 가운데에 놓는다** (2026-08-23 지시). 선택만 해
두면 깊은 경로는 화면 밖에 남아 "폴더는 열렸는데 어디로 갔는지 모르는" 상태가 된다.
끝에 붙이면 그 위나 아래가 보이지 않아 어느 가지에 있는지도 읽히지 않는다. 목록의
처음·끝 근처는 스크롤 한계가 자연스럽게 위아래로 붙인다. 행을 직접 누른 경우는 이미
보이는 행이라 대상이 아니다.

### 경로 해석 규칙

`resolve_svn_browser_path(state, text)`가 박스의 글을 URL로 바꾼다.

- 앞뒤 공백을 버린다. 빈 값이면 실패다.
- root URL로 시작하면 **그대로** 정규화해 쓴다 (이미 인코딩된 URL을 다시 인코딩하지
  않는다).
- 그렇지 않으면 앞의 `/`를 떼고 `/`로 잘라 각 조각을 `append_svn_browser_url`로
  붙인다 — 한글 같은 비 ASCII는 이 경로에서 percent-encoding된다. `.`·`..` 조각은
  거부다.
- 실패하거나 조회 결과에 그 경로가 없으면 메시지 자리에 `그 경로를 찾을 수 없습니다: …`
  를 띄우고 트리는 건드리지 않는다.

---

## 영향 범위

| 파일 | 변경 |
|---|---|
| `application/svn_repository_browser.{h,cpp}` | `reveal_url`, `begin_svn_browser_reveal`, `advance_reveal_path`, `reset_svn_browser_node`, root 기준 경로 ↔ URL 변환 두 함수 |
| `application/app_messages.h` | intent 4개, `logic_message` variant |
| `application/logic_controller.{h,cpp}` | `switch_dialog_state.query`, handler 4개, Git 필터 index 매핑, snapshot 채우기 |
| `presentation/view_snapshot.h` | `switch_query_action`, `switch_dialog_view` 필드 6개 |
| `presentation/list_metrics.h` | 상수 5개 (위 표) |
| `presentation/ui/ui_element.h` | `ui_element_kind` 3개 |
| `presentation/ui/dialog_elements.{h,cpp}` | 공용 텍스트 박스 element |
| `presentation/ui/settings_dialog_element.cpp` | 지역 `timeout_input_element` → 공용으로 교체 |
| `presentation/ui/switch_dialog_element.cpp` | 질의 막대 배치·그리기 |
| `presentation/ui/ui_interaction.cpp` | 초점 kind 일반화, 문자 분기, Enter |

## test 계획

| test | 내용 |
|---|---|
| `svn_repository_browser_tests` | 기존 "현재 위치까지 자동으로 펼친다"를 **"열면 root만 조회한다"**로 바꾼다. reveal 성공·중간 실패·이미 cache된 경로 즉시 도달, `reset` 후 재조회 |
| `switch_dialog_tests` | Git 필터 걸림·선택 index 매핑·질의 변경이 선택을 지움·Enter가 첫 후보를 고름·지우기·갱신이 재조회를 냄. SVN 선택이 박스를 덮어씀·이동·없는 경로 메시지·갱신이 하위 cache를 버림 |
| `ui_interaction_tests` | 질의 박스 초점 획득/상실, 문자가 `edit_switch_query_intent`로 감, Enter가 `submit_switch_query_intent`, dialog가 닫히면 초점 해제 |
| `ui_tree_build_tests` | 전환 dialog에 세 element가 생기고 조회 중에는 비활성 |

---

# 후속 지시 (R1~R3) — 2026-08-22

상태: **구현 완료** (같은 브랜치에서 이어서 진행했다).

| 항목 | 요구 |
|---|---|
| R1 | SVN은 창을 열 때 **자기 branch까지는** 재귀 `svn ls`가 필요하다. Q3의 판단을 정정한다 |
| R2 | 텍스트 박스에 드래그 범위 선택, caret 이동, Ctrl+C·V·X 등 "일반적으로 기대하는 텍스트 편집 기능"을 갖춘다 |
| R3 | 로그 헤더의 경과 시간을 명령이 끝나도 남긴다. 처음엔 없고, 실행 중엔 흐르고, 끝나면 그대로 서 있다가, 다음 명령이 시작하면 새 값으로 갱신한다 |

## R1. 여는 순간의 reveal을 되살린다

Q3에서 지운 것은 **자동 하강 기계**가 아니라 *목표 없는* 하강이었다. reveal 기계는
그대로 두고, dialog가 SVN 브라우저를 만들 때 `reveal_url`을 현재 작업 복사본
경로로 세운다.

- 여전히 **현재 위치로 가는 길만** 조회한다. 형제 가지(`branches` 등)는 열지
  않으므로 "저장소를 미리 다 읽는" 동작은 아니다.
- 목표 노드 자신은 펼치지 않는다. 그 아래는 사용자가 열 때 조회한다.
- `svn_repository_browser` 모듈은 그대로다 — `make_svn_repository_browser`는
  목표를 세우지 않고, 목표를 세우는 것은 logic이다. 모듈 test는 "목표가 없으면
  내려가지 않는다"를 계속 확인한다.

## R2. 텍스트 편집

### R2.1 편집 모델은 domain에 둔다

`domain/text_edit.{h,cpp}`에 글꼴도 UI도 모르는 순수 모델을 둔다. caret과 anchor는
**UTF-8 byte offset**이며 항상 문자 경계다.

```cpp
struct text_edit_state { std::u8string text {}; std::size_t caret {}; std::size_t anchor {}; };
enum class text_edit_motion { left, right, word_left, word_right, line_start, line_end };
```

- 선택 = `[min(caret, anchor), max(caret, anchor))`. 둘이 같으면 선택이 없다.
- 낱말 경계: ASCII 영숫자와 비 ASCII 바이트(≥0x80)가 낱말 문자, 나머지가 구분자다.
  branch 이름의 `/`·`-`와 경로 구분자에서 Ctrl+←/→가 멈춘다.
- 글이 밖에서 바뀌면(`text_edit_set_text`) caret·anchor를 범위 안 문자 경계로
  되돌린다.

### R2.2 편집 intent를 하나로 모은다

`edit_settings_timeout_intent`와 `edit_switch_query_intent`를 없애고 대상과 명령을
담은 하나로 바꾼다. 텍스트 박스가 늘어도 intent는 늘지 않는다.

```cpp
enum class text_input_target { settings_timeout, switch_query };
enum class text_edit_command {
    insert, backspace, delete_forward,
    move_left, move_right, move_word_left, move_word_right, move_line_start, move_line_end,
    select_all, select_word, cut, place_caret,
};
struct edit_text_input_intent
{
    text_input_target target {};
    text_edit_command command {};
    std::u8string text {};      // insert 전용 (UTF-8 조각)
    std::size_t offset { 0 };   // place_caret·select_word 전용
    bool extend { false };      // Shift로 선택을 늘린다
};
```

`cut`은 **지우기만** 한다 — 복사는 클립보드를 만질 수 있는 UI thread가 이미 했다.

logic은 대상별 후처리를 그대로 유지한다.

- `settings_timeout`: 넣는 글에서 숫자만 남기고 4자리를 넘기지 않는다. 글이 바뀌면
  `timeout_defined`를 켠다.
- `switch_query`: Git이면 글이 바뀔 때 선택·확인 단계·스크롤을 되돌린다.

### R2.3 포인터 → offset은 element가 옮긴다

글꼴이 필요한 계산이라 logic thread(`build_ui_tree`)에서는 할 수 없고, 그리기
thread의 상태를 입력 thread가 읽으면 경쟁이 된다. 그래서 **측정 함수를 입력
thread에 주입**하고, 계산은 element가 한다.

```cpp
// 글자 폭을 재는 함수다. 앱은 내장 글꼴로 만들고 test는 고정 폭 가짜를 넣는다.
using text_measurer = std::function<float(std::u8string_view text, float pixel_size)>;

// ui_element의 새 virtual이다. 텍스트 박스가 아니면 nullopt다.
[[nodiscard]] virtual std::optional<std::size_t> offset_at(float x, const text_measurer&) const;
```

- 글자 크기(11 × scale)는 element가 arrange에서 기억하므로 그리기와 hit test가
  같은 값을 쓴다 — 두 곳이 어긋날 수 없다.
- `interaction_controller`가 `interaction_config`로 측정 함수를 받는다. 없으면
  caret을 글 끝에 둔다(첫 프레임 안전장치).
- **가로 스크롤**도 같은 규칙으로 양쪽에서 계산한다: caret이 보이도록 최소한만
  밀고 `[0, max(0, 글 폭 - 안쪽 폭)]`으로 자른다. 글·caret·폭만으로 정해지므로
  그리기와 hit test가 같은 값을 낸다.

### R2.4 마우스와 키

| 입력 | 결과 |
|---|---|
| 누름 | `place_caret(offset, extend=false)` — `pointer_drag_target::on_press` |
| 누른 채 이동 | `place_caret(offset, extend=true)` — 드래그 범위 선택 |
| 더블 클릭 | `select_word(offset)` |
| ←/→ | `move_left`/`move_right` (+Ctrl → word, +Shift → extend) |
| Home/End | `move_line_start`/`move_line_end` (+Shift → extend) |
| Backspace (WM_CHAR U+0008) | `backspace` |
| Delete | `delete_forward` |
| Ctrl+A | `select_all` |
| Ctrl+C | 선택을 클립보드로 (`clipboard_copy_request`) |
| Ctrl+X | 위와 같이 복사한 뒤 `cut` |
| Ctrl+V | `clipboard_paste_request { target }` |

- `key_code`에 `arrow_left`·`arrow_right`·`home`·`end`·`delete_forward`·`key_a`·
  `key_c`·`key_v`·`key_x`를 더하고 `key_pressed_event`에 `shift`를 더한다.
- 글자 키(A·C·V·X)는 **Ctrl이 눌린 동안에만** `key_pressed_event`로 만든다.
  그러지 않으면 WM_CHAR가 막혀 그 글자를 칠 수 없다.
- 초점을 가진 텍스트 박스가 없으면 새 키는 모두 지금처럼 무시된다.

### R2.5 클립보드

`input_action` variant에 둘을 더한다.

```cpp
struct clipboard_copy_request { std::u8string text {}; };
struct clipboard_paste_request { text_input_target target {}; };
```

- 복사할 글은 입력 thread가 tree에서 읽는다 — element에 `text_input()` virtual을
  두어 글·caret·anchor를 내준다.
- 붙여넣기는 UI thread가 클립보드를 읽어(`read_text_from_clipboard`) 대상 target의
  `insert` intent를 logic에 보낸다. 여러 줄이면 첫 줄만 쓴다(한 줄 박스다).
- 클립보드는 계속 UI thread 전용이다 (기존 규칙).

### R2.6 그리기

- 선택 범위를 `accent_soft` 18%로 깔고 그 위에 글을 그린다.
- caret은 이제 글 끝이 아니라 caret offset 위치에 선다.
- 글이 칸보다 길면 잘라 `…`를 붙이는 대신 **clip + 가로 스크롤**로 그린다. 편집
  중에는 잘린 표시보다 caret 주변이 보이는 것이 중요하다.

## R3. 경과 시간을 명령이 끝나도 남긴다

`card_state`에 `std::optional<duration> change_elapsed`를 더한다.

| 시점 | `change_started_at` | `change_elapsed` | 화면 |
|---|---|---|---|
| 문서를 열고 명령 없음 | — | — | 표시 없음 |
| 실행 중 | 시작 시각 | — | 흐른다 (UI timer가 1초마다 다시 그린다) |
| 끝난 뒤 | — | 걸린 시간 | 그 값으로 서 있다 |
| 다음 명령 시작 | 새 시작 시각 | — | 다시 흐른다 |

- view는 두 값을 모두 담고 element는 `change_started_at`이 있으면 흐르는 값을,
  없고 `change_elapsed`가 있으면 그 값을 그린다.
- UI thread의 1초 timer는 `change_started_at`만 보므로 멈춘 값에서는 저절로 꺼진다.
