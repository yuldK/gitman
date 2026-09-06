#pragma once

#include "application/app_messages.h"
#include "domain/project.h"
#include "luil/ui/ui_interaction.h"

#include <optional>
#include <vector>

namespace gitman::ui {
    // luil 상호작용 상태 기계가 모르는 앱 정책이다 (설계 L7). 어느 element가 텍스트
    // 칸인지, 휠이 무엇을 스크롤하는지, 메뉴 kind 짝이 무엇인지, 남은 키가 어떤
    // intent가 되는지를 앱이 답한다.
    //
    // 예전 `interaction_controller`가 들고 있던 것 중 앱의 몫만 남았다: 휠 라우팅,
    // 카드 목록의 키보드 초점, dialog Esc 사슬. 텍스트 칸 키·클립보드·메뉴 탐색·
    // Tab·활성 키·drag 중 Esc는 luil controller가 이미 처리한다.
    //
    // 구현은 input thread에서 불린다.
    class gitman_policy final : public luil::interaction_policy
    {
    public:
        [[nodiscard]] std::optional<luil::text_input_target> text_target_of(luil::ui_element_kind kind) const override;
        [[nodiscard]] luil::input_action make_text_edit_action(const luil::text_edit_request& request) const override;
        [[nodiscard]] luil::input_action make_text_composition_action(const luil::text_composition_event& event) const override;
        [[nodiscard]] std::optional<luil::menu_kinds> menu() const override;
        [[nodiscard]] std::vector<luil::input_action> close_menu() const override;
        [[nodiscard]] std::vector<luil::input_action> on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, float scroll_delta) override;
        [[nodiscard]] std::vector<luil::input_action> on_key(const luil::ui_tree* tree, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot) override;
        void on_click(const luil::ui_element& element) override;

        // 키보드 탐색의 초점이다. 선택의 진실은 logic이 소유한다 (ADR-004) —
        // 이 값은 ↑/↓가 다음에 고를 자리를 알기 위한 입력 상태다.
        [[nodiscard]] const std::optional<project_id>& focused_card() const noexcept;

    private:
        std::optional<project_id> focused_card_ {};
    };

    // gitman `text_input_target`을 luil의 불투명 값으로 옮긴다. 값은 그대로 싣는다
    // (gitman은 0을 쓰지 않아 luil의 `none`과 겹치지 않는다).
    [[nodiscard]] luil::text_input_target to_luil_target(text_input_target target) noexcept;

    // 그 반대다. gitman이 모르는 값은 `none`이다.
    [[nodiscard]] text_input_target from_luil_target(luil::text_input_target target) noexcept;
} // namespace gitman::ui
