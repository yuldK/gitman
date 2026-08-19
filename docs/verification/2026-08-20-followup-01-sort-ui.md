# 검증 기록 - 후속 항목 1 정렬 전환 UI

## 1. 개요

- 날짜: 2026-08-20
- 대상: 후속 항목 1 "정렬 전환 UI" (`docs/verification/2026-08-19-stage-8.md` 8장,
  2026-08-20 사용자 지시로 후속 항목 일괄 진행 중 첫 번째)
- 진행 방식: 체크포인트 종료 시 검수 요청과 제안 커밋 메시지 첨부, 커밋은 사용자

## 2. 구현 내용

- **toolbar 정렬 전환 버튼**: `set_sort_intent`·`card_sort_key`·logic 처리·정렬
  비교자는 단계 6부터 완성되어 있었고 발신자(UI)만 없었다. toolbar에 순환 버튼
  하나를 추가해 이름순 → 상태순 → 문서 순서(custom) → 이름순으로 전환한다.
  - `ui_element_kind::toolbar_sort` 추가, glyph는 `icon_sort_precedence`.
  - tooltip이 현재 기준과 다음 기준을 함께 안내한다 ("이름순 정렬 (누르면
    상태순)" 형식). 기본값(이름순)이 아니면 `button_config.active`로 강조
    배경을 그려 경로 표시 토글과 같은 방식으로 상태를 보여 준다.
  - 다음 기준 계산(`next_sort_key`)과 tooltip 문구는 `toolbar_element.cpp`의
    익명 namespace 순수 함수다.
  - 배치는 경로 표시 토글과 탐색 버튼 사이이며, 기존 규칙대로 창이 좁으면
    자리가 없는 버튼부터 숨는다.
- custom(문서 순서)은 카드 drag & drop이 자동 전환하는 값과 같은 상태라 별도
  이력이 없어도 선택 가능하다 (문서의 프로젝트 순서 그대로 표시).
- 정렬 기준은 기존과 같이 세션 상태다. 문서(`workspace_settings`) 영속은 이번
  범위에 넣지 않았다 (필요 시 별도 지시).

## 3. 테스트

새 test 2개 (TEST_CASE 기준).

- tree 빌드: `toolbar_sort` 존재·표시·활성과 세 기준 각각의 tooltip 문구.
- interaction: 클릭이 `set_sort_intent`를 만들고 key가 이름 → 상태 → 문서 순서
  → 이름으로 순환.
- logic 쪽 `set_sort_intent` 처리·정렬 비교자는 기존 test가 이미 덮는다
  (`logic_controller_tests.cpp`).

## 4. 검증 결과

| 항목 | 결과 |
| --- | --- |
| `vs2022-tests` Debug 빌드 | 통과 |
| Catch2 전체 (`gitman_tests.exe`) | **605 case, 11,575 assertion 통과** (의도된 skip 3) |
| 전체 CTest (`vs2022-tests-debug`) | **628/628 통과** (의도된 skip 3: SVN 1, network 2) |
| clang-format (`gitman_format`) | 적용 후 무위반 |
| source style 검사 | 통과 (372 파일) |

## 5. 비고

- 단계 8 후속 항목 1이 이 체크포인트로 해소됐다.
- 작업 중 발견: `set_filter_intent`도 같은 상태(UI 발신자 없음)다. 필터 입력
  UI는 후속 항목 목록에 없어 이번 범위에서 제외했다.
