# 검증 기록 - BMP 밖 문자 입력 (4단계)

- 일자: 2026-08-23
- 대상: `feature/switch-dialog-query-bar` 브랜치 (이어서 진행)
- 기준 문서: `docs/text-input-and-font-fallback-design.md` (E2.7)
- 앞 단계: `docs/verification/2026-08-23-settings-scroll.md`

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2026-tests-debug` | **810/810 통과** (신규 5 case) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| `cmake --build build/vs2026 --config Debug` (앱) | 성공 |
| `scripts/check_source_style.ps1 -root .` | 461 files 통과 |

## 2. 현황

두 곳이 BMP 밖 문자를 막고 있었다.

- `WM_CHAR`가 surrogate(`0xD800`~`0xDFFF`)를 **버렸다**. BMP 밖 문자는 UTF-16
  surrogate **쌍**으로 두 번에 나눠 오므로 두 조각 모두 사라졌다.
- `text_edit_encode_utf8`이 3바이트까지만 만들었다 (주석에 "WM_CHAR가 surrogate를
  걸러 주므로 3바이트까지면 충분하다"고 적혀 있었다).

## 3. 고친 내용

### 3.1 surrogate 쌍을 합친다

`platform/win32/utf8`에 순수 함수를 뒀다.

```cpp
[[nodiscard]] std::optional<char32_t> merge_utf16_code_unit(std::optional<char16_t>& pending, char16_t code_unit) noexcept;
```

앞쪽 surrogate면 `pending`에 남기고 `nullopt`를 돌려준다. 뒤쪽이 오면 합친다.
**짝이 맞지 않는 조각은 버린다** — 잘못된 UTF-8을 만들지 않는 것이 계약이다.

창 procedure는 이 함수를 부르고 `pending`만 들고 있다. 로직이 창 밖에 있어 test가
가능하다.

### 3.2 4바이트 인코딩

`text_edit_encode_utf8`이 1단계에서 만든 `utf8_encode`로 위임한다. 인코더가 하나만
남아 규칙이 갈리지 않는다. surrogate 코드포인트와 `U+10FFFF` 초과는 빈 값이다.

caret 이동·지우기·낱말 판정은 이미 UTF-8 경계 기준이라 4바이트에서도 그대로 맞는다 —
test로 못박았다.

## 4. 신규 test

`utf8_tests` 2 case.

- `Surrogate pairs from WM_CHAR merge into one codepoint` — `U+1F600`이 `D83D DE00`
  두 번에 나눠 와서 하나로 합쳐진다. BMP 안의 글자는 그대로 지나간다.
- `Unpaired surrogates are dropped instead of becoming broken text` — 앞쪽 없이 온
  뒤쪽, 짝을 기다리다 평범한 글자가 온 경우, 앞쪽이 연달아 온 경우.

`text_edit_tests` 3 case.

- `Text edit accepts characters above the BMP` — 4바이트가 나오고 surrogate·범위
  초과는 빈 값이다.
- `Caret motion treats an emoji as one character` — ←가 이모지를 통째로 넘고
  backspace도 통째로 지운다. 4바이트 가운데로 들어가면 글이 깨진다.
- `Typing an emoji inserts all four bytes at the caret`.

## 5. 남은 확인 (수동)

- 텍스트 박스에 초점을 두고 `Win`+`.`로 이모지를 넣어 보기. 2단계의 글꼴 대체가
  있으므로 실제로 그려져야 한다.
- 컬러로 나오는지 흑백인지 (Skia DirectWrite scaler의 컬러 글리프 지원 여부는 아직
  확인하지 못했다). 흑백이어도 입력·저장·복사는 정상이다.

## 6. 다음 단계

5단계는 TSF 인라인 조합(E2)이다. 가장 크며, 그림자 문서 → text store → 조합 sink →
키 라우팅 순으로 나눈다. 착수 전에 설계 `E2.3`(UI thread의 그림자 문서가 ADR-004에
더하는 예외)을 확인받는 것이 좋겠다.
