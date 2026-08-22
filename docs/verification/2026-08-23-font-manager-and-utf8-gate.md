# 검증 기록 - 글꼴 관리자 교체와 UTF-8 관문 (1단계)

- 일자: 2026-08-23
- 대상: `feature/switch-dialog-query-bar` 브랜치 (이어서 진행)
- 기준 문서: `docs/text-input-and-font-fallback-design.md` (E3.1 · E3.2 · E3.6)
- 진행 방식: 설계 검수 후 세부 단계별로 구현하고 단계마다 검수를 요청한다. 커밋은
  사용자가 한다.

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2026-tests-debug` | **794/794 통과** (신규 5 case) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| `cmake --build build/vs2026 --config Debug` (앱) | 성공 |
| `scripts/check_source_style.ps1 -root .` | 457 files 통과 |
| clang-format (변경 파일) | 위반 없음 |

`gitman_format_check`는 **이번 변경과 무관한 18개 파일에서 이미 실패한다**
(`src/infrastructure/git_command_builder.cpp` 등). 이번에 손댄 파일은 그 목록에
없다. 원인은 별도로 봐야 한다.

## 2. 재현한 원인

사용자가 첨부한 콜스택은 `SkAutoToGlyphs` → `AutoSTArray<32, SkGlyphID>::reset` →
`SkASSERT(count >= 0)`이다. `SkFont::countText`가 잘못된 UTF-8에 -1을 돌려주고 그
값이 `size_t`로 넘어가 `reset(int)`에서 음수가 되는 경로다.

Skia는 `UNICODE` 매크로 없이 빌드되어 `SkFontHost_win.cpp`가 ANSI GDI API로
컴파일된다. 산출물에서 확인했다.

```
dumpbin /SYMBOLS third_party/skia/out/gitman-release/skia.lib
  __imp_EnumFontFamiliesExA
  __imp_CreateFontIndirectA
  __imp_GetTextFaceA
```

두 관리자를 같은 PC에서 나란히 세어 확인했다 (임시 test로 측정한 뒤 지웠다).

| 관리자 | 가족 수 | UTF-8이 아닌 이름 |
| --- | --- | --- |
| `SkFontMgr_New_GDI` | 289 | **22** |
| `SkFontMgr_New_DirectWrite` | 227 | 0 |

깨진 첫 이름의 바이트는 `B8 BC C0 BA 20 B0 ED B5 F1`이며 CP949로 읽으면 정확히
`맑은 고딕`이다. 다음 둘은 `맑은 고딕 Semilight`, `바탕`이었다.

**결론**: 재현 조건은 스크롤 속도가 아니라 한글 이름 글꼴 행이 목록에 처음 들어오는
순간이다. 22개 가족이 지뢰였다.

## 3. 고친 내용

### 3.1 글꼴 관리자를 DirectWrite로 바꿨다

`win32_fonts.cpp`의 `SkFontMgr_New_GDI()` → `SkFontMgr_New_DirectWrite()`.
`embedded_assets.cpp`가 이미 쓰던 관리자와 같아져 앱에 하나만 남는다. Skia
재빌드는 필요 없었다 — `skia.lib`에 심볼이 이미 있고 `dwrite`도 이미 링크되어 있다.

이 교체로 글자 단위 대체(`onMatchFamilyStyleCharacter`)의 길도 열린다. GDI 관리자는
그 진입점이 `return nullptr`이라 대체 자체가 불가능했다 (2단계에서 쓴다).

### 3.2 Skia로 가는 글에 관문을 뒀다

`domain/utf8_text`를 새로 만들고 `draw_primitives`의 `measure_text`·`draw_text`가
그 관문을 지나게 했다. 잘못된 바이트는 `U+FFFD`로 바뀐다. 온전한 글에는 할당도
복사도 없다 (`utf8_is_valid`가 먼저 거른다).

Skia로 가던 나머지 두 진입점도 이 경로로 모았다.

- `ui_tree.cpp`의 tooltip 폭 측정 (`font.measureText` 직접 호출)
- `skia_smoke_view.cpp`의 `draw_utf8` (지우고 `ui::draw_text`로 대체)

이제 `src` 안에서 Skia에 글을 넘기는 곳은 `draw_primitives.cpp` 하나다.

관문은 원인을 고친 뒤에도 남긴다. 밖에서 오는 글(프로세스 출력·경로·설정 파일·
클립보드)은 언제든 깨질 수 있고, 지금 구조에서는 그 한 글자가 프로세스를 죽인다.

### 3.3 목록을 만들 때 한 번 더 거른다

`installed_font_families()`가 이름 끝의 NUL을 떼고, UTF-8이 아닌 이름은 목록에서
뺀다. 관리자를 바꿔도 방어는 남긴다.

## 4. 신규 test

`utf8_tests` 4 case.

- `UTF-8 validation rejects what Skia rejects` — 실제로 앱을 죽였던 CP949 바이트열,
  잘린 이어짐, 이어짐 바이트로 시작, 과잉 인코딩, surrogate, `U+10FFFF` 초과.
- `UTF-8 repair keeps the readable parts` — 잘못된 바이트마다 `U+FFFD` 하나.
- `UTF-8 encoding covers the planes above BMP` — `U+1F600`이 4바이트로 나오고,
  surrogate·범위 초과는 빈 값이다 (잘못된 UTF-8을 만들지 않는 계약).
- `UTF-8 decoding always advances` — 잘못된 자리에서도 길이가 1이라 호출자가 갇히지
  않는다.

`win32_fonts_tests` 1 case (신규 파일).

- `Installed font family names are valid UTF-8` — 이 PC의 설치 글꼴 전부(227개)를
  검사하고 **실제로 `measure_text`로 재 본다**. 고쳐지기 전이라면 여기에서
  abort했을 회귀 test다. 455 assertion.

## 5. 남은 확인 (수동)

- 글꼴 선택 dialog를 열어 목록 끝까지 내려도 죽지 않는 것.
- 한글 이름 글꼴(`맑은 고딕` 등)이 목록과 환경설정 값 줄에 제대로 보이는 것.
- 기존 설정에 CP949로 깨진 이름이 저장돼 있었다면 조용히 기본 글꼴로 돌아가는 것
  (설정 파일은 건드리지 않는다). 다시 고르면 된다.

## 6. 다음 단계

2단계는 글꼴 대체(run 나누기)다 — `Cascadia Code`의 한글과 이모지 표시가 여기에서
해결된다. 설계 `E3.3`·`E3.4`이며 전달 방식은 검수에서 A(한 번 등록)로 정해졌다.
