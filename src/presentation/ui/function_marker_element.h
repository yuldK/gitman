#pragma once

#include "presentation/ui/ui_common.h"

namespace gitman::ui {
    // 기능 버튼과 `⋯` 위의 상태 표식 모양이다 (function-buttons-design 2.3.5, D35). 색뿐 아니라 모양으로도 가른다:
    // 실행 중은 채운 원(accent), 실패는 채운 마름모(error)다.
    enum class function_marker_shape
    {
        running,
        failed,
    };

    // 버튼 오른쪽 위 안쪽의 표식 자리다. 실행 중은 6, 실패는 8이고 두 모양의 가운데가 같다.
    [[nodiscard]] rect_f function_marker_slot(const rect_f& button, float scale, function_marker_shape shape) noexcept;

    // 버튼 위에 겹쳐 그리는 표식이다. 자식 순서가 그리기 순서라 버튼 뒤에 붙인다 (2.3.5). 액션도 tooltip도 없어 hit
    // 대상이 아니고 접근 tree에도 서지 않는다 — 상태는 버튼의 접근 이름이 말한다. 모양은 `⋯`처럼 접힌 버튼에 따라
    // 정해지는 것이 있어 배치 때 고칠 수 있다.
    class function_marker_element final : public ui_element
    {
    public:
        function_marker_element(ui_element_id id, function_marker_shape shape) noexcept;

        void set_shape(function_marker_shape shape) noexcept;
        [[nodiscard]] function_marker_shape shape() const noexcept;

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        function_marker_shape shape_ { function_marker_shape::failed };
    };
} // namespace gitman::ui
