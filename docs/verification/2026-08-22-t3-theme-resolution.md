# T3 (1/2) — 라이트 팔레트·테마 해석·외양 설정 저장

날짜: 2026-08-22 · 설계: `docs/theme-and-banner-menu-design.md` T3 (체크포인트 C4)

## 1. 변경 요약

라이트 팔레트를 추가하고, 앱 설정에 `appearance`(테마 선호 + 키 컬러 id)를 두어
UI thread가 고대비·OS 설정과 함께 해석하도록 배선했다. 이 체크포인트는 **설정을
고르는 UI 없이** 값 · 저장 · 해석 · 팔레트까지다(환경설정 행은 C5).

## 2. 구현 경계

- `domain/appearance.h` — `theme_preference { system, light, dark }`와
  `appearance_settings { theme, accent_id }`. 문서가 덮어쓰지 않는 앱 단위 값이라
  `workspace_settings`가 아니라 `app_settings` 직속이다. JSON 이름 변환
  (`theme_preference_name`·`parse_theme_preference`)도 도메인에 둔다.
- `gitman.app-settings.json`에 `"appearance": { "theme": ..., "accent": ... }`가
  생긴다. 저장은 두 키를 항상 기록하고(파일만 봐도 무엇을 고를 수 있는지 보인다),
  읽기는 표시 설정답게 **어떤 오류도 시작을 막지 않는다**: 모르는 테마 이름·
  object 아님·문자열 아님은 경고 하나를 남기고 기본값을 쓴다.
- **목록에 없는 키 컬러 id는 그대로 나른다.** 물러서는 판정(`accent_for`)은 표시
  계층의 몫이라, 색 목록이 바뀌어도 저장된 선택이 살아남는다.
- `view_snapshot.appearance`로 값을 UI thread에 싣는다. logic은 팔레트를 만들지
  않는다.
- `resolve_color_theme(preference, high_contrast, system_prefers_light)` — 순수
  함수다. 고대비가 가장 세고, `system`은 OS의 앱 모드를 따른다. UI thread는
  `HKCU\...\Themes\Personalize\AppsUseLightTheme`를 **캐시**해 쓰고
  `WM_SETTINGCHANGE`·`WM_THEMECHANGED`에서만 다시 읽는다(두 메시지는 이미 다시
  그리기를 일으킨다). 값을 읽지 못하면 어두운 쪽으로 본다.
- `color_theme::light`와 라이트 중립 팔레트(VSCode Light Modern 기준)를 더했다.
  키 컬러는 테마별 정의(`accent_definition::for_theme`)를 쓴다.
- 외양 intent 2종(`set_theme_preference_intent`·`set_accent_intent`)은 저장·취소
  초안을 거치지 않고 곧바로 앱 설정을 갱신하고 저장을 요청한다. 조회 설정이
  아니므로 카드 재조회는 일어나지 않는다.

## 3. 검증

빌드: `cmake --build --preset vs2026-tests-debug` (경고 없음)

| 테스트 | 결과 |
|---|---|
| `appearance` 왕복(light + blue), 기본값도 파일에 기록 | 통과 |
| 모르는 테마 이름·object 아님은 경고 1건 + 기본값 | 통과 |
| 목록에 없는 키 컬러 id는 경고 없이 그대로 보존 | 통과 |
| 테마·키 컬러 intent가 앱 설정을 갱신하고 저장을 요청하며 snapshot에 즉시 반영 | 통과 |
| 같은 값 재전송·빈 id는 저장을 늘리지 않는다 | 통과 |
| `resolve_color_theme` 6종(고대비 우선·명시 선호·system 양방향) | 통과 |
| 라이트 팔레트가 중립 색을 뒤집고 라이트 키 컬러를 쓴다 | 통과 |
| `theme_preference` 이름 왕복과 모르는 이름 거부 | 통과 |
| `[theme],[app-settings]` 25 case (220 assertions) | 통과 |

## 4. 남은 수동 검수

- 아직 테마를 고르는 UI가 없다. `gitman.app-settings.json`의
  `appearance.theme`을 `"light"`로 바꿔 앱을 켜면 밝은 화면이 나오는지,
  `"system"`으로 두고 Windows 설정에서 앱 모드를 바꾸면 즉시 따라오는지 확인.
- 생성 dialog는 아직 dark 고정이다 (C5에서 배선).
