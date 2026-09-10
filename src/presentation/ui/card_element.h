#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include <chrono>
#include <optional>
#include <string>

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

    private:
        // 세 번째 줄(브랜치·리비전·상태·작업 트리)을 색이 있는 조각으로 그린다.
        void draw_status_row(draw_context& context, const rect_f& row) const;
        // 세 번째 줄 오른쪽 끝의 시간 표시다 (REQ-019). 실행 중이면 경과 `MM:SS`,
        // 아니면 마지막 완료 시각의 상대 표현이다.
        void draw_time(draw_context& context, const rect_f& row) const;
        // 지금 그릴 시간 글이다. 그릴 것이 없으면 비어 있다. draw와 next_update가
        // 같은 판정을 써야 "글은 있는데 갱신이 안 되는" 상태가 생기지 않는다.
        [[nodiscard]] std::u8string time_text(std::chrono::steady_clock::time_point now) const;

        card_view_model card_ {};
        ui_element* refresh_ { nullptr };
        ui_element* update_ { nullptr };
        ui_element* switch_ { nullptr };
        // 이름·경로·상태 줄이 쓸 수 있는 오른쪽 한계다. arrange가 버튼 배치 뒤에
        // 정한다. 창이 좁아 버튼이 숨겨지면 그만큼 넓어진다.
        float text_limit_ { 0.0f };
        // 시간 표시가 오른쪽 정렬될 x다. 0이면 자리가 없어 그리지 않는다 — 로그
        // 헤더의 `elapsed_right_`와 같은 관용구다 (log_view_element.h).
        float elapsed_right_ { 0.0f };
    };
} // namespace gitman::ui
