#pragma once

// 외양·글꼴 값 타입은 luil의 것을 그대로 쓴다 (ADR-007). gitman 코드는 `gitman::`
// 이름으로 계속 부르므로 여기서 전달만 한다. 이 헤더는 값 타입만 담아 UI와 무관한
// 계층(domain·application·infrastructure)이 include해도 된다.
#include "luil/theme/appearance.h"

namespace gitman {
    using luil::appearance_overrides;
    using luil::appearance_settings;
    using luil::apply_overrides;
    using luil::font_overrides;
    using luil::font_settings;
    using luil::parse_theme_preference;
    using luil::theme_preference;
    using luil::theme_preference_name;
} // namespace gitman
