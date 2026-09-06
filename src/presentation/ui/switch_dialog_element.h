#pragma once

#include "presentation/ui/dialog_host.h"
#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace gitman::ui {
    // 후보 행의 element id를 만든다. owner의 값에 index를 담아 행마다 다른 정체성을
    // 준다. hover 강조와 클릭 판정이 이 값으로 행을 구분한다.
    [[nodiscard]] ui_element_id switch_dialog_item_id(std::size_t index);
    [[nodiscard]] ui_element_id switch_dialog_svn_item_id(std::u8string_view url);
    [[nodiscard]] ui_element_id switch_dialog_svn_expand_id(std::u8string_view url);

    // switch dialog의 본체다. Git은 기존 remote-first 후보 목록을, SVN은 lazy
    // repository tree를 그린다. 검증·거부 메시지와 실행·취소 버튼을 공유한다.
    // 이 element는 luil `modal_host_element`의 content다 (설계 D2-A) — scrim·가운데
    // 배치·초점 가둠·Esc는 host가 맡고, 여기서는 panel 상자 안만 그린다.
    class switch_dialog_element final : public ui_element
    {
    public:
        // 창 높이(논리 픽셀)는 스크롤 막대의 viewport와 목록 영역 높이에 필요하다.
        // 목록 높이가 창 높이를 따라 줄어드는 dialog라 창 크기를 함께 받는다.
        switch_dialog_element(const switch_dialog_view& dialog, float window_height);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        switch_dialog_view dialog_ {};
        // 창 높이다 (논리 픽셀). `switch_dialog_list_height`가 이 값을 받는다.
        float window_height_ { 0.0f };
        ui_element* panel_ { nullptr };
        ui_element* scrollbar_ { nullptr };
        // 제목 아래 질의 막대의 세 칸이다 (switch-dialog-query-bar-design Q1).
        ui_element* query_refresh_ { nullptr };
        ui_element* query_input_ { nullptr };
        ui_element* query_action_ { nullptr };
        ui_element* confirm_ { nullptr };
        ui_element* cancel_ { nullptr };
    };

    // 본체를 luil modal host에 담아 만든다 (설계 D2-A). 창 크기는 논리 픽셀이며,
    // panel 크기를 창에 맞게 줄이는 것도 여기서 한다. 배경 클릭과 Esc는 둘 다
    // dialog 취소다.
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_switch_dialog(const switch_dialog_view& dialog, float window_width, float window_height);
} // namespace gitman::ui
