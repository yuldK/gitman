# 실환경 피드백 F1b — 드래그 UX: 떠 있는 카드와 벌어지는 삽입 여백

날짜: 2026-08-20 · 설계: `docs/field-feedback-design.md` 4.1

## 1. 변경 요약

작은 label ghost + 대상 카드 테두리 강조 방식의 드래그를 다음으로 바꿨다.

- **떠 있는 카드**: drag가 시작되면 카드 전체가 잡은 지점 그대로 포인터를
  따라오고(`drag_payload::grab_offset_*` 추가), 원래 slot에서는 그리지 않아
  목록에서 빠진 것으로 보인다. 목록 clip 밖(로그 pane·toolbar 위)까지 따라간다.
- **벌어지는 여백**: 포인터가 가리키는 삽입 위치의 여백이 카드 한 장 크기로
  벌어지고, 나머지 카드는 세로 offset만 이동해 그린다. 포인터가 목록 밖이면
  여백은 원래 자리에 남는다.
- **불변 tree 유지 (ADR-004)**: 배치(bounds)·hit test는 그대로 두고 draw가
  `interaction_snapshot`의 drag 상태로 offset만 계산한다. 여백 위치와 drop
  위치는 같은 순수 함수를 쓴다:
  `card_drag_insertion_slot`/`card_drag_offset` (`list_metrics.{h,cpp}`).
- **drop 대상을 목록 수준으로**: 카드별 drop 대상(위/아래 절반)을 제거하고
  `card_list_element`가 유일한 drop 대상이 됐다. 삽입 경계가 각 카드의 세로
  중앙이라 체감 규칙은 기존과 같고, 카드 사이 여백에 놓아도 동작한다. 제자리
  drop은 intent를 만들지 않는다. 목록이 hit 대상이 되면서 빈 영역 클릭의 선택
  해제를 목록에도 등록했다(기존 root 동작 유지).
- drag 중 휠 스크롤로 카드 element가 tree에서 빠진 경우만 기존 소형 ghost로
  대체한다. `ui_tree::draw_drag_visual`은 카드 drag를 건너뛴다.

## 2. 테스트

- 갱신: drag 상호작용 테스트를 목록 수준 drop으로 재작성 — 아래 절반(뒤 삽입),
  위 절반(앞 삽입 = 이전 카드 뒤), **카드 사이 여백 drop**, 제자리 drop 무시,
  잡은 지점 offset 적재. tree 빌드 테스트는 카드 drop 제거·목록 drop 존재와
  목록 빈 영역 hit/선택 해제를 단정.
- 추가: `card_drag_insertion_slot`/`card_drag_offset` 순수 함수 테스트 1 case —
  중앙 경계, 여백 위치, 목록 밖 고정, 제자리, 빠진 자리 닫힘/여백 벌어짐 offset.
- 실행: `gitman_tests.exe "[logic],[ui],[runtime]"` — **109 case, 3,952 단정
  전부 통과** (VS2022 Debug). 전체 CTest는 FV에서 실행한다.

## 3. 빌드·스타일

- VS2022 Debug: `gitman_tests`, `gitman` 빌드 성공
- `check_source_style.ps1` 통과 (아래 4장 시점)

## 4. 수동 확인 권장 항목

그리기 결과는 자동 테스트가 픽셀을 검증하지 않으므로 실행 후 눈으로 확인:

1. 카드를 끌면 카드 전체가 잡은 지점 그대로 따라오고 원래 자리가 닫히는지
2. 다른 카드 사이로 가면 그 자리 여백이 한 장 크기로 벌어지는지
3. 목록 밖(로그 pane 위)으로 끌면 여백이 원래 자리로 돌아오는지
4. Esc로 취소하면 원상 복구되는지, 여백 자리에 놓으면 그 자리로 들어가는지

## 5. 비고

- 여백 이동의 easing 애니메이션은 하지 않는다(입력마다 즉시 다시 그리는 렌더
  모델 유지). 드래그 중 목록 가장자리 자동 스크롤은 후속 항목.
