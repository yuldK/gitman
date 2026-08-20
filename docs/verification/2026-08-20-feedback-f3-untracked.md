# 실환경 피드백 F3 — 미추적 정책 완화와 문구

날짜: 2026-08-20 · 설계: `docs/field-feedback-design.md` 2.1~2.2

## 1. 변경 요약

미추적 파일 하나만 있어도 업데이트가 차단되던 경험을 고쳤다. 검수 답변대로
**update만 완화**하고 전환(switch)은 현행 차단을 유지한다.

- 도메인에 판정을 분리했다 (`repository_snapshot.{h,cpp}`):
  - `working_tree_summary::has_tracked_changes()` — 추적 중 파일의
    변경(수정·충돌·진행 중 작업·index 잠금)만 센다. 미추적 전용 트리는 false,
    조회하지 못한 `unknown`은 보수적으로 true.
  - `is_safe_for_change()`(전환용)는 그대로 — 미추적 포함 어떤 변경도 막는다.
- update preflight 교체 (`git_repository_provider.cpp` /
  `svn_repository_provider.cpp`): dirty 판정을 `state != clean`에서
  `has_tracked_changes()`로. 미추적만 있으면 pull/svn update가 진행된다.
  도구 자체 보호가 안전장치다 — Git pull은 미추적을 덮어쓰게 되면 스스로
  중단하고 트리를 보존하며, SVN update는 미버전 파일을 건드리지 않는다.
  실행 결과 도구가 중단하면 기존 실패 분류·로그 경로로 보고된다.
- 문구 (`vcs_operation.cpp`): `working_tree_dirty` 메시지를 "커밋하지 않은
  **수정**이 있어 갱신하지 않았습니다."로 조정 — 미추적이 더는 이 사유를
  만들지 않으므로 정확해졌다. 로컬 변경 확인 안내 문장은 F4(다이얼로그)에서
  덧붙인다.

## 2. 테스트 (이 단계 추가·직접 영향분만 실행)

- 추가 3 case: 도메인 `has_tracked_changes` 판정표(미추적 전용/clean/unknown/
  수정/충돌/진행 중/잠금), Git 미추적 전용 preflight 통과 + **실행 수준으로
  pull까지 진행**(fake runner), SVN 미추적 전용 preflight 통과.
- 갱신: dirty 차단 케이스들이 미추적 전용과 구분되도록 `modified_count`를
  명시(git/svn preflight 표).
- 실행: `[update],[repository],[provider],[domain]` 109 case +
  실제 `git.exe` `[integration]` 610 단정 — 전부 통과 (VS2022 Debug).
  preflight가 기존 기능이라 integration까지 제한 실행에 포함했다.

## 3. 빌드·스타일

- VS2022 Debug: `gitman_tests`, `gitman` 빌드 성공
- `check_source_style.ps1` 통과 (커밋 전 재확인)

## 4. 수동 확인 권장 항목

1. 미추적 파일만 있는 실저장소에서 업데이트가 진행되는지
2. 추적 파일을 수정한 저장소에서는 여전히 "커밋하지 않은 수정이 있어 갱신하지
   않았습니다"로 차단되는지
3. 전환(switch)은 미추적만 있어도 여전히 차단되는지 (검수 답변대로 유지)
