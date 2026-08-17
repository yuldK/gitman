#pragma once

#include "presentation/ui/ui_element.h"

#include <string>

namespace gitman::ui {
    // caption 아래 도구 막대다. 문서 경로와 전체 새로 고침, 문서가 없을 때의 열기
    // 버튼을 담는다.
    class toolbar_element final : public ui_element
    {
    public:
        toolbar_element(std::u8string document_text, bool show_open_button);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        ui_element* document_label_ { nullptr };
        ui_element* refresh_all_ { nullptr };
        ui_element* open_document_ { nullptr };
    };
} // namespace gitman::ui
