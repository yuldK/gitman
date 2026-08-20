#include "presentation/ui/build_ui_tree.h"

#include "platform/win32/caption_layout.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/caption_metrics.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>

namespace {
    gitman::view_snapshot make_view(const std::size_t card_count, const float scale = 1.0f, const float scroll = 0.0f, const gitman::view_empty_state empty_state = gitman::view_empty_state::none)
    {
        gitman::view_snapshot view {};
        view.window_width = 800.0f * scale;
        view.window_height = 600.0f * scale;
        view.scale = scale;
        view.scroll_offset = scroll;
        view.empty_state = empty_state;
        for (std::size_t index = 0; index < card_count; ++index)
        {
            gitman::card_view_model card {};
            const std::string digits { std::to_string(index) };
            card.id.value = u8"card-";
            card.id.value.append(digits.begin(), digits.end());
            view.cards.push_back(std::move(card));
        }
        return view;
    }

    gitman::ui::ui_element_id card_id(const gitman::ui::ui_element_kind kind, const std::u8string_view value)
    {
        return { kind, gitman::project_id { std::u8string { value } } };
    }
} // namespace

TEST_CASE("The built tree contains toolbar, cards, and their buttons", "[ui][tree]")
{
    const auto tree { gitman::ui::build_ui_tree(make_view(3)) };

    REQUIRE(tree->ids_of_kind(gitman::ui::ui_element_kind::card_body).size() == 3u);
    REQUIRE(tree->ids_of_kind(gitman::ui::ui_element_kind::card_refresh).size() == 3u);
    REQUIRE(tree->ids_of_kind(gitman::ui::ui_element_kind::card_update).size() == 3u);
    REQUIRE(tree->ids_of_kind(gitman::ui::ui_element_kind::card_switch).size() == 3u);
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::toolbar_refresh_all }) != nullptr);

    // 문서가 열려 있으면 열기 버튼은 숨겨진다.
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::toolbar_open_document })->visible() == false);

    // 단계 7 전까지 update와 switch는 비활성이고 사유 tooltip을 가진다.
    const auto* const update { tree->find(card_id(gitman::ui::ui_element_kind::card_update, u8"card-0")) };
    REQUIRE(update->enabled() == false);
    REQUIRE(update->tooltip().empty() == false);

    // 카드 body는 순서 변경의 drag 출발지다. drop 대상은 목록이다 (4.1).
    const auto* const draggable { tree->find(card_id(gitman::ui::ui_element_kind::card_body, u8"card-1")) };
    REQUIRE(draggable->drag() != nullptr);
    REQUIRE(draggable->drop() == nullptr);
    const auto* const list { tree->find({ gitman::ui::ui_element_kind::card_list }) };
    REQUIRE(list->drop() != nullptr);

    // 버튼은 카드 body 위에 그려지므로 hit에서도 이긴다.
    const auto* const refresh { tree->find(card_id(gitman::ui::ui_element_kind::card_refresh, u8"card-0")) };
    REQUIRE(tree->hit_test(refresh->bounds().x + 1.0f, refresh->bounds().y + 1.0f) == refresh);

    const auto* const body { tree->find(card_id(gitman::ui::ui_element_kind::card_body, u8"card-0")) };
    REQUIRE(tree->hit_test(body->bounds().x + 1.0f, body->bounds().y + 1.0f) == body);

    // 목록 안 빈 영역은 목록이 받고, 목록도 클릭이 선택 해제다.
    const auto* const empty_hit { tree->hit_test(799.0f, 599.0f) };
    REQUIRE(empty_hit->id().kind == gitman::ui::ui_element_kind::card_list);
    REQUIRE(empty_hit->action(gitman::ui::ui_trigger::left_click) != nullptr);
}

TEST_CASE("The open document button appears only without a document", "[ui][tree]")
{
    const auto tree { gitman::ui::build_ui_tree(make_view(0, 1.0f, 0.0f, gitman::view_empty_state::no_document)) };
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::toolbar_open_document })->visible());
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::empty_state })->visible());
}

