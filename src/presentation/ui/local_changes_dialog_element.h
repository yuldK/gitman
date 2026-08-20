#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>

namespace gitman::ui {
    // 로컬 변경 확인 dialog다 (field-feedback-design 2.3). 화면 전체를 덮는 dim 배경
    // 위에 panel을 띄우고, 상단에 변경 항목 목록, 하단에 선택 항목의 diff viewer를
    // 세로로 나눠 담는다. 배경 클릭과 Esc가 닫기다.
    class local_changes_dialog_element final : public ui_element
    {
    public:
        // `scale`은 스크롤 막대의 content·viewport 계산(물리 픽셀)에 필요하다.
        local_changes_dialog_element(local_changes_dialog_view dialog, float scale);

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
} // namespace gitman::ui
