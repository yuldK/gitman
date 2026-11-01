#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include <chrono>
#include <optional>

namespace gitman::ui {
    // 카드 한 장이다. 상태 아이콘과 텍스트를 직접 그리고 refresh/update/switch
    // 버튼 3개를 자식으로 담는다. update와 switch는 단계 7 전까지 비활성이며
    // tooltip으로 사유를 안내한다. 버튼만 luil `button_element`이고 본문 그리기와
    // drag 출발은 그대로 커스텀이다 (설계 L5).
    class card_element final : public ui_element
    {
    public:
        explicit card_element(card_view_model card);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 진행 중인 카드의 sync 아이콘이 돈다 (2026-08-23 지시). 도는 동안은 지금
        // 시각을 돌려주어 platform이 계속 다시 그리게 한다 (설계 L8).
        [[nodiscard]] std::optional<std::chrono::steady_clock::time_point> next_update(const update_context& context, const interaction_snapshot& interaction) const override;
        // 목록의 항목 하나로 읽힌다. 이름은 화면의 세 줄(이름·경로·상태 조각)을 잇는다.
        [[nodiscard]] access_info accessibility() const override;

    private:
        // 세 번째 줄(브랜치·리비전·상태·작업 트리)을 색이 있는 조각으로 그린다.
        void draw_status_row(draw_context& context, const rect_f& row) const;

        card_view_model card_ {};
        ui_element* refresh_ { nullptr };
        ui_element* update_ { nullptr };
        ui_element* switch_ { nullptr };
        // 이름·경로·상태 줄이 쓸 수 있는 오른쪽 한계다. arrange가 버튼 배치 뒤에
        // 정한다. 창이 좁아 버튼이 숨겨지면 그만큼 넓어진다.
        float text_limit_ { 0.0f };
    };
} // namespace gitman::ui
