#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

namespace gitman::ui {
    // 선택 카드 전용 하단 로그 pane이다 (REQ-008, stage-7-plan 4.3). 헤더에 카드
    // 이름과 필터·자동 스크롤·복사·지우기 버튼을 담고, 본문은 progress 접기가
    // 적용된 표시 줄을 스크롤 위치대로 그린다. 오른쪽에 시각적 스크롤 막대를 둔다
    // (stage-8-plan 5.3). 입력 기능은 없다.
    class log_view_element final : public ui_element
    {
    public:
        // `body_viewport_height`는 본문(헤더 아래) 높이의 물리 픽셀 값이다. 스크롤
        // 막대의 thumb 계산이 생성 시점에 내용·화면 높이를 알아야 해서 받는다.
        log_view_element(log_view_model log, float body_viewport_height, float scale);

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
        ui_element* scrollbar_ { nullptr };
        // arrange가 정하는 본문 영역이다 (헤더 아래).
        rect_f body_ {};
        // 막대가 보일 때 본문 글자가 막대를 피해 줄어드는 폭이다 (물리 픽셀).
        float scrollbar_reserved_ { 0.0f };
        // 실행 중 변경 작업의 경과 시간(MM:SS)을 그리는 오른쪽 한계다 (물리 픽셀).
        // 0이면 자리가 없거나 실행 중이 아니라 그리지 않는다.
        float elapsed_right_ { 0.0f };
    };
} // namespace gitman::ui
