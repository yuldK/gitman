# 검증 기록 - 범위 배지 · 외양 초안 · 색 격자 · 문서 닫기 배치 복원

- 일자: 2026-08-22
- 대상: `feature/settings-badge-and-appearance-draft` 브랜치 (D1~D5)
- 기준 문서: `docs/settings-tabs-and-appearance-scope-design.md` "후속 요구 (D1~D4)"
- 진행 방식: 사용자 지시로 체크포인트 검수 없이 브랜치에서 커밋까지 진행하고
  마지막에 전역 test를 실행했다.

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2022-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2022-tests-debug` | **757/757 통과** |
| `cmake --build --preset vs2022-tests-release` | 성공 |
| `ctest --preset vs2022-tests-release` | **757/757 통과** |
| `cmake --build --preset vs2022-analysis` | 성공, 분석 경고 없음 |
| `cmake --build --preset vs2022-release` (앱) | `build/vs2022/src/Release/gitman.exe` 생성 |
| `scripts/check_source_style.ps1 -root .` | 440 files 통과 |
| clang-format (변경 파일) | 위반 없음 |

신규 test 3 case.

- `Closing the document restores the app window placement`
- `Cancelling the settings dialog drops the appearance draft`
- `The appearance draft lands in the document on save` (기존 문서 범위 test를
  초안 규칙으로 다시 씀)

규칙이 바뀌어 고쳐 쓴 test.

- `Theme intents update the app settings and request a save` — dialog 밖 클릭은
  버리고, `저장` 뒤에만 앱 설정과 화면에 반영된다.
- `Appearance clicks edit the draft and need the save button`
- `Every document item shows a scope badge and only overrides are clickable`
- `The scope badge sits at the right end of the item title line`
- `The appearance items show the effective values and a wrapped swatch grid`

## 2. 화면 확인

tree 빌드 → Skia raster surface로 그려 확인했다(확인용 임시 코드는 커밋하지
않았다).

- `도구`(문서·다크): Git 항목에 강조색 `문서 설정`, SVN·제한 시간에 흐린
  `전역 설정` 배지가 제목 줄 오른쪽 끝에 붙는다. 제목은 왼쪽 여백 없이 시작하고
  찾아보기·지우기 버튼이 배지 열만큼 왼쪽으로 밀린다.
- `외양`(문서·다크): 25색이 5 × 5 격자이며 줄마다 한 계열이다(블루·퍼플 /
  퍼플·핑크 / 레드·오렌지 / 앰버·그린 / 그린·스카이). 선택한 색만 링이 있다.
- `외양`(전역·라이트): 배지가 없고 세그먼트가 오른쪽 끝에 붙는다.
- panel은 600 × 428이며 탭을 옮겨도 크기가 같다.

## 3. 손으로 확인할 항목 (남음)

실제 창이 필요해 자동 검증에 포함하지 못했다.

- 문서를 닫으면 창이 앱 설정의 크기·위치로 돌아가는지(앱 설정에 배치가 없으면
  창을 건드리지 않는다).
- 테마를 고르고 `취소`로 닫으면 화면 색이 그대로인지, `저장`을 누르면 즉시
  바뀌는지.
- 고대비 테마에서 `전역 설정` 배지의 대비.
