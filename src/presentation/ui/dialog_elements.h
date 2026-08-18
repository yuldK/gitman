#pragma once

#include "presentation/ui/ui_element.h"

#include <string>

namespace gitman::ui {
    // in-app dialog(update overlay, switch dialog)가 공유하는 글자 버튼이다.
    // 아이콘 전용 button_element와 달리 텍스트를 중앙에 그린다. hover·눌림·비활성
    // 표시는 같은 규칙을 쓴다.
    class text_button_element final : public ui_element
    {
    public:
        text_button_element(ui_element_id id, std::u8string text, bool accent);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        std::u8string text_ {};
        bool accent_ { false };
    };
} // namespace gitman::ui
