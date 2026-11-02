#include "helpers/function_card_fixture.h"
#include "helpers/ui_test_support.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"

#include "luil/ui/font_scale.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 결정안의 기능 줄 부분(function-buttons-design 2.4)을 tree에서 본다 (6.1 T10 `ui_tree_build_tests`·`text_size_metrics_tests`
// 항목). 줄의 view는 logic이 지은 snapshot이라 줄 유무 판정(D23-A)과 element 배치가 한 경로를 지난다.
using namespace gitman::testing;

namespace {
    namespace ui = gitman::ui;

    // 글자 크기 표를 넣고 test가 끝나면 비운다 (`text_size_metrics_tests.cpp`와 같다).
    class text_size_scope
    {
    public:
        explicit text_size_scope(const float factor)
        {
            std::vector<float> sizes {};
            for (std::size_t size = 0; size <= 64; ++size)
                sizes.push_back(static_cast<float>(size) * factor);
            applied_ = luil::set_system_font_sizes(std::move(sizes));
        }

        ~text_size_scope()
        {
            static_cast<void>(luil::set_system_font_sizes({}));
        }

        text_size_scope(const text_size_scope&) = delete;
        text_size_scope& operator=(const text_size_scope&) = delete;

        [[nodiscard]] bool applied() const noexcept
        {
            return applied_;
        }

    private:
        bool applied_ { false };
    };

    // `card` 하나(a), `bar` 둘(b, c), `menu` 하나(e)다.
    std::vector<gitman::function_button> bar_buttons()
    {
        return {
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"bar"),
            make_function_card_button(u8"c", u8"다", u8"bar"),
            make_function_card_button(u8"e", u8"마"),
        };
    }

    // `bar`가 없는 문서다.
    std::vector<gitman::function_button> barless_buttons()
    {
        return {
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"e", u8"마"),
        };
    }

    void select(function_card_fixture& fixture, const std::u8string_view id)
    {
        fixture.controller.handle(gitman::select_card_intent { gitman::project_id { std::u8string { id } } });
    }

    // 글 폭을 바이트 수 × `per_byte`로 재는 측정기다. 기능 줄 버튼 폭이 글에 달려 있으므로 test가 값을 정한다.
    luil::text_measurer byte_measurer(const float per_byte)
    {
        return [per_byte](const std::u8string_view text, float) { return static_cast<float>(text.size()) * per_byte; };
    }

    std::shared_ptr<const luil::ui_tree> measured_tree(const function_card_fixture& fixture, const luil::text_measurer& measurer)
    {
        const std::shared_ptr<const luil::ui_tree> tree { ui::build_ui_tree(*fixture.snapshot(), &measurer) };
        require_clean_tree(*tree);
        return tree;
    }

    // 글자 버튼(아이콘 14 + 간격 6, 양옆 12)의 선호 폭을 기능 줄 버튼 폭 규칙으로 줄인 값이다.
    float expected_width(const std::u8string_view widest, const float per_byte)
    {
        return gitman::function_bar_button_width(20.0f + static_cast<float>(widest.size()) * per_byte + 24.0f);
    }

    std::u8string bar_owner(const std::u8string_view card, const std::u8string_view button)
    {
        return ui::card_function_owner(card, button);
    }

    // 줄의 Tab 정지점 수다. 보이고 활성인 Tab 자리만 센다.
    std::size_t bar_tab_stops(const luil::ui_tree& tree)
    {
        std::size_t count { 0 };
        for (const luil::ui_element_kind kind : { ui::kind_function_bar_button, ui::kind_function_bar_more })
        {
            for (const luil::ui_element_id& id : tree.ids_of_kind(kind))
            {
                const luil::ui_element* const element { tree.find(id) };
                if (element != nullptr && element->tab_stop() && element->enabled() && tree.visibly_contains(id))
                    ++count;
            }
        }
        return count;
    }

    std::vector<gitman::function_button> many_bar_buttons(const std::size_t count)
    {
        std::vector<gitman::function_button> buttons {};
        for (std::size_t index = 0; index < count; ++index)
        {
            std::u8string id { u8"f" };
            id += static_cast<char8_t>(u8'0' + index / 10);
            id += static_cast<char8_t>(u8'0' + index % 10);
            buttons.push_back(make_function_card_button(id, id, u8"bar"));
        }
        return buttons;
    }
}

