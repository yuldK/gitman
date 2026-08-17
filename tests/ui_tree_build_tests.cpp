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

    // 카드 body는 순서 변경의 drag 출발지이자 도착지다.
    const auto* const draggable { tree->find(card_id(gitman::ui::ui_element_kind::card_body, u8"card-1")) };
    REQUIRE(draggable->drag() != nullptr);
    REQUIRE(draggable->drop() != nullptr);

    // 버튼은 카드 body 위에 그려지므로 hit에서도 이긴다.
    const auto* const refresh { tree->find(card_id(gitman::ui::ui_element_kind::card_refresh, u8"card-0")) };
    REQUIRE(tree->hit_test(refresh->bounds().x + 1.0f, refresh->bounds().y + 1.0f) == refresh);

    const auto* const body { tree->find(card_id(gitman::ui::ui_element_kind::card_body, u8"card-0")) };
    REQUIRE(tree->hit_test(body->bounds().x + 1.0f, body->bounds().y + 1.0f) == body);

    // 빈 영역은 root(선택 해제)다.
    REQUIRE(tree->hit_test(799.0f, 599.0f)->id().kind == gitman::ui::ui_element_kind::root);
}

TEST_CASE("The open document button appears only without a document", "[ui][tree]")
{
    const auto tree { gitman::ui::build_ui_tree(make_view(0, 1.0f, 0.0f, gitman::view_empty_state::no_document)) };
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::toolbar_open_document })->visible());
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::empty_state })->visible());
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
