#pragma once

#include "presentation/ui/ui_element.h"

#include <string>

namespace gitman::ui {
    // caption 아래 도구 막대다. 문서 경로와 전체 새로 고침, 문서 생성, 문서가 없을
    // 때의 열기 버튼을 담는다.
    class toolbar_element final : public ui_element
    {
    public:
        // `document_open`은 열린 문서가 필요한 버튼(환경설정, 탐색 등록)의 활성
        // 조건이다.
        toolbar_element(std::u8string document_text, bool show_open_button, bool generation_busy, bool relative_paths, bool document_open);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        ui_element* document_label_ { nullptr };
        ui_element* refresh_all_ { nullptr };
        ui_element* open_document_ { nullptr };
        ui_element* generate_document_ { nullptr };
        ui_element* toggle_path_display_ { nullptr };
        ui_element* settings_ { nullptr };
        ui_element* discover_ { nullptr };
        // 문서가 없을 때만 열기 버튼을 둔다. arrange가 자리 부족으로 숨긴 것과
        // 구분해야 해서 생성 시 결정을 따로 기억한다.
        bool show_open_button_ { false };
    };
} // namespace gitman::ui
