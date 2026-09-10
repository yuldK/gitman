#include "helpers/ui_test_support.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"

#include "luil/ui/ui_element_id.h"
#include "luil/ui/ui_events.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

namespace {
    namespace ui = gitman::ui;
} // namespace

// tree·view 만들기와 clean 검사(중복 id·미배치 없음)는 공통 도우미의 몫이다.
using namespace gitman::testing;

TEST_CASE("The built tree contains toolbar, cards, and their buttons", "[ui][tree]")
{
    const auto tree { make_tree(3) };

    REQUIRE(tree->ids_of_kind(ui::kind_card_body).size() == 3u);
    REQUIRE(tree->ids_of_kind(ui::kind_card_refresh).size() == 3u);
    REQUIRE(tree->ids_of_kind(ui::kind_card_update).size() == 3u);
    REQUIRE(tree->ids_of_kind(ui::kind_card_switch).size() == 3u);
    REQUIRE(find(*tree, ui::kind_toolbar_refresh_all) != nullptr);

    // 문서가 열려 있으면 열기 버튼은 숨겨진다.
    REQUIRE(find(*tree, ui::kind_toolbar_open_document)->visible() == false);

    // 단계 7 전까지 update와 switch는 비활성이고 사유 tooltip을 가진다.
    const luil::ui_element* const update { find(*tree, ui::kind_card_update, u8"card-0") };
    REQUIRE(update->enabled() == false);
    REQUIRE(update->tooltip().empty() == false);

    // 카드 body는 순서 변경의 drag 출발지다. drop 대상은 목록이다 (4.1).
    const luil::ui_element* const draggable { find(*tree, ui::kind_card_body, u8"card-1") };
    REQUIRE(draggable->drag() != nullptr);
    REQUIRE(draggable->drop() == nullptr);
    REQUIRE(find(*tree, ui::kind_card_list)->drop() != nullptr);

    // 스크롤 막대는 kind 하나에 owner로 어느 목록인지를 담는다 (설계 L4). 카드가
    // 적어 흘릴 것이 없으면 있어도 보이지 않는다.
    const luil::ui_element* const scrollbar { find(*tree, ui::kind_scrollbar, ui::scrollbar_owner_cards) };
    REQUIRE(scrollbar != nullptr);
    REQUIRE(scrollbar->visible() == false);

    // 버튼은 카드 body 위에 그려지므로 hit에서도 이긴다.
    const luil::ui_element* const refresh { find(*tree, ui::kind_card_refresh, u8"card-0") };
    REQUIRE(tree->hit_test(refresh->bounds().x + 1.0f, refresh->bounds().y + 1.0f) == refresh);

    const luil::ui_element* const body { find(*tree, ui::kind_card_body, u8"card-0") };
    REQUIRE(tree->hit_test(body->bounds().x + 1.0f, body->bounds().y + 1.0f) == body);

    // 목록 안 빈 영역은 목록이 받고, 목록도 클릭이 선택 해제다.
    const luil::ui_element* const empty_hit { tree->hit_test(799.0f, 599.0f) };
    REQUIRE(empty_hit->id().kind == ui::kind_card_list);
    REQUIRE(empty_hit->action(luil::ui_trigger::left_click) != nullptr);
}

TEST_CASE("The open document button appears only without a document", "[ui][tree]")
{
    const auto tree { make_tree(0, gitman::view_empty_state::no_document) };
    REQUIRE(find(*tree, ui::kind_toolbar_open_document)->visible());
    REQUIRE(find(*tree, ui::kind_empty_state)->visible());
}

TEST_CASE("The toolbar keeps refresh at the left end and close at the right end", "[ui][tree]")
{
    // 2026-08-21 사용자 지시: 새로 고침은 왼쪽 끝, 닫기는 오른쪽 끝이다.
    const auto tree { make_tree(2) };
    const luil::rect_f refresh { bounds_of(*tree, ui::kind_toolbar_refresh_all) };
    const luil::rect_f close { bounds_of(*tree, ui::kind_toolbar_close_document) };
    const luil::rect_f settings { bounds_of(*tree, ui::kind_toolbar_settings) };
    const luil::rect_f label { bounds_of(*tree, ui::kind_toolbar_document_path) };

    REQUIRE(refresh.x < settings.x);
    REQUIRE(refresh.x < label.x);
    // 문서 경로는 새로 고침 오른쪽에서 시작해 겹치지 않는다.
    REQUIRE(label.x >= refresh.x + refresh.width);
    // 닫기가 가장 오른쪽이고 창 오른쪽 끝에 붙는다.
    REQUIRE(close.x > settings.x);
    REQUIRE(close.x + close.width <= 800.0f);
    REQUIRE(close.x + close.width > settings.x + settings.width);
}