TEST_CASE("The log pane splits into bar 36, header 26 and body 98 only with a bar function", "[function_buttons][t10][ui][tree]")
{
    struct window_case
    {
        float width { 0.0f };
        float height { 0.0f };
        // 목록 viewport다 (2.2.4). 줄이 로그 pane 안이라 바뀌지 않는다.
        float viewport { 0.0f };
    };
    const std::vector<window_case> cases {
        { 1280.0f, 720.0f, 484.0f },
        { 480.0f, 320.0f, 84.0f },
    };
    for (const window_case& sample : cases)
    {
        function_card_fixture with_bar { make_function_card_projects(2), bar_buttons(), sample.width, sample.height };
        select(with_bar, u8"p00");
        const auto tree { with_bar.tree() };
        const luil::rect_f bar { bounds_of(*tree, ui::kind_function_bar) };
        const luil::rect_f log { bounds_of(*tree, ui::kind_log_pane) };
        REQUIRE(bar.y == sample.height - 160.0f);
        REQUIRE(bar.height == 36.0f);
        REQUIRE(bar.width == sample.width);
        REQUIRE(log.y == sample.height - 124.0f);
        REQUIRE(log.height == 26.0f + 98.0f);
        REQUIRE(bounds_of(*tree, ui::kind_card_list).height == sample.viewport);
        // 로그 본문의 스크롤 막대는 본문 98 안에 위아래 6을 띄운다.
        REQUIRE(bounds_of(*tree, ui::kind_scrollbar, ui::scrollbar_owner_log).height == 98.0f - 12.0f);

        function_card_fixture without_bar { make_function_card_projects(2), barless_buttons(), sample.width, sample.height };
        select(without_bar, u8"p00");
        const auto plain { without_bar.tree() };
        REQUIRE(find(*plain, ui::kind_function_bar) == nullptr);
        REQUIRE(without_bar.snapshot()->function_bar.has_value() == false);
        REQUIRE(bounds_of(*plain, ui::kind_log_pane).height == 160.0f);
        REQUIRE(bounds_of(*plain, ui::kind_card_list).height == sample.viewport);
    }
}

TEST_CASE("The logic log scroll limit follows the 98 body under the bar", "[function_buttons][t10][ui][tree]")
{
    // 스크롤 한계는 logic이 같은 나누기로 센다. 본문이 98이면 맨 아래는 `내용 - 98`이다.
    function_card_fixture fixture { make_function_card_projects(2), bar_buttons() };
    select(fixture, u8"p00");
    for (int run = 0; run < 8; ++run)
    {
        fixture.run(u8"p00", u8"b");
        fixture.completed(fixture.functions.requests.back(), gitman::function_run_outcome::exited, 0);
    }
    fixture.controller.handle(gitman::log_scroll_intent { -100000.0f });
    fixture.controller.handle(gitman::log_scroll_intent { 100000.0f });
    const auto view { fixture.snapshot() };
    REQUIRE(view->log.has_value());
    const float content { static_cast<float>(view->log->lines.size()) * 15.0f + 16.0f };
    REQUIRE(content > 98.0f);
    REQUIRE(view->log->scroll_offset == content - 98.0f);
}

