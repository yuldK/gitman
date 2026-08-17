#include "presentation/ui/build_ui_tree.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/caption_element.h"
#include "presentation/ui/card_list_element.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/label_element.h"
#include "presentation/ui/toolbar_element.h"

#include "include/core/SkCanvas.h"

#include <string_view>
#include <utility>

namespace gitman::ui {
    namespace {
        std::u8string_view empty_state_text(const view_empty_state state) noexcept
        {
            switch (state)
            {
            case view_empty_state::no_document:
                return u8"열린 문서가 없습니다. 오른쪽 위 버튼으로 .version-list 문서를 여세요.";
            case view_empty_state::document_loading:
                return u8"문서를 여는 중입니다...";
            case view_empty_state::no_projects:
                return u8"문서에 등록된 프로젝트가 없습니다.";
            case view_empty_state::no_filter_match:
                return u8"필터와 일치하는 카드가 없습니다.";
            case view_empty_state::none:
                break;
            }
            return u8"";
        }

        // 화면 전체를 덮는 root다. 자식의 slot 분배가 이곳의 책임이며, 빈 곳 클릭은
        // 선택 해제다. 자식 순서가 곧 그리기 순서라 caption을 마지막에 두어 카드가
        // caption을 덮지 않는다.
        class root_element final : public ui_element
        {
        public:
            explicit root_element(const view_snapshot& view)
                : ui_element { ui_element_id { ui_element_kind::root } }
            {
                set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { select_card_intent {} } } }; });

                std::u8string document_text { view.document_path.empty() ? std::u8string { u8"문서 없음" } : view.document_path };
                auto toolbar { std::make_unique<toolbar_element>(std::move(document_text), view.empty_state == view_empty_state::no_document) };
                toolbar_ = toolbar.get();
                add_child(std::move(toolbar));

                label_config notice_config {};
                notice_config.text = view.notices.empty() ? std::u8string {} : view.notices.front();
                notice_config.color = label_color_role::error;
                auto notice { std::make_unique<label_element>(ui_element_id { ui_element_kind::notice }, std::move(notice_config)) };
                notice->set_visible(view.notices.empty() == false);
                notice_ = notice.get();
                add_child(std::move(notice));

                auto card_list { std::make_unique<card_list_element>(view) };
                card_list_ = card_list.get();
                add_child(std::move(card_list));

                label_config empty_config {};
                empty_config.text = std::u8string { empty_state_text(view.empty_state) };
                empty_config.font_size = 13.0f;
                auto empty_label { std::make_unique<label_element>(ui_element_id { ui_element_kind::empty_state }, std::move(empty_config)) };
                empty_label->set_visible(view.empty_state != view_empty_state::none);
                empty_label_ = empty_label.get();
                add_child(std::move(empty_label));

                auto caption { std::make_unique<caption_element>(u8"Gitman") };
                caption_ = caption.get();
                add_child(std::move(caption));
            }

            void arrange(const arrange_context& context) override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                set_bounds(context.slot);

                const float caption_height { layout_caption_height * scale };
                const float toolbar_height { layout_toolbar_height * scale };
                const float list_top { caption_height + toolbar_height };
                caption_->arrange({ { 0.0f, 0.0f, context.slot.width, caption_height }, scale });
                toolbar_->arrange({ { 0.0f, caption_height, context.slot.width, toolbar_height }, scale });
                notice_->arrange({ { layout_margin * scale, list_top, context.slot.width - layout_margin * 2.0f * scale, 20.0f * scale }, scale });
                card_list_->arrange({ { 0.0f, list_top, context.slot.width, context.slot.height - list_top }, scale, context.scroll_offset });
                const float empty_height { 22.0f * scale };
                const float empty_top { list_top + (context.slot.height - list_top - empty_height) / 2.0f };
                const rect_f empty_slot { layout_margin * scale * 2.0f, empty_top, context.slot.width - layout_margin * 4.0f * scale, empty_height };
                empty_label_->arrange({ empty_slot, scale });
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                context.canvas.clear(context.palette.window_background);
                draw_children(context, interaction);
            }

        private:
            ui_element* caption_ { nullptr };
            ui_element* toolbar_ { nullptr };
            ui_element* notice_ { nullptr };
            ui_element* card_list_ { nullptr };
            ui_element* empty_label_ { nullptr };
        };
    } // namespace

    std::shared_ptr<const ui_tree> build_ui_tree(const view_snapshot& view)
    {
        auto root { std::make_unique<root_element>(view) };
        root->arrange({ { 0.0f, 0.0f, view.window_width, view.window_height }, view.scale, view.scroll_offset });
        return std::make_shared<const ui_tree>(std::move(root));
    }
} // namespace gitman::ui
