#include "presentation/ui/gitman_policy.h"

#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"

#include <cstddef>
#include <cstdint>
#include <utility>

namespace gitman::ui {
    namespace {
        // dialog가 tree에 있는지만 본다. 포인터 자리는 묻지 않는다 — 열린 dialog는
        // 화면을 가두므로 휠·Esc가 그 dialog의 것이다.
        [[nodiscard]] bool tree_has(const luil::ui_tree& tree, const luil::ui_element_kind kind)
        {
            return tree.find(luil::ui_element_id { kind }) != nullptr;
        }

        [[nodiscard]] bool tree_has(const luil::ui_tree* const tree, const luil::ui_element_kind kind)
        {
            return tree != nullptr && tree_has(*tree, kind);
        }
    } // namespace

    luil::text_input_target to_luil_target(const text_input_target target) noexcept
    {
        return static_cast<luil::text_input_target>(static_cast<std::uint32_t>(target));
    }

    text_input_target from_luil_target(const luil::text_input_target target) noexcept
    {
        // 값은 그대로 실려 오지만 모르는 값은 `none`으로 떨어뜨린다. 변환이 여기
        // 한 곳뿐이라 표가 갈릴 자리가 없다 (설계 L4).
        switch (static_cast<text_input_target>(static_cast<std::uint32_t>(target)))
        {
        case text_input_target::settings_timeout:
            return text_input_target::settings_timeout;
        case text_input_target::switch_query:
            return text_input_target::switch_query;
        case text_input_target::font_filter:
            return text_input_target::font_filter;
        case text_input_target::generate_document_name:
            return text_input_target::generate_document_name;
        case text_input_target::none:
            break;
        }
        return text_input_target::none;
    }

    std::optional<luil::text_input_target> gitman_policy::text_target_of(const luil::ui_element_kind kind) const
    {
        // 초점을 받아 문자 입력을 소비하는 element다. 새 텍스트 박스는 이 표에만
        // 더하면 초점·caret·문자·클립보드·IME 배선이 함께 따라온다.
        if (kind == kind_settings_timeout_input)
            return to_luil_target(text_input_target::settings_timeout);
        if (kind == kind_switch_dialog_query_input)
            return to_luil_target(text_input_target::switch_query);
        if (kind == kind_font_dialog_query_input)
            return to_luil_target(text_input_target::font_filter);
        if (kind == kind_generate_dialog_name_input)
            return to_luil_target(text_input_target::generate_document_name);
        return std::nullopt;
    }

    luil::input_action gitman_policy::make_text_edit_action(const luil::text_edit_request& request) const
    {
        edit_text_input_intent intent {};
        intent.target = from_luil_target(request.target);
        intent.command = request.command;
        intent.text = request.text;
        intent.offset = request.offset;
        intent.extend = request.extend;
        return make_logic_action(std::move(intent));
    }

    luil::input_action gitman_policy::make_text_composition_action(const luil::text_composition_event& event) const
    {
        set_text_composition_intent intent {};
        intent.target = from_luil_target(event.target);
        intent.text = event.text;
        intent.caret = event.caret;
        intent.composing_begin = event.composing_begin;
        intent.composing_end = event.composing_end;
        intent.composing = event.composing;
        return make_logic_action(std::move(intent));
    }

    std::optional<luil::menu_kinds> gitman_policy::menu() const
    {
        return luil::menu_kinds { kind_context_menu, kind_context_menu_item };
    }

    std::vector<luil::input_action> gitman_policy::close_menu() const
    {
        return { make_logic_action(close_context_menu_intent {}) };
    }

