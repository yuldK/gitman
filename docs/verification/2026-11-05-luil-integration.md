# luil 앱 연결 후속 검증 (2026-11-05)

## 1. 범위

사용자가 진행을 지시한 세 항목(상태 낭독, 로그 Scroll, 저장 후 자동 종료)을 구현했다.
luil 핀은 [앞선 갱신](2026-11-05-luil-requests.md)의 `077262e` 그대로다.
기존 미커밋 갱신 위에 더했으며 커밋·push는 하지 않았다.

## 2. 구현

- **상태 알림**: 알림 배너, 카드의 기능 버튼과 기능 `⋯`, 기능 줄의 기능 버튼을 polite live region으로
  지정했다. 실행·중지·실패·비활성 사유가 바뀌면 LiveRegionChanged를 보낸다. 편집기의 거절 사유는
  alert 역할의 기본 assertive 알림이다. 경과 시간은 접근 이름·도움말에 넣지 않고 로그 줄은 live
  region으로 지정하지 않아 출력·시간마다 낭독을 끊지 않는다. 실제 내레이터 청취와는 구분한다.
- **로그 Scroll**: 기존 줄 목록이 Scroll 상태와 백분율 요청을 공개한다. 가로는 흘리지 않으며 짧은
  로그는 세로도 NoScroll, view size 100%다. 기존 RangeValue 막대도 유지한다. `log_scroll_intent::percent`
  자체를 logic으로 보내 최신 내용 길이와 본문 높이에서 계산한다. 출력이 늘어도 `100`은 끝 따라가기,
  `0`은 맨 위, 중간은 그때 길이의 비율이다. NaN·무한대·범위 밖 값은 버린다. 옛 끝 픽셀을 새 막대에
  쓰는 한계는 픽셀 API에 남으므로 흐르는 끝에는 Scroll이나 자동 스크롤 버튼을 쓴다.
- **저장 후 종료**: 저장과 이어 나갈 저장이 모두 성공했고 실행 중인 출력 수집 기능이 없으면 controller가
  종료 요청을 세우고 driver가 `post_ui_command(application_exit)`를 한 번만 낸다. 닫힘 훅의 발행본 뒤에
  작업이 이미 끝난 경우도 더 묻지 않고 끝낸다. 실패와 실행 중인 기능의 확인, 5초 상한은 유지한다.
  취소한 뒤 저장이 성공해도 앱은 남는다. 종료 요청 뒤 늦은 입력은 새 변경을 만들지 않고, 종료 저장에
  필요한 마지막 창 배치와 close는 받는다. host는 logic join 뒤에 사라지고 포인터는 원자적으로 나른다.

## 3. 자동 검사

- 영향 범위 먼저 실행: `[luil_followup],[function_buttons][s3],[function_buttons][r2]`,
  **35 test case, 832 assertions 통과**.
- Debug 앱·테스트 빌드 성공. 전체 CTest **1,210개 중 통과 1,207, 건너뜀 3, 실패 0**, 98.99초.
- Release 앱·테스트 빌드와 전체 CTest도 **1,210개 중 통과 1,207, 건너뜀 3, 실패 0**, 77.13초.
  건너뜀은 두 구성 모두 네트워크 test 미설정 두 건과 실제 SVN 미설치 한 건이다.
- 이번 Debug·Release 빌드와 최종 주석 반영 재빌드는 경고 0이다. 소스 스타일 504개 파일,
  `git diff --check`, 사용 설명서 HTML 생성·일치 검사(h2 22개·h3 57개, 문제 0)를 통과했다.
- 새 test case 3개: 최신 길이의 Scroll 백분율, 상태 변경의 live region과 무음 경과 시간,
  host UI 큐에 종료 명령을 한 번만 전달하는 driver 조립.
- 기존 종료 검사에 연속 저장, 성공 자동 종료, 실패 유지, 취소 후 성공, 상한 뒤 성공,
  실행 중 기능의 확인 유지, 이미 끝난 작업의 닫힘을 반영했다.
- 로그: `build/luil-followup-{debug,release}-{build,ctest}.log`, `build/luil-followup-targeted.log`.

## 4. 실제 앱 점검

새 Debug 앱을 `build/luil-latest-uia/gitman-luil-followup.exe`로 복사해 격리 데스크톱에서 실행했다.
같은 폴더의 전용 설정과 임시 저장소 셋을 썼고 사용자 설정·작업 저장소는 바꾸지 않았다.

| 항목 | 결과 |
| --- | --- |
| 흐르는 로그의 Scroll | PowerShell로 100ms마다 출력했다. UIA Scroll `0` 뒤 따라가기가 꺼지고 실제 값은 0. `100` 뒤 새 출력이 더 와도 실제 값은 100이었다. |
| 실제 상태 알림 | C++ UIA 클라이언트로 LiveRegionChanged를 수신했다. 시작의 `흐르는 로그, repo-b, 실행 중`과 중지 뒤 `흐르는 로그, repo-b`, 기능 줄의 비활성·재활성에 따른 알림을 받았다. |
| 중지·창 닫기 | 기능 버튼 Invoke로 중지하고 취소 로그를 확인했다. Window 패턴 Close 뒤 프로세스가 끝났다. 종료 코드는 관찰하지 못했다. |
| 느린 저장 후 자동 종료 | 성공·실패·취소·연속 저장은 결정적 controller 검사, 명령 연결은 실제 headless host 큐 검사로 확인했다. 실제 창에서 느린 저장을 재현한 결과로 읽지 않는다. |

캡처는 `build/luil-latest-uia/gitman-luil-followup.png`다. 같은 폴더의 `followup_probe.ps1`,
`live_event_probe.cpp`·`.exe`, `live-events.log`와 `build/luil-followup-uia.log`가 근거다.
이들은 build 산출물이며 커밋하지 않는다. 앱과 관찰기를 정리했다.

## 5. 남은 수동 확인

후속: 이 기록의 변경은 이후 `31aedb1`로 커밋했다. 느린 저장 대기·5초 상한·취소와 앱 세션
종료 경로도 [종료 저장 안정화](2026-11-05-shutdown-storage.md)에서 실제 격리 앱으로 확인했다.
아래 목록은 앱 연결 직후 시점이며 현재 확인 범위는 후속 기록이 덮는다.

이번 세 앱 연결은 끝났다. 이전에 남긴 conhost·고대비·실제 텍스트 크기 변경·내레이터 청취·경고음,
실제 느린 저장의 5초 상한과 세션 종료는 수동 확인 범위다. runtime 종료 순서, 닿지 않는 UNC 배치,
store 교체 재시도 등의 별도 제품 후속은 바꾸지 않았다.

## 6. 제안 커밋 메시지

```text
feat: 상태 낭독과 로그 백분율 흘림을 연결하고 저장 뒤 바로 종료함

- luil 요청 브랜치 최신 077262e를 소비하고 카드 접근 클릭·메뉴 Enter 반복을 검증함
- 배너와 기능 상태에 live region을 붙이고 로그 출력·경과 시간은 자동 낭독에서 뺌
- 로그 Scroll 백분율을 최신 내용 길이로 계산하고 100%에서 끝을 따라감
- 저장 성공 뒤 UI 종료 명령을 한 번만 내고 실패·취소·실행 중 기능은 확인을 유지함
- 회귀 검사와 실제 UIA Scroll·LiveRegionChanged 수신 결과를 검증 기록에 남김
- Debug·Release 각 1210건 중 통과 1207·건너뜀 3·실패 0, 스타일·설명서 검사를 통과함
```
