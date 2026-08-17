# Gitman 요구사항 기준선

## 1. 문서 상태

- 기준일: 2026-08-17
- 대상 단계: 단계 0~6
- 상태: 단계 0~5 완료, 단계 6 최종 검증(`S6-V1`) 사용자 검수 대기 (수동 checklist 포함)
- 상위 계획: `docs/plan.md`, `docs/stage-6-plan.md`

이 문서는 단계별 구현과 검수에서 사용하는 요구사항 기준선이다. 요구사항의 추가, 변경, 폐기는 `docs/change_log.md`에 사유와 영향 범위를 기록한다.

## 2. 제품 범위

Gitman은 Windows 11에서 실행되는 x64 Win32 네이티브 C++ 애플리케이션이다. JSON 형식의 `.version-list` 작업공간 문서에 등록된 Git 및 SVN 작업 경로의 상태를 조회하고, 명시적인 refresh, 안전한 update 및 switch, 카드별 읽기 전용 로그를 Skia GUI로 제공한다.

초기 버전은 커밋, 푸시, 병합, 리베이스, 충돌 해결, 자동 stash, 강제 전환, 터미널 입력, 자격 증명 저장을 제공하지 않는다.

## 3. 기능 요구사항

| 식별자 | 요구사항 | 단계 0 결정 | 수용 기준 |
| --- | --- | --- | --- |
| REQ-001 | JSON 형식의 `.version-list` 프로젝트 목록을 읽는다. | 스키마 버전과 항목별 오류를 사용한다. | 일부 항목이 잘못되어도 유효 항목을 표시한다. |
| REQ-002 | Git/SVN 상태를 공통 모델로 표시한다. | Git은 remote target을 우선하고 remote가 없을 때 local을 기준으로 한다. | 최신 판단 기준 ref와 remote/local source, 마지막 확인 시각을 표시한다. |
| REQ-003 | Skia GUI를 제공한다. | Windows 11 x64 Win32, Direct3D 기본, CPU fallback, custom caption을 사용한다. | 두 renderer와 일체형 caption이 DPI 및 창 상태 변경에서 정상 동작한다. |
| REQ-004 | 바로 아래 자식 저장소를 발견하고 등록한다. | 깊이 1만 검사한다. | 후보 미리보기와 선택 등록을 제공한다. |
| REQ-005 | 프로젝트를 가로형 카드로 표시한다. | Codicons `v0.0.46-24`를 사용한다. | 아이콘, 색상, 한국어 툴팁으로 상태를 구분한다. |
| REQ-006 | Git pull과 SVN update를 실행한다. | Git은 `--ff-only`와 optional recursive submodule update를 제공한다. | 위험 상태와 인증 필요 상태에서 작업을 시작하지 않고 submodule 선택을 로그로 추적한다. |
| REQ-007 | Git/SVN switch를 제공한다. | Git은 remote branch를 먼저, local branch를 다음에 조사하며 SVN은 JSON 허용 URL만 지원한다. | dialog 검증 실패 시 오류를 표시하고 명령을 생성하지 않는다. |
| REQ-008 | 선택 카드 전용 하단 로그를 표시한다. | 카드별 ring buffer와 sequence를 사용한다. | 병렬 작업 중에도 선택 카드의 로그만 순서대로 보인다. |
| REQ-009 | C++과 Skia를 사용한다. | C++20과 Skia 148을 사용한다. | UI와 무관한 계층에 Skia 타입이 노출되지 않는다. |
| REQ-010 | 코드 및 파일 규칙을 지킨다. | 상세 기준은 `docs/code_style.md`에 따른다. C++, CMake, PowerShell, JSON, XML 및 resource 코드에 공백 4칸을 적용하고, Allman 중괄호, namespace 내부 1단계 들여쓰기, `snake_case`, UTF-8, CRLF, 가능한 중괄호 초기화, 단일 문장 제어문의 중괄호 생략, 조건 연산자의 새 줄 시작, 생성자 초기화 목록의 줄바꿈 쉼표와 긴 표현식의 닫는 기호 줄 분리를 적용한다. | 자동 검사와 코드 리뷰에서 위반이 보고된다. |
| REQ-011 | 설명과 주석을 한국어로 작성한다. | 외부 API와 명령 고유 명칭은 원문을 허용한다. | 이유와 제약을 설명하는 주석과 문서가 한국어다. |
| REQ-012 | 지시와 검증을 문서화한다. | ADR, 단계별 검증 기록, 변경 이력을 사용한다. | 요구사항에서 결정과 검증 결과를 추적할 수 있다. |
| REQ-013 | CMake로 빌드하고 설치한다. | CMake 4.2.0 이상과 공유 preset 및 install rule을 사용한다. | configure, build, test, install 후 `${workspaceRoot}/bin/gitman.exe`가 생성된다. |
| REQ-014 | 명시적인 refresh를 제공한다. | 시작 시 로컬만, 전체 또는 카드 refresh에서 원격을 조회한다. | 중복 refresh가 병합되고 진행 및 오류 상태가 표시된다. |
| REQ-015 | 입력, UI, 로직 스레드를 분리한다. | mutable app state는 logic thread만 소유한다. | UI와 input thread가 직접 업무 상태나 VCS를 변경하지 않는다. |
| REQ-016 | `.version-list`를 solution과 같은 작업공간 문서 및 Windows 연결 프로그램 대상으로 제공한다. | 한 프로세스 및 창은 하나의 활성 문서를 열고 shell에서 전달된 경로를 사용한다. | 문서 double-click으로 Gitman이 실행되어 해당 문서를 열고 association 등록 및 제거가 가능하다. |
| REQ-017 | 문서 수준 환경설정을 `.version-list`의 `settings`에 저장한다. | 2026-08-16 사용자 지시로 추가했다. 단계 4는 Git 및 SVN 실행 파일 경로만 다루고 환경설정 화면은 단계 6~7이 담당한다. | `settings`가 없어도 문서가 열리고, 지정한 절대 경로가 자동 탐색보다 우선하며, 저장 후에도 값과 알 수 없는 키가 보존된다. |

