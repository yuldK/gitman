#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"

#include "luil/ui/modal_host_element.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace gitman::ui {
    // 명령·훅 하나를 고치는 dialog다 (REQ-020). 환경설정의 `명령` 탭 위에 뜨며
    // `확인`이 환경설정 초안의 목록을 고친다.
    //
    // 스크립트 **내용**은 다루지 않는다 (2026-09-10 지시). 경로를 고르고 외부
    // 편집기로 여는 것까지가 이 창의 몫이고, 그다음은 편집기가 할 일이다.
    //
    // luil `modal_host_element`의 content라 scrim·가운데 배치·초점 가둠·Esc는 host의
    // 몫이다 (설계 D2-A).
    class command_editor_dialog_element final : public ui_element
    {
    public:
        explicit command_editor_dialog_element(command_editor_dialog_view dialog);

        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        command_editor_dialog_view dialog_ {};
        ui_element* panel_ { nullptr };
        // 행 순서대로의 컨트롤이다. 행마다 하나 또는 셋(스크립트 줄)이다.
        std::vector<std::vector<ui_element*>> row_controls_ {};
        ui_element* confirm_ { nullptr };
        ui_element* cancel_ { nullptr };
    };

    // 명령과 훅은 다루는 항목이 달라 줄 수가 다르다. 창 높이를 정할 때와 배치할 때가
    // 같은 값을 봐야 하므로 한곳에서 센다.
    [[nodiscard]] std::size_t command_editor_row_count(bool hook) noexcept;

    // 본체를 host에 담아 돌려준다. 창 크기는 **논리 픽셀**이다 (`view.window_* / scale`).
    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_command_editor_dialog(const command_editor_dialog_view& view, float window_width, float window_height);
} // namespace gitman::ui
