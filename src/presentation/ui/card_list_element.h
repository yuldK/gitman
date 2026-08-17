#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <vector>

namespace gitman::ui {
    // 스크롤되는 카드 목록이다. 화면에 걸치는 카드만 자식으로 만들어 카드가 수백
    // 개여도 tree 크기가 화면에 비례한다 (기존 compute_layout과 같은 성질).
    class card_list_element final : public ui_element
    {
    public:
        explicit card_list_element(const view_snapshot& view);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

        [[nodiscard]] float content_height() const noexcept;

    private:
        // 자식 카드가 전체 목록에서 몇 번째인지다. 자식과 같은 순서다.
        std::vector<std::size_t> visible_indices_ {};
        std::size_t total_card_count_ { 0 };
        float content_height_ { 0.0f };
    };
} // namespace gitman::ui
