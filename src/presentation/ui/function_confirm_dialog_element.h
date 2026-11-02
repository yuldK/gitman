#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>

namespace gitman::ui {
    // 기능 실행 확인 dialog의 본체다 (function-buttons-design 3.10, 4.5). 한 element가 두 모드를
    // 그린다. 신뢰 모드는 문서의 기능 정의 전체(치환 전 원문)를, 미리보기 모드는 `confirm` 버튼
    // 하나의 표시용 명령줄과 작업 폴더를 목록으로 보인다.
    //
    // 구조는 탐색 dialog를 따른다. scrim·가운데 배치·초점 가둠·Esc는 luil `modal_host_element`가
    // 맡고, 여기서는 panel(제목·안내·경고와 잘린 목록), 버튼 둘, 스크롤 막대를 둔다.
    class function_confirm_dialog_element final : public ui_element
    {
    public:
        // `window_height`는 논리 픽셀이며 목록 높이를 logic의 스크롤 한계와 같은 함수로 정하는 데 쓴다.
        function_confirm_dialog_element(const function_confirm_dialog_view& dialog, float window_height);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        function_confirm_dialog_view dialog_ {};
        float window_height_ { 0.0f };
        ui_element* panel_ { nullptr };
        ui_element* scrollbar_ { nullptr };
    };

    [[nodiscard]] ui_element_id function_confirm_dialog_item_id(std::size_t index);

    // 본체를 host에 담아 돌려준다. 배경 클릭과 Esc는 모두 취소다.
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_function_confirm_dialog(const function_confirm_dialog_view& view, float window_width, float window_height);
}
