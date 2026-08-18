#include "presentation/ui/build_ui_tree.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/caption_element.h"
#include "presentation/ui/card_list_element.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/label_element.h"
#include "presentation/ui/log_view_element.h"
#include "presentation/ui/switch_dialog_element.h"
#include "presentation/ui/toolbar_element.h"
#include "presentation/ui/update_overlay_element.h"

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
                auto toolbar { std::make_unique<toolbar_element>(std::move(document_text), view.empty_state == view_empty_state::no_document, view.document_generating, view.relative_paths) };
                toolbar_ = toolbar.get();
                add_child(std::move(toolbar));

                label_config notice_config {};
                notice_config.text = view.notices.empty() ? std::u8string {} : view.notices.front();
                notice_config.color = label_color_role::error;
                notice_config.background = label_background_role::notice;
                notice_config.padding = layout_margin;
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

                // 선택 카드가 있을 때만 하단 로그 pane을 둔다 (REQ-008).
                if (view.log.has_value())
                {
                    auto log_pane { std::make_unique<log_view_element>(*view.log) };
                    log_pane_ = log_pane.get();
                    add_child(std::move(log_pane));
                }

                auto caption { std::make_unique<caption_element>(u8"Gitman") };
                caption_ = caption.get();
                add_child(std::move(caption));

                // overlay와 dialog는 맨 마지막 자식이라 모든 것 위에 그려지고 hit
                // test도 먼저 걸린다. 창 이동은 비클라이언트 경로라 계속 동작한다.
                if (view.update_overlay.has_value())
                {
                    auto overlay { std::make_unique<update_overlay_element>(*view.update_overlay) };
                    update_overlay_ = overlay.get();
                    add_child(std::move(overlay));
                }
                if (view.switch_dialog.has_value())
                {
                    auto dialog { std::make_unique<switch_dialog_element>(*view.switch_dialog) };
                    switch_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
            }

            void arrange(const arrange_context& context) override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                set_bounds(context.slot);

                const float caption_height { layout_caption_height * scale };
                const float toolbar_height { layout_toolbar_height * scale };
                // 배너가 보이면 목록이 그만큼 아래에서 시작한다. 목록·element·logic이
                // 같은 함수로 계산해야 스크롤 한계와 그리기가 어긋나지 않는다.
                const list_layout layout { compute_list_layout(context.slot.height, scale, notice_->visible(), log_pane_ != nullptr) };
                caption_->arrange({ { 0.0f, 0.0f, context.slot.width, caption_height }, scale });
                toolbar_->arrange({ { 0.0f, caption_height, context.slot.width, toolbar_height }, scale });
                const float notice_top { caption_height + toolbar_height };
                // 배너는 막대처럼 창 폭을 가득 채운다. 바탕색이 카드와 구분을 만든다.
                notice_->arrange({ { 0.0f, notice_top, context.slot.width, layout_notice_height * scale }, scale });
                card_list_->arrange({ { 0.0f, layout.content_top, context.slot.width, layout.viewport_height }, scale, context.scroll_offset });
                if (log_pane_ != nullptr)
                    log_pane_->arrange({ { 0.0f, layout.log_top, context.slot.width, layout.log_height }, scale });
                if (update_overlay_ != nullptr)
                    update_overlay_->arrange({ context.slot, scale });
                if (switch_dialog_ != nullptr)
                    switch_dialog_->arrange({ context.slot, scale });
                const float empty_height { 22.0f * scale };
                const float empty_top { layout.content_top + (layout.viewport_height - empty_height) / 2.0f };
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
            ui_element* log_pane_ { nullptr };
            ui_element* update_overlay_ { nullptr };
            ui_element* switch_dialog_ { nullptr };
        };
    } // namespace

    std::shared_ptr<const ui_tree> build_ui_tree(const view_snapshot& view)
    {
        auto root { std::make_unique<root_element>(view) };
        root->arrange({ { 0.0f, 0.0f, view.window_width, view.window_height }, view.scale, view.scroll_offset });
        return std::make_shared<const ui_tree>(std::move(root));
    }
} // namespace gitman::ui
