# 실환경 피드백 F4 — 로컬 변경 확인 다이얼로그 + diff viewer

날짜: 2026-08-20 · 설계: `docs/field-feedback-design.md` 2.3

## 1. 변경 요약

미추적·수정이 무엇인지 눈으로 확인하는 세로 분할 다이얼로그를 신설했다.
상단이 변경 항목 목록(종류 배지 + 경로), 하단이 선택 항목의 diff viewer다.

- **도메인**: `local_change_entry { kind, path }`와 종류 배지
  (`domain/local_changes.{h,cpp}`), 표시 상한 256 KiB 상수.
- **조회 2종** (`operation_kind::query_local_changes` / `query_file_diff`):
  - 목록: status 명령을 실행하되 항목 경로를 보존한다. 파서에
    `collect_git_local_changes`/`collect_svn_local_changes` 순수 함수 추가
    (레코드 종류·XY/상태 문자 → 종류 매핑, 무시·외부 항목 제외).
  - diff: 추적 파일은 `git diff HEAD --no-color -- <path>` /
    `svn diff -- <path>`(신규 빌더, local_query 부류). 미추적 파일은 VCS 명령
    없이 파일을 직접 읽어 전체를 "추가" 취급으로 표시 —
    `vcs_file_probe::read_prefix` 계약을 신설했다(win32 구현 + fake, 파일/
    디렉터리/부재 구분, 상한 잘림). NUL 감지 시 "이진 파일", 미추적 디렉터리는
    "내부 파일은 표시하지 않습니다", 256 KiB 초과는 생략 안내.
- **provider 계약 확장**: `query_local_changes`/`query_file_diff` virtual 2개
  (Git/SVN 구현), executor 실행·실패 fallback 경로, scheduler는 카드 lane.
- **logic**: `local_changes_dialog_state` — 열기(목록 조회 제출) → 목록 도착 시
  **첫 항목 자동 선택** + diff lazy 조회 → 행 선택 시 재조회, 늦은 결과는
  operation id로 폐기. 빈 목록은 "표시할 로컬 변경이 없습니다." 안내. 문서
  교체 시 닫힘.
- **UI**: `local_changes_dialog_element` — dim 배경(클릭 닫기), 목록 행
  (배지 chip + 경로, 선택/hover 강조, 휠 스크롤), diff pane(줄 첫 문자 기반
  색상: +초록/−빨강/구조 줄 흐림, 가시 범위만 그리기, 하단 안내 띠), 닫기
  버튼. `classify_diff_line` 순수 함수(`presentation/diff_presentation`).
  휠은 diff pane 위에서 diff를, 그 밖에서 목록을 스크롤하고 Esc가 닫는다.
- **진입점** (설계 조정): 카드 body **더블 클릭**. 상태 chip은 그리기 시점에
  폭이 정해져 hit 대상이 될 수 없어 chip 클릭을 대체했다(설계 2.3에 기록).
  컨텍스트 메뉴 진입점은 F5에서 추가된다.
- 차단 문구에 안내 문장 추가는 F5(메뉴 명칭 확정) 이후 정리한다.

재검수 반영 (2026-08-20):

- **목록 행**: 미추적 항목에 `$(file)`/`$(folder)` codicon과 흐림(비활성 느낌)
  처리, 배지 중립색.
- **외부 열기**: 행 오른쪽 `$(vscode)`/`$(folder-opened)` 아이콘 버튼. 경로는
  아이콘과 겹치기 전에 `…`으로 줄인다(`elide_text`). 인자를 담는
  `open_external_request`를 input_action에 신설하고 input thread → app_runtime
  큐 → UI thread shell 실행(`explorer.exe /select,`, PATH의 `code.cmd`) 경로를
  추가했다. VSCode가 없으면 조용히 아무 일도 하지 않는다.
- **2-way diff**: `build_two_way_diff` 순수 함수가 unified diff를 좌우 행으로
  정렬(짝짓기·빈 칸·문맥 양쪽·구조 줄 전체 폭)하고, pane은 가운데 구분선과
  좌(빨강)/우(초록) 배경 틴트로 그린다.
- **스크롤 막대**: 목록·diff 각각에 끌기 가능한 `scrollbar_element`를 추가,
  내용이 넘칠 때만 표시. 막대 표시 시 행 폭 축소.

3차 검수 반영 (2026-08-20):

- **탭 표시**: 렌더러가 `\t`를 그리지 못해 diff 줄이 깨져 보였다.
  `build_two_way_diff`가 탭을 4칸 공백으로 펼친다(열 정렬은 흉내 내지 않음).
