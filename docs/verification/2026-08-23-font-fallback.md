# 검증 기록 - 글꼴 대체 (2단계)

- 일자: 2026-08-23
- 대상: `feature/switch-dialog-query-bar` 브랜치 (이어서 진행)
- 기준 문서: `docs/text-input-and-font-fallback-design.md` (E3.3 · E3.4 · E3.6)
- 앞 단계: `docs/verification/2026-08-23-font-manager-and-utf8-gate.md`

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2026-tests-debug` | **800/800 통과** (신규 6 case) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| `cmake --build build/vs2026 --config Debug` (앱) | 성공 |
| `scripts/check_source_style.ps1 -root .` | 459 files 통과 |

## 2. 고친 내용

### 2.1 대체 해석기 (E3.4 · 검수 결정 A)

`draw_primitives.h`에 `font_fallback` interface와 `set_font_fallback`을 뒀다.
platform이 시작할 때 한 번 등록하고 창이 사라질 때 되돌린다. 등록하지 않으면 대체
없이 예전과 똑같이 동작하므로 test는 그 경로를 기본값으로 쓴다.

`win32`의 `registry_font_resolver`가 `font_resolver`(가족 이름 → typeface)와
`font_fallback`(글자 → typeface)을 함께 구현한다. 둘 다 같은 registry cache를 보므로
나눌 이유가 없다.

`win32_fonts::fallback_typeface(codepoint)`는
`SkFontMgr::matchFamilyStyleCharacter(nullptr, Normal, nullptr, 0, codepoint)`로
시스템이 고른 글꼴을 받아 codepoint별로 cache한다. 찾지 못한 codepoint도 nullptr로
남겨 같은 글자를 매 frame 다시 뒤지지 않는다.

### 2.2 run 나누기 (E3.3)

`split_text_runs(text, font)`가 글을 조각으로 나눈다. **규칙은 하나다 — 기본 글꼴이
그릴 수 있으면 기본 글꼴이다.**

```
"code 한글"  →  ["code " 기본]["한글" 대체]
"A한A"       →  ["A" 기본]["한" 대체]["A" 기본]
```

초안의 "공백은 직전 run을 따라간다"는 규칙은 **버렸다**. 공백을 이모지 글꼴로 그리면
코드 글꼴의 열이 어긋난다. 기본 글꼴이 공백을 가지고 있으므로 기본 글꼴로 그리는
편이 옳다.

측정(`measure_text`)과 그리기(`draw_text`)가 **이 함수 하나를** 함께 쓴다. 다르게
나누면 caret 자리와 글자가 어긋난다.

성능:

- 글자마다 묻지 않고 `SkFont::unicharsToGlyphs`로 **문자열당 한 번** 묻는다.
  DirectWrite 조회는 호출당 비용이 있다.
- 빠진 글리프가 하나도 없으면(대부분의 글) 나누지 않고 원본 하나를 그대로 돌려준다.
- 대체 해석기가 등록되지 않았으면 곧바로 run 하나다.

### 2.3 미리 보기 예문 (E3.6)

글꼴 선택 dialog의 예문 끝에 이모지를 한 자 넣었다. 대체가 실제로 도는 것이 눈에
보인다.

## 3. 신규 test

`font_fallback_tests` 4 case (신규 파일). Codicons는 아이콘 전용이라 한글도 라틴도
담지 않으므로, 시스템 글꼴 구성과 무관하게 "기본 글꼴에 없는 글자" 상황을 만들 수
있다. 가짜 해석기로 어느 codepoint에 대체를 줄지 정한다.

- `Text runs stay whole when no fallback is installed` — 등록 전에는 run 하나다.
- `Text runs split only where the chosen font has no glyph` — `A한A`가 3조각으로
  갈리고 **대체 뒤에 원래 글꼴로 돌아온다**. 대체를 찾지 못한 글자는 기본 글꼴로
  남는다 (빈 네모가 보이는 편이 침묵보다 낫다).
- `Text runs cover the input exactly` — 조각을 이으면 원문과 같고 빈 조각이 없다.
  BMP 밖 문자를 포함해 4바이트가 쪼개지지 않는 것도 함께 본다.
- `Measuring adds up the run widths` — 대체가 붙으면 폭이 달라진다. 측정이 run을
  따라가지 않으면 이 값이 그대로였을 것이고 caret이 글자와 어긋났을 것이다.

`win32_fonts_tests` 2 case 추가 — 실제 시스템 대체를 확인한다.

- `The system resolves a fallback typeface for Hangul and emoji` — `한`과 `U+1F600`에
  대해 DirectWrite가 실제로 글꼴을 내주고, 그 글꼴에 그 글리프가 있으며, 두 번째
  조회가 cache에서 온다. GDI 관리자로는 이 test가 불가능했다 (`return nullptr`).
- `A code font without Hangul still measures Hangul through fallback` —
  `Cascadia Code`에 한글 글리프가 **없음을 먼저 확인한 뒤**, `code 한글`이
  `["code " 기본]["한글" 대체]`로 갈리고 대체 글꼴이 실제로 한글을 담고 있는 것을
  본다. 글꼴이 설치돼 있지 않은 PC에서는 SKIP한다.

## 4. E4의 답

`Cascadia Code`에 한글이 나오지 않은 것은 그 글꼴에 **한글 글리프가 없기 때문**이다
(글리프 1483개, `U+AC00`·`U+D55C` 없음 — `PresentationCore`로 확인). VS Code·Windows
Terminal에서 보이는 것은 그 앱들이 글꼴 대체를 하기 때문이고, 이번에 Gitman도 같은
일을 한다. 라틴은 `Cascadia Code`로, 한글은 시스템이 고른 글꼴로 그린다.

## 5. 남은 확인 (수동)

- 코드 글꼴을 `Cascadia Code`로 두고 로그·diff에 한글이 섞인 줄이 제대로 보이는 것.
- 글꼴 선택 dialog 미리 보기의 이모지가 보이는 것. **컬러로 나오는지 흑백으로
  나오는지 함께 봐 주면 좋겠다** — Skia DirectWrite scaler의 컬러 글리프 지원 여부는
  아직 확인하지 못했다. 흑백이어도 입력·저장·복사는 정상이다.

## 6. 다음 단계

3단계는 설정 dialog 스크롤(E1)이다.
