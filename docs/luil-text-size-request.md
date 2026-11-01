# luil 요청 — Windows 텍스트 크기와 접근 정보 두 가지

상태: **gitman 쪽 준비 완료, luil 요청** (2026-11-02). 근거는
[text-font-migration-design.md](text-font-migration-design.md)의 7장과 검수 결정 D5다.

luil 핀 `d87096b` 기준으로 확인한 내용이다. luil 저장소에 옮겨 적어 요청한다.

## 1. Windows 텍스트 크기를 플랫폼 계층이 읽는다

### 지금

- luil은 글자 크기 변환 표를 `set_system_font_sizes`로 받는다 (`include/luil/ui/font_scale.h`).
  `scaled_font_size`·`text_line_height`·`make_text_font`·`measure_context::text_width`가 모두 그 표를 따른다.
- 표를 넣는 곳은 Android뿐이다 (`src/android/android_app.cpp:932`). Windows에서는 표가 비어
  있어 크기가 `logical_size * display_scale`이다.
- Windows 설정 > 접근성 > 텍스트 크기(100~225%)는 그래서 luil 앱의 직접 그리는 글에 닿지 않는다.

### 요청

1. Win32 플랫폼 계층이 시작할 때 텍스트 크기 배율을 읽어 선형 표(`sizes[n] = n * factor`)를
   `set_system_font_sizes`로 넣는다. 값은 WinRT `UISettings.TextScaleFactor`이고, Win32에서
   같은 값을 레지스트리(`HKCU\Software\Microsoft\Accessibility\TextScaleFactor`)로 읽을 수
   있는지는 확인하지 않았다.
2. 값이 바뀌면(`UISettings.TextScaleFactorChanged` 또는 해당 `WM_SETTINGCHANGE`) 표를 바꾸고
   글꼴·배치 cache를 비우고 앱에 다시 배치하도록 알린다 (창 크기가 바뀐 것과 같은 경로).
3. 끄는 옵션(`window_config`의 한 칸)을 둔다. 고정 높이를 아직 정리하지 않은 앱이 받을
   자리다.

## 2. 글이 드는 컴포넌트 높이를 글 크기에서 정한다

표를 넣으면 컴포넌트의 글도 커진다. 그런데 다음 높이가 상수라 150% 근처부터 글이 칸 위아래로
넘친다. 앱이 그 상수로 자리를 잡는 곳(gitman: 컨텍스트 메뉴, dialog 버튼 줄, 질의 칸)도 함께
넘친다.

| 상수 | 값 | 헤더 |
| --- | --- | --- |
| `menu_item_height` | 26 | `menu_element.h:62` |
| `text_button_height` | 28 | `dialog_elements.h:184` |
| `dropdown_height` | 28 | `dropdown_element.h:32` |
| `check_row_height` | 24 | `check_element.h:65` |
| `choice_radio_row_height`·`choice_toggle_height` | 24·26 | `choice_group_element.h:63` |
| `group_header_height` | 28 | `group_element.h:37` |
| `list_header_height` | 24 | `grouped_list_element.h:36` |
| `table_header_height` | 28 | `table_element.h:83` |
| `text_area_line_height` | 18 | `text_area_element.h:136` |
| `toast_height` | 40 | `toast_element.h:64` |
| `command_palette_field_height` | 28 | `command_palette.h:43` |

요청: 이 높이들을 "100% 값 + 그 칸 글이 자란 몫"을 내는 함수로 바꾼다. 100%에서 지금 상수와
같은 값이어야 기존 앱 화면이 바뀌지 않는다. gitman은 같은 모양을 `list_metrics.h`의
`text_growth`로 이미 했다 (커밋 `0b454dc`).

## 3. 접근 정보 두 가지

UI/UX 점검(`change_log.md` 2026-11-02)에서 찾았다.

1. `text_button_element::accessibility()`가 적힌 글이 있으면 `set_access_name`을 무시한다
   (`src/ui/dialog_elements.cpp:377`). gitman 환경설정의 "찾아보기"·"지우기"처럼 같은 글의
   단추가 둘 이상이면 화면 읽기로 어느 항목의 것인지 갈리지 않는다. 요청: 앱이 단 이름이
   있으면 그것을 먼저 쓴다 (`text_input_element`가 이미 그렇게 한다, 같은 파일 825행).
2. `button_element`(아이콘 단추)의 `button_config::active`(토글 켬)가 접근 정보에 실리지 않는다.
   gitman의 경로 표시·로그 자동 스크롤 토글은 켬·끔이 읽히지 않는다. 요청: 토글로 쓰는 단추가
   `access_role::toggle_switch`(또는 button + `checked`)로 읽히는 길.

## 4. luil이 반영하면 gitman이 할 일

- 1·2가 들어오면 핀을 올리고 100%·150%·225%에서 카드·로그·시작 페이지·dialog를 스크린샷으로
  본다. 치수 함수는 이미 표를 따르므로 코드 변경은 dialog chrome 높이(설계 7.3에서 상수로 둔
  것) 정도다.
- 3-1이 들어오면 환경설정 단추에 항목 제목을 이름으로 단다 (점검 때 넣었다가 효과가 없어 뺀 것).
- 3-2가 들어오면 도구 막대·로그 토글을 그 길로 옮긴다.
