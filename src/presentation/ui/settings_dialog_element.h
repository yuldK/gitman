#pragma once

#include "presentation/ui/ui_common.h"
#include "presentation/view_snapshot.h"
#include "luil/ui/modal_host_element.h"

#include <memory>

namespace gitman::ui {
    struct settings_dialog_scroll_metrics
    {
        float panel_content_height { 0.0f };
        float content_height { 0.0f };
        float viewport_height { 0.0f };
        float maximum_scroll { 0.0f };
    };

    [[nodiscard]] settings_dialog_scroll_metrics settings_dialog_metrics(const settings_dialog_view& dialog, float window_height, float window_width = 840.0f);

    // 범위와 카테고리는 고정하고 항목만 표준 스크롤 영역에 담는다.
    class settings_dialog_element final : public ui_element
    {
    public:
        settings_dialog_element(settings_dialog_view dialog, float window_height, float window_width = 840.0f);
        void arrange(const arrange_context& context) override;
        void draw(draw_context& context, const interaction_snapshot& interaction) const override;

    private:
        ui_element* content_ { nullptr };
    };

    [[nodiscard]] std::unique_ptr<luil::modal_host_element> make_settings_dialog(const settings_dialog_view& view, float window_width, float window_height);
}
