#pragma once

// luil 위로 옮긴 UI test가 함께 쓰는 도우미다 (luil 이식 C2-4). 재작성 test의 공통
// 규칙(계획 7장): `ui_element_id { kind, u8string }`, `luil::interaction_controller
// { &policy, config }`, tree마다 `duplicate_ids()`·`unarranged()`가 비어 있음을 확인.

#include "application/app_messages.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/gitman_policy.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/ui_events.h"
#include "luil/ui/ui_interaction.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gitman::testing {
    // test가 정하는 결정적 시각이다. controller는 이벤트의 시각만 본다.
    [[nodiscard]] inline std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    // 창 800×600, 배율 1의 view에 카드 `card-0`…`card-N`을 담는다.
    [[nodiscard]] inline view_snapshot make_view(const std::size_t card_count, const view_empty_state empty_state = view_empty_state::none, const float scale = 1.0f, const float scroll = 0.0f)
    {
        view_snapshot view {};
        view.window_width = 800.0f * scale;
        view.window_height = 600.0f * scale;
        view.scale = scale;
        view.scroll_offset = scroll;
        view.empty_state = empty_state;
        for (std::size_t index = 0; index < card_count; ++index)
        {
            card_view_model card {};
            const std::string digits { std::to_string(index) };
            card.id.value = u8"card-";
            card.id.value.append(digits.begin(), digits.end());
            view.cards.push_back(std::move(card));
        }
        return view;
    }

    // 중복 id와 배치되지 않은 element가 없어야 정상이다. 모든 tree가 이 검사를 지난다.
    inline void require_clean_tree(const luil::ui_tree& tree)
    {
        REQUIRE(tree.duplicate_ids().empty());
        REQUIRE(tree.unarranged().empty());
    }

    [[nodiscard]] inline std::shared_ptr<const luil::ui_tree> make_tree(const view_snapshot& view)
    {
        const std::shared_ptr<const luil::ui_tree> tree { ui::build_ui_tree(view) };
        require_clean_tree(*tree);
        return tree;
    }

    [[nodiscard]] inline std::shared_ptr<const luil::ui_tree> make_tree(const std::size_t card_count, const view_empty_state empty_state = view_empty_state::none)
    {
        return make_tree(make_view(card_count, empty_state));
    }

    [[nodiscard]] inline luil::ui_element_id id_of(const luil::ui_element_kind kind, const std::u8string_view owner = u8"")
    {
        return { kind, std::u8string { owner } };
    }

    [[nodiscard]] inline const luil::ui_element* find(const luil::ui_tree& tree, const luil::ui_element_kind kind, const std::u8string_view owner = u8"")
    {
        return tree.find(id_of(kind, owner));
    }

    [[nodiscard]] inline luil::rect_f bounds_of(const luil::ui_tree& tree, const luil::ui_element_kind kind, const std::u8string_view owner = u8"")
    {
        const luil::ui_element* const element { find(tree, kind, owner) };
        REQUIRE(element != nullptr);
        return element->bounds();
    }

    // 기본 상호작용 설정이다. 더블 클릭 500ms·4px, drag 시작 6px, 글자 폭 10px 고정.
    [[nodiscard]] inline luil::interaction_config test_interaction_config()
    {
        luil::interaction_config config {};
        config.double_click_time = std::chrono::milliseconds { 500 };
        config.double_click_distance = 4.0f;
        config.drag_start_distance = 6.0f;
        config.measure_text = [](const std::u8string_view text, float) { return static_cast<float>(text.size()) * 10.0f; };
        return config;
    }

    // gitman policy를 꽂은 controller다. policy는 controller보다 오래 살아야 하므로
    // 둘을 함께 든다.
    struct interaction_harness
    {
        ui::gitman_policy policy {};
        luil::interaction_controller controller { &policy, test_interaction_config() };

        explicit interaction_harness(std::shared_ptr<const luil::ui_tree> tree = {})
        {
            if (tree != nullptr)
                controller.set_tree(std::move(tree));
        }

        [[nodiscard]] std::vector<luil::input_action> process(const luil::raw_input_event& event)
        {
            return controller.process(event);
        }

        [[nodiscard]] const luil::interaction_snapshot& snapshot() const noexcept
        {
            return controller.snapshot();
        }
    };

    // `bounds`의 왼쪽 위 안쪽 1px를 누르고 뗀다. 누름은 액션을 내지 않는다.
    [[nodiscard]] inline std::vector<luil::input_action> click(luil::interaction_controller& controller, const luil::rect_f& bounds, const int time_milliseconds = 0, const luil::pointer_button button = luil::pointer_button::left)
    {
        const float x { bounds.x + 1.0f };
        const float y { bounds.y + 1.0f };
        const auto pressed { controller.process(luil::pointer_pressed_event { x, y, button, at(time_milliseconds) }) };
        REQUIRE(pressed.empty());
        return controller.process(luil::pointer_released_event { x, y, button, at(time_milliseconds) });
    }

    [[nodiscard]] inline std::vector<luil::input_action> click(interaction_harness& harness, const luil::rect_f& bounds, const int time_milliseconds = 0, const luil::pointer_button button = luil::pointer_button::left)
    {
        return click(harness.controller, bounds, time_milliseconds, button);
    }

    // 액션 하나에 든 logic 메시지다. 없거나 둘 이상이면 nullptr다.
    [[nodiscard]] inline const logic_message* as_message(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1u)
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions.front()) };
        if (message == nullptr)
            return nullptr;
        return message->get<logic_message>();
    }

    // 액션 목록의 `index`번째 logic 메시지다. 종류가 다르면 nullptr다.
    [[nodiscard]] inline const logic_message* message_at(const std::vector<luil::input_action>& actions, const std::size_t index)
    {
        if (index >= actions.size())
            return nullptr;
        const auto* const message { std::get_if<luil::app_message>(&actions[index]) };
        return message != nullptr ? message->get<logic_message>() : nullptr;
    }

    template<typename intent_type>
    [[nodiscard]] const intent_type* as_intent(const std::vector<luil::input_action>& actions)
    {
        const logic_message* const message { as_message(actions) };
        return message != nullptr ? std::get_if<intent_type>(message) : nullptr;
    }

    // 액션 하나에 든 앱 UI 명령이다 (파일 dialog·셸 실행). 없으면 nullptr다.
    [[nodiscard]] inline const luil::app_ui_command* as_app_command(const std::vector<luil::input_action>& actions)
    {
        if (actions.size() != 1u)
            return nullptr;
        return std::get_if<luil::app_ui_command>(&actions.front());
    }

    // luil의 불투명 앱 메시지에서 logic 메시지를 되돌린다 (driver 경계와 같은 걸음).
    [[nodiscard]] inline const logic_message* unwrap(const luil::app_message& message)
    {
        return message.get<logic_message>();
    }
} // namespace gitman::testing
