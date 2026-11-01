#include "presentation/ui/start_page_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"

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

        // 최근 항목 행 양끝의 아이콘 버튼 폭이다 (논리 픽셀).
        constexpr float recent_icon_width { 20.0f };

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
            start_action_element(const ui_element_kind kind, const char32_t glyph, std::u8string label, input_action action)
                : ui_element { ui_element_id { kind } }
                , glyph_ { glyph }
                , label_ { std::move(label) }
            {
                // 툴팁이 없어 이름이 비면 접근 tree에서 접힌다. 그린 라벨이 이름이다.
                set_access_name(label_);
                set_action(ui_trigger::left_click, [action = std::move(action)](const ui_action_context&) -> std::vector<input_action> { return { action }; });
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
                    const SkFont glyph_font { icon_font(context, 13.0f) };
                    draw_centered_glyph(context.canvas, glyph_, { text_left, box.y, 16.0f * scale, box.height }, glyph_font, foreground);
                }
                text_left += 22.0f * scale;

                const SkFont font { ui_font(context, 13.0f) };
                static_cast<void>(draw_text_within(context.canvas, label_, text_left, box.y + centered_text_baseline(font, box.height), box.x + box.width - text_left, font, foreground));
            }

        private:
            char32_t glyph_ { 0 };
            std::u8string label_ {};
        };

        // 최근 항목 행 양끝의 아이콘 버튼이다. 행보다 먼저 hit되어 문서를 열지
        // 않는다. 색은 **글과 같다** — 흐리게 그렸더니 아이콘이 없는 것으로 보였다
        // (2026-08-23 지시).
        class recent_icon_element final : public ui_element
        {
        public:
            recent_icon_element(ui_element_id id, const char32_t glyph, std::u8string tooltip, input_action action)
                : ui_element { std::move(id) }
                , glyph_ { glyph }
            {
                set_tooltip(std::move(tooltip));
                set_action(ui_trigger::left_click, [action = std::move(action)](const ui_action_context&) -> std::vector<input_action> { return { action }; });
                // 행 안의 아이콘은 Tab 자리가 아니다 (설계 D1-B). 최근 목록이 길면
                // 자리가 행마다 둘씩 늘어 Tab이 목록을 벗어나지 못한다.
                set_tab_stop(false);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot&) const override
            {
                if (context.codicon_typeface == nullptr)
                    return;

                const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                const SkFont glyph_font { icon_font(context, 12.0f) };
                draw_centered_glyph(context.canvas, glyph_, bounds(), glyph_font, foreground);
            }

        private:
            char32_t glyph_ { 0 };
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
                // 이름은 화면에 보이는 순서(문서 이름, 폴더)다. 전체 경로는 툴팁에 남는다.
                set_access_name(recent.display_name + u8" (" + recent.folder + u8")");
                set_action(ui_trigger::left_click,
                    [path = recent.path](const ui_action_context&) -> std::vector<input_action> { return { make_logic_action(open_document_intent { path }) }; });
                // 최근 항목 행은 Tab 자리가 아니다 (설계 D1-B). Tab은 시작 열의
                // 두 줄(문서 열기·새 문서 만들기)만 지난다.
                set_tab_stop(false);

                // 고정한 항목은 채워진 글리프로 구별한다 (2026-08-23 지시).
                const char32_t pin_glyph { recent.pinned ? codicons::icon_pinned : codicons::icon_pin };
                std::u8string pin_tooltip { recent.pinned ? u8"고정 해제" : u8"목록에 고정" };
                auto pin {
                    std::make_unique<recent_icon_element>(
                        start_page_recent_pin_id(index), pin_glyph, std::move(pin_tooltip), make_logic_action(toggle_recent_document_pin_intent { recent.path })),
                };
                pin_ = pin.get();
                add_child(std::move(pin));

                auto remove {
                    std::make_unique<recent_icon_element>(start_page_recent_remove_id(index), codicons::icon_close, std::u8string { u8"최근 목록에서 제거" },
                        make_logic_action(remove_recent_document_intent { recent.path })),
                };
                remove_ = remove.get();
                add_child(std::move(remove));
            }

            void arrange(const arrange_context& context) override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                set_bounds(context.slot);
                const float icon { recent_icon_width * scale };
                pin_->arrange({ { context.slot.x, context.slot.y, icon, context.slot.height }, scale });
                remove_->arrange({ { context.slot.x + context.slot.width - icon, context.slot.y, icon, context.slot.height }, scale });
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const bool hovered { interaction.hovered == id() || interaction.hovered == pin_->id() || interaction.hovered == remove_->id() };
                if (hovered)
                {
                    const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.08f)));
                }

                // 글은 양끝의 아이콘 버튼 사이에만 그린다.
                const float right { box.x + box.width - (recent_icon_width + 4.0f) * scale };
                const SkFont name_font { ui_font(context, 13.0f) };
                const float baseline { box.y + centered_text_baseline(name_font, box.height) };
                const float text_left { box.x + (recent_icon_width + 4.0f) * scale };
                const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
                const float name_width { draw_text_within(context.canvas, recent_.display_name, text_left, baseline, right - text_left, name_font, foreground) };

                // 폴더는 이름 뒤에 흐리게 붙는다. 남은 폭이 없으면 그리지 않는다.
                const float folder_left { text_left + name_width + 8.0f * scale };
                if (folder_left < right)
                {
                    SkPaint dim { solid_paint(context.palette.primary_foreground) };
                    dim.setAlphaf(0.55f);
                    const SkFont folder_font { ui_font(context, 11.0f) };
                    static_cast<void>(draw_text_within(context.canvas, recent_.folder, folder_left, baseline, right - folder_left, folder_font, dim));
                }

                // 아이콘 버튼은 자식이다. 이 호출을 빠뜨려 삭제 버튼이 한 번도 그려지지
                // 않았다 (2026-08-23 지시).
                draw_children(context, interaction);
            }

        private:
            recent_document_view recent_ {};
            ui_element* pin_ { nullptr };
            ui_element* remove_ { nullptr };
        };
    } // namespace

    ui_element_id start_page_recent_item_id(const std::size_t index)
    {
        ui_element_id id { kind_start_page_recent_item };
        id.owner = index_owner_value(u8"start-page-recent-", index);
        return id;
    }

    ui_element_id start_page_recent_pin_id(const std::size_t index)
    {
        ui_element_id id { kind_start_page_recent_pin };
        id.owner = index_owner_value(u8"start-page-pin-", index);
        return id;
    }

    ui_element_id start_page_recent_remove_id(const std::size_t index)
    {
        ui_element_id id { kind_start_page_recent_remove };
        id.owner = index_owner_value(u8"start-page-remove-", index);
        return id;
    }

    start_page_element::start_page_element(const start_page_view& page)
        : ui_element { ui_element_id { kind_start_page } }
        , page_ { page }
    {
        add_child(std::make_unique<start_action_element>(
            kind_start_page_open_document, codicons::icon_folder_opened, u8"문서 열기…", make_app_command_action(app_command::show_open_document_dialog)));
        add_child(std::make_unique<start_action_element>(
            kind_start_page_generate_document, codicons::icon_new_file, u8"새 문서 만들기…", make_logic_action(open_generate_document_intent {})));
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

        // 고정한 항목은 `고정됨` 무리로 따로 모은다 (2026-08-23 지시). `recents`가
        // 이미 고정을 앞에 모아 두므로 앞에서부터 세기만 하면 된다.
        const std::size_t recent_count { page_.recents.size() };
        pinned_count_ = 0;
        while (pinned_count_ < recent_count && page_.recents[pinned_count_].pinned)
            ++pinned_count_;

        // 남은 높이에 들어가는 행만 그린다. 스크롤을 두지 않고 넘치는 개수를 마지막
        // 줄로 알린다 (app-shell-design A1.3의 스크롤 대신 채택한 단순화). 머리글도
        // 높이를 먹으므로 고정 무리가 있으면 한 줄이 더 든다.
        const float headers { (pinned_count_ > 0 ? 2.0f : 1.0f) * section_height };
        float available { bottom - recent_top - headers };
        if (available < 0.0f)
            available = 0.0f;

        std::size_t capacity { row_height > 0.0f ? static_cast<std::size_t>(available / row_height) : 0 };
        if (capacity < recent_count && capacity > 0)
            --capacity;
        visible_recents_ = capacity < recent_count ? capacity : recent_count;

        // 머리글과 행을 위에서부터 차례로 쌓는다. 보이지 않는 행은 자리를 먹지
        // 않으므로 안내 줄이 목록 밖으로 밀려나지 않는다.
        float row_y { recent_top };
        if (pinned_count_ > 0)
        {
            pinned_header_ = { recent_left, row_y, recent_width, section_height };
            row_y += section_height;
        }
        else
            pinned_header_ = {};

        const std::size_t visible_pinned { visible_recents_ < pinned_count_ ? visible_recents_ : pinned_count_ };
        for (std::size_t index = 0; index < visible_pinned; ++index)
        {
            rows[index + 2]->set_visible(true);
            rows[index + 2]->arrange({ { recent_left, row_y, recent_width, row_height }, scale });
            row_y += row_height;
        }

        // `최근 항목` 머리글은 고정 무리가 없어도 언제나 있다 — 목록이 비었다는
        // 안내가 붙을 자리이기도 하다.
        recent_header_ = { recent_left, row_y, recent_width, section_height };
        row_y += section_height;

        for (std::size_t index = pinned_count_; index < recent_count; ++index)
        {
            ui_element& row { *rows[index + 2] };
            row.set_visible(index < visible_recents_);
            row.arrange({ { recent_left, row_y, recent_width, row_height }, scale });
            if (index < visible_recents_)
                row_y += row_height;
        }
        for (std::size_t index = visible_pinned; index < pinned_count_; ++index)
            rows[index + 2]->set_visible(false);

        recent_empty_ = { recent_left, row_y, recent_width, row_height };
    }

    void start_page_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        SkPaint dim { solid_paint(context.palette.primary_foreground) };
        dim.setAlphaf(0.55f);

        const SkFont title_font { ui_font(context, 26.0f) };
        static_cast<void>(draw_text_within(context.canvas, u8"Gitman", title_.x, title_.y + centered_text_baseline(title_font, title_.height), title_.width, title_font, foreground));

        const SkFont body_font { ui_font(context, 12.0f) };
        static_cast<void>(draw_text_within(context.canvas, u8".version-list 작업공간을 열어 여러 저장소의 상태를 한 화면에서 관리합니다.", subtitle_.x,
            subtitle_.y + centered_text_baseline(body_font, subtitle_.height), subtitle_.width, body_font, dim));

        const SkFont section_font { ui_font(context, 13.0f) };
        static_cast<void>(draw_text_within(context.canvas, u8"시작", action_section_.x + 4.0f * scale,
            action_section_.y + centered_text_baseline(section_font, layout_start_page_section_height * scale), action_section_.width, section_font, dim));
        if (pinned_header_.width > 0.0f)
            static_cast<void>(draw_text_within(context.canvas, u8"고정됨", pinned_header_.x + 4.0f * scale,
                pinned_header_.y + centered_text_baseline(section_font, pinned_header_.height), pinned_header_.width, section_font, dim));
        static_cast<void>(draw_text_within(context.canvas, u8"최근 항목", recent_header_.x + 4.0f * scale,
            recent_header_.y + centered_text_baseline(section_font, recent_header_.height), recent_header_.width, section_font, dim));

        draw_children(context, interaction);

        // 목록 자리의 안내다. 읽는 중, 항목 없음, 잘린 개수를 같은 줄에 그린다.
        std::u8string notice {};
        if (page_.loading)
            notice = u8"최근 항목을 읽는 중입니다…";
        else if (visible_recents_ < page_.recents.size())
            notice = count_text(page_.recents.size() - visible_recents_);
        else if (pinned_count_ == page_.recents.size())
            // 다 고정했거나 아예 비었다. 어느 쪽이든 `최근 항목` 무리는 비어 있다.
            notice = u8"최근에 연 문서가 없습니다.";
        if (notice.empty())
            return;

        static_cast<void>(
            draw_text_within(context.canvas, notice, recent_empty_.x + 4.0f * scale, recent_empty_.y + centered_text_baseline(body_font, recent_empty_.height), recent_empty_.width, body_font, dim));
    }
} // namespace gitman::ui
