#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>

namespace gitman::ui {
    // 목록 행의 element id다. owner의 값에 index를 담아 행마다 다른 정체성을 준다.
    [[nodiscard]] ui_element_id font_dialog_item_id(std::size_t index);

    // 글꼴 선택 dialog의 **본체**다 (fonts-and-in-app-dialogs-design F2.5). luil
    // `modal_host_element`의 content로 들어간다 — scrim·가운데 배치·초점 가둠·Esc는
    // host가 맡고 (설계 D2-A) 본체는 host가 잡아 준 panel 상자를 그대로 받는다.
    // 목록의 각 행을 그 글꼴로 그려 미리 보기가 되고, 아래 칸이 고른 글꼴로 예문을
    // 보여 준다. 이름 필터는 switch dialog의 질의 막대와 같은 텍스트 박스다.
    class font_dialog_element final : public ui_element
    {
    public:
        // 스크롤 막대에 넘기는 치수가 논리 픽셀이라(설계 L4) 조립 시점에 배율이 필요 없다.
        explicit font_dialog_element(const font_dialog_view& dialog);

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

    // 글꼴 선택 dialog를 host째 만든다 (설계 D2-A). `window_width`·`window_height`는
    // **논리 픽셀**이며, panel이 창을 넘지 않게 여기서 미리 줄여 host에 넘긴다.
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_font_dialog(const font_dialog_view& view, float window_width, float window_height);
} // namespace gitman::ui
