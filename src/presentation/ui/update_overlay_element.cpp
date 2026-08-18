#include "presentation/ui/update_overlay_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/ui/dialog_elements.h"
#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        // panel의 논리 치수다. overlay 하나에서만 쓰므로 이 파일에 둔다.
        constexpr float panel_width { 340.0f };
        constexpr float panel_height { 150.0f };
        constexpr float panel_padding { 14.0f };
        constexpr float row_height { 24.0f };
        constexpr float action_button_width { 88.0f };
        constexpr float action_button_height { 28.0f };

        // checkbox 한 줄이다. 상자와 설명 글자를 함께 그리고 줄 전체가 클릭 대상이다.
        class checkbox_row_element final : public ui_element
        {
        public:
            checkbox_row_element(const ui_element_id id, std::u8string text, const bool checked)
                : ui_element { id }
                , text_ { std::move(text) }
                , checked_ { checked }
            {}

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };

                if (interaction.hovered == id())
                    context.canvas.drawRRect(
                        SkRRect::MakeRectXY(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.06f)));

                const float check_size { 14.0f * scale };
                const float check_top { box.y + (box.height - check_size) / 2.0f };
                const SkRect check_box { SkRect::MakeXYWH(box.x, check_top, check_size, check_size) };
                SkPaint border { solid_paint(checked_ ? context.palette.positive_accent : context.palette.primary_foreground) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(check_box, 2.0f * scale, 2.0f * scale), border);
                if (checked_ && context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 12.0f * scale };
                    draw_centered_glyph(context.canvas, codicons::icon_check, { box.x, check_top, check_size, check_size }, icon_font, solid_paint(context.palette.positive_accent));
                }

                const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                const float text_left { box.x + check_size + 8.0f * scale };
                static_cast<void>(draw_text_within(
                    context.canvas, text_, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - text_left, font, solid_paint(context.palette.primary_foreground)));
            }

        private:
            std::u8string text_ {};
            bool checked_ { false };
        };

        // panel 배경이다. 클릭을 흡수해 배경 닫기로 흐르지 않게 하고 제목과 안내를
        // 그린다. 자식(checkbox와 버튼)은 overlay가 직접 배치한다.
        class overlay_panel_element final : public ui_element
        {
        public:
            explicit overlay_panel_element(std::u8string title)
                : ui_element { ui_element_id { ui_element_kind::update_overlay_panel } }
                , title_ { std::move(title) }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
            }

            // 기반 클래스의 add_child는 protected다. overlay가 panel 안에 자식을
            // 넣을 수 있게 공개 경로를 둔다.
            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 5.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.surface_background));
                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const float padding { panel_padding * scale };
                const SkFont title_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                static_cast<void>(draw_text_within(context.canvas, std::u8string { u8"업데이트 실행 - " } + title_, box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f,
                    title_font, solid_paint(context.palette.primary_foreground)));

                const SkFont body_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                SkPaint dim { solid_paint(context.palette.primary_foreground) };
                dim.setAlphaf(0.65f);
                static_cast<void>(draw_text_within(
                    context.canvas, u8"선택한 remote 대상으로 git pull --ff-only를 실행합니다.", box.x + padding, box.y + padding + 30.0f * scale, box.width - padding * 2.0f, body_font, dim));

                draw_children(context, interaction);
            }

        private:
            std::u8string title_ {};
        };
    } // namespace

    update_overlay_element::update_overlay_element(update_overlay_view overlay)
        : ui_element { ui_element_id { ui_element_kind::update_overlay } }
        , overlay_ { std::move(overlay) }
    {
        // 배경 클릭은 취소다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_update_options_intent {} } } }; });

        auto panel { std::make_unique<overlay_panel_element>(overlay_.title) };

        const ui_element_id submodule_id { ui_element_kind::update_overlay_submodule, overlay_.card };
        auto submodule { std::make_unique<checkbox_row_element>(submodule_id, std::u8string { u8"submodule 함께 갱신 (기본 꺼짐)" }, overlay_.update_submodules) };
        submodule->set_tooltip(u8"켜면 parent pull 성공 후 recursive submodule update를 실행합니다");
        submodule->set_action(ui_trigger::left_click, [next = overlay_.update_submodules == false](const ui_action_context&) -> std::vector<input_action> {
            return { input_action { logic_message { set_update_submodules_intent { next } } } };
        });
        panel->adopt(std::move(submodule));

        auto confirm { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::update_overlay_confirm, overlay_.card }, std::u8string { u8"실행" }, true) };
        confirm->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { confirm_update_intent {} } } }; });
        panel->adopt(std::move(confirm));

        auto cancel { std::make_unique<text_button_element>(ui_element_id { ui_element_kind::update_overlay_cancel, overlay_.card }, std::u8string { u8"취소" }, false) };
        cancel->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { cancel_update_options_intent {} } } }; });
        panel->adopt(std::move(cancel));

        panel_ = panel.get();
        add_child(std::move(panel));
    }

    void update_overlay_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { panel_width * scale };
        float height { panel_height * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        // panel 안의 자식 배치: checkbox 한 줄과 오른쪽 아래 버튼 두 개다.
        const float padding { panel_padding * scale };
        const std::span<const std::unique_ptr<ui_element>> children { panel_->children() };
        if (children.size() >= 3)
        {
            children[0]->arrange({ { left + padding, top + 52.0f * scale, width - padding * 2.0f, row_height * scale }, scale });

            const float button_width { action_button_width * scale };
            const float button_height { action_button_height * scale };
            const float button_top { top + height - padding - button_height };
            children[1]->arrange({ { left + width - padding - button_width * 2.0f - 8.0f * scale, button_top, button_width, button_height }, scale });
            children[2]->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
        }
    }

    void update_overlay_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        // 화면 전체를 어둡게 덮어 뒤 내용이 비활성임을 보인다.
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
