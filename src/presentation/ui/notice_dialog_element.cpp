#include "presentation/ui/notice_dialog_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/ui/dialog_elements.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        // panel의 논리 치수다. 본문 줄 수에 따라 높이만 늘어난다.
        constexpr float panel_width { 380.0f };
        constexpr float panel_padding { 16.0f };
        constexpr float title_height { 26.0f };
        constexpr float line_height { 18.0f };
        constexpr float button_width { 88.0f };
        constexpr float button_height { 28.0f };

        [[nodiscard]] float panel_height(const std::size_t line_count) noexcept
        {
            const float lines { static_cast<float>(line_count == 0 ? 1 : line_count) * line_height };
            return panel_padding * 2.0f + title_height + lines + 12.0f + button_height;
        }

        // 제목·본문을 그리는 panel이다. 클릭을 흡수해 배경의 닫기로 흐르지 않게 한다.
        class notice_panel_element final : public ui_element
        {
        public:
            explicit notice_panel_element(const notice_dialog_view& dialog)
                : ui_element { ui_element_id { ui_element_kind::notice_dialog_panel } }
                , dialog_ { dialog }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                static_cast<void>(interaction);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 4.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.surface_background));

                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const float padding { panel_padding * scale };
                const ui_color accent { dialog_.error ? context.palette.error_accent : context.palette.positive_accent };
                float text_left { box.x + padding };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 14.0f * scale };
                    const char32_t glyph { dialog_.error ? codicons::icon_error : codicons::icon_check };
                    draw_centered_glyph(context.canvas, glyph, { text_left, box.y + padding, 18.0f * scale, title_height * scale }, icon_font, solid_paint(accent));
                    text_left += 24.0f * scale;
                }

                SkFont title_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                title_font.setEmbolden(true);
                static_cast<void>(draw_text_within(context.canvas, dialog_.title, text_left, box.y + padding + centered_text_baseline(title_font, title_height * scale),
                    box.x + box.width - padding - text_left, title_font, solid_paint(accent)));

                const SkFont line_font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                SkPaint line_paint { solid_paint(context.palette.primary_foreground) };
                line_paint.setAlphaf(0.85f);
                float line_top { box.y + padding + title_height * scale };
                for (const std::u8string& line : dialog_.lines)
                {
                    static_cast<void>(
                        draw_text_within(context.canvas, line, box.x + padding, line_top + centered_text_baseline(line_font, line_height * scale), box.width - padding * 2.0f, line_font, line_paint));
                    line_top += line_height * scale;
                }
            }

        private:
            notice_dialog_view dialog_ {};
        };
    } // namespace

    notice_dialog_element::notice_dialog_element(notice_dialog_view dialog)
        : ui_element { ui_element_id { ui_element_kind::notice_dialog } }
        , dialog_ { std::move(dialog) }
    {
        // 배경 클릭은 닫기다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { dismiss_notice_intent {} } } }; });

        auto panel { std::make_unique<notice_panel_element>(dialog_) };
        panel_ = panel.get();
        add_child(std::move(panel));

        auto confirm { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::notice_dialog_confirm }, std::u8string { u8"확인" }, true) };
        confirm->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { dismiss_notice_intent {} } } }; });
        confirm_ = confirm.get();
        add_child(std::move(confirm));
    }

    void notice_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { panel_width * scale };
        float height { panel_height(dialog_.lines.size()) * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { panel_padding * scale };
        confirm_->arrange({ { left + width - padding - button_width * scale, top + height - padding - button_height * scale, button_width * scale, button_height * scale }, scale });
    }

    void notice_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
