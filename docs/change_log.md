# 변경 이력

## 2026-08-22 - 환경설정 탭 구조 · 외양의 문서 특수화 · 창 배치 범위 · 덮어씀 배지

### 사용자 지시

- 환경설정 창을 섹션으로 계층화하고 섹션마다 탭을 둔다. 설정이 더 늘어나도
  견뎌야 한다.
- 테마와 키 컬러도 전역/문서 특수화로 나눈다. 키 컬러가 20개 규모라 지금처럼
  한 줄로 나열하면 담기지 않는다.
- 앱 단위 창 위치·크기는 문서를 열지 않은 경우에만 유지한다. 문서를 열면 그
  문서의 배치를 따르고 앱 단위로 기록하지 않으며, 문서를 여는 순간의 배치를 앱
  설정에 저장한다.
- `덮어씀` 배지가 얄팍하다. 세부 항목 바로 옆에 약간의 여백만 두고 배지를 붙인다.
- (진행 방식) 별도 브랜치에서 커밋까지 자동 진행하고 마지막에 전역 test를 실행한
  뒤 보고한다.

### 반영 내용

- 설계를 `docs/settings-tabs-and-appearance-scope-design.md`에 기록하고 체크포인트
  6구간으로 나눠 커밋했다.
- **창 배치 범위**: 문서가 열려 있는 동안의 배치는 문서에만 남긴다. 문서를 여는
  순간(열기·생성 채택) 직전 배치를 앱 설정에 한 번 기록해 다음에 문서 없이 시작할
  때 복원한다. logic이 현재 배치를 알고 있도록 UI thread가 `WM_EXITSIZEMOVE`와
  최대화 ↔ 복원 전환에서도 배치를 게시한다.
- **외양의 문서 특수화**: 문서 JSON에 `appearance` object(`theme`·`accent`)를 두고
  `apply_overrides`로 앱 값 위에 얹는다. 문서가 열려 있으면 외양 선택이 문서
  override를 편집하고, 배지가 정의를 지우면 앱 설정을 따른다. 외양은 지금처럼
  초안 없이 즉시 반영·저장한다.
- **탭 구조**: 행 번호와 자식 index로 배치하던 dialog를 탭 → 섹션 → 항목 모델과
  순회 배치로 재작성했다. 왼쪽 rail이 `도구`·`작업`·`외양`·`시스템` 네 탭을 고르고
  활성 탭의 항목만 컨트롤을 만든다. panel 높이는 가장 높은 탭에 맞춰 고정해 탭을
  옮겨도 창이 튀지 않는다. 파일 연결 버튼은 `시스템` 탭으로 옮겼다.
- **덮어씀 배지**: 항목 제목 왼쪽의 고정 열(56px)에서 48×18 알약으로 그리고,
  덮어쓴 항목은 블록 바탕으로 함께 구분한다. 배치가 글꼴 없는 logic thread에서
  계산되어 제목 문자열의 실제 폭을 알 수 없으므로 오른쪽이 아니라 정렬되는 왼쪽
  열을 골랐다.
- **키 컬러 20색**: `assets/accents.json`에 색상환을 따라 15색을 더했다. 색
  동그라미는 값 줄 아래의 줄바꿈 격자로 놓여 목록이 늘어도 담긴다.
- 신규 8 case를 추가했다(총 731 case).

### 영향 요구사항

- REQ-017 (환경설정), REQ-005 (표시)

## 2026-08-22 - 배너 컨텍스트 메뉴 · unix 경로 표시 · 테마와 키 컬러

### 사용자 지시

- 상단 배너에 컨텍스트 메뉴(`경로를 탐색기로 열기`, `vscode로 열기`)를 추가한다.
- 배너의 문서 주소와 저장소 카드의 경로를 unix 스타일로 바꾼다.
- 설정에서 라이트/시스템/다크 테마를 codicon 아이콘 토글로 지정한다.
- 키 컬러(현재 민트 하나)를 여러 색으로 늘리고 제시된 JSON 포맷을 읽는다.
- (진행 방식) 별도 브랜치를 따서 체크포인트마다 커밋하고 마지막에 보고한다.
  사용자가 각 커밋을 체리픽하며 사후 검수한다.

### 검수 결정 (2026-08-22)

- 키 컬러 목록은 **JSON 파일을 빌드 시점에 내장**한다 (`assets/accents.json` →
  CMake → `gitman/generated/accents.h`). 런타임 파싱도, 실행 파일 옆 파일 읽기도
  두지 않는다.
- `accentSoft`는 낮은 알파로 겹치는 옅은 강조 바탕, `accentEmphasisFg`는 바탕 위
  강조 글자로 배정한다.
- 배너의 `VSCode로 열기`는 **문서가 있는 폴더**를 연다 (탐색기는 문서 파일을
  선택한 채 연다).
- 테마 아이콘은 `lightbulb`(라이트) · `device-desktop`(시스템) ·
  `color-mode`(다크)다. codicon에 sun/moon이 없다.

### 반영 내용

- 설계를 `docs/theme-and-banner-menu-design.md`에 기록하고 검수 결정 4건을
  반영했다. 구현은 체크포인트 5구간(C1~C5)으로 나눠 커밋했다.
- **표시 경로 통일**: `to_display_path`로 배너 문서 주소·카드 경로·시작 페이지
  최근 폴더를 `/`로 그린다. 저장·셸 실행·파일 dialog가 쓰는 경로는 Windows
  원형이라 `view_snapshot`이 표시용 `document_display_path`를 따로 싣는다.
- **배너 컨텍스트 메뉴**: 카드 전용이던 메뉴를 대상 기준으로 일반화했다. 메뉴
  수준 경로를 항목 수준 `target_path`로 옮기고 logic의 메뉴 상태에 kind를 두었다.
  배너는 문서가 열려 있을 때만 우클릭 메뉴를 연다.
- **키 컬러 4역할**: `positive_accent`를 `accent`·`accent_hover`·`accent_soft`·
  `accent_emphasis_foreground`로 나누고 그리기 코드 35곳에 역할을 배정했다. 색
  목록은 `assets/accents.json`(민트·블루·퍼플·앰버·로즈)에서 생성하며 형식 오류는
  빌드를 세운다.
- **테마**: 앱 설정에 `appearance`(테마 선호 + 키 컬러 id)를 추가하고 UI thread가
  고대비·OS 앱 모드와 함께 해석한다(`resolve_color_theme`). 라이트 팔레트를
  더하고 생성 dialog도 같은 팔레트·caption 색을 쓴다.
- **환경설정 UI**: 외양 2행(테마 세그먼트 · 색 동그라미)을 추가했다. 문서 모드
  에서도 앱 설정을 편집하며 클릭 즉시 반영·저장된다(저장/취소 초안 아님).
- 신규 17 case를 추가했다.

### 영향 요구사항

- REQ-005 (카드 표시), REQ-017 (환경설정)

## 2026-08-22 - 앱 셸 배치 검증

### 반영 내용

- A1~A5 전체를 대상으로 최종 검증을 수행하고 `docs/verification/2026-08-22-app-shell.md`에
  기록했다. Debug·Release CTest 714 통과(실 Git·SVN 3건 skip), 정적 분석 무경고,
  Release 앱 빌드와 auto/CPU smoke 통과, source style 418 files 통과.
- `check_source_style.ps1`이 도구가 만든 `worktrees` 디렉터리를 검사 대상에서
  제외한다. 같은 파일이 원본 위치에서 이미 검사되므로 중복이며, 임시 체크아웃의
  줄 끝 때문에 검사가 실패하던 문제를 없앤다.
- README의 검증 요약을 이번 기록으로 갱신했다.

## 2026-08-22 - 저장소 로그 파일 적재 (A4)

### 사용자 지시

- 프로젝트 폴더에 `${projectname}.version-list.log` 폴더를 만들어 개별 저장소의
  로그를 적재한다. 파일은 타임스탬프.log 형태이며 오래된 로그는 지우지 않는다.
  같은 폴더명을 가진 저장소가 여럿이면 상위 주소를 연달아 붙이고(`a-b-c`),
  드라이브까지 구분해야 하면 `c-drive_a-b-c`처럼 만든다.
  (`docs/app-shell-design.md` A4, 검수 결정: 문서 세션당 저장소 1파일 + 조회
  로그까지 전부 적재)

### 반영 내용

- 로그 루트는 문서가 있는 폴더의 `.<문서 이름>.log`다 (2026-08-22 추가 지시로 이름
  앞에 `.`을 붙여 탐색기에서 저장소 폴더들 사이에 섞이지 않고 맨 앞에 모이게 했다). 저장소 폴더 이름은 순수 함수
  `log_folder_names`가 문서 단위로 계산한다: 마지막 폴더 이름 → 겹치는 것들만
  상위 세그먼트를 하나씩 붙임 → 그래도 겹치면 드라이브·UNC share를 앞에 붙임
  (`c-drive_a-b-c`, `server-share_a-b`). 마지막에 Windows 파일 이름 규칙(금지
  문자·끝의 점과 공백·예약 이름·길이 80자 + 해시)을 적용한다.
- 파일은 문서를 연 뒤 그 저장소에 **첫 로그가 생길 때** 만들고 앱이 끝날 때까지
  이어 쓴다. 이름은 로컬 시각 `YYYYMMDD-HHmmss.log`이며 같은 이름이 있으면 `-2`를
  붙인다. 첫 두 줄은 문서·저장소 머리글이고, 줄 형식은
  `2026-08-21 18:40:12.128 [stdout/warning] 내용`이다(UTF-8, CRLF).
- 적재 경로는 logic → `log_file_sink`(비차단 큐) → 전용 writer thread → 파일이다.
  logic thread는 파일을 만지지 않는다. 큐 상한 8,192를 넘으면 오래된 항목부터
  버리고 유실 수를 다음 기록에 한 줄로 남긴다. 폴더 생성·쓰기가 실패하면 그 문서의
  파일 로그를 끄고 사유를 카드 로그에 한 번만 남긴다.
- 검수 결정("조회 로그까지 전부")에 맞춰 조회 진단 중 경고·오류를 카드 로그에도
  남기도록 했다. 지금까지는 카드 표시로만 알렸다.
- 문서 설정 `write_log_files`(기본 켬)를 추가하고 환경설정에 토글 1행을 넣었다.
  끄면 폴더를 만들지 않으며, 저장 즉시 적재가 멈추거나 시작된다.
- 신규 13 case(폴더 이름 규칙 4종·파일 이름·줄 형식·writer 3종·실패 2종·logic
  배선 2종·설정 파싱·설정 토글·실제 Win32 폴더 생성과 덧붙여 쓰기) 포함 CTest
  714개가 통과했다.

## 2026-08-21 - 앱 스타일 알림 다이얼로그 (A3)와 문서 닫기 (A5)

### 사용자 지시

- 연결 파일 적용하는 다이얼로그가 윈도우 기본 다이얼로그다. 앱 스타일을
  사용하도록 바꾼다. (`docs/app-shell-design.md` A3)
- 프로젝트가 열린 경우에 명시적으로 닫을 수 있으면 좋겠다. 열려 있는 상태에서
  새 프로젝트 만들기 아이콘이 안 보였으면 좋겠다. (A5)

### 반영 내용

- 확인 버튼 하나짜리 **알림 다이얼로그**를 신설했다. 파일 연결 전용이 아니라
  재사용 가능한 공용 dialog다. UI thread가 registry 작업을 수행한 뒤 결과를
  `show_notice_intent`로 logic에 보내고, logic 상태를 거쳐 Skia로 그린다.
  확인 버튼·배경 클릭·Esc가 모두 닫기이며 다른 dialog 위, 컨텍스트 메뉴 아래에
  그려진다. 성공은 키 컬러 + `$(check)`, 실패는 오류 색 + `$(error)`에 진단
  메시지를 줄 단위로 담는다.
- 창이 없는 경로(`--register-file-association` 명령줄 모드, 명령줄 인자 오류)는
  기존 MessageBox를 유지한다. 파일·폴더 선택기는 shell 기능이라 대상이 아니다.
- toolbar에 **문서 닫기 버튼**을 추가했다. 문서가 열려 있을 때만 보이고, 누르면
  진행 중 변경 작업을 취소하고 미반영 창 배치를 한 번 저장한 뒤 문서·카드·선택·
  필터·로그·dialog 상태를 버리고 시작 페이지로 돌아간다. 최근 목록은 유지한다.
- **새 문서 만들기 버튼은 문서가 없을 때만** 보인다. toolbar가 "문서 없음 =
  열기·만들기", "문서 있음 = 닫기"로 대칭이 됐다.
- 추가 지시로 도구 막대 배치를 바꿨다. 전체 새로 고침은 **왼쪽 끝**, 문서 닫기는
  **오른쪽 끝**이다. 문서 경로는 새로 고침 오른쪽에서 시작하고 나머지 버튼은
  닫기 왼쪽으로 이어진다.
- 신규 6 case(알림 view·tree·hit·Esc, 문서 닫기 상태 정리, 배치 저장, 닫기 버튼
  액션, 도구 막대 배치)와 toolbar 규칙이 바뀐 기존 2 case를 갱신해 CTest 700개가
  통과했다.

## 2026-08-21 - 실행 인자 문서 경로 정규화 (A2)

### 사용자 지시

- 실행 인자로 프로젝트 파일을 제공하여 해당 프로젝트를 연 상태로 실행한다.
  (`docs/app-shell-design.md` A2)

### 반영 내용

- 인자로 문서를 여는 경로 자체는 이미 있었다. 실제 결함은 **경로를 정규화하지
  않는 것**이었다. 상대 경로로 실행하면 문서 기준 상대 저장소 경로가 문서 폴더가
  아닌 곳으로 풀려 카드가 엉뚱한 경로를 가리켰다.
- `absolute_workspace_document_path`를 신설해 명령줄 인자, drag & drop, 파일
  선택기 결과를 모두 절대 경로로 편 뒤 `open_document_intent`로 보낸다. 구분자를
  역슬래시로 통일하고 `.`·`..`과 끝 구분자를 정리하며, 펼 수 없는 형식은 원본을
  그대로 보내 기존 열기 실패 진단이 사유를 알린다. 존재 여부는 확인하지 않는다.
- 확장자 오류 문구에 사용 예를 덧붙였다. 창을 만들기 전이라 시스템 MessageBox는
  유지한다(스크립트·shell 진입점).
- 신규 1 case(상대·혼합 구분자·`.`/`..`·끝 구분자·UNC·잘못된 형식) 포함 CTest
  694개 통과.

## 2026-08-21 - 앱 단위 설정과 시작 페이지 (A1), 저장 backup 제거

### 사용자 지시

- 앱 단위 설정이 필요하다. VSCode 시작 페이지를 모방해 최근에 연 프로젝트
  목록을 제공한다. (`docs/app-shell-design.md` A1, 2026-08-21 검수 승인)
- `.version-list`와 설정 파일 모두 `.bak` 백업을 남길 필요 없다. 너무 과하다.

### 반영 내용

