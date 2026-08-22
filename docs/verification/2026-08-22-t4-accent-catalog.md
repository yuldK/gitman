# T4 — 키 컬러 4역할과 빌드 시점 색 목록

날짜: 2026-08-22 · 설계: `docs/theme-and-banner-menu-design.md` T4 (체크포인트 C3)

## 1. 변경 요약

키 컬러 하나(민트)를 네 역할(`accent`·`accent_hover`·`accent_soft`·
`accent_emphasis_foreground`)로 나누고, 고를 수 있는 색 목록을
`assets/accents.json`에서 빌드 시점에 C++ 표로 만들어 내장했다. 이 체크포인트는
색을 **고르는 UI 없이** 표·팔레트 합성·역할 배정까지다(기본값 mint 그대로).

## 2. 구현 경계

- `assets/accents.json` — 민트·블루·퍼플·앰버·로즈 5색. 스키마는 지시받은 그대로
  (`id`·`label`·`swatch`·`dark`/`light`의 `accent`·`accentHover`·`accentSoft`·
  `accentEmphasisFg`)다. 블루는 지시에 실린 값을 그대로 썼다.
- `cmake/generate_accents.cmake` — JSON을 읽어
  `generated/include/gitman/generated/accents.h`(의존성 없는 값 표)를 만든다.
  Codicons 매핑과 같은 custom command 구조다. **검증은 생성 시점에** 한다:
  `#rrggbb`가 아닌 색, 빈·중복·대소문자 섞인 `id`, 빈 `label`, 4역할 누락,
  `mint` 없음은 `FATAL_ERROR`로 빌드를 세운다. 런타임 JSON 파싱은 없다.
- `ui_theme` — `accent_color_set`/`accent_definition`, `accent_catalog()`,
  `accent_for(id)`(모르는 id는 mint로 물러섬), `accent_exists(id)`를 더했다.
  팔레트는 중립 색 위에 키 컬러를 얹어 **값으로** 돌려준다
  (`color_palette_for(theme, accent)`). 고대비는 키 컬러를 무시하고 네 역할이
  모두 흰색이다.
- 역할 배정(설계 T4.2 그대로):

| 역할 | 적용한 곳 |
|---|---|
| `accent` | 카드 상태 아이콘·선택 테두리·drag 강조·SVN 현재 위치 표시·체크박스 테두리·입력 칸 초점 테두리·토글 트랙(켬) |
| `accent_hover` | 토글 트랙 hover, 강조 버튼 hover 바탕 |
| `accent_soft` | 낮은 알파로 겹치는 옅은 바탕 — 선택 행, `덮어씀` 배지, 강조 버튼 바탕, 켜진 도구 막대 토글 |
| `accent_emphasis_foreground` | 바탕 위 강조 글자 — 설정 행 제목, 선택 행 글자, 강조 버튼 라벨, 알림 제목, 켜진 토글 아이콘 |

- `version_list_generation_dialog`은 상수 팔레트를 가리키던 포인터를 값 멤버로
  바꿨다(키 컬러가 섞여 상수 하나를 가리킬 수 없다). 색 선택은 아직 dark 고정이며
  테마 배선은 C4·C5에서 한다.

## 3. 검증

빌드: `cmake --build --preset vs2026-tests-debug` (경고 없음). 생성 header를
확인해 5색 20개 역할 값이 `0xAARRGGBB`로 들어간 것을 확인했다.

| 테스트 | 결과 |
|---|---|
| 내장 표가 온전하다 (id 유일·빈 값 없음·모든 색 불투명·accent≠hover) | 통과 |
| `mint`·`blue`가 있고 없는 id는 없다고 답한다 | 통과 |
| 모르는 id와 빈 id는 기본색(mint)으로 물러선다 | 통과 |
| 팔레트가 고른 키 컬러를 얹고 중립 색은 그대로다 | 통과 |
| 고대비는 키 컬러를 무시한다(네 역할 모두 전경색) | 통과 |
| `[theme]` 4 case (90 assertions) | 통과 |
| 영향 범위 재실행 `[ui],[logic],[theme],[display],[start-page],[context-menu]` 168 case (4,483 assertions) | 통과 |

## 4. 남은 수동 검수

- 기본색(mint)에서 화면이 이전과 크게 다르지 않은지 — 다만 강조 글자·옅은
  바탕은 역할 값이 달라져 조금 밝아진다(의도).
- 색을 실제로 고르는 UI는 C5에서 붙인다.
