#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>

namespace gitman::ui {
    // 로컬 변경 확인 dialog의 본체다 (field-feedback-design 2.3). panel 안 상단에 변경
    // 항목 목록, 하단에 선택 항목의 diff viewer를 세로로 나눠 담는다.
    //
    // 이 element는 luil `modal_host_element`의 content다 (설계 D2-A). 화면을 덮는
    // scrim·가운데 배치·초점 가둠·Esc는 host가 맡고, `arrange`가 받는 slot이 곧
    // panel 상자다.
    class local_changes_dialog_element final : public ui_element
    {
    public:
        // 스크롤 값이 논리 픽셀이라(설계 L5) 막대가 배율을 스스로 곱하고, 조립
        // 시점에 배율이 필요 없다.
        explicit local_changes_dialog_element(local_changes_dialog_view dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        local_changes_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* diff_ { nullptr };
        ui_element* close_ { nullptr };
        ui_element* list_scrollbar_ { nullptr };
        ui_element* diff_scrollbar_ { nullptr };
    };

    // 목록 행의 element id다. owner 값에 index를 담는다 (switch dialog와 같은 규칙).
    [[nodiscard]] ui_element_id local_changes_item_id(std::size_t index);

    // 본체를 host에 담아 돌려준다. 배경(scrim) 클릭과 Esc가 모두 닫기다.
    // `window_width`·`window_height`는 논리 픽셀이며 panel이 창을 넘지 않게 하는 데 쓴다.
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_local_changes_dialog(const local_changes_dialog_view& view, float window_width, float window_height);
} // namespace gitman::ui
