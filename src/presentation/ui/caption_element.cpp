#include "presentation/ui/caption_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/ui/button_element.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <utility>

namespace gitman::ui {
    namespace {
        ui_action make_command_action(const ui_command command)
        {
            return [command](const ui_action_context&) -> std::vector<input_action> { return { input_action { command } }; };
        }

        std::unique_ptr<button_element> make_caption_button(const ui_element_kind kind, const button_config config, const ui_command command, std::u8string tooltip)
        {
            auto button { std::make_unique<button_element>(ui_element_id { kind }, config) };
            button->set_action(ui_trigger::left_click, make_command_action(command));
            button->set_tooltip(std::move(tooltip));
            return button;
        }
    } // namespace

    ui_element_id caption_button_element_id(const caption_button_hover hover) noexcept
    {
        switch (hover)
        {
        case caption_button_hover::minimize:
            return { ui_element_kind::caption_minimize };
        case caption_button_hover::maximize:
            return { ui_element_kind::caption_maximize };
        case caption_button_hover::close:
            return { ui_element_kind::caption_close };
        case caption_button_hover::none:
            break;
        }
        return {};
    }

    caption_element::caption_element(std::u8string title, const caption_ui_metrics metrics)
        : ui_element { ui_element_id { ui_element_kind::caption } }
        , title_ { std::move(title) }
        , metrics_ { metrics }
    {
        const button_config window_button {
            .glyph = codicons::icon_chrome_minimize,
            .icon_size = static_cast<float>(metrics_.button_icon_size),
            .corner_radius = 0.0f,
            .role = button_visual_role::caption,
        };

        button_config minimize_config { window_button };
        auto minimize { make_caption_button(ui_element_kind::caption_minimize, minimize_config, ui_command::window_minimize, u8"최소화") };
        minimize_ = minimize.get();
        add_child(std::move(minimize));

        button_config maximize_config { window_button };
        maximize_config.glyph = codicons::icon_chrome_maximize;
        maximize_config.maximized_glyph = codicons::icon_chrome_restore;
        auto maximize { make_caption_button(ui_element_kind::caption_maximize, maximize_config, ui_command::window_toggle_maximize, u8"최대화 또는 이전 크기로 복원") };
        maximize_ = maximize.get();
        add_child(std::move(maximize));

        button_config close_config { window_button };
        close_config.glyph = codicons::icon_chrome_close;
        close_config.role = button_visual_role::caption_close;
        auto close { make_caption_button(ui_element_kind::caption_close, close_config, ui_command::window_close, u8"닫기") };
        close_ = close.get();
        add_child(std::move(close));
    }

    void caption_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const float caption_height { static_cast<float>(metrics_.height) * scale };
        set_bounds({ context.slot.x, context.slot.y, context.slot.width, caption_height });

        const float button_width { static_cast<float>(metrics_.button_width) * scale };
        const float close_left { context.slot.x + context.slot.width - button_width };
        const float maximize_left { close_left - button_width };
        const float minimize_left { maximize_left - button_width };
        minimize_->arrange({ { minimize_left, context.slot.y, button_width, caption_height }, scale });
        maximize_->arrange({ { maximize_left, context.slot.y, button_width, caption_height }, scale });
        close_->arrange({ { close_left, context.slot.y, button_width, caption_height }, scale });
    }

    void caption_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkPaint fill { solid_paint(context.palette.caption.background) };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), fill);

        const SkPaint foreground { solid_paint(context.palette.caption.foreground) };
        const SkFont title_font { sk_ref_sp(context.ui_typeface), static_cast<float>(metrics_.title_font_size) * scale };

        float title_left { box.x + static_cast<float>(metrics_.title_left_padding) * scale };
        if (context.codicon_typeface != nullptr)
        {
            const SkFont icon_font { sk_ref_sp(context.codicon_typeface), static_cast<float>(metrics_.application_icon_size) * scale };
            const rect_f icon_slot { box.x, box.y, static_cast<float>(metrics_.application_icon_slot_width) * scale, box.height };
            draw_centered_glyph(context.canvas, codicons::icon_source_control, icon_slot, icon_font, foreground);
            title_left = box.x + static_cast<float>(metrics_.application_icon_slot_width + metrics_.title_icon_gap) * scale;
        }
        draw_text(context.canvas, title_, title_left, box.y + centered_text_baseline(title_font, box.height), title_font, foreground);

        draw_children(context, interaction);
    }

    ui_tree make_caption_tree(const float window_width, const float scale, std::u8string title)
    {
        auto caption { std::make_unique<caption_element>(std::move(title)) };
        caption->arrange({ { 0.0f, 0.0f, window_width, 0.0f }, scale });
        return ui_tree { std::move(caption) };
    }
} // namespace gitman::ui