TEST_CASE("Bar slots follow the 2.2.4 width table", "[function_buttons][t10][ui][tree]")
{
    struct width_case
    {
        float width { 0.0f };
        float name { 0.0f };
        float left { 0.0f };
        float right { 0.0f };
        float area { 0.0f };
    };
    const std::vector<width_case> cases {
        { 1280.0f, 150.0f, 173.0f, 1236.0f, 1063.0f },
        { 853.0f, 150.0f, 173.0f, 809.0f, 636.0f },
        { 600.0f, 72.0f, 95.0f, 556.0f, 461.0f },
        { 480.0f, 0.0f, 10.0f, 436.0f, 426.0f },
    };
    for (const width_case& sample : cases)
    {
        const gitman::function_bar_slots slots { gitman::layout_function_bar_slots(sample.width, true) };
        REQUIRE(slots.name_width == sample.name);
        REQUIRE(slots.buttons_left == sample.left);
        REQUIRE(slots.buttons_right == sample.right);
        REQUIRE(slots.buttons_right - slots.buttons_left == sample.area);
        REQUIRE(slots.more_x == sample.width - 38.0f);
    }

    // 카드 이름 경계는 640과 560이다.
    REQUIRE(gitman::layout_function_bar_slots(640.0f, true).name_width == 150.0f);
    REQUIRE(gitman::layout_function_bar_slots(639.0f, true).name_width == 72.0f);
    REQUIRE(gitman::layout_function_bar_slots(560.0f, true).name_width == 72.0f);
    REQUIRE(gitman::layout_function_bar_slots(559.0f, true).name_width == 0.0f);
    // 선택이 없으면 이름 칸이 없고 힌트가 왼쪽 여백에서 시작한다.
    REQUIRE(gitman::layout_function_bar_slots(1280.0f, false).name_width == 0.0f);
    REQUIRE(gitman::layout_function_bar_slots(1280.0f, false).buttons_left == 10.0f);

    // tree도 같은 자리다.
    for (const width_case& sample : cases)
    {
        function_card_fixture fixture { make_function_card_projects(2), bar_buttons(), sample.width, 720.0f };
        select(fixture, u8"p00");
        const auto tree { fixture.tree() };
        REQUIRE(bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"p00", u8"b")).x == sample.left);
        REQUIRE(bounds_of(*tree, ui::kind_function_bar_more).x == sample.width - 38.0f);
        REQUIRE(bounds_of(*tree, ui::kind_function_bar_more).width == 28.0f);
        REQUIRE(bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"p00", u8"b")).height == 28.0f);
    }
}

TEST_CASE("Bar button widths come from the longest state text and do not change with the state", "[function_buttons][t10][ui][tree]")
{
    std::vector<gitman::function_button> buttons { bar_buttons() };
    buttons[1].label = u8"abc";
    // 60자 라벨은 선호 폭이 160을 넘는다.
    buttons[2].label = std::u8string(60, u8'x');
    function_card_fixture fixture { make_function_card_projects(2), std::move(buttons) };
    select(fixture, u8"p00");
    const luil::text_measurer measurer { byte_measurer(2.0f) };
    const std::u8string b_owner { bar_owner(u8"p00", u8"b") };
    const std::u8string c_owner { bar_owner(u8"p00", u8"c") };

    const float b_width { expected_width(u8"abc · 실행 중", 2.0f) };
    REQUIRE(b_width > 64.0f);
    REQUIRE(b_width < 160.0f);

    const auto idle { measured_tree(fixture, measurer) };
    REQUIRE(bounds_of(*idle, ui::kind_function_bar_button, b_owner).width == b_width);
    // 긴 라벨은 160에서 멈추고 말줄임에 맡긴다. 접근 이름은 글 전체다.
    REQUIRE(bounds_of(*idle, ui::kind_function_bar_button, c_owner).width == 160.0f);
    REQUIRE(find(*idle, ui::kind_function_bar_button, b_owner)->accessibility().name == u8"abc");
    const float c_x { bounds_of(*idle, ui::kind_function_bar_button, c_owner).x };

    // 실행 중이면 글이 `abc · 실행 중`이 되지만 폭과 옆 버튼 자리는 그대로다.
    fixture.run(u8"p00", u8"b");
    fixture.started(fixture.functions.requests.back(), std::chrono::steady_clock::now());
    const auto running { measured_tree(fixture, measurer) };
    REQUIRE(find(*running, ui::kind_function_bar_button, b_owner)->accessibility().name == u8"abc · 실행 중");
    REQUIRE(bounds_of(*running, ui::kind_function_bar_button, b_owner).width == b_width);
    REQUIRE(bounds_of(*running, ui::kind_function_bar_button, c_owner).x == c_x);

    // 실패하면 `abc · 실패`와 점이다. 폭은 그대로다.
    fixture.completed(fixture.functions.requests.back(), gitman::function_run_outcome::exited, 1);
    const auto failed { measured_tree(fixture, measurer) };
    REQUIRE(find(*failed, ui::kind_function_bar_button, b_owner)->accessibility().name == u8"abc · 실패");
    REQUIRE(bounds_of(*failed, ui::kind_function_bar_button, b_owner).width == b_width);
    REQUIRE(bounds_of(*failed, ui::kind_function_bar_button, c_owner).x == c_x);
    REQUIRE(find(*failed, ui::kind_function_bar_marker, b_owner) != nullptr);

    // 글이 짧아도 64보다 좁지 않다.
    const auto narrow { measured_tree(fixture, byte_measurer(0.0f)) };
    REQUIRE(bounds_of(*narrow, ui::kind_function_bar_button, b_owner).width == 64.0f);
}

