#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace gitman {
    using ui_color = std::uint32_t;

    enum class color_theme
    {
        dark,
        high_contrast,
    };

    struct caption_color_palette
    {
        ui_color background { 0 };
        ui_color foreground { 0 };
        ui_color button_hover_background { 0 };
        ui_color button_hover_foreground { 0 };
        ui_color close_button_hover_background { 0 };
        ui_color close_button_hover_foreground { 0 };
    };

    // 키 컬러 하나가 한 테마에서 맡는 네 역할이다
    // (docs/theme-and-banner-menu-design.md T4.2). 값은 assets/accents.json에서
    // 빌드 시점에 들어온다.
    struct accent_color_set
    {
        // 채움·테두리·상태 표시의 기본 색이다.
        ui_color accent { 0 };
        // 위 요소의 hover다.
        ui_color hover { 0 };
        // 낮은 알파로 겹치는 옅은 강조 바탕이다 (배지·토글 트랙·선택 행).
        ui_color soft { 0 };
        // 바탕 위 강조 글자다 (설정 행 제목·강조 버튼 라벨).
        ui_color emphasis_foreground { 0 };
    };

    // 고를 수 있는 키 컬러 하나다. 문자열은 생성 표를 가리키는 정적 수명이다.
    struct accent_definition
    {
        std::u8string_view id {};
        std::u8string_view label {};
        // 설정의 색 동그라미다. 테마와 무관한 대표색이다.
        ui_color swatch { 0 };
        accent_color_set dark {};
        accent_color_set light {};

        [[nodiscard]] const accent_color_set& for_theme(color_theme theme) const noexcept;
    };

    // 앱 설정이 모르는 id를 담고 있을 때 물러설 기본 키 컬러다.
    inline constexpr std::u8string_view default_accent_id { u8"mint" };

    // 빌드 시점에 내장된 키 컬러 목록이다 (T4.3). 순서는 JSON 그대로다.
    [[nodiscard]] std::span<const accent_definition> accent_catalog() noexcept;
    // id로 찾는다. 없으면 기본 키 컬러다 — 저장된 값은 지우지 않는다.
    [[nodiscard]] const accent_definition& accent_for(std::u8string_view id) noexcept;
    // 목록에 그 id가 있는지다. 설정을 읽을 때 경고를 남길지 판정한다.
    [[nodiscard]] bool accent_exists(std::u8string_view id) noexcept;

    struct ui_color_palette
    {
        ui_color window_background { 0 };
        ui_color surface_background { 0 };
        ui_color primary_foreground { 0 };
        // 키 컬러의 네 역할이다. 상태 표시(정상)도 이 색을 쓴다.
        ui_color accent { 0 };
        ui_color accent_hover { 0 };
        ui_color accent_soft { 0 };
        ui_color accent_emphasis_foreground { 0 };
        ui_color warning_accent { 0 };
        ui_color error_accent { 0 };
        // caption 밖의 일반 버튼(도구 막대·카드)의 hover와 눌림 표시다.
        ui_color button_hover_background { 0 };
        ui_color button_hover_foreground { 0 };
        ui_color button_pressed_background { 0 };
        ui_color tooltip_background { 0 };
        ui_color tooltip_border { 0 };
        // 상단 막대 아래로 내용이 지나갈 때 쓰는 그림자다. 알파는 그리는 쪽이
        // 거리에 따라 낮춘다.
        ui_color content_shadow { 0 };
        // notice 배너의 바탕이다. 카드와 같은 색으로 보이지 않도록 구분한다.
        ui_color notice_background { 0 };
        caption_color_palette caption {};
    };

    [[nodiscard]] constexpr ui_color make_ui_color(const std::uint8_t red, const std::uint8_t green, const std::uint8_t blue, const std::uint8_t alpha = 255) noexcept
    {
        return static_cast<ui_color>(alpha) << 24U | static_cast<ui_color>(red) << 16U | static_cast<ui_color>(green) << 8U | static_cast<ui_color>(blue);
    }

    // 테마의 중립 색 위에 키 컬러를 얹은 팔레트다. 고대비는 키 컬러를 무시하고
    // 네 역할을 모두 흰색으로 둔다 (가독성이 우선이다).
    [[nodiscard]] ui_color_palette color_palette_for(color_theme theme, const accent_definition& accent) noexcept;
    // 기본 키 컬러(mint)를 쓰는 팔레트다. 설정을 아직 모르는 경로가 쓴다.
    [[nodiscard]] ui_color_palette color_palette_for(color_theme theme) noexcept;
} // namespace gitman