TEST_CASE("The toolbar carries a batch update button next to refresh", "[ui][tree]")
{
    // 일괄 업데이트는 왼쪽 고정이다 (REQ-018). 오른쪽 묶음은 창이 좁아지면 먼저
    // 사라지는 자리라 "위쪽에 제공"이라는 지시와 맞지 않는다.
    const auto tree { make_tree(2) };
    const luil::ui_element* const update_all { find(*tree, ui::kind_toolbar_update_all) };
    REQUIRE(update_all != nullptr);
    REQUIRE(update_all->visible());

    const luil::rect_f refresh { bounds_of(*tree, ui::kind_toolbar_refresh_all) };
    const luil::rect_f batch { update_all->bounds() };
    const luil::rect_f label { bounds_of(*tree, ui::kind_toolbar_document_path) };
    REQUIRE(batch.x >= refresh.x + refresh.width);
    REQUIRE(label.x >= batch.x + batch.width);

    // 준비된 카드가 없으면 비활성이고 사유를 tooltip으로 알린다.
    REQUIRE(update_all->enabled() == false);
    REQUIRE(update_all->tooltip().empty() == false);

    gitman::view_snapshot ready { make_view(2) };
    ready.can_update_all = true;
    REQUIRE(find(*make_tree(ready), ui::kind_toolbar_update_all)->enabled());
}

TEST_CASE("The batch update button is hidden without a document", "[ui][tree]")
{
    // 시작 페이지에 비활성 버튼을 남기지 않는다 (REQ-018).
    const auto tree { make_tree(0, gitman::view_empty_state::no_document) };
    REQUIRE(find(*tree, ui::kind_toolbar_update_all)->visible() == false);
}

TEST_CASE("A card asks for repaints while it works and for a slow tick after it stops", "[ui][tree]")
{
    // 도는 카드는 회전 아이콘과 경과 시간 때문에 계속 다시 그려야 하지만, 멎은
    // 카드가 같은 주기를 요구하면 아무것도 안 하는 창이 30fps로 깨어난다 (REQ-019).
    const luil::update_context context { at(1000) };
    const luil::interaction_snapshot idle {};

    gitman::view_snapshot working { make_view(1) };
    working.cards.front().busy = true;
    working.cards.front().operation_started_at = { at(0) };
    const auto working_tree { make_tree(working) };
    const auto working_next { working_tree->next_update(context, idle) };
    REQUIRE(working_next.has_value());
    REQUIRE(*working_next == context.now);

    gitman::view_snapshot stopped { make_view(1) };
    stopped.cards.front().last_finished_at = { std::chrono::system_clock::now() };
    const auto stopped_tree { make_tree(stopped) };
    const auto stopped_next { stopped_tree->next_update(context, idle) };
    REQUIRE(stopped_next.has_value());
    REQUIRE(*stopped_next > context.now);
    REQUIRE(*stopped_next <= context.now + std::chrono::seconds { 1 });
}

TEST_CASE("The toolbar has no generate button and the close button follows the document", "[ui][tree]")
{
    // 새 문서 만들기 진입점은 시작 페이지뿐이다 (2026-08-22 사용자 지시, G2).
    // 도구 막대는 문서가 열려 있으면 닫기 버튼을, 없으면 열기 버튼만 보인다.
    const auto open_tree { make_tree(2) };
    REQUIRE(open_tree->ids_of_kind(ui::kind_start_page_generate_document).empty());
    REQUIRE(find(*open_tree, ui::kind_toolbar_close_document)->visible());

    const auto tree { make_tree(0, gitman::view_empty_state::no_document) };
    REQUIRE(find(*tree, ui::kind_toolbar_open_document)->visible());
    // 문서가 없으면 닫기 버튼은 없다.
    REQUIRE(find(*tree, ui::kind_toolbar_close_document)->visible() == false);
}

TEST_CASE("Only cards near the viewport become elements", "[ui][tree]")
{
    const auto tree { make_tree(300) };
    const std::size_t visible { tree->ids_of_kind(ui::kind_card_body).size() };
    REQUIRE(visible >= 6u);
    REQUIRE(visible < 20u);
    // 흘릴 것이 있으면 카드 목록 막대가 보인다.
    REQUIRE(find(*tree, ui::kind_scrollbar, ui::scrollbar_owner_cards)->visible());

    // 스크롤하면 앞 카드가 빠지고 뒤 카드가 들어온다.
    const auto scrolled { make_tree(make_view(300, gitman::view_empty_state::none, 1.0f, 4000.0f)) };
    const auto scrolled_cards { scrolled->ids_of_kind(ui::kind_card_body) };
    REQUIRE(scrolled_cards.empty() == false);
    REQUIRE(scrolled_cards.front().owner != u8"card-0");
}

