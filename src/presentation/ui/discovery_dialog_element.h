#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>

namespace gitman::ui {
    // 후보 행 element의 id다. index를 owner 값에 담아 tree 재빌드 후에도 같은 행이
    // 같은 정체성을 가진다.
    [[nodiscard]] ui_element_id discovery_dialog_item_id(std::size_t index);

    // 탐색 후보 선택 등록 dialog의 본체다 (REQ-004, stage-8-plan 5.2). luil
    // `modal_host_element`의 content라 자기 자리는 panel 상자 하나뿐이다 — scrim·가운데
    // 배치·초점 가둠·Esc는 host의 몫이다 (설계 D2-A). 체크박스가 붙은 후보 행 목록과
    // 등록·취소 버튼을 담는다.
    class discovery_dialog_element final : public ui_element
    {
    public:
        // 스크롤 막대에 넘기는 치수가 논리 픽셀이라(설계 L4) 조립 시점에 배율이 필요 없다.
        explicit discovery_dialog_element(const discovery_dialog_view& dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        discovery_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* scrollbar_ { nullptr };
    };

    // 본체를 host에 담아 돌려준다. 창 크기는 **논리 픽셀**이다 (`view.window_* / scale`).
    // 배경 클릭과 Esc가 모두 취소다 (등록 실행 중에는 logic이 닫기를 거부한다).
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_discovery_dialog(const discovery_dialog_view& view, float window_width, float window_height);
} // namespace gitman::ui
