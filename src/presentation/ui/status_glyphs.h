#pragma once

#include <string_view>

namespace gitman::ui {
    // status_presentation의 Codicon 이름을 내장 글리프로 옮긴다. 모르는 이름은
    // question으로 표시해 침묵하지 않는다. gitman 전용 표라 luil로 옮기지 않았다.
    [[nodiscard]] char32_t codicon_for_name(std::u8string_view name) noexcept;

    // 사용자가 설정에 적은 codicon 이름을 찾는다 (REQ-020). 빌드 때 mapping.json에서
    // 만든 전체 표(별칭 포함)를 이분 탐색하며, 모르는 이름이면 `fallback`이다 —
    // 무엇을 기본으로 삼을지는 부르는 쪽이 정한다.
    [[nodiscard]] char32_t codicon_for_configured_name(std::u8string_view name, char32_t fallback) noexcept;

    // 아이콘을 지정하지 않은 사용자 명령의 기본 글리프다.
    [[nodiscard]] char32_t default_command_codicon() noexcept;
} // namespace gitman::ui
