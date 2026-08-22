#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>

namespace gitman::ui {
    // 카드 body 또는 배너 우클릭이 여는 컨텍스트 메뉴다 (field-feedback-design
    // 3장, theme-and-banner-menu-design T1). 화면 전체를 덮는 투명 overlay가 바깥
    // 클릭을 흡수해 닫고, 앵커 좌표에 소형 panel을 붙인다. panel이 창 밖으로 나가면
    // 안쪽으로 민다. Esc와 항목 실행도 닫는다.
    class context_menu_element final : public ui_element
    {
    public:
        explicit context_menu_element(context_menu_view menu);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        context_menu_view menu_ {};
        ui_element* panel_ { nullptr };
    };

    // 항목 행의 element id다. owner 값에 index를 담는다 (switch dialog와 같은 규칙).
    [[nodiscard]] ui_element_id context_menu_item_id(std::size_t index);
} // namespace gitman::ui
