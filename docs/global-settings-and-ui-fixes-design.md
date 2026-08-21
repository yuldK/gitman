# 전역 설정 계층 · 앱 단위 창 배치 · UI 결함 수정 설계

상태: **구현 완료** (2026-08-22 작성·검수·구현. 검증 기록:
`docs/verification/2026-08-22-g4-switch-scroll-tooltip.md` ·
`2026-08-22-g5-undetermined-color.md` · `2026-08-22-g2-toolbar-generate-removal.md` ·
`2026-08-22-g1-app-window-placement.md` · `2026-08-22-dialog-border-and-elapsed-time.md`
(후속 지시) · `2026-08-22-g3-global-settings.md`)

2026-08-22 사용자 요구 5건의 설계를 기록한다.

| 항목 | 요구 |
|---|---|
| G1 | 앱 단위 pos + size 저장: 마지막 열었던 프로젝트의 위치·크기에 맞춰 저장 |
| G2 | 새 문서 만들기가 시작 페이지에 있으므로 상단 도구 막대에서 제거 |
| G3 | 문서가 없어도 환경설정 활성화. 전역 설정이 기존 설정을 모두 제공하고, 문서별로 덮어쓴다. 문서에서 정의하지 않은 항목은 앱 설정을 따른다 |
| G4 | 브랜치 전환 dialog 버그: 항목이 많을 때 스크롤 없음 · 휠 스크롤 중 툴팁이 쓸려 다님 |
| G5 | 확인되지 않아 `?`로 표시되는 상태를 키 컬러가 아니라 비활성 색으로 표시 |

---

## G1. 앱 단위 창 배치 저장

### G1.1 현황

창 배치는 `.version-list` 문서의 optional `window` object에만 남는다
(window-state-and-scroll-design 1장). 문서 없이 시작 페이지로 뜨면 항상 기본
크기·기본 위치다. 문서를 여닫는 흐름이 늘어난 지금(시작 페이지·닫기 버튼)
"앱을 다시 열면 마지막 자리"라는 기대가 문서 유무와 무관하게 생겼다.

### G1.2 결정: 배치를 앱 설정으로 옮긴다

`gitman.app-settings.json`에 optional `window` object를 추가한다. 형태와 좌표
규칙은 문서의 `window`와 같다 (`WINDOWPLACEMENT::rcNormalPosition` 그대로,
최대화 여부 별도).

```json
{
    "schema_version": 1,
    "recent_documents": [],
    "window": { "x": 320, "y": 180, "width": 1280, "height": 720, "maximized": false }
}
```

- **갱신**: `WM_CLOSE`의 `window_placement_intent`(기존 경로)를 받으면 logic이
  앱 설정의 `window`를 갱신하고 dirty로 둔다. 종료 처리(`begin_shutdown`)에서
  dirty면 앱 설정 저장을 한 번 내보낸다 — 문서의 종료 저장과 같은 구간이라
  기존 종료 순서(logic close 확인 → worker join)가 그대로 실행을 보장한다.
- **복원**: 앱 설정을 읽은 뒤(`app_settings_loaded_event`) 배치가 있으면
  `view_snapshot`의 기존 `window_placement_request`/`revision` 경로로 1회
  적용을 요청한다. 최소 크기·모니터 겹침 방어는 UI thread의 기존 코드가 그대로
  담당한다.
- **문서의 `window` 필드는 그대로 동작한다** (2026-08-22 검수 결정): 문서를
  열면 지금처럼 문서의 배치를 적용하고, 닫을 때 문서에도 갱신한다. 앱 설정의
  `window`는 그 위에 더해지는 fallback이다 — 문서 없이 시작 페이지로 뜰 때
  마지막 종료 배치를 복원한다. 즉 적용 우선순위는 "여는 문서의 배치 >
  앱 설정 배치 > 기본값"이다.
- 명령줄 인자로 문서를 받아 시작하는 경우에도 적용되는 배치는 앱 설정 하나라
  재배치는 최대 1회다(현행과 같은 허용 범위).

