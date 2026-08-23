# 검증 기록 - TSF 인라인 조합 (5단계)

- 일자: 2026-08-23
- 대상: `feature/switch-dialog-query-bar` 브랜치 (이어서 진행)
- 기준 문서: `docs/text-input-and-font-fallback-design.md` (E2)
- 앞 단계: `docs/verification/2026-08-23-astral-characters.md`

## 1. 자동 검증

| 항목 | 결과 |
| --- | --- |
| `cmake --build --preset vs2026-tests-debug` | 성공 (경고 없음) |
| `ctest --preset vs2026-tests-debug` | **817/817 통과** (신규 7 case) |
| `cmake --build --preset vs2026-analysis` | 분석 경고 없음 |
| `cmake --build build/vs2026 --config Debug` (앱) | 성공 |
| `scripts/check_source_style.ps1 -root .` | 465 files 통과 |
| 앱 실행 (Debug) | 기동·유지·종료 정상 |

## 2. 무엇을 만들었나

검수 결정대로 IMM32가 아니라 **TSF(`ITextStoreACP2`)**다. 문서를 OS에 열어 주고
IME(TIP)가 직접 조회·수정하므로 조합이 텍스트 박스 **안에서** 이루어진다.

```
main.cpp          CoInitializeEx (이미 있었다)
 └ application_window : tsf_host
    └ tsf_input_session          platform/win32/win32_tsf_input.{h,cpp}
       ├ ITfThreadMgr2           CLSID_TF_ThreadMgr → ActivateEx(TF_TMAE_NOACTIVATETIP)
       ├ ITfKeystrokeMgr         키를 TIP에 먼저 준다
       ├ ITfDocumentMgr          창에 하나
       ├ ITfContext              CreateContext(client_id, 0, text_store, …)
       └ text_store              ITextStoreACP2 25개 + ITfContextOwnerCompositionSink 3개
```

`ITfDocumentMgr`는 창에 하나만 둔다. 텍스트 박스는 4종이지만 동시에 초점을 갖는 것은
하나뿐이라, 초점이 옮겨 가면 문서를 바꾸는 대신 **그림자 내용을 갈아 끼우고** sink에
알린다.

## 3. ADR-004와의 접점 — UI thread의 그림자 문서

설계 `E2.3`이 지적한 지점이다. TSF는 `GetText`·`GetSelection`·`SetText`를 **그 자리
에서** 처리하기를 요구하는데 Gitman의 글 상태는 logic thread 소유이고 UI thread에는
snapshot으로만 온다.

초점을 가진 박스 **하나**의 사본을 UI thread가 갖는 것으로 풀었다. `interaction_snapshot`
과 같은 "입력 정규화 상태"이며 확정된 글의 진실은 언제나 logic에 있다.

| 시점 | 동작 |
| --- | --- |
| 초점이 박스로 들어옴 | snapshot의 글·caret·anchor로 그림자를 채우고 `SetFocus(document_mgr)` |
| TSF가 씀 | 그림자를 먼저 바꾸고, lock이 풀릴 때 결과를 logic으로 보낸다 |
| logic snapshot 도착 | 조합이 없고 그림자와 다르면 그림자를 맞추고 sink에 알린다 |
| 조합 중 snapshot 도착 | **미룬다.** 조합 도중 글이 밑에서 바뀌면 TIP이 혼란에 빠진다 |
| 초점 상실·창 종료 | 조합을 확정하고 `SetFocus(nullptr)` |

**조합 글은 초안(`text_edit_state`)에 들어가지 않는다.** 그 동안 화면에 보일 글만
`set_text_composition_intent`로 따로 실어 보내고, 조합이 끝나면
`edit_text_input_intent{replace_all}` 하나로 확정한다. 그래서 **조합 한 번이 실행
취소 한 덩어리**다 — 조합 단계마다 기록이 쌓이면 쓸 수 없다.

ACP는 UTF-16 코드 단위이고 그림자는 UTF-8이라 경계 변환을 한 쌍 뒀다
(`acp_from_utf8` · `utf8_from_acp`). 단위가 섞이면 조합이 엉뚱한 자리에 들어간다.

## 4. 그리기

`text_input_element`가 조합 중에는 조합 글이 섞인 쪽을 그리고 그 구간에 강조색 밑줄을
긋는다. 가로 스크롤도 같은 글 기준이라 조합 중에 caret이 칸 밖으로 나가지 않는다.
조합 중에는 선택 띠를 그리지 않고 포인터로 caret을 옮기지도 않는다 — 그 동안 조합은
IME의 것이다.

## 5. 키와 메시지

