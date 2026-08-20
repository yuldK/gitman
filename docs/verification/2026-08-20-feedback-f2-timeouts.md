# 실환경 피드백 F2 — 제한 시간 정책 개편 (조회 설정화·업데이트 무제한)

날짜: 2026-08-20 (재검수 반영) · 설계: `docs/field-feedback-design.md` 1장

## 1. 변경 요약

대형 저장소에서 status가 5~10분, update가 최대 3시간까지 걸리는 실측을 반영했다.
1차 구현(단계 버튼 UI, 항목 2개, update 600초)에 대한 재검수 지시로 UI와 정책을
고쳤다.

- **기본값** (`vcs_execution_policy.cpp`): `local_query` 30초 → **600초**,
  `remote_query` 120초 → **600초**, **`update` 600초 → 무제한**(가늠해 자르지
  않는다 — 종료는 기존의 명시적 취소가 담당하고, process runner는 timeout 없는
  요청을 INFINITE 대기로 이미 지원한다). `tool_probe` 5초, `switch_target`
  300초 유지.
- **문서 설정** (`settings.query_timeout_seconds`, 10~3600초): 로컬·원격 조회에
  함께 적용되는 항목 **하나**다. 파서는 잘못된 값에 경고만 남기고 기본값을 쓰며,
  기본값 복원 시 필드를 지운다. schema version은 올리지 않는다.
- **전달 경로**: `vcs_timeout_overrides { query }` +
  `vcs_timeouts_from_settings()`. executor가 요청 settings에서 만들어 provider
  생성 시 주입, provider가 조회 builder 15종에 전달. 범위 밖 값은 어떤 경로로
  와도 무시된다.
- **환경설정 UI — 숫자 전용 텍스트 박스** (재검수 지시): "상태 확인 제한 시간
  (초)" 행 하나. 이를 위해 문자 입력 경로를 신설했다:
  `WM_CHAR`(win32_application) → `character_typed_event`(raw_input_event) →
  환경설정 dialog가 열려 있을 때만 `edit_settings_timeout_intent` →
  logic이 숫자(0~9, 최대 4자리)와 backspace만 초안에 반영. 빈 초안 = 기본값
  ("600 (기본값)" 안내), 10~3600 밖 값은 "제한 시간은 10~3600초 사이여야
  합니다." 메시지와 함께 저장 버튼 비활성(실행 파일 검증과 같은 패턴).
  단계(±) 버튼·프리셋은 제거했다. panel은 3행(Git/SVN/제한 시간)이다.
- **초점·caret** (추가 지시): `interaction_snapshot::focused_input`/
  `focus_started_at` 신설. 초점은 텍스트 박스를 **눌러야** 생기고(재검수:
  자동 초점 제거), 다른 곳을 누르거나 dialog가 닫히면 해제. 초점 중에는 글
  끝에서 caret이 **깜빡이고**(반주기 530ms, 초점 순간 켜짐) 테두리를 강조한다.
  깜빡임 재그리기는 UI thread의 caret timer(tooltip timer 패턴)가 다음 뒤집힘
  시점에 일으킨다. 문자 입력은 초점 칸으로만 라우팅된다.
- **한 박자 늦던 갱신 수정** (추가 지시): 원인은 `publish_snapshots`의 게시
  순서 race — view(wake 신호 부착)를 먼저 게시하고 tree를 나중에 게시하는데
  tree slot에는 신호가 없어, wake를 받은 UI thread가 이전 tree로 그리면 새
  tree는 다음 이벤트까지 화면에 반영되지 않았다. tree → view 순서로 바꿔 wake
  시점에 항상 최신 tree가 있게 했다 (`win32_app_runtime.cpp`). 키 입력뿐 아니라
  모든 상태 갱신의 지연 가능성을 없애는 수정이다.

## 2. 테스트 (이 단계 추가·갱신분만 실행)

- 추가·재작성 7 case: 문자 입력 편집(숫자만·backspace·4자리 상한), 범위 밖
  차단·메시지, 확인 저장 + refresh 요청 탑재 + 재열기 초안 복원, dialog 열림
  여부에 따른 문자 라우팅과 텍스트 박스 존재, policy override(단일 query·update
  무제한·범위 밖 무시), schema 파싱(정상/경고), store 왕복(쓰기·필드 제거).
- 갱신: 부류 기본값 단정(조회 600초, update 무제한), update 명령 3종(git pull,
  submodule update, svn update)의 무제한 단정.
- 추가(초점·caret): 초점 없이는 문자 차단(자동 초점 없음), 박스 클릭 시 초점과
  caret 위상 시각 기록, 패널 클릭 해제, 닫힘 시 해제 — 2 case.
- 실행 범위: `[settings-ui],[policy],[schema],[store],[command],[interaction]`
  — 통과 (VS2022 Debug). raw_input_event variant 확장·초점 추가가 기존
  상호작용을 건드려 `[interaction]`·builder `[command]`를 제한적으로 포함.
  전체 suite는 큰 단계 종료 시 별도 지시로 실행한다.

## 3. 빌드·스타일

- VS2022 Debug: `gitman_tests`, `gitman` 빌드 성공
- `check_source_style.ps1` 통과 (커밋 전 재확인)

## 4. 수동 확인 권장 항목

1. 환경설정 dialog 배치가 정상인지 (3행 + 텍스트 박스, 이전 검수에서 깨짐 보고)
2. 텍스트 박스에 숫자 입력·backspace·범위 밖 값의 메시지가 동작하는지 —
   특히 키 입력이 **즉시** 화면에 반영되는지(한 박자 지연 수정 확인), 초점이
   클릭으로만 생기는지(열릴 때 자동 초점 없음), caret이 약 0.5초 간격으로
   깜빡이는지
3. 실측 대형 저장소에서 상태 확인 완주(기본 600초), 낮춘 값(예: 30초)에서
   시간 초과 실패 메시지 확인
4. 3시간급 update가 중간에 끊기지 않는지, 실행 중 취소 버튼으로 즉시 중단되는지
5. `.version-list`의 `query_timeout_seconds`가 재시작 후 유지·빈 값 저장 시
   제거되는지