TEST_CASE("Bar buttons fold from the right into the more button", "[function_buttons][t10][ui][tree]")
{
    struct fold_case
    {
        float width { 0.0f };
        float height { 0.0f };
        // 버튼 64 + 간격 6이 영역(1063, 426)에 드는 수다.
        std::size_t visible { 0 };
    };
    const std::vector<fold_case> cases {
        { 1280.0f, 720.0f, 15 },
        { 480.0f, 320.0f, 6 },
    };
    for (const fold_case& sample : cases)
    {
        function_card_fixture fixture { make_function_card_projects(1), many_bar_buttons(20), sample.width, sample.height };
        select(fixture, u8"p00");
        const auto tree { measured_tree(fixture, byte_measurer(0.0f)) };
        const std::vector<luil::ui_element_id> ids { tree->ids_of_kind(ui::kind_function_bar_button) };
        REQUIRE(ids.size() == 20u);
        for (std::size_t index = 0; index < ids.size(); ++index)
        {
            // 그리기 순서가 문서 순서다. 앞쪽이 보이고 뒤쪽(오른쪽)부터 접힌다.
            REQUIRE(ids[index].owner == bar_owner(u8"p00", many_bar_buttons(20)[index].id));
            REQUIRE(tree->visibly_contains(ids[index]) == (index < sample.visible));
        }
        const luil::rect_f last { bounds_of(*tree, ui::kind_function_bar_button, ids[sample.visible - 1].owner) };
        REQUIRE(last.x + last.width <= sample.width - 44.0f);
        REQUIRE(tree->visibly_contains(id_of(ui::kind_function_bar_more)));
        // 접힌 기능은 줄의 `⋯`가 여는 기능 메뉴에 그대로 있다.
        REQUIRE(fixture.snapshot()->function_bar->total_functions == 20u);
    }
}

TEST_CASE("Bar tab stops are the visible bar buttons plus the more button", "[function_buttons][t10][ui][tree]")
{
    function_card_fixture fixture { make_function_card_projects(1), many_bar_buttons(20), 480.0f, 320.0f };
    select(fixture, u8"p00");
    const auto tree { measured_tree(fixture, byte_measurer(0.0f)) };
    REQUIRE(bar_tab_stops(*tree) == 6u + 1u);

    function_card_fixture two { make_function_card_projects(2), bar_buttons() };
    select(two, u8"p01");
    const auto small { two.tree() };
    REQUIRE(bar_tab_stops(*small) == 2u + 1u);

    // 줄은 tree에서 로그 헤더 앞이다 (2.2.6). 줄의 `⋯` 다음 Tab은 로그 필터다.
    interaction_harness harness { small };
    for (int step = 0; step < 100 && harness.snapshot().focused != id_of(ui::kind_function_bar_more); ++step)
        static_cast<void>(harness.process(luil::key_pressed_event { luil::key_code::tab }));
    REQUIRE(harness.snapshot().focused == id_of(ui::kind_function_bar_more));
    static_cast<void>(harness.process(luil::key_pressed_event { luil::key_code::tab }));
    REQUIRE(harness.snapshot().focused.kind == ui::kind_log_filter);
}

