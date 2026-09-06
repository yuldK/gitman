# luil 이식 최종 검증 (C4)

날짜: 2026-09-06 · 계획: `docs/luil-migration-plan.md` C4 · 설계: `docs/luil-migration-design.md` · ADR: `docs/decisions/ADR-007-luil-ui-framework-adoption.md` · 브랜치: `feature/luil-migration`

체크포인트 기록: [C1](2026-09-06-luil-migration-c1.md) · [C2](2026-09-06-luil-migration-c2.md) · [C3](2026-09-06-luil-migration-c3.md)

## 1. 변경 요약

gitman의 UI 프레임워크 사본(Skia 직접 빌드 포함)을 지우고 앱을 luil submodule(`c971f32`) 위에 올렸다. 커밋 12개다.

| 구간 | 커밋 | 내용 |
|---|---|---|
| 설계 | `e6aeeb4` | 설계·계획서·ADR-007 |
| C1 | `70288c3` | luil submodule, 실행 파일 리소스를 luil `.rc` 조각(ID 101~104) + gitman(105~)으로 분리 |
| C2 | `48aacbf` `e1ac361` `6a8887c` `2411c0b` `956430e` | 기반 계층 삭제와 luil 채택, driver·policy·delegate 조립, element 이식, test 정리, C2 기록 |
| C3 | `72760d3` `f411fce` `d435a9b` `160424f` | Tab 초점 정책(D1-B), dialog `modal_host`(D2-A), 컨텍스트 메뉴 popup(D3-A), IME 점진 검색·hover 정리(D5) |
| C4 | (이 기록을 담은 커밋) | 문서·ADR 갱신, 최종 검증 |

앱에 남은 UI 코드: `src/presentation/ui/`(앱 element 28쌍 + `dialog_host.h`·`context_menu_popup`·`element_kinds.h`·`ui_common.h`·`ui_actions.h`·`gitman_policy`), `src/platform/win32/{main.cpp, gitman_driver, gitman_delegate, operation_result_relay}`, `src/application/cancellation_registry`. 삭제: Skia submodule 8개·GN args·패치·`build_skia.ps1`·`verify_skia_root.ps1`·`skia.cmake`·코디콘 자산과 생성기·프레임워크 파일 약 60쌍.

## 2. 구현 경계와 설계 대비 차이

체크포인트 기록의 2장을 모은다. 요점:

