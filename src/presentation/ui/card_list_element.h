#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <vector>

namespace gitman::ui {
    // 스크롤되는 카드 목록이다. 화면에 걸치는 카드와 위아래 한 장씩만 자식으로 만들어
    // 카드가 수백 개여도 tree 크기가 화면에 비례한다 (기존 compute_layout과 같은
    // 성질). 화면 밖의 여분 카드는 보이지 않는 상태라 키보드 순회에만 걸린다.
    class card_list_element final : public ui_element
    {
    public:
        explicit card_list_element(const view_snapshot& view);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        // 목록 밖 좌표는 자식까지 함께 걸러낸다. 스크롤로 걸친 카드가 toolbar 자리의
        // 클릭을 가져가지 않게 한다.
        [[nodiscard]] const ui_element* hit_test(float x, float y) const override;

        [[nodiscard]] float content_height() const noexcept;

    private:
        // 내용이 화면보다 길 때만 오른쪽 여백에 thumb를 그린다. 표시 전용이다.
        void draw_scroll_indicator(draw_context& context) const;

        // 자식 카드가 전체 목록에서 몇 번째인지다. 자식과 같은 순서다.
        std::vector<std::size_t> visible_indices_ {};
        std::size_t total_card_count_ { 0 };
        float content_height_ { 0.0f };
        // arrange가 고정한 실제 스크롤 값이다 (물리 픽셀). thumb 위치가 쓴다.
        float scroll_ { 0.0f };
    };
} // namespace gitman::ui
