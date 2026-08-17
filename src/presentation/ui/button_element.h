#pragma once

#include "presentation/ui/ui_element.h"

namespace gitman::ui {
    // 색은 theme이 그리기 시점에 정해지므로 구체 색이 아니라 역할만 담는다.
    enum class button_visual_role
    {
        toolbar,
        caption,
        caption_close,
    };

    struct button_config
    {
        char32_t glyph { 0 };
        // 0이 아니면 최대화된 창에서 이 글리프를 대신 그린다 (복원 아이콘).
        char32_t maximized_glyph { 0 };
        float icon_size { 14.0f };
        float corner_radius { 2.0f };
        button_visual_role role { button_visual_role::toolbar };
    };

    // 아이콘 버튼이다. hover·눌림 강조와 비활성 흐림을 일관되게 그린다. 클릭 액션과
    // tooltip은 기반 클래스 API로 등록한다.
    class button_element final : public ui_element
    {
    public:
        button_element(ui_element_id id, button_config config) noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        button_config config_ {};
    };
} // namespace gitman::ui