- **luil은 바꾸지 않았다** (D4-B). 사용자가 미리 반영한 `c971f32`(글꼴 공개 API·drag 강조 opt-out·`LUIL_DEFAULT_ACCENT_ID`)로 설계의 우회가 없어졌다. 남은 요청은 설계 부록 B (5장).
- **모양이 바뀐 곳** (모두 설계 5장·L5가 수용): dialog scrim 색(검정 0.45 → 팔레트 `content_shadow` 0.45), 메뉴 popup(여백 5 → 4, luil 메뉴 색, 모니터 작업 영역 안으로 다듬기, 우클릭 흡수 없음), 행 hover 색(`primary_foreground` 0.06 → 팔레트 hover/pressed), Tab 순회(버튼·dialog 컨트롤·텍스트 칸만), dialog 초점 가둠, Esc 우선순위(tree 순서), 조합 중 점진 검색.
- **스크롤 값이 논리 픽셀**이고 스크롤 막대는 `kind_scrollbar` 하나 + owner 문자열이다 (L4).
- **relay·종료**: `operation_result_relay`가 결과 큐의 진실이고 driver `cancel()`이 `cancellation_registry`로 취소를 건 뒤 close만 통과시킨다 (L3).
- **C2-2 커밋에 test 삭제 12개가 섞였다** (staging 실수, C2 기록).
- **Skia 작업 트리 백업**: `E:\Programming\Lusain\gitman-skia-backup\` (저장소 밖, 필요 없으면 삭제).

## 3. 자동 검증

| 항목 | 결과 |
|---|---|
| `cmake --preset vs2026` / `vs2026-tests` / `vs2026-analysis` | 성공 (`Skia:` 줄이 luil 패키지 하나, WebView2 SDK 경로 인식) |
| `cmake --build --preset vs2026-debug` / `vs2026-release` | 성공, 오류 0·경고 0 (Debug 20:06~, Release 20:01~20:05) |
| `cmake --build --preset vs2026-tests-debug` + `ctest` 전체 | **812 / 812** 통과, skip 3 (실제 GitHub·SVN·자격 증명 원격), 72초 |
| `cmake --build --preset vs2026-tests-release` + `ctest` 전체 | **812 / 812** 통과, skip 3, 61초 |
| `cmake --build --preset vs2026-analysis` | 성공 (20:02~20:14), 분석 경고 0 — 로그의 "error" 일치는 `win32_error.cpp`·`winhttp_error.cpp`·`vcs_error_classifier.cpp` 파일 이름뿐 |
| `scripts/check_source_style.ps1 -root .` | 통과 (429 files) |
| smoke 4종 (`--renderer=cpu`·`auto`·`auto` + 실패 주입·`direct3d`) | ctest에 포함, Debug·Release 통과 |
| `cmake --install build\vs2026 --config Release` → `bin/gitman.exe` | 성공 — `bin/gitman.exe` 8,757,760 B (20:05), 설치본 smoke cpu·auto 종료 0 |
| 기준선 대비 test 수 | C1 837 → C2 774 → C3 812 (삭제 11 파일·재작성 축소·신설 7 파일, C3에서 사례 38 추가) |

## 4. 새 clone 재현 (준비 절차)

`scratchpad/fresh_clone.ps1`로 저장소를 새 폴더에 clone하고 문서(`docs/build.md` 2장) 순서 그대로 준비·구성·빌드·설치·smoke를 돌렸다.

| 경로 | 결과 |
|---|---|
| **온라인** (`gitman-fresh-online`) | clone → submodule 4개(`nlohmann-json`·`luil`·`luil/nlohmann_json`·`catch2`) → `fetch_skia.ps1 -Configuration Debug,Release`가 GitHub 릴리스에서 Debug·Release 아카이브를 받아 설치(1,004 MB) → `fetch_webview2.ps1`가 nuget에서 SDK(13 MB) → `cmake --preset vs2026`(Skia·WebView2 경로 인식) → Release 빌드 → install → `--renderer=cpu --smoke-test` 종료 0. 9분 |
| **오프라인** (`gitman-fresh-offline`, 아카이브를 미리 `skia-prep-archives/`·`webview2-prep-archives/`에 복사) | fetch 두 스크립트가 `cached`만 내고 `downloading` 줄이 없다 → configure(Skia·WebView2 경로 인식) → Release 빌드 → install → smoke 종료 0. 6분. 실행 파일 크기가 온라인 경로와 같다(8,754,688 B) |
| 빌드 로그의 네트워크 취득 | configure·build 로그에 URL·다운로드 줄 없음 (취득은 fetch 스크립트 두 번뿐) |

빌드 경고: 새 clone이 `%TEMP%` 아래라 MSBuild가 MSB8029(임시 디렉터리 아래 출력)를 내지만 코드 경고는 0이다.

## 5. 산출물

| 항목 | 값 |
|---|---|
| `bin/gitman.exe` 크기 | 이식 전(2026-08-24 빌드) 7,117,312 B → 이식 후 8,757,760 B (새 clone의 C3-3 시점 빌드는 8,754,688 B; WebView2 정적 로더·luil의 HTTP·이미지·팝업 계층이 링크된다; 부록 B의 `LUIL_ENABLE_WEBVIEW=OFF`가 오면 줄어든다) |
| 제3자 고지 (`IDR_THIRD_PARTY_NOTICES`) | luil 생성기가 만든다 (`build/vs2026/generated/third_party_notices.txt`, Component 69개): Skia와 번들 구성 요소(skcms·SPIRV-Cross·SPIRV-Headers·D3D12MA·libjpeg-turbo·libwebp·Wuffs·Rust crate) + WebView2 SDK + nlohmann/json + Codicons. 머리글에 CMake list `;` 잔재 (부록 B) |
| 코디콘 자산 | luil의 `v0.0.46-24` (gitman 자산 삭제) |

## 6. 수동 확인

C2·C3 기록의 시나리오(문서 열기·카드 선택·로그 pane·F5·환경설정 dialog·경로 토글·Esc 해제·창 닫기)에 더해 이번에 컨텍스트 메뉴 popup(우클릭 자리에 6항목, ↓↓ 강조 이동, Esc 닫힘)을 C3-4 뒤 Debug 빌드로 확인했다. 캡처는 `scratchpad/drive5/`.

하지 못한 것(자동 입력으로 재현하지 않음): 파일 dialog로 문서 열기·드롭·카드 drag 순서 변경·로그 복사·switch Git/SVN 실행·글꼴 dialog·IME 조합·붙여넣기·창 배치 복원·DPI 변경·Alt+F4. intent 수준 test가 덮고, 화면 확인은 사용자 검수에서 마저 본다.

## 7. luil에 남긴 요청 (설계 부록 B)

- `LUIL_ENABLE_WEBVIEW=OFF` (WebView2 준비·링크·고지 제외)
- fetch 스크립트 `-Offline` (아카이브 없으면 네트워크 시도 없이 안내)
- 시작 실패 메시지 상자의 제목·본문 훅
- `LUIL_WARNINGS_AS_ERRORS` (`/WX` 끄기)
- (낮음) `text_input_config` 지우기 버튼 글리프
- (낮음) 고지 생성기의 `set(notice_text "a" "b")` list `;` 잔재
- (낮음) `menu_element`의 `menu_config::owner`를 채우면 controller `find_menu`가 메뉴를 못 찾는다 — 문서화하거나 `find_menu`가 owner를 무시하게

## 8. 알려진 제한

- 행 hover 색은 화면으로 확인하지 않았다 (C3 기록 4장).
- delegate의 외부 열기 뒤 메뉴 닫기 게시는 test가 없다 (실제 셸 호출 경로).
- smoke 모드에서도 driver를 만들어 worker pool·로그 writer thread가 잠깐 선다.
- 커스텀 element의 접근성(`accessibility()`)과 스크롤 막대의 UIA `SetValue`는 후속이다 (설계 5장).
