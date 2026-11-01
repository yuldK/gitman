# ADR-007: luil 채택과 Skia·WebView2 패키지 취득

## 상태

승인됨 - 2026-09-05 설계 검수(D1~D8)에서 채택, 2026-09-06 계획서 승인으로 구현 시작, 같은 날 구현 완료 (`feature/luil-migration`, 최종 검증 `docs/verification/2026-09-06-luil-migration.md`). 설계 문서는 [luil-migration-design.md](../luil-migration-design.md)다.
ADR-006의 Skia 취득 절(submodule + 직접 GN 빌드)과 ADR-002의 Skia 행을
이 문서가 대체한다. ADR-006의 원칙(빌드 체계는 네트워크를 쓰지 않는다, 준비는
사람이 한다)과 Catch2·단일 경로·고지 규칙은 그대로 유효하다. ADR-005의 ASan
검증 조항은 luil 저장소의 CI로 위임한다 (아래 결정 8).

## 배경

gitman의 UI 계층(불변 element tree, 스레드 경계 messaging, Win32 창·Skia 렌더러,
TSF·클립보드·글꼴)은 skia-ui를 거쳐 luil(https://github.com/yuldK/luil)이라는
독립 라이브러리가 됐다. 같은 코드가 두 저장소에 있고 luil 쪽만 발전하고 있다
(키보드 초점, 접근성, popup 표면, 세션 종료 저장, 이미지, HTTP, WebView2).
gitman이 자기 사본을 유지하면 개선을 받지 못하고 Skia 빌드도 스스로 감당해야
한다.

Skia 취득 측면에서 ADR-006의 구성은 submodule 10개(그중 ICU는
`chromium.googlesource.com`에만 있어 대상 환경의 접근이 미확인), 해시 검증이 없는
`gn`·`ninja` 바이너리, 2 GB 작업 트리, 수 시간의 GN 빌드를 요구한다. luil은
사전 빌드 Skia 패키지(skia-prep)와 WebView2 SDK 패키지를 SHA-256으로 고정해 받는
스크립트를 제공한다.

## 결정

1. gitman은 luil을 `third_party/luil` git submodule로 두고 커밋을 고정한다
   (초기 핀 `c971f32fe451275192d08e9066f12aa9e4ec3116`, 2026-09-06, 0.1.0 + 글꼴 공개
   API·drag 강조 opt-out). gitman의 UI 단
   코드는 luil 공개 API(`include/luil/**`)만 쓴다. 중복되던 프레임워크 코드는
   삭제한다. UI와 무관한 계층은 luil의 값 타입 헤더(`luil/text`, `luil/theme`,
   `luil/messaging`, `luil/win32/renderer_policy.h`)만 include한다.
2. Skia는 gitman이 직접 빌드하지 않는다. `third_party/skia`·`skia-externals/*`·
   `skia-args`·`patches`·`scripts/build_skia.ps1`·`scripts/verify_skia_root.ps1`·
   `cmake/dependencies/skia.cmake`를 삭제한다. Skia 헤더·정적 라이브러리·고지는
   `third_party/luil/scripts/fetch_skia.ps1`이 설치하는 skia-prep 패키지
   (`third_party/luil/third_party/skia-prep.json`이 판번·SHA-256을 고정; 현재
   Skia milestone 152, commit `0873ec164a06…`, rust png 코덱)에서 온다.
3. WebView2 SDK 패키지(`fetch_webview2.ps1`, `webview2-prep.json`, 1.0.4191.47)도
   준비한다. luil이 모든 라이브러리 구성에서 요구하며 opt-out이 없다. gitman은
   WebView2를 호출하지 않지만 정적 로더가 실행 파일에 들어가고 그 고지가 실행
   파일 고지에 포함된다. Evergreen Runtime은 필요 없다 (단일 `.exe` 원칙 유지).
4. 준비 절차 (사람이 실행한다. CMake·CTest는 네트워크를 쓰지 않는다):

   ```powershell
   git submodule update --init third_party/nlohmann-json          # 모든 구성 필수
   git submodule update --init third_party/luil
   git -C third_party/luil submodule update --init third_party/nlohmann_json
   third_party\luil\scripts\fetch_skia.ps1 -Configuration Debug,Release
   third_party\luil\scripts\fetch_webview2.ps1
   git submodule update --init third_party/catch2                 # test 구성일 때만
   ```

   제한된 환경에서는 브라우저로 아카이브 3개를 받아
   `third_party/luil/third_party/skia-prep-archives/`와
   `webview2-prep-archives/`에 두고 같은 스크립트를 실행한다. 스크립트는 해시가
   맞는 아카이브가 있으면 네트워크를 쓰지 않는다. 새로 확인할 접근 대상은
   `github.com/yuldK/skia-prep` 릴리스와 `api.nuget.org`다.
5. nlohmann/json과 Catch2는 gitman의 submodule을 유지한다 (luil의 것과 같은
   커밋). luil의 중첩 `catch2`는 받지 않고 `LUIL_BUILD_TESTS`는 켜지 않는다.
6. 제3자 고지는 luil의 `luil_generate_third_party_notices`가 만든다 (Skia 패키지
   `NOTICE.md`, WebView2, nlohmann/json, Codicons). gitman의 `generate_notices.cmake`와
   자체 Codicons·accent 자산·생성 스크립트는 삭제한다 (자산은 luil의 것과 byte
   단위로 같다).
7. 실행 파일 리소스는 두 `.rc`로 구성한다: luil 조각(ID 101~104)과 gitman 조각
   (manifest, `IDI_GITMAN` 105, VERSIONINFO). 이후 gitman 리소스는 106부터다.
8. messaging component는 luil 소유가 되므로 gitman의 `gitman_messaging_asan_tests`
   target을 없앤다. ADR-005가 요구한 `/fsanitize=address` 검증은 luil 저장소의
   `luil_messaging_asan_tests`(luil CI)가 맡는다. gitman 빌드에서는 돌지 않는다.

## 고려한 대안

- gitman 사본을 유지하고 luil의 개선을 손으로 옮긴다: 두 벌을 계속 맞추는 비용이
  크고 Skia 빌드 부담이 남는다.
- luil을 설치 패키지(`find_package`)로 소비한다: 설치본은 Skia·WebView2 경로를
  소비자가 다시 지정해야 하고, 같은 저자가 두 저장소를 함께 고치는 지금은
  submodule이 반복이 빠르다. 나중에 안정되면 전환할 수 있다.
- Skia만 skia-prep으로 바꾸고 UI 코드는 유지한다: 취득 부담은 줄지만 코드 중복은
  그대로다.

## 결과

- 취득 지점: submodule 4개(gitman의 nlohmann-json·catch2, luil, luil의
  nlohmann_json) + 해시 고정 아카이브 2개. 로컬 빌드 단계는 없다 (압축 해제만).
- 글꼴 목록·기본 UI 글꼴·drag 강조는 luil 공개 API(`luil/text/fonts.h`,
  `drag_payload::suppress_drop_highlight`)로 지금 동작을 유지한다. gitman에 Skia
  글꼴 관리자를 직접 부르는 코드는 남지 않는다.
- 가용성 위험: Skia 패키지는 개인 저장소의 릴리스 자산이다. 무결성은 해시로,
  가용성은 보장되지 않는다. 대안 경로로 `LUIL_SKIA_ROOT`·`LUIL_SKIA_BUILD_*`에
  직접 빌드한 Skia 트리를 지정할 수 있다 (luil `docs/skia-build.md`).
- 실행 파일 크기: WebView2 정적 로더만큼(수백 KB) 는다.
- 동작 변화: 설계 문서 5장. 키보드 초점·접근성(표준 컨트롤에 한함)·세션 종료
  저장이 생기고 popup 메뉴·초점 규칙이 luil의 것으로 바뀐다.
- ADR-002의 의존성 표에서 Skia 행은 "152, luil skia-prep 패키지"로, Codicons 행은
  "luil `assets/codicons`가 관리"로 바뀐다. `docs/dependency-provisioning-design.md`는
  폐기한다. ADR-004의 스레드·종료 순서 서술은 luil 소유라고 주석한다.

## 검증 방법

- 새 clone에서 결정 4의 준비 절차만으로 configure·빌드·설치가 성립한다. 빌드
  로그에 네트워크 취득이 없다.
- 아카이브를 미리 둔 오프라인 경로에서도 같은 결과가 나온다.
- 패키지가 없거나 불완전하면 configure가 원인과 조치(`fetch_skia.ps1 …`)를 보고하며
  실패한다.
- 실행 파일 고지에 Skia·WebView2·nlohmann/json·Codicons 항목이 모두 있다.
- 전체 CTest(Debug·Release)와 smoke test 4종이 통과한다.

## 갱신 (2026-11-02)

- 핀을 `d87096b9…`(luil main, 2026-11-02)로 옮긴다. 이 판의 skia-prep 패키지는
  Skia milestone 157, commit `b94213a11b3a…`, package revision 1이며 clang-cl로
  빌드되어 있다. 결정 2의 "152, `0873ec164a06…`"은 이 값으로 읽는다.
- 결정 3을 바꾼다. luil에 `LUIL_ENABLE_WEBVIEW` 옵션이 생겨 gitman은 이를 OFF로
  구성한다. WebView2 SDK 준비(`fetch_webview2.ps1`)·정적 로더·고지 항목이 빠지고,
  결과의 "실행 파일 크기" 항목과 검증 방법의 WebView2 고지 항목은 더 해당하지 않는다.
- 결정 4의 오프라인 경로는 `fetch_skia.ps1 -Offline`으로 네트워크 시도 없이 실패할
  수 있게 되었다.
- 시작 실패 메시지 상자의 제목·첫 문단을 `window_config::startup_error`로 한국어로
  정한다 (설계 2.4의 "한국어 브랜딩 메시지가 사라진다"를 되돌린다).

## 근거 자료

- luil `docs/concepts/consumer-contract.md`, `docs/concepts/build-system.md`,
  `docs/skia-build.md`, `cmake/dependencies/{skia,webview2}.cmake`
- [ADR-006](ADR-006-restricted-network-dependency-provisioning.md)
- [ADR-002](ADR-002-dependencies-and-assets.md)
- [ADR-005](ADR-005-thread-messaging.md)
