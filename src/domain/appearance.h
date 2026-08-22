#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace gitman {
    // 화면 테마 선호다 (docs/theme-and-banner-menu-design.md T3). `system`은 OS
    // 설정을 따르며, 실제 팔레트 선택은 표시 계층이 고대비 여부와 함께 해석한다.
    enum class theme_preference
    {
        system,
        light,
        dark,
    };

    // 앱 단위 외양 설정이다. 문서가 덮어쓰지 않는다 — 외양은 사용자·기기 단위이지
    // 문서 단위가 아니다. 그래서 `workspace_settings`가 아니라 `app_settings`
    // 직속이다.
    struct appearance_settings
    {
        theme_preference theme { theme_preference::system };
        // 키 컬러 id다. 표시 계층의 목록(assets/accents.json)에 없으면 그쪽이
        // 기본색으로 물러서고 저장된 값은 그대로 둔다.
        std::u8string accent_id { u8"mint" };

        [[nodiscard]] bool operator==(const appearance_settings&) const noexcept = default;
    };

    // 문서가 덮어쓴 외양만 담는다 (settings-tabs-and-appearance-scope-design
    // S2.2). 값이 없는 항목은 앱 설정을 따르며, 문서 JSON의 `appearance`에는
    // 정의된 키만 남는다.
    struct appearance_overrides
    {
        std::optional<theme_preference> theme {};
        // 빈 문자열은 정의로 보지 않는다 — 고를 수 있는 색은 항상 id가 있다.
        std::optional<std::u8string> accent_id {};

        [[nodiscard]] bool operator==(const appearance_overrides&) const noexcept = default;
        // 모든 항목이 "앱 설정 따름"이다. 저장 시 appearance object를 만들지 않는
        // 판정이다.
        [[nodiscard]] bool empty() const noexcept;
    };

    // 앱 단위 외양 위에 문서 override를 얹은 유효 값이다. 표시 계층이 이 결과만
    // 본다.
    [[nodiscard]] appearance_settings apply_overrides(const appearance_settings& base, const appearance_overrides& overrides);

    // JSON에 적는 이름이다 (`"system"`·`"light"`·`"dark"`).
    [[nodiscard]] std::u8string_view theme_preference_name(theme_preference preference) noexcept;
    // 이름을 선호로 되돌린다. 모르는 이름이면 false이고 대상은 건드리지 않는다.
    [[nodiscard]] bool parse_theme_preference(std::u8string_view name, theme_preference& target) noexcept;
} // namespace gitman
