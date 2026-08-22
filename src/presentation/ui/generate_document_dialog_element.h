#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

namespace gitman::ui {
    // 새 문서 만들기 dialog다 (fonts-and-in-app-dialogs-design F3). 이전의 Win32 모달
    // 팝업을 앱 UI로 옮긴 것이라 색·글꼴·버튼 모양이 창의 나머지와 같다. 폴더 선택만
    // Win32 dialog에 맡긴다 — 앱이 파일 탐색기를 다시 만들 이유가 없다.
    class generate_document_dialog_element final : public ui_element
    {
    public:
        explicit generate_document_dialog_element(generate_document_dialog_view dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        generate_document_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* name_input_ { nullptr };
        ui_element* scan_browse_ { nullptr };
        ui_element* same_folder_ { nullptr };
        ui_element* location_browse_ { nullptr };
        ui_element* confirm_ { nullptr };
        ui_element* cancel_ { nullptr };
    };
} // namespace gitman::ui
