# G5 — 확인되지 않은 상태(`?`)를 비활성 색으로

날짜: 2026-08-22 · 설계: `docs/global-settings-and-ui-fixes-design.md` (G5)

## 1. 변경 요약

조회가 판정되지 않은 카드(`?`, "확인되지 않음")가 카드 상태의 강조색(ready면
키 컬러 초록)으로 그려져 정상처럼 보였다. 이제 비활성 계열(중립 전경색을
흐리게)로 그려 제대로 로드되지 않았다는 것이 색으로 드러난다.

## 2. 구현 경계

- `status_glyph`에 `undetermined` flag를 추가하고, `question`을 돌려주는 두
  자리(`sync_state_glyph`의 unknown fallback, `availability_glyph`의
  ready/unknown fallback)에서 true로 둔다. codicon 이름 문자열 비교 대신 flag를
  쓰므로 글리프가 바뀌어도 표현 규칙이 유지된다.
- `card_element`가 flag를 소비한다:
  - 카드 왼쪽 상태 아이콘: 강조색 대신 `primary_foreground` 45%. 진행 중
    (busy, sync 아이콘)일 때는 해당 없음.
  - 세 번째 줄 동기화 상태 chip: 배경 `neutral 10%`, 글자 `neutral 50%`
    (리비전 chip과 같은 흐린 계열).
- logic의 카드 view 구성(`model.status = ..._glyph(...)`)은 구조체 복사라
  변경 없이 flag가 전파된다.

## 3. 검증

| 테스트 | 결과 |
|---|---|
| `Only the question glyphs are marked undetermined` (신규) | 통과 |
| 기존 기능 접촉분: `[presentation][app]` 태그 6 case | 통과 (44 assertions) |

빌드: `vs2022-tests` Debug에서 `gitman_tests`·`gitman`(앱) 모두 성공.

## 4. 남은 수동 검수

- 원격 조회 전(또는 조회 불가) 카드의 `?` 아이콘과 "확인되지 않음" chip이
  흐린 중립색으로 보이는지, 새로 고침으로 판정되면 강조색으로 돌아오는지 확인.