- **explorer 열기 수정**: status 상대 경로의 `/` 구분자가 섞인 절대 경로를
  `explorer /select,`가 받지 못했다. `join_local_change_path`가 구분자를
  Windows `\`로 통일한다.
- **배지 안 codicon**: 미추적의 `$(file)`/`$(folder)` 아이콘을 배지 라벨 밖이
  아니라 **배지 상자 안(글자 앞)** 에 그리도록 옮겼다.
- **로그 pane 상시 표시**: 카드를 클릭해야 pane이 열리던 UX를 제거했다.
  pane은 항상 열려 있고, 선택 카드가 없으면 "카드를 선택하면 로그가
  표시됩니다" 제목의 빈 pane이다 (`has_log_pane()` 상시 true).
- **update 확인 overlay 제거**: submodule 여부를 매번 묻지 않는다. 문서
  `settings.update_submodules`(boolean, 기본 off = ADR-003 유지)가 정하고,
  환경설정 4행에 켜기/끄기 토글을 추가했다. 카드 update 버튼은 Git/SVN 모두
  곧바로 실행하며 logic이 settings 값을 options에 채운다. overlay 관련
  intent 4종·element·view·escape 분기·상태를 전부 제거했다.

4차 검수 반영 (2026-08-20, 환경설정 시각 위계):

- 제목에 `$(settings-gear)` codicon을 붙이고 bold(embolden)로 강조.
- 세부 기능 타이틀 4종은 키 컬러(positive accent) + semi-bold로 강조하고,
  같은 줄의 버튼·토글은 타이틀과 겹치지 않게 15px 아래로 내렸다.
- submodule 설정은 텍스트 버튼 대신 **토글 스위치**(트랙 색 + 손잡이 위치로
  현재 값 표시, 클릭 시 뒤집기)로 교체 — element 종류·intent는 유지라 기존
  테스트가 그대로 통과한다.
- 타이틀이 아닌 본문(경로·설명)은 알파 0.65(안내 문구 0.45)로 낮춰 위계를
  만들었다.

## 2. 테스트 (이 단계 추가·직접 영향분만 실행)

- 신규 14 case (`tests/local_changes_tests.cpp`): 파서 매핑(git/svn), diff 빌더
  인자, 미추적 읽기(추가 줄 변환·이진·디렉터리·부재), 표시 상한, git provider
  목록·추적 diff·미추적 무프로세스 읽기, logic 열기→자동 선택→diff, 선택
  전환·늦은 결과 폐기·닫기, 빈 목록 안내, diff 줄 분류, dialog tree·행 클릭·
  휠 라우팅·Esc, 카드 더블 클릭 진입, **2-way 짝짓기, 행 플래그·절대 경로·열기
  아이콘의 open_external_request, 스크롤 막대 표시 조건**.
- 3차: overlay 테스트를 settings 기반 update 테스트로 재작성(설정 off/on 반영,
  카드 버튼 직행), settings 토글 테스트 추가, 로그 pane 상시·경로 정규화
  단정 갱신.
- 직접 영향 범위: provider 계약·probe 계약·logic dispatch·input_action variant
  확장·ui tree/상호작용·로그 pane·settings —
  `[logic],[ui],[runtime],[schema],[store],[log],[update-ui],[local-changes],[settings-ui]`
  163 case 통과 (VS2022 Debug). 전체 suite는 큰 단계 종료 시 실행한다.

## 3. 빌드·스타일

- VS2022 Debug: `gitman_tests`, `gitman` 빌드 성공
- `check_source_style.ps1` 통과 (커밋 전 재확인)

## 4. 수동 확인 권장 항목

1. 카드 더블 클릭으로 다이얼로그가 열리고 목록·첫 항목 diff가 나오는지
2. 2-way diff의 좌(빨강)/우(초록) 정렬과 가운데 구분선, 행 전환 동작
3. 미추적 행의 파일/폴더 아이콘·흐림 처리, 긴 경로의 `…` 말줄임과 아이콘 유지
4. `$(vscode)`로 VSCode가, `$(folder-opened)`로 탐색기(대상 선택)가 열리는지
   (VSCode 미설치 시 아무 일 없음)
5. 이진 파일·미추적 디렉터리·큰 파일(256 KiB 초과)의 안내 문구
6. 휠·스크롤 막대(끌기)가 목록/diff 영역에서 따로 동작하는지, Esc·배경·닫기
7. SVN 작업 복사본에서 목록·diff가 나오는지 (SVN 실측은 기존 후속 항목 범위)
8. 탭이 든 파일의 diff가 깨지지 않는지, explorer 열기가 실제로 동작하는지
9. 로그 pane이 시작부터 열려 있고 카드 선택 시 그 카드 로그로 바뀌는지
10. update 버튼이 바로 실행되고, 환경설정 submodule 토글이 반영·저장되는지