- 앱 단위 설정 파일을 신설했다. 위치는 **실행 파일과 같은 폴더**의
  `gitman.app-settings.json`이며(검수 결정), 최근에 연 문서를 최신 순으로 최대
  10개 담는다. 경로 열쇠는 `/`→`\` 통일과 대소문자 무시로 같은 문서를 판정하고,
  표시 이름은 확장자를 뗀 파일 이름이다.
- 읽기·쓰기는 `load_app_settings`·`save_app_settings` 작업 2종으로 문서 lane에서
  직렬화한다. logic은 파일을 만지지 않는다. 읽기가 끝나기 전에 문서를 열면 저장을
  보류했다가 파일 내용 위에 합쳐 한 번만 쓰고, 저장이 진행 중이면 다음 변경을
  한 번으로 합친다. 저장 실패(보호된 실행 파일 폴더 등)는 세션당 한 번만 알리고
  최근 목록은 메모리에만 남는다.
- 문서가 없을 때 안내 문구 한 줄 대신 **시작 페이지**를 그린다. 왼쪽은 `문서 열기…`
  ·`새 문서 만들기…`(기존 toolbar 명령과 동일 경로), 오른쪽은 최근 항목 목록이다.
  행 클릭은 문서 열기, 오른쪽 `x`는 목록에서만 제거한다. 창이 좁으면 두 열을
  세로로 쌓고, 남은 높이에 다 들어가지 않으면 들어가는 행만 그린 뒤 "외 N개 더
  있습니다"로 알린다. 시작 페이지에서는 하단 로그 pane을 두지 않는다.
- **저장 backup(`.bak`) 생성을 제거했다**(사용자 지시). 문서·앱 설정 모두 임시
  파일 → flush → 원자적 rename만 수행한다. `ReplaceFileW` 대신
  `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`을 쓰고 임시 파일 속성을 지운다.
  backup 읽기·복구 진단(`load_backup`, `backup_invalid`, `recovery_available`,
  commit 실패의 `restore`)과 관련 문서 설명도 함께 삭제했다.
- 신규 test 17 case(앱 설정 도메인·JSON·store·logic 병합·시작 페이지 view/tree/
  hit test)와 backup 기대를 바꾼 기존 test를 포함해 CTest 693개가 통과했다
  (실 Git·SVN 3건 skip). 신규·수정 파일의 clang-format과 source style도 통과했다.

## 2026-08-21 - 실환경 피드백 F6: SVN repo-browser

### 사용자 지시

- `docs/field-feedback-design.md`의 F6을 진행한다. 비어 있는 문서
  `svn_switch_targets` 후보 대신 저장소를 직접 탐색하는 SVN 전환 다이얼로그를
  만든다.

### 반영 내용

- SVN 전환 다이얼로그 초기 조회가 로컬 `svn info --show-item repos-root-url`과
  `url`을 읽는다. root 행을 자동으로 펼치고, 노드마다 비recursive
  `svn ls <url>`를 `remote_query` 제한으로 lazy 실행한다. `/`로 끝나는
  디렉터리만 표시하고 파일은 버린다.
- URL별 펼침·loading·loaded·failed 상태와 자식 cache를 순수 모듈로 분리했다.
  현재 URL까지 조상을 응답 순서대로 자동 확장하며, 접었다 다시 펼친 loaded
  노드는 process를 만들지 않는다. 다이얼로그가 닫히면 상태와 cache가 함께
  폐기되고 늦은 응답은 operation id로 무시된다.
- SVN UI는 들여쓰기, 펼침 글리프, 폴더/repository 아이콘, 현재 위치 강조,
  `조회 중…`/노드별 오류 행을 갖는 넓은 트리 panel이다. 인증 오류는
  `인증이 필요해 조회하지 못했습니다`로 구분한다. 현재 URL을 선택하면 [전환]
  버튼이 비활성이고 다른 디렉터리를 선택해야 실행된다. Git dialog는 기존 후보
  목록과 tracking branch 확인 흐름을 유지한다.
- `validate_svn_switch_target`에서 문서 허용 목록 대조를 제거했다. URL 형식 필터와
  작업 트리 보호는 유지하며, provider가 실행 직전에 대상 root/UUID를 원격
  재조회해 저장소 동일성을 확인한다. `svn_switch_targets`는 JSON에서 계속 읽고
  쓰되 후보·검증에는 사용하지 않는다.
- Apache Subversion `svn list` 문서 계약 기반 fixture와 builder/parser,
  provider, executor, 순수 트리 상태, logic/UI 테스트를 추가했다. 실제
  `svn.exe`가 없는 호스트라 명령 실측은 후속 검증으로 유지한다.
- 직접 영향 태그 159 case(1,475 단정) 중 158 통과, 실제 SVN 실행 1 skip.
  Debug 앱 빌드와 CPU/auto smoke, source style 394 files도 통과했다. 상세는
  `docs/verification/2026-08-21-feedback-f6-svn-browser.md`.

## 2026-08-20 - 실환경 피드백 F4: 로컬 변경 확인 다이얼로그

### 사용자 지시

- 미추적이 무엇인지 보여 줘야 한다. 세로 분할(상단 미추적·변경 목록, 하단
  diff viewer) 다이얼로그를 만든다. (설계 2.3 승인분)

### 반영 내용

- `query_local_changes`/`query_file_diff` 작업 2종과 provider 계약 확장.
  목록은 status의 항목 경로를 보존해 만들고(git/svn 매핑 순수 함수), diff는
  추적 파일이면 `git diff HEAD`/`svn diff`, 미추적이면
  `vcs_file_probe::read_prefix`(신설 계약)로 내용을 읽어 "추가" 취급으로
  표시한다. 이진(NUL)·미추적 디렉터리·256 KiB 초과는 안내 문구로 대체한다.
- 다이얼로그: 상단 목록(종류 배지+경로, 자동 첫 항목 선택), 하단 **2-way diff
  viewer**(좌 빨강/우 초록, `build_two_way_diff`로 짝짓기, 가시 범위만 그리기),
  휠·끌기 스크롤 막대 분리, Esc/배경/닫기. 진입점은 카드 더블 클릭(상태 chip은
  hit 불가 — 설계 조정 기록).
- 재검수 반영: 미추적 행에 `$(file)`/`$(folder)` 아이콘(배지 라벨 안)과 흐림
  처리, 행 오른쪽 `$(vscode)`/`$(folder-opened)` 외부 열기 아이콘(경로는 `…`
  말줄임). 인자를 담는 `open_external_request` 경로(input thread → app_runtime
  큐 → UI thread shell 실행)를 신설했다. diff의 탭은 4칸 공백으로 펼치고,
  절대 경로 구분자를 `\`로 통일해 explorer `/select,`가 동작하게 했다.
- 함께 반영한 UX 수정: 로그 pane을 상시 표시(선택 없으면 안내 제목의 빈
  pane), update 확인 overlay를 제거하고 submodule 여부를 문서
  `settings.update_submodules`(환경설정 토글, 기본 off)로 옮겼다 — update
  버튼은 즉시 실행된다.
- 환경설정 시각 위계: 제목에 `$(settings-gear)`+bold, 세부 기능 타이틀은
  키 컬러+semi-bold(버튼·토글은 타이틀 줄 아래로), submodule은 상태가 보이는
  토글 스위치, 본문은 흐리게(0.65/0.45).
- 검증: 신규 14 case + overlay 테스트 재작성, 직접 영향
  `[logic],[ui],[runtime],[schema],[store],[log]` 등 163 case 통과.
  상세는 `docs/verification/2026-08-20-feedback-f4-local-changes.md`.

## 2026-08-20 - 실환경 피드백 F3: 미추적 정책 완화

### 사용자 지시

- 미추적이 하나라도 있으면 업데이트가 실패하는 건 나쁜 경험이다. (검수 답변:
  완화는 update만, 전환은 현행 차단 유지)

### 반영 내용

- `working_tree_summary::has_tracked_changes()`를 신설해 update preflight의
  dirty 판정을 교체했다. 미추적 파일만 있으면 git pull/svn update가 진행되고,
  수정·충돌·`unknown`·진행 중 작업·index 잠금은 그대로 차단된다. 도구 자체
  보호(pull의 덮어쓰기 중단, svn update의 미버전 파일 보존)가 안전장치다.
- 전환(switch)의 `is_safe_for_change()`는 그대로 — 미추적 포함 모든 변경을
  막는다.
- `working_tree_dirty` 문구를 "커밋하지 않은 수정이 있어 갱신하지 않았습니다"로
  조정했다(미추적은 더는 이 사유를 만들지 않음).
- 검증: 신규 3 case + 직접 영향 범위(update/provider/domain 109 case, 실측
  integration 610 단정) 통과.
  상세는 `docs/verification/2026-08-20-feedback-f3-untracked.md`.

## 2026-08-20 - 실환경 피드백 F2: 제한 시간 정책 개편

### 사용자 지시

- 대형 저장소는 상태 확인만 5~10분 걸려 실패한다. 이를 감안한다. (검수 답변:
  기본값 상향 + 설정 항목까지 추가)
- 재검수: 환경설정 UI가 깨졌다. 상태 확인은 숫자만 받는 텍스트 박스로 제공하고,
  update는 실측 최대 3시간이니 가늠해 제한하지 말 것(명시적 취소로 충분).

### 반영 내용

- `local_query` 30초→600초, `remote_query` 120초→600초, **update는 무제한**
  (process runner의 timeout 없는 요청 = INFINITE 대기 경로 사용).
- 문서 `settings`에 `query_timeout_seconds`(10~3600초, 로컬·원격 조회 공통)를
  추가했다. 잘못된 값은 경고만 남기고 기본값을 쓰며, 기본값 복원 시 필드를
  지운다. `vcs_timeout_overrides`가 executor→provider→builder로 전달된다.
- 환경설정 dialog에 숫자 전용 텍스트 박스 행을 추가했다. 문자 입력 경로를
  신설했다: `WM_CHAR` → `character_typed_event` → (초점 칸으로)
  `edit_settings_timeout_intent` → logic이 숫자·backspace만 초안에 반영, 범위
  밖은 메시지와 함께 저장 차단.
- 텍스트 박스 초점(`interaction_snapshot::focused_input`)과 caret을 추가했다.
  초점은 박스 클릭으로만 생기고(자동 초점 없음), 초점 중 caret이 반주기
  530ms로 깜빡인다(초점 시각이 위상 기준, caret timer가 재그리기).
- 키 입력이 한 박자 늦게 보이던 게시 순서 race를 고쳤다: tree slot에는 wake
  신호가 없으므로 tree를 먼저, view(wake)를 나중에 게시한다.
- 검증: 신규·직접 영향 범위 한정 실행 통과(전체는 큰 단계 종료 시 실행).
  상세는 `docs/verification/2026-08-20-feedback-f2-timeouts.md`.

## 2026-08-20 - `build_skia.ps1`의 gn·ninja 로컬 탐색

### 사용자 지시

- 이 머신에는 `ninja`도 `gn`도 설치되어 있지 않다. `build_skia.ps1`이 로컬 경로를
  기반으로 둘을 찾게 한다. Skia에서 받을 수 있으면 그렇게 한다.

### 반영 내용

- `scripts/build_skia.ps1`이 `gn`과 `ninja`를 정해진 로컬 자리에서 먼저 찾는다.
  `third_party/skia-tools`, Skia의 `bin/fetch-*`가 두는 자리, Visual Studio가
  CMake 지원과 함께 설치하는 `ninja`, 마지막이 `PATH` 순이다.
- 자동 탐색은 1.13 미만의 `ninja`를 건너뛴다. `-NinjaPath`로 직접 준 것은 경고만
  하고 그대로 쓴다.
- `-FetchTools`를 새로 두었다. 어디에도 없을 때만 Skia의 `bin/fetch-gn`과
  `bin/fetch-ninja`를 돌린다. **기본값이 아니다.** 자동 취득을 사람이 인자로
  지시한 경우로 한정하는 것이 ADR-006이 그은 경계와 맞는다.
- 실패 메시지가 찾아본 자리를 모두 적고 세 가지 조치(`-FetchTools`, 브라우저 취득,
  경로 인자)를 안내한다.
- 개발 머신 실측: `ninja`는 Visual Studio 18 설치본(1.13.2)을 그대로 찾았고, `gn`은
  `bin/fetch-gn`으로 받아 `third_party/skia/bin/gn.exe`에서 찾았다. 이어서 `gn gen`이
  89 target을 만들고 Release 빌드가 그대로 진행됨을 확인했다.
- `docs/skia-build.md` 1·3장과 `README.md`의 준비 절차를 갱신했다. `.gitignore`에
  `/third_party/skia-tools/`를 추가했다.

### 영향 요구사항

- NFR-001 (빌드 재현성), ADR-006의 취득 경계

## 2026-08-20 - 실환경 피드백 F1b: 드래그 UX 개선

### 사용자 지시

- 드래그하면 카드 전체가 딸려가면서 기존 자리에서 빠지고, 다른 카드 사이에
  두면 그 위치의 여백이 벌어지는 시각 표현으로 바꾼다.

### 반영 내용

- 떠 있는 카드가 잡은 지점 그대로 포인터를 따라오고 원래 slot은 닫힌다.
  삽입 위치의 여백이 카드 한 장 크기로 벌어진다. tree는 불변 유지(ADR-004),
  draw가 offset만 이동하며 여백·drop 위치는 같은 순수 함수를 쓴다
  (`card_drag_insertion_slot`/`card_drag_offset`).
- drop 대상을 카드별(위/아래 절반)에서 목록 수준(가장 가까운 삽입 위치)으로
  바꿨다. 카드 사이 여백에 놓아도 동작하고 제자리 drop은 intent를 만들지
  않는다. 목록 빈 영역 클릭의 선택 해제는 유지된다.
- 검증: `[logic],[ui],[runtime]` 109 case 통과.
  상세는 `docs/verification/2026-08-20-feedback-f1b-drag-ux.md`.

## 2026-08-20 - 실환경 피드백 F1: 정렬 제거, 문서 순서 고정

### 사용자 지시

- 실환경 피드백 5건의 설계(`docs/field-feedback-design.md`)를 승인하고 F1부터
  순차 진행. 열린 결정 3건은 검수 답변으로 확정(미추적 완화는 update만,
  `svn_switch_targets`는 무시·보존, 타임아웃은 설정 항목까지).
- F1: 정렬 기능이 쓸모없다 — 문서 등록 순서 그대로 보여주고 드래그로 바꾸면
  문서에 반영되는 동작만 남긴다.

### 반영 내용

- 이름순/상태순 정렬과 toolbar 정렬 순환 버튼(`0fe4e69`, 후속 항목 1)을
  제거했다. 카드는 항상 문서 `projects` 순서로 표시되고, drag & drop → 문서
  갱신 → 저장은 기존 경로 그대로다.
- `card_sort_key`/`set_sort_intent`/`view_snapshot::sort`/`toolbar_sort`와
  정렬 비교자를 제거하고, 관련 테스트를 문서 순서 단정으로 재작성했다.
- 검증: `[logic],[ui],[runtime]` 108 case 통과, 스타일 374 파일 통과.
  상세는 `docs/verification/2026-08-20-feedback-f1-sort-removal.md`.

## 2026-08-19 - vcpkg 제거와 submodule 전환 (`N1`~`N5`)

### 사용자 지시

- vcpkg를 완전히 제거하고 의존성을 submodule로 둔다.
- Catch2는 test를 만들 때만 유효성을 확인하고 일반 빌드에서는 없어도 되게 한다.
- Skia를 submodule로 함께 두고 실측한 뒤 진행한다.

### 반영 내용

- `vcpkg.json`, `cmake/vcpkg_toolchain.cmake`, `cmake/triplets/`를 삭제하고
  preset에서 vcpkg 배선을 걷어냈다. preset 이름과 개수는 그대로 두어 사용자가
  익힌 명령이 바뀌지 않는다.
- `cmake/dependencies.cmake`와 `cmake/dependencies/`를 신설했다. Skia는 사용자가
  빌드한 산출물을 검사해 `unofficial::skia::skia`로 합성하고, nlohmann/json과
  Catch2는 submodule을 `add_subdirectory`한다. **`src/`는 한 줄도 수정하지
  않았다.**
- Catch2는 `GITMAN_BUILD_TESTS=ON`일 때만 구성한다. submodule을 초기화하지 않은
  상태에서 앱 빌드가 성립함을 확인했다.
- 제3자 고지 생성을 vcpkg installed 트리에서 submodule 라이선스 원문으로 바꾸었다.
  이에 맞춰 `tests/embedded_assets_tests.cpp`의 고지 형식 검사를 갱신했다.
- `scripts/build_skia.ps1`과 `scripts/verify_skia_root.ps1`을 추가하고
  `docs/skia-build.md`를 작성했다. 빌드 스크립트는 사용자가 손으로 실행하며
  CMake와 CTest는 호출하지 않는다.
- 검증: configure·Release·Debug 빌드와 install 성공, smoke 3종 통과, CTest
  VS2022 Debug **585/585**, VS2022 Release **585/585**(ASan 17 실계측 포함),
  VS2026 Debug **585/585**, `/analyze` 무경고, source style 351 파일 통과.
  `bin/gitman.exe` 6,510,080 byte (이전 7,185,920 byte).
- **렌더링 동등성을 픽셀 단위로 확인했다.** 변경 전 커밋을 별도 worktree에서
  vcpkg로 빌드해 기준선을 만들고 같은 조건에서 창을 캡처해 비교한 결과
  409,440 픽셀 전부 일치했다(최대 채널 차이 0). harfbuzz·icu 없이도 한국어와
  Codicon이 기존과 동일하게 그려진다.
- VS2022로 빌드한 Skia를 VS2026이 그대로 링크·실행함을 확인했다. generator마다
  의존성을 따로 빌드하던 vcpkg triplet 구성이 필요 없어졌다.
- 상세는 `docs/verification/2026-08-19-vcpkg-removal.md`.

### 영향 요구사항

- NFR-001 (빌드 재현성), ADR-002의 취득 수단

### 다음 작업 제한

- VS2026 재현, `/analyze` 무경고, 창 기반 렌더링 육안 검증이 남아 있다.
- 텍스트 구성(harfbuzz + libgrapheme) 도입은 `N6`이며 별도로 진행한다.


## 2026-08-19 - 의존성 구성 재편 설계와 Skia 수동 빌드 실측

### 사용자 지시

- 실사용 환경이 조직 프록시로 통제되어 curl 방식의 자동 취득이 불가능하다. 사람이
  브라우저로 접근하는 것은 모두 가능하다. GitHub에 소스만 제공하고 그 환경에서
  빌드해 쓴다.
- vcpkg를 완전히 제거한다.
- 의존성은 벤더링하지 않고 submodule로 둔다.
- Catch2는 test 프로젝트를 만들 때만 유효성을 확인하고, 일반 빌드에서는 없어도
  상관없게 한다.
- harfbuzz·ICU를 포함한 Skia가 필요할 것이므로 처리 방안을 제시한다.
- Skia를 submodule로 함께 두고 실측한다.

### 반영 내용

- ADR-006을 초안으로 작성했다. vcpkg를 제거하고 의존성을 submodule로 두며, Skia는
  사용자가 1회 직접 빌드한 산출물을 CMake가 연결만 하는 구성이다.
- `docs/dependency-provisioning-design.md`에 파일 단위 구현 설계를 작성했다.
- **Skia 수동 빌드를 실측했다 (S-1~S-3 전부 성공)**. Gitman은 `drawSimpleText`와
  `measureText`만 쓰고 shaping·codec·PDF·SVG를 쓰지 않아, 최소 구성이 external
  3개로 성립했다. `skia.lib` 52.4 MB (vcpkg 산출물은 538.0 MB).
- full ICU가 Windows에서만 `icudtl.dat`를 런타임 파일로 읽어 단일 exe 원칙과
  충돌함을 확인하고, `skia_use_libgrapheme`로 대체 가능함을 실측했다. 상세는
  `docs/verification/2026-08-19-skia-manual-build.md`.
- submodule 8개를 고정 commit으로 등록하고, D3D 패치와 GN args 파일을 저장소에
  넣었다.
- `scripts/check_source_style.ps1`의 제외 디렉터리에 `third_party`를 추가했다.

### 영향 요구사항

- NFR-001 (빌드 재현성), ADR-002의 취득 수단

### 다음 작업 제한

- ADR-006과 설계 문서는 초안이며 검수 대기 상태다.
- 다음 작업은 설계의 `N2`(CMake에서 vcpkg 제거와 Skia imported target 연결)다.
- 텍스트 구성(harfbuzz + libgrapheme) 도입은 `N6`이며 최소 구성 완료 후에 한다.


## 2026-08-19 - 단계 7 `S7-V1` 최종 검증

### 사용자 지시

- 단계 7 자동 진행 위임 (2026-08-18). 이 구간으로 단계 7이 마무리된다.

### 반영 내용

- **병렬 로그 격리 stress test를 추가했다**: 실제 `task_scheduler` worker 4개가
  카드 8개의 update를 병렬 실행하며 카드마다 로그 200줄을 쏟아 내도 각 카드
  buffer에 자기 로그만 sequence 순서대로 남는다 (REQ-008). 전체 CTest 585.
- 전체 검증 matrix를 수행했다: VS2022 Debug/Release(ASan 17 실계측 포함)와
  VS2026 Debug 각각 **585/585**, Debug 3회 반복 무결함, `/analyze` 무경고,
  clang-format 전수(235 파일) 무위반, source style(341 파일) 통과, Release
  install 단일 exe(7,185,920 byte)와 설치본 smoke 4종 통과.
- stage-7-plan 7장의 완료 조건 9개를 대조해 검증 기록 4장에 결과를 남겼다.
  창 기반 수동 검증 checklist 9항목을 5장에 제시했다.
- `docs/plan.md`, `docs/requirements.md`, `docs/handoff.md`를 단계 7 완료
  상태로 갱신했다. 상세는 `docs/verification/2026-08-19-stage-7.md`.

### 영향 요구사항

- REQ-006, REQ-007, REQ-008, REQ-012, REQ-015

### 다음 작업 제한

- 단계 7 완료 보고를 제출한다. 다음 작업은 사용자 확인 후 단계 8 계획(`S8-P0`)이다.
- SVN 실측, 실제 네트워크·인증, association 등록은 단계 8이다.

## 2026-08-19 - 단계 7 `S7-D4` switch dialog

### 사용자 지시

- 단계 7 자동 진행 위임 (2026-08-18).

### 반영 내용

- 카드 switch 버튼을 활성화하고 in-app switch dialog를 구현했다 (REQ-007).
  열면 remote-first 후보 조회를 제출하고, 후보 목록(remote group 먼저, stale
  표시)에서 선택·검증·실행을 진행한다. 조회·실행 중에도 UI는 멈추지 않는다.
- 현재 참조 후보는 확인을 차단하고 명령을 만들지 않으며, 나머지 검증은
  provider가 실행 직전에 재수행한다. 재검증 거부는 dialog에 사유를 표시한 채
  남고, 실행된 전환은 dialog를 닫고 카드 로그·상태로 추적한다.
- tracking branch 생성은 두 단계 확인(브랜치 만들고 전환 → 생성 확인) 후에만
  `tracking_branch_confirmed`로 실행한다 (단계 4 계약).
- 실제 Git 저장소 end-to-end(후보 조회 → 확인 → 전환·tracking branch 생성)를
  통합 test로 실측했다. dialog 공용 텍스트 버튼을 분리해 update overlay와
  공유한다.
- test 7개 추가, 전체 CTest **584/584** 통과. clang-format·style 통과. 상세는
  `docs/verification/2026-08-19-stage-7-d4.md`.

### 영향 요구사항

- REQ-005, REQ-007, REQ-009~REQ-012, REQ-014, REQ-015

### 다음 작업 제한

- 사용자 위임에 따라 `S7-V1` 단계 7 최종 검증을 진행한다.

## 2026-08-19 - 단계 7 `S7-D3` update 실행 UI

### 사용자 지시

- 단계 7 자동 진행 위임 (2026-08-18).

### 반영 내용

- 카드 update 버튼을 활성화했다 (REQ-006). 준비된 Git 카드는 submodule option이
  있는 확인 overlay를 열고, SVN 카드는 곧바로 실행하며, 실행 중에는 중지 버튼이
  되어 그 작업만 취소한다. 준비 전에는 비활성 + 사유 tooltip이다.
- update 확인 overlay를 in-app으로 구현했다: dim 배경(클릭·Esc 취소), panel의
  안내와 `submodule 함께 갱신` checkbox(기본 off, ADR-003), 실행·취소 버튼.
  overlay 상태는 logic이 소유하고 view snapshot으로 게시된다.
- 실제 Git 저장소의 executor update end-to-end(fast-forward 성공, 재조회
  snapshot, 로그 event 스트리밍)를 통합 test로 실측했다.
- test 5개 추가, 전체 CTest **577/577** 통과. clang-format·style 통과. 상세는
  `docs/verification/2026-08-19-stage-7-d3.md`.

### 영향 요구사항

- REQ-005, REQ-006, REQ-009~REQ-012, REQ-014, REQ-015

### 다음 작업 제한

- 사용자 위임에 따라 `S7-D4` switch dialog를 계속 진행한다.

## 2026-08-19 - 단계 7 `S7-D2` 선택 카드 전용 하단 로그 뷰

### 사용자 지시

- 단계 7 자동 진행 위임 (2026-08-18).

### 반영 내용

- 카드를 선택하면 목록 아래에 로그 pane(논리 160px)이 나타난다. 헤더에 카드
  이름과 필터 순환(전체/출력/오류)·자동 스크롤 토글·복사·지우기 버튼, 본문에
  시각 열과 심각도·스트림별 색의 record 표시다 (REQ-008).
- 필터 규칙·시각 형식·복사 텍스트는 `presentation/log_presentation`의 순수
  함수로 logic과 UI가 공유한다.
- `compute_list_layout`이 로그 pane을 반영한다. 창이 작으면 로그가 먼저 줄어든다.
- 자동 스크롤은 기본 켜짐, 위로 굴리면 꺼지고 맨 아래에서 복귀한다. 선택 변경은
  로그 뷰 상태를 초기화한다. 로그 pane 위의 휠은 로그를 스크롤한다.
- 복사는 UI thread 전용 `ui_command::copy_selected_log`이며 새 Win32 클립보드
  adapter가 표시 중 로그를 CRLF로 복사한다.
- test 9개 추가, 전체 CTest **572/572** 통과. clang-format·style 통과. 상세는
  `docs/verification/2026-08-18-stage-7-d2.md`.

### 영향 요구사항

- REQ-005, REQ-008, REQ-009~REQ-012, REQ-015

### 다음 작업 제한

- 사용자 위임에 따라 `S7-D3` update 실행 UI를 계속 진행한다.

## 2026-08-18 - 단계 7 `S7-D1` 로그 model과 변경 작업 배관

### 사용자 지시

- 단계 7 자동 진행 위임 (2026-08-18).

### 반영 내용

- 카드별 로그 ring buffer(`domain/operation_log`, 상한 1,000 record, 카드별
  단조 sequence)를 추가하고 logic thread가 소유한다 (ADR-004, plan 3.9).
- `operation_kind`에 update·switch_to·query_switch_candidates를, logic_message에
  변경 작업 intent 4종과 event 3종을 추가했다.
- executor가 변경 작업을 실행한다. provider의 log sink 자리에 배치 sink(16
  record)를 꽂아 프로세스 출력을 `operation_log_event`로 옮기고, 어떤 실패에서도
  종류에 맞는 final event를 보낸다.
- logic의 변경 작업 상태 기계: 작업별 취소 token(카드 단위 취소), busy 중복 거부,
  늦은 결과·로그 폐기, 완료 후 자동 refresh 연쇄, 수명 주기·진단의 로그 기록,
  종료·문서 교체 시 변경 작업 취소.
- test 19개 추가, 전체 CTest **563/563** 통과. clang-format·style 통과. 상세는
  `docs/verification/2026-08-18-stage-7-d1.md`.

### 영향 요구사항

- REQ-006, REQ-007, REQ-008, REQ-009~REQ-012, REQ-015

### 다음 작업 제한

- 사용자 위임에 따라 `S7-D2` 하단 로그 뷰를 계속 진행한다.

## 2026-08-18 - 단계 7 개시 및 `S7-P0` 계획 수립

### 사용자 지시

- 단계 7 작업을 개시한다. 지시가 필요하지 않으면 커밋까지 자동화해 단계 7을
  마무리하고 보고한다. (2026-08-18)
- 진행 원칙에 따라 이 지시로 단계 6 최종 검수와, 검수 대기 중이던 UI 개선·CMake
  구성 정리를 승인한 것으로 기록한다.

### 반영 내용

- `docs/stage-7-plan.md`를 작성했다. 카드별 구조화 로그(logic 소유 ring buffer,
  카드당 1,000 record), 선택 카드 전용 하단 로그 뷰(필터·복사·지우기·자동
  스크롤), update 실행(Git submodule option overlay, 실행 중 중지 버튼,
  per-operation 취소), switch dialog(remote-first group, 검증 실패 시 미실행,
  tracking branch 명시 확인), 명령 전후 자동 상태 갱신을 담는다.
- 체크포인트: `S7-D1` 로그 model·변경 작업 배관 → `S7-D2` 하단 로그 뷰 →
  `S7-D3` update UI → `S7-D4` switch dialog → `S7-V1` 최종 검증.
- 시작 기준선을 실측했다: `vs2022-tests` Debug 전체 CTest **544/544** 통과
  (검수 대기로 미뤄져 있던 스크롤·CMake 변경 이후의 첫 전체 실행).
- `docs/plan.md` 1.1/1.2와 `docs/handoff.md`를 단계 7 진행 상태로 갱신했다.

### 영향 요구사항

- REQ-006, REQ-007, REQ-008, REQ-012

### 다음 작업 제한

- 사용자 위임에 따라 `S7-D1`부터 `S7-V1`까지 자동 진행하며 체크포인트마다
  검증 문서를 남기고 커밋한다.

## 2026-08-18 - CMake target 정리 (솔루션 축소, test는 flag로 분리)

### 사용자 지시

- CMake가 만드는 프로젝트가 과도하다(19개쯤). 실제 빌드에 필요한 것만 프로젝트로
  포함하고, 테스트용 플래그를 넣으면 테스트 솔루션이 나오게 한다. (2026-08-18)

### 반영 내용

- **static library 10개를 `gitman_lib` 하나로 합쳤다.** 계층은 디렉터리 구조와
  소스 목록의 묶음·주석으로 유지하고, `source_group(TREE ...)`로 IDE에서도 같은
  구조로 보이게 했다. target 경계로 강제하던 계층 규칙은 코드 리뷰와 test가 맡는다.
- **custom target을 없앴다.** Codicons 생성은 생성 header를 라이브러리 소스에 넣어
  순서를 보장하고, 자산 checksum 검증은 라이브러리의 `PRE_BUILD` 명령으로 옮겼다.
  `gitman_messaging`은 소스 없는 INTERFACE target으로 되돌려 프로젝트가 생기지
  않는다.
- **`include(CTest)`를 `enable_testing()`으로 바꿨다.** Continuous·Experimental·
  Nightly·NightlyMemoryCheck dashboard target이 사라진다.
- **test와 개발 도구를 flag 뒤로 뺐다.** `GITMAN_BUILD_TESTS`(기본 OFF)와
  `GITMAN_BUILD_TOOLING`(기본 OFF)이다. preset에 `vs2022-tests`·`vs2026-tests`를
  추가해 test 솔루션을 별도 binary directory로 만든다. test preset도 이 configure
  preset을 가리킨다.
- 결과: 기본 솔루션은 `gitman`, `gitman_lib`와 VS 기본 target 3개뿐이다 (31개 →
  5개). test를 켜면 test target 3개와 `RUN_TESTS`가 더해진다.

### 영향 요구사항

- NFR-001, NFR-002 (빌드 구성)

### 검증

- `cmake --preset vs2022` 실제 configure 성공. 솔루션 프로젝트가 **31개 → 5개**로
  줄었다 (`gitman`, `gitman_lib`, ALL_BUILD, ZERO_CHECK, INSTALL).
- `cmake --build build/vs2022 --config Debug` **빌드·링크 성공** (경고 0).
  `gitman_lib.lib`와 `gitman.exe`가 만들어졌다. 이번 세션에서 쌓인 UI·스크롤·
  창 배치 변경도 이 빌드로 함께 컴파일·링크가 확인됐다.
- test·도구 flag를 켠 configure도 확인했다: 위 5개 + `gitman_tests`,
  `gitman_process_test_child`, `gitman_messaging_asan_tests`, `RUN_TESTS`,
  `gitman_format`, `gitman_format_check` = 11개.

### 다음 작업 제한

- **사용자 검수 대기**다. test 실행은 검수 뒤에 한다.

## 2026-08-18 - 스크롤 막대 여백 · 상단 막대와 카드 구분

### 사용자 지시

- 스크롤 막대 위아래에 약간의 여백을 준다.
- 배너와 카드가 겹칠 때 같은 색이라 구분이 안 된다. 그림자 등으로 구분을 준다.
  (2026-08-18)

### 반영 내용

- **스크롤 막대에 위아래 여백을 뒀다** (`layout_scrollbar_vertical_inset`, 논리 6px).
  thumb 비율 계산은 짧아진 track 기준이라 위치 대응은 그대로다.
- **상단 막대 아래에 그림자를 드리운다.** 카드와 도구 막대가 같은
  `surface_background`라 카드가 막대 아래로 들어가면 경계가 사라졌다. 목록이 위로
  밀려 있을 때만(`scroll > 0`) 목록 clip 안에서 위쪽 경계부터 아래로 옅어지는
  그림자를 그린다. 셰이더 없이 알파를 제곱으로 낮춘 띠 8개를 쌓으며
  (`draw_downward_shadow`), 스크롤이 막 시작된 구간에서는 그림자도 옅게 시작해
  갑자기 나타나지 않는다.
- **notice 배너에 바탕색을 깔았다.** 배너가 창 폭을 가득 채우고 theme의
  `notice_background`(어두운 적갈색)를 쓴다. 카드와 색이 달라 겹쳐 보이지 않고,
  경고 메시지라는 성격도 함께 드러난다. label에 바탕 역할과 좌우 여백 설정을
  추가했다.
- theme palette에 `content_shadow`와 `notice_background`를 더했다. 고대비 theme은
  흰색 그림자와 검은 배너 바탕을 쓴다.
- 검증: 변경한 파일을 MSVC `/Zs /W4 /permissive-`로 구문 검사해 오류가 없음을
  확인했다. clang-format·source style 검사 통과.

### 영향 요구사항

- REQ-005, NFR-011~NFR-014

### 다음 작업 제한

- **사용자 검수 대기**다. test 추가와 CTest 전체 실행은 검수 뒤에 한다.
- 그림자 높이(논리 8px)와 진하기(0.45)는 `list_metrics.h` 상수로 빼 두었다.

## 2026-08-18 - 스크롤 막대 끌기 · 크기 조절 테두리 축소 · 최소 창 크기

### 사용자 지시

- 스크롤을 잡고 드래그해 움직이는 기능을 만든다.
- 스크롤이 창 끝에 있어 크기 조절 이벤트와 겹친다. resize 이벤트를 받는 갭을 줄이고
  스크롤에 클릭·드래그를 붙인다.
- 최소 너비/높이를 480x320으로 지정하고, 직접 조정할 수 있게 값을 잘 빼 둔다.
  (2026-08-18)

### 반영 내용

- **element 계층에 `pointer_drag_target`을 추가했다.** 카드의 drag & drop(ghost +
  drop 대상)과 달리 누른 채 움직이는 동안 포인터 이동을 메시지로 바꾸는 경계다.
  `on_press`(누른 순간 1회)와 `on_move`(직전·현재 위치)로 이루어지며, interaction
  controller가 누름에서 대상을 기억해 눌린 동안의 이동을 그 element로 보낸다. 놓으면
  클릭으로 치지 않는다.
- **`scrollbar_element`를 추가했다.** 표시 전용이던 thumb를 element로 올려 클릭·
  끌기를 받는다. 이동은 픽셀 변화량을 `scrollable / (track - thumb)` 비율로 곱한
  `scroll_intent`(논리 픽셀)로 나가므로 휠과 같은 경로를 탄다. 상대 변화량만 쓰기
  때문에 스크롤로 tree가 다시 빌드되어도 끌기가 이어진다. thumb 밖의 track을 누르면
  그 자리로 한 번 이동한 뒤 이어서 끌리고, hover·누름에 따라 진해진다.
- **크기 조절 테두리를 좁혔다.** 시스템 기본값(보통 8px) 대신
  `resize_border_thickness`(논리 4px)를 쓰고 모서리만 `resize_corner_thickness`
  (논리 10px)로 넓게 둔다. 막대도 창 가장자리에서 6px 안으로 들어오고, 보이는 폭은
  8px이지만 클릭·끌기 hit 영역은 16px이다. 막대가 보이면 카드 폭을 그만큼 줄여
  클릭 영역이 겹치지 않는다.
- **최소 창 크기를 480x320(client 기준 논리 픽셀)으로 강제한다.**
  `WM_GETMINMAXINFO`에서 `AdjustWindowRectExForDpi`로 창 크기로 바꿔 넣고, 문서에서
  복원한 배치도 이 값보다 작으면 적용하지 않는다. 네 상수는
  `win32_application.cpp` 한 블록에 모여 있어 숫자만 바꾸면 된다.
- 검증: 변경·추가한 파일을 MSVC `/Zs /W4 /permissive-`로 구문 검사해 오류가 없음을
  확인했다. clang-format·source style 검사 통과.

### 영향 요구사항

- REQ-005, NFR-011~NFR-014

### 다음 작업 제한

- **사용자 검수 대기**다. test 추가와 CTest 전체 실행은 검수 뒤에 한다.
- 스크롤 막대의 Page Up/Down·Home/End는 후속 작업이다.

## 2026-08-18 - 좁은 창 글자 겹침 · 경로 표시 토글 · 상태 줄 강조

### 사용자 지시

- 앱을 가로로 좁히면 폰트와 UI가 겹쳐 스타일이 깨진다. 겹치지 않도록 예외 처리한다.
- 카드 경로를 전체 경로 대신 상대 경로로 표시하는 토글 버튼을 만들고, 그 설정을
  프로젝트(문서) 속성으로 기록한다.
- 경로 아래 브랜치·상태 표시가 단순 폰트라 인지가 어렵다. 블록·색상 등으로 시각
  강조를 준다. (2026-08-18)

### 반영 내용

- **글자를 폭 안에서 자른다.** `draw_primitives`에 `measure_text`·`elide_text`·
  `draw_text_within`을 추가했다. UTF-8 문자 경계에서 자르고 `…`를 붙이며, `…`도
  못 들어가면 그리지 않는다. label(문서 경로·notice·빈 상태), caption 제목, 카드의
  이름·경로·상태 줄이 모두 이 경로를 쓴다.
- **자리가 없으면 버튼을 숨긴다.** toolbar와 카드가 오른쪽부터 버튼을 놓다가 최소
  글자 폭을 지킬 수 없으면 그 버튼부터 숨긴다. 숨긴 버튼은 그리기와 hit test에서
  함께 빠진다. 카드 글자의 오른쪽 한계는 배치가 정하고 그리기가 그 값을 쓴다.
- **카드 경로 표시 토글을 추가했다.** toolbar의 `root-folder` 토글이
  `toggle_path_display_intent`를 보내고, logic이 문서의
  `settings.show_relative_paths`를 뒤집은 뒤 순서 변경과 같은 저장 경로로 문서에
  기록한다. 켜진 토글은 배경·아이콘 강조색으로 표시된다(`button_config::active`).
  상대 경로는 도메인의 순수 어휘 함수 `relative_windows_path`가 계산하며
  (filesystem 조회 없음, 대소문자 무시, drive가 다르면 전체 경로 유지, 위 폴더는
  `..`), 문서에 저장된 경로 값 자체는 바뀌지 않는다.
- **상태 줄을 조각(chip)으로 그린다.** 참조(브랜치/URL, SVN은 link 아이콘),
  리비전(`git-commit`), 동기화 상태(상태 강조색 배경 18% + 같은 색 글자), 작업 트리
  요약(경고색)을 각각 둥근 블록으로 나눴다. 남은 폭이 모자라면 조각 단위로 생략하고
  조각 안 글자도 잘린다.
- 검증: 변경한 19개 translation unit을 MSVC `/Zs /W4 /permissive-`로 구문 검사해
  오류가 없음을 확인했다. clang-format·source style 검사 통과.

### 영향 요구사항

- REQ-005, NFR-011~NFR-014

### 다음 작업 제한

- 이번 변경도 **사용자 검수 대기**다. test 추가와 CTest 전체 실행은 검수 뒤에 한다.
- 전체 빌드·링크는 아직 하지 못했다. vcpkg가 Skia를 다시 빌드하는 중이라 시간이
  오래 걸린다.

## 2026-08-18 - 창 배치 저장 · 생성 위치 선택 · 스크롤 UI 점검

### 사용자 지시

- `.version-list`에 마지막으로 열었던 위치와 앱 크기를 저장하고, 열 때 복원하며
  종료 이벤트에서 갱신한다.
- `.version-list`를 만들 때 저장 위치를 고를 수 있게 하고, 지금처럼 해당(스캔)
  폴더에 만드는 선택지도 체크 상자로 함께 제공한다.
- 카드가 많아 스크롤이 생길 때 상단 배너가 layout에 반영되지 않는다. 스크롤 UI를
  전반적으로 점검한다. (2026-08-18)

### 반영 내용

- 설계 초안을 `docs/window-state-and-scroll-design.md`에 정리했다 (검수 대기).
- **문서가 창 배치를 담는다.** `.version-list` top-level에 optional `window`
  object(`x`, `y`, `width`, `height`, `maximized`)를 추가했다. `settings`와 같은
  규칙으로 schema version은 올리지 않으며, 값이 깨졌으면 경고
  (`invalid_window_placement`)만 남기고 배치 없이 문서를 연다. 좌표는
  `WINDOWPLACEMENT::rcNormalPosition` 값 그대로라 저장·복원이 왕복 일관된다.
- **복원은 한 번만 일어난다.** logic이 문서를 채택할 때 `view_snapshot`의
  `window_placement_revision`을 올리고, UI thread는 번호가 바뀐 snapshot에서만
  `SetWindowPlacement`를 부른다. 최소 크기 미만이거나 저장된 사각형이 어떤
  모니터와도 겹치지 않으면 위치를 버리고 크기만 적용한다.
- **갱신은 종료 이벤트에서 한다.** `WM_CLOSE`가 `GetWindowPlacement` 결과를
  `window_placement_intent`로 보내고 곧이어 종료를 시작한다. logic은 배치가
  달라졌을 때만 문서를 갱신하고 `close_intent` 처리에서 저장 요청을 한 번
  내보낸다. ADR-005 7.3의 종료 순서에 "logic의 close 처리 확인" 단계를 끼워
  (상한 3초) 종료 저장이 worker inbox에 들어간 뒤에 inbox를 닫는다. 채널은 닫힌
  뒤에도 남은 요청을 소비하므로 저장은 worker join 안에서 끝까지 실행된다.
- **생성 dialog에 저장 위치 행을 추가했다.** `스캔 폴더에 만들기` 체크 상자가
  기본 체크이며 기존 동작과 같다. 체크를 풀면 위치를 따로 고를 수 있고
  (`IFileOpenDialog` 폴더 모드), 절대 경로·디렉터리 존재를 스캔 폴더와 같은
  기준으로 검증한다. 등록되는 프로젝트 경로가 절대 경로라 문서를 스캔 폴더 밖에
  두어도 해석이 달라지지 않는다. 체크 상자도 dark theme에 맞춰 owner-draw다.
- **스크롤 UI 6건을 고쳤다.**
  - notice 배너가 layout에서 자리를 차지한다. `compute_list_layout`을 새로 두어
    tree 빌드·카드 목록 element·logic이 같은 함수로 목록 영역을 계산한다 (지시
    사항. 이전에는 배너와 목록이 같은 y에서 시작해 카드가 배너를 덮었다).
  - 카드 목록이 자기 영역으로 `clipRect`하고 `hit_test`도 영역 밖을 먼저
    걸러낸다. 위로 스크롤된 카드가 toolbar 위에 그려지거나 그 자리의 클릭을
    가져가지 않는다.
  - 스크롤 한계를 필터를 통과한 카드 수로 계산한다 (이전에는 전체 카드 수).
  - 창 크기·필터·정렬이 바뀐 뒤 저장된 스크롤 값을 다시 고정한다.
  - 카드 목록이 위아래로 한 장씩 더 element를 만들되 화면 밖 카드는 보이지 않는
    상태로 둔다. 키보드 순회가 화면 끝에서 막히지 않고, 선택이 바뀌면 logic이 그
    카드를 화면 안으로 스크롤한다.
  - 내용이 화면보다 길면 오른쪽 여백에 스크롤 thumb를 그린다 (표시 전용).

### 영향 요구사항

- REQ-004, REQ-005, NFR-011~NFR-014

### 다음 작업 제한

- 이번 변경은 **사용자 검수 대기**다. test 추가와 CTest 전체 실행은 검수 뒤에
  진행한다.
- 스크롤 thumb 끌기, Page Up/Down, Home/End는 후속 작업이다.
- 문서 A를 열어 둔 채 B를 열면 A의 배치는 갱신되지 않는다. 배치는 닫을 때 열려
  있던 문서에만 기록한다.

## 2026-08-18 - `.version-list` 생성 기능 (깊이 1 저장소 탐색 → 새 문서)

### 사용자 지시

- 특정 경로의 깊이 1 하위 폴더에서 저장소를 확인해 `.version-list`를 만드는 기능.
  생성 이름 입력과 경로 지정이 있는 팝업 UI 제공 (2026-08-18).

### 반영 내용

- **toolbar에 생성 버튼(`new-file` codicon)을 추가했다.** 클릭하면
  `ui_command::show_generate_document_dialog`로 UI thread가 모달 팝업을 연다.
  팝업은 이름 텍스트 박스(표준 EDIT, IME 지원), 폴더 경로와 `IFileOpenDialog`
  폴더 선택(`FOS_PICKFOLDERS`), 생성·취소 버튼을 담고 앱 dark theme 색으로
  owner-draw한다. 폴더를 고르면 빈 이름을 폴더 이름으로 미리 채운다. Enter가
  생성, Esc가 취소다.
- **생성은 기존 탐색·등록 계층의 조합이다.** 새
  `version_list_generation_service`가 (1) 출력 경로 부재를 `project_store::load`의
  missing revision으로 확인하고 (2) `discovery_service::discover_children`으로
  깊이 1 후보를 모은 뒤 (3) `project_registration_service::register_candidates`로
  검증·유일 id 부여·원자적 저장까지 위임한다. 확인과 저장 사이에 파일이 생겨도
  missing revision의 낙관적 잠금이 충돌로 거른다.
- 메시지 흐름: dialog가 `generate_document_intent { scan_root, document_path }`를
  logic에 보내고, `operation_kind::generate_document`가 load·save와 같은 0번
  lane에서 실행되어 `document_generated_event`로 보고한다. 성공하면 생성된
  문서를 곧바로 열고(카드 로컬 조회 시작), 실패는 현재 문서를 유지한 채 진단을
  notice로 알린다. 진행 중에는 toolbar 생성 버튼이 비활성이고
  (`view_snapshot::document_generating`), 늦은 결과는 operation id로 버린다.
- 정책: 기존 문서는 덮어쓰지 않는다(`generation_output_exists`). 하위에서
  저장소를 못 찾으면 빈 문서를 만들지 않고 경고한다
  (`generation_no_repositories`, 스캔 루트 자체가 저장소면 상위 폴더 안내).
- 부수 수정: executor의 최상위 예외 경로가 `load_document`·`generate_document`
  실패에도 종류에 맞는 final event를 보내도록 분기를 추가했다 (기존에는 load
  실패가 `query_completed_event`로 잘못 나갔다). 문서 채택 시 이전 문서의 저장
  대기 상태(`pending_save_operation_id_`)를 함께 정리한다.
- test 12개 추가: 생성 service 단위(잘못된 요청, 덮어쓰기 거부, 후보 수집·정렬,
  저장소 없음, 취소)와 실제 디스크 round-trip 통합, logic의 위임·중복 차단·채택·
  실패 notice·늦은 결과 폐기, toolbar 버튼 표시·비활성, 클릭 → ui_command 변환.
  전체 CTest **544/544** 통과, clang-format·source style 검사 통과. main에 남아
  있던 format 위반(`win32_application.cpp`, test 2개 파일)도 formatter 적용으로
  함께 해소했다.

### 영향 요구사항

- REQ-004, REQ-005, NFR-011~NFR-014

### 다음 작업 제한

- 팝업은 Win32 자체 창이다. 앱 내부 overlay로 옮기려면 텍스트 입력 element와
  `WM_CHAR`/IME 경로가 먼저 필요하다 (ui-element-design 4장의 후속 항목).
- 탐색 후보를 미리 보여 주고 선택 등록하는 dialog(단계 7)는 별도 작업이다. 이
  기능은 선택 없이 발견된 저장소 전부를 문서에 넣는다.

## 2026-08-18 - 카드 drag & drop 순서 변경 (drag & drop 첫 소비자)

### 사용자 지시

- UI element 계층의 drag & drop 소비자까지 구현 (2026-08-18).

### 반영 내용

- **카드 body가 drag 출발지이자 도착지가 됐다.** 카드를 끌어 다른 카드의 위쪽
  절반에 놓으면 앞으로, 아래쪽 절반이면 뒤로 삽입된다. 대상 카드의 버튼 영역에
  놓아도 카드가 drop 대상으로 잡히도록 `ui_tree::find_drop_target`(drop 대상만
  걸리는 hit test)를 추가했다. drag ghost는 카드 표시 이름을 보여 준다.
- `reorder_card_intent { id, target, place_after }`를 추가했다. logic이 카드와
  문서의 프로젝트 순서를 함께 바꾸고 정렬을 `card_sort_key::custom`(문서 순서)으로
  전환한다. 제자리 drop도 custom 정렬 전환으로 간주한다.
- **순서가 문서 파일에 저장된다.** `operation_kind::save_document`가
  `project_store::save`(원자적 쓰기 + revision 낙관적 잠금)를 worker에서 호출하고
  `document_saved_event`로 결과를 보고한다. load·save는 scheduler의 0번 lane에서
  직렬화된다. 저장은 한 번에 하나만 나가고 진행 중 변경은 한 번으로 병합되며,
  다른 문서를 연 뒤 도착한 늦은 저장 결과는 operation id로 버린다.
- 저장 실패는 진단 message를 view notices의 맨 앞에 표시하고 다음 성공이 지운다.
- test 11개 추가: logic 재정렬·병합·실패 notice, executor save 경로, UI drag →
  intent 변환(위/아래 절반, 자기 자신 제외), 실제 파일에 새 순서가 저장되는
  runtime 통합 test. 전체 CTest **539/539** 통과 (문서 생성 기능 test 포함).
- 부수: `version_list_generation_dialog.cpp`의 style 검사 위반(여러 줄 brace 닫힘)
  을 재구성으로 해소했다.

### 영향 요구사항

- REQ-005, REQ-009~REQ-012, NFR-011~NFR-014

### 다음 작업 제한

- 정렬을 name/status로 되돌리는 UI는 아직 없다(`set_sort_intent`만 존재). 정렬
  전환 UI를 붙일 때 custom(문서 순서) 항목을 함께 노출한다.
- drag 중 목록 가장자리 자동 스크롤은 미구현이다. 필요해지면 interaction
  controller에 스크롤 intent 방출을 더한다.

## 2026-08-17 - UI element 계층 도입 (`docs/ui-element-design.md`)

### 사용자 지시

- 모든 UI를 최상위 추상 클래스에서 상속받는 일관 API로 모듈화. hover 강조·tooltip,
  왼쪽/오른쪽/더블 클릭 등록, 활성/비활성, drag & drop 액션 재설정 지원. 설계 초안
  검수 후 진행 (2026-08-17 승인, 검수 항목 4건 모두 제안안 채택).

### 반영 내용

- **`src/presentation/ui/` 모듈(`gitman_ui` target)을 신설했다.** 최상위 추상 클래스
  `ui_element`(arrange/draw/hit_test 계약 + 액션·tooltip·drag/drop·활성 상태 등록)와
  `button_element`, `label_element`, `caption_element`, `toolbar_element`,
  `card_element`, `card_list_element`, 불변 `ui_tree`, 순수 빌드 함수
  `build_ui_tree`로 구성된다.
- ADR-004를 지키기 위해 tree는 **snapshot마다 다시 빌드되는 불변 구조**다. logic
  thread가 `layout_snapshot` 대신 tree를 게시하고, 액션은 상태를 바꾸지 않고
  `logic_message` 또는 `ui_command`(dialog·창 명령, UI thread 전용)를 반환한다.
- `interaction_controller`(input thread)가 hover, 눌림, 왼쪽/오른쪽/더블 클릭,
  drag & drop 상태 기계를 소유하고 `interaction_snapshot`으로 게시한다. UI thread는
  이것으로 hover 강조와 tooltip(500ms 지연, WM_TIMER 재그리기)을 그린다. 판정은
  이벤트 timestamp만 사용해 test가 결정적이며, 더블 클릭 임계는
  `GetDoubleClickTime()`을 주입한다.
- caption을 통합했다. 창 버튼 3개가 일반 `button_element`이고 비클라이언트 클릭도
  tree에 등록된 액션을 실행한다. `WM_NCHITTEST` 동기 판정(`caption_layout`)은
  유지하고 tree 좌표와의 일치를 test로 고정했다.
- Win32 입력 전달을 확장했다: 오른쪽 버튼, `WM_LBUTTONDBLCLK` 재해석,
  `WM_MOUSELEAVE`(hover 해제), 이벤트 timestamp.
- drag & drop은 API·상태 기계·ghost/drop 대상 강조까지 구현했고 화면 소비자는
  후속 단계로 미뤘다 (검수 결정).
- 제거: `caption_ui`, `card_list_view`, `layout_model`, `input_controller`.
  스크롤 수학은 `presentation/list_metrics.*`로 이동했다.
- test: `ui_element_tests`, `ui_tree_build_tests`(기존 layout test 승계 + caption
  일치), `ui_interaction_tests`(기존 input test 승계 + hover/더블 클릭/오른쪽
  클릭/drag/pump)를 추가했다. 전체 CTest **521/521** 통과, style/format 통과,
  smoke 4종 통과.

### 영향 요구사항

- REQ-005, REQ-009~REQ-012, NFR-011~NFR-014

### 다음 작업 제한

- 단계 7에서 update/switch 버튼을 활성화할 때는 `build_ui_tree`에서 해당
  `button_element`에 액션을 등록하는 방식을 쓴다.
- drag & drop의 첫 소비자(카드 순서 변경)는 별도 검수 후 진행한다.

## 2026-08-17 - 단계 6 `S6-V1` 최종 검증

### 사용자 지시

- 단계 6 종료까지 자동 진행 (2026-08-17 위임).

### 반영 내용

- `build/vs2022`를 삭제하고 preset으로 다시 configure한 뒤 전체 검증 matrix를 수행했다.
- VS2022 Debug/Release와 VS2026 Debug의 전체 CTest가 각각 **511/511** 통과했다. Debug 3회 반복 무결함, Release ASan 17/17 실계측, `/analyze` 무경고, format/style(288개 파일) 통과.
- **120개 카드 heartbeat stress test를 추가했다.** 실제 provider가 카드 120개를 병렬 조회하는 동안 select·scroll intent가 계속 처리되고 snapshot 흐름이 유지되며 전 카드가 판정을 마친다. ADR-004의 "100개 이상 모의 카드" 검증 항목을 자동 test로 고정한 것이다.
- Release install이 `bin/gitman.exe` 단일 파일(6,807,552 byte)이며 설치본 renderer smoke 4종이 모두 종료 코드 0이다.
- 계획 6장의 완료 조건 10개를 대조해 검증 기록 3장에 결과를 남겼다. CTest 수는 단계 5 종료 437에서 **511**로 74개 늘었다.
- 창 기반 수동 검증 checklist 9항목(문서 열기, 카드 표시, refresh, 선택·키보드·스크롤, caption, DPI, 비활성 버튼, 종료)을 검증 기록 5장에 제시했다.
- `docs/plan.md`, `docs/requirements.md`, `docs/handoff.md`를 최종 상태로 갱신했다.
- 결과를 `docs/verification/2026-08-17-stage-6.md`에 기록했다.

### 영향 요구사항

- REQ-001~REQ-003, REQ-005, REQ-009~REQ-016
- NFR-005~NFR-009, NFR-011~NFR-014

### 다음 작업 제한

- 단계 6은 **사용자 최종 검수 대기** 상태다. 수동 checklist 확인이 포함된다.
- 승인 후 다음 작업은 단계 7(작업 UI와 로그) 계획 `S7-P0`이다.

## 2026-08-17 - 단계 6 `S6-D5` UI 렌더링과 앱 조립 및 test

### 사용자 지시

- 단계 6 종료까지 자동 진행 (2026-08-17 위임).

### 반영 내용

- `presentation/card_list_view.*`를 추가했다. view/layout snapshot만 읽어 toolbar, 카드(상태 글리프와 한국어 툴팁, 참조·리비전·작업 트리 요약), 활성 refresh와 비활성 update·switch 버튼, 빈 상태 5종, 진단 notice를 그린다.
- `smoke_view_state`에 앱 모드 포인터를 더해 renderer 배관을 바꾸지 않고 카드 화면으로 분기한다. caption은 두 모드가 같은 코드를 쓴다.
- layout에 custom caption 높이(40 논리px)를 반영했다.
- `platform/win32/win32_app_runtime.*`를 추가했다. Win32 구현체 주입, 채널·slot, logic thread(배치 처리 후 snapshot 게시), input thread, worker `min(4, hardware_concurrency)`의 scheduler와 ADR-005 7.3 순서의 shutdown을 한 곳에서 소유한다.
- `win32_application.cpp`에 앱 모드를 통합했다. `WM_APP` wake와 dialog marshal, 입력 메시지의 raw event 게시, 창 크기·DPI intent, `IFileOpenDialog` 문서 열기, `WM_CLOSE`/`WM_DESTROY`의 스레드 정리, 명령행 문서 열기다. smoke test 경로는 스레드를 만들지 않아 기존 smoke 4종이 그대로 유효하다.
- 실행 파일이 처음으로 messaging·workspace·process·vcs·discovery·app 전 계층을 링크한다. install 결과는 단일 exe(6,807,552 byte)로 유지된다.
- 창 없는 조립 통합 test 2개: 실제 문서 열기부터 실제 provider의 초기 조회까지 end-to-end 왕복과, 빈 조립의 초기 snapshot·멱등 shutdown이다.
- 전체 CTest가 508에서 **510**으로 늘었고 세 구성 각각 510/510, Debug 3회 반복, `/analyze` 무경고, format/style, 설치본 smoke 통과.
- 결과를 `docs/verification/2026-08-17-stage-6-d5.md`에 기록했다.

### 영향 요구사항

- REQ-001~REQ-003, REQ-005, REQ-009~REQ-016
- NFR-005~NFR-009, NFR-011~NFR-013

### 다음 작업 제한

- 사용자 위임에 따라 `S6-V1` 최종 검증까지 자동 진행한다.

## 2026-08-17 - 단계 6 `S6-D4` input thread 구현 및 test

### 사용자 지시

- 단계 6 종료까지 자동 진행 (2026-08-17 위임).

### 반영 내용

- `presentation/input_controller.*`를 추가했다. `raw_input_event` variant(포인터·휠·키), hit test 기반 intent 변환, input thread 소비 루프 `run_input_pump`다.
- 클릭은 같은 대상 위의 누름과 뗌으로 판정하고, 벗어난 release는 아무 intent도 만들지 않는다.
- 파일 dialog는 UI thread 전용이라 `input_action`이 logic 메시지와 dialog 요청을 구분한다. Win32 연결은 `S6-D5`가 담당한다.
- 키보드 초점(화살표·Enter·F5·Escape)은 input의 지역 상태이고 선택의 진실은 logic이 소유한다. 초점 이동은 layout의 보이는 카드 순서를 따른다.
- pump는 처리 직전 최신 layout으로 갱신하고, intent를 사본 재시도로 유실 없이 전달하며, 채널 close로 종료한다.
- test 7개: 클릭 해석 4종, 벗어난 release, 비활성 버튼, dialog 요청, 휠 변환, 키보드 순회, 실제 스레드 pump 왕복.
- 전체 CTest가 501에서 **508**로 늘었고 세 구성 각각 508/508, Debug 3회 반복, `/analyze` 무경고, format/style 통과.
- 결과를 `docs/verification/2026-08-17-stage-6-d4.md`에 기록했다.

### 영향 요구사항

- REQ-003, REQ-005, REQ-009~REQ-012, REQ-014, REQ-015
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- 사용자 위임에 따라 `S6-D5`부터 계속 자동 진행한다.

## 2026-08-17 - 단계 6 `S6-D3` scheduler와 worker pool 구현 및 test

### 사용자 지시

- 단계 6 종료까지 자동 진행 (2026-08-17 위임).

### 반영 내용

- `application/task_scheduler.*`를 추가했다. worker별 MPSC inbox와 안정 hash lane 배정으로 같은 카드는 FIFO 직렬, 다른 카드는 병렬이다(ADR-005의 MPMC 없는 분배). 문서 load는 0번 lane, 전체 동시 상한은 worker 수다.
- `application/operation_executor.h` 경계로 scheduler의 스레드 정책과 VCS 실행을 분리해 scheduler를 프로세스 없이 검증한다.
- `infrastructure/vcs_operation_executor.*`를 추가했다. store 경유 문서 load, settings 기준 도구 조사 cache, hint·단계 5 표식 기반 provider 선택, refresh의 로컬→원격 2단 event이며 어떤 실패에서도 final event를 보낸다.
- event 유실 방지: 가득 찬 logic inbox에는 사본으로 재시도하고 닫힌 inbox로 가는 event만 버린다(ADR-005 7.3).
- test 11개: 단일 worker의 접수 순서와 peak 동시 1, **다른 lane의 실제 병렬(peak 2)**, event 도달, shutdown 거부·멱등, 도구 없는 조회의 프로세스 0개, 표식 기반 provider 선택, refresh 2단 event, cache.
- 전체 CTest가 490에서 **501**로 늘었고 세 구성 각각 501/501, Debug 3회 반복, `/analyze` 무경고, format/style 통과. C4702와 formatter 위반을 같은 구간에서 해소했다.
- 결과를 `docs/verification/2026-08-17-stage-6-d3.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009~REQ-015
- NFR-005~NFR-009

