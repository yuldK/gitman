# 단계 8 구현 계획 - 안정화와 배포

## 1. 문서 상태

- 작성일: 2026-08-19
- 대상: 구현 단계 8 (최종 단계)
- 현재 상태: `S8-P0` 수립. 2026-08-19 사용자 검수 의견(진행 방식)을 반영,
  계획 승인 대기
- 관련 요구사항: REQ-004, REQ-016, REQ-017과 안정화 대상 전반 (REQ-001~REQ-015)
- 상위 문서: `docs/plan.md` 8장 단계 8, `docs/handoff.md` 7장,
  `docs/verification/2026-08-19-stage-7.md` 6장,
  `docs/decisions/ADR-003-vcs-runtime-policy.md`
- 선행 조건: 단계 7 완료 보고 (2026-08-19 제출). 사용자가 단계 8 개시를
  지시했다
- 시작 기준선: 전체 CTest **585/585** 통과 (2026-08-19 `S7-V1` 실측)

진행 방식: production code와 test code는 단계 5~7처럼 한 검수 구간에서 함께
작성한다. 다만 2026-08-19 사용자 지시로 단계 7의 자동 진행 위임을 종료하고,
**매 체크포인트 종료 시 검증 문서와 제안 커밋 메시지를 첨부해 검수를 요청하며,
사용자 승인과 커밋 후에만 다음 구간을 진행한다.**

## 2. 사용자 결정 사항 (2026-08-19)

계획 수립 전 브리핑에서 다음 세 가지를 결정받았다.

1. **SVN CLI는 계속 설치하지 않는다.** 실제 `svn.exe` 실측은 수행하지 않고
   fixture·fake runner 검증까지를 보증 범위로 하며, 미검증 사실과 실제 환경
   적용 시 확인 목록(`docs/handoff.md` 8.6의 네 가지)을 알려진 제한으로
   문서화한다.
2. **네트워크 원격·인증 실측은 GitHub 공개 저장소를 사용한다.** 자격 증명은
   사용하지 않으며, 존재하지 않는 비공개 URL로 인증 실패 즉시 종료를 실측한다.
3. **단계 7 이월 UI 세 항목을 단계 8에 포함한다.** 환경설정 화면, 탐색 후보
   선택 등록 dialog, 로그 pane 개선(시각적 스크롤 막대, progress record 접기).

## 3. 목표

단계 8은 남은 UI 이월 항목을 마무리하고, `.version-list` file association을
구현하며, 안정화 시험과 실측 검증·배포 문서화로 최초 마일스톤(plan 12장)을
닫는다.

- 환경설정 화면: Git/SVN 실행 파일 경로를 문서 `settings`에서 편집한다
  (REQ-017).
- 탐색 후보 선택 등록 dialog: 깊이 1 탐색 결과를 미리 보고 선택 항목만
  등록한다 (REQ-004).
- 로그 pane 개선: 시각적 스크롤 막대와 progress record 접기.
- Windows file association: `.version-list` 등록·제거와 double-click 실행 검증
  (REQ-016).
- 안정화 시험: 장시간 실행, 종료 중 작업, 네트워크 단절, 대량 로그, 문서 동시
  수정 충돌.
- 실측 검증: GitHub 공개 원격의 refresh/update/switch와 인증 실패 즉시 종료.
- 배포 문서화와 절차 검증: 라이선스 고지, install, 설정 위치, 업그레이드,
  백업·복구.

## 4. 단계 8에서 하지 않는 일

- 실제 `svn.exe` 실측 (사용자 결정 1. 알려진 제한으로 문서화)
- 네트워크 드라이브·실제 느린 경로의 탐색 검증 (이 호스트에 재현 수단이 없다.
  알려진 제한으로 문서화)
- 파일 로그 (메모리 ring buffer 유지. plan 3.9의 보존 기간·저장 위치는 필요할
  때 정한다)
- 자격 증명 저장·대화형 인증 (ADR-003 유지)
- MSIX·installer 패키징 (단일 exe 복사 배포가 확정 방식이다)
- 정렬 전환 UI와 스크롤 막대 Page Up/Down (기존 후속 항목 유지)

## 5. 설계 제안 사항

검수에서 확정할 제안이다.

### 5.1 환경설정 화면 (REQ-017)

- 상단 도구 모음에 환경설정 버튼(codicon `settings-gear`)을 추가하고, in-app
  overlay dialog로 연다 (update overlay·switch dialog와 같은 방식. logic이
  상태를 소유하고 view snapshot에 담는다).
- 항목은 `git_executable`, `svn_executable` 두 경로다. 각 항목은 텍스트
  표시란과 `찾아보기`(Win32 파일 선택, platform adapter 경유), `지우기` 버튼을
  갖는다. 자유 텍스트 입력은 단계 8 범위에서 제공하지 않는다 — 파일 선택
  대화상자가 존재하는 경로만 만들고, 빈 값은 자동 탐색을 뜻하므로 오타 검증
  UI를 따로 만들 필요가 없어진다.
