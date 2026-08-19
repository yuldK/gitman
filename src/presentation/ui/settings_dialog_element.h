#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

namespace gitman::ui {
    // 환경설정 dialog다 (REQ-017, stage-8-plan 5.1). 화면 전체를 덮는 dim 배경 위에
    // panel을 띄우고 Git/SVN 실행 파일 경로 행과 저장·취소 버튼을 담는다. 경로는
    // 자유 입력 없이 찾아보기(파일 선택)와 지우기로만 바꾼다. 배경 클릭과 Esc가
    // 취소다.
    class settings_dialog_element final : public ui_element
    {
    public:
        explicit settings_dialog_element(settings_dialog_view dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        settings_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
    };
} // namespace gitman::ui