### G1.3 영향 범위

- `domain/app_settings.h`(`window` 필드), `infrastructure/json_app_settings_store`
  (직렬화 왕복), `logic_controller`(배치 intent에서 앱 설정도 함께 갱신, 앱 설정
  로드 후 문서가 없으면 적용 요청, 종료 시 dirty 저장), `win32_application`
  (변화 없음 — 기존 intent·적용 경로 재사용).
- 문서 쪽 배치 코드(`workspace_document::window` 적용·갱신)는 그대로 둔다.

---

## G2. 도구 막대의 새 문서 만들기 제거

- `toolbar_element`에서 `새 문서 만들기` 버튼(`toolbar_generate_document`)을
  없앤다. 진입점은 시작 페이지의 `새 문서 만들기…` 한 곳이 된다 (문서가
  열려 있으면 지금도 버튼이 없으므로 실질 변화는 시작 페이지 화면뿐이다).
- 생성 진행 중 표시(`generation_busy`)는 시작 페이지 쪽 버튼이 이미 같은
  정보를 받으므로 도구 막대 관련 배선만 걷어낸다.
- element kind(`toolbar_generate_document`)와 관련 test를 함께 정리한다.

---

## G3. 전역 설정과 문서별 덮어쓰기

### G3.1 모델: 전역 값 + 문서 override

지금 `workspace_settings`는 문서 전용이며 "값 없음 = 하드코딩된 기본값"이다.
이를 두 층으로 나눈다.

- **전역 설정**: `app_settings`에 `workspace_settings settings {}`를 통째로
  추가한다. 모든 필드가 구체 값이며 기본값은 지금의 기본값과 같다. 앱 설정
  파일의 `settings` object로 저장한다 (없으면 전부 기본값 — 기존 파일 호환).
- **문서 override**: 문서가 갖는 것은 `workspace_settings_overrides` — 모든
  필드가 `std::optional`인 구조체다. `nullopt` = "앱 설정을 따름". 문서 JSON의
  `settings`에는 **명시적으로 덮어쓴 키만** 남긴다.
- **유효 설정**: `apply_overrides(global, overrides) -> workspace_settings`
  순수 함수 하나로 합성한다. logic은 조회·작업 요청을 만들 때마다 이 유효
  설정 사본을 싣는다(지금과 같은 전달 방식이라 worker 쪽은 변화 없다).

기존 문서 호환: 지금 문서에 저장된 `settings` 키들은 그대로 override로
읽힌다. 전역 기본값이 기존 하드코딩 기본값과 같으므로 어떤 문서도 동작이
바뀌지 않는다. `settings`가 없던 문서는 "전부 앱 설정을 따름"이 된다.

`show_relative_paths`도 같은 규칙을 탄다: 도구 막대 토글은 문서가 열려 있을
때 문서 override에 기록한다(현행 유지). 전역 설정에서는 새 문서의 기본 표시
방식을 정하는 값이 된다.

### G3.2 환경설정 dialog

- 도구 막대의 환경설정 버튼은 **항상 활성**이다.
- **문서가 없으면 전역 설정을 편집한다.** 제목을 `환경설정 (전역)`으로 구분
  한다. 행 구성은 지금과 같고 모든 행이 구체 값을 편집한다.
- **문서가 열려 있으면 문서 override를 편집한다.** 제목은 `환경설정 (문서)`.
  덮어쓰기는 **암묵적**이다 (2026-08-22 검수 결정: per-행 체크 없이, 건드리면
  그 옵션을 문서가 사용).
  - dialog 초안은 유효 값(전역 위에 문서 override를 합성한 값)으로 시작하고,
    행마다 문서에 정의되어 있는지의 flag를 함께 든다.
  - 사용자가 어떤 행의 값을 바꾸면 그 행이 "문서에 정의됨"이 된다. 저장 시
    정의된 행만 문서 JSON에 남고, 건드리지 않은 행은 키가 빠져 앱 설정을
    따른다.
  - 덮어쓴 행에는 라벨 줄 오른쪽에 **`덮어씀` 배지**를 붙인다 (2026-08-22 추가
    지시: "앱 설정 따름" 문구 표기 대신). 배지를 클릭하면 그 행의 문서
    override가 삭제되어 앱 설정을 따르고, 초안에는 앱의 값이 다시 보인다.
  - 실행 파일 행의 `지우기`는 두 모드 모두 빈 값(자동 탐색)이다. 문서 모드에서는
    "빈 값으로 덮어씀"이 되고, 정의 삭제는 배지가 담당한다.
