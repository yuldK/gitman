#include "presentation/ui/context_menu_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/list_metrics.h"
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
#include <vector>

namespace gitman::ui {
    namespace {
        std::u8string item_owner_value(const std::size_t index)
        {
            std::u8string value { u8"context-menu-" };
            std::size_t remaining { index };
            std::u8string digits {};
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                remaining /= 10;
            } while (remaining > 0);
            value += digits;
            return value;
        }

        char32_t entry_glyph(const context_menu_entry entry) noexcept
        {
            switch (entry)
            {
            case context_menu_entry::open_repository:
                return codicons::icon_folder_opened;
            case context_menu_entry::show_local_changes:
                return codicons::icon_diff;
            case context_menu_entry::refresh:
                return codicons::icon_refresh;
            case context_menu_entry::update:
                return codicons::icon_repo_pull;
            case context_menu_entry::switch_to:
                return codicons::icon_source_control;
            }
            return codicons::icon_question;
        }

        // 항목의 클릭 결과다. 어느 항목이든 먼저 메뉴를 닫고 이어서 본 동작을 낸다.
        std::vector<input_action> entry_actions(const context_menu_entry entry, const project_id& owner, const std::u8string& repository_path)
        {
            std::vector<input_action> actions { input_action { logic_message { close_context_menu_intent {} } } };
            switch (entry)
            {
            case context_menu_entry::open_repository:
                actions.push_back(input_action { open_external_request { external_open_target::explorer_folder, repository_path } });
                break;
            case context_menu_entry::show_local_changes:
                actions.push_back(input_action { logic_message { open_local_changes_intent { owner } } });
                break;
            case context_menu_entry::refresh:
                actions.push_back(input_action { logic_message { refresh_card_intent { owner } } });
                break;
            case context_menu_entry::update:
                // submodule 여부는 logic이 문서 settings로 채운다 (카드 버튼과 동일).
                actions.push_back(input_action { logic_message { request_update_intent { owner, {} } } });
                break;
            case context_menu_entry::switch_to:
                actions.push_back(input_action { logic_message { begin_switch_intent { owner } } });
                break;
            }
            return actions;
        }

        // 메뉴 항목 한 행이다. hover와 키보드 강조를 같은 모양으로 그린다.
        class menu_item_element final : public ui_element
        {
        public:
            menu_item_element(const std::size_t index, const context_menu_item_view& item, const project_id& owner, const std::u8string& repository_path)
                : ui_element { context_menu_item_id(index) }
                , item_ { item }
            {
                set_enabled(item.enabled);
                set_action(ui_trigger::left_click, [entry = item.entry, owner, repository_path](const ui_action_context&) { return entry_actions(entry, owner, repository_path); });
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

                if (enabled() && (interaction.hovered == id() || interaction.menu_highlight == id()))
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.08f)));

                const float dim { enabled() ? 1.0f : 0.4f };
                const float inset { 8.0f * scale };
                float text_left { box.x + inset };
                SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                foreground.setAlphaf(dim);
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 12.0f * scale };
                    draw_centered_glyph(context.canvas, entry_glyph(item_.entry), { text_left, box.y, 14.0f * scale, box.height }, icon_font, foreground);
                }
                text_left += 20.0f * scale;

                const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
                static_cast<void>(
                    draw_text_within(context.canvas, item_.label, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - inset - text_left, font, foreground));
            }

        private:
            context_menu_item_view item_ {};
        };

        // 앵커에 붙는 소형 panel이다. 클릭을 흡수해 overlay의 닫기로 흐르지 않게
        // 한다.
        class menu_panel_element final : public ui_element
        {
        public:
            menu_panel_element()
                : ui_element { ui_element_id { ui_element_kind::context_menu_panel } }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
                set_action(ui_trigger::right_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
            }

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
                const float radius { 4.0f * scale };

                // dim 배경 없이 떠 있으므로 옅은 그림자가 경계를 만든다.
                const SkRect shadow { SkRect::MakeXYWH(box.x + 2.0f * scale, box.y + 2.0f * scale, box.width, box.height) };
                context.canvas.drawRRect(SkRRect::MakeRectXY(shadow, radius, radius), solid_paint(with_alpha(0xFF000000u, 0.25f)));

                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.surface_background));
                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                draw_children(context, interaction);
            }
        };
    } // namespace

    ui_element_id context_menu_item_id(const std::size_t index)
    {
        ui_element_id id { ui_element_kind::context_menu_item };
        id.owner.value = item_owner_value(index);
        return id;
    }

    context_menu_element::context_menu_element(context_menu_view menu)
        : ui_element { ui_element_id { ui_element_kind::context_menu } }
        , menu_ { std::move(menu) }
    {
        // 바깥 클릭(좌·우)은 닫기다. 우클릭도 흡수해야 메뉴 아래 카드가 새 메뉴를
        // 열지 않는다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { close_context_menu_intent {} } } }; });
        set_action(ui_trigger::right_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { close_context_menu_intent {} } } }; });

        auto panel { std::make_unique<menu_panel_element>() };
        for (std::size_t index = 0; index < menu_.items.size(); ++index)
            panel->adopt(std::make_unique<menu_item_element>(index, menu_.items[index], menu_.owner, menu_.repository_path));
        panel_ = panel.get();
        add_child(std::move(panel));
    }

    void context_menu_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        const float padding { layout_context_menu_padding * scale };
        const float row_height { layout_context_menu_row_height * scale };
        float width { layout_context_menu_width * scale };
        float height { static_cast<float>(menu_.items.size()) * row_height + padding * 2.0f };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;

        // 앵커에 붙이되 창 밖으로 나가면 안쪽으로 민다.
        float left { menu_.anchor_x };
        float top { menu_.anchor_y };
        if (left + width > context.slot.x + context.slot.width)
            left = context.slot.x + context.slot.width - width;
        if (top + height > context.slot.y + context.slot.height)
            top = context.slot.y + context.slot.height - height;
        if (left < context.slot.x)
            left = context.slot.x;
        if (top < context.slot.y)
            top = context.slot.y;
        panel_->arrange({ { left, top, width, height }, scale });

        const std::span<const std::unique_ptr<ui_element>> rows { panel_->children() };
        for (std::size_t index = 0; index < rows.size(); ++index)
            rows[index]->arrange({ { left + padding, top + padding + static_cast<float>(index) * row_height, width - padding * 2.0f, row_height }, scale });
    }

    void context_menu_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // overlay 자체는 투명하다. dim 없이 카드 위에 메뉴만 뜬다.
        draw_children(context, interaction);
    }
} // namespace gitman::ui