## 4. 비기능 요구사항

| 식별자 | 요구사항 | 기준선 |
| --- | --- | --- |
| NFR-001 | 플랫폼 | Windows 11 x64 데스크톱 |
| NFR-002 | 언어 | C++20, MSVC 19.40 이상 |
| NFR-003 | 빌드 | CMake 4.2.0 이상, Visual Studio 2022 17.10 이상 |
| NFR-004 | Windows SDK | 10.0.22621 이상 |
| NFR-005 | 문자 처리 | 로직 `std::u8string`, 내부 UTF-8, Win32 adapter 경계 UTF-16, 소스 UTF-8 무 BOM |
| NFR-006 | 줄 끝과 스타일 | `docs/code_style.md` 및 `.clang-format`에 정의한 Allman 중괄호, namespace 1단계 들여쓰기, CRLF, 공백 4칸, `snake_case`, template/signature 줄 분리, 중괄호 초기화, 단일 문장 제어문의 중괄호 생략, 조건 연산자의 새 줄 시작, 생성자 초기화 목록의 새 줄 쉼표, 긴 표현식의 닫는 기호 독립 줄 |
| NFR-007 | 외부 명령 | shell 미사용, 인자 배열, timeout, 취소, 비대화형 |
| NFR-008 | 보안 | 자격 증명 미저장, 민감 로그 마스킹, 강제 변경 명령 제외 |
| NFR-009 | 반응성 | UI/input thread에서 파일, 네트워크, 프로세스 I/O 금지 |
| NFR-010 | 재현성 | vcpkg baseline, Codicons 태그 및 커밋, 자산 checksum 고정 |
| NFR-011 | 배포 | Gitman dependency와 asset을 포함한 단일 `gitman.exe`; Git/SVN CLI는 외부 prerequisite |
| NFR-012 | 렌더링 | Direct3D 기본, CPU 명시 선택 및 자동 fallback |
| NFR-013 | 창 UI | Skia custom caption과 Windows 11 drag, resize, system menu, Snap Layout 호환 |
| NFR-014 | 메시지 재사용 | 범용 메시지 구조는 단계 6 구현 전 별도 사용자 설계 승인을 받아야 함 |

## 5. 단계 0 완료 기준

- 플랫폼, 아키텍처, 언어, 컴파일러, CMake 4.2.0 최소 버전이 ADR로 기록되어 있다.
- Skia, JSON, 테스트 프레임워크, Codicons의 취득 및 고정 방식이 기록되어 있다.
- Git/SVN 최소 버전, 미설치 UX, refresh, update, switch, 인증 정책이 기록되어 있다.
- 스레드와 app state 소유권 및 범용 메시지 설계 검수 게이트가 기록되어 있다.
- 로컬 환경 확인 결과와 아직 실행하지 않은 검증이 구분되어 있다.
- 이번 세션에서는 단계 1 파일과 실제 구현을 만들지 않고 `docs/handoff.md`로 인계한다.

## 6. 후속 단계로 이관한 항목

- 프로젝트 목록은 고정 기본 위치 없이 여러 `.version-list` 작업공간 문서로 둘 수 있으며, 한 프로세스 및 창은 하나의 문서를 활성화한다.
- junction, 심볼릭 링크, Git worktree, bare repository 세부 범위는 단계 5에서 확정했다. linked worktree와 submodule(`.git` 파일)은 탐색 후보로 허용, bare 저장소는 표시 후 등록 제외, reparse point는 판정 없이 제외하되 목록에 표시한다. 상세는 `docs/stage-5-plan.md` 4.3~4.4다.
- worker 동시 실행 상한과 로그 크기는 단계 6~7의 성능 측정 후 확정한다.
- 범용 스레드 메시지 component의 API와 queue topology는 단계 6 직전 사용자 검수로 확정한다.
