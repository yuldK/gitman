# ADR-002: C++ 의존성과 Codicons 자산 고정

## 상태

승인됨 - 2026-08-14 사용자 검수 반영

## 배경

Skia는 자체 빌드 체계와 많은 전이 의존성을 가지고 있다. JSON 파서, 테스트 프레임워크, Codicons 자산까지 개발자마다 다른 버전을 사용하면 빌드와 UI가 재현되지 않는다.

## 결정

### C++ 의존성

vcpkg manifest mode를 사용하고 저장소의 `vcpkg.json`에 `builtin-baseline`을 기록한다. 기준 baseline은 다음 커밋으로 고정한다.

```text
b9a5010d499952121b0f1a40eb98963c37da32dc
```

| 의존성 | 고정 버전 | 용도 | 기능 정책 |
| --- | --- | --- | --- |
| Skia | 148 | 2D UI 렌더링 | default feature를 끄고 `direct3d`, `harfbuzz`, `icu` 활성화 |
| nlohmann/json | 3.12.0#2 | 프로젝트 JSON | 추가 feature 없음 |
| Catch2 | 3.15.3 | 단위 및 통합 테스트 | `thread-safe-assertions` 활성화 |

vcpkg triplet은 `x64-windows-static`으로 하여 프로젝트 의존성을 정적으로 연결한다. CMake에서는 vcpkg가 제공하는 config target만 사용하며 임의의 include 및 library 경로를 하드코딩하지 않는다.

Skia 148 vcpkg port가 참조하는 upstream Skia 커밋은 다음과 같다.

```text
e7c90ecca9444fe09598f1630ab7cee2c0ee027a
```

### Codicons 자산

- `@vscode/codicons`의 `v0.0.46-24`를 사용한다.
- 태그 커밋은 `abd28d775fc5c40b437b8303807c17a6e63f6d6a`로 고정한다.
- 후속 구현 세션의 단계 1에서 공식 패키지의 `codicon.ttf`, 매핑 원본, `LICENSE`, `LICENSE-CODE`를 provenance 및 checksum과 함께 준비한다.
- 필요한 아이콘 이름과 코드포인트는 생성 스크립트로 C++ 헤더에 변환한다.
- 생성 결과와 font byte를 Win32 resource 또는 정적 byte 배열로 실행 파일에 포함하여 최종 사용자의 PC에 외부 font, Node.js나 npm을 요구하지 않는다.
- 폰트 로드 실패 시 Codicon 이름에 대응하는 한국어 텍스트를 표시한다.

### 업데이트 정책

- dependency baseline 또는 Codicons 버전 변경은 기능 변경과 분리한다.
- 업데이트 시 configure, build, unit test, 한글 및 아이콘 시각 검증을 모두 수행한다.
- 모든 제3자 라이선스와 고지 text를 실행 파일 resource에 포함하고 앱의 라이선스 화면에서 열람할 수 있게 한다. 별도 sidecar 파일을 필수로 만들지 않는다.
- Gitman runtime dependency와 자산은 단일 `.exe`에 포함한다. Git/SVN CLI는 외부 prerequisite로 남긴다.

## 고려한 대안

### Skia 직접 clone 및 GN 빌드

Skia와 전이 의존성의 commit 및 build option을 프로젝트가 직접 관리해야 한다. vcpkg port가 Windows와 GN 연결을 제공하므로 최초 기준선에서는 채택하지 않는다.

### CMake FetchContent 혼용

두 개의 dependency resolver가 생겨 잠금과 캐시 정책이 복잡해지므로 사용하지 않는다.

### Codicons 런타임 다운로드

오프라인 실행과 공급망 재현성을 해치므로 사용하지 않는다.

## 결과

- 후속 구현 세션의 단계 1에서 `vcpkg.json`, `CMakePresets.json`, 자산 provenance 파일을 추가해야 한다.
- 최초 dependency build는 Skia 때문에 시간이 오래 걸릴 수 있다.
- 정적 연결에 따른 라이선스 고지와 바이너리 크기를 배포 단계에서 검증해야 한다.

## 검증 방법

- 깨끗한 vcpkg binary cache에서 동일 baseline이 동일 버전을 선택하는지 확인한다.
- Codicons tag, package checksum, 생성 헤더의 codepoint가 일치하는지 검사한다.
- 앱 실행 시 네트워크와 Node.js 없이 폰트가 로드되는지 확인한다.
- `${sourceDir}/bin/gitman.exe` 외에 프로젝트 DLL과 asset 파일 없이 실행되는지 확인한다.
- 오프라인 상태에서 실행 파일에 포함된 모든 제3자 라이선스 text를 열람할 수 있는지 확인한다.

## 근거 자료

- [vcpkg manifest mode](https://learn.microsoft.com/vcpkg/concepts/manifest-mode)
- [vcpkg versioning](https://learn.microsoft.com/vcpkg/users/versioning)
- [vcpkg Skia port](https://github.com/microsoft/vcpkg/tree/master/ports/skia)
- [VS Code Codicons](https://github.com/microsoft/vscode-codicons)
- [Codicons npm package](https://www.npmjs.com/package/@vscode/codicons)
