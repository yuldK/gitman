# G1 — 앱 단위 창 배치 저장

날짜: 2026-08-22 · 설계: `docs/global-settings-and-ui-fixes-design.md` (G1)

## 1. 변경 요약

창 배치가 `.version-list` 문서에만 남아 문서 없이(시작 페이지) 시작하면 항상
기본 크기였다. 이제 앱 설정 파일(`gitman.app-settings.json`)에도 마지막 종료
배치가 남고, 적용 우선순위는 **여는 문서의 배치 > 앱 설정 배치 > 기본값**이다
(검수 결정: 문서 동작은 기존 그대로).

```json
"window": { "x": 320, "y": 180, "width": 1280, "height": 720, "maximized": false }
```

## 2. 구현 경계

- **모델**: `app_settings`에 `std::optional<window_placement> window`를 추가했다.
  좌표 규칙은 문서의 `window`와 동일하다 (`rcNormalPosition` 그대로, 최대화
  별도). schema version은 올리지 않는다.
- **직렬화**: 문서 파서와 같은 규칙이다 — 깨진 값(정수 아님·범위 밖·크기 0)은
  경고 1건만 남기고 무시하며, 값이 없으면 필드를 만들지 않고 파일에 이미 있던
  `window`는 지우지 않는다.
- **갱신**: `WM_CLOSE`의 `window_placement_intent`(기존 경로)를 받으면 문서
  유무와 무관하게 앱 설정에 배치를 남기고 dirty로 둔다. 저장은
  `begin_shutdown`이 문서 종료 저장과 같은 구간에서 한 번 내보낸다 — worker
  lane 0이 FIFO라 진행 중이던 일반 저장 뒤에 실행되어 마지막 상태가 남는다.
  앱 설정 읽기가 끝나기 전의 종료(드문 경로)는 파일의 다른 항목을 지울 수 있어
  저장을 포기한다.
- **복원**: 앱 설정을 읽은 뒤 **아직 어떤 배치도 적용되지 않았을 때만**
  (`window_placement_revision_ == 0`) 기존 `window_placement_request`/`revision`
  경로로 1회 적용을 요청한다. 문서가 자기 배치를 이미 적용했으면 문서가
  우선이고, 명령줄 문서와의 도착 순서 경합에서도 최대 한 번의 짧은 재배치만
  생긴다. 최소 크기·모니터 겹침 방어는 UI thread의 기존 코드가 그대로 담당한다.
- 읽기 전에 받은 배치는 파일 값보다 최신이라 로드 결과 설치 후에도 보존한다
  (최근 목록의 pending 병합과 같은 규칙).
- `win32_application`은 변경이 없다 (기존 intent·적용 경로 재사용).

## 3. 검증

| 테스트 | 결과 |
|---|---|
| `The app settings window placement round trips and ignores broken values` (신규) | 통과 |
| `The closing window placement lands in the app settings save at shutdown` (신규) | 통과 |
| `The app settings placement restores the window only when nothing was applied` (신규, 문서 우선 2 section) | 통과 |
| 기존 기능 접촉분: `[app-settings]`·`[close-document]`·`[logic]` 92 case | 통과 (3,762 assertions) |

빌드: `vs2022-tests` Debug에서 `gitman_tests`·`gitman`(앱) 모두 성공.

## 4. 남은 수동 검수

- 문서 없이 앱을 닫았다가 다시 실행하면 마지막 위치·크기(최대화 포함)로 뜨는지.
- 배치가 있는 문서를 열면 그 문서의 배치로 이동하는지 (기존 동작 유지).
- `gitman.app-settings.json`에 `window` 항목이 종료 후 갱신되는지.