- 저장 시 단계 2 store의 원자적 저장을 사용하고, 동시 수정 감지 충돌은 기존
  계약대로 오류를 표시한다. 저장 후 도구 재발견과 전체 refresh를 자동 실행한다.
- 검증: 지정 경로가 존재하지 않으면 저장을 막고 한국어 오류를 표시한다.

### 5.2 탐색 후보 선택 등록 dialog (REQ-004)

- 카드 도구 버튼 또는 상단 도구 모음에서 등록 경로를 대상으로 탐색을 실행하고,
  결과를 overlay dialog에 표시한다: 후보 경로, 저장소 종류, 제외 사유(이미
  등록됨·저장소 아님·접근 실패), 항목별 체크박스.
- 기본 체크 상태는 등록 가능 후보 전체 on이다. 제외 사유가 있는 항목은
  비활성으로 표시만 한다.
- `등록` 확정 시에만 단계 5의 선택 등록 API로 문서를 원자적으로 갱신하고, 저장
  충돌 시 오류를 표시하며 dialog를 유지한다 (재시도 가능).
- 탐색은 worker 작업으로 실행하고 진행 중 취소를 제공한다 (단계 5 API의 자식
  경계 취소).
- 기존 `.version-list` 생성 기능의 전량 등록 동작은 유지한다. 이 dialog는 이미
  열린 문서에 추가 등록하는 경로다.

### 5.3 로그 pane 개선

- 시각적 스크롤 막대: 카드 목록 스크롤 막대와 같은 구현을 로그 pane 본문에
  적용한다. drag 동작도 동일하게 제공한다.
- progress record 접기: 같은 작업의 연속된 `progress` record는 마지막 것만
  표시하고 "진행 표시 N줄 접힘" 표식을 단다. ring buffer에는 그대로 남기고
  (복사·진단 보존) 표시 단계에서만 접는다. 필터와 조합될 때도 접힘 계산은 표시
  목록 기준이다.

### 5.4 Windows file association (REQ-016)

- **HKCU 범위만 사용한다** (`HKCU\Software\Classes`). 관리자 권한이 필요 없고
  제거가 사용자 단위로 끝난다. HKLM·admin 등록은 제공하지 않는다.
- ProgID는 `Gitman.VersionList`로 하고 `.version-list` 확장자, 표시 이름,
  `DefaultIcon`(exe 자신), `shell\open\command`(`"<exe 경로>" "%1"`)를
  등록한다. 등록 후 `SHChangeNotify(SHCNE_ASSOCCHANGED, ...)`를 호출한다.
- 진입점은 명령행 옵션 `--register-file-association`과
  `--unregister-file-association`이다. 창을 띄우지 않고 등록·제거만 수행한 뒤
  종료 코드로 성공 여부를 알린다. 앱 내 UI 진입점은 환경설정 dialog의 버튼
  두 개로 같은 코드를 호출한다.
- 등록은 멱등이다. 기존 값이 있으면 갱신하고, 제거는 이 앱이 만든 ProgID와
  확장자 연결만 지우며 다른 앱의 연결은 건드리지 않는다.
- 레지스트리 접근은 platform adapter(`platform/win32`)에 격리하고, 등록 값
  생성·판정 로직은 순수 함수로 분리해 단위 test한다. 실제 레지스트리 test는
  임시 하위 키(`HKCU\Software\GitmanTest\...`)로 수행해 실 연결을 오염시키지
  않는다.

### 5.5 안정화 시험

자동 test로 재현 가능한 것은 test로, 창·시간이 필요한 것은 절차 문서와 수동
검증 기록으로 남긴다.

- 장시간 실행: 설치본을 2시간 이상 실행하며 주기적 refresh를 반복하고 Process
  Explorer 계열 지표(Private Bytes, Handle 수)의 단조 증가가 없음을 기록한다.
- 종료 중 작업: update 실행 중 창 닫기 → 취소 신호와 job object 정리로 자식
  프로세스가 남지 않음을 실측한다 (단계 3 계약의 실측 확인).
- 네트워크 단절: GitHub 원격 카드에서 도달 불가 상태(어댑터 차단 또는 hosts
  차단)로 refresh → `offline` 판정과 무한 대기 없음을 실측한다.
- 대량 로그: 출력 수만 줄 명령으로 ring buffer 상한·표시·복사가 유지되는 것을
  자동 test와 수동으로 확인한다.
- 문서 동시 수정: 앱이 연 문서를 외부에서 수정 후 앱에서 저장 → 충돌 감지
  오류 표시를 실측한다 (단계 2 계약의 UI 경로 확인).

### 5.6 실측 검증 (GitHub)

- 공개 저장소를 clone한 카드로 refresh(remote-first ahead/behind), update
  (fast-forward), switch(원격 브랜치 후보)를 실측한다.
