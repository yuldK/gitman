#include "presentation/ui/gitman_policy.h"

#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace gitman::ui {
    namespace {
        // dialog가 tree에 있는지만 본다. 포인터 자리는 묻지 않는다 — 열린 dialog는
        // 화면을 가두므로 휠이 그 dialog의 것이다. Esc는 여기까지 오지 않는다
        // (luil `modal_host_element`가 먼저 받는다).
        [[nodiscard]] bool tree_has(const luil::ui_tree& tree, const luil::ui_element_kind kind)
        {
            return tree.find(luil::ui_element_id { kind }) != nullptr;
        }

        // 기능 실행 확인 dialog 목록의 스크롤 키다. 목록 항목은 Tab 자리가 아니라서 키보드만 쓰는
        // 사용자는 이 키로 보이지 않는 항목에 닿는다 (function-buttons-design B6, T7).
        [[nodiscard]] std::optional<function_confirm_scroll_step> function_confirm_key_step(const luil::key_pressed_event& event) noexcept
        {
            if (event.control || event.alt || event.shift)
                return std::nullopt;
            switch (event.key)
            {
            case luil::key_code::arrow_up:
                return function_confirm_scroll_step::line_up;
            case luil::key_code::arrow_down:
                return function_confirm_scroll_step::line_down;
            case luil::key_code::page_up:
                return function_confirm_scroll_step::page_up;
            case luil::key_code::page_down:
                return function_confirm_scroll_step::page_down;
            case luil::key_code::home:
                return function_confirm_scroll_step::start;
            case luil::key_code::end:
                return function_confirm_scroll_step::end;
            default:
                break;
            }
            return std::nullopt;
        }

        // `Alt+1..3`의 N이다 (function-buttons-design 2.3.4). Alt만 눌린 숫자 키다.
        [[nodiscard]] std::optional<std::size_t> function_shortcut_number(const luil::key_pressed_event& event) noexcept
        {
            if (event.alt == false || event.control || event.shift || event.meta)
                return std::nullopt;
            switch (event.key)
            {
            case luil::key_code::key_1:
                return 1;
            case luil::key_code::key_2:
                return 2;
            case luil::key_code::key_3:
                return 3;
            default:
                break;
            }
            return std::nullopt;
        }

        [[nodiscard]] bool is_function_menu_key(const luil::key_pressed_event& event) noexcept
        {
            return event.key == luil::key_code::arrow_down && event.alt && event.control == false && event.shift == false && event.meta == false;
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
        case text_input_target::function_editor_label:
        case text_input_target::function_editor_program:
        case text_input_target::function_editor_arguments:
        case text_input_target::function_editor_working_directory:
        case text_input_target::function_editor_timeout:
            return static_cast<text_input_target>(static_cast<std::uint32_t>(target));
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
        // 기능 편집 dialog의 다섯 칸이다 (function-buttons-design 2.7).
        if (kind == kind_function_editor_label_input)
            return to_luil_target(text_input_target::function_editor_label);
        if (kind == kind_function_editor_program_input)
            return to_luil_target(text_input_target::function_editor_program);
        if (kind == kind_function_editor_arguments_input)
            return to_luil_target(text_input_target::function_editor_arguments);
        if (kind == kind_function_editor_working_directory_input)
            return to_luil_target(text_input_target::function_editor_working_directory);
        if (kind == kind_function_editor_timeout_input)
            return to_luil_target(text_input_target::function_editor_timeout);
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
        // 메뉴는 luil `menu_element`가 뿌리인 popup tree다 (설계 D3-A). 키보드
        // 탐색(↑/↓/Enter/Esc)이 그 tree에서 돌도록 luil kind를 그대로 돌려준다.
        return luil::menu_kinds { luil::ui_element_kind::menu, luil::ui_element_kind::menu_item };
    }

    std::vector<luil::input_action> gitman_policy::close_menu() const
    {
        return { make_logic_action(close_context_menu_intent {}) };
    }

    std::vector<luil::input_action> gitman_policy::on_focus_moved(const luil::ui_tree& tree, const luil::ui_element_id& focused)
    {
        // 기능 편집 dialog의 폼과 목록은 앱이 스크롤 값을 든다. Tab·화살표로 닿은 칸이나 행이 보이는 자리 밖이면 그만큼
        // 흘린다 (luil `route_reveal`, 얼마나는 그 element의 `scroll_delta_to_reveal`이 답한다). 그 밖의 화면은 키보드 초점이
        // 흘리는 창에 들지 않아 기본(빈 목록) 그대로다.
        if (tree_has(tree, kind_function_editor_dialog) == false)
            return luil::route_reveal(tree, focused);
        const std::array<luil::scroll_route, 2> routes {
            luil::scroll_route {
                luil::ui_element_id { kind_function_editor_form },
                [](const float delta) -> luil::input_action
                {
                    return make_logic_action(function_editor_scroll_intent { function_editor_scroll_region::form, delta });
                },
            },
            luil::scroll_route {
                luil::ui_element_id { kind_function_editor_list },
                [](const float delta) -> luil::input_action
                {
                    return make_logic_action(function_editor_scroll_intent { function_editor_scroll_region::list, delta });
                },
            },
        };
        return luil::route_reveal(tree, focused, routes);
    }

    std::vector<luil::input_action> gitman_policy::on_wheel(const luil::ui_tree& tree, const luil::mouse_wheel_event& event, const float scroll_delta)
    {
        // 메뉴 popup 위의 휠은 아무 데도 흘리지 않는다. 주 창 위의 휠은 UI thread가
        // `ui_popup::dismiss`로 닫기를 먼저 내지만(설계 D3-A), popup 표면 자체의 휠은
        // 닫힘 계기가 아니라 popup tree가 그대로 여기로 온다 — 뒤의 카드 목록이
        // 스크롤되면 메뉴가 앵커에서 떠 버린다.
        if (tree.find(luil::ui_element_id { luil::ui_element_kind::menu }) != nullptr)
            return {};

        // 기능 실행 확인 dialog는 카드 위의 다른 dialog보다 위에 뜬다. 휠은 그 목록을 스크롤한다.
        if (tree_has(tree, kind_function_confirm_dialog))
            return { make_logic_action(function_confirm_scroll_intent { scroll_delta }) };

        // 기능 편집 dialog는 목록(또는 변수 탭) 위의 휠이 목록을, 그 밖의 휠이 폼을 스크롤한다.
        if (tree_has(tree, kind_function_editor_dialog))
        {
            // 폼 밖(목록, 변수 탭, 미리보기 등)은 목록이다. 폼이 없으면(변수 탭) 늘 목록이다.
            const luil::ui_element* const form { tree.find(luil::ui_element_id { kind_function_editor_form }) };
            const bool over_form { form != nullptr && form->visible() && form->bounds().contains(event.x, event.y) };
            return { make_logic_action(function_editor_scroll_intent { over_form ? function_editor_scroll_region::form : function_editor_scroll_region::list, scroll_delta }) };
        }

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
        // 기능 줄은 로그 pane 160 안 맨 위 36이다 (function-buttons-design 2.4). 줄이 pane 자리를 나눠 가졌을 뿐이라
        // 그 위의 휠도 줄이 없을 때처럼 로그를 스크롤한다. 줄 자신은 스크롤할 것이 없다.
        const luil::ui_element* const bar { tree.find(luil::ui_element_id { kind_function_bar }) };
        if (bar != nullptr && bar->visible() && bar->bounds().contains(event.x, event.y))
            return { make_logic_action(log_scroll_intent { scroll_delta }) };

        return { make_logic_action(scroll_intent { scroll_delta }) };
    }

    std::vector<luil::input_action> gitman_policy::on_key(const luil::ui_tree* const tree, const luil::key_pressed_event& event, const luil::interaction_snapshot& snapshot)
    {
        // 텍스트 칸·메뉴·Tab·활성 키·가둠 Esc·drag 중 Esc는 luil controller가 이미
        // 가져갔다. 여기 오는 것은 앱만 뜻을 아는 키다.
        //
        // D1-B로 카드 body가 Tab 자리에서 빠졌으므로 카드를 클릭한 뒤의 Enter도
        // 활성 키에 걸리지 않고 여기까지 온다 — 아래 `enter` 가지가 `focused_card_`
        // 새로 고침을 낸다 (클릭은 `on_click`이 그 값을 세워 둔다).
        //
        // 기능 실행 확인 dialog가 떠 있으면 스크롤 키는 그 목록의 것이다. 방향키가 dialog 뒤의 카드
        // 선택을 바꾸지 않게 여기서 끝낸다.
        if (tree != nullptr && tree_has(*tree, kind_function_confirm_dialog))
        {
            const std::optional<function_confirm_scroll_step> step { function_confirm_key_step(event) };
            if (step.has_value())
                return { make_logic_action(function_confirm_scroll_intent { 0.0f, *step }) };
        }

        // 기능 편집 dialog가 떠 있으면 `Alt+↑/↓`이 고른 기능의 순서를 바꾼다 (2.7). 다른 앱 키는 dialog 뒤의 카드에 가지
        // 않게 여기서 끝낸다 (방향키의 카드 이동, Enter의 카드 갱신 등).
        if (tree != nullptr && tree_has(*tree, kind_function_editor_dialog))
        {
            const bool alt_only { event.alt && event.control == false && event.shift == false && event.meta == false };
            if (alt_only && (event.key == luil::key_code::arrow_up || event.key == luil::key_code::arrow_down))
                return { make_logic_action(edit_function_editor_list_intent { event.key == luil::key_code::arrow_up ? function_editor_list_edit::move_up : function_editor_list_edit::move_down }) };
            return {};
        }

        // 카드 기능의 단축키다 (function-buttons-design 2.3.4). `Alt+1..3`은 초점 카드의 N번째 `card` 기능을
        // 시작하고, `Alt+↓`은 초점 카드의 기능 메뉴를 연다. 아래 ↑/↓ 카드 이동보다 먼저 본다.
        const std::optional<std::size_t> shortcut { function_shortcut_number(event) };
        if (shortcut.has_value() || is_function_menu_key(event))
        {
            // 누르고 있는 동안의 자동 반복으로 거듭 내지 않는다. dialog가 떠 있으면 키는 그 dialog의 것이다 —
            // 뒤의 카드에서 기능이 시작되면 안 된다.
            if (tree == nullptr || event.repeat || tree->ids_of_kind(luil::ui_element_kind::modal_host).empty() == false)
                return {};
            const std::optional<project_id> card { focus_card(*tree, snapshot) };
            if (card.has_value() == false)
                return {};
            if (shortcut.has_value())
                return { make_logic_action(run_function_shortcut_intent { *card, *shortcut }) };
            // 메뉴는 `⋯`의 왼쪽 아래에 연다 (`⋯` 클릭과 같은 자리). 적용 기능이 없어 `⋯`가 없거나 보이지 않는
            // 카드(목록 밖, 아주 좁은 창)에서는 아무것도 하지 않는다. `visible()`은 자기 표시만 말하므로 카드 body도
            // 본다 — 목록은 화면 위아래 한 장씩을 숨긴 채 tree에 두어(`card_list_element`) 그 `⋯`는 화면 밖이다.
            const luil::ui_element* const body { tree->find(luil::ui_element_id { kind_card_body, card->value }) };
            const luil::ui_element* const more { tree->find(luil::ui_element_id { kind_card_function_more, card->value }) };
            if (body == nullptr || body->visible() == false || more == nullptr || more->visible() == false)
                return {};
            const luil::rect_f box { more->bounds() };
            return { make_logic_action(open_function_menu_intent { *card, box.x, box.y + box.height }) };
        }

        switch (event.key)
        {
        case luil::key_code::arrow_down:
        case luil::key_code::arrow_up: {
            // `Alt+↓`은 위에서 기능 메뉴로 끝나 여기 오지 않는다. `Alt+↑`은 예전처럼 카드 이동이다 (설계가 정하지 않았다).
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
        case luil::key_code::f10: {
            // Shift+F10은 Windows의 키보드 컨텍스트 메뉴다. 초점 카드의 메뉴를 카드
            // 왼쪽 위 안쪽에 연다 — 로컬 변경 확인·저장소 열기가 마우스만의 길이었다.
            if (event.shift == false || event.control || event.alt || tree == nullptr || focused_card_.has_value() == false)
                return {};
            const luil::ui_element* const card { tree->find(luil::ui_element_id { kind_card_body, focused_card_->value }) };
            if (card == nullptr || card->visible() == false)
                return {};
            const luil::rect_f box { card->bounds() };
            return { make_logic_action(open_context_menu_intent { *focused_card_, box.x + box.height / 2.0f, box.y + box.height / 2.0f }) };
        }
        case luil::key_code::f5:
            // 누르고 있는 동안의 자동 반복으로 전체 갱신을 거듭 제출하지 않는다.
            if (event.repeat)
                return {};
            return { make_logic_action(refresh_all_intent {}) };
        case luil::key_code::escape: {
            // 열린 dialog의 Esc는 여기까지 오지 않는다. dialog마다 luil
            // `modal_host_element`가 서 있고 controller의 `process_dismiss_key`가
            // 가둠의 dismiss 액션을 먼저 낸다 (설계 D2-A). 어느 dialog가 임자인지는
            // 그리기 순서가 정하므로 우선순위도 tree를 짓는 순서에 산다 — 앱이
            // 사슬을 따로 들면 두 곳이 반드시 어긋난다.
            //
            // 그래서 여기 남는 것은 가둠이 하나도 없을 때의 몫뿐이다: 카드 선택 해제.
            //
            // 선택이 풀리면 기능 줄에 버튼이 없고 줄의 `⋯`는 비활성이라 그 초점이 사라진다. logic이 해제 직전 카드의 `⋯`로
            // 초점을 옮기도록 알린다 (function-buttons-design 2.2.5, D33). 어느 element가 초점을 가졌는지는 여기만 안다.
            focused_card_.reset();
            const bool on_bar {
                snapshot.focused_surface.empty()
                && (snapshot.focused.kind == kind_function_bar_button || snapshot.focused.kind == kind_function_bar_more)
            };
            return { make_logic_action(select_card_intent { std::nullopt, on_bar }) };
        }
        default:
            break;
        }
        return {};
    }

    void gitman_policy::on_click(const luil::ui_element& element)
    {
        // 키보드 탐색의 초점을 마우스 선택에 잇는다 (예전 `process_release`의 몫).
        // 빈 배경(root·카드 목록의 빈 자리·배너 바탕)을 누르면 선택이 풀리므로 초점도
        // 같이 지운다 — 셋 다 `select_card_intent {}`를 내는 자리다 (D1-B).
        const luil::ui_element_kind kind { element.id().kind };
        if (kind == kind_card_body)
            focused_card_ = project_id { element.id().owner };
        // `card` 기능 버튼을 누르면 그 카드가 초점 카드다 (function-buttons-design 2.2.6, D32). 새로 고침·업데이트·전환은
        // 누름이 luil 초점을 세워 owner로 정해지지만 `card` 버튼은 Tab 자리가 아니라 초점이 서지 않으므로 여기서 잇는다.
        // 우클릭(배치 메뉴)의 누름도 같다. 선택은 바꾸지 않는다 — 선택은 카드 body의 클릭 액션만 바꾼다.
        else if (kind == kind_card_function)
            focused_card_ = project_id { std::u8string { card_of_function_owner(element.id().owner) } };
        else if (kind == luil::ui_element_kind::root || kind == kind_card_list || kind == kind_toolbar)
            focused_card_.reset();
    }

    const std::optional<project_id>& gitman_policy::focused_card() const noexcept
    {
        return focused_card_;
    }

    std::optional<project_id> gitman_policy::focus_card(const luil::ui_tree& tree, const luil::interaction_snapshot& snapshot) const
    {
        // Tab 초점이 카드 안 버튼이면 그 카드다. Tab으로 `⋯`에 서도 `focused_card_`는 세워지지 않으므로
        // (`on_click`은 카드 body와 `card` 기능 버튼만 본다) owner를 먼저 본다 (2.3.4).
        if (snapshot.focused_surface.empty())
        {
            const luil::ui_element_id& focused { snapshot.focused };
            // `card` 기능 버튼은 Tab 자리가 아니라 초점을 갖지 않는다. 대신 `on_click`이 누른 버튼의 카드를
            // `focused_card_`로 세우므로 누른 뒤의 `Alt+N`도 그 카드의 것이다 (2.2.6, D32).
            if (focused.kind == kind_card_function_more || focused.kind == kind_card_refresh || focused.kind == kind_card_update || focused.kind == kind_card_switch)
                return project_id { focused.owner };
            // 기능 줄에 Tab 초점이 있으면 줄의 카드(선택 카드)다 (2.2.6). D32로 `focused_card_`가 다른 카드의 `card` 버튼을
            // 눌러 선택과 갈릴 수 있으므로, 보고 있는 줄과 다른 카드의 기능이 시작되지 않게 `focused_card_`보다 먼저 본다.
            // 줄 버튼은 owner가 `카드:버튼`이고 줄의 `⋯`는 owner가 없어 아래 선택 카드로 간다.
            if (focused.kind == kind_function_bar_button)
                return project_id { std::u8string { card_of_function_owner(focused.owner) } };
        }
        const bool on_bar_more { snapshot.focused_surface.empty() && snapshot.focused.kind == kind_function_bar_more };
        if (focused_card_.has_value() && on_bar_more == false)
            return focused_card_;
        // 마지막은 선택 카드다. 선택의 진실은 logic의 것이고 policy는 tree에 그려진 카드의 선택 표시로만 안다.
        // 그래서 선택 카드가 목록 밖이라 tree에 없으면 초점 카드가 없는 것으로 본다.
        for (const luil::ui_element_id& id : tree.ids_of_kind(kind_card_body))
        {
            const luil::ui_element* const card { tree.find(id) };
            if (card != nullptr && card->accessibility().selected == std::optional<bool> { true })
                return project_id { id.owner };
        }
        return std::nullopt;
    }
} // namespace gitman::ui
