# 실환경 피드백 F1 — 정렬 제거 (문서 순서 고정)

날짜: 2026-08-20 · 설계: `docs/field-feedback-design.md` 4장

## 1. 변경 요약

정렬 기능(이름순/상태순)과 toolbar 정렬 순환 버튼(커밋 `0fe4e69`)을 제거했다.
카드는 항상 문서의 `projects` 배열 순서로 표시되고, drag & drop 순서 변경 →
문서 갱신 → 저장 경로는 기존 그대로가 최종 동작이 된다.

- `card_sort_key` enum과 `view_snapshot::sort` 필드 제거 (`view_snapshot.h`)
- `set_sort_intent` struct와 `logic_message` variant 항목 제거 (`app_messages.h`)
- logic: `sort_` 멤버, intent 처리, `build_ordered_cards`의 정렬 분기와
  비교자(`name_before`/`status_before`/`state_rank`) 제거. `handle_reorder_card`의
  custom 전환 코드를 걷어내 위치가 그대로면 아무것도 하지 않는다
  (`logic_controller.{h,cpp}`)
- UI: toolbar 정렬 버튼과 `next_sort_key`/`sort_tooltip`, `toolbar_element`
  생성자의 `sort` 매개변수, `ui_element_kind::toolbar_sort` 제거
  (`toolbar_element.{h,cpp}`, `build_ui_tree.cpp`, `ui_element.h`)
- README 알려진 제한: 정렬 전환 UI 문구를 제거하고 "문서 순서 고정 + 드래그
  변경" 안내로 교체 (stale이던 줄 정리 포함)

## 2. 테스트

- 갱신: 이름순 기본 정렬을 단정하던 logic 테스트를 문서 순서 단정으로 재작성
  ("Cards keep the document order ..."), reorder 3건에서 `sort == custom` 단정
  제거, runtime 통합 테스트의 custom 단정 제거
- 삭제: 정렬 버튼 tooltip 테스트(ui_tree_build), 순환 클릭 테스트(ui_interaction)
  — 대상 코드가 사라짐
- 실행: `gitman_tests.exe "[logic],[ui],[runtime]"` — **108 case, 3,917 단정
  전부 통과** (VS2022 Debug). 전체 CTest는 FV(최종 검증)에서 실행한다.

## 3. 빌드·스타일

- VS2022 Debug: `gitman_tests`, `gitman` 빌드 성공
- `check_source_style.ps1`: 374 파일 통과 (신규 설계 문서 CRLF 정규화 1건 포함)

## 4. 비고

- 후속 항목 1(정렬 전환 UI, `0fe4e69`)은 구현 직후 실사용 피드백으로 제거됐다.
  기록은 `docs/verification/2026-08-20-followup-01-sort-ui.md`와 change_log에
  남아 있다.
- `codicons::icon_sort_precedence`는 생성 헤더라 그대로 둔다(사용처 없음).
