#pragma once

#include "presentation/ui/ui_element.h"

namespace gitman::ui {
    // 카드 목록의 세로 스크롤 막대다. 표시뿐 아니라 클릭·끌기로 스크롤 위치를 바꾼다.
    // 좌표 변화량만 메시지로 바꾸므로(상대 이동) tree가 다시 빌드되어도 끌기가
    // 이어진다. 값은 모두 물리 픽셀이며 scroll intent만 논리 픽셀로 되돌린다.
    class scrollbar_element final : public ui_element
    {
    public:
        scrollbar_element(float content_height, float viewport_height, float scroll_offset, float scale);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        // 끌 수 있는 여유가 없으면(내용이 화면보다 짧거나 thumb가 track을 다 채우면)
        // 클릭과 끌기가 아무 일도 하지 않는다.
        [[nodiscard]] bool draggable() const noexcept;
        // thumb를 pixels(물리)만큼 움직이는 스크롤 변화량이다 (논리 픽셀).
        [[nodiscard]] float scroll_delta_for(float pixels) const noexcept;

        float content_height_ { 0.0f };
        float viewport_height_ { 0.0f };
        float scroll_ { 0.0f };
        float scale_ { 1.0f };
        // arrange가 정하는 track과 thumb의 세로 범위다.
        float track_top_ { 0.0f };
        float track_height_ { 0.0f };
        float thumb_top_ { 0.0f };
        float thumb_height_ { 0.0f };
    };
} // namespace gitman::ui
