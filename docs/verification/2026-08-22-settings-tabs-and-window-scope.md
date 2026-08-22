# 검증 기록 - 환경설정 탭 구조 · 외양의 문서 특수화 · 창 배치 범위 · 덮어씀 배지

- 일자: 2026-08-22
- 대상: `feature/settings-tabs-and-window-scope` 브랜치 (C1~C6)
- 기준 문서: `docs/settings-tabs-and-appearance-scope-design.md`
- 진행 방식: 사용자 지시로 체크포인트 검수 없이 브랜치에서 커밋까지 자동 진행하고
  마지막에 전역 test를 실행했다.

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2022-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2022-tests-debug` | **755/755 통과** (실 Git·SVN 네트워크 3건 skip은 test 내부 skip) |
| `cmake --build --preset vs2022-tests-release` | 성공 |
| `ctest --preset vs2022-tests-release` | **755/755 통과** |
| `cmake --build --preset vs2022-analysis` | 성공, 분석 경고 없음 |
| `cmake --build --preset vs2022-release` (앱) | `build/vs2022/src/Release/gitman.exe` 생성 |
| `scripts/check_source_style.ps1 -root .` | 439 files 통과 |
| clang-format (변경 파일) | 위반 없음 |

이전 기준선(714 case)에서 41 case가 늘었다. 이번 작업의 신규 test는 8 case다.

- `A document keeps the window placement out of the app settings`
- `Closing the document returns the placement to the app settings`
- `Schema parser reads the document appearance override`
- `Project store round-trips the document appearance`
- `The appearance follows the document scope while a document is open`
- `The appearance edits the app settings without a document`
- `The settings dialog shows one tab at a time and keeps the drafts`
- `The override badge sits in the column beside the item title`
- `Every settings control stays inside the panel on all tabs`
- 도메인 test(`domain_model_tests`)에 `appearance_overrides` 합성 검사를 더했다.

## 2. 화면 확인

환경설정 dialog를 tree 빌드 → Skia raster surface로 그려 네 탭과 두 테마를
확인했다(확인용 임시 코드는 커밋하지 않았다).

- `도구`(문서·다크): 실행 파일 2항목 + 조회 1항목, Git 항목에 `덮어씀` 배지와
  블록 바탕이 붙는다. 제목은 배지 열만큼 들여써 세로로 정렬된다.
- `작업`(문서·다크): 업데이트·상태 확인·로그 세 섹션, 토글 3개가 제목 줄
  오른쪽에 붙는다.
- `외양`(문서·다크): 테마 세그먼트 3칸, 색 20개가 12열 2줄 격자로 담기고 선택한
  색만 링이 있다. 키 컬러 항목에 `덮어씀` 배지가 붙는다.
- `외양`(전역·라이트): 배지 열이 없어 14열 2줄이다. 라이트 팔레트에서 글자·배지·
  격자 모두 읽힌다.
- `시스템`(전역·라이트): 파일 연결 설명과 등록·해제 버튼.

panel은 600 × 340이며 탭을 옮겨도 크기가 같다.

## 3. 손으로 확인할 항목 (남음)

실제 창이 필요해 이번 자동 검증에 포함하지 못했다.

- 문서 없이 창을 옮기고 끈 뒤 다시 시작하면 그 배치로 열리는지.
- 문서를 열 때 직전 배치가 앱 설정에 저장되고, 문서를 연 뒤 창을 옮겨 끄면 앱
  설정의 배치는 그대로인지.
- 최대화 ↔ 복원 전환과 크기 조절 종료(`WM_EXITSIZEMOVE`)에서 배치가 반영되는지.
- 고대비 테마에서 탭 rail과 배지의 대비.

## 4. 비고

- `assets/accents.json`은 20색으로 늘렸다. 사용자가 따로 만든 목록이 있으면 같은
  스키마로 파일만 교체하면 되며 코드 변경은 없다.
- 기존에 실패하던 style·format 위반 3건(`win32_application.cpp`,
  `switch_dialog_element.cpp` 2건)과 clang-format 위반 1건
  (`logic_controller.cpp`)을 별도 커밋으로 정리해 `gitman_source_style` test가
  다시 통과한다. main에서도 실패하던 항목이다.