- `WM_KEYDOWN`·`WM_KEYUP`은 `ITfKeystrokeMgr::TestKeyDown` → `KeyDown`으로 TIP에
  먼저 준다. 먹었으면 앱은 손대지 않는다.
- `WM_CHAR`는 조합 중이면 흘리지 않는다 — 확정 글은 TSF가 이미 보냈고 여기서 또 넣으면
  같은 글자가 두 번 들어간다. 조합이 아닐 때는 4단계의 surrogate 병합 경로 그대로다
  (이모지 패널은 TSF를 거치지 않는다).
- `WM_IME_SETCONTEXT`에서 `ISC_SHOWUICOMPOSITIONWINDOW`를 끈다. 켜 두면 같은 글자가
  두 번 그려진다.
- 기존 `WM_IME_STARTCOMPOSITION`·`WM_IME_COMPOSITION` 처리와
  `place_composition_window`는 **지웠다**. TSF와 IMM32를 함께 두면 조합이 두 번
  그려진다.

## 6. 신규 test

`tsf_input_tests` 5 case (신규 파일).

- ACP ↔ UTF-8 경계 변환 4 case — 한글(3바이트/1단위), 이모지(4바이트/2단위),
  왕복, **surrogate 쌍 한가운데를 가리키면 그 문자의 시작으로 되돌리는 것**,
  범위를 벗어난 값의 자르기.
- `A TSF session activates and tears down cleanly` — 실제 COM으로 `ITfThreadMgr2`
  활성화부터 document manager · context · text store까지 한 벌을 세우고 정리한다.
  초점이 없을 때 아무것도 내보내지 않는 것, 초점이 들어와도 조합이 없으면 초안으로
  나가는 것이 없음을 함께 본다. **SKIP 없이 실제로 통과했다** (이 PC에서 TSF 사용
  가능).

`switch_dialog_tests` 2 case.

- `An IME composition shows in the box without touching the draft` — 조합 글이 view에
  실리지만 확정된 `text`는 비어 있다.
- `Ending a composition clears the overlay and commits as one undo step` — 확정 뒤
  overlay가 사라지고, 실행 취소 한 번이 조합 전체를 되돌린다.

## 7. 남은 확인 (수동 · 사람이 해야 한다)

TSF는 COM과 실제 TIP에 기대므로 단위 test로 덮이지 않는 부분이 남는다. 아래를 직접
확인해 주면 좋겠다.

1. 한글 IME로 텍스트 박스(환경설정 제한 시간 칸은 숫자만 받으므로 **switch dialog의
   질의 막대**나 **글꼴 필터**가 좋다)에 `한글`을 쳐서
   - 조합 중인 글자가 **박스 안에** 보이는지 (창 구석의 별도 창이 아니라),
   - 조합 구간에 밑줄이 보이는지,
   - 한자 변환 후보 창이 조합 글자 **밑에** 뜨는지.
2. 조합을 확정한 뒤 `Ctrl+Z` 한 번이 조합 전체를 되돌리는지.
3. 조합 도중 `Esc`로 dialog를 닫거나 다른 박스를 눌렀을 때 글이 깨지지 않는지.
4. 영문 입력(IME 영문 모드)에서 글자가 **두 번** 들어가지 않는지 — TIP이 먹은 키의
   `WM_CHAR` 억제가 제대로 도는지 보는 지점이다.
5. `Win`+`.` 이모지 패널이 여전히 동작하는지.

## 8. 알려진 한계

- **조합 밑줄이 한 종류다.** IME가 지정하는 밑줄 종류(진한 밑줄 = 변환 대상 절 등)를
  읽지 않는다. `GUID_PROP_ATTRIBUTE` + `ITfDisplayAttributeMgr`로 붙일 수 있으며,
  동작을 확인한 뒤에 하기로 설계에서 미뤄 두었다.
- **`GetACPFromPoint`는 `E_NOTIMPL`이다.** 포인터로 조합 글 안을 찍는 기능이 없다.
  한 줄 박스에서는 얻는 것보다 어긋날 위험이 크다고 보았다.
- **`GetTextExt`가 가로 스크롤을 반영하지 않는다.** 글이 칸을 넘긴 뒤에는 후보 창이
  조금 왼쪽에 뜬다.
- **조합 없이 TIP이 곧바로 쓴 글은 `replace_all`로 간다.** 그 경우 실행 취소 묶음이
  글자 단위가 된다. 평범한 `WM_CHAR` 타이핑은 기존 경로 그대로라 묶음이 유지된다.
- `ITfMessagePump`는 쓰지 않았다. `ITfKeystrokeMgr` 경유로 충분하다고 보았고, 실제
  IME에서 부족하면 그때 바꾼다.
