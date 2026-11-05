# luil 요청 완료판 갱신 검증 (2026-11-05)

## 1. 범위와 핀

- Gitman 기준: `main`, `bf0d7cb`. luil은 `d87096b81132bc0151223424f5488cb3cf547405`에서
  `077262ec8abfab9d06c3fe926d1c5bef724dc283`으로 옮겼다.
- 사용자가 지정한 `claude/blissful-mccarthy-1bmvzg`의 최신판이다. 요청 구현 `91ab3c9`와
  긴 popup의 입력·자식 메뉴·접근 좌표 보정 `077262e`가 함께 들어 있다.
- 확인 당시 luil `origin/main`은 `0e3927e`이며 요청 브랜치를 포함하지 않는다. 두 브랜치의
  공통 조상은 이전 핀 `d87096b`다. 이번 핀은 main 최신판이라는 뜻이 아니다.
- Skia 패키지 핀은 바뀌지 않았다(Skia 157, package revision 1). WebView는 계속 OFF이며,
  추가 submodule이나 패키지 취득 없이 `vs2026-tests` configure를 통과했다.
- Gitman의 production code는 바꾸지 않았다. D36의 정책 연결과 메뉴 Enter 반복 방지를
  기존 상호작용 fixture로 검사했다. 커밋과 push는 하지 않았다.

## 2. 핸드오프 요청 대조

| 요청 | luil 완료판과 Gitman 적용 범위 |
| --- | --- |
| 메뉴 Enter 자동 반복 | luil 메뉴 경로가 반복 Enter를 삼킨다. Gitman의 첫 활성 기능 기본 항목을 둔 메뉴에서도 검사한다. 일반 버튼의 모든 실행 키 반복을 막는다는 뜻은 아니다. |
| faulted 뒤 닫힘 훅 | luil의 `request_main_close`가 `faulted()`를 확인한다. 실제 앱 thread에 장애를 주입하지는 않았다. |
| live region | `set_live_region`과 UIA LiveRegionChanged가 생겼고 alert 역할은 기본 assertive다. 기능 편집기의 alert 메시지는 자동 적용되지만, 일반 상태 글의 낭독 대상과 빈도 지정은 Gitman 후속이다. |
| 소비한 Alt 뒷 메시지 | key report ticket으로 받은 키를 확인하고 `WM_SYSCHAR`·`WM_SYSKEYUP`을 처리한다. 실제 경고음 청취는 수동 확인이다. |
| UIA Scroll | 백분율 요청과 `scroll_area_config::scroll_to_percent`가 생겼다. Gitman 로그는 자체 줄 목록과 RangeValue 막대여서 새 Scroll 패턴을 연결하는 후속이 필요하다. |
| enabled·HelpText 변경 알림 | `access_info_of`가 enabled·tooltip 도움말을 발행본에 싣고 속성 변경을 알린다. 기존 비활성 버튼에 자동 적용된다. |
| 접근 Invoke의 on_click (D36) | provider가 실행 뒤 `access_click_event`를 input thread에 보낸다. Gitman policy가 기존 클릭과 같은 카드로 다음 단축키를 잇는다. |
| Windows 텍스트 크기 | UISettings.TextScaleFactor와 변경 구독, font size 표, metrics 재발행이 생겼다. Gitman의 기존 글꼴·줄 높이 계산이 이를 따른다. 시스템 설정을 바꾸는 실측은 하지 않았다. |
| 긴 메뉴 스크롤 | popup을 작업 영역 높이로 줄이고 휠·화살표 영역·키보드 강조로 흘린다. `077262e`가 흘린 뒤 입력·하위 메뉴·UIA 좌표도 맞춘다. Gitman의 12개/8개 쪽 나누기는 유지한다. |
| logic에서 앱 종료 | `app_host::post_ui_command`가 생겼다. 저장 완료 뒤 이 명령을 내는 Gitman 연결은 후속이다. 현재 저장 성공 뒤 종료를 한 번 더 누르는 동작은 그대로다. |

## 3. 회귀 검사

`tests/function_card_interaction_tests.cpp`:

- 새 D36 검사: p00을 선택하고 p01의 Tab 자리가 아닌 기능을 접근 명령으로 실행한 뒤,
  provider가 보내는 `access_click_event`를 넣는다. 실행은 한 번이고 선택·luil 초점은 그대로이며,
  다음 `Alt+2`·`Alt+↓`의 대상은 p01이다.
- 기존 첫 활성 기능 기본 항목 검사에 반복 Enter를 먼저 넣는다. 반복은 실행하지 않고,
  그다음 새 Enter 한 번만 기능을 실행한다.

## 4. 실행 결과

### 4.1 자동 검사

- `cmake --preset vs2026-tests`: 통과.
- `cmake --build --preset vs2026-tests-debug`: 앱·테스트 빌드 성공.
  luil_core의 MD4C C 소스에서 `/W4`를 `/w`로 덮는 MSVC 명령줄 경고 D9025가 1건 있다.
  Gitman 코드의 컴파일 경고는 없다. luil의 `src/CMakeLists.txt`가 해당 외부 소스의 경고를 끄는
  설정이며, 이번 소비자 갱신에서 luil 작업 트리를 고치지는 않았다.
