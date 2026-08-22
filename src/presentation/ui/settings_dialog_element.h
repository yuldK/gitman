#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <utility>
#include <vector>

namespace gitman::ui {
    // 환경설정 dialog다 (REQ-017, stage-8-plan 5.1,
    // docs/settings-tabs-and-appearance-scope-design.md S1). 화면 전체를 덮는 dim
    // 배경 위에 panel을 띄우고, 왼쪽 탭 rail이 고른 탭의 섹션과 항목만 담는다.
    // 경로는 자유 입력 없이 찾아보기(파일 선택)와 지우기로만 바꾼다. 배경 클릭과
    // Esc가 취소다.
    class settings_dialog_element final : public ui_element
    {
    public:
        explicit settings_dialog_element(settings_dialog_view dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        settings_dialog_view dialog_ {};
        // panel은 활성 탭의 모델과 배치를 함께 들고 있다. dialog는 panel이 계산한
        // 항목 사각형 위에 컨트롤을 얹으므로 그리기와 배치가 어긋나지 않는다.
        ui_element* panel_ { nullptr };
        // 네 탭 중 가장 높은 내용에 맞춘 panel 높이다. 탭을 옮길 때 창이 튀지 않게
        // 고정한다 (S1.3).
        float panel_height_ { 0.0f };
        std::vector<ui_element*> tab_items_ {};
        // 활성 탭의 항목과 같은 순서다. 항목 하나가 컨트롤을 0~2개 갖는다.
        std::vector<std::vector<ui_element*>> item_controls_ {};
        // 문서가 덮어쓴 항목의 `덮어씀` 배지다 (항목 index, element). 문서 모드에서
        // 덮어쓴 항목에만 만들어지고 클릭이 그 항목의 문서 정의를 지운다 (S4.2).
        std::vector<std::pair<std::size_t, ui_element*>> badges_ {};
        // 외양 탭의 컨트롤이다. 테마 세 칸과 색 동그라미들이며 index가 아니라 이
        // 목록으로 배치한다.
        std::vector<ui_element*> theme_options_ {};
        std::vector<ui_element*> swatches_ {};
        ui_element* confirm_ { nullptr };
        ui_element* cancel_ { nullptr };
    };
} // namespace gitman::ui
