#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

namespace gitman::ui {
    // 확인 버튼 하나짜리 알림 다이얼로그다 (app-shell-design A3.2). 파일 연결
    // 등록·제거처럼 UI thread가 곧바로 수행한 작업의 결과를 앱 스타일로 알린다.
    // 배경 클릭·확인 버튼·Esc가 모두 닫기다.
    class notice_dialog_element final : public ui_element
    {
    public:
        explicit notice_dialog_element(notice_dialog_view dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        notice_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* confirm_ { nullptr };
    };
} // namespace gitman::ui