### 다음 작업 제한

- 사용자 위임에 따라 `S6-D4`부터 계속 자동 진행한다.

## 2026-08-17 - 단계 6 `S6-D2` 표시·상태 모델과 logic 구현 및 test

### 사용자 지시

- 단계 6 종료까지 자동 진행 (2026-08-17 위임).

### 반영 내용

- 새 static library `gitman_app`을 추가했다. `logic_controller`, `app_messages`(intent·operation·`logic_message` variant·`operation_submitter` 계약), `view_snapshot`/`card_view_model`, `status_presentation`(plan 3.2 Codicon 표와 한국어 툴팁), `layout_model`(layout 상수·`compute_layout`·`hit_test`)이다. Win32에 링크하지 않는다.
- 문서 load도 worker에 위임한다. logic thread는 blocking 파일 I/O 금지이므로(plan 3.8) `open_document`는 작업 제출로 끝난다.
- 초기 표시는 활성 카드의 로컬 조회만이고 remote-first 판정은 refresh에서 수행한다 (plan 5.1). refresh worker는 로컬·원격 결과를 두 event로 순서대로 보고한다.
- generation 정책을 logic 한곳에 모았다. 오래된 세대와 삭제 카드의 event 폐기, 실행 중 중복 refresh의 병합 후 1회 재실행을 test로 고정했다.
- layout을 view snapshot의 순수 함수로 두어 렌더러와 input thread의 hit test가 항상 일치하게 했다. 화면에 걸치는 카드만 hit 영역을 만든다.
- 종료 intent가 취소 token을 전파하고 이후 refresh를 거부하는 것을 test로 고정했다.
- 전체 CTest가 471에서 **490**으로 늘었고 세 구성 각각 490/490, `/analyze` 무경고, Debug 3회 반복, format/style 통과. production 결함 없음, style 위반 2건은 명명 함수로 해소.
- 결과를 `docs/verification/2026-08-17-stage-6-d2.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-005, REQ-009~REQ-012, REQ-014, REQ-015
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- 사용자 위임에 따라 `S6-D3`부터 계속 자동 진행한다.

## 2026-08-17 - 단계 6 `S6-D1` messaging component 구현 및 test

### 사용자 지시

- `S6-P0`을 승인하고 진행한다. 단계 중 커밋도 위임한다.

### 반영 내용

- ADR-005의 `messaging::channel<T>`(MPSC FIFO)와 `messaging::latest_slot<T>`(최신값 병합)를 header-only INTERFACE target `gitman_messaging`으로 구현했다. 어떤 gitman target에도 의존하지 않는다.
- channel: 비블로킹 `post`, `try_receive`/`receive_wait`/`drain`, `close` 의미론(닫힌 뒤 post 거부·잔여 소비 가능), 비어 있다 채워질 때와 close의 signal callback, 통계, 주입 시계, debug consumer thread ownership assert.
- latest_slot: 덮어쓰기 게시와 `take_newer`, **미소비 batch당 1회 signal**로 연속 게시가 wake 하나로 병합된다.
- 계약 test 17개: FIFO·sequence·고정 시계, overflow 2정책과 sequence 구멍, close 의미론, signal 시점, drain 상한, move-only payload, receive_wait 3경로, 다중 producer 부분 순서, close 경합의 무유실, stress(8 producer × 20000).
- 같은 test를 `/fsanitize=address`로 빌드해 실행하는 `gitman_messaging_asan_tests`를 추가했다. Debug의 `/RTC1` 충돌 때문에 비Debug 구성에서만 계측하고, C5072에 따라 `/Zi`·`/DEBUG`를 켜고, vcpkg Catch2와의 STL 컨테이너 주석 불일치(LNK2038)는 주석 비활성으로, ASan 동적 runtime DLL은 cl.exe 디렉터리에서 복사로 해소했다.
- 전체 CTest가 437에서 **471**로 늘었고 VS2022 Debug/Release·VS2026 Debug 각각 471/471, Release ASan 17/17 실계측 통과. `/analyze` 무경고, Debug 3회 반복, format/style 통과.
- test lambda의 닫는 중괄호 style 위반 1건을 도우미 함수로 대체해 해소했다. production 결함은 없다.
- 결과를 `docs/verification/2026-08-17-stage-6-d1.md`에 기록했다.

### 영향 요구사항

- REQ-009, REQ-010, REQ-012, REQ-015
- NFR-009, NFR-014

### 다음 작업 제한

- 사용자 위임에 따라 `S6-D2`부터 `S6-V1`까지 자동 진행하며 체크포인트마다 커밋한다.

## 2026-08-17 - ADR-005 메시지 구조 확정과 단계 6 계획 `S6-P0` 수립

### 사용자 지시

- 다음 구간을 진행한다. 진행 원칙에 따라 이 지시로 `MSG-P0` 설계안을 승인한 것으로 기록한다.

### 반영 내용

- 승인된 메시지 구조 결정을 `docs/decisions/ADR-005-thread-messaging.md`로 기록했다. ADR-004의 구현 차단 조건이 해소됐다.
- `docs/stage-6-plan.md`를 작성했다. 단계 6 GUI와 상태 연결의 범위, 6구간 체크포인트, test 전략과 완료 조건을 담는다.
- 체크포인트 제안: `S6-D1` messaging component → `S6-D2` 표시·상태 모델과 logic → `S6-D3` scheduler와 worker pool → `S6-D4` input thread → `S6-D5` Skia 렌더링과 앱 조립 → `S6-V1` 최종 검증. 각 구간은 코드와 test를 함께 담는다.
- 단계 6 카드 동작 범위를 제안했다: 상태 표시와 전체/카드별 refresh까지 동작하고 update·switch·로그·탐색 dialog는 단계 7이다.
- 문서 열기 방식을 제안했다: 명령행 인자의 `.version-list` 경로 우선, 없으면 빈 상태 화면의 Win32 파일 dialog. association은 단계 8이다.
- worker 수 `min(4, hardware_concurrency)`와 전체 동시 작업 상한 초기값을 제안했다 (`docs/plan.md` 10장의 미확정 항목).
- 단계 6에서 실행 파일이 처음으로 `gitman_messaging`·`gitman_workspace`·`gitman_process`·`gitman_vcs`·`gitman_discovery`를 링크하고 Win32 구현체를 주입 조립한다.
- production code와 test는 변경하지 않았다. 전체 CTest 437 유지.

### 영향 요구사항

- REQ-002, REQ-003, REQ-005, REQ-012~REQ-016
- NFR-009, NFR-012~NFR-014

### 다음 작업 제한

- `S6-P0`은 사용자 계획 검수 대기 상태다. 특히 `docs/stage-6-plan.md` 7.1의 확정 필요 사항 5개에 대한 결정이 필요하다.
- 승인 전에는 `S6-D1`의 messaging component를 작성하지 않는다.

## 2026-08-17 - 범용 메시지 구조 설계안 `MSG-P0` 제출

### 사용자 지시

- 다음 구간을 진행한다. 진행 원칙에 따라 이 지시로 `S5-V1`과 단계 5 전체를 최종 승인한 것으로 기록한다.

### 반영 내용

- ADR-004의 단계 6 차단 조건에 따라 범용 스레드 메시지 구조 설계안 `docs/thread-message-design.md`를 작성했다. ADR-004가 요구한 11개 항목(API, envelope, topology, backpressure, 취소·shutdown, test 전략 등)을 모두 다룬다.
- 핵심 제안: consumer 스레드당 하나의 **MPSC FIFO `channel<T>`** 와 snapshot 병합용 **`latest_slot<T>`** 두 primitive, "스레드당 inbox 하나" topology, 비블로킹 `post()`와 signal callback 기반 wake-up(Win32 연동은 adapter의 `PostMessageW`), envelope 최소화(correlation·generation은 payload와 응용층), `close()` 기반 shutdown protocol, worker 분배는 MPMC 없이 scheduler 지정 inbox.
- late result와 dedup은 메시지 계층이 아니라 응용층 정책(generation 검사, lane 병합)임을 명시하고 ADR-004의 검증 항목과 연결했다.
- test 전략: 단일 스레드 계약 test(완전 결정적), producer별 부분 순서만 단정하는 다중 스레드 test, stress와 3회 반복, MSVC ASan job 추가 제안. **TSan은 MSVC 미지원**이므로 단일 consumer 설계와 stress 반복으로 완화하고 한계를 명시했다.
- library 구성: 최상위 namespace `messaging`, `src/messaging/` 신규 target, gitman의 어떤 target에도 의존하지 않는 분리.
- production code와 test는 변경하지 않았다. 전체 CTest 437 유지.
- `docs/plan.md`, `docs/requirements.md`, `docs/handoff.md`를 갱신했다.

### 영향 요구사항

- REQ-015, REQ-012
- NFR-009, NFR-014

### 다음 작업 제한

- `MSG-P0`은 사용자 설계 검수 대기 상태다. 특히 `docs/thread-message-design.md` 12장의 검수 요청 항목 8개에 대한 결정이 필요하다.
- **설계 승인 전에는 message queue, dispatcher, thread bridge를 구현하지 않는다** (ADR-004 차단 조건).
- 승인 후에는 ADR-005 기록과 `S6-P0` 단계 6 계획을 진행한다.

## 2026-08-17 - 단계 5 `S5-V1` 최종 검증

### 사용자 지시

- 단계 5 종료까지 자동 진행 (2026-08-17 위임).

### 반영 내용

- `build/vs2022`를 삭제하고 preset으로 다시 configure한 뒤 전체 검증 matrix를 수행했다.
- VS2022 Debug/Release와 VS2026 Debug의 전체 CTest가 각각 **437/437** 통과했다. `/analyze`는 무경고, 전체 suite 3회 반복 무결함이다.
- aggregate `gitman_format_check`(248개 파일), `gitman_source_style`, `gitman_assets_checksum`, `git diff --check`가 통과했다.
- Release install 결과가 `bin/gitman.exe` 단일 파일(6,255,616 byte, 단계 4와 동일)이며 설치본 renderer smoke test 4종이 모두 종료 코드 0이다. 실행 파일은 `gitman_discovery`를 링크하지 않으므로 크기가 그대로인 것이 맞다.
- 보강 test 2개를 추가했다. **자식 사이 취소**가 처리 중이던 후보를 보존하고 다음 자식으로 넘어가지 않는 것과, **한글·공백·emoji 자식 이름**의 실제 filesystem 왕복이다. production source는 `S5-D3` 이후 변경하지 않았다.
- 계획 9장의 완료 조건 15개를 대조해 검증 기록 4장에 결과를 남겼다. CTest 수는 단계 4 종료 393에서 **437**로 44개 늘었다.
- 자식 단위 접근 실패를 관찰할 수 없는 `vcs_file_probe` 계약 한계와 네트워크 드라이브 미검증을 알려진 한계로 기록했다.
- ADR-004 범용 메시지 구조의 단계 6 사전 설계 게이트를 검증 기록 6장에 재고지했다.
- `docs/plan.md`, `docs/requirements.md`(6장 이관 항목 확정), `docs/handoff.md`를 최종 상태로 갱신했다.
- 결과를 `docs/verification/2026-08-17-stage-5.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-004, REQ-009~REQ-013
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- 단계 5는 **사용자 최종 검수 대기** 상태다.
- 승인 후 다음 작업은 ADR-004 범용 메시지 구조 설계안 제시다. 이 설계의 사용자 승인 전에는 message queue, dispatcher, thread bridge를 작성하지 않으며 단계 6을 시작하지 않는다.

