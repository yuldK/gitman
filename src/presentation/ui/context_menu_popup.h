#pragma once

#include "presentation/view_snapshot.h"

#include "luil/win32/app_host.h"

#include <optional>
#include <string>
#include <string_view>

namespace gitman::ui {
    // 카드 body 또는 배너 우클릭이 여는 컨텍스트 메뉴다 (field-feedback-design 3장,
    // theme-and-banner-menu-design T1). 주 tree의 overlay가 아니라 창 경계를 넘는
    // popup 창이고(설계 D3-A), 본체는 luil `menu_element`다. 바깥 클릭·휠·Esc·활성
    // 전환의 닫기는 UI thread가 계기를 감지해 `ui_popup::dismiss`로 알린다.
    //
    // popup은 항목의 모양(라벨·아이콘·단축키·구분선·radio·기본 항목)을 항목 데이터
    // (`context_menu_item_view`)에서만 읽는다 (function-buttons-design T8). 하위 동작마다 모양을
    // 고르는 표를 두지 않으므로 카드 메뉴·배너 메뉴·기능 메뉴·배치 메뉴가 이 함수 하나를 쓴다.
    // 고른 뒤 어느 길로 갈지(외부 열기 앱 명령인지 logic intent인지)만 하위 동작 표
    // (`item_action`)가 정한다.

    // 하위 동작 하나를 가리키는 키다. entry 이름을 그대로 쓴다.
    [[nodiscard]] std::u8string_view context_menu_entry_key(context_menu_entry entry) noexcept;

    // 키를 entry로 되돌린다. 모르는 키면 비어 있다.
    [[nodiscard]] std::optional<context_menu_entry> parse_context_menu_entry(std::u8string_view key) noexcept;

    // 항목 하나를 가리키는 키다 (`menu_item_config::key`, popup tree 항목 id의 owner, select
    // factory의 인자). `argument`가 비면 entry 키 그대로이고, 있으면 `entry 키:argument`라
    // 같은 동작을 나눠 쓰는 항목(기능 메뉴의 기능들)도 메뉴 안에서 서로 다르다.
    [[nodiscard]] std::u8string context_menu_item_key(const context_menu_item_view& item);

    // 아이콘 이름을 codicon 글리프로 바꾼다. 비었거나 모르는 이름이면 0(아이콘 없음)이다.
    [[nodiscard]] char32_t context_menu_icon_glyph(std::u8string_view name) noexcept;

    // popup id다. 카드 메뉴와 문서(배너) 메뉴를 가른다.
    inline constexpr std::u8string_view card_menu_popup_id { u8"card-menu" };
    inline constexpr std::u8string_view document_menu_popup_id { u8"document-menu" };

    // view에서 popup 하나를 짓는다. `anchor_x`·`anchor_y`(창 좌표의 물리 픽셀)를
    // 배율로 나눠 논리 픽셀 자리로 삼는다. 창·모니터 밖으로 나가는 다듬기는 luil UI
    // thread가 한다 (앱은 모니터 배치를 모른다).
    [[nodiscard]] luil::win32::ui_popup build_context_menu_popup(const context_menu_view& menu, float scale);
} // namespace gitman::ui
