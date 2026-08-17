#pragma once

#include "presentation/ui/ui_element.h"

namespace gitman::ui {
    enum class label_color_role
    {
        primary,
        dim,
        error,
    };

    struct label_config
    {
        std::u8string text {};
        float font_size { 12.0f };
        label_color_role color { label_color_role::dim };
    };

    // 텍스트 한 줄이다. bounds 안에서 왼쪽 정렬, 세로 중앙 baseline으로 그린다.
    class label_element final : public ui_element
    {
    public:
        label_element(ui_element_id id, label_config config);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        label_config config_ {};
    };
} // namespace gitman::ui
