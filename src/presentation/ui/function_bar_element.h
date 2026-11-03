#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include <optional>
#include <string>
#include <vector>

namespace gitman::ui {
    class function_marker_element;

    // 선택 카드 기능 줄이다 (function-buttons-design 2.2.2, 2.4). 로그 pane 160 안 맨 위 36에 서고 구성은
    // `[카드 이름 │ bar 기능 글자 버튼 …… ⋯]`이다. 버튼은 luil 글자 버튼이고, 줄 폭에 들지 않는 버튼은 오른쪽부터
    // 숨겨 줄의 `⋯`(그 카드의 기능 메뉴)에 맡긴다. 버튼이 없으면 그 자리에 힌트를 그린다.
    //  - 빈 곳 클릭이 root의 선택 해제로 흐르지 않게 hit를 흡수한다 (로그 pane과 같다).
    //  - 버튼과 줄의 `⋯`는 Tab 자리다. 줄 자신은 자리가 아니다.
    //  - 버튼 폭은 가장 긴 상태 글(`라벨 · 실행 중`)로 정해 상태가 바뀌어도 같다 (2.2.4).
    //  - 카드 이름이나 힌트가 칸에 들지 않아 줄여 그리면 줄 자신의 tooltip이 줄이기 전 글을 보인다. 버튼은 자기 tooltip이 있다.
    class function_bar_element final : public ui_element
    {
    public:
        explicit function_bar_element(function_bar_view bar);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 도구 막대로 읽힌다. 이름은 `기능 줄`, 선택 카드 이름, 힌트다 — 힌트는 그린 글이라 따로 서지 않는다.
        [[nodiscard]] access_info accessibility() const override;

    private:
        struct button_slot
        {
            ui_element* button { nullptr };
            // 실패 표식(마름모)이다. 실패했고 실행 중이 아닐 때만 있다.
            function_marker_element* marker { nullptr };
            // 폭을 재는 글과 글리프다 (가장 긴 상태 글).
            std::u8string widest {};
            char32_t glyph { 0 };
        };

        function_bar_view bar_ {};
        std::vector<button_slot> buttons_ {};
        ui_element* more_ { nullptr };
        // 배치가 정한 그리기 자리다 (물리 px). 이름이 없으면 `name_box_`의 폭이 0이다.
        rect_f name_box_ {};
        std::optional<float> separator_x_ {};
        rect_f hint_box_ {};
    };
} // namespace gitman::ui