## 2026-08-17 - 단계 5 `S5-D3` 선택 등록 구현 및 test

### 사용자 지시

- 단계 5 종료까지 자동 진행 (2026-08-17 위임).

### 반영 내용

- `application/project_registration_service.h/.cpp`를 추가했다. 선택 재검증, id 생성, `project_definition` 변환, 문서 갱신과 revision 전달이다.
- **부분 등록은 없다.** 빈 선택, 제외 후보, 종류 미판정, 상대 경로, 문서와의 중복, 선택 목록 안의 중복 중 하나라도 있으면 저장을 호출하지 않고 전체를 거부한다. fake store의 저장 호출 수 0으로 단정했다.
- 정규화 값은 등록 시점에 다시 해석한다. 탐색 시점 값으로 중복 검사를 통과시키지 않는다.
- id는 디렉터리 이름 그대로, 충돌 시 `-2`부터 숫자 접미사다. `make_unique_registration_id`를 순수 함수로 분리해 접미사 재충돌과 한글 이름을 직접 test한다.
- 저장 충돌 감지는 단계 2 store의 revision token 비교를 그대로 사용한다. 실제 store 통합 test에서 외부 수정 후의 등록이 `concurrent_modification`으로 거부되고 **외부 수정본이 원문 그대로 남는 것**을 확인했다.
- 실제 round-trip test에서 등록 저장 후에도 unknown field와 `settings`의 알 수 없는 키가 보존되는 것을 확인했다.
- `diagnostic_code`에 `registration_candidate_rejected`를 추가했다.
- 전체 CTest가 428에서 **436**으로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 436/436 통과했다. `/analyze` 무경고, Debug 3회 반복 통과, format/style 통과. production 결함은 없다.
- 결과를 `docs/verification/2026-08-17-stage-5-d3.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-004, REQ-010, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- 사용자 위임에 따라 `S5-V1` 최종 검증까지 자동 진행한다.
- 단계 5 전체에 대한 사용자 검수는 `S5-V1` 보고에서 받는다.

## 2026-08-17 - 단계 5 `S5-D2` 탐색 실행 구현 및 test

### 사용자 지시

- `S5-D1`을 기존 로그와 유사한 메시지로 커밋하고, 단계 5가 끝날 때까지 자동으로 진행한다.

### 반영 내용

- `S5-P0`과 `S5-D1`을 기존 커밋 관례(`feat: 단계 N ...`)로 나눠 커밋했다.
- `application/discovery_service.h/.cpp`를 추가했다. 루트 검증, 깊이 1 열거, 표식 판정 적용, 중복 표시, 자식 경계 취소와 구조화 진단이다.
- **service는 `process_runner`를 받지 않는다.** 의존성이 열거·probe·해석기뿐이라 계약 수준에서 프로세스 실행이 불가능하며, 계획 9장의 "탐색은 프로세스를 만들지 않는다" 완료 조건이 구조로 보장된다.
- reparse point 자식은 표식을 확인하지 않고 제외한다. 대상이 실제 저장소여도 판정하지 않는 것을 test로 고정했다.
- 중복 표시(`already_registered`)는 선택 가능한 후보에만 씌운다. 이미 제외된 후보의 사유를 덮으면 실제 원인이 가려진다. 비활성 프로젝트와의 중복도 같은 사유로 본다.
- 루트 자체 판정 `root_is_repository`는 작업 복사본과 bare 저장소를 모두 참으로 본다.
- 현재 `vcs_file_probe` 계약은 접근 실패를 구분하지 않아 자식 하나의 `probe_failed`를 켤 수 없다. 판정 규칙의 `inaccessible` 분기는 계약 확장을 위해 유지하고 한계를 header 주석과 검증 기록에 남겼다.
- `diagnostic_code`에 `discovery_root_unavailable`, `discovery_child_skipped`, `discovery_cancelled`를 추가했다.
- fake 기반 시나리오 test 11개와 실제 환경 통합 test 5개를 추가했다. 통합 test는 실제 `.svn` 표식(svn.exe 불필요), `git worktree add`가 만든 `.git` 파일, `git init --bare`, **`mklink /J`로 만든 실제 junction**, 구분자가 다른 표기의 실제 중복 감지, 자식 300개 완주를 다룬다.
- `scoped_scan_directory` 공용 임시 디렉터리 도우미를 추가하고 열거 test가 함께 쓰도록 정리했다.
- 전체 CTest가 412에서 **428**로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 428/428 통과했다. `/analyze` 무경고, Debug 3회 반복 통과, format/style 통과.
- 개발 중 style 위반 1건(여러 줄 중괄호 초기화의 닫는 중괄호 위치)을 한 줄 표현과 formatter 수용으로 해소했다. production 결함은 없다.
- 결과를 `docs/verification/2026-08-17-stage-5-d2.md`에 기록했다.

### 영향 요구사항

- REQ-004, REQ-009, REQ-010, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- 사용자 위임에 따라 `S5-D3` 선택 등록과 `S5-V1` 최종 검증까지 자동 진행한다.
- 단계 5 전체에 대한 사용자 검수는 `S5-V1` 보고에서 받는다.

## 2026-08-17 - 단계 5 `S5-D1` 탐색 계약·판정과 열거 구현 및 test

### 사용자 지시

- `S5-P0`을 승인하고 진행한다. 단계 5부터는 production code와 test code 작성을 한 검수 구간으로 같이 진행한다.

### 반영 내용

- `docs/stage-5-plan.md` 7장의 체크포인트를 `S5-P0`, `S5-D1`~`S5-D3`, `S5-V1`의 5개로 개정하고 10.0에 검수 결과를 기록했다. `CODE`/`TEST` 분리와 별도 `FIX` 체크포인트를 두지 않으며, 검수에서 발견된 결함은 해당 구간의 재제출로 처리한다.
- `domain/discovery.h/.cpp`를 추가했다. 후보·결과 값 type, 제외 사유 7종과 **표식 판정 순수 함수** `classify_discovery_markers`다. 판정 순서는 확인 실패 → 메타데이터 충돌 → `.git` 디렉터리 → `.git` 파일 → `.svn` → bare 휴리스틱 → 비저장소다.
- bare 휴리스틱은 `HEAD`+`objects`+`refs` 세 표식이 모두 있을 때만 인정하고 kind는 `git`으로 남긴다. 목록에 "Git bare 저장소라서 제외"라는 정보가 필요하기 때문이다.
- 후보 정렬 `discovery_candidate_before`를 추가했다. 이름 ASCII 대소문자 무시 → code unit → 절대 경로 순서로 filesystem 열거 순서와 무관하게 결정적이다.
- `application/directory_enumerator.h`를 추가했다. 깊이 1 열거 계약이며 항목마다 디렉터리·reparse point 여부를 담는다. UTF-8로 표현할 수 없는 이름은 조용히 버리지 않고 `unreadable_name_count`로 남긴다.
- `platform/win32/win32_directory_enumerator.h/.cpp`를 추가했다. `FindFirstFileExW` 기반이며 `\\?\` 확장 접두어로 `MAX_PATH` 초과 경로를 지원한다. 상대 경로는 OS 호출 없이 거부하고, 패턴 불일치(`ERROR_FILE_NOT_FOUND`)는 빈 목록으로, 반복 중간 실패는 실패로 보고한다.
- 새 static library `gitman_discovery`를 추가했다. `gitman_domain`·`gitman_vcs`·`gitman_workspace` PUBLIC, `gitman_win32_platform` PRIVATE이며 실행 파일에는 링크하지 않는다.
- `tests/discovery_domain_tests.cpp`(11개)와 `tests/directory_enumerator_tests.cpp`(8개), `tests/helpers/discovery_test_doubles.*`(fake enumerator)를 추가했다. bare 진부분집합 7종 전수, 충돌·우선순위, 정렬 동률의 양방향, 실제 filesystem의 비ASCII 이름과 실패 경로를 고정한다.
- 전체 CTest가 393에서 **412**로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 412/412 통과했다. `/analyze` 무경고, Debug 3회 반복 통과, `gitman_format_check` 238개 파일 통과.
- 개발 중 formatter 위반 1건(`find_handle` 생성자 초기화 목록)을 formatter 결과 수용으로 해소했다. production 결함은 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-5-d1.md`에 기록했다.

### 영향 요구사항

- REQ-004, REQ-009, REQ-010, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- `S5-D1`은 사용자 검수 대기 상태다.
- 승인 전에는 `S5-D2`의 `discovery_service` 탐색 실행과 junction 통합 test를 작성하지 않는다.

## 2026-08-17 - 단계 5 `S5-P0` 구현 계획 수립

### 사용자 지시

- `S5-P0`을 진행한다. 진행 원칙에 따라 이 지시로 `S4-V1`과 단계 4 전체를 최종 승인한 것으로 기록한다.

### 반영 내용

- `docs/stage-5-plan.md`를 작성했다. 단계 5 탐색과 등록의 범위, 설계 제안, 11개 체크포인트와 test 전략을 담는다.
- 탐색은 **프로세스를 만들지 않는 표식 기반 판정**으로 제안했다. `.git` 디렉터리/파일, `.svn` 디렉터리, bare 휴리스틱(`HEAD`+`objects`+`refs`)으로 종류를 정하고, 정확한 상태 판정은 등록 후 단계 4 provider가 담당한다.
- `docs/requirements.md` 6장이 단계 5로 이관한 링크·worktree·bare 세부 범위의 확정안을 제안했다. linked worktree와 submodule(`.git` 파일)은 후보 허용, bare는 표시 후 제외, reparse point는 판정 없이 제외하되 목록에 표시한다.
- 중복 판정은 단계 2 `project_path_resolver`, 저장은 단계 2 `project_store`와 revision token을 그대로 주입받아 재사용한다. 새 정규화·저장 장치를 만들지 않는다.
- 등록 규칙을 제안했다. id는 디렉터리 이름과 중복 시 숫자 접미사, 경로는 절대 경로, `vcs_hint`는 판정 종류, 부적격 후보가 섞인 선택 목록은 부분 등록 없이 전체 거부한다.
- 새 계약은 깊이 1 열거의 `directory_enumerator` 하나이며 Win32 구현은 새 static library `gitman_discovery`에 둔다. 표식 확인은 단계 4의 `vcs_file_probe`, 취소는 단계 3의 `process_cancellation_token`을 재사용한다.
- 판정이 표식 기반이므로 SVN 경로도 `svn.exe` 없이 통합 검증이 가능하다는 test 전략을 기록했다.
- 체크포인트는 `S5-P0`, `S5-D1`(계약·판정) / `S5-D2`(탐색 실행) / `S5-D3`(선택 등록)의 `CODE`/`TEST`/`FIX` 3분할과 `S5-V1`로 총 11개다.
- `S5-V1` 종료 보고에서 ADR-004 범용 메시지 구조의 단계 6 사전 설계 게이트를 다시 알리도록 계획에 명시했다.
- `docs/plan.md`의 지시 이력과 단계 상태, `docs/requirements.md`의 문서 상태, `docs/handoff.md`의 현재 체크포인트와 진행 원장을 갱신했다.
- production code와 test는 변경하지 않았다.

### 영향 요구사항

- REQ-001, REQ-004, REQ-012, REQ-016
- NFR-005, NFR-006, NFR-009

### 다음 작업 제한

- `S5-P0`은 사용자 계획 검수 대기 상태다. 특히 `docs/stage-5-plan.md` 10.1의 확정 필요 사항 8개에 대한 결정이 필요하다.
- 승인 전에는 `S5-D1-CODE`의 도메인 모델, `directory_enumerator` 계약과 판정 규칙을 작성하지 않는다.
- ADR-004의 범용 메시지 구조는 단계 6 구현 전 별도 설계 승인을 받아야 한다. 이 차단 조건은 그대로 유효하다.

## 2026-08-17 - 단계 4 `S4-V1` 최종 검증

### 사용자 지시

- `S4-D6-TEST`를 승인하고 무결함 `S4-D6-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.

### 반영 내용

- `build/vs2022`를 삭제하고 preset으로 다시 configure한 뒤 전체 검증 matrix를 수행했다.
- VS2022 Debug/Release와 VS2026 Debug의 전체 CTest가 각각 **393/393** 통과했다. VS2022 `/analyze`는 무경고로 통과했다.
- aggregate `gitman_format_check`, `gitman_source_style`, `gitman_assets_checksum`, `git diff --check`와 `git diff --cached --check`가 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- Release install 결과가 `bin/gitman.exe` 단일 파일임을 확인했다. 크기 6,255,616 byte, PE 의존성은 Windows 시스템 DLL 9개뿐이며 VC runtime과 프로젝트 DLL이 없다. 설치본 renderer smoke test 4종이 모두 종료 코드 0이다.
- **7개 저장소 동시 조회 stress를 3회 수행했다.** 실제 `git.exe`로 만든 동기·behind·ahead·dirty·detached·remote 없음·없는 경로 저장소를 4개 스레드에서 조회했고, 회차마다 조회 56회에서 기대값 불일치 0, switch 후보 합계 88로 동일했다. 프로세스 handle은 71로 유지되어 실행별 누수가 없다.
- 계획 9장의 완료 조건 14개를 하나씩 대조해 검증 기록 7장에 결과를 남겼다.
- CTest 수가 단계 3 종료 및 감사 수정 시점의 139개에서 **393개**로 254개 늘었다. 태그별로 `[git]` 136개, `[svn]` 49개, `[switch]` 38개, `[update]` 21개, `[integration]` 29개다.
- 실제 `svn.exe` 실행 경로가 미검증으로 남는다는 사실과 실제 환경 연결 시 확인할 네 가지를 검증 기록 9장에 명시했다.
- 결과를 `docs/verification/2026-08-17-stage-4.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006~REQ-014, REQ-017
- NFR-005~NFR-009, NFR-011

### 다음 작업 제한

- 단계 4는 **사용자 최종 검수 대기** 상태다.
- 승인 전에는 단계 5의 탐색 및 등록을 시작하지 않는다.
- ADR-004의 범용 메시지 구조는 단계 6 구현 전 별도 설계 승인을 받아야 한다. 이 차단 조건은 그대로 유효하다.

## 2026-08-17 - 단계 4 `S4-D6-TEST` switch test 작성

### 사용자 지시

- `S4-D6-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/switch_validation_tests.cpp`를 추가했다. 검증 서비스의 규칙 test 13개다. 프로세스를 만들지 않는 순수 함수라 거부 사유와 그 우선순위를 실제 저장소 없이 전수 단정한다.
- `tests/git_switch_tests.cpp`를 추가했다. 후보 목록 조립과 provider의 후보 조회 및 전환 test 20개다.
- **명령 미생성을 요청 기록으로 직접 단정했다.** 빈 대상과 도구 부재는 0개, 저장소 아님은 1개, 검증 거부는 조회 5개에서 끝나며 어느 경로에도 `switch` 명령이 없다.
- 정상 경로의 명령 수와 순서를 고정했다. Git은 8개(재조회 2 → `remote` → `for-each-ref` → `worktree list` → `switch` → 재조회 2), SVN은 18개다. Git 전환 경로에 `fetch`가 **0개**인 것도 함께 단정한다.
- 후보 목록 규칙을 전수 고정했다. 정렬, 심볼릭 ref 제외, 여러 remote의 같은 이름, stale 표시, `/`가 든 remote 이름, 지워진 remote가 남긴 tracking ref다.
- **local 후보 중복 제거의 양쪽 경우를 모두 단정했다.** remote 후보로 도달할 수 있으면 넣지 않고, upstream이 다른 remote를 가리키면 남긴다. 뒤의 규칙이 없으면 그 branch로 전환할 방법이 사라진다.
- 검증 거부 사유 8종과 그 우선순위를 단정했다. 사유를 하나씩 없애면 다음 사유가 나오는 것으로 순서 자체를 고정했다.
- 작업 트리 위험 판정 5종(dirty, 충돌, **미상**, 진행 중 작업, `index.lock`)을 전수 확인했다.
- tracking branch는 확인 전 거부와 확인 후 승인을 쌍으로 단정하고, **확인했더라도 실제 차단 사유가 우선**하는 것을 함께 고정했다.
- SVN은 URL 형식 14종, 허용 목록, 형식 오류 메시지, 현재 위치, 작업 복사본 상태와 저장소 대조 7종을 다룬다. **대상 값을 못 읽은 것과 값이 다른 것을 구분**하고 현재 값을 모르면 통과시키지 않는 것도 단정했다.
- 전환 실패 분류가 로캘 독립 신호로만 이뤄지는 것을 timeout, 취소, 한국어 미분류 실패로 확인했다.
- `tests/git_integration_tests.cpp`에 실제 Git test 5개를 추가했다. **확인 후 실제로 tracking branch를 만들어 전환하고**, 확인 전에는 branch가 만들어지지 않으며, 실제 `git worktree`가 잡은 branch와 dirty 작업 트리를 거부하는 것을 확인한다.
- 명령 test와 파서 test를 각 파일에 추가했다. `for-each-ref` 형식, `worktree list`, `switch`의 금지 인자 6종, tracking 생성 인자 순서, `svn switch`의 금지 인자 3종과 원격 조회 한도다.
- 전체 CTest가 338에서 **393**으로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 393/393 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D6-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-4-d6-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013, REQ-014
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D6-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D6-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-V1`의 단계 4 최종 검증과 검증 문서를 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D6-CODE` switch 구현

