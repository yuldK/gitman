#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <vector>

namespace gitman::ui {
    // 후보 행의 element id를 만든다. owner의 값에 index를 담아 행마다 다른 정체성을
    // 준다. hover 강조와 클릭 판정이 이 값으로 행을 구분한다.
    [[nodiscard]] ui_element_id switch_dialog_item_id(std::size_t index);

    // switch dialog다 (REQ-007, stage-7-plan 4.5). dim 배경 위 panel에 remote 후보
    // 먼저인 목록(스크롤 가능), stale 표시, 검증·거부 메시지와 실행·취소 버튼을
    // 담는다. 배경 클릭과 Esc가 취소다.
    class switch_dialog_element final : public ui_element
    {
    public:
        explicit switch_dialog_element(switch_dialog_view dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        switch_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        // panel 자식 중 후보 행이 시작하는 위치다. 행 앞에 버튼 2개가 있다.
        std::vector<std::size_t> visible_items_ {};
    };
} // namespace gitman::ui
