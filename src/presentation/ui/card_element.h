#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include <chrono>
#include <cstddef>
#include <optional>
#include <vector>

namespace gitman::ui {
    // 카드 한 장이다. 상태 아이콘과 텍스트를 직접 그리고 오른쪽 열의 버튼을 자식으로 담는다.
    // 열은 `[card 기능 0~3][⋯] │ [새로 고침][업데이트][전환]`이다 (function-buttons-design 2.3). 적용 기능이 없으면
    // `⋯`와 구분선 없이 지금처럼 내장 세 버튼뿐이다. 버튼만 luil `button_element`이고 본문 그리기와
    // drag 출발은 그대로 커스텀이다 (설계 L5).
    class card_element final : public ui_element
    {
    public:
        explicit card_element(card_view_model card);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 진행 중인 카드의 sync 아이콘이 돈다 (2026-08-23 지시). 도는 동안은 지금
        // 시각을 돌려주어 platform이 계속 다시 그리게 한다 (설계 L8). 기능 실행 중에도 돈다 —
        // 3번째 줄 칩의 경과 시간이 함께 흐른다 (function-buttons-design 2.3.5).
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        // 목록의 항목 하나로 읽힌다. 이름은 화면의 세 줄(이름·경로·상태 조각)을 잇는다.
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 세 번째 줄(브랜치·리비전·상태·작업 트리)을 색이 있는 조각으로 그린다.
        void draw_status_row(draw_context& context, const rect_f& row) const;

        // `card` 배치 기능 버튼 하나와 그 실패 점이다. 점은 실패했고 실행 중이 아닐 때만 있다.
        struct function_button_slot
        {
            ui_element* button { nullptr };
            ui_element* marker { nullptr };
            // `card_.functions` 안의 순번이다.
            std::size_t function { 0 };
        };

        card_view_model card_ {};
        ui_element* refresh_ { nullptr };
        ui_element* update_ { nullptr };
        ui_element* switch_ { nullptr };
        // 문서 순서로 이 카드에 적용되는 `card` 기능 버튼이다 (최대 3).
        std::vector<function_button_slot> function_buttons_ {};
        // `⋯`와 그 점이다. 적용 기능이 없으면 없다. 점은 고정하지 않았거나 접힌 기능의 실행 중·실패다.
        ui_element* more_ { nullptr };
        ui_element* more_marker_ { nullptr };
        // 구분선의 가운데 x다 (물리 px). `⋯`가 보일 때만 그린다.
        std::optional<float> separator_x_ {};
        // 이름·경로·상태 줄이 쓸 수 있는 오른쪽 한계다. arrange가 버튼 배치 뒤에
        // 정한다. 창이 좁아 버튼이 숨겨지면 그만큼 넓어진다.
        float text_limit_ { 0.0f };
    };
} // namespace gitman::ui