### 사용자 지시

- `S4-D5-TEST`를 승인하고 무결함 `S4-D5-FIX` 생략을 확인한 뒤 `S4-D6-CODE`를 진행한다.

### 반영 내용

- `application/switch_validation_service.h/.cpp`를 추가했다. Git과 SVN의 전환 검증 규칙 전체를 프로세스도 filesystem도 쓰지 않는 순수 함수로 모았다. **검증 실패 시 명령을 만들지 않는다**는 REQ-007 수용 기준이 provider 한 곳에서만 지켜지면 되게 하려는 분리다.
- `infrastructure/git_command_builder.*`에 `for-each-ref`, `worktree list --porcelain`, `switch --no-guess`와 tracking branch 생성 요청을 추가했다. `--discard-changes`, `--merge`, `--force`는 쓰지 않는다.
- `for-each-ref` 형식에 `%(symref)`를 넣어 `refs/remotes/<remote>/HEAD` 같은 심볼릭 항목을 이름 규칙이 아니라 값으로 제외한다. 계획 4.8의 형식에 한 칸을 더한 것이다.
- `switch`와 tracking 생성 모두 `--`로 인자를 끊는다. 호스트 Git 2.52.0에서 `switch`가 `--`를 받아들이는 것과 `--track` 뒤의 완전한 ref가 옵션 값이 아니라 시작 지점으로 해석되는 것을 실측했다.
- `infrastructure/git_status_parser.*`에 `parse_git_reference_list`와 `parse_git_worktree_branches`를 추가했다. ref 이름에는 TAB이 들어갈 수 없어 TAB 구분 형식의 경계가 흔들리지 않는다.
- `build_git_switch_candidates`를 순수 함수로 추가했다. remote 후보를 먼저, local branch를 뒤에 두고, 같은 이름이 여러 remote에 있어도 합치지 않으며 자동으로 고르지 않는다.
- **remote 후보로 도달할 수 있는 local branch는 목록에 두 번 넣지 않는다.** 반대로 upstream이 그 remote와 다른 local branch는 remote 후보로 도달할 수 없으므로 그대로 남긴다. 계획 4.8의 "local-only"를 그대로 읽으면 이런 branch로 전환할 방법이 사라진다.
- 후보를 새로 고칠 remote는 `preferred_remote` → `origin` → 유일한 remote 순서로 고른다. 좁혀지지 않으면 **fetch하지 않고** 이미 받아 둔 tracking ref로 목록을 만든 뒤 `stale`로 알린다. upstream은 현재 branch에 종속된 값이라 후보 조회 기준으로 쓰지 않는다.
- fetch가 실패해도 목록 자체는 만든다. 원격을 새로 고치지 못한 것과 후보를 전혀 알 수 없는 것은 사용자가 할 일이 다르다.
- Git 검증 순서를 정했다. 대상 없음 → remote 미지정 → 저장소 조회 불가 → 이미 대상 → 다른 worktree 사용 중 → 작업 트리 위험 → tracking 충돌 → 확인 요구다. **확인 요구를 가장 뒤에 둔다.** 오류가 아니라 확인 요구이므로 실제 차단 사유가 있으면 그것을 먼저 알려야 한다.
- `switch_candidate`에 `tracking_branch_confirmed`를 추가했다. 후보 조회는 채우지 않고 dialog가 확인을 받은 뒤에만 켠다. 계약에 별도 인자가 없어 확인 여부를 실을 자리가 필요했고, 이 값이 없으면 "확인 후 생성"과 "무조건 생성" 중 하나만 구현할 수 있다.
- upstream이 **없는** local branch는 tracking 충돌로 보지 않는다. 이때 전환은 upstream을 건드리지 않고 그 branch로 옮기기만 한다.
- `switch_rejection::repository_unavailable`을 추가했다. 조회 자체가 안 되는 상태를 `working_tree_unsafe`로 보고하면 카드에 잘못된 사유가 뜬다.
- `switch_to`는 **재조회 → 재검증 → 실행 → 사후 재조회** 순서다. dialog 검증과 실행 사이의 상태 변경을 방어한다. 정상 경로의 명령 수는 Git이 8개, SVN이 16개다.
- 실행 직전에는 fetch하지 않는다. 전환은 이미 받아 둔 ref로만 하며 `--no-guess`가 목록에 없던 대상으로의 암묵 전환을 막는다.
- `infrastructure/svn_*`에 `svn switch`와 URL 대상 원격 `info --show-item`을 추가했다. `--ignore-ancestry`, `--force`, `--accept`는 쓰지 않는다.
- **SVN 후보 조회는 process request를 하나도 만들지 않는다.** 후보가 문서의 `svn_switch_targets`뿐이고 저장소 layout을 자동으로 가정하지 않기 때문이다. 형식을 해석할 수 없는 값은 후보에서 빼고 warning 진단으로 남긴다.
- SVN 검증은 허용 목록·형식·현재 위치·작업 트리를 **네트워크보다 먼저** 본다. 어차피 실패할 전환에 원격을 건드릴 이유가 없다. 계획 4.8의 나열 순서를 바꾼 것이며 판정 결과는 같다.
- 저장소 root와 UUID는 양쪽 값이 모두 있고 같을 때만 통과시킨다. 전환은 되돌리기 어려운 동작이라 확인하지 못한 것을 안전하다고 보지 않는다.
- `tests/`의 "아직 구현하지 않은 동작" test 2개를 **빈 대상은 조회 없이 거부한다**는 test로 좁혔다. 원래 단정은 더 이상 사실이 아니지만 REQ-007 수용 기준은 남겨야 한다. 새 test는 작성하지 않았다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 338/338 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 228개 항목을 확인하고 삭제했다. 실제 Git으로 **확인 후 tracking branch를 실제로 만들어 전환했고**, 확인 전 거부·기존 branch 복귀·재선택 거부·다른 worktree 점유 거부·dirty 거부를 모두 확인했다.
- 결과를 `docs/verification/2026-08-17-stage-4-d6-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013, REQ-014
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D6-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D6-TEST`의 후보 정렬과 거부 사유 test를 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D5-TEST` update test 작성

### 사용자 지시

- `S4-D5-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/git_update_tests.cpp`를 추가했다. 차단 사유 matrix, pull 성공·실패, submodule 경로 test 17개다.
- 차단 사유를 전수 확인했다. 저장소 아님, 충돌, 진행 중 작업, `index.lock`, detached, dirty, **미상**, diverged와 통과 3종(`behind`, `up_to_date`, 원격 미확인)이다.
- 사유가 여럿일 때 하나씩 사라질 때마다 다음 사유가 나오는 것으로 **우선순위 자체를 고정**했다.
- provider 층의 모든 차단 경로에서 **`pull` 명령이 0개**임을 요청 기록으로 직접 단정했다.
- 실행 경로는 명령 수와 순서로 고정했다. 기본 6개(조회 2 → remote → pull → 재조회 2), submodule 옵션 시 8개(조사와 갱신이 pull 앞뒤로 들어감)다.
- 실패해도 사후 재조회를 수행하는 것, timeout이 차단이 아니라 실패인 것, 인증 실패가 한국어 메시지에서도 분류되는 것을 단정했다.
- `tests/git_integration_tests.cpp`에 실제 Git test 4개를 추가했다. 뒤처진 저장소를 **실제로 fast-forward**하고, **원격 이력이 다시 쓰인 저장소에서 `pull --ff-only`가 merge를 만들지 않고 실패**하며, dirty·diverged·detached·remote 없음이 차단되고, 실제 submodule이 off/on에 따라 다르게 처리되는 것을 확인한다.
- 이력 재작성 test가 이 구간의 핵심이다. 사전 검사만으로 걸러지지 않는 경우에 `--ff-only`가 마지막 방어선으로 동작하는 것을 실제 Git으로 보여 준다.
- `tests/svn_repository_provider_tests.cpp`에 SVN 사전 검사와 update test 6개를 추가했다. 판정할 수 없는 switched·mixed가 update를 막지 않는 것도 함께 고정했다.
- 명령 test와 `submodule status` 파서 test를 각 파일에 추가했다.
- `git_repository_fixture`의 준비 명령에 `-c protocol.file.allow=always`를 추가했다. 임시 디렉터리의 로컬 경로를 submodule 원본으로 쓰기 위한 것이며 **production 명령은 이 설정을 만들지 않는다.**
- 전체 CTest가 306에서 **338**로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 338/338 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D5-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-4-d5-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D5-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D5-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D6-CODE`의 switch 후보와 검증을 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D5-CODE` update 구현

### 사용자 지시

- `S4-D4-TEST`를 승인하고 무결함 `S4-D4-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.

### 반영 내용

- `infrastructure/git_command_builder.*`에 `pull --ff-only`, `submodule status --recursive`, `submodule update --init --recursive` 요청을 추가했다. `--force`, `--rebase`, `--autostash`는 쓰지 않는다.
- pull은 remote와 branch를 명시하고 앞에 `--`를 둔다. 설정에 따라 다른 대상이 당겨지지 않고 `-`로 시작하는 이름이 인자로 해석되지 않는다. 호스트 Git 2.52.0으로 실측했다.
- `infrastructure/git_status_parser.*`에 `parse_git_submodule_status`를 추가했다. `<표시><커밋 ID> <경로> (<describe>)` 형식을 읽고 describe 접미사를 떼어 낸다.
- `evaluate_git_update_preflight`와 `evaluate_svn_update_preflight`를 순수 함수로 추가했다. 보호 정책 자체를 프로세스 없이 검증할 수 있다.
- **사전 검사는 지금 다시 조회한 상태로 한다.** 카드가 들고 있는 값으로 판단하면 그 사이에 바뀐 저장소에서 명령이 나갈 수 있다.
- 차단 사유 우선순위를 정했다. 도구 부재 → 저장소 아님 → 충돌 → 진행 중 작업 → `index.lock` → detached → dirty → diverged → 대상 없음이다. `working_tree_state::unknown`을 dirty와 함께 막는다.
- **SVN의 switched·mixed는 값이 있을 때만 차단한다.** `svnversion`이 없어 판정할 수 없다는 이유로 update를 영영 막으면 도구 구성 문제 하나로 기능이 사라진다. 조회가 이미 warning을 남긴다.
- submodule 옵션이 켜지면 pull 전에 `submodule status --recursive`로 조사하고 충돌(`U`)이나 커밋 불일치(`+`)가 하나라도 있으면 **parent pull을 시작하지 않는다.** 미초기화(`-`)는 `--init`이 처리하므로 위험으로 보지 않는다.
- `submodule update --init --recursive`는 parent pull이 **성공한 경우에만** 실행한다. 실패한 pull 뒤에 submodule을 옮기면 되돌리기 어려운 조합이 남는다.
- 계획 4.7의 "submodule dirty 검사"는 `git submodule status`가 내부 dirty를 보고하지 않아 충돌과 커밋 불일치로 좁혔다. 내부 dirty 검사는 단계 6~7에서 다시 본다.
- 성공과 실패 모두 실행 직후 로컬 상태를 다시 조회한다. 실행의 성공 여부와 조회 결과를 분리해 보고한다.
- `infrastructure/svn_*`에 `svn update`를 추가했다. `--accept`를 주지 않아 충돌을 자동으로 해결하지 않는다.
- `tests/`의 "아직 구현하지 않은 동작" test 2개에서 `update` 단정만 걷어냈다. 이번 구간이 구현한 동작이라 더 이상 사실이 아니다. 새 test는 작성하지 않았다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 306/306 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 109개 항목을 확인하고 삭제했다. 실제 Git으로 **원격이 앞선 저장소를 실제로 fast-forward했고** dirty·diverged·remote 없음·detached 저장소가 모두 차단되는 것을 확인했다.
- 결과를 `docs/verification/2026-08-17-stage-4-d5-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-013
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D5-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D5-TEST`의 차단 사유 matrix와 update 통합 test를 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D4-TEST` SVN 조회 test 작성

### 사용자 지시

- `S4-D4-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/svn_command_builder_tests.cpp`에 명령 test 6개를 추가했다. 네 명령의 인자와 한도, `--non-interactive` 위치, **`svnversion`에 공통 인자가 없음**, `--trust-server-cert`와 자격 증명 인자를 절대 만들지 않는 것을 단정한다.
- `tests/svn_output_parser_tests.cpp`에 파서 test 8개를 추가했다. 상태 문자 12종, switched·tree conflict 칸, 부가 설명 줄 무시, 무시·외부 항목 제외, `svnversion` 단일·범위·`M`/`S`/`P`·비작업복사본·잘못된 형식을 다룬다.
- 상태 칸 뒤 패딩이 하나 더 있어도 경로가 잘리지 않는 것을 test로 고정했다. 계획의 "고정 9칸" 대신 공백을 건너뛰는 구현의 근거다.
- `tests/fixtures/vcs/svn/`에 fixture 2개를 추가했다. 실제 출력을 캡처할 수 없어 **Apache Subversion 공식 문서의 출력 계약을 근거로 작성하고 출처와 미대조 사실을 파일 주석에 남겼다.** 주석은 `#`로 시작하며 test 도우미가 버린다.
- `tests/svn_repository_provider_tests.cpp`에 provider test 16개를 추가했다. 정상 조회의 명령 7개 순서, `svnversion` 부재와 해석 실패, `status` 실패, 원격 behind·up_to_date, 실패 분류 3종과 미구현 동작을 다루며 **명령 미생성을 요청 기록 수로 직접 단정한다.**
- 실패 분류 test는 모두 한국어 메시지에 SVN 오류 코드가 붙은 형태다. 번역된 메시지에도 코드가 남는다는 전제를 언어에 의존하지 않고 단정한다.
- `tests/svn_integration_tests.cpp`에 통합 test 2개를 추가했다. SVN이 없는 호스트에서 앱이 계속 동작하는 것은 **실제로 실행되고**, 실제 `svn.exe`를 쓰는 test는 skip된다. 두 test는 서로 배타적이라 SVN이 설치된 호스트에서는 반대로 동작한다.
- 실제 작업 복사본을 만들려면 `svnadmin`이 필요하다. 단계 4는 실행 파일 연결과 판정까지만 확인하고 실제 작업 복사본 통합 검증은 단계 8로 남긴다.
- 전체 CTest가 274에서 **306**으로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 306/306 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D4-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-17-stage-4-d4-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-011, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D4-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D4-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D5-CODE`의 update 보호 정책과 실행을 작성하지 않는다.

## 2026-08-17 - 단계 4 `S4-D4-CODE` SVN 조회 구현

### 사용자 지시

- `S4-D3-TEST`를 승인하고 무결함 `S4-D3-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.
- SVN은 실제 개발 시점에 쓰지 않는다. 나중에 프로덕션 환경에 **부품 끼워넣듯 최소 노력으로 적용할 수 있기만 하면 된다.**

### 반영 내용

