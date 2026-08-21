#pragma once

#include "presentation/ui/ui_element.h"

#include <string>

namespace gitman::ui {
    // caption 아래 도구 막대다. 문서 경로와 전체 새로 고침, 문서가 없을 때의
    // 열기 버튼, 문서가 열려 있을 때의 닫기 버튼을 담는다. 새 문서 만들기는
    // 시작 페이지가 유일한 진입점이라 여기에 두지 않는다 (2026-08-22 사용자 지시).
    class toolbar_element final : public ui_element
    {
    public:
        // `document_open`은 열린 문서가 필요한 버튼(환경설정, 탐색 등록, 닫기)의
        // 활성 조건이다. 문서 열기는 반대로 문서가 없을 때만 둔다.
        toolbar_element(std::u8string document_text, bool show_open_button, bool relative_paths, bool document_open);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        ui_element* document_label_ { nullptr };
        ui_element* refresh_all_ { nullptr };
        ui_element* open_document_ { nullptr };
        ui_element* close_document_ { nullptr };
        ui_element* toggle_path_display_ { nullptr };
        ui_element* settings_ { nullptr };
        ui_element* discover_ { nullptr };
        // 문서가 없을 때만 열기 버튼을, 있을 때만 닫기 버튼을 둔다. arrange가
        // 자리 부족으로 숨긴 것과 구분해야 해서 생성 시 결정을 따로 기억한다.
        bool show_open_button_ { false };
        bool document_open_ { false };
    };
} // namespace gitman::ui
