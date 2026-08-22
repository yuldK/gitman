#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <utility>
#include <vector>

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
        // 문서가 덮어쓴 행의 `덮어씀` 배지다 (행 index, element). 문서 모드에서
        // 덮어쓴 행에만 만들어지고 클릭이 그 행의 문서 정의를 지운다 (G3.2).
        std::vector<std::pair<std::size_t, ui_element*>> badges_ {};
        // 외양 행의 컨트롤이다 (theme-and-banner-menu-design T3.3). 테마 세 칸과
        // 색 동그라미들이며, index가 아니라 이 목록으로 배치한다.
        std::vector<ui_element*> theme_options_ {};
        std::vector<ui_element*> swatches_ {};
    };
} // namespace gitman::ui
