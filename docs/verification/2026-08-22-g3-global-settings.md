# G3 — 전역 설정과 문서별 덮어쓰기

날짜: 2026-08-22 · 설계: `docs/global-settings-and-ui-fixes-design.md` (G3) ·
브랜치: `feature/global-settings`

## 1. 변경 요약

설정이 두 층이 된다: **앱 전역 설정**(`gitman.app-settings.json`의 `settings`,
모든 필드가 구체 값) + **문서 override**(`.version-list`의 `settings`, 명시적으로
덮어쓴 키만). 문서에서 정의하지 않은 항목은 앱 설정을 따르고, 환경설정은 문서가
없어도 열려 전역 설정을 편집한다.

## 2. 구현 경계

- **도메인**: `workspace_settings_overrides`(전 필드 `std::optional`,
  `empty()`) + `apply_overrides(전역, override) -> 유효 설정` 순수 함수.
  `workspace_document::settings`의 타입이 override로 바뀌고, `app_settings`에
  `workspace_settings settings`가 추가된다. 빈 실행 파일 경로도 유효한
  정의다("자동 탐색을 쓴다") — nullopt만 "따름"이다.
- **문서 직렬화**: 파서는 문서에 있는 키만 override로 읽는다(검증 규칙 동일,
  기존 문서의 키는 그대로 override가 되어 동작이 바뀌지 않는다). 저장은 정의된
  키만 남기고 정의를 거둔 키는 지우며, 알 수 없는 키는 계속 보존한다.
- **앱 설정 직렬화**: `settings` object를 모든 키로 항상 기록한다(파일을 직접
  고치기 쉽다). 읽기는 문서와 같은 검증을 전부 경고로 적용해 시작을 막지 않는다.
- **logic**: `effective_settings()`가 합성을 한곳에서 한다. 작업 요청
  (`make_request`)·update의 submodule 옵션·파일 로그 여부
  (`publish_log_targets`)·상대 경로 표시(`relative_paths`)가 모두 유효 설정을
  쓴다. worker(executor)는 이전과 같이 `request.settings` 사본만 보므로 변경이
  없다. 도구 막대의 경로 표시 토글은 유효 값을 뒤집은 결과를 문서 override로
  정의한다.
- **환경설정 dialog**:
  - 문서 없음 → 전역 모드(제목 `환경설정 (전역)`), 모든 행이 구체 값 편집,
    확인 시 `save_app_settings`.
  - 문서 있음 → 문서 모드(제목 `환경설정 (문서)`), **암묵 덮어쓰기**(2026-08-22
    검수 결정): 값을 건드린 행만 "문서에 정의됨"이 되고, 확인 시 정의된 행만
    문서 JSON에 남는다.
  - 덮어쓴 행에는 라벨 줄 오른쪽에 **`덮어씀` 배지**가 붙는다 (2026-08-22 추가
    지시 — "앱 설정 따름" 문구 표기는 쓰지 않는다). 배지를 클릭하면 그 행의
    문서 override가 삭제되어 앱 설정을 따르고, 초안에는 앱의 값이 다시 보인다
    (`clear_settings_override_intent`, 행은 `settings_override_field`로 구분).
  - `지우기`: 두 모드 모두 빈 값(자동 탐색)이다. 문서 모드에서는 "빈 값으로
    덮어씀"이 된다 — 정의 삭제는 배지의 몫이다. 제한 시간은 빈 칸이면 "따로
    정하지 않음"이다.
  - `show_relative_paths`는 dialog에 행이 없어 두 모드 모두 유지된다(도구 막대
    토글이 문서 override를 관리).
  - 확인 후 **유효 설정이 실제로 바뀐 경우에만** 파일 로그 대상 갱신과 활성
    카드 재조회를 한다.
- **도구 막대**: 환경설정 버튼이 항상 활성이다. tooltip이 모드를 예고한다
  (`환경설정 (문서)`/`환경설정 (전역)`).

## 3. 검증

| 테스트 | 결과 |
|---|---|
| 도메인: override 합성·`empty()` (domain_model_tests 확장) | 통과 |
| 문서 스키마: 키 존재 여부 = 정의 여부 (project_schema_tests 개편) | 통과 |
| 문서 저장: 정의된 키만 왕복 (json_project_store_tests 개편) | 통과 |
| 앱 설정: 전역 settings 왕복·검증 경고 (신규) | 통과 |
| dialog: 전역 모드 열기·저장 (신규), 암묵 덮어쓰기 (신규), `덮어씀` 배지 클릭 = 정의 삭제 (신규 logic·UI 2건), 지우기 = 빈 값 정의 (신규) | 통과 |
| 유효 설정 합성 재조회 요청 검증 (신규 test 내 포함) | 통과 |
| **전체 CTest 대응 실행: 705 case 통과 (12,322 assertions)** — 타입 변경이 전 계층에 닿아 전체를 실행했다 | 통과 |
| 앱 빌드 + `--smoke-test` | 통과 (exit 0) |

## 4. 남은 수동 검수

- 문서 없이 환경설정을 열어 전역 값 편집·저장 후 `gitman.app-settings.json`의
  `settings` 확인.
- 문서를 열고 환경설정에서 일부 행만 건드려 저장 → 문서 JSON에 그 키만
  남는지, 안 건드린 행이 전역 변경을 따라가는지 확인.
- 기존 문서(설정 키가 있는)를 열어 동작이 그대로인지 확인.