- 문서가 열려 있을 때 전역 설정을 고치려면 문서를 닫고 연다(1차 범위).
  필요해지면 dialog 안 전환을 후속으로 둔다.
- `연결 등록`·`연결 해제`는 앱 수준 동작이므로 두 모드 모두에 보인다.
- 설정 변경 후 재조회 규칙: 유효 설정이 실제로 바뀐 경우에만 활성 카드를
  재조회한다(전역을 바꿔도 문서가 전부 덮어쓰고 있으면 재조회하지 않는다).

### G3.3 저장 경로

- 전역 설정 저장은 기존 `save_app_settings` 작업(병합·shadow JSON 보존)을
  그대로 쓴다. 실패는 기존 규칙대로 진단 1건 + 그 세션 메모리 유지.
- 문서 override 저장은 기존 문서 저장(`request_save`) 그대로다.

### G3.4 영향 범위

- `domain/project.h`(`workspace_settings_overrides`, `apply_overrides`),
  `domain/app_settings.h`(`settings` 필드), `json_workspace_document`·
  `json_project_store`(override 직렬화 — 있는 키만), `json_app_settings_store`
  (`settings` object), `logic_controller`(유효 설정 합성, dialog 상태에 모드·
  override 여부 추가, 열림 조건 완화), `view_snapshot`(`settings_dialog_view`
  확장), `settings_dialog_element`(모드 제목, per-행 체크, 따름 문구),
  `toolbar_element`(환경설정 항상 활성), `log_file_sink` 대상 게시 등
  `write_log_files` 소비처는 유효 설정 기준으로 동일 동작.

---

## G4. 브랜치 전환 dialog의 스크롤·툴팁

### G4.1 스크롤 표시

휠 스크롤 경로(`switch_dialog_scroll_intent` → clamp)는 이미 있으나 **스크롤
막대가 없어** 목록이 더 있는지, 어디쯤인지 보이지 않고 끌어서 이동할 수도
없다. 카드 목록과 같은 `scrollbar_element`를 dialog 목록 영역 오른쪽에
추가한다.

- content = 행 수 × 행 높이, viewport = 목록 영역 높이 — logic의 clamp 계산과
  같은 값이라 thumb와 실제 스크롤이 어긋나지 않는다.
- 끌기는 `switch_dialog_scroll_intent`(논리 픽셀 delta)로 보낸다. 기존
  scrollbar의 비율 변환·track 클릭 규칙을 그대로 재사용한다.
- 목록이 viewport보다 짧으면 만들지 않는다.
- 같은 구조의 탐색(discovery) dialog에도 막대가 없다. 같은 방식이 그대로
  적용되므로 이번에 함께 붙인다.
- 증상은 스크롤 막대 부재로 확인되었다 (2026-08-22 검수). 휠 경로는 기존
  코드를 유지한다.

### G4.2 휠 스크롤 중 툴팁이 쓸리는 문제

원인: hover 대상은 포인터가 **움직일 때만** 다시 판정한다. 휠로 내용이
흐르면 커서 아래 행이 바뀌지만 hover는 이전 행 id에 붙어 있고, tree가 다시
만들어지면 그 행의 새 위치에 툴팁이 따라가 위·아래로 쓸려 다닌다.

수정: **포인터가 머문 자리에서 내용이 움직여도 hover를 다시 판정한다.**

