# T2 — 배너·카드 경로의 unix 스타일 표시

날짜: 2026-08-22 · 설계: `docs/theme-and-banner-menu-design.md` T2 (체크포인트 C1)

## 1. 변경 요약

화면에 그리는 경로의 구분자를 `/`로 통일했다. 배너의 문서 주소, 카드의 경로
(전체·상대 표시 모두), 시작 페이지 최근 항목의 폴더가 대상이다. 저장·실행에
쓰는 경로는 Windows 원형 그대로 남는다.

## 2. 구현 경계

- `domain/path_syntax.h`에 `to_display_path`를 추가했다. 순수 문자열 변환이며
  `\`만 `/`로 바꾼다(길이·내용은 그대로).
- `view_snapshot`에 `document_display_path`를 추가했다. `document_path`는
  Windows 원형을 유지한다 — UI thread가 탐색 폴더 선택의 시작 폴더와 셸 실행
  경로로 쓰는데 Win32 파일 dialog는 `/`를 받지 못한다. 표시 문자열을 logic이
  만드는 것은 카드의 `path`와 같은 규칙이라 element는 그대로 그리기만 한다
  (설계에서는 `build_ui_tree` 변환을 적었으나 표시 문자열의 주인을 한 곳으로
  모으고 test로 확인할 수 있게 조정했다. 설계 문서 T2.2에 반영했다).
- `logic_controller::display_path`와 시작 페이지 `folder` 조립에 같은 함수를
  적용했다. 상대 경로(`..\other\beta`)도 `../other/beta`로 나온다.
- 문서 JSON 저장, 최근 목록의 `path`, 로그, 프로세스 인자, 필터 대상은 바뀌지
  않았다.

## 3. 검증

빌드: `cmake --build --preset vs2026-tests-debug` (경고 없음)

| 테스트 | 결과 |
|---|---|
| `to_display_path` 6종(절대·상대·혼합 구분자·UNC·구분자 없음·빈 값) | 통과 |
| snapshot이 원형 `document_path`와 표시용 `document_display_path`를 함께 담는다 | 통과 |
| 카드 경로가 전체·상대 표시 모두 `/`로 나온다 | 통과 |
| 시작 페이지 최근 폴더는 `/`, 여는 경로는 원형 | 통과 |
| 신규 4 case (19 assertions) | 통과 |
| 영향 범위 재실행 `[start-page],[ui],[logic],[workspace],[display],[domain]` 244 case (5,330 assertions) | 통과 |

기존 test 1건(`start_page_tests.cpp`의 최근 폴더 표기)을 새 규칙으로 갱신했다.

## 4. 남은 수동 검수

- 앱을 열어 배너의 문서 주소와 카드 경로가 `C:/work/alpha` 형태로 보이는지,
  경로 표시 토글을 켰을 때 `../other/beta` 형태인지 확인.
