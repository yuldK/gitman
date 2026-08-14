# 변경 이력

## 2026-08-14 - 단계 2 schema parser test 작성

### 사용자 지시

- `S2-D2-CODE`를 승인하고 다음 작업을 진행한다.

### 반영 내용

- 정상, 손상, 부분 성공, 이전 및 미래 version과 unknown field fixture 6개를 추가했다.
- schema parser 계약 test 8개와 project field 오류 matrix 12종을 추가했다.
- unknown field의 JSON pointer escape와 입력 JSON byte의 정확한 shadow 보존을 검증했다.
- `.verison-list` test asset도 UTF-8 무 BOM 및 CRLF 검사를 받도록 품질 도구 범위를 확장했다.
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
- schema version 1의 `.verison-list` JSON parser와 구조화 parse result를 추가했다.
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

## 2026-08-14 - `.verison-list` 작업공간 문서 결정 및 단계 2 계획 승인

### 사용자 지시

- 프로젝트 목록은 고정 `projects.json`이 아니라 solution 및 `.code-workspace`와 같은 문서로 취급한다.
- 확장자는 사용자 지시의 철자 그대로 `.verison-list`를 사용한다.
- Gitman을 해당 확장자의 Windows 연결 프로그램으로 동작하게 한다.
- 나머지 단계 2 계획을 승인하고 추가 승인 요청 없이 첫 production code 구현을 진행한다.

### 반영 내용

- 고정 기본 config 위치를 제거하고 한 창당 하나의 `.verison-list` 활성 문서로 변경했다.
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
