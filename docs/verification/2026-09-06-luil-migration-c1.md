# luil 이식 C1 — 빌드 준비

날짜: 2026-09-06 · 계획: `docs/luil-migration-plan.md` C1 · 설계: `docs/luil-migration-design.md` L1·L2 · 브랜치: `feature/luil-migration`

## 1. 변경 요약

luil을 저장소에 들이고 실행 파일 리소스를 luil 조각으로 나눴다. **어떤 gitman
target도 `luil::luil`을 링크하지 않는다** — gitman은 자기 Skia(148)로, luil은 자기
Skia 패키지(152)로 빌드되어 한 솔루션에 공존한다.

| ID | 내용 |
|---|---|
| C1-1 | `third_party/luil` submodule 추가, `c971f32`(2026-09-06) 고정. luil의 `third_party/nlohmann_json` init |
| C1-2 | skia-prep Debug·Release 아카이브를 `third_party/luil/third_party/skia-prep-archives/`에 두고 `fetch_skia.ps1 -Configuration Debug,Release` 실행(해시 검증 후 설치, 네트워크 없음). WebView2 패키지는 로컬 luil clone의 설치본을 복사하고 `fetch_webview2.ps1`이 "Already installed"로 검증 |
| C1-3 | 최상위 `CMakeLists.txt`: `LUIL_BUILD_TESTS/EXAMPLES/TOOLING OFF`, `add_subdirectory(third_party/luil SYSTEM)`, `LUIL_SOURCE_DIRECTORY`·`LUIL_ASSET_DIRECTORY`·`LUIL_NLOHMANN_JSON_ROOT` 재정의, `GITMAN_GENERATED_LUIL_RESOURCE` 정의, `luil_generate_third_party_notices`·`luil_configure_resource_script` 호출. `src/CMakeLists.txt`의 자체 고지 생성 호출·코디콘 경로 변수 제거. luil target을 IDE `Libraries` 폴더에 |
| C1-4 | `gitman.rc.in`에서 RCDATA 4줄 삭제(manifest·아이콘·VERSIONINFO만). `gitman`·`gitman_tests`에 `${GITMAN_GENERATED_LUIL_RESOURCE}` 추가 |
| C1-5 | `.gitattributes`에 `/third_party/luil/** -text`, `normalize_line_endings.ps1` 제외 목록에 `third_party` |
| C1-6 | `docs/build.md`·`README.md`의 준비 절차에 luil submodule·패키지 스크립트 추가. gitman 자체 Skia 절은 "C2 이전까지 필요"로 표시하고 유지 |

## 2. 구현 경계와 설계 대비 차이

- **`resource_ids.h`의 IDR 101~104를 C1에서는 남겼다** (설계 L2는 `IDI_GITMAN`만
  남기라고 했다). gitman의 `embedded_assets.cpp`·`win32_application.cpp`·
  `embedded_assets_tests.cpp`가 아직 그 매크로로 luil 조각의 리소스를 읽으므로
  지우면 C1이 빌드되지 않는다. 주석으로 "luil 조각의 거울, C2에서 삭제"를 적었다.
  실행 파일에는 luil 조각이 같은 ID로 싣는 리소스 하나씩만 있다 (gitman.rc에는
  더 이상 없다).
- 고지 파일은 luil 생성기가 만든다. C1 시점의 gitman.exe는 WebView2와 Skia 152를
  링크하지 않지만 고지에는 그 항목(Skia 152 패키지 `NOTICE.md`의 rust crate 고지
  포함, WebView2 SDK)이 이미 들어간다. C2가 곧 따라오므로 수용한다.
- luil 생성기의 고지 머리글에 `;`가 섞인다 (`set(notice_text "a" "b")`가 list를
  만드는 CMake 동작. gitman의 예전 생성기도 같았다). 표시상 사소하며 luil 요청
  목록(설계 부록 B)에 추가한다.
- WebView2 nupkg 아카이브는 로컬에 없어 `api.nuget.org` 접근 없이 설치본을
  복사했다. 새 clone 재현(C4)에서는 아카이브를 받아 두거나 같은 복사로 한다.

## 3. 자동 검증

| 항목 | 결과 |
|---|---|
| `cmake --preset vs2026` | 성공. `Skia: .../third_party/skia`(gitman), `Skia: .../luil/third_party/skia-prep (Debug, Release)`, `WebView2 SDK: .../webview2-prep` 세 줄 확인 |
| `cmake --build --preset vs2026-debug` / `vs2026-release` | 성공, 경고 0 (luil 라이브러리 포함) |
| `cmake --preset vs2026-tests` + `--build --preset vs2026-tests-debug` | 성공, 경고 0 |
| `ctest --preset vs2026-tests-debug` 전체 (**기준선**) | **837 / 837 통과**, 실패 0, skip 3 (실제 GitHub clone·실제 SVN·자격 증명 원격 — 기존과 동일). 69.5초 |
| smoke 4종 × Debug·Release (앱 preset 실행 파일 직접 실행) | 8건 모두 종료 코드 0 (`cpu`, `auto`, `auto --simulate-direct3d-failure`, `direct3d`) |
| `scripts/check_source_style.ps1 -root .` | 통과 |
| 생성물 | `build/vs2026/generated/luil_resources.rc`가 luil 자산 경로·고지 경로를 담음. 고지 `Component:` 항목에 Skia 패키지·rust crate·nlohmann/json·WebView2·Codicons 있음 |

기준선 수치: 전체 CTest 837 (asan 17, smoke 4 포함). C2-4 이후에는 삭제·재작성으로
수가 바뀐다.

## 4. 수동 확인

- 앱 preset의 Debug·Release `gitman.exe`가 smoke 모드로 창을 띄우고 종료한다
  (위 8건). 일반 실행의 화면 확인은 C1이 UI 코드를 바꾸지 않으므로 생략했다.

## 5. 알려진 제한

- 두 Skia(148·152)가 한 솔루션에 있다. C2에서 gitman의 Skia를 지우기 전까지
  configure에 `Skia:` 줄이 둘 찍힌다.
- luil의 WebView2 정적 로더는 아직 gitman.exe에 들어가지 않는다 (C2부터).

## 6. 다음 체크포인트

C2 기계적 이식 (C2-1 기반 교체 → C2-2 앱 조립 → C2-3 element 이식 → C2-4 test).
C2-1~C2-3은 단독으로 빌드되지 않을 수 있고 C2-4에서 빌드·전체 CTest를 돌린다.