TEST_CASE("The bar shows a hint without a selection or without bar items", "[function_buttons][t10][ui][tree]")
{
    // `a`(card)는 p00·p01에, `b`(bar)는 p00에만 적용된다. p02에는 적용 기능이 없다.
    std::vector<gitman::function_button> buttons { make_function_card_button(u8"a", u8"가", u8"card"), make_function_card_button(u8"b", u8"나", u8"bar") };
    buttons[0].project_ids = std::vector<std::u8string> { u8"p00", u8"p01" };
    buttons[1].project_ids = std::vector<std::u8string> { u8"p00" };
    function_card_fixture fixture { make_function_card_projects(3), std::move(buttons) };

    SECTION("선택이 없으면 고르라는 힌트이고 줄의 `⋯`는 비활성이다")
    {
        const auto view { fixture.snapshot() };
        REQUIRE(view->function_bar.has_value());
        REQUIRE(view->function_bar->hint == u8"카드를 선택하면 기능이 나옵니다");
        const auto tree { fixture.tree() };
        REQUIRE(find(*tree, ui::kind_function_bar)->accessibility().name == u8"기능 줄, 카드를 선택하면 기능이 나옵니다");
        REQUIRE(find(*tree, ui::kind_function_bar)->accessibility().role == luil::access_role::tool_bar);
        REQUIRE(find(*tree, ui::kind_function_bar_more)->enabled() == false);
        REQUIRE(tree->ids_of_kind(ui::kind_function_bar_button).empty());
    }

    SECTION("선택한 카드에 bar 기능이 없으면 ⋯로 안내한다")
    {
        select(fixture, u8"p01");
        const auto view { fixture.snapshot() };
        REQUIRE(view->function_bar->hint == u8"이 카드에는 기능 줄 항목이 없습니다 — ⋯에서 모든 기능을 봅니다");
        REQUIRE(view->function_bar->card_name == u8"p01");
        const auto tree { fixture.tree() };
        REQUIRE(find(*tree, ui::kind_function_bar_more)->enabled());
        REQUIRE(find(*tree, ui::kind_function_bar)->accessibility().name == u8"기능 줄, p01, 이 카드에는 기능 줄 항목이 없습니다 — ⋯에서 모든 기능을 봅니다");
    }

    SECTION("적용 기능이 없는 카드는 그 사실을 알리고 줄의 ⋯는 아래 구역만 연다")
    {
        select(fixture, u8"p02");
        REQUIRE(fixture.snapshot()->function_bar->hint == u8"이 카드에 적용되는 기능이 없습니다");
        const auto tree { fixture.tree() };
        interaction_harness harness { tree };
        const auto actions { click(harness, bounds_of(*tree, ui::kind_function_bar_more)) };
        const auto* const intent { as_intent<gitman::open_function_menu_intent>(actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"p02");
        fixture.controller.handle(*intent);
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(function_item_count(menu) == 0u);
        REQUIRE(menu.items.size() == 2u);
        REQUIRE(menu.items[0].label == u8"기능 편집…");
        REQUIRE(menu.items[1].entry == gitman::context_menu_entry::reload_document);
    }

    SECTION("bar 기능이 있으면 힌트가 없고 이름 칸이 선다")
    {
        select(fixture, u8"p00");
        const auto view { fixture.snapshot() };
        REQUIRE(view->function_bar->hint.empty());
        REQUIRE(view->function_bar->functions.size() == 1u);
        REQUIRE(view->function_bar->functions[0].button_id == u8"b");
        // `card` 기능은 줄에 없다 (한 기능은 한 자리에만 버튼이다).
        REQUIRE(find(*fixture.tree(), ui::kind_function_bar_button, bar_owner(u8"p00", u8"a")) == nullptr);
    }
}

TEST_CASE("A shortened card name or hint shows in the bar tooltip", "[function_buttons][t10][ui][tree]")
{
    SECTION("600 폭의 이름 칸 72에 들지 않는 이름이다")
    {
        function_card_fixture fixture { make_function_card_projects(2), bar_buttons(), 600.0f, 720.0f };
        select(fixture, u8"p00");
        // `p00` 3바이트 × 30 = 90 > 72라 줄여 그린다.
        const auto wide { measured_tree(fixture, byte_measurer(30.0f)) };
        REQUIRE(find(*wide, ui::kind_function_bar)->tooltip() == u8"p00");
        // 칸에 들면 tooltip이 없다.
        const auto fits { measured_tree(fixture, byte_measurer(1.0f)) };
        REQUIRE(find(*fits, ui::kind_function_bar)->tooltip().empty());
    }

    SECTION("480 폭의 영역 426에 들지 않는 힌트다")
    {
        function_card_fixture fixture { make_function_card_projects(2), bar_buttons(), 480.0f, 720.0f };
        const std::u8string hint { u8"카드를 선택하면 기능이 나옵니다" };
        REQUIRE(fixture.snapshot()->function_bar->hint == hint);
        const auto narrow { measured_tree(fixture, byte_measurer(426.0f / static_cast<float>(hint.size()) + 1.0f)) };
        REQUIRE(find(*narrow, ui::kind_function_bar)->tooltip() == hint);
        // 접근 이름은 tooltip과 상관없이 전체 글이다.
        REQUIRE(find(*narrow, ui::kind_function_bar)->accessibility().name == u8"기능 줄, " + hint);
        const auto fits { measured_tree(fixture, byte_measurer(1.0f)) };
        REQUIRE(find(*fits, ui::kind_function_bar)->tooltip().empty());
    }

    SECTION("둘 다 줄이면 이름과 힌트를 함께 보인다")
    {
        std::vector<gitman::function_button> buttons { make_function_card_button(u8"a", u8"가", u8"card"), make_function_card_button(u8"b", u8"나", u8"bar") };
        buttons[1].project_ids = std::vector<std::u8string> { u8"p00" };
        function_card_fixture fixture { make_function_card_projects(2), std::move(buttons), 600.0f, 720.0f };
        select(fixture, u8"p01");
        const std::u8string hint { fixture.snapshot()->function_bar->hint };
        REQUIRE(hint.empty() == false);
        const auto tree { measured_tree(fixture, byte_measurer(30.0f)) };
        REQUIRE(find(*tree, ui::kind_function_bar)->tooltip() == u8"p01: " + hint);
    }
}

TEST_CASE("The bar more button has its own access name beside the card more button", "[function_buttons][t10][ui][tree]")
{
    function_card_fixture fixture { make_function_card_projects(2), bar_buttons() };
    select(fixture, u8"p00");
    const auto tree { fixture.tree() };
    // 같은 메뉴를 열지만 한 tree에 함께 서므로 이름이 달라야 낭독과 UIA 이름 찾기가 둘을 가른다.
    REQUIRE(find(*tree, ui::kind_function_bar_more)->accessibility().name == u8"기능 더 보기 (기능 줄), p00, 4개");
    REQUIRE(find(*tree, ui::kind_card_function_more, u8"p00")->accessibility().name == u8"기능 더 보기, p00, 4개");
}

TEST_CASE("Clicking an empty spot of the bar keeps the selection", "[function_buttons][t10][ui][tree]")
{
    function_card_fixture fixture { make_function_card_projects(2), bar_buttons() };
    select(fixture, u8"p00");
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    const luil::rect_f bar { bounds_of(*tree, ui::kind_function_bar) };
    // 버튼 둘 뒤, `⋯` 앞의 빈 곳이다. 줄이 hit를 흡수해 root의 선택 해제(`select_card_intent {}`)로 새지 않는다.
    const auto actions { click(harness, { bar.x + 700.0f, bar.y + 10.0f, 4.0f, 4.0f }) };
    REQUIRE(actions.empty());
    REQUIRE(find(*tree, ui::kind_function_bar)->hit_opaque());
}

TEST_CASE("No bar without a bar function that applies to some card", "[function_buttons][t10][ui][tree]")
{
    SECTION("project_ids가 어느 카드에도 없으면 줄이 없다")
    {
        std::vector<gitman::function_button> buttons { bar_buttons() };
        buttons[1].project_ids = std::vector<std::u8string> { u8"ghost" };
        buttons[2].project_ids = std::vector<std::u8string> {};
        function_card_fixture fixture { make_function_card_projects(2), std::move(buttons) };
        select(fixture, u8"p00");
        REQUIRE(fixture.snapshot()->function_bar.has_value() == false);
        const auto tree { fixture.tree() };
        REQUIRE(find(*tree, ui::kind_function_bar) == nullptr);
        REQUIRE(bounds_of(*tree, ui::kind_log_pane).height == 160.0f);
    }

    SECTION("vcs 한정이 모든 카드의 vcs_hint와 어긋나면 줄이 없다")
    {
        std::vector<gitman::function_button> buttons { make_function_card_button(u8"b", u8"나", u8"bar") };
        buttons[0].vcs = gitman::function_vcs_scope::subversion;
        std::vector<gitman::project_definition> projects { make_function_card_projects(2) };
        for (gitman::project_definition& project : projects)
            project.hint = gitman::vcs_hint::git;
        function_card_fixture fixture { std::move(projects), std::move(buttons) };
        REQUIRE(fixture.snapshot()->function_bar.has_value() == false);
    }

    SECTION("vcs_hint가 auto이고 조회 전인 카드는 적용되는 것으로 센다")
    {
        std::vector<gitman::function_button> buttons { make_function_card_button(u8"b", u8"나", u8"bar") };
        buttons[0].vcs = gitman::function_vcs_scope::subversion;
        function_card_fixture fixture { make_function_card_projects(2), std::move(buttons) };
        select(fixture, u8"p00");
        const auto view { fixture.snapshot() };
        REQUIRE(view->function_bar.has_value());
        REQUIRE(view->function_bar->functions.size() == 1u);
        REQUIRE(view->function_bar->functions[0].enabled == false);
        REQUIRE(find(*fixture.tree(), ui::kind_function_bar_button, bar_owner(u8"p00", u8"b"))->enabled() == false);
    }

    SECTION("placement를 하나도 적지 않은 문서는 모두 menu라 줄이 없다 (D22-B)")
    {
        std::vector<gitman::function_button> buttons { make_function_card_button(u8"x", u8"엑스"), make_function_card_button(u8"y", u8"와이") };
        function_card_fixture fixture { make_function_card_projects(2), std::move(buttons) };
        select(fixture, u8"p00");
        REQUIRE(fixture.snapshot()->function_bar.has_value() == false);
        const auto tree { fixture.tree() };
        REQUIRE(find(*tree, ui::kind_function_bar) == nullptr);
        REQUIRE(bounds_of(*tree, ui::kind_log_pane).height == 160.0f);
        // 기능은 카드의 `⋯` 기능 메뉴에만 있다.
        REQUIRE(find(*tree, ui::kind_card_function_more, u8"p00") != nullptr);
    }
}

TEST_CASE("The bar grows with the system text size and takes it from the log body", "[function_buttons][t10][ui][tree][text-size]")
{
    function_card_fixture fixture { make_function_card_projects(2), bar_buttons() };
    select(fixture, u8"p00");
    const text_size_scope scope { 1.5f };
    REQUIRE(scope.applied());
    // 버튼 라벨(12)이 6 자라 줄이 42다. 로그 pane 160은 그대로라 그만큼 로그 쪽이 준다.
    const float bar_height { 36.0f + 12.0f * 0.5f };
    REQUIRE(gitman::layout_function_bar_height() == bar_height);
    const auto tree { fixture.tree() };
    REQUIRE(bounds_of(*tree, ui::kind_function_bar).height == bar_height);
    REQUIRE(bounds_of(*tree, ui::kind_log_pane).height == 160.0f - bar_height);
    // 버튼은 줄의 slot 높이로 자란다 (위아래 4).
    REQUIRE(bounds_of(*tree, ui::kind_function_bar_button, bar_owner(u8"p00", u8"b")).height == bar_height - 8.0f);
    // 로그 본문은 헤더(26 + 11.5 × 0.5)까지 빼고 남은 높이다.
    const float body { 160.0f - bar_height - gitman::layout_log_header_height() };
    REQUIRE(bounds_of(*tree, ui::kind_scrollbar, ui::scrollbar_owner_log).height == body - 12.0f);
}
