#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>

namespace gitman::ui {
    // 최근 항목 행과 그 제거 아이콘의 정체성이다. 컨텍스트 메뉴 항목처럼 owner 값에
    // index를 담아 구분한다 (app-shell-design A1.3).
    [[nodiscard]] ui_element_id start_page_recent_item_id(std::size_t index);
    [[nodiscard]] ui_element_id start_page_recent_remove_id(std::size_t index);

    // 열린 문서가 없을 때 카드 목록 자리를 채우는 시작 페이지다. 왼쪽은 문서 열기·
    // 새 문서 만들기, 오른쪽은 최근 항목 목록이다. 창이 좁으면 두 열을 세로로 쌓고,
    // 목록이 남은 높이를 넘으면 들어가는 만큼만 그린 뒤 남은 개수를 알린다.
    class start_page_element final : public ui_element
    {
    public:
        explicit start_page_element(const start_page_view& page);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        start_page_view page_ {};
        // arrange가 정한 표시 개수다. 나머지는 "외 N개"로 알린다.
        std::size_t visible_recents_ { 0 };
        rect_f title_ {};
        rect_f subtitle_ {};
        rect_f action_section_ {};
        rect_f recent_section_ {};
        rect_f recent_empty_ {};
    };
} // namespace gitman::ui