- `infrastructure/svn_command_builder.*`에 `info --show-item`, 비verbose `status`, `svnversion`, 원격 리비전 요청을 추가했다.
- **`svnversion`에는 공통 인자를 붙이지 않는다.** `svn`과 다른 실행 파일이라 `--non-interactive`를 받지 않고, 그대로 붙이면 인자 오류로 실패한다. timeout과 인코딩 정책은 다른 명령과 같게 맞췄다.
- `infrastructure/svn_output_parser.*`에 값 한 줄 추출, 고정 칸 `status` 파서, `svnversion` 파서와 작업 트리 요약을 추가했다.
- `status` 경로는 앞 7칸(항목·속성·잠금·이력·switched·잠금 토큰·tree conflict)을 상태 칸으로 보고 그 뒤 공백을 모두 건너뛴 지점부터 읽는다. 계획의 "고정 9칸"보다 배포판별 패딩 차이에 강하다.
- 상태 칸이 모두 공백인 줄(`> moved from ...`)은 항목이 아니므로 건너뛴다. `I`(무시)와 `X`(외부 항목)는 어느 수에도 넣지 않는다.
- `infrastructure/svn_repository_provider.*`에 로컬 및 원격 조회를 구현했다. 구조와 실패 처리를 Git provider와 똑같이 맞춰 나중에 붙일 때 읽어야 할 새 개념이 없게 했다.
- mixed revision과 switched는 `svnversion`으로 판정한다. `svnversion`이 없거나 출력을 해석하지 못하면 조회를 막지 않고 `has_mixed_revision`을 비운 채 `status`의 switched 칸으로 보조 판정한다.
- 원격 조회는 `info --show-item url`로 현재 URL을 다시 물어본 뒤 원격 HEAD 리비전과 비교한다. SVN에는 `ahead`와 `diverged`가 없어 `behind`와 `up_to_date`만 나오고 `ahead_count`는 항상 0이다.
- 실패는 `S4-D1-CODE`의 분류기가 SVN `E<숫자>` 코드로 판정한다. 번역된 메시지에도 코드가 붙어 로캘에 의존하지 않는다.
- SVN이 없는 환경은 계속 정상 상태다. 도구 부재는 warning이고 어떤 명령도 만들지 않는다. 이 경로는 이 호스트에서 실제로 확인했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 274/274 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 115개 항목을 확인하고 삭제했다. 그중 실제 실행으로 확인한 SVN 경로는 미설치 감지뿐이다.
- 결과를 `docs/verification/2026-08-17-stage-4-d4-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-011, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D4-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D4-TEST`의 SVN fixture와 파서 test를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D3-TEST` remote-first 판정 test 작성

### 사용자 지시

- `S4-D3-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/git_remote_query_tests.cpp`를 추가했다. 대상 선택 matrix, 명령 순서, 실패 분류와 값 보존 test 16개다.
- 대상 선택은 upstream, `/`가 든 branch 이름, 가장 긴 remote 접두사, local branch upstream, preferred, preferred 부재, origin, 유일 remote, 모호, remote 없음, detached, branch 미상을 모두 단정한다.
- **네트워크를 쓰기 전에 끝나는 경로**를 요청 수로 직접 단정했다. 도구 부재·로컬 미준비·경로 소멸·detached는 0개, `local_only`와 모호는 1개, fetch 실패는 2개, ref 부재와 커밋 없음은 3개다.
- 실패 분류는 같은 실패의 영어 출력과 한국어 출력이 같은 분류를 내는지 쌍으로 단정한다. libcurl, OpenSSH, HTTP 상태와 미분류 실패, timeout, 취소를 다룬다.
- fetch가 실패해도 직전 로컬 비교, ahead 수, 작업 트리 상태와 이전에 성공한 `remote_checked_at`이 남는 것을 단정했다. 반대로 `remote_target_missing`에서는 비교 값을 지우는 것도 단정했다.
- `tests/git_integration_tests.cpp`에 실제 원격 비교 test 6개를 추가했다. 동기, ahead, behind, diverged, remote 없음, 원격 branch 부재, 도달 불가 URL과 비ASCII 왕복이다. 로컬 bare 저장소만 쓰며 네트워크에 접근하지 않는다.
- `behind`와 `diverged`는 원격을 건드리지 않고 clone을 `reset --hard HEAD~1`로 되돌려 만든다. 준비가 결정적이다.
- `tests/git_command_builder_tests.cpp`에 새 명령 4종의 인자와 한도 test를, `tests/git_status_parser_tests.cpp`에 remote 이름과 ahead/behind 파서 test를 추가했다.
- **한국어 Git 출력 인코딩을 실측해 기록했다.** 이 호스트의 시스템 ANSI code page는 949지만 Git for Windows 2.52.0에는 번역 catalog가 설치되어 있지 않아(`share/locale` 부재) `LANGUAGE`, `LC_ALL`, `LANG`을 어떻게 줘도 메시지가 영어다. Git이 되돌려 주는 비ASCII 내용은 UTF-8이라 `active_code_page_fallback`이 건드리지 않는다. 다른 호스트에는 번역본이 있을 수 있으므로 오류 분류는 계속 로캘 독립 신호만 쓴다.
- 전체 CTest가 246에서 **274**로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 274/274 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D3-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-4-d3-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009~REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D3-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D3-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D4-CODE`의 SVN 명령 조립과 파서를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D3-CODE` Git remote-first 판정 구현

### 사용자 지시

- `S4-D2-TEST`를 승인하고 무결함 `S4-D2-FIX` 생략을 확인한 뒤 다음 구간을 진행한다.

### 반영 내용

- `infrastructure/git_command_builder.*`에 `remote`, `fetch --prune`, `rev-parse --verify --quiet`, `rev-list --left-right --count` 요청을 추가했다.
- `fetch`에는 `--`를 붙여 remote 이름이 옵션으로 해석되지 않게 했다. 반대로 `rev-parse`에는 `--`를 쓰지 않는다. 뒤의 값을 경로로 만들어 항상 실패하기 때문이며 호스트 Git 2.52.0으로 실측해 확정했다.
- `infrastructure/git_status_parser.*`에 `parse_git_remote_names`와 `parse_git_ahead_behind`를 추가했다.
- `infrastructure/git_repository_provider.*`에 `select_git_remote_target` 순수 함수와 `query_remote` 본문을 구현했다. 선택 순서는 ADR-003대로 upstream → `preferred_remote` → `origin` → 유일한 remote이며, 좁혀지지 않으면 **자동으로 고르지 않고** `remote_target_missing`으로 보고한다.
- upstream에서 remote 이름을 뗄 때 설정된 remote 중 가장 긴 접두사를 고른다. branch 이름에도 `/`가 들어갈 수 있어 첫 `/`로 자르면 `origin/feature/a/b`를 잘못 나눈다.
- `branch.<name>.remote = .`처럼 upstream이 local branch를 가리키면 원격 비교에 쓰지 않고 나머지 규칙으로 넘어간다.
- 지정한 `preferred_remote`가 저장소에 없으면 다음 규칙으로 진행하되 warning 진단을 남긴다. 지정한 값이 조용히 무시되면 사용자가 알 수 없다.
- detached HEAD와 remote가 없는 저장소에서는 **네트워크를 쓰지 않는다**. 각각 `remote_target_missing`과 `local_only`다.
- remote branch 존재 확인을 fetch **뒤**에 한다. 한 번도 fetch하지 않은 저장소에는 tracking ref가 없어 fetch 전에 확인하면 원격에 있는 branch를 없다고 오판한다. 계획 4.5의 4·5번 순서를 바꾼 것이며 판정 결과는 같다.
- fetch 실패는 로캘 독립 신호로 `offline`, `authentication_required`, `error`를 구분한다. 실패해도 작업 트리 상태, 마지막 성공 원격 확인 시각과 직전 로컬 비교 값을 지우지 않는다.
- 비교 대상 자체가 없다고 판정한 경우에는 이전 비교 값을 지운다. 유효하지 않은 비교를 남기면 카드가 잘못된 수를 계속 보여 준다.
- 커밋이 하나도 없는 저장소는 `HEAD`가 없어 대칭 차이를 계산할 수 없다. fetch와 ref 확인까지만 하고 `sync_state`를 `unknown`으로 두며 값을 추측하지 않는다.
- `tests/git_repository_provider_tests.cpp`의 "아직 구현하지 않은 동작" test에서 `query_remote` 단정만 걷어냈다. 이번 구간이 구현한 동작이라 더 이상 사실이 아니다. 새 test는 작성하지 않았다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 246/246 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 124개 항목을 확인하고 삭제했다. 그중 11개는 실제 `git.exe`와 임시 저장소 7종(동기, ahead, behind, diverged, remote 없음, 원격 branch 없음, 도달 불가 URL)을 사용했다.
- 도달 불가 URL의 실제 fetch 실패가 libcurl 영어 문장 덕분에 로캘과 무관하게 `offline`으로 분류되는 것을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-4-d3-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009~REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D3-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D3-TEST`의 대상 선택 matrix와 원격 통합 test를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D2-TEST` Git 로컬 조회 test 작성

### 사용자 지시

- `S4-D2-CODE`를 승인하고 다음 구간을 진행한다.

### 반영 내용

- `tests/git_command_builder_tests.cpp`에 명령 조립 test 4개를 추가했다. `rev-parse` 인자 순서, `status` 인자, **어떤 인자에도 `-z`가 없음**, `status`만 큰 레코드 상한을 쓰는 것과 단계 3 요청 검증 통과를 단정한다.
- `tests/git_status_parser_tests.cpp`에 파서 test 15개를 추가했다. 인용 해제 이스케이프 전종, 배치 파서, 레코드 종류별 파싱, 알 수 없는 헤더 무시, 잘못된 `branch.ab` 4종, 해석 실패 시 `unknown` 상태를 단정한다.
- `tests/fixtures/vcs/git/`에 호스트 Git 2.52.0이 실제로 낸 `status --porcelain=v2 --branch` 출력 5종을 그대로 저장했다. rename fixture는 공백·한글·emoji가 든 경로와 TAB 구분자를 담는다.
- 개행이 든 경로가 인용 덕분에 한 레코드로 남는 것을 test로 고정했다. `-z`를 쓰지 않기로 한 결정의 근거다.
- `tests/git_repository_provider_tests.cpp`에 provider test 20개를 추가했다. 도구 부재, 상대 경로, 사라진 경로, 저장소 아님, bare, git dir 안, timeout, 취소, status 실패, 해석 실패와 미구현 동작에서 **요청 기록 수로 명령 미생성을 직접 단정**한다.
- 저장소 아님 판정 test에 한국어 Git 메시지를 넣어 분류가 메시지 본문이 아니라 구조적 신호로 이뤄지는 것을 언어 독립으로 단정했다.
- `tests/helpers/git_repository_fixture.h/.cpp`를 추가했다. 실제 `git.exe`로 임시 저장소를 만들고 소멸자에서 반드시 지운다. `HOME`, `GIT_CONFIG_GLOBAL`, `GIT_CONFIG_NOSYSTEM`과 커밋 저자·시각을 고정해 호스트 설정과 분리한다.
- `tests/git_integration_tests.cpp`에 통합 test 12개를 추가했다. 정상, ahead, dirty, 충돌, 중단된 rebase, detached, 커밋 없음, bare, linked worktree, 한글·emoji 경로, 비저장소와 없는 경로, 도달 불가 remote를 실제 Git으로 확인한다. Git이 없으면 skip한다.
- 도달 불가 remote를 등록한 저장소로 로컬 조회가 네트워크에 접근하지 않는 것을 실제 실행으로 확인했다.
- 전체 CTest가 195에서 **246**으로 늘었고 VS2022 Debug/Release와 VS2026 Debug에서 각각 246/246 통과했다. `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- `--untracked-files=normal`이 미추적 디렉터리를 항목 하나로 접어 보고한다는 사실을 기록했다. `untracked_count`는 파일 수가 아니라 Git이 보고한 항목 수다.
- production source를 변경하지 않았고 `S4-D2-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-4-d2-test.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009, REQ-010, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D2-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D2-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D3-CODE`의 remote 열거와 fetch를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D2-CODE` Git 로컬 조회 구현

### 사용자 지시

- `S4-D2-CODE`를 진행한다.

### 반영 내용

- `infrastructure/git_command_builder.*`에 `rev-parse` 배치 조회와 `status --porcelain=v2` 요청 조립을 추가했다.
- `rev-parse` 인자 순서를 `--absolute-git-dir --is-bare-repository --is-inside-work-tree --show-toplevel`로 고정했다. 마지막 인자는 bare 저장소에서 실패하지만 앞의 값은 이미 출력되므로 실패한 경우에도 배치를 판정할 수 있다.
- **`status`에 `-z`를 쓰지 않기로 확정했다.** 단계 3 파이프라인이 줄 끝 문자를 남기지 않아 NUL 구분 출력은 경계 정보를 잃고, 개행이 든 경로가 오히려 손상된다. 줄 단위 출력에서는 Git이 그런 경로를 C 인용으로 감싸므로 경계가 흔들리지 않는다. 계획 4.10이 이 구간에서 정하라고 남긴 항목이다.
- `infrastructure/git_status_parser.*`에 배치 파서, porcelain v2 파서, 작업 트리 요약과 C 인용 경로 해제(`unquote_git_path`)를 추가했다.
- 해석하지 못한 레코드가 있거나 branch 헤더를 찾지 못하면 작업 트리 상태를 `unknown`으로 둔다. 출력을 다 읽지 못한 저장소를 깨끗하다고 보고하면 보호 정책이 무력해진다.
- `infrastructure/git_repository_provider.*`에 진행 중 작업 표식 probe와 `query_local`의 snapshot 변환을 추가했다. `index.lock`은 중단된 작업과 구분해 따로 보고한다.
- `domain/repository_snapshot.*`에 `repository_availability::unsupported_layout`을 추가했다. bare 저장소와 git dir 안의 경로를 `not_a_repository`로 보고하면 카드에 잘못된 사유가 뜬다. 계획 11장이 이 구간에서 정하라고 남긴 항목이다.
- linked worktree는 추가 처리 없이 조회된다. git dir이 worktree 전용 디렉터리이고 진행 중 작업 표식도 그곳에 있다.
- 저장소 아님 판정을 번역되는 `fatal: not a git repository` 문장 대신 "정상 종료했는데 출력이 없다"는 구조적 신호로 한다. 로캘 독립 원칙을 따른다.
- 명령을 만들기 전에 등록 경로의 절대 경로 여부와 디렉터리 존재를 확인해 `path_unavailable`을 먼저 판정한다.
- 로컬 조회가 `branch.ab`로 `sync_state`를 채우되 근거를 `comparison_source::local`로 남긴다. 원격을 실제로 확인하는 remote-first 판정은 `S4-D3`이 덮어쓴다.
- `make_vcs_process_request`에 `maximum_record_bytes` 기본 인자를 추가하고 `status`에만 64 KiB를 준다. rename 레코드가 기본 8 KiB를 넘겨 줄이 끊기면 파서가 다른 레코드로 오해한다.
- `git_repository_provider`가 `repository_provider` 계약을 구현한다. 아직 구현하지 않은 원격 조회, switch 후보, update와 switch는 어떤 process request도 만들지 않고 중립 값을 돌려준다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 195/195 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 177개 항목을 확인하고 삭제했다. 그중 19개는 실제 `git.exe`와 임시 저장소 6종(dirty, 충돌, bare, linked worktree, 커밋 없음, 비저장소)을 사용했다.
- 새 test source를 작성하지 않았다. 결과를 `docs/verification/2026-08-16-stage-4-d2-code.md`에 기록했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-009, REQ-010, REQ-012
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D2-CODE`는 사용자 코드 검수 대기 상태다.
- 승인 전에는 `S4-D2-TEST`의 파서 test와 Git 통합 fixture를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D1-TEST` 계약 test 작성

### 사용자 지시

- `S4-D1-CODE`를 승인하고 다음 단계를 진행한다.

### 반영 내용

- `tests/helpers/vcs_test_doubles.h/.cpp`에 요청을 기록하는 `fake_process_runner`와 등록한 경로만 존재하는 `fake_vcs_file_probe`를 추가했다. 계획의 `fake_process_runner.*` 대신 probe 대역까지 담는 이름으로 정했다.
- `tests/vcs_version_tests.cpp`에 Git과 SVN banner 파싱, patch 생략 표기, 첫 줄 계약과 최소 버전 경계 test 5개를 추가했다.
- `tests/vcs_tool_discovery_tests.cpp`에 `PATH` 분해, 후보 순서, 도구 조사, 버전 미달과 판독 실패, 지정 경로 정책과 registry test 13개를 추가했다.
- `tests/vcs_error_classifier_tests.cpp`에 로캘 독립 분류 test 12개를 추가했다. 같은 실패의 영어 출력과 한국어 출력이 같은 분류를 내는지 쌍으로 단정한다.
- HTTP 상태 오탐 방지를 `issue-403`, `branch 404`, `error: 4031`, `error: 1401`로 고정했다.
- 프로세스 결과가 stderr 검사보다 우선한다는 것과 취소가 마지막 판정을 오류로 덮지 않는다는 것을 확인했다.
- `tests/vcs_execution_policy_tests.cpp`에 명령 부류별 한도, 비대화형 환경 override, 공통 인자 순서와 단계 3 요청 검증 통과 test 8개를 추가했다.
- `LC_ALL`, `LANG`, `LANGUAGE` override가 없고 모든 명령이 `active_code_page_fallback`을 쓰는 것을 test로 고정했다.
- `tests/vcs_domain_tests.cpp`에 도구 값, 버전 비교, 작업 트리 안전성, snapshot 기본값과 switch 및 update 열거형 test 11개를 추가했다.
- **모든 VCS가 없는 환경**을 전용 test로 고정했다. 두 도구 모두 `not_found`, 프로세스를 하나도 만들지 않음, 진단이 모두 warning, `none_available()` 참을 단정한다.
- 한쪽만 없는 구성에서 나머지 도구가 계속 동작하는 것과, 조사 전 registry가 경고를 내지 않는 것도 확인했다.
- `tests/project_schema_tests.cpp`에 `settings` parse test 3개와 `workspace-settings.version-list` fixture를 추가했다. 기존 fixture가 그대로 열리는 회귀도 함께 단정한다.
- `tests/json_project_store_tests.cpp`에 `settings` 저장 test 4개를 추가했다. 알 수 없는 키 보존, 기본값 문서에 필드를 만들지 않음, 값 생성 시 기록, 상대 경로 저장 거부를 확인한다.
- `tests/domain_model_tests.cpp`에 새 diagnostic code 11개, `authentication_required` 이름과 `workspace_settings` 기본값 단정을 추가했다.
- `gitman_tests`에 `gitman_vcs` 링크와 `${GITMAN_TEST_DIRECTORY}` include 경로를 추가했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 195/195 통과했고 `/analyze`도 무경고로 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- production source를 변경하지 않았고 `S4-D1-FIX` 후보도 발견하지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-4-d1-test.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-002, REQ-006, REQ-007, REQ-009~REQ-013, REQ-017
- NFR-005~NFR-008

### 다음 작업 제한

- `S4-D1-TEST`는 사용자 test 검수 대기 상태다.
- 발견 production 결함이 없어 `S4-D1-FIX`는 사용자 확인 후 생략한다.
- 승인 전에는 `S4-D2-CODE`의 Git 명령 조립과 출력 파서를 작성하지 않는다.

## 2026-08-16 - 단계 4 `S4-D1-CODE` 계약과 도구 발견 구현

### 사용자 지시

- 개정한 `S4-P0` 계획을 승인하고 코드 작성을 진행한다.
- 환경에 따라 Git이 없을 수도, SVN이 없을 수도 있다. 통합 사용 환경을 제공하는 것이 목표이므로 **모든 VCS가 없는 경우도 상정**해야 한다.

### 반영 내용

- `domain/vcs_tool.*`에 도구 가용성, 버전 값과 `vcs_tool_set`을 추가했다. `none_available()`로 Git과 SVN이 모두 없는 환경을 1급 상태로 표현한다.
- `domain/repository_snapshot.*`에 `repository_availability`를 추가해 도구 부재가 오류가 아니라 상태로 표현되게 했다. 카드는 목록에 남고 동작만 비활성화된다.
- 도구 부재 진단의 severity를 warning으로 정했다. 앱을 멈추지 않고 다른 VCS와 프로젝트 목록은 계속 사용할 수 있다.
- `repository_provider` 계약에 `available()`을 넣어 호출자가 조회와 변경을 시도하기 전에 걸러낼 수 있게 했다.
- `remote_sync_state::authentication_required`와 작업 트리의 진행 중 작업, `index.lock`, detached 표시를 추가했다.
- `working_tree_summary::is_safe_for_change()`를 추가했다. `unknown` 상태도 안전으로 보지 않는다.
- `submodule_status`, SVN 저장소 root 및 UUID, `std::optional<bool>` switched 및 mixed revision 필드를 추가했다.
- `domain/vcs_operation.*`에 switch 후보, 거부 사유 12종, update 차단 사유 13종과 각각의 한국어 메시지를 추가했다.
- `domain/diagnostic.*`에 VCS 관련 code 11개와 이름 매핑을 추가했다.
- `is_absolute_windows_path`를 `application/process_request`에서 `domain/path_syntax`로 옮겼다. 문서 `settings` 검증과 프로세스 요청 검증이 같은 규칙을 쓰게 하려는 이동이며, `process_request.h`가 새 헤더를 include해 기존 호출자는 변경 없이 컴파일된다.
- `.version-list`에 optional `settings` object를 추가했다. 스키마 버전 1을 유지하고, 없으면 진단 없이 기본값이며, 절대 경로가 아닌 값은 `vcs_tool_path_invalid` 오류로 보고한다.
- 저장 시 기존 `settings` object를 template으로 삼아 알 수 없는 키를 보존하고, 문서에 없었고 값도 기본값이면 필드를 만들지 않는다.
- `application/vcs_tool_registry.*`를 값 container로 두고 탐색 로직은 infrastructure에 두어 application이 infrastructure를 참조하지 않게 했다.
- `application/vcs_file_probe.h`로 git dir 표식 파일 확인을 계약화했다. Git에 진행 중 작업을 알려 주는 기계 판독 명령이 없기 때문이다.
- `infrastructure/vcs_tool_discovery.*`에 `settings` → `PATH` → 기본 설치 위치 순서의 탐색을 구현했다. 후보 생성은 filesystem을 보지 않는 순수 함수다.
- `PATH` 분해에서 따옴표를 벗기고 빈 항목과 상대 경로 항목을 버리며 ASCII 대소문자를 무시하고 중복을 제거한다.
- 지정 경로가 상대 경로거나 없거나 `--version`이 실패하면 `path_invalid`로 보고하고 자동 탐색으로 물러서지 않게 했다.
- `svnversion.exe`를 `svn.exe`와 같은 디렉터리에서 찾아 보조 도구로 담고, 없어도 조회를 막지 않게 했다.
- `infrastructure/vcs_version.*`에 접두어에 의존하지 않는 버전 파서를 구현했다. `git version 2.52.0.windows.1`과 `svn, version 1.14.5 (r1922182)`를 모두 처리한다.
- `infrastructure/vcs_execution_policy.*`에 명령 부류별 timeout 및 캡처 상한과 Git 비대화형 환경 override 7종, 공통 인자 4종, SVN `--non-interactive`를 모았다.
- 로캘을 강제하지 않고 모든 명령에서 `active_code_page_fallback` 인코딩을 쓰도록 했다.
- `infrastructure/vcs_error_classifier.*`를 로캘 독립 신호(SVN 오류 코드, libcurl 및 OpenSSH 원문, HTTP 상태 번호)만으로 분류하도록 구현했다. HTTP 상태는 독립 토큰이면서 `http` 문맥이 있을 때만 인정해 오탐을 막는다.
- 어느 신호에도 맞지 않는 실패는 추측하지 않고 `command_failed`로 보고하게 했다.
- `infrastructure/vcs_command_runner.*`로 stdout과 stderr를 분리 수집하면서 호출자 sink에도 전달하게 했다.
- `platform/win32/win32_vcs_file_probe.*`에 파일 존재 확인과 환경 변수 읽기를 구현했다. 계층 방향을 지키려고 target은 `gitman_vcs`에 두었다.
- 새 static library `gitman_vcs`를 추가하고 `gitman_domain`과 `gitman_process`를 PUBLIC, `gitman_win32_platform`을 PRIVATE으로 링크했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 139/139 통과했고 `/analyze`도 무경고로 통과했다.
- 저장소 밖 임시 프로그램으로 69개 항목을 확인하고 삭제했다. 한국어 Git 출력에서도 libcurl 문자열 덕분에 `offline` 분류가 유지되는 것과, 빈 `PATH`에서 두 도구 모두 `not_found`이면서 진단이 warning뿐인 것을 확인했다.
- 임시 프로그램의 기대값 오류 2건을 발견해 고쳤다. production 결함은 아니며 `S4-D1-TEST`에서 test로 고정한다.
- 결과를 `docs/verification/2026-08-16-stage-4-d1-code.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-002, REQ-006, REQ-007, REQ-009~REQ-013, REQ-017
- NFR-005, NFR-007, NFR-008, NFR-009

### 다음 작업 제한

- `S4-D1-CODE` 검수 전에는 `S4-D1-TEST`의 test source, fixture와 `gitman_tests` 링크를 추가하지 않는다.
- Git 명령 조립과 출력 파서는 `S4-D2-CODE` 승인 후에만 작성한다.
- test에서 production 결함이 발견되어도 `S4-D1-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-16 - 단계 4 `S4-P0` 1차 검수 결정 반영

### 사용자 지시

- `remote_sync_state`에 `authentication_required`를 제안대로 추가한다.
- 로캘은 강제하지 말고 시스템 로캘에 맞춘다. 이 프로젝트는 한국어 기반이므로 한국어 출력이 나오며, stderr를 표시하는 로그 뷰를 앱이 직접 제공하므로 인코딩 문제는 앱이 감당한다.
- SVN CLI는 설치하지 않는다. CLI가 있다고 가정하고 SVN 명령만 연결한다. XML 처리가 꼭 필요한지 재검토한다.
- Git 및 SVN 경로의 수동 입력을 추후 환경설정에서 제어할 수 있게 한다. 환경설정 값은 프로젝트 파일의 `settings` 속성에 둔다.

### 반영 내용

- SVN에서 XML을 사용하지 않기로 확정했다. `info --show-item`(값 한 줄), 비verbose `status`(고정 9칸 + 경로), `svnversion`(`4123:4168MS` 형태) 조합으로 필요한 값을 모두 얻는다.
- `status --verbose`는 작성자 컬럼 때문에 공백 포함 경로에서 경계가 모호해지므로 사용하지 않기로 하고, mixed revision 판정을 `svnversion`으로 옮겼다.
- 원격 대비 상태를 로캘 의존 요약 줄 대신 원격 URL의 `--show-item revision`과 작업 복사본 리비전 비교로 판정하게 했다.
- XML 파서 dependency를 추가하지 않으므로 `vcpkg.json`과 ADR-002는 변경하지 않는다.
- `LC_ALL=C` 강제를 계획에서 제거하고 시스템 로캘을 따르도록 바꿨다.
- 로캘을 강제하지 않으면 번역된 메시지로 오류를 분류할 수 없으므로, 분류 근거를 SVN `E<숫자>` 코드, libcurl 및 OpenSSH 원문 문자열, HTTP 상태 번호, 프로세스 완료 사유 같은 로캘 독립 신호로 다시 설계했다.
- 어떤 신호에도 맞지 않는 실패는 추측하지 않고 `error`로 보고하도록 정했다.
- 오류 분류 test가 같은 오류의 영어 출력과 한국어 출력에서 같은 분류를 내는지 단정하도록 test 계획을 보강했다.
- 인코딩 모드를 Git 포함 모든 명령에서 `active_code_page_fallback`으로 통일했다. 단계 3에서 이미 구현하고 검증한 경로다.
- `remote_sync_state`에 `authentication_required`를 추가하기로 확정하고 `docs/plan.md` 3.2의 상태 표와 Codicon 표에 `key` 아이콘 및 “인증 필요”를 반영했다. `offline` 설명에서 인증 실패를 분리했다.
- 프로젝트 문서에 optional `settings` 속성을 도입하는 설계를 계획 4.11에 추가했다. 스키마 버전은 1을 유지하고, 없으면 자동 탐색 기본값이며, 알 수 없는 키까지 round-trip 보존한다.
- 도구 탐색 순서를 `settings` 수동 지정 → PATH → 기본 설치 경로로 바꾸고, 지정 경로가 잘못되면 자동 탐색으로 물러서지 않고 `vcs_tool_path_invalid`로 보고하도록 정했다.
- `docs/plan.md` 3.7의 스키마 예시와 `docs/requirements.md`에 REQ-017을 추가했다.
- `S4-D1` 구간 범위에 문서 `settings` 스키마 확장과 기존 fixture 6종 회귀를 포함했다.
- SVN 통합 검증 정책을 미설치 확정 기준으로 다시 썼다. 도구 미설치 감지는 이 호스트에서 실제 검증 가능한 유일한 SVN 경로이므로 유지한다.

### 영향 요구사항

- REQ-001, REQ-002, REQ-006, REQ-007, REQ-008, REQ-011, REQ-012, REQ-017
- NFR-005, NFR-007, NFR-008

### 다음 작업 제한

- 개정한 `S4-P0` 계획 승인 전에는 `src/`와 `tests/`에 VCS provider 관련 source를 추가하지 않는다.
- 승인 후에도 `S4-D1-CODE` 한 구간만 수행하고 보고 뒤 중지한다.
- 환경설정 화면은 단계 6~7 범위이며 단계 4에서 UI를 만들지 않는다.

## 2026-08-16 - 단계 3 승인과 단계 4 `S4-P0` 계획 작성

### 사용자 지시

- 단계 4를 진행한다.
- 이전 단계와 같이 작업 단위의 변경마다 검수를 받고, 검수 후 사용자가 직접 커밋한다.

### 반영 내용

- 단계 4 진행 지시를 단계 3 최종 승인으로 처리하고 `docs/plan.md`와 `docs/handoff.md`의 승인 대기 상태를 완료로 갱신했다.
- `docs/stage-4-plan.md`에 Git 및 SVN provider 구현 계획을 작성했다.
- provider가 `process_runner`를 주입받아 Win32 API를 직접 호출하지 않는 계층 경계와 의존성 방향을 정의했다.
- PATH 직접 분해 기반 도구 탐색, `--version` 파싱과 `not_found` / `version_unreadable` / `too_old` / `available` 상태를 제안했다.
- Git 비대화형 환경 override 7종과 공통 인자 `-c core.quotepath=false`, `-c gc.auto=0`, `-c color.ui=false`, `--no-pager`를 제안했다.
- 단계 3이 미정으로 남긴 명령별 timeout과 스트림당 캡처 상한을 부류별 값으로 확정 제안했다.
- `rev-parse`와 `status --porcelain=v2 --branch -z` 기반 로컬 상태 조회, 진행 중 작업 표식 파일 판정을 제안했다.
- upstream → `preferred_remote` → `origin` → 유일한 remote 순서의 remote target 선택과 `fetch --prune`, `rev-list --left-right --count` 기반 ahead/behind 판정을 제안했다.
- SVN `info --xml`, `status --verbose --xml`, `status --show-updates --xml` 기반 조회와 mixed revision 및 switched subtree 판정을 제안했다.
- SVN XML 처리 방식으로 pugixml 추가를 권장하고 자체 reader 및 `--show-item` 대체안과 함께 검수 항목으로 올렸다.
- update의 사전 차단 사유 8종, `pull --ff-only`, submodule dirty 사전 검사와 recursive update 순서를 제안했다.
- switch 후보의 remote-first 정렬, ambiguous remote 자동 선택 금지, tracking branch 확인 요구와 `--no-guess` 실행을 제안했다.
- 검증 실패 시 `process_request`를 만들지 않는 REQ-007 수용 기준을 fake runner 기록으로 직접 검증하는 test 전략을 정의했다.
- `remote_sync_state`의 `authentication_required` 추가와 `docs/plan.md` 3.2 Codicon 표 갱신을 검수 항목으로 올렸다.
- stderr 패턴 기반 `authentication_required` / `offline` / `repository_not_found` / `error` 분류기를 제안했다.
- fake runner 단위 test와 실제 임시 Git 저장소 통합 test의 두 층 전략, fixture 12종과 SVN 미설치 대응 정책을 정의했다.
- `CODE` / `TEST` / `FIX` 6분할과 `S4-V1`로 구성한 20개 체크포인트 및 검수 게이트를 정의했다.
- `docs/handoff.md`의 현재 단계, 진행 원장과 미해결 항목을 단계 4 기준으로 갱신했다.

### 영향 요구사항

- REQ-002, REQ-006, REQ-007, REQ-009~REQ-014
- NFR-005~NFR-009

### 다음 작업 제한

- `S4-P0` 계획 승인 전에는 `src/`와 `tests/`에 VCS provider 관련 source를 추가하지 않는다.
- 승인 후에도 `S4-D1-CODE` 한 구간만 수행하고 보고 뒤 중지한다.
- pugixml 추가와 ADR-002 개정은 계획 검수에서 승인된 뒤에만 수행한다.
- ADR-004의 범용 메시지 구조는 단계 6 이전 별도 승인 없이 구현하지 않는다.

## 2026-08-16 - 단계 2·3 독립 감사 및 발견 사항 해소

### 사용자 지시

- 단계 4 진행 전에 단계 2·3의 진행 상황을 독립적으로 감사하고 보고한다.
- 감사에서 확인한 발견 사항들을 해소한 뒤 단계 4로 넘어간다.

### 감사 결과

- 계획 문서 대비 코드·테스트 전수 대조와 현재 HEAD의 build/test 재현으로 두 단계 모두 완료 조건 충족을 확인했다.
- 검증 기록의 test 개수 등 수치 주장이 실측과 일치했고 과장이나 허위는 발견되지 않았다.

### 반영 내용

- runner에 reader join drain 유예(2초)와 `CancelSynchronousIo` 최후 수단을 추가해, 자식이 정상 종료해도 출력 pipe를 상속한 손자 때문에 `run()`이 무기한 블록되는 경로를 없앴다. 강제 마감은 warning 진단으로 보고한다.
- reader 생성 이후 구간을 예외 안전하게 만들어 joinable 스레드 unwinding에 의한 `std::terminate` 경로를 제거하고, reader catch-all이 pipe를 계속 비우며 `process_pipe_failed` 진단을 남기게 했다.
- `text_transcoder`에 `safe_split_position`을 추가하고 fallback 강제 분할이 활성 code page 문자 경계를 따르게 해 CP949 2 byte 문자 훼손을 막았다.
- URL userinfo 마스킹이 authority 안의 마지막 `@`를 구분자로 삼아 percent-encoding 없는 password `@`가 부분 누출되지 않게 했다.
- `application/project_path_resolver.h` 계약을 추가하고 `gitman_workspace`의 Win32 platform 링크를 제거했다. store와 경로 해석은 주입받은 resolver만 사용하며 단위 test는 lexical fake를 쓴다.
- `ReplaceFileW` 실패 후 원본 복원까지 실패한 경우를 `workspace_file_commit_failure::restore`로 구분하고 `.bak` 복구 안내 메시지를 추가했다.
- `default_project_display_name`을 공개해 parser와 store의 중복 정의를 통합했다.
- test 보강: 손자 pipe 점유 drain 회귀(`spawn-detached`/`hold-handles` 도우미), emoji 실행 파일 경로, 8 MiB 대용량 상향, fallback 강제 분할 경계, raw `@` URL 마스킹, `restore` 매핑, `project_path_state_from_error` 매핑. 전체 Catch2 test에 CTest TIMEOUT 120초를 부여했다.
- 로컬 NTFS에서 deny ACE로 `GetFileAttributesW`를 실패시킬 수 없음을 실측으로 확인하고 `inaccessible` 검증을 오류 매핑 방식으로 확정했다.
- TOCTOU 창, 레코드 분할 마스킹 우회, unknown field의 ID 매칭 의존 등은 설계상 수용으로 문서화했다.
- VS2022 Debug/Release, VS2026 Debug 전체 CTest 각각 139/139, `/analyze` 무경고, aggregate format/style 154개 파일 통과.
- 결과를 `docs/verification/2026-08-16-stage-2-3-audit-fix.md`에 기록했다.

### 영향 요구사항

- REQ-001, REQ-006, REQ-008~REQ-013
- NFR-005~NFR-009

### 다음 작업 제한

- 단계 3 최종 사용자 승인 대기 상태는 유지된다. 승인 전에는 단계 4를 시작하지 않는다.
- drain 유예 상수와 Git background 프로세스 대응 정책은 단계 4 계획(`S4-P0`)에서 재검토한다.

## 2026-08-16 - 단계 3 최종 자동 검증

### 사용자 지시

- `S3-D5-TEST`를 승인하고 무결함 `S3-D5-FIX` 생략을 확인한 뒤 `S3-V1`을 진행한다.

### 반영 내용

- `build/vs2022`를 삭제하고 재configure한 뒤 Debug build와 전체 CTest 135/135가 통과했다.
- VS2022 Release build와 CTest 135/135, VS2026 Debug build와 CTest 135/135가 통과했다.
- VS2022 `/analyze` build가 경고 없이 통과했고 aggregate `gitman_format_check`가 152개 파일에서 통과했다.
- 전체 suite를 `--repeat until-fail:3`으로 3회 반복해 flakiness가 없음을 확인했다.
- 4스레드 × 25회, 합계 100개 프로세스 동시 실행 stress를 3회 수행했다. 실패 0건, sequence 역전 0건이며 레코드 수 112,525가 매번 같았다.
- handle 증가폭이 매번 고정된 6개이고 반복해도 누적되지 않아 실행별 누수가 없음을 확인했다.
- Release install 결과가 `bin/gitman.exe` 한 파일(6,255,616 byte)이며 Windows 시스템 DLL 외 의존성이 없음을 확인했다.
- 설치본의 CPU, auto, 강제 fallback과 Direct3D smoke test가 모두 종료 코드 0으로 통과했다.
- test 전용 도우미 target이 install tree에 포함되지 않음을 확인했다.
- 실행 파일 크기가 단계 2와 같은 이유가 `gitman_process`를 아직 exe가 링크하지 않기 때문임을 기록했다.
- 결과를 `docs/verification/2026-08-16-stage-3.md`에 기록했다.

### 영향 요구사항

- REQ-006~REQ-013
- NFR-005~NFR-009, NFR-011

### 다음 작업 제한

- `S3-V1` 자동 완료 조건은 충족했으며 단계 3 최종 사용자 검수 대기 상태다.
- 사용자 최종 승인 전에는 단계 4 Git 및 SVN provider를 시작하지 않는다.

## 2026-08-16 - 단계 3 `S3-D5-TEST` 마스킹 test 작성

### 사용자 지시

- `S3-D5-CODE`를 승인하고 `S3-D5-TEST`를 진행한다.

### 반영 내용

- `tests/secret_masking_tests.cpp`에 URL userinfo, 자격 증명 option, 헤더와 `Basic`, token 접두어, 일반 출력 불변, 복수 비밀과 보수적 경계 test 9개를 추가했다.
- 모든 단정이 기대값 비교와 함께 결과를 다시 마스킹해도 같은지 확인하는 idempotency 검사를 거치게 했다.
- `://`가 없는 SSH 축약 형태(`git@github.com:owner/repo.git`)와 `--password-from-stdin` 같은 접두어 공유 이름이 변형되지 않는 것을 고정했다.
- 자격 증명에 붙은 구두점이 함께 가려지는 문서화된 동작을 test로 고정해 의도치 않은 변경을 잡을 수 있게 했다.
- `tests/win32_process_runner_tests.cpp`에 end-to-end 적용 test를 추가해 기록된 명령줄과 출력 레코드 양쪽에서 비밀이 사라지는 것을 확인했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 135/135 통과했고 `/analyze`도 무경고로 통과했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d5-test.md`에 기록했다.

### 영향 요구사항

- REQ-008, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-006, NFR-008

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D5-FIX`는 사용자 확인 후 생략한다.
- 남은 작업은 `S3-V1` 단계 3 최종 검증뿐이며 승인 후에만 시작한다.