TEST_CASE("DPI scale multiplies every bound", "[ui][tree]")
{
    const auto base { make_tree(make_view(2, gitman::view_empty_state::none, 1.0f)) };
    const auto scaled { make_tree(make_view(2, gitman::view_empty_state::none, 2.0f)) };

    const luil::rect_f base_body { bounds_of(*base, ui::kind_card_body, u8"card-0") };
    const luil::rect_f scaled_body { bounds_of(*scaled, ui::kind_card_body, u8"card-0") };
    REQUIRE(scaled_body.y == base_body.y * 2.0f);
    REQUIRE(scaled_body.height == base_body.height * 2.0f);
}

TEST_CASE("The caption row carries the three window buttons", "[ui][tree][caption]")
{
    // caption 버튼의 배치와 WM_NCHITTEST 일치는 luil caption test의 몫이다 (ADR-007).
    // gitman은 caption이 tree에 있고 목록 layout과 같은 높이를 차지하는지만 본다.
    const auto tree { make_tree(1) };
    const luil::ui_element* const caption { find(*tree, luil::ui_element_kind::caption) };
    REQUIRE(caption != nullptr);
    REQUIRE(caption->bounds().height == gitman::layout_caption_height);

    // 창 버튼 3개는 luil caption이 만든다 (gitman element가 아니다).
    REQUIRE(find(*tree, luil::ui_element_kind::caption_minimize) != nullptr);
    REQUIRE(find(*tree, luil::ui_element_kind::caption_maximize) != nullptr);
    REQUIRE(find(*tree, luil::ui_element_kind::caption_close) != nullptr);
}

TEST_CASE("Card drag slot and offset share one insertion formula", "[ui][tree][drag]")
{
    // 목록 top 76, scale 1: 카드 i의 content top = 10 + i*70, 높이 64다.
    constexpr float list_top { 76.0f };
    const auto slot = [](const float pointer_y, const std::size_t dragged) {
        return gitman::card_drag_insertion_slot(pointer_y, list_top, 0.0f, dragged, 3u, 1.0f);
    };

    // 경계는 각 카드의 세로 중앙이다. card-2(화면 top 226, 중앙 258)의 위
    // 절반이면 그 카드 앞(slot 1), 아래 절반이면 뒤(slot 2)다.
    REQUIRE(slot(list_top + 10.0f + 2.0f * 70.0f + 5.0f, 0u) == 1u);
    REQUIRE(slot(list_top + 10.0f + 2.0f * 70.0f + 60.0f, 0u) == 2u);
    // 카드 사이 여백도 가장 가까운 삽입 위치로 간다.
    REQUIRE(slot(list_top + 10.0f + 70.0f + 64.0f + 3.0f, 0u) == 1u);
    // 목록 위·아래 밖은 처음·끝으로 고정된다.
    REQUIRE(slot(list_top - 50.0f, 1u) == 0u);
    REQUIRE(slot(list_top + 1000.0f, 1u) == 2u);
    // 자기 자리 위 절반·아래 절반은 모두 제자리다.
    REQUIRE(slot(list_top + 10.0f + 5.0f, 0u) == 0u);
    REQUIRE(slot(list_top + 10.0f + 60.0f, 0u) == 0u);

    // offset: card-0을 card-2 뒤(slot 2)로 끌면, 남은 카드 1·2가 한 slot 위로
    // 올라가 빠진 자리를 닫고 끝의 여백이 벌어진다.
    constexpr float pitch { 70.0f };
    REQUIRE(gitman::card_drag_offset(1u, 0u, 2u, 1.0f) == -pitch);
    REQUIRE(gitman::card_drag_offset(2u, 0u, 2u, 1.0f) == -pitch);
    // card-2를 맨 앞(slot 0)으로 끌면 카드 0·1이 내려가 앞자리가 벌어진다.
    REQUIRE(gitman::card_drag_offset(0u, 2u, 0u, 1.0f) == pitch);
    REQUIRE(gitman::card_drag_offset(1u, 2u, 0u, 1.0f) == pitch);
    // 제자리 여백(원래 위치)이면 아무 카드도 움직이지 않는다.
    REQUIRE(gitman::card_drag_offset(1u, 0u, 0u, 1.0f) == 0.0f);
    REQUIRE(gitman::card_drag_offset(2u, 0u, 0u, 1.0f) == 0.0f);
}

