#include "presentation/layout_model.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <string_view>

namespace {
    gitman::view_snapshot make_view(const std::size_t card_count, const float scale = 1.0f, const float scroll = 0.0f)
    {
        gitman::view_snapshot view {};
        view.window_width = 800.0f;
        view.window_height = 600.0f;
        view.scale = scale;
        view.scroll_offset = scroll;
        view.empty_state = card_count == 0 ? gitman::view_empty_state::no_projects : gitman::view_empty_state::none;
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

    std::size_t count_kind(const gitman::layout_snapshot& layout, const gitman::hit_target_kind kind)
    {
        std::size_t count { 0 };
        for (const gitman::hit_area& area : layout.areas)
            if (area.kind == kind)
                ++count;
        return count;
    }

    const gitman::hit_area* find_first(const gitman::layout_snapshot& layout, const gitman::hit_target_kind kind)
    {
        for (const gitman::hit_area& area : layout.areas)
            if (area.kind == kind)
                return &area;
        return nullptr;
    }
} // namespace

TEST_CASE("The layout places the toolbar refresh and per card areas", "[layout][app]")
{
    const gitman::layout_snapshot layout { gitman::compute_layout(make_view(3)) };
    REQUIRE(count_kind(layout, gitman::hit_target_kind::toolbar_refresh_all) == 1u);
    REQUIRE(count_kind(layout, gitman::hit_target_kind::toolbar_open_document) == 0u);
    REQUIRE(count_kind(layout, gitman::hit_target_kind::card_body) == 3u);
    REQUIRE(count_kind(layout, gitman::hit_target_kind::card_refresh) == 3u);
    REQUIRE(count_kind(layout, gitman::hit_target_kind::card_update_disabled) == 3u);
    REQUIRE(count_kind(layout, gitman::hit_target_kind::card_switch_disabled) == 3u);

    // 카드 위 버튼이 카드 body보다 나중에 걸린다. 겹치는 영역은 위에 그려진 쪽이다.
    const gitman::hit_area* const refresh { find_first(layout, gitman::hit_target_kind::card_refresh) };
    REQUIRE(refresh != nullptr);
    const gitman::hit_area hit { gitman::hit_test(layout, refresh->bounds.x + 1.0f, refresh->bounds.y + 1.0f) };
    REQUIRE(hit.kind == gitman::hit_target_kind::card_refresh);
    REQUIRE(hit.id.value == u8"card-0");

    const gitman::hit_area* const body { find_first(layout, gitman::hit_target_kind::card_body) };
    const gitman::hit_area body_hit { gitman::hit_test(layout, body->bounds.x + 1.0f, body->bounds.y + 1.0f) };
    REQUIRE(body_hit.kind == gitman::hit_target_kind::card_body);

    // 아무것도 없는 좌표는 none이다.
    REQUIRE(gitman::hit_test(layout, 799.0f, 599.0f).kind == gitman::hit_target_kind::none);
}

TEST_CASE("The open document button appears only without a document", "[layout][app]")
{
    gitman::view_snapshot view { make_view(0) };
    view.empty_state = gitman::view_empty_state::no_document;
    const gitman::layout_snapshot layout { gitman::compute_layout(view) };
    REQUIRE(count_kind(layout, gitman::hit_target_kind::toolbar_open_document) == 1u);
}

TEST_CASE("Only cards intersecting the viewport get hit areas", "[layout][app]")
{
    // 창 높이 600에서 카드 300개면 대부분 화면 밖이다. hit 영역 수가 화면에
    // 비례해야 카드 수백 개에서도 목록이 작게 유지된다.
    const gitman::layout_snapshot layout { gitman::compute_layout(make_view(300)) };
    const std::size_t visible { count_kind(layout, gitman::hit_target_kind::card_body) };
    REQUIRE(visible >= 6u);
    REQUIRE(visible <= 12u);

    // 스크롤하면 다른 카드가 보인다.
    const gitman::layout_snapshot scrolled { gitman::compute_layout(make_view(300, 1.0f, 4000.0f)) };
    const gitman::hit_area* const first_body { find_first(scrolled, gitman::hit_target_kind::card_body) };
    REQUIRE(first_body != nullptr);
    REQUIRE(first_body->id.value != u8"card-0");
}

TEST_CASE("Scroll clamping keeps the offset inside the content", "[layout][app]")
{
    REQUIRE(gitman::clamp_scroll_offset(-10.0f, 1000.0f, 500.0f) == 0.0f);
    REQUIRE(gitman::clamp_scroll_offset(200.0f, 1000.0f, 500.0f) == 200.0f);
    REQUIRE(gitman::clamp_scroll_offset(9999.0f, 1000.0f, 500.0f) == 500.0f);
    // 내용이 화면보다 작으면 스크롤이 없다.
    REQUIRE(gitman::clamp_scroll_offset(50.0f, 300.0f, 500.0f) == 0.0f);
}

TEST_CASE("DPI scale multiplies every layout measurement", "[layout][app]")
{
    const gitman::layout_snapshot base { gitman::compute_layout(make_view(2, 1.0f)) };
    const gitman::layout_snapshot scaled { gitman::compute_layout(make_view(2, 2.0f)) };
    REQUIRE(scaled.content_height == base.content_height * 2.0f);

    const gitman::hit_area* const base_body { find_first(base, gitman::hit_target_kind::card_body) };
    const gitman::hit_area* const scaled_body { find_first(scaled, gitman::hit_target_kind::card_body) };
    REQUIRE(scaled_body->bounds.height == base_body->bounds.height * 2.0f);
    REQUIRE(scaled_body->bounds.y > base_body->bounds.y);
}
