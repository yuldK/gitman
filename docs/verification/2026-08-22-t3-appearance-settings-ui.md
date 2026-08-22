# T3 (2/2) — 환경설정의 테마 토글과 키 컬러 스와치

날짜: 2026-08-22 · 설계: `docs/theme-and-banner-menu-design.md` T3.3 (체크포인트 C5)

## 1. 변경 요약

환경설정 dialog 맨 아래에 외양 2행(테마·키 컬러)을 붙였다. 테마는 codicon 3종의
세그먼트 토글이고, 키 컬러는 내장 목록의 색 동그라미다. 둘 다 **클릭 즉시**
반영·저장된다. `.version-list` 생성 dialog도 같은 팔레트를 쓴다.

## 2. 구현 경계

- panel 높이 440 → 544, 행 6·7 추가. 두 행은 문서 모드에서도 **앱 설정을 그대로
  편집**하며 `덮어씀` 배지가 없다(제목에 `(전역)` 표기). 문서를 연 채로 여는
  경우가 대부분이라 문서 모드에서 숨기면 사실상 접근할 수 없다.
- 테마 세그먼트: `라이트`(`lightbulb`) · `시스템`(`device-desktop`) ·
  `다크`(`color-mode`) — codicon에 sun/moon이 없어 고른 3종이다(검수 결정 4).
  고른 칸만 `accent_soft` 바탕 + `accent_emphasis_foreground` 아이콘이다. 값 줄이
  지금 무엇을 따르는지 한 줄로 알린다.
- 키 컬러: `accent_catalog()`의 색 동그라미를 나란히 놓고 고른 색에 링을 두른다.
  tooltip이 색 이름이다. 목록은 표시 계층이 직접 알기 때문에 view 모델에는 현재
  값(`accent_id`)만 싣는다.
- 두 컨트롤은 `저장`/`취소` 초안에 들어가지 않는다 — 클릭이 곧바로
  `set_theme_preference_intent`·`set_accent_intent`를 내고 logic이 앱 설정을
  갱신·저장한다. **취소로 닫아도 되돌아가지 않는다**(색은 눌러 보고 고르는
  항목이라 미리 보기가 곧 값이어야 한다).
- 새 컨트롤은 index가 아니라 포인터 목록으로 배치해 기존 자식들의 index 기반
  배치를 건드리지 않는다.
- `show_version_list_generation_dialog`가 테마와 키 컬러를 받는다. 라이트에서는
  `DWMWA_USE_IMMERSIVE_DARK_MODE`를 끄고 시스템 caption도 밝게 칠한다. UI thread는
  `current_color_theme()`/`current_appearance()`로 본 창과 같은 판정을 쓴다.

## 3. 검증

빌드: `cmake --build --preset vs2026-tests-debug` (경고 없음)

| 테스트 | 결과 |
|---|---|
| 문서 모드에서도 외양 행이 앱 설정 값을 보여 준다 (theme·accent_id) | 통과 |
| 테마 3칸과 색 동그라미(내장 목록 수만큼)가 각자 id로 존재한다 | 통과 |
| 세 칸이 panel 안에서 라이트→시스템→다크 순으로 놓인다 | 통과 |
| 테마 칸 클릭이 `set_theme_preference_intent`를 낸다 | 통과 |
| 색 동그라미 클릭이 `set_accent_intent { blue }`를 낸다 | 통과 |
| 취소로 닫아도 적용된 테마가 유지된다 | 통과 |
| `[settings-ui]` 25 case (198 assertions) | 통과 |
| 영향 범위 재실행 `[ui],[logic],[theme],[app-settings],[display],[context-menu],[start-page]` 185 case (4,618 assertions) | 통과 |
| 앱 실행 `gitman.exe --smoke-test` | 통과 (exit 0) |

## 4. 남은 수동 검수

- 환경설정에서 라이트/시스템/다크를 눌러 화면이 즉시 바뀌는지, 색 동그라미로
  키 컬러가 즉시 바뀌는지, 앱을 껐다 켜도 유지되는지.
- 시스템을 고른 상태에서 Windows 앱 모드를 바꾸면 즉시 따라오는지.
- 라이트 테마에서 카드 목록·로그 pane·switch dialog·로컬 변경 dialog·생성
  dialog의 대비가 읽을 만한지(색 값 조정이 필요하면 별도 지시로 반영).