- `interaction_controller`가 마지막 포인터 위치를 기억한다(이동·휠 이벤트에서
  갱신).
- 새 tree를 받을 때(`set_tree`) 기억한 위치로 hit test를 다시 수행해 hovered를
  갱신한다. 대상이 바뀌면 hover 시작 시각도 초기화되어 툴팁은 잠시 사라졌다가
  새 행 위에서 지연 후 다시 뜬다 — 쓸려 다니는 움직임이 사라진다.
- input pump가 지금은 이벤트를 받은 턴에만 tree를 갱신하므로, 대기 타임아웃
  (250 ms) 턴에도 tree를 받아 hover를 재판정하고 snapshot이 바뀌면 게시한다.
  휠을 멈춘 뒤 마지막 tree 반영이 이 경로로 들어온다.
- 이 수정은 dialog만이 아니라 카드 목록·로그 등 스크롤되는 모든 화면의 같은
  증상을 함께 고친다.

---

## G5. 확인되지 않은 상태(`?`)의 색

### G5.1 현황

`sync_state_glyph`·`availability_glyph`가 판정 불가일 때 `question` 글리프와
"확인되지 않음" 문구를 준다. 카드의 상태 아이콘과 상태 chip은 색을 **카드
상태**(ready → 키 컬러 초록)에서 가져오므로, 제대로 확인되지 않은 카드가
정상처럼 초록 `?`로 보인다.

### G5.2 결정

`status_glyph`에 `undetermined` flag를 추가하고 `question`을 돌려주는 두
자리에서 true로 둔다. 카드 그리기는 이 flag가 켜져 있으면 상태 강조색 대신
**비활성 표현(중립 전경색을 흐리게, 기존 disabled 카드와 같은 계열)** 을
쓴다.

- 적용 대상: 카드 왼쪽 상태 아이콘, 세 번째 줄의 동기화 상태 chip(배경·글자).
- codicon 이름 문자열(`"question"`) 비교가 아니라 flag를 쓰는 이유: 표현
  규칙을 presentation 한곳에 두고, 글리프가 바뀌어도 의미가 유지되게 한다.

---

## 진행 순서와 검증 계획

작은 항목부터 진행한다: **G4 → G5 → G2 → G1 → G3**. 각 세부단계는 체크포인트
관례(검수 요청 + 제안 커밋 메시지, 해당 단계에서 추가·수정한 테스트만 실행)를
따른다.

| 항목 | 신규·수정 test |
|---|---|
| G1 | 앱 설정 `window` 직렬화 왕복·깨진 값 무시, 로드 후 적용 요청 1회, 종료 시 dirty 저장, 문서 `window` 미적용·보존 |
| G2 | toolbar tree에 생성 버튼 부재, 시작 페이지 진입점 유지 |
| G3 | override 합성 규칙(전부 nullopt→전역, 부분 덮어쓰기), 문서 직렬화(덮어쓴 키만), 앱 설정 `settings` 왕복, dialog 모드별 view 구성, 유효 설정 변경 시에만 재조회 |
| G4 | switch·discovery dialog scrollbar 생성 조건·thumb 계산, tree 갱신 시 hover 재판정(쓸림 없음) |
| G5 | `undetermined` flag 부여 자리, 카드 아이콘·chip 색 선택 |

## 검수 결과 (2026-08-22)

| 질문 | 결정 |
|---|---|
| G1 문서 `window` 처리 | **기존과 동일하게 유지** — 문서를 열면 문서의 배치를 쓴다. 앱 설정 배치는 문서가 없을 때의 fallback |
| G3 덮어쓰기 UI | **암묵 덮어쓰기** — 문서 모드에서 설정을 건드리면 그 옵션을 문서가 사용하고, 문서에 정의하지 않은 옵션은 앱 설정을 따른다 (per-행 체크·탭 없음) |
| G4 증상 | **스크롤 막대가 없음** — 막대 추가로 해결, 휠 경로는 유지 |
