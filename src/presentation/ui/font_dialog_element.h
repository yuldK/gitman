#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include <cstddef>

namespace gitman::ui {
    // 목록 행의 element id다. owner의 값에 index를 담아 행마다 다른 정체성을 준다.
    [[nodiscard]] ui_element_id font_dialog_item_id(std::size_t index);

    // 글꼴 선택 dialog다 (fonts-and-in-app-dialogs-design F2.5). 목록의 각 행을 그
    // 글꼴로 그려 미리 보기가 되고, 아래 칸이 고른 글꼴로 예문을 보여 준다. 이름
    // 필터는 switch dialog의 질의 막대와 같은 텍스트 박스다.
    class font_dialog_element final : public ui_element
    {
    public:
        // scale 인자는 tree를 짓는 쪽의 호출 모양을 지키려고 남긴다. 스크롤 막대에
        // 넘기는 치수가 논리 픽셀이 되어(설계 L4) 조립 시점에는 배율이 필요 없다.
        font_dialog_element(const font_dialog_view& dialog, float scale);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        font_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* scrollbar_ { nullptr };
        ui_element* query_input_ { nullptr };
        ui_element* query_clear_ { nullptr };
        ui_element* confirm_ { nullptr };
        ui_element* cancel_ { nullptr };
    };
} // namespace gitman::ui
