# 검증 기록 - 전환 dialog 질의 막대 · SVN lazy 탐색

- 일자: 2026-08-22
- 대상: `feature/switch-dialog-query-bar` 브랜치
- 기준 문서: `docs/switch-dialog-query-bar-design.md`
- 진행 방식: 설계 초안을 검수받아 3건(Git 우측 버튼은 지우기, SVN 갱신은 선택
  노드, `현재 위치로` 버튼 없음)을 확정한 뒤, 사용자 지시로 체크포인트 검수 없이
  브랜치에서 커밋까지 진행하고 마지막에 전역 test를 실행했다.

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2026-tests-debug` | **772/772 통과** (환경 gate 3건 skip) |
| `ctest --preset vs2026-tests-release` | **772/772 통과** |
| `cmake --build --preset vs2026-analysis` | 성공, 분석 경고 없음 |
| `cmake --build build/vs2026 --config Debug` (앱) | `gitman.exe` 생성 |
| `scripts/check_source_style.ps1 -root .` | 443 files 통과 |
| clang-format (변경 파일 17개) | 위반 없음 |

기존 위반이 남아 있는 항목이라 이번 변경과 무관하게 실패하는 것 두 가지를
기록한다.

- `gitman_format_check`는 이번에 건드리지 않은 파일 20개에서 clang-format 위반을
  보고한다 (`git_command_builder`, `card_element`, `ui_tree_build_tests` 등).
  변경 파일 17개는 모두 통과한다.
- Release 구성에서 `gitman_messaging_asan_tests` link가 Catch2의
  `annotate_optional` 불일치(LNK2038)로 실패한다. vcpkg가 준 Catch2와 ASan 옵션의
  조합 문제이고 source와 무관하다. `gitman_tests`는 정상 build·통과한다.

## 2. 신규 test (16 case)

`tests/svn_repository_browser_tests.cpp`

- `SVN browser loads only the root when it opens` — 기존 "현재 위치까지 자동으로
  펼친다"를 대체한다. 현재 위치가 두 단계 아래여도 root 조회 하나로 끝난다.
- `SVN browser reveals a requested path one level at a time`
- `SVN browser reports a reveal target that the listing does not contain`
- `SVN browser reveal gives up when a step fails`
- `SVN browser reset drops a subtree so it can be queried again`
- `SVN browser translates between display paths and URLs`

`tests/switch_dialog_tests.cpp`

- `The Git query bar filters candidates while it is typed` — 대소문자 무시 필터와
  거른 목록 기준 선택 index가 원본 후보로 되돌아가는 것까지 확인한다.
- `Editing the Git query drops the selection and empty results say so`
- `Enter picks the first filtered Git candidate`
- `Refreshing the Git switch dialog queries again and keeps the filter`
- `The SVN address bar starts at the current path without descending`
- `The SVN address bar walks down to the typed path one query at a time`
- `The SVN address bar reports a path the repository does not have`
- `Selecting an SVN node rewrites the address bar`
- `Refreshing the SVN dialog requeries the selected directory`
- `The switch dialog tree wires the query bar` — 세 element의 활성 상태, 클릭
  intent, 박스 초점 획득, 문자·Enter 전달, dialog가 닫힐 때 초점 해제.

## 3. 화면 확인

tree 빌드 → Skia raster surface로 그려 확인했다(확인용 임시 코드는 커밋하지
않았다. Skia가 PNG encoder 없이 build되어 raw 픽셀을 덤프한 뒤 변환했다).

- Git(다크, 800 × 600): 제목 `전환 - alpha` 아래에 갱신 · 입력 칸 · `✕` 한 줄이
  놓이고, 목록 경계가 그 아래에서 시작한다. 입력 칸에 `feat`가 들어 있고 목록에는
  `origin/feature-a`와 선택된 `feature-b`만 남는다. panel은 400 × 352다.
- SVN(다크, 800 × 600): 주소 막대에 `/trunk/source`, 오른쪽 버튼이 `→`다. 트리는
  root · branches · trunk · source 순이고 `source`가 `(현재 위치)`로 선택되어
  있다. panel은 640 × 462다.
- 두 경우 모두 막대가 제목과 목록 사이에 들어가면서 목록 높이(190 / 300)는
  그대로이고 버튼 줄과 겹치지 않는다.

## 4. 손으로 확인할 항목 (남음)

실제 SVN 서버와 창이 필요해 자동 검증에 넣지 못했다.

- 깊은 작업 복사본에서 dialog를 열 때 `svn ls`가 한 번만 나가는지(전에는 depth
  만큼 연쇄로 나갔다).
- 주소 막대에 한글이 든 경로를 쳐서 이동할 때 percent-encoding이 맞는지.
- 갱신 버튼으로 새로 만든 branch가 dialog를 닫지 않고 보이는지.
- 텍스트 박스가 BMP 문자만 받는다 (`WM_CHAR`의 surrogate 쌍은 버린다). 저장소
  경로에 BMP 밖 문자가 든 경우는 주소 막대로 칠 수 없고 트리에서 골라야 한다.
