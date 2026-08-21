#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <string_view>

namespace gitman::ui {
    // 후보 행의 element id를 만든다. owner의 값에 index를 담아 행마다 다른 정체성을
    // 준다. hover 강조와 클릭 판정이 이 값으로 행을 구분한다.
    [[nodiscard]] ui_element_id switch_dialog_item_id(std::size_t index);
    [[nodiscard]] ui_element_id switch_dialog_svn_item_id(std::u8string_view url);
    [[nodiscard]] ui_element_id switch_dialog_svn_expand_id(std::u8string_view url);

    // switch dialog다. Git은 기존 remote-first 후보 목록을, SVN은 lazy repository
    // tree를 그린다. 검증·거부 메시지와 실행·취소 버튼을 공유하며 배경 클릭과 Esc가
    // 취소다.
    class switch_dialog_element final : public ui_element
    {
    public:
        // scale과 창 높이(물리 픽셀)는 스크롤 막대의 content·viewport 계산에
        // 필요하다. 목록 높이가 창 높이를 따라 줄어드는 dialog라 view의 창 크기를
        // 함께 받는다.
        switch_dialog_element(switch_dialog_view dialog, float scale, float window_height);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        switch_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* scrollbar_ { nullptr };
    };
} // namespace gitman::ui
