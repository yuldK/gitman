#pragma once

#include "application/app_commands.h"
#include "application/app_messages.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_events.h"

#include <string>
#include <utility>

namespace gitman::ui {
    // element 액션이 logic 메시지를 만드는 도우미다 (설계 L3 메시지 운반). luil의
    // 불투명 상자 `app_message`에 **`logic_message` variant를 통째로** 담는다. 받는
    // 쪽(driver)이 `get<logic_message>()`로 복원해 지금처럼 `std::visit`한다 —
    // variant의 exhaustiveness가 그대로 남는다.
    template<typename intent_type>
    [[nodiscard]] luil::input_action make_logic_action(intent_type intent)
    {
        return luil::make_app_action(logic_message { std::move(intent) });
    }

    // "누르면 이 intent 하나"인 클릭 액션이다. `set_action`과 config의 액션 자리에
    // 바로 꽂는다.
    template<typename intent_type>
    [[nodiscard]] luil::ui_action make_logic_click(intent_type intent)
    {
        return luil::make_message_action(logic_message { std::move(intent) });
    }

    // UI thread 전용 앱 명령이다 (설계 L7). delegate가 `parse_app_command`로 되돌린다.
    [[nodiscard]] inline luil::input_action make_app_command_action(const app_command command, std::u8string argument = {})
    {
        return luil::input_action { luil::app_ui_command { static_cast<std::uint32_t>(command), std::move(argument) } };
    }

    [[nodiscard]] inline luil::ui_action make_app_command_click(const app_command command, std::u8string argument = {})
    {
        return [command, argument = std::move(argument)](const luil::ui_action_context&) -> std::vector<luil::input_action> { return { make_app_command_action(command, argument) }; };
    }
} // namespace gitman::ui
