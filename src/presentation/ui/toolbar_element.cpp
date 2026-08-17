#include "presentation/ui/toolbar_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/button_element.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/label_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <memory>
#include <utility>

namespace gitman::ui {
    toolbar_element::toolbar_element(std::u8string document_text, const bool show_open_button)
        : ui_element { ui_element_id { ui_element_kind::toolbar } }
    {
        auto document_label { std::make_unique<label_element>(ui_element_id { ui_element_kind::toolbar_document_path }, label_config { .text = std::move(document_text) }) };
        document_label_ = document_label.get();
        add_child(std::move(document_label));

        auto refresh_all { std::make_unique<button_element>(ui_element_id { ui_element_kind::toolbar_refresh_all }, button_config { .glyph = codicons::icon_refresh }) };
        refresh_all->set_tooltip(u8"모든 카드 새로 고침");
        refresh_all->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { refresh_all_intent {} } } }; });
        refresh_all_ = refresh_all.get();
        add_child(std::move(refresh_all));

        auto open_document { std::make_unique<button_element>(ui_element_id { ui_element_kind::toolbar_open_document }, button_config { .glyph = codicons::icon_folder_opened }) };
        open_document->set_tooltip(u8".version-list 문서 열기");
        open_document->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::show_open_document_dialog } }; });
        open_document->set_visible(show_open_button);
        open_document_ = open_document.get();
        add_child(std::move(open_document));
    }

    void toolbar_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        const float margin { layout_margin * scale };
        const float button { layout_button_size * scale };
        const float button_y { context.slot.y + (context.slot.height - button) / 2.0f };
        const float refresh_x { context.slot.x + context.slot.width - margin - button };
        refresh_all_->arrange({ { refresh_x, button_y, button, button }, scale });
        open_document_->arrange({ { refresh_x - margin - button, button_y, button, button }, scale });

        const float label_width { refresh_x - margin * 2.0f - (open_document_->visible() ? margin + button : 0.0f) };
        document_label_->arrange({ { context.slot.x + margin, context.slot.y, label_width, context.slot.height }, scale });
    }

    void toolbar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        const SkPaint fill { solid_paint(context.palette.surface_background) };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);
        draw_children(context, interaction);
    }
} // namespace gitman::ui
