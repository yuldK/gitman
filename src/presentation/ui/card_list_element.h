#pragma once

#include "presentation/ui/ui_element.h"
#include "presentation/view_snapshot.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace gitman::ui {
    // 스크롤되는 카드 목록이다. 화면에 걸치는 카드와 위아래 한 장씩만 자식으로 만들어
    // 카드가 수백 개여도 tree 크기가 화면에 비례한다 (기존 compute_layout과 같은
    // 성질). 화면 밖의 여분 카드는 보이지 않는 상태라 키보드 순회에만 걸린다.
    // 카드 순서 변경의 drop 대상이기도 하다: 카드는 drag 출발지만 되고, 놓이는
    // 위치(가장 가까운 카드 사이)는 목록이 정한다 (field-feedback-design 4.1).
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
        // 카드 drag가 진행 중일 때의 그리기 상태다. 값이 없으면 일반 그리기다.
        struct drag_view
        {
            std::size_t dragged_index { 0 };
            // dragged를 뺀 목록 기준의 삽입 위치다. 이 자리의 여백이 벌어진다.
            std::size_t insertion_slot { 0 };
            const ui_element* dragged_child { nullptr };
        };

        [[nodiscard]] std::optional<drag_view> derive_drag_view(const interaction_snapshot& interaction) const;
        void draw_lifted_card(draw_context& context, const interaction_snapshot& interaction, const drag_view& drag) const;

        // 내용이 화면보다 길 때만 만드는 스크롤 막대다. 클릭·끌기를 직접 처리한다.
        ui_element* scrollbar_ { nullptr };

        // 자식 카드가 전체 목록에서 몇 번째인지다. 자식과 같은 순서다.
        std::vector<std::size_t> visible_indices_ {};
        // 문서 순서 그대로의 전체 카드 id다. drop 위치를 reorder intent로 바꿀 때 쓴다.
        std::vector<project_id> card_ids_ {};
        std::size_t total_card_count_ { 0 };
        float content_height_ { 0.0f };
        // arrange가 고정한 실제 스크롤 값이다 (물리 픽셀). thumb 위치가 쓴다.
        float scroll_ { 0.0f };
        float scale_ { 1.0f };
    };
} // namespace gitman::ui
