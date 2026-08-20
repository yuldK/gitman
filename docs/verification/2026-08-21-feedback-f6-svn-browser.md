# 실환경 피드백 F6 — SVN repo-browser

날짜: 2026-08-21 · 설계: `docs/field-feedback-design.md` 5장

## 1. 변경 요약

문서의 `svn_switch_targets`를 평면 후보로 보여 주던 SVN 전환 경로를 저장소
실조회 기반 lazy 트리 브라우저로 교체했다. Git 전환 다이얼로그는 기존 동작을
유지한다.

## 2. 구현 경계

- `svn_command_builder`: `svn ls <url>` 요청. 공통 `--non-interactive`,
  `remote_query` timeout/capture 제한을 사용하고 recursive·인증·인증서 신뢰
  인자를 만들지 않는다.
- `svn_output_parser`: 기본 `svn list` 출력에서 `/` 접미사가 있는 디렉터리만
  남긴다. fixture는 Apache Subversion 명령 문서의 `README.txt`/`examples/`
  출력 계약을 근거로 작성했고 실제 출력과 대조하지 않았음을 `#` 주석으로
  남겼다.
- `svn_repository_browser`: URL별 node cache와 펼침/loading/loaded/failed,
  current/selected URL, 현재 경로 자동 확장, 표시 행 flatten을 process 없는 순수
  함수로 소유한다. 자식 이름은 UTF-8 URL path component로 percent-encode한다.
- executor/logic: 초기 `query_switch_candidates`는 SVN일 때 root/current URL만
  돌려주고, `query_svn_directory`가 노드별 목록을 돌려준다. logic은 열린
  다이얼로그의 operation id만 적용해 닫힌 이전 다이얼로그의 늦은 결과를 버린다.
- UI: directory 행과 expand 글리프가 서로 다른 hit target이다. loading/error는
  부모 아래 상태 행이고 현재 위치는 별도 강조한다. 최소 창 높이에서도 목록,
  메시지와 하단 버튼이 겹치지 않도록 list viewport를 panel 높이에 맞춘다.
- 검증: 문서 허용 목록 비교를 제거하고 URL 형식·작업 트리 보호를 유지했다.
  `svn switch` 직전의 remote root/UUID 비교는 그대로라 다른 저장소 URL은 실행되지
  않는다.

## 3. 자동 검증

- 앱/테스트 Debug 대상 컴파일 성공 (VS2026).
- 직접 영향 태그 `[svn],[switch-ui],[executor],[workspace],[switch]`: 159 case
  중 158 통과 / 실제 SVN 실행 1 skip, 1,475 assertions 전부 통과. 신규 SVN
  builder/parser/provider/executor/logic/UI, 기존 Git 후보·tracking branch 흐름,
  `svn_switch_targets` parse/save 보존을 함께 포함한다.
- `gitman` Debug 앱 빌드 성공, CPU/auto renderer smoke test 통과.
- `check_source_style.ps1`: 394 files 통과. `git diff --check`도 오류 없음.

## 4. 남은 실환경 확인

1. 실제 SVN 1.14에서 비verbose `svn ls`가 디렉터리에 `/`를 붙이는지 fixture와
   byte 단위로 대조
2. 공백·한글·percent 문자가 있는 디렉터리 URL 전환
3. 인증 cache가 없는 private 노드에서 prompt 없이 즉시 실패하고 노드 오류 행이
   표시되는지 확인
4. 큰 저장소에서 lazy 요청 수, 600초 기본 제한과 노드 cache 재사용 확인
