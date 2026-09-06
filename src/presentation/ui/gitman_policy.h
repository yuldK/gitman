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
    // Tab 자리는 D1-B다 (설계 4장): 버튼·dialog 컨트롤·텍스트 칸만 자리이고 카드
    // body·root·메뉴·목록 행은 element 쪽에서 `set_tab_stop(false)`로 빠졌다.
    // 그래서 카드 body를 클릭한 뒤의 Enter는 luil의 활성 키(= 그 body의 좌클릭)가
    // 아니라 여기 `on_key`로 흘러 `focused_card_` 새로 고침이 된다. 초점 순회 순서
    // (`order_focus`)와 이동 알림(`on_focus_moved`)은 luil 기본 그대로다 — 그리기
    // 순서가 곧 Tab 순서이고, 앱이 초점 이동에 낼 메시지가 없다.
    // 반대로 Tab으로 도구 막대 버튼에 선 채 ↓로 카드 선택을 옮기면 Enter는 luil의
    // 초점 컨트롤(그 버튼)이 먼저 받는다 — 초점 모델이 둘(luil `focused`·여기
    // `focused_card_`)이라 생기는 수용된 차이다 (설계 5장).
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
