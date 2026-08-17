#pragma once

#include "presentation/ui/ui_element.h"

#include <memory>
#include <vector>

namespace gitman::ui {
    // 배치가 끝난 element tree다. 게시 후 불변이며 UI thread(그리기), input
    // thread(hit test), UI thread의 동기 조회(caption NC 클릭)가 공유한다.
    class ui_tree
    {
    public:
        explicit ui_tree(std::unique_ptr<ui_element> root);
        ui_tree(ui_tree&&) noexcept = default;
        ui_tree& operator=(ui_tree&&) noexcept = default;
        ui_tree(const ui_tree&) = delete;
        ui_tree& operator=(const ui_tree&) = delete;
        ~ui_tree() = default;

        [[nodiscard]] const ui_element& root() const noexcept;
        [[nodiscard]] const ui_element* hit_test(float x, float y) const;
        [[nodiscard]] const ui_element* find(const ui_element_id& id) const noexcept;
        // 그리기 순서(pre-order)대로 해당 종류의 id를 모은다. 키보드 탐색이 쓴다.
        [[nodiscard]] std::vector<ui_element_id> ids_of_kind(ui_element_kind kind) const;

        // root와 자식을 그린 뒤 tooltip과 drag 표시를 최상위에 얹는다.
        void draw(draw_context& context, const interaction_snapshot& interaction) const;

    private:
        void index_element(const ui_element& element);
        void draw_tooltip(draw_context& context, const interaction_snapshot& interaction) const;
        void draw_drag_visual(draw_context& context, const drag_visual& drag) const;

        std::unique_ptr<ui_element> root_ {};
        // 그리기 순서로 평탄화한 색인이다. 화면에 걸친 element만 담기므로 작다.
        std::vector<const ui_element*> index_ {};
    };
} // namespace gitman::ui
