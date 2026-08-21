# G2 — 도구 막대의 새 문서 만들기 버튼 제거

날짜: 2026-08-22 · 설계: `docs/global-settings-and-ui-fixes-design.md` (G2)

## 1. 변경 요약

새 문서 만들기 진입점이 시작 페이지에 있으므로 상단 도구 막대의 생성 버튼을
제거했다. 도구 막대는 "문서 없음 = 열기", "문서 있음 = 닫기"만 남는다.

## 2. 구현 경계

- `toolbar_element`에서 생성 버튼과 `generation_busy` 매개변수를 걷어냈다.
  `ui_element_kind::toolbar_generate_document`도 제거했다.
- 시작 페이지의 `새 문서 만들기…`(`start_page_generate_document`)는 그대로다.
- `view_snapshot::document_generating`은 유지한다. logic이 생성 중복 실행을
  막는 상태의 표면이고 기존 logic test가 검증한다. UI 소비처는 현재 없다.

## 3. 검증

| 테스트 | 결과 |
|---|---|
| `The toolbar has no generate button and the close button follows the document` (기존 test 개편) | 통과 |
| 도구 막대의 생성 버튼 클릭 test 삭제 (시작 페이지 클릭 test가 같은 명령을 검증) | — |
| 기존 기능 접촉분: `[tree],[interaction],[close-document],[notice],[start-page]` 33 case | 통과 (289 assertions) |

빌드: `vs2022-tests` Debug에서 `gitman_tests`·`gitman`(앱) 모두 성공.

## 4. 남은 수동 검수

- 문서가 없는 시작 화면에서 도구 막대에 열기 버튼만 보이고, 새 문서 만들기는
  시작 페이지에서 동작하는지 확인.