- `ctest --preset vs2026-tests-debug --output-on-failure`: **1,207개 중 통과 1,204, 건너뜀 3,
  실패 0**, 95.05초. D36과 반복 Enter 검사, renderer smoke 4종도 통과했다.
- `cmake --build --preset vs2026-tests-release`: 앱·테스트 빌드 성공, 경고 0.
- `ctest --preset vs2026-tests-release --output-on-failure`: **1,207개 중 통과 1,204, 건너뜀 3,
  실패 0**, 75.96초. Debug와 같은 세 test만 건너뛰었다.
- 건너뜀은 공개 GitHub clone과 인증 원격(네트워크 test 미설정), 실제 SVN CLI(미설치)다.
- `scripts/check_source_style.ps1`: 503개 파일 통과. `git diff --check` 통과.
- `py scripts/render_user_manual.py`와 `--check`: h2 22개·h3 57개, 문제 0.
- 로그: `build/luil-requests-tests-{debug,release}-{build,ctest}.log`.

### 4.2 실제 앱 (UI Automation 격리 데스크톱)

새 Debug 실행 파일을 `build/luil-latest-uia/gitman-luil-probe.exe`로 복사하고 같은 폴더의
전용 앱 설정을 썼다. 실제 사용자 설정이나 저장소 대신 임시 Git 저장소 셋과 `PING.EXE -n 40
127.0.0.1` 카드·기능 줄 버튼을 썼다. 스크립트와 로그는 `build/luil-latest-uia/probe.ps1`,
`build/luil-requests-uia.log`에 있다(빌드 산출물, 커밋하지 않음).

| 항목 | 결과 |
| --- | --- |
| 신뢰·카드 기능 Invoke | 통과. repo-a를 고른 채 repo-b의 핑을 실행하고 `신뢰하고 실행`으로 승인했다. repo-b만 실행 중이고 repo-a 선택은 그대로였다. |
| 비활성 버튼과 사유 | 통과. repo-b 업데이트·전환은 같은 이름의 비활성 Button으로 남고 HelpText는 `기능 "핑"이 실행 중입니다`다. |
| 속성 변경 알림 | 통과. C# UIA 이벤트 관찰기로 업데이트·전환의 IsEnabled false/true, 실행 중 사유와 원래 tooltip으로 돌아가는 HelpText 알림을 모두 수신했다. |
| 로그·중지 | 통과. repo-b 선택 뒤 로그가 줄 목록으로 나타났고 기능 메뉴의 `핑 — 실행 중`을 Invoke해 중지했다. `경고: [핑] 취소됨` 줄과 버튼 재활성을 확인했다. |
| Scroll 패턴 연결 범위 | 로그 목록은 Selection, 로그 막대는 RangeValue만 공개한다. 새 Scroll 패턴이 앱에 자동으로 생기지 않음을 실제 응답으로 확인했다. |

앱과 UIA worker를 정리했다. D36의 다음 Alt 단축키 대상은 3장의 상호작용 검사로 확인하며,
실제 앱에 키를 주입해 확인한 결과로 읽지 않는다. 텍스트 크기·고대비 등 시스템 설정은 바꾸지 않았다.

## 5. 핀 갱신 시점의 후속 (이후 구현됨)

- 일반 상태 글의 live region 지정(무엇을 언제 읽을지 결정), 로그 목록의 UIA Scroll 백분율,
  저장 완료 뒤 `post_ui_command(application_exit)`를 연결하는 작업을 같은 날 사용자 지시로 마쳤다.
  현재 구현·검증과 최종 커밋 제안은 [앱 연결 검증](2026-11-05-luil-integration.md)에 있다.
- conhost, 고대비, 실제 텍스트 크기 변경, 내레이터 낭독, 경고음 청취, 느린 저장과 종료 상한의
  실제 앱 확인은 이전 수동 확인 범위를 유지한다.
- luil main에 요청 브랜치가 합쳐지면 그때 main 핀을 다시 검증한다. 이번 작업은 luil 저장소의
  브랜치나 main을 변경하지 않았다.

## 6. 제안 커밋 메시지

```text
chore: luil 요청 완료판으로 핀을 옮기고 카드 접근 클릭과 메뉴 반복을 검증함

- luil을 요청 브랜치 최신 077262e로 옮기고 Skia 패키지 핀은 유지함
- 접근 클릭 뒤 카드 단축키 대상과 메뉴 Enter 반복의 회귀 검사를 더함
- UIA로 기능 실행·중지와 활성·도움말 변경 알림을 확인함
- Debug·Release 전체 CTest 각각 1207건 중 통과 1204, 건너뜀 3, 실패 0을 확인함
- 소스 스타일·diff 공백 검사와 사용 설명서 HTML 일치 검사를 통과함
- 자동 검사 결과와 남은 앱 연결 작업을 핸드오프·사용 설명서에 반영함
```
