# 후속 — 컨텍스트 메뉴 `VSCode로 열기`

날짜: 2026-08-22 · 지시: 미추적·변경 정리는 앱이 아니라 편집기에서 하는 편이
낫다 (2026-08-22 사용자)

## 1. 변경 요약

카드 컨텍스트 메뉴의 `저장소 열기` 바로 아래에 `VSCode로 열기`를 추가했다.
작업 복사본 폴더를 VSCode workspace로 연다.

## 2. 구현 경계

- `context_menu_entry::open_in_vscode` 신설. 메뉴 순서: 저장소 열기 →
  **VSCode로 열기** → 로컬 변경 확인 → 상태 갱신 → 업데이트 → 전환.
- 클릭은 로컬 변경 dialog의 파일 열기와 같은 UI thread 경로
  (`open_external_request { external_open_target::vscode, 작업 복사본 경로 }`)를
  탄다 — PATH의 `code.cmd`로 실행하며 없으면 조용히 아무 일도 하지 않는다
  (기존 규칙 유지). 아이콘은 `$(vscode)`, 항목은 항상 활성이다.

## 3. 검증

| 테스트 | 결과 |
|---|---|
| 메뉴 순서(저장소 열기 다음이 VSCode)와 항목 수 6 (기존 test 확장) | 통과 |
| `VSCode로 열기` 클릭 → 닫기 + vscode 열기 요청 (신규 section) | 통과 |
| 항목 index 이동에 따른 기존 클릭·키보드 test 갱신 | 통과 |
| 전체: 705 case 통과 (12,329 assertions), 앱 빌드 + `--smoke-test` | 통과 |

## 4. 남은 수동 검수

- 카드 우클릭 → `VSCode로 열기`로 해당 저장소 폴더가 VSCode에서 열리는지 확인
  (PATH에 `code`가 있어야 한다).
