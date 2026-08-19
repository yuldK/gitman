# Gitman 배포 안내

단일 실행 파일 `gitman.exe`의 배포, 설치, 업그레이드, 제거와 사용자 데이터
위치를 정리한다 (REQ-013, REQ-016, stage-8-plan 5.7). 빌드 절차는
[build.md](build.md), 검증 기록은 `verification/`에 있다.

## 1. 지원 환경과 prerequisite

| 항목 | 기준 |
| --- | --- |
| 운영체제 | Windows 11 x64 |
| 그래픽 | Direct3D 12 기본, 실패 시 CPU 래스터 자동 전환 (`--renderer=cpu`로 명시 선택 가능) |
| Git | 2.43.0 이상 CLI (선택. 없으면 Git 카드의 조회·변경만 비활성) |
| SVN | 1.14.5 이상 CLI (선택. 동일) |

Git/SVN CLI는 단일 파일 범위에서 제외된 유일한 외부 prerequisite다. 실행 파일
경로는 자동 탐색되며, 환경설정 화면에서 문서별로 수동 지정할 수 있다 (REQ-017).
인증은 각 CLI에 이미 구성된 credential helper만 사용하고 앱은 자격 증명을
저장하지 않는다 (ADR-003).

## 2. 설치

배포 산출물은 `bin/gitman.exe` 파일 하나다 (Skia·C/C++ runtime 정적 연결,
Codicons font·앱 아이콘·제3자 고지문 resource embed). 설치는 이 파일을 원하는
위치에 복사하는 것으로 끝난다. installer, MSIX, 레지스트리 필수 항목이 없다.

산출물 생성은 저장소에서 다음으로 한다 ([build.md](build.md) 5장).

```powershell
cmake --install build\vs2022 --config Release
```

## 3. `.version-list` 연결 프로그램 (REQ-016)

double-click으로 문서를 열려면 연결을 등록한다. **현재 사용자(HKCU) 범위**라
관리자 권한이 필요 없다.

```powershell
bin\gitman.exe --register-file-association
```

- 같은 작업을 환경설정 화면의 `연결 등록` 버튼으로도 할 수 있다.
- 제거는 `--unregister-file-association` 또는 `연결 해제` 버튼이다. 확장자
  연결이 다른 앱으로 바뀐 뒤에는 그 연결을 건드리지 않는다.
- 등록·제거 모두 멱등이며, exe를 다른 위치로 옮겼다면 등록을 다시 실행해
  경로를 갱신한다.
- Explorer의 "연결 프로그램" 사용자 선택(UserChoice)은 HKCU 연결보다 우선하는
  Windows 정책 영역이다.

## 4. 사용자 데이터 위치

앱은 자체 설정 저장소를 두지 않는다. 모든 상태는 사용자가 만든 `.version-list`
문서에 있다.

| 데이터 | 위치 |
| --- | --- |
| 프로젝트 목록·문서 설정(Git/SVN 경로, 경로 표시 방식)·창 배치 | 사용자가 만든 `<이름>.version-list` (UTF-8 JSON, 스키마 버전 1) |
| 저장 backup | 같은 폴더의 `<이름>.version-list.bak` (저장 시 원자적 교체의 이전본) |
| 작업 로그 | 메모리 전용 (카드당 1,000 record ring buffer). 파일 로그는 없다 |
| file association | `HKCU\Software\Classes`의 `.version-list`·`Gitman.VersionList` (등록한 경우만) |

## 5. 업그레이드

`gitman.exe`를 새 버전으로 덮어쓰는 것으로 끝난다.

- 문서 스키마는 버전 1이 유지된다. 구버전이 만든 문서는 그대로 열리고, 알 수
  없는 필드는 항목별 진단으로 보고된다 (단계 2 계약).
- exe 경로가 바뀌지 않았다면 file association도 그대로 유효하다. 경로가
  바뀌었으면 `--register-file-association`을 다시 실행한다.

## 6. 백업과 복구

- 저장은 임시 파일 작성 → flush → 원자적 교체 순서이며, 교체 직전 원본이
  `.bak`으로 남는다. 저장 중 중단돼도 원본 또는 backup이 보존된다.
- 문서가 손상되면 앱이 항목별 오류를 표시하고 원본을 덮어쓰지 않는다. 유효한
  `.bak`이 있으면 "유효한 backup이 있습니다" 진단이 표시되며, 복구는 `.bak`
  파일을 명시적으로 열고 저장하는 방식이다 (자동 복구는 하지 않는다).
- 다른 프로그램이 문서를 동시에 수정하면 저장 시 revision 비교로 감지해 충돌
  오류를 표시하고 덮어쓰지 않는다.

## 7. 제거

1. (등록했다면) `bin\gitman.exe --unregister-file-association`
2. `gitman.exe` 삭제
3. `.version-list` 문서와 `.bak`은 사용자 소유 데이터이므로 필요 시 직접 삭제

이외의 잔여물(레지스트리, AppData, 서비스)은 없다.

## 8. 제3자 고지

Skia, nlohmann/json, VS Code Codicons(font·mapping)의 라이선스 고지문이 실행
파일 resource(`IDR_THIRD_PARTY_NOTICES`, `IDR_CODICONS_LICENSE*`)에 embed된다.
고지문은 submodule의 라이선스 원문에서 빌드 시 생성되며, 의존성 목록과의
일치는 `dependency_versions_tests`·`embedded_assets_tests`가 검사한다. Catch2는
test 전용이라 배포 산출물에 포함되지 않는다.

## 9. 알려진 제한

- **SVN 실측 미수행**: SVN 경로는 fixture·fake runner 검증까지가 보증 범위다
  (2026-08-19 사용자 결정: 실서비스 적용 시 검증). 실제 환경에 붙일 때 확인할
  4항목은 [handoff.md](handoff.md) 8.6에 있다.
- **네트워크 드라이브·느린 경로**: 탐색·조회를 검증하지 못했다 (재현 수단
  없음). 탐색은 자식 경계 취소가 가능하고 조회는 명령별 제한 시간이 있다.
- 자격 증명이 저장된 호스트에서는 존재하지 않는 원격 접근이 인증 요구가 아닌
  "저장소를 찾을 수 없습니다"로 표시된다 (credential helper가 응답하기 때문,
  `verification/2026-08-19-stage-8-d5.md` 3장).
- 커밋·푸시·병합·리베이스·충돌 해결, 대화형 셸, 자격 증명 저장, 깊이 2 이상
  탐색은 범위 밖이다 (plan 2.2).
