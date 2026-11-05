#include "presentation/ui/build_ui_tree.h"

#include "domain/path_syntax.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/card_list_element.h"
#include "presentation/ui/discovery_dialog_element.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/font_dialog_element.h"
#include "presentation/ui/function_bar_element.h"
#include "presentation/ui/function_confirm_dialog_element.h"
#include "presentation/ui/function_editor_dialog_element.h"
#include "presentation/ui/generate_document_dialog_element.h"
#include "presentation/ui/gitman_caption.h"
#include "presentation/ui/local_changes_dialog_element.h"
#include "presentation/ui/log_view_element.h"
#include "presentation/ui/notice_dialog_element.h"
#include "presentation/ui/settings_dialog_element.h"
#include "presentation/ui/start_page_element.h"
#include "presentation/ui/switch_dialog_element.h"
#include "presentation/ui/toolbar_element.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/caption_element.h"
#include "luil/ui/label_element.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

        std::u8string count_digits(std::size_t value)
        {
            std::u8string digits {};
            do
            {
                digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + value % 10));
                value /= 10;
            } while (value > 0);
            return digits;
        }

        // 화면 전체를 덮는 root다. 자식의 slot 분배가 이곳의 책임이며, 빈 곳 클릭은
        // 선택 해제다. 자식 순서가 곧 그리기 순서라 caption을 마지막에 두어 카드가
        // caption을 덮지 않는다.
        //
        // luil `root_element`는 final이고 자식을 배치하지 않으므로 앱이 같은 예약
        // kind로 자기 root를 둔다 (설계 L5). 창 배경은 렌더러가 이미 칠했다.
        class root_element final : public ui_element
        {
        public:
            explicit root_element(const view_snapshot& view)
                : ui_element { ui_element_id { ui_element_kind::root } }
            {
                set_action(ui_trigger::left_click, make_logic_click(select_card_intent {}));
                // 배경은 마우스로만 누르는 자리다. Tab은 버튼·dialog 컨트롤·텍스트
                // 칸만 돈다 (설계 D1-B).
                set_tab_stop(false);

                // 배너의 문서 주소는 unix 스타일 표시 문자열이다 (T2). snapshot의
                // `document_path`는 셸·파일 dialog가 쓰는 Windows 원형이라 따로 있다.
                std::u8string document_text { view.document_path.empty() ? std::u8string { u8"문서 없음" } : view.document_display_path };
                // 환경설정·탐색 등록은 열린 문서가 필요한 기능이다.
                const bool document_open { view.empty_state != view_empty_state::no_document && view.empty_state != view_empty_state::document_loading };
                const bool show_open_button { view.empty_state == view_empty_state::no_document };
                // 탐색 등록의 폴더 dialog가 열리는 자리다. UI thread는 view를 읽지
                // 않으므로 명령 인자로 실어 보낸다 (설계 L7).
                std::u8string discovery_start_folder { windows_parent_directory(view.document_path) };
                auto toolbar { std::make_unique<toolbar_element>(std::move(document_text), show_open_button, view.relative_paths, document_open, std::move(discovery_start_folder)) };
                toolbar_ = toolbar.get();
                add_child(std::move(toolbar));

                // 배너는 한 줄이라 첫 알림만 싣는다. 나머지는 개수로 알리고, 툴팁이
                // 전부를 보인다 — 잘린 알림이 없었던 것처럼 보이지 않게 한다. 묶은
                // 알림의 상세는 띠에 보이지 않으므로 알림이 하나여도 툴팁에 싣는다
                // (function-buttons-design 3.8).
                luil::label_config notice_config {};
                std::u8string notice_tooltip {};
                if (view.notices.empty() == false)
                {
                    notice_config.text = view.notices.front().text;
                    if (view.notices.size() > 1)
                    {
                        notice_config.text += u8" (외 ";
                        notice_config.text += count_digits(view.notices.size() - 1);
                        notice_config.text += u8"건)";
                    }

                    const bool has_details {
                        std::ranges::any_of(view.notices
                            , [](const notice_view_model& notice)
                            {
                                return notice.details.empty() == false;
                            }
                        )
                    };

                    if (view.notices.size() > 1 || has_details)
                    {
                        for (const notice_view_model& notice : view.notices)
                        {
                            if (notice_tooltip.empty() == false)
                                notice_tooltip += u8"\n";
                            notice_tooltip += notice.text;
                            for (const std::u8string& detail : notice.details)
                            {
                                notice_tooltip += u8"\n  ";
                                notice_tooltip += detail;
                            }
                        }
                    }
                }
                notice_config.color = luil::label_color_role::error;
                notice_config.background = luil::label_background_role::notice;
                notice_config.padding = layout_margin;
                auto notice { std::make_unique<luil::label_element>(ui_element_id { kind_notice }, std::move(notice_config)) };
                notice->set_live_region(luil::access_live::polite);
                notice->set_visible(view.notices.empty() == false);
                if (notice_tooltip.empty() == false)
                    notice->set_tooltip(std::move(notice_tooltip));
                notice_ = notice.get();
                add_child(std::move(notice));

                auto card_list { std::make_unique<card_list_element>(view) };
                card_list_ = card_list.get();
                add_child(std::move(card_list));

                luil::label_config empty_config {};
                empty_config.text = std::u8string { empty_state_text(view.empty_state) };
                empty_config.font_size = 13.0f;
                auto empty_label { std::make_unique<luil::label_element>(ui_element_id { kind_empty_state }, std::move(empty_config)) };
                // 열린 문서가 없으면 안내 문구 대신 시작 페이지가 그 자리를 채운다
                // (app-shell-design A1.3).
                empty_label->set_visible(view.empty_state != view_empty_state::none && view.start_page.has_value() == false);
                empty_label_ = empty_label.get();
                add_child(std::move(empty_label));

                if (view.start_page.has_value())
                {
                    auto start_page { std::make_unique<start_page_element>(*view.start_page) };
                    start_page_ = start_page.get();
                    add_child(std::move(start_page));
                }

                // 선택 카드가 있을 때만 하단 로그 pane을 둔다 (REQ-008). 스크롤
                // 막대의 thumb 계산에 본문 높이가 필요해 layout을 미리 구한다
                // (arrange의 계산과 같은 함수·같은 조건이다).
                if (view.log.has_value())
                {
                    const float scale { view.scale > 0.0f ? view.scale : 1.0f };
                    const list_layout layout { compute_list_layout(view.window_height, scale, view.notices.empty() == false, true) };
                    // 기능 줄은 로그 pane 160 안 맨 위를 가져간다 (function-buttons-design 2.4). 목록 viewport는
                    // 그대로이고 로그 본문이 그만큼 준다. Tab 순서가 그리기 순서라 줄을 로그 헤더 앞에 둔다 (2.2.6).
                    float bar_height { 0.0f };
                    if (view.function_bar.has_value())
                    {
                        auto bar { std::make_unique<function_bar_element>(*view.function_bar) };
                        function_bar_ = bar.get();
                        add_child(std::move(bar));
                        bar_height = layout_function_bar_height() * scale;
                    }
                    float body_height { layout.log_height - bar_height - layout_log_header_height() * scale };
                    if (body_height < 0.0f)
                        body_height = 0.0f;
                    auto log_pane { std::make_unique<log_view_element>(*view.log, body_height, scale) };
                    log_pane_ = log_pane.get();
                    add_child(std::move(log_pane));
                }

                auto caption { std::make_unique<luil::caption_element>(gitman_caption()) };
                caption_ = caption.get();
                add_child(std::move(caption));

                // dialog는 맨 마지막 자식이라 모든 것 위에 그려지고 hit test도 먼저
                // 걸린다. 창 이동은 비클라이언트 경로라 계속 동작한다.
                //
                // dialog 하나하나가 luil `modal_host_element`다 (설계 D2-A). host가
                // scrim·가운데 배치·초점 가둠·Esc를 맡고 gitman element는 그 안의
                // content다. 그래서 **추가 순서가 곧 Esc 우선순위**다 — luil은 그리기
                // 순서상 마지막 가둠에 Esc를 준다. 목표 우선순위(notice >
                // function_confirm > function_editor > local_changes > discovery > generate > font > settings > switch)를
                // 뒤집어 단다.
                //
                // factory에 넘기는 창 크기는 **논리 픽셀**이다 (설계 L4). host가
                // 자기 arrange에서 배율을 곱한다.
                const float dialog_scale { view.scale > 0.0f ? view.scale : 1.0f };
                const float logical_width { view.window_width / dialog_scale };
                const float logical_height { view.window_height / dialog_scale };
                if (view.switch_dialog.has_value())
                {
                    auto dialog { make_switch_dialog(*view.switch_dialog, logical_width, logical_height) };
                    switch_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                if (view.settings_dialog.has_value())
                {
                    auto dialog { make_settings_dialog(*view.settings_dialog, logical_width, logical_height) };
                    settings_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                // 글꼴 선택 dialog는 환경설정 위에 뜬다 (F2.5).
                if (view.font_dialog.has_value())
                {
                    auto dialog { make_font_dialog(*view.font_dialog, logical_width, logical_height) };
                    font_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                // 새 문서 만들기 dialog다 (F3).
                if (view.generate_dialog.has_value())
                {
                    auto dialog { make_generate_document_dialog(*view.generate_dialog, logical_width, logical_height) };
                    generate_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                if (view.discovery_dialog.has_value())
                {
                    auto dialog { make_discovery_dialog(*view.discovery_dialog, logical_width, logical_height) };
                    discovery_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                if (view.local_changes_dialog.has_value())
                {
                    auto dialog { make_local_changes_dialog(*view.local_changes_dialog, logical_width, logical_height) };
                    local_changes_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                // 기능 편집은 메뉴에서 열린다 (function-buttons-design 2.7). 확인 dialog와 알림만 그 위다.
                if (view.function_editor.has_value())
                {
                    auto dialog { make_function_editor_dialog(*view.function_editor, logical_width, logical_height) };
                    function_editor_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                // 기능 실행 확인은 카드·메뉴·키에서 열린다. 알림만 그 위다 (function-buttons-design 3.10).
                if (view.function_confirm_dialog.has_value())
                {
                    auto dialog { make_function_confirm_dialog(*view.function_confirm_dialog, logical_width, logical_height) };
                    function_confirm_dialog_ = dialog.get();
                    add_child(std::move(dialog));
                }
                // 알림은 다른 dialog 위다 (app-shell-design A3.2). 컨텍스트 메뉴는
                // 주 tree에 없다 — 창 경계를 넘는 popup 창이다 (설계 D3-A,
                // `build_context_menu_popup`).
                if (view.notice_dialog.has_value())
                {
                    auto dialog { make_notice_dialog(*view.notice_dialog, logical_width, logical_height) };
                    notice_dialog_ = dialog.get();
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
                notice_->arrange({ { 0.0f, notice_top, context.slot.width, layout_notice_height() * scale }, scale });
                card_list_->arrange({ { 0.0f, layout.content_top, context.slot.width, layout.viewport_height }, scale, context.scroll_offset });
                if (log_pane_ != nullptr)
                {
                    // 기능 줄이 있으면 pane 위를 줄이 쓰고 로그 pane은 남은 높이다 (100%에서 36 + 124). 창이 아주 낮아
                    // pane이 줄보다 낮으면 줄이 먼저 남는다. 줄은 배치의 측정기로 버튼 폭을 재므로 그대로 넘긴다.
                    float bar_height { 0.0f };
                    if (function_bar_ != nullptr)
                    {
                        bar_height = std::min(layout_function_bar_height() * scale, layout.log_height);
                        function_bar_->arrange(context.for_child({ 0.0f, layout.log_top, context.slot.width, bar_height }));
                    }
                    log_pane_->arrange({ { 0.0f, layout.log_top + bar_height, context.slot.width, layout.log_height - bar_height }, scale });
                }
                // dialog host는 창 slot을 통째로 받는다 — scrim이 그것을 덮고 본체는
                // host가 가운데에 놓는다 (설계 D2-A).
                if (switch_dialog_ != nullptr)
                    switch_dialog_->arrange({ context.slot, scale });
                if (settings_dialog_ != nullptr)
                    settings_dialog_->arrange({ context.slot, scale });
                if (font_dialog_ != nullptr)
                    font_dialog_->arrange({ context.slot, scale });
                if (generate_dialog_ != nullptr)
                    generate_dialog_->arrange({ context.slot, scale });
                if (discovery_dialog_ != nullptr)
                    discovery_dialog_->arrange({ context.slot, scale });
                if (local_changes_dialog_ != nullptr)
                    local_changes_dialog_->arrange({ context.slot, scale });
                if (function_editor_dialog_ != nullptr)
                    function_editor_dialog_->arrange({ context.slot, scale });
                if (function_confirm_dialog_ != nullptr)
                    function_confirm_dialog_->arrange({ context.slot, scale });
                if (notice_dialog_ != nullptr)
                    notice_dialog_->arrange({ context.slot, scale });
                if (start_page_ != nullptr)
                    start_page_->arrange({ { 0.0f, layout.content_top, context.slot.width, layout.viewport_height }, scale });

                const float empty_height { 22.0f * scale };
                const float empty_top { layout.content_top + (layout.viewport_height - empty_height) / 2.0f };
                const rect_f empty_slot { layout_margin * scale * 2.0f, empty_top, context.slot.width - layout_margin * 4.0f * scale, empty_height };
                empty_label_->arrange({ empty_slot, scale });
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                // 창 배경은 렌더러가 팔레트의 창 배경색으로 이미 칠했다 (luil
                // `root_element`와 같은 계약). 여기서는 자식만 그린다.
                draw_children(context, interaction);
            }

        private:
            ui_element* caption_ { nullptr };
            ui_element* toolbar_ { nullptr };
            ui_element* notice_ { nullptr };
            ui_element* card_list_ { nullptr };
            ui_element* empty_label_ { nullptr };
            ui_element* start_page_ { nullptr };
            ui_element* function_bar_ { nullptr };
            ui_element* log_pane_ { nullptr };
            ui_element* switch_dialog_ { nullptr };
            ui_element* settings_dialog_ { nullptr };
            ui_element* font_dialog_ { nullptr };
            ui_element* generate_dialog_ { nullptr };
            ui_element* discovery_dialog_ { nullptr };
            ui_element* local_changes_dialog_ { nullptr };
            ui_element* function_editor_dialog_ { nullptr };
            ui_element* function_confirm_dialog_ { nullptr };
            ui_element* notice_dialog_ { nullptr };
        };
    } // namespace

    std::shared_ptr<const ui_tree> build_ui_tree(const view_snapshot& view, const text_measurer* const measurer)
    {
        auto root { std::make_unique<root_element>(view) };
        // 스크롤 값은 논리 픽셀이다 (설계 L4). 배율은 흘리는 컨테이너가 곱한다.
        arrange_context context { { 0.0f, 0.0f, view.window_width, view.window_height }, view.scale, view.scroll_offset };
        context.measurer = measurer;
        root->arrange(context);
        return std::make_shared<const ui_tree>(std::move(root));
    }

    std::optional<ui_element_id> function_focus_target(const ui_tree& tree, const function_focus_request& request)
    {
        std::vector<ui_element_id> candidates {};
        for (const std::u8string& button : request.next_buttons)
            candidates.push_back({ kind_function_bar_button, card_function_owner(request.card.value, button) });
        candidates.push_back({ kind_function_bar_more });
        candidates.push_back({ kind_card_function_more, request.card.value });
        // 보이지 않는(접힌) 버튼, 비활성 버튼(선택이 없는 줄의 `⋯` 등)은 초점을 받지 못한다. luil이 초점을 줄 때와
        // 쥐고 있는 동안의 조건(활성, 조상까지 보임)과 같게 본다.
        for (const ui_element_id& id : candidates)
        {
            const ui_element* const element { tree.find(id) };
            if (element != nullptr && element->enabled() && element->tab_stop() && tree.visibly_contains(id))
                return id;
        }
        return std::nullopt;
    }
} // namespace gitman::ui
