#include "helpers/ui_test_support.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"

#include "luil/ui/ui_element_id.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

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
