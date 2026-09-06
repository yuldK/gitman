#pragma once

#include "luil/ui/modal_host_element.h"
#include "luil/ui/ui_element.h"

#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    // in-app dialog 하나의 뼈대 명세다 (설계 L6, D2-A). scrim·가운데 배치·초점 가둠·Esc는
    // luil `modal_host_element`가 맡고, gitman은 본체(panel·행·버튼)만 그린다.
    struct dialog_host_spec
    {
        // dialog마다 다른 owner다 (`dialog_owner_*`). host·scrim id를 가른다.
        std::u8string owner {};
        // 본체의 논리 크기다. 창보다 크면 factory가 미리 줄여 넣는다.
        float content_width { 0.0f };
        float content_height { 0.0f };
        // scrim 클릭이다. 배경 클릭이 취소였던 dialog는 그 intent를, 아니면 비워 흡수만 한다.
        luil::ui_action outside {};
        // 가둠이 떠 있는 동안의 Esc다. 비우면 Esc가 앱 정책으로 흐른다.
        luil::ui_action dismiss {};
    };

    // 명세대로 host를 만들고 본체를 content로 넣는다. 자동 초점(focus_entry)과 되돌리기
    // (focus_return)는 두지 않는다 — 열릴 때 자동 초점을 주지 않는 지금 규칙을 지킨다.
    [[nodiscard]] inline std::unique_ptr<luil::modal_host_element> make_dialog_host(dialog_host_spec spec, std::unique_ptr<luil::ui_element> content)
    {
        luil::modal_host_config config {};
        config.owner = std::move(spec.owner);
        config.scrim_opacity = 0.45f;
        config.outside = std::move(spec.outside);
        config.dismiss = std::move(spec.dismiss);
        config.content_width = spec.content_width;
        config.content_height = spec.content_height;
        auto host { std::make_unique<luil::modal_host_element>(std::move(config)) };
        host->set_content(std::move(content));
        return host;
    }
} // namespace gitman::ui
