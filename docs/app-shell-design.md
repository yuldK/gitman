# 앱 셸 설계: 앱 단위 설정·시작 페이지·실행 인자·로그 적재

상태: **승인** (2026-08-21 작성, 2026-08-21 검수 승인)

2026-08-21 사용자 요구 4건의 설계를 기록한다. 지금까지 Gitman의 모든 상태는
열린 `.version-list` 문서 안에만 있었다. 이번 변경으로 **문서보다 바깥의
상태(앱 단위 설정)** 와 **문서 폴더에 남는 산출물(로그 파일)** 이 처음 생긴다.

| 항목 | 요구 |
|---|---|
| A1 | 앱 단위 설정이 필요하다. VSCode 시작 페이지를 모방해 최근에 연 프로젝트 목록을 제공한다 |
| A2 | 실행 인자로 프로젝트 파일을 주면 그 문서를 연 상태로 실행한다 |
| A3 | 파일 연결 적용 결과가 Windows 기본 다이얼로그다. 앱 스타일 다이얼로그로 바꾼다 |
| A4 | 문서 폴더에 `${projectname}.version-list.log` 폴더를 만들어 저장소별 로그를 파일로 적재한다 |

진행 순서는 A1 → A2 → A3 → A4다 (사용자 지시: 제시한 항목을 순차 진행).

---

## A1. 앱 단위 설정과 시작 페이지

### A1.1 앱 설정 파일

문서 밖의 상태를 담을 JSON 파일을 신설한다. 위치는 **실행 파일과 같은 폴더**다
(2026-08-21 검수 결정: 앱 위치와 동일).

```text
<gitman.exe가 있는 폴더>\gitman.app-settings.json
```

