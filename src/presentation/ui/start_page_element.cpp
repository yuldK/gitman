#include "presentation/ui/start_page_element.h"

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
        std::u8string index_owner_value(const std::u8string_view prefix, const std::size_t index)
        {
            std::u8string value { prefix };
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

        std::u8string count_text(const std::size_t count)
        {
            std::u8string digits {};
            std::size_t remaining { count };
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                remaining /= 10;
            } while (remaining > 0);
            return u8"외 " + digits + u8"개 더 있습니다";
        }

        // "시작" 열의 한 줄이다. 아이콘과 라벨을 그리고 UI thread 명령을 낸다.
        class start_action_element final : public ui_element
        {
        public:
            start_action_element(const ui_element_kind kind, const char32_t glyph, std::u8string label, const ui_command command)
                : ui_element { ui_element_id { kind } }
                , glyph_ { glyph }
                , label_ { std::move(label) }
            {
                set_action(ui_trigger::left_click, [command](const ui_action_context&) -> std::vector<input_action> { return { input_action { command } }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                if (interaction.hovered == id())
                {
                    const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.08f)));
                }

                const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                float text_left { box.x + 4.0f * scale };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 13.0f * scale };
                    draw_centered_glyph(context.canvas, glyph_, { text_left, box.y, 16.0f * scale, box.height }, icon_font, foreground);
                }
                text_left += 22.0f * scale;

                const SkFont font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                static_cast<void>(draw_text_within(context.canvas, label_, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - text_left, font, foreground));
            }

        private:
            char32_t glyph_ { 0 };
            std::u8string label_ {};
        };

        // 최근 항목 행 오른쪽의 제거 아이콘이다. 행보다 먼저 hit되어 목록에서만
        // 지우고 문서는 열지 않는다.
        class recent_remove_element final : public ui_element
        {
        public:
            recent_remove_element(const std::size_t index, std::u8string path)
                : ui_element { start_page_recent_remove_id(index) }
            {
                set_tooltip(u8"최근 목록에서 제거");
                set_action(ui_trigger::left_click,
                    [path = std::move(path)](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { remove_recent_document_intent { path } } } }; });
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                foreground.setAlphaf(interaction.hovered == id() ? 1.0f : 0.45f);
                if (context.codicon_typeface == nullptr)
                    return;

                const SkFont icon_font { sk_ref_sp(context.codicon_typeface), 12.0f * scale };
                draw_centered_glyph(context.canvas, codicons::icon_close, box, icon_font, foreground);
            }
        };

        // 최근 항목 한 행이다. 이름 뒤에 폴더를 흐리게 붙이고, 클릭하면 그 문서를
        // 연다.
        class recent_item_element final : public ui_element
        {
        public:
            recent_item_element(const std::size_t index, const recent_document_view& recent)
                : ui_element { start_page_recent_item_id(index) }
                , recent_ { recent }
            {
                set_tooltip(recent.path);
                set_action(ui_trigger::left_click,
                    [path = recent.path](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { open_document_intent { path } } } }; });
                auto remove { std::make_unique<recent_remove_element>(index, recent.path) };
                remove_ = remove.get();
                add_child(std::move(remove));
            }

            void arrange(const arrange_context& context) override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                set_bounds(context.slot);
                const float icon { 20.0f * scale };
                remove_->arrange({ { context.slot.x + context.slot.width - icon, context.slot.y, icon, context.slot.height }, scale });
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const bool hovered { interaction.hovered == id() || interaction.hovered == remove_->id() };
                if (hovered)
                {
                    const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.08f)));
                }

                const float remove_width { 24.0f * scale };
                const float right { box.x + box.width - remove_width };
                const SkFont name_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                const float baseline { box.y + centered_text_baseline(name_font, box.height) };
                const float text_left { box.x + 4.0f * scale };
                const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                const float name_width { draw_text_within(context.canvas, recent_.display_name, text_left, baseline, right - text_left, name_font, foreground) };

                // 폴더는 이름 뒤에 흐리게 붙는다. 남은 폭이 없으면 그리지 않는다.
                const float folder_left { text_left + name_width + 8.0f * scale };
                if (folder_left >= right)
                    return;

                SkPaint dim { solid_paint(context.palette.primary_foreground) };
                dim.setAlphaf(0.55f);
                const SkFont folder_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                static_cast<void>(draw_text_within(context.canvas, recent_.folder, folder_left, baseline, right - folder_left, folder_font, dim));
            }

        private:
            recent_document_view recent_ {};
            ui_element* remove_ { nullptr };
        };
    } // namespace

    ui_element_id start_page_recent_item_id(const std::size_t index)
    {
        ui_element_id id { ui_element_kind::start_page_recent_item };
        id.owner.value = index_owner_value(u8"start-page-recent-", index);
        return id;
    }

    ui_element_id start_page_recent_remove_id(const std::size_t index)
    {
        ui_element_id id { ui_element_kind::start_page_recent_remove };
        id.owner.value = index_owner_value(u8"start-page-remove-", index);
        return id;
    }

    start_page_element::start_page_element(const start_page_view& page)
        : ui_element { ui_element_id { ui_element_kind::start_page } }
        , page_ { page }
    {
        add_child(std::make_unique<start_action_element>(ui_element_kind::start_page_open_document, codicons::icon_folder_opened, u8"문서 열기…", ui_command::show_open_document_dialog));
        add_child(std::make_unique<start_action_element>(ui_element_kind::start_page_generate_document, codicons::icon_new_file, u8"새 문서 만들기…", ui_command::show_generate_document_dialog));
        for (std::size_t index = 0; index < page_.recents.size(); ++index)
            add_child(std::make_unique<recent_item_element>(index, page_.recents[index]));
    }

    void start_page_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        const float padding { layout_start_page_padding * scale };
        const float row_height { layout_start_page_row_height * scale };
        const float section_height { layout_start_page_section_height * scale };
        const float left { context.slot.x + padding };
        const float content_width { context.slot.width - padding * 2.0f };
        const float bottom { context.slot.y + context.slot.height - padding };

        title_ = { left, context.slot.y + padding, content_width, layout_start_page_title_height * scale };
        subtitle_ = { left, title_.y + title_.height, content_width, layout_start_page_subtitle_height * scale };

        const float content_top { subtitle_.y + subtitle_.height + padding * 0.5f };
        const float action_height { section_height + row_height * 2.0f };
        const bool stacked { context.slot.width < layout_start_page_stack_width * scale };

        float action_width { layout_start_page_action_column_width * scale };
        if (action_width > content_width)
            action_width = content_width;
        float recent_left { left + action_width + padding };
        float recent_width { context.slot.x + context.slot.width - padding - recent_left };
        float recent_top { content_top };
        if (stacked)
        {
            action_width = content_width;
            recent_left = left;
            recent_width = content_width;
            recent_top = content_top + action_height + padding * 0.5f;
        }
        if (recent_width < row_height)
            recent_width = row_height;

        action_section_ = { left, content_top, action_width, action_height };
        recent_section_ = { recent_left, recent_top, recent_width, bottom - recent_top };

        const std::span<const std::unique_ptr<ui_element>> rows { children() };
        rows[0]->arrange({ { left, content_top + section_height, action_width, row_height }, scale });
        rows[1]->arrange({ { left, content_top + section_height + row_height, action_width, row_height }, scale });

        // 남은 높이에 들어가는 행만 그린다. 스크롤을 두지 않고 넘치는 개수를 마지막
        // 줄로 알린다 (app-shell-design A1.3의 스크롤 대신 채택한 단순화).
        const float rows_top { recent_top + section_height };
        float available { bottom - rows_top };
        if (available < 0.0f)
            available = 0.0f;

        const std::size_t recent_count { page_.recents.size() };
        std::size_t capacity { row_height > 0.0f ? static_cast<std::size_t>(available / row_height) : 0 };
        if (capacity < recent_count && capacity > 0)
            --capacity;
        visible_recents_ = capacity < recent_count ? capacity : recent_count;
        recent_empty_ = { recent_left, rows_top + static_cast<float>(visible_recents_) * row_height, recent_width, row_height };

        for (std::size_t index = 0; index < recent_count; ++index)
        {
            ui_element& row { *rows[index + 2] };
            row.set_visible(index < visible_recents_);
            row.arrange({ { recent_left, rows_top + static_cast<float>(index) * row_height, recent_width, row_height }, scale });
        }
    }

    void start_page_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        SkPaint dim { solid_paint(context.palette.primary_foreground) };
        dim.setAlphaf(0.55f);

        const SkFont title_font { sk_ref_sp(context.ui_typeface), 26.0f * scale };
        static_cast<void>(draw_text_within(context.canvas, u8"Gitman", title_.x, title_.y + centered_text_baseline(title_font, title_.height), title_.width, title_font, foreground));

        const SkFont body_font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
        static_cast<void>(draw_text_within(context.canvas, u8".version-list 작업공간을 열어 여러 저장소의 상태를 한 화면에서 관리합니다.", subtitle_.x,
            subtitle_.y + centered_text_baseline(body_font, subtitle_.height), subtitle_.width, body_font, dim));

        const SkFont section_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
        static_cast<void>(draw_text_within(context.canvas, u8"시작", action_section_.x + 4.0f * scale,
            action_section_.y + centered_text_baseline(section_font, layout_start_page_section_height * scale), action_section_.width, section_font, dim));
        static_cast<void>(draw_text_within(context.canvas, u8"최근 항목", recent_section_.x + 4.0f * scale,
            recent_section_.y + centered_text_baseline(section_font, layout_start_page_section_height * scale), recent_section_.width, section_font, dim));

        draw_children(context, interaction);

        // 목록 자리의 안내다. 읽는 중, 항목 없음, 잘린 개수를 같은 줄에 그린다.
        std::u8string notice {};
        if (page_.loading)
            notice = u8"최근 항목을 읽는 중입니다…";
        else if (page_.recents.empty())
            notice = u8"최근에 연 문서가 없습니다.";
        else if (visible_recents_ < page_.recents.size())
            notice = count_text(page_.recents.size() - visible_recents_);
        if (notice.empty())
            return;

        static_cast<void>(
            draw_text_within(context.canvas, notice, recent_empty_.x + 4.0f * scale, recent_empty_.y + centered_text_baseline(body_font, recent_empty_.height), recent_empty_.width, body_font, dim));
    }
} // namespace gitman::ui