- 존재하지 않는 비공개 URL 원격으로 refresh → 인증 프롬프트 없이
  `authentication_required` 즉시 실패를 실측한다 (자격 증명 미사용).
- 결과는 검증 문서에 원격 URL, Git 버전, 소요 시간과 함께 기록한다.

### 5.7 배포 문서화와 절차 검증

- `docs/deployment.md`를 새로 작성한다: 지원 환경, prerequisite (Git/SVN CLI),
  install 절차와 산출물, `.version-list` 문서·창 배치 등 사용자 데이터 위치,
  file association 등록·제거, 업그레이드(단일 exe 교체) 절차, 백업·복구
  (문서 `.bak` 정책), 알려진 제한 (SVN 미실측, 네트워크 드라이브).
- 라이선스: embedded 제3자 고지(Codicons, Skia, nlohmann/json 등)가 앱에서
  열람 가능함을 확인하고 submodule 전환 이후의 의존성 목록과 대조한다.
- 절차 검증: 깨끗한 사용자 프로필에서 exe 복사 → association 등록 →
  double-click 실행 → 제거 → 잔여물 없음을 실측한다. 구버전 문서 fixture로
  업그레이드 호환(스키마 버전 1 유지)을 확인한다.

## 6. 체크포인트

| 구간 | 내용 | 의존 |
| --- | --- | --- |
| `S8-D1` | 환경설정 화면: settings dialog, 파일 선택 adapter 연결, 저장·재발견·refresh 연쇄 | 없음 |
| `S8-D2` | 탐색 후보 선택 등록 dialog: 탐색 작업 배관, 후보 표시·선택, 등록과 충돌 표시 | 없음 |
| `S8-D3` | 로그 pane 개선: 시각적 스크롤 막대, progress record 접기 | 없음 |
| `S8-D4` | file association: 레지스트리 adapter, 명령행 옵션, 환경설정 dialog 버튼, 등록·제거·멱등 test | D1 |
| `S8-D5` | 안정화·실측: 자동화 가능한 시험 test 추가, GitHub 실측, 수동 시험 수행과 기록 | D1~D4 |
| `S8-V1` | 최종 검증: 전체 matrix, `docs/deployment.md`, 배포 절차 검증, 알려진 제한 정리, 최초 마일스톤(plan 12장) 대조 | 전부 |

각 구간은 코드와 test를 함께 담고, 검증 문서
(`docs/verification/2026-08-XX-stage-8-d*.md`)와 제안 커밋 메시지를 첨부해
검수를 요청한다. 사용자 승인·커밋 후 다음 구간을 시작한다.

## 7. 테스트 계획

- `S8-D1`: settings dialog 상태 기계(열기·편집·저장·취소·충돌), 존재하지 않는
  경로 거부, 저장 후 도구 재발견·refresh 연쇄, 저장 round-trip (단계 2 fixture
  재사용).
- `S8-D2`: dialog 상태 기계(조회 중·후보·선택·등록·충돌 재시도), 제외 사유
  표시, 선택 항목만 문서에 반영, 미선택 후보 미기록 (REQ-004 수용 기준).
- `S8-D3`: 스크롤 막대 layout·drag 계산, progress 접기 규칙(연속 판정, 필터
  조합, buffer 보존).
- `S8-D4`: 등록 값 생성·판정 순수 함수, 임시 키 등록·제거·멱등, 명령행 옵션
  분기와 종료 코드. 실 연결 등록·double-click은 수동 검증으로 기록.
- `S8-D5`: 대량 로그 자동 test, 종료 중 취소 전파 test. GitHub 실측·장시간·
  단절·동시 수정은 수동 검증 기록.
- `S8-V1`: 전체 CTest matrix (VS2022 Debug/Release, VS2026 Debug), `/analyze`,
  ASan, format/style, 단일 exe install과 설치본 smoke,
  `--repeat until-fail:3`.

## 8. 단계 8 완료 조건

- 환경설정 화면에서 Git/SVN 경로를 지정·해제할 수 있고 값이 문서에 보존된다
  (REQ-017).
- 탐색 결과를 미리 보고 선택한 항목만 중복 없이 등록된다 (REQ-004).
- 로그 pane에 스크롤 막대가 표시되고 progress record가 접힌다.
- `.version-list` association의 등록, double-click 실행, 제거가 검증된다
  (REQ-016).
- 안정화 시험 5종과 GitHub 실측이 수행되고 결과가 기록된다. 인증 필요 시
  프롬프트 없이 즉시 실패한다.
- `docs/deployment.md`와 알려진 제한 (SVN 미실측, 네트워크 드라이브)이
  기록된다.
- 최초 마일스톤 완료 정의 (plan 12장) 각 항목의 충족 근거가 정리된다.
- 전체 build/test/analyze/format/install matrix가 통과한다.
