#pragma once

#include "presentation/ui/caption_metrics.h"
#include "presentation/ui/ui_element.h"
#include "presentation/ui/ui_tree.h"

#include <string>

namespace gitman::ui {
    [[nodiscard]] ui_element_id caption_button_element_id(caption_button_hover hover) noexcept;

    // custom caption 막대다. 제목·앱 아이콘을 직접 그리고 창 버튼 3개를 일반
    // button element로 담는다. 버튼 액션은 `ui_command`를 반환한다. 창 끌기 영역
    // 판정(WM_NCHITTEST)은 platform의 `caption_layout`이 같은 metrics로 동기
    // 계산하며, 두 계산의 일치는 test로 고정한다.
    class caption_element final : public ui_element
    {
    public:
        explicit caption_element(std::u8string title, caption_ui_metrics metrics = default_caption_ui_metrics);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        std::u8string title_ {};
        caption_ui_metrics metrics_ {};
        ui_element* minimize_ { nullptr };
        ui_element* maximize_ { nullptr };
        ui_element* close_ { nullptr };
    };

    // smoke 화면처럼 view snapshot 없이 caption만 그릴 때 쓰는 단독 tree다.
    [[nodiscard]] ui_tree make_caption_tree(float window_width, float scale, std::u8string title);
} // namespace gitman::ui
