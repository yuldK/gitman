# 검증 기록 - 실행 취소 · 글꼴 설정 · 새 문서 dialog

- 일자: 2026-08-23
- 대상: `feature/switch-dialog-query-bar` 브랜치 (이어서 진행)
- 기준 문서: `docs/switch-dialog-query-bar-design.md` (R2 보충),
  `docs/fonts-and-in-app-dialogs-design.md`
- 진행 방식: 사용자 지시로 체크포인트 검수 없이 커밋까지 진행하고 마지막에 전역
  test를 실행했다.

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2026-tests-debug` | **789/789 통과** |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| `cmake --build build/vs2026 --config Debug` (앱) | 성공 |
| `scripts/check_source_style.ps1 -root .` | 452 files 통과 |
| clang-format (변경 파일) | 위반 없음 |

## 2. 텍스트 편집 보충

`text_edit_state`에 undo/redo 기록을 넣고 편집을 덩어리로 묶는다. 묶는 기준은
"같은 종류 + caret이 직전 편집이 끝난 자리에서 이어짐"이고, caret 이동·선택은
덩어리를 끊는다.

신규 test 5 case (`text_edit_tests`).

- `Text edit undo groups a typing run and redo replays it`
- `Text edit undo breaks the group when the caret moves`
- `Text edit undo restores the selection that a replacement consumed`
- `Text edit deletes whole words with Ctrl`
- `Text edit history is dropped when the text comes from outside`

신규 test 1 case (`switch_dialog_tests`).

- `The query bar routes undo, word delete and the legacy clipboard keys` —
  Ctrl+Z/Y/Shift+Z, U+007F(Ctrl+Backspace), Ctrl+Delete, Ctrl/Shift+Insert,
  Shift+Delete, Shift+클릭, 세 번 클릭 전체 선택.

낱말 지우기의 실제 동작을 test로 못박았다. `origin/feature-a`에서 Ctrl+Backspace는
먼저 `a`만, 다음에 `feature-`까지 지운다 (`-`가 구분자다). Ctrl+Delete는 다음 낱말의
시작까지 지운다 — 둘 다 Windows 관례와 같다.

## 3. 글꼴

- 앱 설정 JSON에 `fonts: { ui, code }`가 추가된다. 빈 값이 내장 글꼴이다.
- `SkFontMgr_New_GDI()`가 이 Skia build에 링크되어 있어 가족 열거와 해석이 된다
  (DirectWrite fontmgr은 빌드에 없다).
- 글꼴 registry는 process 단위 cache다. 업무 상태가 아니라 OS 자원 cache라
  ADR-004의 단일 소유 규칙 밖이며, 그리기 thread와 입력 thread가 같은 값을 봐야
  하므로 공유가 필요하다.
- 내장 글꼴도 cache에 넣는다. 매번 새로 만들어 돌려주면 미리 보기 해석기가 raw
  포인터로 받는 순간 수명이 끊긴다 (실제로 refcount 단언에 걸려 고쳤다).

## 4. 새 문서 dialog

`platform/win32/version_list_generation_dialog.{h,cpp}` 753줄을 지우고 앱 UI
dialog로 옮겼다. 폴더 선택만 Win32 dialog가 맡는다.

검증 위치가 바뀐 것을 기록한다.

- **어휘 검증**(빈 이름, 파일 이름으로 쓸 수 없는 문자, 폴더 미지정)은 logic이 한다.
- **존재·중복 확인**은 지금처럼 worker의 생성 service가 원자적으로 한다. 이전
  Win32 dialog가 하던 "폴더를 찾을 수 없습니다"·"같은 이름의 문서가 이미 있습니다"
  인라인 검사는 사라졌고, 대신 생성 실패 사유가 dialog에 그대로 표시된다. 폴더는
  Win32 폴더 dialog가 고르므로 존재가 보장되고, 중복은 생성 service가
  `generation_output_exists`로 정확히 알린다.

## 5. 화면 확인

tree 빌드 → Skia raster로 그려 확인했다(임시 코드는 커밋하지 않았다).

- 글꼴 선택 dialog: `(기본 글꼴)` 다음으로 설치된 글꼴이 이름 순으로 서고, 각 행이
  자기 글꼴로 그려진다(Algerian·Arial Black 등이 눈에 띄게 다르다). 미리 보기는
  고른 글꼴로 그려지며, 한글 글리프가 없는 글꼴은 한글이 두부(□)로 보인다 — 그
  자체가 판단 근거다.
- 새 문서 dialog: 이름 칸·저장소 폴더·저장 위치·`저장소 폴더에 만들기` 토글·만들
  경로 미리 보기가 한 panel에 서고, 색·버튼 모양이 창의 나머지와 같다. `저장소
  폴더에 만들기`가 켜져 있으면 저장 위치 칸이 흐려진다.

## 6. 손으로 확인할 항목 (남음)

- 실제 창에서 Ctrl+Z/Y가 시스템 클립보드 조작과 섞였을 때의 순서.
- 한글 IME로 칠 때 조합 창이 캐럿 아래에 붙는지. 조합 중인 글자를 칸 안에 직접
  그리지는 않는다 — 시스템 IME 창이 보여 준다.
- UI 글꼴을 바꾼 뒤 caret 자리와 클릭 위치가 맞는지 (측정 함수와 그리기가 같은
  registry를 보므로 어긋나지 않아야 한다).
- 새 문서 dialog에서 이미 있는 이름으로 만들 때 실패 사유가 dialog에 뜨는지.