TEST_CASE("The generate document button stays visible and follows the busy state", "[ui][tree]")
{
    // 문서가 열려 있어도 새 문서 생성은 가능해야 하므로 버튼이 항상 보인다.
    const auto tree { gitman::ui::build_ui_tree(make_view(2)) };
    const auto* const button { tree->find({ gitman::ui::ui_element_kind::toolbar_generate_document }) };
    REQUIRE(button != nullptr);
    REQUIRE(button->visible());
    REQUIRE(button->enabled());
    REQUIRE(button->action(gitman::ui::ui_trigger::left_click) != nullptr);

    // 생성이 진행 중이면 비활성이고 사유 tooltip을 가진다.
    gitman::view_snapshot busy_view { make_view(2) };
    busy_view.document_generating = true;
    const auto busy_tree { gitman::ui::build_ui_tree(busy_view) };
    const auto* const busy_button { busy_tree->find({ gitman::ui::ui_element_kind::toolbar_generate_document }) };
    REQUIRE(busy_button->enabled() == false);
    REQUIRE(busy_button->tooltip().empty() == false);
}

TEST_CASE("Only cards near the viewport become elements", "[ui][tree]")
{
    const auto tree { gitman::ui::build_ui_tree(make_view(300)) };
    const std::size_t visible { tree->ids_of_kind(gitman::ui::ui_element_kind::card_body).size() };
    REQUIRE(visible >= 6u);
    REQUIRE(visible < 20u);

    // 스크롤하면 앞 카드가 빠지고 뒤 카드가 들어온다.
    const auto scrolled { gitman::ui::build_ui_tree(make_view(300, 1.0f, 4000.0f)) };
    const auto scrolled_cards { scrolled->ids_of_kind(gitman::ui::ui_element_kind::card_body) };
    REQUIRE(scrolled_cards.empty() == false);
    REQUIRE(scrolled_cards.front().owner.value != u8"card-0");
}

TEST_CASE("DPI scale multiplies every bound", "[ui][tree]")
{
    const auto base { gitman::ui::build_ui_tree(make_view(2, 1.0f)) };
    const auto scaled { gitman::ui::build_ui_tree(make_view(2, 2.0f)) };

    const auto* const base_body { base->find(card_id(gitman::ui::ui_element_kind::card_body, u8"card-0")) };
    const auto* const scaled_body { scaled->find(card_id(gitman::ui::ui_element_kind::card_body, u8"card-0")) };
    REQUIRE(scaled_body->bounds().y == base_body->bounds().y * 2.0f);
    REQUIRE(scaled_body->bounds().height == base_body->bounds().height * 2.0f);
}

TEST_CASE("Caption buttons in the tree match the Win32 non-client layout", "[ui][tree][caption]")
{
    // WM_NCHITTEST는 caption_layout으로 동기 판정하고, 그리기는 tree의 caption
    // element가 담당한다. 두 계산이 같은 metrics에서 같은 좌표를 내야 한다.
    const auto tree { gitman::ui::build_ui_tree(make_view(1)) };
    const auto layout { gitman::win32::make_caption_layout(800, 96) };

    const auto* const minimize { tree->find({ gitman::ui::ui_element_kind::caption_minimize }) };
    const auto* const maximize { tree->find({ gitman::ui::ui_element_kind::caption_maximize }) };
    const auto* const close { tree->find({ gitman::ui::ui_element_kind::caption_close }) };
    REQUIRE(minimize->bounds().x == static_cast<float>(layout.minimize_left));
    REQUIRE(maximize->bounds().x == static_cast<float>(layout.maximize_left));
    REQUIRE(close->bounds().x == static_cast<float>(layout.close_left));
    REQUIRE(minimize->bounds().width == static_cast<float>(layout.button_width));
    REQUIRE(minimize->bounds().height == static_cast<float>(layout.height));

    // caption 높이 상수는 목록 layout과도 일치해야 한다 (list_metrics 주석).
    REQUIRE(static_cast<float>(gitman::ui::default_caption_ui_metrics.height) == gitman::layout_caption_height);
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
