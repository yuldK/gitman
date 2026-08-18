#pragma once

#include "presentation/ui/ui_element.h"

namespace gitman::ui {
    enum class label_color_role
    {
        primary,
        dim,
        error,
    };

    // 바탕 역할이다. 구체 색은 theme이 그리기 시점에 정한다.
    enum class label_background_role
    {
        none,
        // notice 배너다. 카드와 같은 색으로 보이지 않도록 바탕을 깐다.
        notice,
    };

    struct label_config
    {
        std::u8string text {};
        float font_size { 12.0f };
        label_color_role color { label_color_role::dim };
        label_background_role background { label_background_role::none };
        // 바탕이 있을 때 글자를 좌우로 들여쓰는 여백이다 (논리 픽셀).
        float padding { 0.0f };
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
