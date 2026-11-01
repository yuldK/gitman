#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/scrollbar_element.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace gitman::ui {
    // 스크롤되는 카드 목록이다. 화면에 걸치는 카드와 위아래 한 장씩만 자식으로 만들어
    // 카드가 수백 개여도 tree 크기가 화면에 비례한다 (기존 compute_layout과 같은
    // 성질). 화면 밖의 여분 카드는 보이지 않는 상태라 키보드 순회에만 걸린다.
    // 카드 순서 변경의 drop 대상이기도 하다: 카드는 drag 출발지만 되고, 놓이는
    // 위치(가장 가까운 카드 사이)는 목록이 정한다 (field-feedback-design 4.1).
    // 가상화·삽입 틈·ghost는 luil에 대응 요소가 없어 커스텀으로 남고, 스크롤 막대만
    // luil `scrollbar_element`다 (설계 L5).
    class card_list_element final : public ui_element
    {
    public:
        explicit card_list_element(const view_snapshot& view);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;
        [[nodiscard]] access_info accessibility() const override;

        // 배치된 목록 내용의 전체 높이다 (물리 픽셀).
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

        // 내용이 화면보다 길 때만 보이는 스크롤 막대다. 값은 전부 논리 픽셀로 준다.
        luil::scrollbar_element* scrollbar_ { nullptr };

        // 자식 카드가 전체 목록에서 몇 번째인지다. 자식과 같은 순서다.
        std::vector<std::size_t> visible_indices_ {};
        // 문서 순서 그대로의 전체 카드 id다. drop 위치를 reorder intent로 바꿀 때 쓴다.
        std::vector<project_id> card_ids_ {};
        std::size_t total_card_count_ { 0 };
        float content_height_ { 0.0f };
        // arrange가 고정한 실제 스크롤 값이다 (물리 픽셀). 카드 배치와 thumb 위치가
        // 쓴다. 막대에 넘기는 값은 논리 픽셀이라 여기에 배율을 곱하지 않는다.
        float scroll_ { 0.0f };
        float scale_ { 1.0f };
    };
} // namespace gitman::ui
