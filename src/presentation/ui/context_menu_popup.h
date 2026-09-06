#pragma once

#include "presentation/view_snapshot.h"

#include "luil/win32/app_host.h"

#include <optional>
#include <string_view>

namespace gitman::ui {
    // 카드 body 또는 배너 우클릭이 여는 컨텍스트 메뉴다 (field-feedback-design 3장,
    // theme-and-banner-menu-design T1). 주 tree의 overlay가 아니라 창 경계를 넘는
    // popup 창이고(설계 D3-A), 본체는 luil `menu_element`다. 바깥 클릭·휠·Esc·활성
    // 전환의 닫기는 UI thread가 계기를 감지해 `ui_popup::dismiss`로 알린다.

    // 항목 하나를 가리키는 키다 (`menu_item_config::key`와 select factory의 인자).
    // entry 이름을 그대로 쓴다.
    [[nodiscard]] std::u8string_view context_menu_entry_key(context_menu_entry entry) noexcept;

    // 키를 entry로 되돌린다. 모르는 키면 비어 있다.
    [[nodiscard]] std::optional<context_menu_entry> parse_context_menu_entry(std::u8string_view key) noexcept;

    // popup id다. 카드 메뉴와 문서(배너) 메뉴를 가른다.
    inline constexpr std::u8string_view card_menu_popup_id { u8"card-menu" };
    inline constexpr std::u8string_view document_menu_popup_id { u8"document-menu" };

    // view에서 popup 하나를 짓는다. `anchor_x`·`anchor_y`(창 좌표의 물리 픽셀)를
    // 배율로 나눠 논리 픽셀 자리로 삼는다. 창·모니터 밖으로 나가는 다듬기는 luil UI
    // thread가 한다 (앱은 모니터 배치를 모른다).
    [[nodiscard]] luil::win32::ui_popup build_context_menu_popup(const context_menu_view& menu, float scale);
} // namespace gitman::ui
