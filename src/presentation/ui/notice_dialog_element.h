#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <memory>
#include <vector>

namespace gitman::ui {
    // 확인 버튼 하나짜리 알림 다이얼로그의 본체다 (app-shell-design A3.2). 파일 연결
    // 등록·제거처럼 UI thread가 곧바로 수행한 작업의 결과를 앱 스타일로 알린다.
    //
    // 이 element는 luil `modal_host_element`의 content다 (설계 D2-A). scrim·가운데
    // 배치·초점 가둠·Esc는 host가 맡고, 여기서는 panel과 확인 버튼만 그린다 —
    // `arrange`가 받는 slot이 곧 panel 상자다.
    class notice_dialog_element final : public ui_element
    {
    public:
        // `lines`는 본문 줄마다 factory가 panel 폭에서 바꾼 줄이다 (`make_notice_dialog`).
        notice_dialog_element(const notice_dialog_view& dialog, std::vector<luil::text_block_layout> lines);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        notice_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* confirm_ { nullptr };
    };

    // 본체를 host에 담아 돌려준다. 배경(scrim) 클릭과 Esc가 모두 닫기다.
    // `window_width`·`window_height`는 논리 픽셀이며 panel이 창을 넘지 않게 하는 데 쓴다.
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_notice_dialog(const notice_dialog_view& view, float window_width, float window_height);
} // namespace gitman::ui
