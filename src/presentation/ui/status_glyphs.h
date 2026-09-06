#pragma once

#include <string_view>

namespace gitman::ui {
    // status_presentation의 Codicon 이름을 내장 글리프로 옮긴다. 모르는 이름은
    // question으로 표시해 침묵하지 않는다. gitman 전용 표라 luil로 옮기지 않았다.
    [[nodiscard]] char32_t codicon_for_name(std::u8string_view name) noexcept;
} // namespace gitman::ui