## 2026-08-16 - 단계 3 `S3-D5-CODE` 비밀 마스킹 구현

### 사용자 지시

- `S3-D4-TEST`를 승인하고 무결함 `S3-D4-FIX` 생략을 확인한 뒤 `S3-D5-CODE`를 진행한다.

### 반영 내용

- `infrastructure/secret_masking.*`에 `std::regex`를 쓰지 않는 단일 통과 scanner를 구현했다.
- URL userinfo는 사용자 이름을 남기고 비밀만 가리며, 사용자 이름 없는 값은 token으로 보고 전체를 가린다.
- 자격 증명 option 6종의 값을 `=` 형태와 공백 구분 형태 모두에서 가리고, 명령줄 인용이 남아 있으면 따옴표 안쪽만 가린다.
- `Authorization:`, `PRIVATE-TOKEN:`, `x-access-token:` 값을 줄 끝까지 가리고 단독 `Basic` 자격 증명도 처리한다.
- `ghp_`, `gho_`, `ghu_`, `ghs_`, `ghr_`, `github_pat_`, `glpat-` 접두어가 붙은 token을 가린다.
- option과 token 이름은 단어의 처음에서만 인식하고, 이름 뒤에 `=`나 공백이 오는지로 접두어 충돌을 판정해 목록 순서에 의존하지 않게 했다.
- 마스킹을 인코딩 확정 이후 단계에 두어 UTF-8과 code page fallback 경로 모두에서 sink 도달 전에 적용되게 했다.
- 기록용 `masked_command_line`에만 마스킹을 적용하고 자식에게 넘기는 실제 명령줄은 원본을 유지했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 125/125 통과했고 `/analyze`도 무경고로 통과했다.
- 임시 프로그램으로 26개 항목과 각 항목의 idempotency를 확인했고, 4 MB 출력 마스킹이 863 ms로 실용적임을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d5-code.md`에 기록했다.

### 영향 요구사항

- REQ-008, REQ-009, REQ-011, REQ-012, REQ-013
- NFR-007, NFR-008

### 다음 작업 제한

- `S3-D5-CODE` 검수 전에는 마스킹 test source를 추가하지 않는다.
- 단계 3 최종 검증 `S3-V1`은 `S3-D5-TEST` 승인 후에만 시작한다.

## 2026-08-16 - 단계 3 `S3-D4-TEST` timeout과 취소 test 작성

### 사용자 지시

- `S3-D4-CODE`를 승인하고 `S3-D4-TEST`를 진행한다.

### 반영 내용

- 도우미에 `sleep`, `write-marker`, `spawn-child` 명령을 추가했다. `sleep`은 대기 전에 한 줄을 출력해 timeout 이전 레코드 전달을 확인할 수 있게 했다.
- timeout 종료, 실행 중 취소, 사전 취소, timeout 이전 정상 종료, 미사용 취소 source와 handle 누수 test를 추가했다.
- 손자 종료 test에 대조군을 넣어 "파일이 없다"는 단정이 종료 동작 때문임을 보장했다. 손자가 200 ms 뒤 marker를 만들면 파일이 생기고, 2초 뒤 만들도록 하고 300 ms에 종료하면 3.5초를 기다려도 생기지 않는다.
- handle 누수 test는 20회 반복 실행 전후의 프로세스 handle 수 차이를 확인한다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 125/125 통과했고 `/analyze`도 무경고로 통과했다.
- 타이밍에 의존하는 test 14개를 `--repeat until-fail:3`으로 반복 실행해 flakiness가 없음을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d4-test.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-012, REQ-013
- NFR-007, NFR-009

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D4-FIX`는 사용자 확인 후 생략한다.
- `S3-D5-CODE` 승인 전에는 마스킹 구현을 시작하지 않는다.

## 2026-08-16 - 단계 3 `S3-D4-CODE` timeout과 취소 구현

### 사용자 지시

- `S3-D3-TEST`를 승인하고 무결함 `S3-D3-FIX` 생략을 확인한 뒤 `S3-D4-CODE`를 진행한다.

### 반영 내용

- 이미 취소된 요청은 프로세스를 만들지 않고 `cancelled`로 반환하도록 했다.
- `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` job을 만들고 자식을 `CREATE_SUSPENDED`로 시작한 뒤 배정하고 재개하도록 했다. 배정 전에 손자가 만들어지는 경쟁을 없앤다.
- job을 만들거나 배정하지 못하면 warning 진단과 함께 단일 프로세스 종료로 물러서도록 했다. 제한된 환경에서 실행 자체가 막히지 않게 한 선택이다.
- 취소 token 콜백이 event 하나를 신호하고 `WaitForMultipleObjects`가 프로세스와 취소 event를 함께 기다리도록 했다. polling이 없고 Win32 type도 상위 계층에 노출되지 않는다.
- event handle을 registration보다 먼저 선언해 콜백이 닫힌 event를 신호하지 않도록 수명 순서를 고정했다.
- timeout 초과와 취소에서 트리를 종료하고, 종료로 pipe가 닫혀 reader 스레드가 EOF를 보고 끝나도록 했다.
- 강제 종료한 실행은 종료 코드를 채우지 않고 `timed_out` 또는 `cancelled`로 보고하며 그때까지 수집한 출력은 유지한다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 118/118 통과했고 `/analyze`도 무경고로 통과했다.
- 임시 프로그램으로 19개 항목을 확인했다. 400 ms timeout이 483 ms에 반환되고, 300 ms 취소가 314 ms에 반환되며, 손자 프로세스가 job과 함께 사라지고, 30회 반복 실행 후 handle 수가 늘지 않았다.
- 결과를 `docs/verification/2026-08-16-stage-3-d4-code.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-007, REQ-009, REQ-012, REQ-013
- NFR-007, NFR-009

### 다음 작업 제한

- `S3-D4-CODE` 검수 전에는 도우미 명령과 test source를 추가하지 않는다.
- 마스킹은 `S3-D5-CODE` 승인 후에만 구현한다.

## 2026-08-16 - 단계 3 `S3-D3-TEST` code page fallback test 작성

### 사용자 지시

- `S3-D3-CODE`를 승인하고 `S3-D3-TEST`를 진행한다.

### 반영 내용

- `tests/process_output_pipeline_tests.cpp`에 대역 transcoder를 넣어 판정 순서 test 6개를 추가했다. 유효하지 않은 레코드만 변환, 유효 UTF-8 보존, 변환 실패 시 U+FFFD 복귀, `utf8` 모드의 미호출과 transcoder 없음 경로를 확인한다.
- `is_valid_utf8_text`가 `normalize_utf8_text`와 같은 기준으로 overlong, surrogate, 범위 초과와 미완결 sequence를 거부하는지 확인했다.
- `tests/win32_text_transcoder_tests.cpp`를 추가해 빈 입력, ASCII, 해석 불가 byte와 한국어 호스트의 CP949 복원을 확인했다. 활성 code page가 949가 아니면 해당 단정을 `WARN`으로 건너뛴다.
- `tests/win32_process_runner_tests.cpp`에 end-to-end fallback test 3개를 추가했다. 확인 문자열을 `WideCharToMultiByte(CP_ACP, ...)`로 runtime에 인코딩해 code page 949와 UTF-8 호스트 모두에서 유효한 검증이 되게 했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 118/118 통과했고 `/analyze`도 무경고로 통과했다.
- test 작성 중 UTF-8 한글 byte를 CP949로 해석하면 다른 문자가 된다는 기대값이 틀렸음을 확인했다. 엄격 변환이 실패하는 것이 실제 계약이며 production 수정 없이 기대값을 고쳤다.
- 결과를 `docs/verification/2026-08-16-stage-3-d3-test.md`에 기록했다.

### 영향 요구사항

- REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-006

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D3-FIX`는 사용자 확인 후 생략한다.
- `S3-D4-CODE` 승인 전에는 timeout과 취소 구현을 시작하지 않는다.
- 도우미의 `sleep`과 `spawn-child` 명령은 `S3-D4-TEST`에서만 추가한다.

## 2026-08-16 - 단계 3 `S3-D3-CODE` 활성 code page fallback 구현

### 사용자 지시

- `S3-D2-TEST`를 승인하고 무결함 `S3-D2-FIX` 생략을 확인한 뒤 `S3-D3-CODE`를 진행한다.

### 반영 내용

- `application/text_transcoder.h`에 변환 실패를 값으로 보고하는 `noexcept` transcoder 계약을 추가했다.
- `platform/win32/win32_text_transcoder.*`에 `CP_ACP`와 `MB_ERR_INVALID_CHARS` 기반 엄격 변환을 구현했다. 해석할 수 없는 byte는 실패로 보고해 호출자가 U+FFFD 대체로 되돌릴 수 있게 했다.
- `process_output_pipeline`이 인코딩 모드와 transcoder를 받도록 확장했다. 기본값이 있어 기존 호출과 test는 변경 없이 동작한다.
- 레코드가 유효한 UTF-8이면 그대로 두고, 아닐 때만 활성 code page로 변환한 뒤 `transcoded_from_active_code_page`를 세우도록 했다. 변환이 실패하면 U+FFFD 대체로 되돌린다.
- 판단 단위를 레코드 하나로 두어 한 실행에서 UTF-8 줄과 code page 줄이 섞여도 각각 알맞게 처리된다.
- `is_valid_utf8_text`를 공개해 `normalize_utf8_text`와 같은 기준으로 유효성을 판정하게 했다.
- runner는 fallback을 요청한 실행에서만 transcoder를 만들고 두 스트림 파이프라인이 공유하게 했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 107/107 통과했고 `/analyze`도 무경고로 통과했다.
- 활성 code page 949 호스트에서 임시 프로그램으로 15개 항목을 확인했다. CP949 byte가 `한글`로 복원되고, 유효한 UTF-8은 변형되지 않으며, CP949에서도 해석할 수 없는 byte는 U+FFFD로 대체된다.
- 결과를 `docs/verification/2026-08-16-stage-3-d3-code.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-007

### 다음 작업 제한

- `S3-D3-CODE` 검수 전에는 fallback test source를 추가하지 않는다.
- timeout, 취소와 마스킹은 각각 `S3-D4-CODE`, `S3-D5-CODE` 승인 후에만 구현한다.

## 2026-08-16 - 단계 3 `S3-D2-TEST` 도우미 target과 실행 계층 test 작성

### 사용자 지시

- 수정한 `S3-D2-CODE`를 승인하고 `S3-D2-TEST`를 진행한다.

### 반영 내용

- `tests/helpers/process_test_child.cpp`와 `gitman_process_test_child` 콘솔 target을 추가했다. 표준 `wmain` argv를 쓰고 raw byte로 출력하며 install 대상이 아니다.
- test는 `GetModuleFileNameW`로 도우미 경로를 runtime에 찾는다. compile definition으로 넘기면 Windows 경로의 backslash가 문자열 이스케이프로 해석되는 문제를 피했다.
- `tests/command_line_builder_tests.cpp`에 인용 규칙, backslash 처리, 셸 metacharacter 보존과 명령줄 조립 test 5개를 추가했다.
- `tests/process_output_pipeline_tests.cpp`에 UTF-8 정규화, 줄 분할, 진행 표시, chunk 경계, 강제 분할, 캡처 상한과 flush test 10개를 추가했다.
- `tests/win32_process_runner_tests.cpp`에 종료 코드, 인자 왕복, 한글 및 공백 작업 디렉터리, 환경 override, 4 MB 출력, 절단, 두 스트림 순서, 혼합 출력, 읽기 경계, null sink, stdin EOF, 시작 실패, 잘못된 요청과 동시 실행 test 14개를 추가했다.
- `tests/process_execution_tests.cpp`에 `internal_error` 이름 매핑과 성공 아님 판정을 추가했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 107/107 통과했고 `/analyze`도 무경고로 통과했다.
- Release install 결과가 `bin/gitman.exe` 한 파일임을 다시 확인해 도우미 target이 배포에 포함되지 않음을 검증했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d2-test.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-007, NFR-011

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D2-FIX`는 사용자 확인 후 생략한다.
- `S3-D3-CODE` 승인 전에는 code page fallback transcoder를 구현하지 않는다.
- `sleep`과 `spawn-child` 도우미 명령은 `S3-D4-TEST`에서만 추가한다.

## 2026-08-16 - 단계 3 `S3-D2-CODE` 수정 재제출

### 사용자 지시

- `S3-D2-CODE` 1차 제출을 수정 후 재검수로 판정하고 두 항목을 지시했다.
- `WaitForSingleObject` 실패 시 자식을 정리한다.
- `S3-D3`의 출력 pipe와 reader 스레드를 `S3-D2`에 포함한다.

### 반영 내용

- `process_completion::internal_error`를 추가해 프로세스는 시작했지만 결과를 신뢰할 수 없는 경로를 구분했다.
- wait 실패, reader 스레드 생성 실패, 종료 코드 확인 실패에서 자식을 `TerminateProcess`로 정리하고 `internal_error`를 반환하도록 했다.
- stdout과 stderr에 익명 pipe를 연결하고 쓰기 end만 상속시키며 시작 직후 부모 사본을 닫아 EOF를 관측하게 했다.
- pipe별 전용 reader 스레드를 만들고 `run` 반환 전에 항상 join하도록 했다.
- `infrastructure/process_output_pipeline.*`에 줄 단위 레코드, `\r\n` 및 단독 `\r` 처리, 강제 분할, UTF-8 경계 보정, 잘못된 byte의 U+FFFD 대체, 스트림별 캡처 상한과 절단 표시를 구현했다.
- `output_collector`가 mutex 아래에서 실행 단위 sequence를 부여하고 sink 예외를 흡수한 뒤 진단으로 보고하게 했다.
- 절단 발생 시 warning 진단을 남기고 실행 자체는 실패로 보지 않도록 했다.
- 저장소 밖 임시 프로그램으로 출력 34개 항목을 확인했다. 8,000,028 byte 출력이 교착 없이 131,148 레코드로 수집되고, chunk 경계에 걸친 한글 문자가 온전하며, 8 byte 상한에서 `continued` 분할이 동작한다.
- 구현 중 강제 분할 경계 계산 오류를 발견해 같은 구간에서 고쳤다.
- 범위 이동에 따라 `S3-D3`은 활성 code page fallback transcoder와 파이프라인 단위 test 보강만 담당하도록 계획을 갱신했다.

### 이전 제출 내용

- `infrastructure/command_line_builder.*`에 `CommandLineToArgvW` 규칙 인자 인용과 명령줄 조립을 구현했다.
- `platform/win32/win32_process_runner.*`에 `lpApplicationName` 기반 절대 경로 실행, 셸 미사용 시작, 작업 디렉터리 적용을 구현했다.
- 부모 환경 상속과 override 설정 및 삭제, 대소문자 무시 이름 비교, 정렬된 환경 block 생성을 구현했다.
- stdin을 항상 `NUL`에 연결해 대화형 프롬프트가 즉시 EOF가 되게 했다.
- `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`로 표준 handle 세 개만 상속시키고 시작 직후 부모 사본을 닫았다.
- 종료 코드를 bit 값 그대로 보존하고 시작 실패를 `start_failed`와 Win32 error code, 구조화 진단으로 반환했다.
- 실행용 명령줄과 기록용 `masked_command_line`을 분리해 `S3-D5-CODE`의 마스킹이 실행 인자에 영향을 주지 않게 했다.
- `gitman_process` target에 두 source를 넣고 `gitman_win32_platform`을 PRIVATE으로 링크했다. static library 순환 참조를 피하려고 runner의 target만 계획과 다르게 정했고 파일 위치는 계획대로 유지했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest 78/78, `/analyze` 무경고, aggregate format/style이 통과했다.
- 저장소 밖 임시 프로그램으로 21개 시작 계약 항목을 수동 확인하고 삭제했다. 결과는 `docs/verification/2026-08-16-stage-3-d2-code.md`에 기록했다.
- `cmd.exe`가 argv 규칙을 쓰지 않아 test 자식으로 부적합함을 확인하고 계획 8.1에 도우미 요건을 명시했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-007, NFR-008

### 다음 작업 제한

- `S3-D2-CODE` 검수 전에는 도우미 실행 파일 target과 test source를 추가하지 않는다.
- 출력 수집, timeout 및 취소, 마스킹은 각각 `S3-D3-CODE`, `S3-D4-CODE`, `S3-D5-CODE` 승인 후에만 구현한다.

## 2026-08-16 - 단계 3 `S3-D1-TEST` 계약 test 작성과 format 기준선 정렬

### 사용자 지시

- `S3-D1-CODE`를 승인하고 `S3-D1-TEST`를 진행한다.
- clang-format이 수동 줄바꿈을 되돌리는 3개 파일은 formatter 결과를 수용한다.

### 반영 내용

