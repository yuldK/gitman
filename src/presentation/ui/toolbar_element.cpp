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
#include <string>
#include <utility>

namespace gitman::ui {
    toolbar_element::toolbar_element(std::u8string document_text, const bool show_open_button, const bool generation_busy, const bool relative_paths, const bool document_open)
        : ui_element { ui_element_id { ui_element_kind::toolbar } }
        , show_open_button_ { show_open_button }
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

        auto generate_document { std::make_unique<button_element>(ui_element_id { ui_element_kind::toolbar_generate_document }, button_config { .glyph = codicons::icon_new_file }) };
        generate_document->set_tooltip(generation_busy ? std::u8string { u8".version-list 생성 중" } : std::u8string { u8"하위 폴더 저장소로 .version-list 만들기" });
        generate_document->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::show_generate_document_dialog } }; });
        generate_document->set_enabled(generation_busy == false);
        generate_document_ = generate_document.get();
        add_child(std::move(generate_document));

        // 경로 표시 토글이다. 켜져 있으면 강조 배경으로 상태를 계속 보여 준다.
        auto toggle_path_display {
            std::make_unique<button_element>(ui_element_id { ui_element_kind::toolbar_toggle_path_display }, button_config { .glyph = codicons::icon_root_folder, .active = relative_paths }),
        };
        toggle_path_display->set_tooltip(relative_paths ? std::u8string { u8"카드 경로를 전체 경로로 표시" } : std::u8string { u8"카드 경로를 문서 기준 상대 경로로 표시" });
        toggle_path_display->set_action(
            ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { toggle_path_display_intent {} } } }; });
        toggle_path_display_ = toggle_path_display.get();
        add_child(std::move(toggle_path_display));

        // 환경설정은 문서 `settings`를 편집하므로 열린 문서가 있을 때만 활성이다
        // (REQ-017).
        auto settings { std::make_unique<button_element>(ui_element_id { ui_element_kind::toolbar_settings }, button_config { .glyph = codicons::icon_settings_gear }) };
        settings->set_tooltip(document_open ? std::u8string { u8"환경설정" } : std::u8string { u8"환경설정 (문서를 먼저 여세요)" });
        settings->set_enabled(document_open);
        settings->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { open_settings_intent {} } } }; });
        settings_ = settings.get();
        add_child(std::move(settings));

        // 탐색 등록은 열린 문서에 추가하는 경로다 (REQ-004, 단계 8). 스캔 폴더는
        // UI thread의 폴더 선택이 고른다.
        auto discover { std::make_unique<button_element>(ui_element_id { ui_element_kind::toolbar_discover }, button_config { .glyph = codicons::icon_search }) };
        discover->set_tooltip(document_open ? std::u8string { u8"하위 폴더 저장소 탐색·등록" } : std::u8string { u8"하위 폴더 저장소 탐색·등록 (문서를 먼저 여세요)" });
        discover->set_enabled(document_open);
        discover->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::show_discovery_folder_picker } }; });
        discover_ = discover.get();
        add_child(std::move(discover));
    }

    void toolbar_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        const float margin { layout_margin * scale };
        const float button { layout_button_size * scale };
        const float button_y { context.slot.y + (context.slot.height - button) / 2.0f };
        const float label_left { context.slot.x + margin };
        // 창이 좁으면 오른쪽부터 들어가고 자리가 없는 버튼은 숨긴다. 남은 버튼과
        // 문서 경로가 서로 겹치는 것보다 낫다.
        float next_x { context.slot.x + context.slot.width - margin - button };
        const auto place = [&](ui_element* const element, const bool wanted) {
            if (wanted == false || next_x < label_left)
            {
                element->set_visible(false);
                return;
            }
            element->set_visible(true);
            element->arrange({ { next_x, button_y, button, button }, scale });
            next_x -= button + margin;
        };

        place(refresh_all_, true);
        place(open_document_, show_open_button_);
        place(generate_document_, true);
        place(toggle_path_display_, true);
        place(discover_, true);
        place(settings_, true);

        const float label_width { next_x + button - label_left };
        document_label_->arrange({ { label_left, context.slot.y, label_width < 0.0f ? 0.0f : label_width, context.slot.height }, scale });
    }

    void toolbar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        const SkPaint fill { solid_paint(context.palette.surface_background) };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);
        draw_children(context, interaction);
    }
} // namespace gitman::ui