TEST_CASE("Escape closes only the topmost dialog", "[ui][tree][dialog]")
{
    // 우선순위는 policy의 사슬이 아니라 **tree를 짓는 순서**가 정한다 (설계 D2-A):
    // dialog마다 luil `modal_host_element`가 서고, luil은 그리기 순서상 마지막
    // 가둠에 Esc를 준다. 그래서 겹쳐 뜬 dialog 중 위의 것 하나만 닫힌다.
    const auto dismiss = [](const gitman::view_snapshot& view) {
        const auto tree { make_tree(view) };
        REQUIRE(tree->focus_trap() != nullptr);
        interaction_harness harness { tree };
        return harness.process(luil::key_pressed_event { luil::key_code::escape });
    };

    // 알림은 다른 dialog 위다 (app-shell-design A3.2).
    gitman::view_snapshot notice_over_settings { make_view(1) };
    notice_over_settings.settings_dialog = gitman::settings_dialog_view {};
    notice_over_settings.notice_dialog = gitman::notice_dialog_view {};
    const auto notice { dismiss(notice_over_settings) };
    REQUIRE(std::holds_alternative<gitman::dismiss_notice_intent>(*as_message(notice)));

    // 글꼴 선택은 환경설정 위에서 열린다 (F2.5).
    gitman::view_snapshot font_over_settings { make_view(1) };
    font_over_settings.settings_dialog = gitman::settings_dialog_view {};
    font_over_settings.font_dialog = gitman::font_dialog_view {};
    const auto font { dismiss(font_over_settings) };
    REQUIRE(std::holds_alternative<gitman::cancel_font_dialog_intent>(*as_message(font)));

    // 새 문서 만들기는 글꼴 선택보다 위다.
    gitman::view_snapshot generate_over_font { make_view(1) };
    generate_over_font.font_dialog = gitman::font_dialog_view {};
    generate_over_font.generate_dialog = gitman::generate_document_dialog_view {};
    const auto generate { dismiss(generate_over_font) };
    REQUIRE(std::holds_alternative<gitman::cancel_generate_document_intent>(*as_message(generate)));

    // 로컬 변경 확인은 탐색 등록보다 위다.
    gitman::view_snapshot local_over_discovery { make_view(1) };
    local_over_discovery.discovery_dialog = gitman::discovery_dialog_view {};
    local_over_discovery.local_changes_dialog = gitman::local_changes_dialog_view {};
    const auto local_changes { dismiss(local_over_discovery) };
    REQUIRE(std::holds_alternative<gitman::cancel_local_changes_dialog_intent>(*as_message(local_changes)));
}

TEST_CASE("An open dialog keeps the focus order inside itself", "[ui][tree][dialog]")
{
    // 가둠이 서면 Tab 순회가 그 안에서 시작한다 (modal-dialog-design). 도구 막대와
    // 카드 버튼은 목록에서 통째로 빠진다.
    gitman::view_snapshot view { make_view(3) };
    view.settings_dialog = gitman::settings_dialog_view {};
    const auto tree { make_tree(view) };

    const luil::ui_element* const trap { tree->focus_trap() };
    REQUIRE(trap != nullptr);
    REQUIRE(trap->id().kind == luil::ui_element_kind::modal_host);
    REQUIRE(trap->id().owner == std::u8string { ui::dialog_owner_settings });

    const std::vector<luil::ui_element_id> order { tree->focus_order() };
    REQUIRE(order.empty() == false);
    for (const luil::ui_element_id& id : order)
    {
        REQUIRE(tree->within_focus_trap(id));
        REQUIRE(id.kind != ui::kind_toolbar_refresh_all);
        REQUIRE(id.kind != ui::kind_toolbar_update_all);
        REQUIRE(id.kind != ui::kind_toolbar_settings);
        REQUIRE(id.kind != ui::kind_card_body);
        REQUIRE(id.kind != ui::kind_card_refresh);
    }

    // dialog가 없으면 도구 막대가 다시 순회에 든다.
    const auto plain { make_tree(3) };
    REQUIRE(plain->focus_trap() == nullptr);
    const std::vector<luil::ui_element_id> plain_order { plain->focus_order() };
    bool has_toolbar { false };
    for (const luil::ui_element_id& id : plain_order)
        if (id.kind == ui::kind_toolbar_refresh_all)
            has_toolbar = true;
    REQUIRE(has_toolbar);
}
