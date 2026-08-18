#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

namespace gitman::ui {
    // 선택 카드 전용 하단 로그 pane이다 (REQ-008, stage-7-plan 4.3). 헤더에 카드
    // 이름과 필터·자동 스크롤·복사·지우기 버튼을 담고, 본문은 필터를 통과한 로그
    // record를 스크롤 위치대로 그린다. 입력 기능은 없다.
    class log_view_element final : public ui_element
    {
    public:
        explicit log_view_element(log_view_model log);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        void draw_records(draw_context& context, const rect_f& body) const;

        log_view_model log_ {};
        ui_element* title_ { nullptr };
        ui_element* filter_ { nullptr };
        ui_element* autoscroll_ { nullptr };
        ui_element* copy_ { nullptr };
        ui_element* clear_ { nullptr };
        // arrange가 정하는 본문 영역이다 (헤더 아래).
        rect_f body_ {};
    };
} // namespace gitman::ui
