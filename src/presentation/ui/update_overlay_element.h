#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

namespace gitman::ui {
    // Git update 확인 overlay다 (stage-7-plan 4.4). 화면 전체를 덮는 dim 배경 위에
    // panel을 띄우고 submodule option과 실행·취소 버튼을 담는다. 배경 클릭과 Esc가
    // 취소다.
    class update_overlay_element final : public ui_element
    {
    public:
        explicit update_overlay_element(update_overlay_view overlay);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        update_overlay_view overlay_ {};
        ui_element* panel_ { nullptr };
    };
} // namespace gitman::ui
