#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>

namespace gitman::ui {
    // 후보 행 element의 id다. index를 owner 값에 담아 tree 재빌드 후에도 같은 행이
    // 같은 정체성을 가진다.
    [[nodiscard]] ui_element_id discovery_dialog_item_id(std::size_t index);

    // 탐색 후보 선택 등록 dialog다 (REQ-004, stage-8-plan 5.2). 화면 전체를 덮는
    // dim 배경 위에 panel을 띄우고 체크박스가 붙은 후보 행 목록과 등록·취소 버튼을
    // 담는다. 배경 클릭과 Esc가 취소다 (등록 실행 중에는 logic이 닫기를 거부한다).
    class discovery_dialog_element final : public ui_element
    {
    public:
        // scale은 스크롤 막대의 content·viewport 계산(물리 픽셀)에 필요하다.
        discovery_dialog_element(discovery_dialog_view dialog, float scale);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        discovery_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* scrollbar_ { nullptr };
    };
} // namespace gitman::ui
