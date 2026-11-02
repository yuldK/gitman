#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>
#include <string_view>

namespace gitman::ui {
    // 기능 편집 dialog의 본체다 (function-buttons-design 2.7). 기능 / 변수 두 탭, 왼쪽 목록(추가·복제·삭제·순서), 오른쪽
    // 폼(이름·아이콘·프로그램·인자·작업 폴더·제한 시간·실행 방식·적용 범위·배치·실행 전 확인), 아래 미리보기와 취소·적용
    // 버튼이다. 폭이 640보다 좁으면 목록을 위쪽 한 줄 선택기로 접고 폼을 전체 폭으로 쓴다.
    //
    // 초안은 logic이 들고, element는 그 표시 모델을 그린 뒤 intent만 낸다. scrim·가운데 배치·초점 가둠·Esc는 luil
    // `modal_host_element`가 맡는다.
    class function_editor_dialog_element final : public ui_element
    {
    public:
        explicit function_editor_dialog_element(const function_editor_view& dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        function_editor_view dialog_ {};
        ui_element* panel_ { nullptr };
        ui_element* list_ { nullptr };
        ui_element* form_ { nullptr };
        ui_element* list_scrollbar_ { nullptr };
        ui_element* form_scrollbar_ { nullptr };
    };

    // 목록 행의 id다. 값은 `function_editor_row_view::key`(`button-<id>`·`source-<N>`)라 순서를 바꿔도 같은 항목을 가리킨다.
    [[nodiscard]] ui_element_id function_editor_item_id(std::u8string_view key);

    // 본체를 host에 담아 돌려준다. 창 크기는 논리 픽셀이다. Esc는 취소이고, 배경 클릭은 흡수만 한다 — 실수로 누른
    // 배경이 고친 초안을 버리지 않게 한다.
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_function_editor_dialog(const function_editor_view& view, float window_width, float window_height);
}