- Gitman은 단일 exe를 원하는 위치에 복사해 쓰는 배포 형태라(README "배포와
  데이터"), 설정도 실행 파일 옆에 둔다. 앱을 통째로 옮기면 최근 목록이 따라
  가고 known folder에는 아무것도 만들지 않는다.
- 실행 파일 폴더에 쓸 수 없으면(Program Files 같은 보호 위치) **저장만 실패하고
  앱은 그대로 동작한다**: 최근 목록은 그 세션의 메모리에만 남고 사유는 진단
  1건으로 알린다. 반복 실패해도 문구는 한 번만 남긴다.
- 형식은 문서와 같은 규칙이다: UTF-8, 공백 4칸, `schema_version`, 알 수 없는
  키 보존, 원자적 교체(`.bak` 백업 포함).

```json
{
    "schema_version": 1,
    "recent_documents": [
        {
            "path": "D:\\workspaces\\team.version-list",
            "display_name": "team",
            "opened_at": "2026-08-21T18:40:12Z"
        }
    ]
}
```

- `recent_documents`는 **최근 순**이며 상한은 **10개**다. 초과분은 뒤에서
  버린다.
- 같은 문서 판정은 정규화 경로(절대 경로, `/`→`\`, 대소문자 무시)로 한다.
  다시 열면 기존 항목을 맨 앞으로 올리고 시각만 갱신한다.
- `display_name`은 확장자를 뗀 파일 이름이다(`team.version-list` → `team`).
  표시 전용이며 저장 시점에 계산한다.
- 파일이 없거나 깨졌으면 **빈 설정으로 시작**한다. 문서 열기와 앱 실행은 이
  실패에 영향받지 않고, 사유는 진단 1건으로만 남긴다.

### A1.2 읽기·쓰기 경로 (스레드 규칙)

logic thread는 파일 I/O를 하지 않는다(ADR-004). 문서와 같은 방식으로 worker
작업을 2종 추가한다.

| 작업 | 시점 | lane |
|---|---|---|
| `load_app_settings` | logic 시작 직후 1회 | 문서 lane(0번) |
| `save_app_settings` | 최근 목록이 바뀔 때 | 문서 lane(0번) |

- 저장은 문서 저장과 같은 병합 규칙을 쓴다: 진행 중이면 한 번으로 합치고,
  결과는 `app_settings_saved_event`로 받는다. 실패는 조용히 진단만 남긴다
  (사용자 흐름을 막지 않는다).
- 최근 목록 갱신 시점은 **문서 열기 성공**(`document_loaded_event`에 문서가
  있을 때), **문서 생성 성공**, **선택 등록으로 문서가 교체될 때**다. 열기
  실패는 목록에 넣지 않는다.
- 앱 설정 파일 경로는 platform 계층이 정한다(`win32`의 실행 파일 경로 +
  `gitman.app-settings.json`). application 계층은 경로 문자열만 받는다 —
  test는 임시 폴더 경로를 준다.

### A1.3 시작 페이지

`view_empty_state::no_document`일 때 지금은 안내 문구 한 줄만 그린다. 이
자리를 **시작 페이지 element**로 바꾼다. 문서가 열려 있으면 존재하지 않는다.

구성(VSCode 시작 페이지를 모방하되 Gitman에 필요한 것만 둔다):

```text
  Gitman                                  (큰 제목 + 부제)

  시작                                    최근 항목
   $(folder-opened) 문서 열기…             team            D:\workspaces
   $(new-file)      새 문서 만들기…        client-tools    E:\work\tools
                                          (최대 10개, 클릭 시 열기)
```

- **시작 열**: `문서 열기…`(`show_open_document_dialog`),
  `새 문서 만들기…`(`show_generate_document_dialog`). 기존 toolbar 버튼과 같은
  명령이라 새 경로가 생기지 않는다.
- **최근 항목 열**: 행마다 이름(굵게) + 폴더 경로(흐림). 왼쪽 클릭이
  `open_document_intent`다. hover 강조는 카드·메뉴와 같은 규칙을 쓴다.
  행 오른쪽 `$(close)` 아이콘은 그 항목만 목록에서 지운다
  (`remove_recent_document_intent`). 목록이 비면 안내 문구를 대신 그린다.
- 존재하지 않는 문서를 클릭하면 기존 열기 실패 경로가 그대로 동작한다(상단
  notice 배너). 항목은 자동으로 지우지 않는다 — 네트워크 드라이브가 잠시
  끊긴 경우를 지우면 안 되기 때문이다.
- 창이 좁으면(폭 < 720 논리 픽셀) 두 열을 세로로 쌓는다. 목록이 길면 기존
  `scrollbar_element`를 재사용한다.
- 키보드 이동은 만들지 않는다(카드 목록과 달리 선택 개념이 없다). 마우스
  클릭만 받는다.

### A1.4 영향 범위

- 신설: `domain/app_settings.{h,cpp}`(모델과 정규화·병합 규칙, 순수 함수),
  `application/app_settings_store.h`(계약), `infrastructure/json_app_settings_store.{h,cpp}`
  (파싱·직렬화·파일 접근), `platform/win32/win32_app_settings_path.{h,cpp}`(경로),
  `presentation/ui/start_page_element.{h,cpp}`(A1.3).
- 확장: `app_messages.h`(작업 2종·event 2종·intent 1종), `logic_controller`
  (상태·핸들러·view 구성), `view_snapshot.h`(`start_page_view`),
  `build_ui_tree.cpp`, `ui_element.h`(element kind 3종), `task_scheduler`
  (lane 배정), `vcs_operation_executor`(작업 2종 실행과 파일 경로 보관),
  `win32_app_runtime`(조립과 시작 시 읽기 제출).

---

## A2. 실행 인자로 문서 열기

### A2.1 현황

이미 동작하는 경로다: `parse_application_options`가 확장자를 확인해
`workspace_document_path`에 담고, 창이 뜬 뒤 `open_document_intent`를 보낸다
(`win32_application.cpp`). 파일 연결(`shell\open\command` = `"exe" "%1"`)도 이
경로를 쓴다.

### A2.2 결정: 정규화와 실패 표면을 고친다

기능을 새로 만들지 않고 아래 3가지를 고친다.

1. **절대 경로 정규화**: 인자를 UI thread에서 `GetFullPathNameW`로 절대 경로로
   바꾸고 `/`를 `\`로 통일한 뒤 logic에 보낸다. 상대 경로로 실행하면 문서의
   상대 저장소 경로가 "문서 폴더 기준"으로 풀리지 않아 카드가 엉뚱한 곳을
   가리키는 문제가 있었다. drag & drop 경로도 같은 정규화를 거친다.
2. **확장자 오류 문구**: 인자 오류는 창을 만들기 전이라 지금처럼 시스템
   MessageBox로 알린다(스크립트·shell 진입점이므로 유지). 다만 문구에 허용
   형식을 넣는다.
3. **존재하지 않는 문서**: 지금처럼 창을 띄우고 문서 열기 실패 notice로
   알린다. 최근 목록에는 넣지 않는다(A1.2).

### A2.3 영향 범위

- 확장: `platform/win32/main.cpp` 또는 `win32_application`(정규화 위치는
  Win32 API를 쓰므로 platform 계층), `application_options` test 보강.
- 정규화 자체는 순수 규칙(`/`→`\`, 중복 구분자 정리)과 Win32 호출로 나눠
  순수 부분만 test한다.

---

## A3. 파일 연결 결과를 앱 스타일 다이얼로그로

### A3.1 현황

환경설정의 `연결 등록`·`연결 해제`는 registry 작업 후 결과를 `MessageBoxW`로
알린다(`execute_file_association_command`). 앱의 다른 모든 확인·오류는 Skia로
그린 in-app 다이얼로그라 이 창만 이질적이다.

### A3.2 결정: 공용 알림 다이얼로그를 신설한다

파일 연결 전용이 아니라 **재사용 가능한 알림 다이얼로그**를 만든다.

```cpp
// view_snapshot.h
struct notice_dialog_view
{
    std::u8string title {};            // "파일 연결"
    std::vector<std::u8string> lines {};  // 본문(여러 줄)
    bool error { false };              // 아이콘·강조 색 선택
};
```

- 흐름: UI thread가 registry 작업을 수행(지금과 동일) → 결과를 새 intent
  `show_notice_intent { title, lines, error }`로 logic에 보냄 → logic이
  `notice_dialog_` 상태에 담음 → view snapshot → `notice_dialog_element`가
  그린다. 닫기는 `dismiss_notice_intent`(확인 버튼·Esc·배경 클릭).
- 환경설정 다이얼로그 위에 겹쳐 뜬다. tree에서 컨텍스트 메뉴 바로 앞에 두어
  다른 다이얼로그보다 위, 메뉴보다 아래다.
- 성공 문구는 지금 문구를 유지하고, 실패는 진단 메시지를 줄 단위로 담는다.
- **창이 없는 경로는 그대로 둔다**: `--register-file-association` 명령줄
  모드와 명령줄 인자 오류는 창이 없으므로 MessageBox를 유지한다.
- 파일·폴더 선택기(`IFileOpenDialog`)는 shell 기능이라 계속 시스템
  다이얼로그를 쓴다. 이번 변경 대상이 아니다.

### A3.3 영향 범위

- 신설: `presentation/ui/notice_dialog_element.{h,cpp}`.
- 확장: `view_snapshot.h`, `app_messages.h`(intent 2종), `logic_controller`,
  `build_ui_tree.cpp`, `ui_element.h`(kind 3종), `win32_application`
  (MessageBox 호출 2곳 → intent 게시), 키보드 Esc 처리.

---

## A4. 저장소 로그 파일 적재

### A4.1 폴더와 파일 이름

문서가 `D:\workspaces\team.version-list`이면 로그 루트는 다음이다.

```text
D:\workspaces\team.version-list.log\
    frontend\20260821-184012.log
    a-b-c\20260821-184012.log
    c-drive_a-b-c\20260821-184012.log
```

- 루트 = **문서 경로 + `.log`** (요구의 `${projectname}.version-list.log`).
  문서를 처음 기록할 때 만들고, 만들지 못하면 그 세션 동안 파일 로그를 끈다.
- 저장소 폴더 이름 규칙 (사용자 지시):
  1. 기본은 작업 복사본 경로의 **마지막 폴더 이름**이다 (`C:\a\b\c` → `c`).
  2. 문서 안에서 이름이 겹치면 **겹치는 저장소들만** 상위 세그먼트를 하나씩
     앞에 붙여 서로 구분될 때까지 늘린다 (`a\b\c` → `b-c` → `a-b-c`).
  3. 루트까지 붙여도 겹치면 드라이브를 붙인다: `C:\a\b\c` →
     `c-drive_a-b-c`. UNC(`\\server\share\a\b`)는 `server-share_a-b` 형태로
     `\\`를 `server-share`로 접는다.
  4. 금지 문자(`<>:"/\|?*`, 제어 문자)는 `_`로 바꾸고, 끝의 `.`과 공백은
     지운다. 예약 이름(`CON`, `PRN`, `AUX`, `NUL`, `COM1`~`COM9`,
     `LPT1`~`LPT9`)은 뒤에 `_`를 붙인다.
  5. 그래도 260자 제한에 걸릴 만큼 길면 앞 80자 + 경로 해시 8자로 자른다.
- 이름 계산은 **문서 단위 순수 함수**다(`log_folder_names(문서의 저장소 목록)`).
  저장소가 추가·삭제되면 다시 계산하므로, 문서가 바뀌면 같은 저장소의 폴더
  이름이 달라질 수 있다. 과거 폴더는 지우지 않는다(요구: 삭제 불필요).
- 파일 이름은 로컬 시각 `YYYYMMDD-HHmmss.log`다. 같은 이름이 이미 있으면
  `-2`, `-3`을 붙인다.

### A4.2 파일 단위: 문서 세션당 저장소 1파일 (제안)

**결정**: 문서를 연 뒤 그 저장소에 **첫 로그가 생길 때** 파일을 하나 만들고,
문서를 닫거나 앱이 끝날 때까지 그 파일에 이어 쓴다. 파일 이름의 타임스탬프는
생성 시각이다 (2026-08-21 검수 승인).

- 근거: 작업 1건당 1파일로 하면 refresh만 눌러도 파일이 수십 개씩 늘어나고,
  한 번의 문제 상황(조회 → 실패 → 업데이트 재시도)이 여러 파일로 흩어진다.
- **적재 범위는 카드 로그 전부**다(검수 결정). 조회(refresh/status)의 수명
  주기·실패와 변경 작업(update·switch)의 출력까지 화면 로그와 같은 내용을
  그대로 남긴다.

### A4.3 기록 내용과 형식

메모리 ring buffer에 들어가는 record와 **같은 내용**을 그대로 쓴다(마스킹은
이미 적용된 값이다).

```text
2026-08-21 18:40:12.128 [lifecycle/information] update 시작 (git pull --ff-only)
2026-08-21 18:40:13.004 [stdout/information] Updating 3f2c1a9..8b71d02
2026-08-21 18:40:13.220 [stderr/warning] warning: ...
```

- UTF-8(BOM 없음), CRLF. 화면에서 접히는 progress record도 파일에는 모두
  남긴다(원본 보존).
- 파일 첫 줄에 머리글 1줄을 둔다: 문서 경로, 저장소 표시 이름, 작업 복사본
  절대 경로, 앱 버전.

### A4.4 스레드와 I/O 경로

logic thread는 파일을 쓰지 않는다. **로그 writer 스레드**를 신설한다.

```text
worker/logic → logic_controller(카드 buffer 적재) → log_file_sink::append(비차단 큐)
                                                        └→ writer thread → 파일
```

- `log_file_sink`는 application 계층 인터페이스다(`append(project_id, record)`,
  `set_document(문서 경로, 저장소 목록)`, `flush()`). 구현은
  `infrastructure/file_log_writer`와 win32 파일 API다.
- 큐는 상한이 있고(예: 8,192 entry) 넘치면 **가장 오래된 것부터 버리며**
  버린 수를 다음 기록에 남긴다. 디스크가 느려도 logic이 멈추지 않게 한다.
- 쓰기 실패(권한·디스크)는 카드 로그에 경고 1건을 남기고 그 세션 동안 파일
  로그를 끈다. 반복 실패로 로그가 도배되지 않게 한다.
- 종료 시 `app_runtime::shutdown`의 순서(취소 → worker join → logic join)
  다음에 sink flush → writer join을 둔다.

### A4.5 설정

문서 `settings`에 optional 필드 1개를 추가한다(schema version은 올리지 않는다,
기존 규칙과 동일).

```json
"settings": {
    "write_log_files": true
}
```

- 기본값 **true**(이번 요구의 기본 동작). 환경설정 다이얼로그에 토글 1행을
  추가한다("로그를 문서 폴더에 파일로 남김").
- 끄면 그 문서에서는 폴더를 만들지 않는다.

### A4.6 영향 범위

- 신설: `domain/log_file_naming.{h,cpp}`(폴더 이름 규칙, 순수 함수),
  `application/log_file_sink.h`, `infrastructure/file_log_writer.{h,cpp}`,
  `platform/win32/win32_log_file_system.{h,cpp}`(폴더 생성·append 열기).
- 확장: `logic_controller`(sink 주입과 호출 지점 2곳: lifecycle append와
  `operation_log_event` 처리), `workspace_settings`(필드 1개),
  `json_workspace_document`, `settings_dialog`(토글 1행), `app_runtime`(소유·종료).

---

## 검증 계획

| 항목 | 신규 test |
|---|---|
| A1 | 앱 설정 직렬화 왕복·알 수 없는 키 보존, 최근 목록 병합(중복·상한·정규화), 깨진 파일 무시, 시작 페이지 tree 구성과 클릭 액션, 목록 항목 제거 |
| A2 | 상대 경로 정규화, `/` 혼용, 확장자 오류 문구, 인자 경로가 open intent로 나가는 조립 test |
| A3 | 알림 다이얼로그 view 구성, Esc·배경·확인 닫기, 성공·실패 문구, 환경설정 위 겹침 순서 |
| A4 | 폴더 이름 규칙(중복 없음/중복/드라이브까지/UNC/금지 문자/예약 이름/길이), 파일 이름 충돌 회피, 형식 한 줄 포맷, 큐 넘침 시 손실 기록, 쓰기 실패 후 비활성, 설정 off 시 폴더 미생성 |

전체 CTest와 source style 검사, Debug 앱 빌드 + CPU/auto smoke를 항목마다
수행한다.

## 검수 결과 (2026-08-21)

| 질문 | 결정 |
|---|---|
| A4 파일 단위 | **문서 세션당 저장소 1파일** |
| A4 적재 범위 | **카드 로그 전부** (조회 + 변경) |
| A3 해석 | 환경설정의 **연결 등록·해제 결과 알림**이 맞다. 파일·폴더 선택기는 대상 외 |
| A1 저장 위치 | **실행 파일과 같은 폴더** (`gitman.app-settings.json`) |