    std::vector<luil::input_action> gitman_policy::on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float scroll_delta)
    {
        // 컨텍스트 메뉴가 떠 있는 동안 휠은 무시한다. 앵커에 붙은 메뉴 아래로
        // 목록이 흘러가면 대상 카드와 어긋난다.
        if (tree_has(tree, kind_context_menu))
            return {};

        // 로컬 변경 dialog가 열려 있으면 휠은 diff pane 위에서는 diff를, 그 밖에서는
        // 목록을 스크롤한다.
        if (tree_has(tree, kind_local_changes_dialog))
        {
            const luil::ui_element* const diff { tree.find(luil::ui_element_id { kind_local_changes_diff }) };
            if (diff != nullptr && diff->bounds().contains(event.x, event.y))
                return { make_logic_action(local_changes_diff_scroll_intent { scroll_delta }) };
            return { make_logic_action(local_changes_scroll_intent { scroll_delta }) };
        }

        // 글꼴 선택 dialog는 환경설정 위에 뜨므로 휠을 먼저 가져간다.
        if (tree_has(tree, kind_font_dialog))
            return { make_logic_action(font_dialog_scroll_intent { scroll_delta }) };

        // 환경설정은 창이 낮을 때만 스크롤할 것이 있다. 막대가 없으면 logic이
        // 한계 0으로 걸러 아무 일도 일어나지 않는다 (E1.4).
        if (tree_has(tree, kind_settings_dialog))
            return { make_logic_action(settings_scroll_intent { scroll_delta }) };

        // switch·탐색 dialog가 열려 있으면 휠은 그 후보 목록을 스크롤한다.
        if (tree_has(tree, kind_switch_dialog))
            return { make_logic_action(switch_dialog_scroll_intent { scroll_delta }) };
        if (tree_has(tree, kind_discovery_dialog))
            return { make_logic_action(discovery_dialog_scroll_intent { scroll_delta }) };

        // 하단 로그 pane 위의 휠은 목록이 아니라 로그를 스크롤한다.
        const luil::ui_element* const pane { tree.find(luil::ui_element_id { kind_log_pane }) };
        if (pane != nullptr && pane->visible() && pane->bounds().contains(event.x, event.y))
            return { make_logic_action(log_scroll_intent { scroll_delta }) };

        return { make_logic_action(scroll_intent { scroll_delta }) };
    }

    std::vector<luil::input_action> gitman_policy::on_key(const luil::ui_tree* const tree, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot)
    {
        // 텍스트 칸·메뉴·Tab·활성 키·가둠 Esc·drag 중 Esc는 luil controller가 이미
        // 가져갔다. 여기 오는 것은 앱만 뜻을 아는 키다.
        switch (event.key)
        {
        case luil::key_code::arrow_down:
        case luil::key_code::arrow_up: {
            if (tree == nullptr)
                return {};
            const std::vector<luil::ui_element_id> cards { tree->ids_of_kind(kind_card_body) };
            if (cards.empty())
                return {};

            std::size_t next { 0 };
            if (focused_card_.has_value())
            {
                std::size_t current { cards.size() };
                for (std::size_t index = 0; index < cards.size(); ++index)
                    if (cards[index].owner == focused_card_->value)
                        current = index;

                if (current == cards.size())
                    next = 0;
                else if (event.key == luil::key_code::arrow_down)
                    next = current + 1 < cards.size() ? current + 1 : current;
                else
                    next = current > 0 ? current - 1 : 0;
            }
            focused_card_ = project_id { cards[next].owner };
            return { make_logic_action(select_card_intent { project_id { cards[next].owner } }) };
        }
        case luil::key_code::enter:
            // 초점을 가진 질의 박스가 있으면 Enter는 그 박스의 몫이다. 카드 갱신으로
            // 흘러가지 않는다 (switch-dialog-query-bar-design Q1).
            if (snapshot.focused_input.kind == kind_switch_dialog_query_input)
                return { make_logic_action(submit_switch_query_intent {}) };
            if (focused_card_.has_value())
                return { make_logic_action(refresh_card_intent { *focused_card_ }) };
            return {};
        case luil::key_code::f5:
            // 누르고 있는 동안의 자동 반복으로 전체 갱신을 거듭 제출하지 않는다.
            if (event.repeat)
                return {};
            return { make_logic_action(refresh_all_intent {}) };
        case luil::key_code::escape:
            // 열린 dialog·overlay가 있으면 닫기가 먼저다. 알림은 다른 dialog 위에
            // 뜨므로 가장 먼저 닫힌다 (app-shell-design A3.2).
            if (tree_has(tree, kind_notice_dialog))
                return { make_logic_action(dismiss_notice_intent {}) };
            if (tree_has(tree, kind_local_changes_dialog))
                return { make_logic_action(cancel_local_changes_dialog_intent {}) };
            if (tree_has(tree, kind_discovery_dialog))
                return { make_logic_action(cancel_discovery_dialog_intent {}) };
            if (tree_has(tree, kind_generate_dialog))
                return { make_logic_action(cancel_generate_document_intent {}) };
            // 글꼴 선택 dialog가 환경설정 위에 있으므로 먼저 닫는다.
            if (tree_has(tree, kind_font_dialog))
                return { make_logic_action(cancel_font_dialog_intent {}) };
            if (tree_has(tree, kind_settings_dialog))
                return { make_logic_action(cancel_settings_dialog_intent {}) };
            if (tree_has(tree, kind_switch_dialog))
                return { make_logic_action(cancel_switch_dialog_intent {}) };
            focused_card_.reset();
            return { make_logic_action(select_card_intent {}) };
        default:
            break;
        }
        return {};
    }

    void gitman_policy::on_click(const luil::ui_element& element)
    {
        // 키보드 탐색의 초점을 마우스 선택에 잇는다 (예전 `process_release`의 몫).
        if (element.id().kind == kind_card_body)
            focused_card_ = project_id { element.id().owner };
        else if (element.id().kind == luil::ui_element_kind::root)
            focused_card_.reset();
    }

    const std::optional<project_id>& gitman_policy::focused_card() const noexcept
    {
        return focused_card_;
    }
} // namespace gitman::ui