- `tests/process_execution_tests.cpp`에 출력 레코드 및 실행 결과 기본값, 성공 판정, 비정상 완료와 이름 매핑 test 5개를 추가했다.
- `tests/process_request_tests.cpp`에 기본값, 어휘적 절대 경로 판정, 실행 파일과 작업 디렉터리, 인자 NUL, 환경 override, timeout 및 상한 경계와 복합 오류 test 9개를 추가했다.
- `tests/process_cancellation_tests.cpp`에 기본 token, 통지 횟수, 취소 후 등록, 해제, registration 이동, 이동한 source, source 소멸, 동시 취소와 등록 경합 test 10개를 추가했다.
- `tests/domain_model_tests.cpp`의 diagnostic 이름 표에 프로세스 code 6개를 추가했다.
- `gitman_tests`에 `gitman_process`를 링크했다.
- `utf8.cpp`, `win32_application.cpp`, `ui_theme.h`를 clang-format 19.1.5 결과로 정렬해 aggregate `gitman_format_check`를 다시 통과시켰다. 의미 변경은 없다.
- `docs/code_style.md` 2장에 `ColumnLimit` 200 안의 표현식은 formatter 결과가 기준이라는 규칙을 명시했다.
- VS2022 Debug/Release와 VS2026 Debug 전체 CTest가 각각 78/78 통과했고 `/analyze`도 무경고로 통과했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d1-test.md`에 기록했다.

### 영향 요구사항

- REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-006, NFR-007

### 다음 작업 제한

- 발견 production 결함이 없어 `S3-D1-FIX`는 사용자 확인 후 생략한다.
- `S3-D2-CODE` 승인 전에는 `CreateProcessW` 실행 코드를 작성하지 않는다.
- 자식 프로세스를 실행하는 test와 콘솔 도우미 실행 파일은 `S3-D2-TEST`에서만 추가한다.

## 2026-08-16 - 단계 3 `S3-D1-CODE` 계약 production code 구현

### 사용자 지시

- `S3-P0` 계획을 승인하고 `S3-D1-CODE`를 진행한다.
- 체크포인트는 17개를 유지하고 활성 code page fallback은 단계 3에 포함한다.

### 반영 내용

- `domain/process_execution.*`에 스트림, 완료 사유, 출력 레코드와 실행 결과 값을 추가했다.
- `domain/diagnostic.*`에 프로세스 실행 관련 diagnostic code 6개와 이름 매핑을 추가했다.
- `application/process_request.*`에 요청 값과 filesystem 조회 없는 요청 검증, 어휘적 절대 경로 판정을 추가했다.
- 요청 검증에 NUL 문자, 중복 환경 override, 0 이하 timeout, 0 캡처 상한과 4 byte 미만 레코드 상한 규칙을 넣었다.
- `application/process_runner.h`에 sink 직렬화와 동기 `run` 계약을 정의했다.
- `application/process_cancellation.*`에 콜백 등록과 RAII 해제를 지원하는 취소 primitive를 추가했다.
- `gitman_process` static library target을 추가하고 `gitman_domain`에 프로세스 값 model을 넣었다.
- VS2022/VS2026 Debug build, 양 toolchain 전체 CTest 54/54, VS2022 `/analyze` 무경고, source style과 `git diff --check`를 통과했다.
- 결과를 `docs/verification/2026-08-16-stage-3-d1-code.md`에 기록했다.

### 영향 요구사항

- REQ-006, REQ-009, REQ-010, REQ-011, REQ-012, REQ-013
- NFR-005, NFR-007, NFR-009

### 다음 작업 제한

- `S3-D1-CODE` 검수 전에는 `S3-D1-TEST`의 test source와 `gitman_tests` 링크를 추가하지 않는다.
- 기존 aggregate `gitman_format_check` 실패 3개 파일은 사용자가 처리 방향을 정하기 전에는 수정하지 않는다.
- Win32 프로세스 실행 구현은 `S3-D2-CODE` 승인 후에만 작성한다.

## 2026-08-16 - 단계 2 승인과 단계 3 `S3-P0` 계획 작성

### 사용자 지시

- 단계 3을 진행한다.
- 단계 2처럼 모든 세션을 자동으로 처리하지 말고 계획, 작업과 테스트 각 과정의 중간에 진행 내용과 처리 방침을 보고하고 검수받는다.

### 반영 내용

- 단계 3 진행 지시를 단계 2 최종 승인으로 처리하고 `docs/plan.md`, `docs/stage-2-plan.md`, `docs/verification/2026-08-16-stage-2.md`의 승인 대기 상태를 완료로 갱신했다.
- `docs/stage-3-plan.md`에 프로세스 실행 계층 구현 계획을 작성했다.
- 실행 API를 동기 블로킹 `run` 하나로 두고 scheduler와 스레드 배치를 단계 6~7로 미루는 범위를 정의했다.
- 셸 미사용, 절대 경로 실행 파일, `CommandLineToArgvW` 규칙 인자 인용, stdin `NUL` 연결, handle 상속 제한과 job object 트리 종료 계약을 제안했다.
- pipe별 reader 스레드, 줄 단위 레코드, chunk 경계 UTF-8 보류, 단독 `\r` progress 표시, 캡처 상한과 실행 단위 단조 sequence를 제안했다.
- timeout 및 취소 primitive와 Win32 event 콜백 연결, 앱 종료 시 취소 후 join 정책을 제안했다.
- URL userinfo, 자격 증명 option, token prefix 마스킹 규칙과 `std::regex` 미사용 결정을 제안했다.
- 실제 Git/SVN 대신 결정적 검증에 사용할 테스트 전용 콘솔 도우미 실행 파일 target을 제안했다.
- `CODE` / `TEST` / `FIX` 5분할과 `S3-V1`로 구성한 17개 체크포인트 및 검수 게이트를 정의했다.
- `docs/handoff.md`의 현재 단계, 진행 원장과 보고 방식 지시를 단계 3 기준으로 갱신했다.

### 영향 요구사항

- REQ-006, REQ-007, REQ-008, REQ-009~REQ-013
- NFR-005~NFR-009

### 다음 작업 제한

- `S3-P0` 계획 승인 전에는 `src/`와 `tests/`에 프로세스 실행 관련 source를 추가하지 않는다.
- 승인 후에도 `S3-D1-CODE` 한 구간만 수행하고 보고 뒤 중지한다.
- ADR-004의 범용 메시지 구조는 단계 6 이전 별도 승인 없이 구현하지 않는다.

## 2026-08-16 - 단계 2 최종 자동 검증

### 사용자 지시

- `S2-D5-TEST` 결과 이후 다음 작업을 계속 진행한다.
- 이를 `S2-D5-TEST` 승인, 무결함 `S2-D5-FIX` 생략과 `S2-V1` 진행 지시로 처리했다.

### 반영 내용

- VS2022 Debug/Release build와 각 전체 CTest 54/54가 통과했다.
- VS2022 `/analyze` build가 경고 없이 통과했고 VS2026 Debug build 및 전체 CTest 54/54도 통과했다.
- 기존 aggregate format 기준선 위반 3개 파일을 clang-format 19.1.5로만 정렬하고 모든 build, test와 분석을 다시 통과시켰다.
- aggregate `gitman_format_check`와 source style 검사가 통과했다.
- Release install tree에 6,255,616 byte의 `bin/gitman.exe` 한 파일만 생성되는 것을 확인했다.
- 설치본의 CPU, auto, 강제 fallback과 Direct3D smoke test가 모두 종료 코드 0으로 통과했다.
- `dumpbin /dependents`로 Windows 시스템 DLL 외의 VC runtime, 프로젝트 및 제3자 DLL이 없음을 확인했다.
- 결과를 `docs/verification/2026-08-16-stage-2.md`에 기록했다.

### 다음 작업 제한

- `S2-V1` 자동 완료 조건은 충족했으며 단계 2 최종 사용자 검수 대기 상태다.
- 사용자 최종 승인 전에는 단계 3 프로세스 실행 계층을 시작하지 않는다.

## 2026-08-16 - 단계 2 positional launch path test 작성

### 사용자 지시

- `S2-D5-CODE` 검수 뒤 계속 진행한다.
- 이를 `S2-D5-CODE` 승인과 다음 체크포인트 `S2-D5-TEST` 진행 지시로 처리한다.

### 반영 내용

- 문서 경로가 없는 기존 시작에서 optional launch path가 비어 있는지 검증했다.
- 한글과 공백을 포함한 `.version-list` 원문, path와 renderer option의 전후 순서 및 대문자 확장자 허용을 검증했다.
- option이 사이에 있어도 두 번째 positional path를 거부하는지 검증했다.
- `.json`, `.version-list.bak`과 trailing space suffix를 잘못된 확장자로 거부하는지 검증했다.
- 신규 Catch2 test 4개와 VS2022/VS2026 전체 CTest 54/54가 통과했다.
- production source를 변경하지 않았고 `S2-D5-FIX` 후보도 발견하지 않았다.

### 다음 작업 제한

- `S2-D5-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D5-FIX`를 생략하거나 `S2-V1` 단계 2 최종 검증을 시작하지 않는다.
- 승인되고 무결함 결과가 확인되면 `S2-D5-FIX`를 수정 없이 생략한 뒤 `S2-V1`만 진행한다.

## 2026-08-16 - 단계 2 positional launch path production code 구현

### 사용자 지시

- `S2-D4-TEST` 이후 남은 단계 2 작업을 확인하고 진행한다.
- 이를 `S2-D4-TEST` 승인, 무결함 `S2-D4-FIX` 생략과 다음 체크포인트 `S2-D5-CODE` 진행 지시로 처리한다.

### 반영 내용

- `application_options`에 선택적인 UTF-8 `workspace_document_path`를 추가해 기존 Win32 진입 경계를 통해 보존한다.
- 실행 파일 이름 뒤 하나의 positional `.version-list` path를 허용하고, 두 번째 positional path는 한 창당 한 문서 계약에 따라 거부한다.
- 확장자는 Windows 사용 방식에 맞게 ASCII 대소문자를 구분하지 않고 판정하며 다른 확장자와 backup suffix는 거부한다.
- 미지원 `--` option은 문서 경로로 오인하지 않고 기존 command-line 오류로 유지한다.
- parser는 path 원문을 바꾸거나 filesystem을 조회하지 않으며 실제 load, schema, backup과 recovery 판단은 project store 경계에 남긴다.
- VS2022와 VS2026 Debug build 및 기존 전체 CTest 50/50을 통과했다.
- 이번 체크포인트에서는 test source와 fixture를 변경하지 않았다.

### 다음 작업 제한

- `S2-D5-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D5-TEST`의 launch path test를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D5-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-15 - 단계 2 원자적 저장과 복구 test 작성

### 사용자 지시

- 다음 작업 진행을 시작한다.
- 이를 `S2-D4-CODE` 승인과 다음 체크포인트 `S2-D4-TEST` 진행 지시로 처리한다.

### 반영 내용

- in-memory file adapter fake로 최초 생성, 기존 교체, exact-byte 동시 수정, 후보 재검증과 write/flush/replace 실패 주입을 검증했다.
- canonical JSON의 UTF-8 무 BOM, 공백 4칸, CRLF와 unknown field 및 원문 path 보존을 검증했다.
- valid/invalid backup 탐지, 자동 적용 금지와 명시적 backup load 뒤 save 복구를 검증했다.
- 실제 Win32 임시 디렉터리에서 최초 생성, `ReplaceFileW` 교체, 직전 원본 backup, 외부 변경 충돌과 replace 실패 뒤 임시 파일 정리를 검증했다.
- 신규 Catch2 test 9개와 VS2022/VS2026 전체 CTest 50/50이 통과했다.
- production source를 변경하지 않았고 `S2-D4-FIX` 후보도 발견하지 않았다.

### 다음 작업 제한

- `S2-D4-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D4-FIX`를 생략하거나 `S2-D5-CODE`를 시작하지 않는다.
- 승인되고 무결함 결과가 확인되면 `S2-D4-FIX`를 수정 없이 생략한 뒤 `S2-D5-CODE`만 진행한다.

## 2026-08-15 - 단계 2 원자적 저장과 복구 production code 구현

### 사용자 지시

- `S2-D3-TEST`를 검수 완료로 처리한다.
- 발견 production 결함이 없는 `S2-D3-FIX`를 생략한다.
- `S2-D4-CODE`의 원자적 저장, 동시 수정 감지, backup 및 명시적 recovery production 구현을 시작한다.

### 반영 내용

- 호출자가 해석하지 않는 revision token과 primary/backup load, save 결과를 제공하는 project store 계약을 추가했다.
- load 당시 primary 원문 byte와 save 직전 byte를 정확히 비교해 동시 수정이면 파일을 변경하지 않는다.
- unknown field와 원문 path를 보존하면서 UTF-8 무 BOM, 공백 4칸과 CRLF JSON을 serialize하고 저장 전 전체 후보를 재검증한다.
- 대상과 같은 디렉터리의 임시 파일에 write 및 `FlushFileBuffers`를 수행하고, 기존 문서는 `ReplaceFileW`와 `.bak`, 최초 문서는 write-through move로 교체한다.
- primary load 실패 시 유효한 backup을 진단으로만 알리고, 별도 `load_backup` 뒤 새 save 요청으로만 복구하도록 분리했다.
- write, flush와 replace 실패를 구분하는 file adapter 계약을 두어 다음 test 체크포인트의 결정적 실패 주입 경계를 마련했다.
- VS2022와 VS2026 Debug build 및 기존 전체 CTest 41/41을 통과했다.
- 이번 체크포인트에서는 test source와 fixture를 변경하지 않았다.

### 다음 작업 제한

- `S2-D4-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D4-TEST`의 save/recovery test와 fixture를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D4-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-15 - 단계 2 project path test 작성

### 사용자 지시

- `S2-D3-CODE`를 승인하고 test 작성을 진행한다.

### 반영 내용

- runtime 임시 디렉터리 fixture와 project path test 7개를 추가했다.
- document 기준 상대 path, drive, extended drive, UNC 및 extended UNC 정규화를 검증했다.
- 한글, emoji, 공백, long path와 directory, file, missing 및 invalid 상태를 검증했다.
- 부분 성공 문서에서 대소문자 중복 path를 제외하고 원래 JSON project index와 diagnostic 위치를 보존하는지 검증했다.
- VS2022와 VS2026 Debug build 및 전체 CTest 41/41을 통과했다.
- production source를 변경하지 않았고 path production 결함도 발견되지 않았다.

### 다음 작업 제한

- `S2-D3-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D3-FIX`를 생략하거나 `S2-D4-CODE`를 시작하지 않는다.

## 2026-08-15 - 단계 2 project path production code 구현

### 사용자 지시

- `S2-D2-TEST`를 승인하고 다음 구현을 진행한다.

### 반영 내용

- production 결함이 없었던 `S2-D2-FIX`를 생략하고 `S2-D3-CODE`를 진행했다.
- document 디렉터리 기준 상대 path와 absolute drive 및 UNC path의 lexical 정규화를 구현했다.
- UTF-8 원문을 보존하면서 별도의 absolute normalized path와 filesystem 상태를 계산한다.
- missing, inaccessible, not-directory와 invalid 상태 및 구조화 diagnostic을 구현했다.
- Windows case-insensitive ordinal 비교로 중복 normalized path의 첫 project만 유지한다.
- schema parse와 filesystem resolution을 분리하고 원래 JSON project index를 shadow metadata에 보존했다.
- Win32 path 및 UTF 구현을 UI/Skia와 독립적인 `gitman_win32_platform` target으로 분리했다.
- VS2022와 VS2026 Debug build 및 기존 CTest 34/34를 통과했다.
- 이번 체크포인트에서는 test source와 fixture를 변경하지 않았다.

### 다음 작업 제한

- `S2-D3-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D3-TEST` path test와 fixture를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D3-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-14 - 단계 2 schema parser test 작성

### 사용자 지시

- `S2-D2-CODE`를 승인하고 다음 작업을 진행한다.

### 반영 내용

- 정상, 손상, 부분 성공, 이전 및 미래 version과 unknown field fixture 6개를 추가했다.
- schema parser 계약 test 8개와 project field 오류 matrix 12종을 추가했다.
- unknown field의 JSON pointer escape와 입력 JSON byte의 정확한 shadow 보존을 검증했다.
- `.version-list` test asset도 UTF-8 무 BOM 및 CRLF 검사를 받도록 품질 도구 범위를 확장했다.
- VS2022와 VS2026 Debug build 및 CTest 34/34를 통과했다.
- production source를 변경하지 않았고 parser production 결함도 발견되지 않았다.

### 다음 작업 제한

- `S2-D2-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D2-FIX`를 생략하거나 `S2-D3-CODE`를 시작하지 않는다.

## 2026-08-14 - 단계 2 schema parser production code 구현

### 사용자 지시

- `S2-D1-TEST`를 승인하고 다음 구현을 진행한다.

### 반영 내용

- production 결함이 없었던 `S2-D1-FIX`를 생략하고 `S2-D2-CODE`를 진행했다.
- schema version 1의 `.version-list` JSON parser와 구조화 parse result를 추가했다.
- 문서 수준 실패, project별 부분 성공, optional 기본값, 중복 ID와 unknown field warning을 구현했다.
- unknown field의 후속 round-trip을 위해 입력 JSON byte를 shadow에 그대로 보존했다.
- public header에서 nlohmann/json, Win32와 Skia type을 노출하지 않았다.
- VS2022와 VS2026 Debug build 및 기존 CTest 26/26을 통과했다.
- 이번 체크포인트에서는 새 test source와 fixture를 작성하지 않았다.

### 다음 작업 제한

- `S2-D2-CODE`는 사용자 코드 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D2-TEST`의 schema fixture와 test를 작성하지 않는다.
- test에서 production 결함이 발견되어도 `S2-D2-FIX` 승인 전에는 수정하지 않는다.

## 2026-08-14 - 단계 2 도메인 model test 작성

### 사용자 지시

- `S2-D1-CODE`를 승인하고 test 작성을 시작한다.

### 반영 내용

- workspace/project 기본값, VCS hint, path 상태, repository snapshot, operation과 diagnostic 계약 test 6개를 추가했다.
- VS2022와 VS2026 Debug build 및 CTest 26/26을 통과했다.
- test 작성 중 production source와 fixture를 변경하지 않았다.
- production 결함은 발견되지 않았다.

### 다음 작업 제한

- `S2-D1-TEST`는 사용자 test 검수 대기 상태다.
- 사용자 승인 전에는 `S2-D2-CODE` schema/parser 구현을 시작하지 않는다.

## 2026-08-14 - `.version-list` 작업공간 문서 결정 및 단계 2 계획 승인

### 사용자 지시

- 프로젝트 목록은 고정 `projects.json`이 아니라 solution 및 `.code-workspace`와 같은 문서로 취급한다.
- 확장자는 사용자 지시의 철자 그대로 `.version-list`를 사용한다.
- Gitman을 해당 확장자의 Windows 연결 프로그램으로 동작하게 한다.
- 나머지 단계 2 계획을 승인하고 추가 승인 요청 없이 첫 production code 구현을 진행한다.

### 반영 내용

- 고정 기본 config 위치를 제거하고 한 창당 하나의 `.version-list` 활성 문서로 변경했다.
- shell positional path로 문서를 여는 계약과 단계 8 file association 등록 및 제거 검증을 요구사항에 추가했다.
- `S2-P0`을 승인 완료로, 현재 체크포인트를 `S2-D1-CODE`로 갱신했다.
- UI, Win32와 JSON library에 의존하지 않는 `gitman_domain` target을 추가했다.
- project, repository snapshot, operation과 diagnostic production type 및 안정적인 이름 변환을 구현했다.
- VS2022와 VS2026 Debug build 및 기존 CTest 20/20, source style 80개를 통과했다.

### 다음 작업 제한

- `S2-D1-CODE`는 사용자 코드 검수 대기 상태다.
- 새 test source와 fixture는 사용자가 코드 검수 후 `S2-D1-TEST` 진행을 승인하기 전까지 작성하지 않는다.

## 2026-08-14 - 단계 2 구현 계획 및 세부 검수 게이트 작성

### 사용자 지시

- 단계 2를 진행하되 구현 계획, production code, test code와 bug 수정을 한 번에 수행하지 않는다.
- 일정 부분마다 사용자 검수를 받고 다음 작업으로 넘어간다.
- 짧은 세션과 context 압축에 대비해 영속 세션 메모리를 계속 갱신한다.

### 반영 내용

- `docs/stage-2-plan.md`에 도메인 model, schema v1, 부분 오류, path 정규화, 원자적 저장과 복구 계획을 작성했다.
- production code, test code와 bug 수정을 분리한 `S2-P0`~`S2-V1` 체크포인트를 정의했다.
- `docs/handoff.md`를 단계 2의 영속 진행 원장으로 갱신하고 현재 상태를 `S2-P0` 사용자 검수 대기로 기록했다.
- 이번 체크포인트에서는 source, test와 fixture를 변경하지 않았다.

### 다음 작업 제한

- 사용자가 `S2-P0` 계획을 승인하기 전에는 `S2-D1-CODE`를 시작하지 않는다.
- 승인 뒤에도 도메인 production code만 구현하고 새 test code는 작성하지 않은 채 다시 검수를 요청한다.

## 2026-08-14 - 캡션 UI 클래스 및 컬러 테마 팔레트 분리

### 사용자 수정 요청

- 캡션 커스터마이징을 전용 UI 클래스에서 통합 처리한다.
- 캡션 높이를 줄여 시각적 두께를 완화한다.
- 컬러 테마 확장을 위해 색상 값을 렌더링 코드에서 분리한다.

### 반영 내용

- `caption_ui`가 캡션 배경, 제목, 앱 아이콘, 창 버튼과 hover 렌더링을 통합하도록 분리했다.
- 캡션의 공용 논리 높이를 48px에서 40px로 줄이고 Win32 hit test도 같은 메트릭을 사용하도록 연결했다.
- 화면과 캡션 색상을 의미 기반 `ui_color_palette`로 분리하고 dark 및 high contrast 팔레트를 제공했다.
- 캡션 레이아웃과 테마 팔레트 선택을 단위 테스트로 검증한다.

## 2026-08-14 - 전역 공백 4칸 기본값 적용

### 사용자 수정 요청

- CMake와 기타 코드에도 공백 4칸을 기본 들여쓰기로 적용한다.

### 반영 내용

- `.editorconfig`에 C++, CMake, PowerShell, JSON, XML 및 Windows resource 코드의
  공백 4칸 기본값을 추가했다.
- `vcpkg.json`과 CMake continuation 들여쓰기를 공백 4칸으로 정렬했다.
- 줄 끝 정규화 및 source style 검사 대상에 `.editorconfig`를 추가했다.

## 2026-08-14 - 단일 문장 제어문 및 조건식 줄바꿈 규칙 반영

### 사용자 수정 요청

- 단일 문장 제어문은 가급적 중괄호를 생략한다.
- 여러 줄 조건식의 `&&`와 `||`는 새 줄의 처음에 둔다.

### 반영 내용

- `.clang-format`에 단일 문장 제어문 중괄호 제거와 이항 연산자의 줄 앞 배치를 추가했다.
- `src/` 전체를 새 규칙으로 재포맷했다.
- 코드 컨벤션 문서와 요구사항·빌드 안내를 갱신했다.

## 2026-08-14 - C++ 코드 컨벤션 갱신

### 사용자 수정 요청

- Allman 중괄호, namespace 들여쓰기, 제어문 본문 배치, 중괄호 초기화, 생성자
  초기화 목록, 긴 표현식의 끝 표시 규칙을 추가한다.

### 반영 내용

- `docs/code_style.md`에 C++ 코드 컨벤션을 단일 기준으로 정리했다.
- `.clang-format`에 Allman 중괄호, namespace 내부 1단계 들여쓰기, 짧은 제어문
  본문 줄바꿈, 생성자 초기화 목록의 새 줄 쉼표를 반영했다.
- 기존 C++ source와 test를 새 formatter 설정으로 정렬했다.
- `requirements.md`와 `build.md`가 새 컨벤션 문서를 참조하도록 갱신했다.

### 다음 작업 제한

- 부정 연산자 지양과 제어문 본문의 중괄호 생략 여부는 자동 실패 규칙이 아니라
  코드 리뷰 기준으로 둔다.

## 2026-08-14 - 단계 1 캡션 버튼 hover 동작 연결

### 사용자 수정 요청

- 커스텀 캡션 버튼에 포인터 hover 동작까지 구현한다.

### 반영 내용

- `WM_NCMOUSEMOVE`와 `TrackMouseEvent`의 non-client leave 추적으로 최소화, 최대화/복원과 닫기 버튼의 hover 상태를 관리한다.
- 최소화와 최대화/복원에는 중립 hover 배경을, 닫기에는 경고색 hover 배경을 표시한다.
- 포인터가 버튼 또는 non-client 영역을 벗어나거나 창이 비활성화 및 크기 변경되면 hover 상태를 즉시 해제한다.
- non-client mouse message는 기본 Win32 처리에도 전달해 maximize button hover의 Snap Layout 경로를 보존한다.
- 144 DPI 실제 창에서 세 버튼의 hover, 포인터 이탈 후 원상복귀와 최대화 hover의 Windows 11 Snap Layout 표시를 화면 캡처 및 픽셀 값으로 검증했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 캡션 버튼 Codicon 및 기능 연결

### 사용자 수정 요청

- 최소화, 복원, 최대화와 닫기 버튼을 각각 `chrome-minimize`, `chrome-restore`, `chrome-maximize`, `chrome-close` Codicon으로 표시한다.
- 커스텀 캡션 버튼에 실제 창 동작을 연결한다.

### 반영 내용

- 캡션 버튼의 수동 선 그리기를 제거하고 embedded Codicons glyph를 실제 bounds 기준으로 중앙 배치했다.
- 일반 상태에는 `chrome-maximize`, 최대화 상태에는 `chrome-restore`를 표시한다.
- non-client 버튼의 누름을 직접 처리해 최소화, 최대화/복원과 닫기 시스템 명령을 실행한다.
- embedded asset test가 캡션에서 사용하는 네 chrome glyph를 모두 검증하도록 확장했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 커스텀 캡션 검수 의견 반영

### 사용자 수정 요청

- 시스템 캡션과 커스텀 캡션이 겹쳐 그려지는 문제를 수정한다.
- 커스텀 캡션 아이콘에 Codicon을 자연스럽게 적용한다.

### 반영 내용

- 표준 overlapped window로 기본 배치를 받은 뒤 표시 전에 `WS_CAPTION`을 제거하고 frame을 재계산했다. resize, minimize, maximize와 system menu style은 유지했다.
- 커스텀 캡션 아이콘을 repository glyph에서 `source-control` Codicon으로 변경했다.
- Codicon을 UTF-8 text의 고정 좌표로 그리지 않고 실제 glyph bounds를 기준으로 아이콘 영역 중앙에 배치했다.
- embedded asset test가 실제 캡션에서 사용하는 `source-control` glyph의 존재를 검증하도록 변경했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 코드 스타일 검수 의견 반영

### 사용자 수정 요청

- template 선언과 함수 signature를 서로 다른 줄에 둔다.
- 중괄호 초기화 안쪽에 여백을 추가한다.
- 여러 줄 중괄호 초기화의 마지막 닫는 중괄호를 독립된 줄에 둔다.

### 반영 내용

- `.clang-format`에 `template<...>` 표기와 signature 줄 분리 및 중괄호 초기화 여백 규칙을 고정했다.
- 단계 1 C++ source와 test를 새 규칙으로 다시 포맷했다.
- 중첩된 여러 줄 초기화에는 후행 쉼표를 사용해 마지막 닫는 중괄호가 독립된 줄에 유지되도록 했다.
- source style 검사에 같은 줄의 template/signature와 여러 줄 닫는 중괄호 위반 검사를 추가했다.
- REQ-010, NFR-006과 빌드 안내에 검수된 스타일 기준을 기록했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2를 시작하지 않는다.

## 2026-08-14 - 단계 1 빌드 및 품질 기준선 구현

### 사용자 지시

- ADR-001 작업을 개시한다.
- 한 번에 한 단계씩 진행한다는 기존 검수 gate를 유지한다.

### 반영 내용

- CMake 4.2.0과 VS2022/VS2026 preset, v143/v145 정적 vcpkg triplet을 추가했다.
- Skia 148 Direct3D 및 CPU renderer, 자동 fallback, Win32 custom caption skeleton과 UTF 변환 경계를 구현했다.
- Codicons `v0.0.46-24` 자산, checksum, 생성 mapping, 라이선스와 vcpkg 고지문을 실행 파일 resource로 포함했다.
- Catch2 단위 테스트, renderer smoke test, source style, clang-format, `/analyze`와 install 검증을 구성했다.
- VS2022 Debug/Release, VS2026 Debug의 각 19개 CTest와 설치본 네 renderer smoke test를 통과했다.
- 결과와 남은 수동 검수 항목을 `docs/verification/2026-08-14-stage-1.md`에 기록했다.

### 다음 작업 제한

- 단계 1은 사용자 검수 대기 상태다.
- 사용자 승인 전에는 단계 2 도메인 및 설정 저장소 구현을 시작하지 않는다.
- ADR-004의 범용 메시지 구조는 단계 6 전 별도 설계 승인까지 구현하지 않는다.

## 2026-08-14 - 단계 0 검수 의견 반영 및 인계

### 사용자 지시

- 최소 CMake를 4.2.0으로 변경한다.
- CPU를 최소 surface 기준선으로 두고 Direct3D GPU surface를 기본으로 사용한다.
- Win32 API를 격리하고 로직은 UTF-8 `std::u8string`을 사용한다.
- 기본 Windows caption 대신 앱과 일체화된 caption UI를 제공한다.
- UWP를 제외하고 Gitman 자체를 단일 `.exe`로 배포한다.
- CMake install 결과를 `${workspaceRoot}/bin/`에 둔다.
- Git 최신 상태와 switch candidate는 remote-first, remote 없음은 local 기준으로 처리한다.
- submodule 동시 update option을 제공한다.
- 범용 스레드 메시지 구조는 구현 시점에 별도 설계 검수를 받는다.
- 이번 세션에서는 실제 구현하지 않고 후속 세션용 인수인계 문서를 작성한다.

### 반영 내용

- ADR-001, ADR-002, ADR-003을 `승인됨`으로 변경하고 검수 결정을 반영했다.
- ADR-004에 단계 6 이전의 범용 메시지 구조 설계 검수 gate를 추가했다.
- 요구사항, 계획과 단계 0 검증 기록을 같은 기준으로 갱신했다.
- `docs/handoff.md`에 후속 세션의 시작 조건, 금지 사항과 검수 지점을 기록했다.

### 다음 작업 제한

- 이번 세션에서는 코드와 build file을 추가하지 않는다.
- 후속 세션은 단계 1만 수행하고 사용자 검수 후 멈춘다.
- 단계 6의 메시지 관련 코드는 별도 설계 승인 전에는 작성하지 않는다.

## 2026-08-14 - 단계 0 결정 사항 확정

### 사용자 지시

- 모든 단계를 한 번에 수행하지 않고 한 단계씩 구현한 뒤 검수를 요청한다.
- 우선 단계 0만 진행한다.

### 반영 내용

- 단계 0을 `검수 대기`, 단계 1~8을 `시작 전`으로 기록했다.
- 플랫폼과 도구체인을 ADR-001로 기록했다.
- C++ dependency와 Codicons 고정 정책을 ADR-002로 기록했다.
- Git/SVN 실행 정책을 ADR-003으로 기록했다.
- 스레드와 상태 소유권을 ADR-004로 기록했다.
- 요구사항 기준선과 단계 0 검증 기록을 추가했다.

### 영향 요구사항

- REQ-003, REQ-005~REQ-015
- NFR-001~NFR-010

### 다음 작업 제한

사용자가 단계 0을 승인하기 전에는 단계 1의 CMake 및 source 파일을 추가하지 않는다.
