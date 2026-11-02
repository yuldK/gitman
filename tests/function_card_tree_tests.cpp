#include "helpers/function_card_fixture.h"
#include "helpers/ui_test_support.h"

#include "presentation/function_presentation.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"

#include "luil/ui/font_scale.h"
#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 결정안의 카드 부분(function-buttons-design 2.3)을 tree에서 본다 (6.1 T9 `ui_tree_build_tests` 항목). 카드 view는
// logic이 지은 snapshot이라 판정(4.8)과 element 배치가 한 경로를 지난다.
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

    // 예시 문서다: `card` 셋(빌드, 테스트, 열기), `bar` 하나(린트), `menu` 하나(정리, 배치 생략 → menu).
    std::vector<gitman::function_button> sample_buttons()
    {
        return {
            make_function_card_button(u8"build", u8"빌드", u8"card"),
            make_function_card_button(u8"lint", u8"린트", u8"bar"),
            make_function_card_button(u8"test", u8"테스트", u8"card"),
            make_function_card_button(u8"clean", u8"정리"),
            make_function_card_button(u8"open", u8"열기", u8"card"),
        };
    }

    // 카드 왼쪽 가장자리 기준 x다 (논리 px, 배율 1).
    float relative_x(const luil::ui_tree& tree, const luil::ui_element_kind kind, const std::u8string_view owner, const luil::rect_f& card)
    {
        return bounds_of(tree, kind, owner).x - card.x;
    }

    // 카드 하나의 Tab 정지점 수다. 카드 body는 자리가 아니므로 오른쪽 열의 버튼만 센다.
    std::size_t card_tab_stops(const luil::ui_tree& tree, const std::u8string_view card)
    {
        std::size_t count { 0 };
        for (const luil::ui_element_kind kind : { ui::kind_card_function, ui::kind_card_function_more, ui::kind_card_refresh, ui::kind_card_update, ui::kind_card_switch })
        {
            for (const luil::ui_element_id& id : tree.ids_of_kind(kind))
            {
                const std::u8string_view owner { kind == ui::kind_card_function ? ui::card_of_function_owner(id.owner) : std::u8string_view { id.owner } };
                if (owner != card)
                    continue;
                const luil::ui_element* const element { tree.find(id) };
                if (element != nullptr && element->tab_stop() && element->visible())
                    ++count;
            }
        }
        return count;
    }
}

TEST_CASE("Card function buttons follow the 2.3 position table", "[function_buttons][t9][ui][tree]")
{
    struct width_case
    {
        float window_width { 0.0f };
        float window_height { 0.0f };
        float card_width { 0.0f };
    };
    // 스크롤 막대가 보이도록 카드를 넉넉히 두면 카드 폭은 `W - 20 - 12`다 (2.1).
    const std::vector<width_case> cases {
        { 1280.0f, 720.0f, 1248.0f },
        { 480.0f, 320.0f, 448.0f },
    };

    for (const width_case& sample : cases)
    {
        function_card_fixture fixture { make_function_card_projects(12), sample_buttons(), sample.window_width, sample.window_height };
        const auto tree { fixture.tree() };
        const luil::rect_f card { bounds_of(*tree, ui::kind_card_body, u8"p00") };
        REQUIRE(card.width == sample.card_width);
        const float cw { card.width };

        // 위치표 (2.3): 고정 1·2·3번 `cw-248/-214/-180`, `⋯` `cw-146`, 내장 `cw-106/-72/-38`.
        // 1280이면 1000·1034·1068·1102·1142·1176·1210, 480이면 200·234·268·302·342·376·410이다.
        REQUIRE(relative_x(*tree, ui::kind_card_function, u8"p00:build", card) == cw - 248.0f);
        REQUIRE(relative_x(*tree, ui::kind_card_function, u8"p00:test", card) == cw - 214.0f);
        REQUIRE(relative_x(*tree, ui::kind_card_function, u8"p00:open", card) == cw - 180.0f);
        REQUIRE(relative_x(*tree, ui::kind_card_function_more, u8"p00", card) == cw - 146.0f);
        REQUIRE(relative_x(*tree, ui::kind_card_refresh, u8"p00", card) == cw - 106.0f);
        REQUIRE(relative_x(*tree, ui::kind_card_update, u8"p00", card) == cw - 72.0f);
        REQUIRE(relative_x(*tree, ui::kind_card_switch, u8"p00", card) == cw - 38.0f);
        if (sample.window_width == 1280.0f)
        {
            REQUIRE(relative_x(*tree, ui::kind_card_function, u8"p00:build", card) == 1000.0f);
            REQUIRE(relative_x(*tree, ui::kind_card_function_more, u8"p00", card) == 1102.0f);
            REQUIRE(relative_x(*tree, ui::kind_card_switch, u8"p00", card) == 1210.0f);
        }
        else
        {
            REQUIRE(relative_x(*tree, ui::kind_card_function, u8"p00:build", card) == 200.0f);
            REQUIRE(relative_x(*tree, ui::kind_card_function_more, u8"p00", card) == 302.0f);
            REQUIRE(relative_x(*tree, ui::kind_card_switch, u8"p00", card) == 410.0f);
        }

        // 버튼은 28×28이고 세로 가운데다.
        const luil::rect_f more { bounds_of(*tree, ui::kind_card_function_more, u8"p00") };
        REQUIRE(more.width == 28.0f);
        REQUIRE(more.height == 28.0f);
        REQUIRE(more.y == card.y + (card.height - 28.0f) / 2.0f);
    }
}

TEST_CASE("Card text widths follow the 2.3 table and the reserve folds only below 160", "[function_buttons][t9][ui][tree]")
{
    // 2.3 표의 글 영역 폭이다: 적용 0 / 고정 0(`⋯`만) / 고정 1 / 2 / 3.
    struct width_row
    {
        float card_width { 0.0f };
        std::vector<float> text_widths {};
    };
    const std::vector<width_row> rows {
        { 1248.0f, { 1108.0f, 1068.0f, 1034.0f, 1000.0f, 966.0f } },
        { 821.0f, { 681.0f, 641.0f, 607.0f, 573.0f, 539.0f } },
        { 448.0f, { 308.0f, 268.0f, 234.0f, 200.0f, 166.0f } },
    };

    for (const width_row& row : rows)
    {
        REQUIRE(gitman::layout_card_function_slots(row.card_width, 0, false).text_right - gitman::layout_card_text_left == row.text_widths[0]);
        for (std::size_t count = 0; count <= 3; ++count)
        {
            const gitman::card_function_slots slots { gitman::layout_card_function_slots(row.card_width, count, true) };
            REQUIRE(slots.visible_count == count);
            REQUIRE(slots.text_right - gitman::layout_card_text_left == row.text_widths[count + 1]);
            REQUIRE(slots.more_x == row.card_width - 146.0f);
            REQUIRE(slots.separator_x == row.card_width - 118.0f);
        }
    }

    // 접힘은 안전장치다: 접지 않고 둘 수 있는 고정 수는 `floor((cw - 340) / 34)`이고 상한 3은 카드 폭 442부터
    // 접히지 않는다 (2.3 검산). 최소 창 480의 카드 폭은 448이다.
    REQUIRE(gitman::layout_card_function_slots(442.0f, 3, true).visible_count == 3u);
    REQUIRE(gitman::layout_card_function_slots(441.0f, 3, true).visible_count == 2u);
    REQUIRE(gitman::layout_card_function_slots(374.0f, 3, true).visible_count == 1u);
    REQUIRE(gitman::layout_card_function_slots(373.0f, 3, true).visible_count == 0u);
    // 접힌 뒤에도 글 영역은 예약 160 이상이다.
    REQUIRE(gitman::layout_card_function_slots(441.0f, 3, true).text_right - gitman::layout_card_text_left >= gitman::layout_card_function_text_reserve);
}

TEST_CASE("At 480 width every function stays reachable", "[function_buttons][t9][ui][tree]")
{
    // 카드당 상한 20개: `card` 셋 + 나머지 17개(`bar`·`menu`). 최소 폭에서도 고정 셋이 접히지 않고 `⋯`가 보이며,
    // 기능 메뉴가 20개 모두를 싣는다 (B6, 2.3.1).
    std::vector<gitman::function_button> buttons {
        make_function_card_button(u8"f00", u8"기능 0", u8"card"),
        make_function_card_button(u8"f01", u8"기능 1", u8"card"),
        make_function_card_button(u8"f02", u8"기능 2", u8"card"),
    };

    for (std::size_t index = 3; index < 20; ++index)
    {
        std::u8string id { u8"f" };
        id += static_cast<char8_t>(u8'0' + index / 10);
        id += static_cast<char8_t>(u8'0' + index % 10);
        buttons.push_back(make_function_card_button(id, u8"기능 " + id, index % 2 == 0 ? std::optional<std::u8string_view> { u8"bar" } : std::nullopt));
    }
    function_card_fixture fixture { make_function_card_projects(2), std::move(buttons), 480.0f, 320.0f };
    const auto tree { fixture.tree() };

    for (const std::u8string_view owner : { u8"p00:f00", u8"p00:f01", u8"p00:f02" })
    {
        const luil::ui_element* const button { find(*tree, ui::kind_card_function, owner) };
        REQUIRE(button != nullptr);
        REQUIRE(button->visible());
        REQUIRE(button->enabled());
    }
    const luil::ui_element* const more { find(*tree, ui::kind_card_function_more, u8"p00") };
    REQUIRE(more != nullptr);
    REQUIRE(more->visible());
    REQUIRE(more->enabled());

    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 10.0f });
    const gitman::context_menu_view menu { fixture.menu() };
    REQUIRE(function_item_count(menu) == 20u);
    for (const gitman::context_menu_item_view& item : menu.items)
    {
        if (item.entry == gitman::context_menu_entry::run_function)
            REQUIRE(item.enabled);
    }
}

TEST_CASE("Bar and menu functions stay off the card column and appear in the function menu", "[function_buttons][t9][ui][tree]")
{
    std::vector<gitman::function_button> buttons { sample_buttons() };
    // 4번째 `card`는 보일 때 `bar`다 (2.2.2 규칙 4). 카드 열에 없고 `Alt+N` 번호도 없다.
    buttons.push_back(make_function_card_button(u8"fourth", u8"넷째", u8"card"));
    function_card_fixture fixture { make_function_card_projects(2), std::move(buttons) };
    const auto tree { fixture.tree() };

    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:build") != nullptr);
    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:test") != nullptr);
    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:open") != nullptr);
    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:lint") == nullptr);
    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:clean") == nullptr);
    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:fourth") == nullptr);

    // 기능 메뉴는 배치와 상관없이 그 카드의 적용 기능 전체를 문서 순서로 싣는다. `card` 항목만 단축키 열이 있다.
    fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 10.0f });
    const gitman::context_menu_view menu { fixture.menu() };
    REQUIRE(function_item_count(menu) == 6u);
    const std::vector<std::pair<std::u8string_view, std::u8string_view>> expected {
        { u8"build", u8"Alt+1" },
        { u8"lint", u8"" },
        { u8"test", u8"Alt+2" },
        { u8"clean", u8"" },
        { u8"open", u8"Alt+3" },
        { u8"fourth", u8"" },
    };

    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        REQUIRE(menu.items[index].entry == gitman::context_menu_entry::run_function);
        REQUIRE(menu.items[index].argument == expected[index].first);
        REQUIRE(menu.items[index].shortcut_text == expected[index].second);
    }
}

TEST_CASE("A card without applicable functions keeps the previous column", "[function_buttons][t9][ui][tree]")
{
    // 적용 범위가 먼저다 (2.3.1 규칙 2·3). 기능이 적용되지 않는 카드에는 `⋯`도 구분선도 없고 내장 셋의 x는 같다.
    gitman::function_button only_p00 { make_function_card_button(u8"build", u8"빌드", u8"card") };
    only_p00.project_ids = std::vector<std::u8string> { u8"p00" };
    function_card_fixture fixture { make_function_card_projects(12), { only_p00 } };
    const auto tree { fixture.tree() };

    REQUIRE(find(*tree, ui::kind_card_function_more, u8"p01") == nullptr);
    REQUIRE(find(*tree, ui::kind_card_function, u8"p01:build") == nullptr);
    REQUIRE(find(*tree, ui::kind_card_function_more, u8"p00") != nullptr);
    const luil::rect_f card_0 { bounds_of(*tree, ui::kind_card_body, u8"p00") };
    const luil::rect_f card_1 { bounds_of(*tree, ui::kind_card_body, u8"p01") };
    REQUIRE(relative_x(*tree, ui::kind_card_refresh, u8"p00", card_0) == relative_x(*tree, ui::kind_card_refresh, u8"p01", card_1));
    REQUIRE(fixture.card(u8"p01").functions.empty());
}

TEST_CASE("Card function owners join the card and button ids at the last colon", "[function_buttons][t9][ui][tree]")
{
    // 카드마다 같은 버튼 id가 되풀이되므로 owner는 카드 id + `:` + 버튼 id다. 버튼 id에는 `:`가 없고 project id에는
    // 있을 수 있어 되돌릴 때는 마지막 `:`에서 나눈다.
    REQUIRE(ui::card_function_owner(u8"team:web", u8"build") == u8"team:web:build");
    REQUIRE(ui::card_of_function_owner(u8"team:web:build") == u8"team:web");
    REQUIRE(ui::card_of_function_owner(u8"alpha") == u8"alpha");

    std::vector<gitman::project_definition> projects { make_function_card_project(u8"team:web"), make_function_card_project(u8"team") };
    function_card_fixture fixture { std::move(projects), sample_buttons() };
    const auto tree { fixture.tree() };
    // 두 카드가 같은 버튼을 가져도 id가 겹치지 않는다 (`make_tree`가 중복 id 없음을 확인한다).
    REQUIRE(find(*tree, ui::kind_card_function, u8"team:web:build") != nullptr);
    REQUIRE(find(*tree, ui::kind_card_function, u8"team:build") != nullptr);
    REQUIRE(find(*tree, ui::kind_card_function_more, u8"team:web") != nullptr);
    REQUIRE(find(*tree, ui::kind_card_function_more, u8"team") != nullptr);

    // 점 표식도 겹치지 않는다. 카드 `team`의 버튼 `build` 점(owner `team:build`)과 카드 `team:build`의 `⋯` 점은
    // owner가 같아 kind를 나눴다.
    function_card_fixture colliding { { make_function_card_project(u8"team"), make_function_card_project(u8"team:build") }, sample_buttons() };
    colliding.run(u8"team", u8"build");
    colliding.run(u8"team:build", u8"lint");
    REQUIRE(colliding.functions.requests.size() == 2u);
    colliding.completed(colliding.functions.requests[0], gitman::function_run_outcome::exited, 1);
    colliding.completed(colliding.functions.requests[1], gitman::function_run_outcome::exited, 2);
    const auto marked { colliding.tree() };
    REQUIRE(find(*marked, ui::kind_card_function_marker, u8"team:build") != nullptr);
    REQUIRE(find(*marked, ui::kind_card_function_more_marker, u8"team:build") != nullptr);
    REQUIRE(find(*marked, ui::kind_card_function_more_marker, u8"team:build")->visible());
}

TEST_CASE("Each card has four tab stops with functions and three without", "[function_buttons][t9][ui][tree]")
{
    // `card` 버튼은 Tab 자리가 아니다 (2.3.4). 정지점은 `⋯`와 내장 셋이다.
    gitman::function_button only_p00 { make_function_card_button(u8"build", u8"빌드", u8"card") };
    only_p00.project_ids = std::vector<std::u8string> { u8"p00" };
    function_card_fixture fixture { make_function_card_projects(3), { only_p00, make_function_card_button(u8"lint", u8"린트", u8"card") } };
    fixture.query(u8"p00");
    fixture.query(u8"p01");
    fixture.query(u8"p02");
    const auto tree { fixture.tree() };

    REQUIRE(card_tab_stops(*tree, u8"p00") == 4u);
    REQUIRE(find(*tree, ui::kind_card_function, u8"p00:build")->tab_stop() == false);

    // Tab 순서는 `⋯`, 새로 고침, 업데이트, 전환이다 (2.3.4). 시각 순서와 같다.
    interaction_harness harness { tree };
    std::vector<luil::ui_element_kind> order {};
    for (int step = 0; step < 80 && order.size() < 4; ++step)
    {
        static_cast<void>(harness.process(luil::key_pressed_event { luil::key_code::tab }));
        const luil::ui_element_id& focused { harness.snapshot().focused };
        if (focused.owner == u8"p00" && std::ranges::find(order, focused.kind) == order.end())
            order.push_back(focused.kind);
    }
    REQUIRE(order == std::vector<luil::ui_element_kind> { ui::kind_card_function_more, ui::kind_card_refresh, ui::kind_card_update, ui::kind_card_switch });

    gitman::function_button only_p00_again { make_function_card_button(u8"build", u8"빌드", u8"card") };
    only_p00_again.project_ids = std::vector<std::u8string> { u8"p00" };
    function_card_fixture without { make_function_card_projects(3), { only_p00_again } };
    // update·switch는 준비된 카드에서만 누를 수 있어 정지점이다. 세 카드 모두 조회를 끝낸다.
    without.query(u8"p00");
    without.query(u8"p01");
    without.query(u8"p02");
    const auto plain { without.tree() };
    REQUIRE(card_tab_stops(*plain, u8"p00") == 4u);
    REQUIRE(card_tab_stops(*plain, u8"p01") == 3u);
}

TEST_CASE("Card height stays layout_card_height with function buttons", "[function_buttons][t9][ui][tree]")
{
    // 기능 열은 카드 높이를 바꾸지 않는다. 100%에서 64이고(`text_size_metrics_tests.cpp`), 글 크기를 키우면
    // 그 산식대로 자라며 버튼은 가운데에 남는다.
    {
        function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
        const auto tree { fixture.tree() };
        const luil::rect_f card { bounds_of(*tree, ui::kind_card_body, u8"p00") };
        REQUIRE(card.height == gitman::layout_card_height());
        REQUIRE(card.height == 64.0f);
    }
    {
        const text_size_scope scope { 1.5f };
        REQUIRE(scope.applied());
        function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
        const auto tree { fixture.tree() };
        const luil::rect_f card { bounds_of(*tree, ui::kind_card_body, u8"p00") };
        REQUIRE(card.height == gitman::layout_card_height());
        REQUIRE(card.height == 64.0f + (14.0f + 11.0f + 10.5f) * 0.5f);
        const luil::rect_f build { bounds_of(*tree, ui::kind_card_function, u8"p00:build") };
        REQUIRE(build.height == 28.0f);
        REQUIRE(build.y == card.y + (card.height - 28.0f) / 2.0f);
    }
}

TEST_CASE("Running and failed functions show on the card column", "[function_buttons][t9][ui][tree]")
{
    function_card_fixture fixture { make_function_card_projects(3), sample_buttons() };
    fixture.query(u8"p00");
    fixture.query(u8"p01");
    const auto start { std::chrono::steady_clock::now() };

    // p00: 빌드(card)가 실행 중이다. p01: 테스트(card)가 종료 코드 1로, 린트(bar)가 시간 초과로 실패했다.
    fixture.run(u8"p00", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.started(fixture.functions.requests[0], start);
    fixture.run(u8"p01", u8"test");
    fixture.run(u8"p01", u8"lint");
    REQUIRE(fixture.functions.requests.size() == 2u);
    fixture.completed(fixture.functions.requests[1], gitman::function_run_outcome::exited, 1);
    fixture.run(u8"p01", u8"lint");
    REQUIRE(fixture.functions.requests.size() == 3u);
    fixture.completed(fixture.functions.requests[2], gitman::function_run_outcome::timed_out);

    const gitman::card_view_model running { fixture.card(u8"p00") };
    REQUIRE(running.function_running);
    REQUIRE(running.busy == false);
    REQUIRE(running.running_function_label == u8"빌드");
    REQUIRE(running.running_function_started_at == std::optional<std::chrono::steady_clock::time_point> { start });

    const auto tree { fixture.tree() };
    interaction_harness harness { tree };

    // 실행 중인 `card` 버튼은 제자리에서 중지 버튼이다. 이름에 상태가 있고 경과 시간은 없다.
    const luil::ui_element* const build { find(*tree, ui::kind_card_function, u8"p00:build") };
    REQUIRE(build->enabled());
    REQUIRE(build->access_name() == u8"빌드, p00, 실행 중");
    REQUIRE(build->tooltip() == u8"빌드 — 실행 중입니다. 누르면 중지합니다");
    const auto stop_actions { click(harness, build->bounds()) };
    const auto* const stop { as_intent<gitman::stop_function_intent>(stop_actions) };
    REQUIRE(stop != nullptr);
    REQUIRE(stop->id.value == u8"p00");
    REQUIRE(stop->button_id == u8"build");

    // 같은 카드의 다른 출력 수집 버튼은 비활성이고 사유는 tooltip에 있다 (4.7).
    const luil::ui_element* const test { find(*tree, ui::kind_card_function, u8"p00:test") };
    REQUIRE(test->enabled() == false);
    REQUIRE(test->tooltip() == u8"테스트 — 이 카드에서 다른 기능이 실행 중입니다");
    REQUIRE(click(harness, test->bounds()).empty());
    // update·switch는 그 기능 이름으로 막힌 사유를 보인다 (4.7 표).
    REQUIRE(find(*tree, ui::kind_card_update, u8"p00")->enabled() == false);
    REQUIRE(find(*tree, ui::kind_card_update, u8"p00")->tooltip() == u8"기능 \"빌드\"가 실행 중입니다");
    REQUIRE(find(*tree, ui::kind_card_switch, u8"p00")->tooltip() == u8"기능 \"빌드\"가 실행 중입니다");
    // 실행 중인 `card` 버튼에는 점이 없다. `⋯`의 점은 고정하지 않은 기능의 몫이라 p00에는 없다.
    REQUIRE(find(*tree, ui::kind_card_function_marker, u8"p00:build") == nullptr);
    const luil::ui_element* const p00_more_marker { find(*tree, ui::kind_card_function_more_marker, u8"p00") };
    REQUIRE((p00_more_marker == nullptr || p00_more_marker->visible() == false));

    // 실패한 `card` 버튼에는 오른쪽 위 6px 점이 버튼 뒤(위)에 붙는다. 점은 hit 대상이 아니다.
    const luil::ui_element* const failed { find(*tree, ui::kind_card_function, u8"p01:test") };
    REQUIRE(failed->access_name() == u8"테스트, p01, 마지막 실패: 종료 코드 1");
    const luil::ui_element* const marker { find(*tree, ui::kind_card_function_marker, u8"p01:test") };
    REQUIRE(marker != nullptr);
    REQUIRE(marker->visible());
    const luil::rect_f box { failed->bounds() };
    REQUIRE(marker->bounds().width == 6.0f);
    REQUIRE(marker->bounds().x == box.x + box.width - 2.0f - 6.0f);
    REQUIRE(marker->bounds().y == box.y + 2.0f);
    REQUIRE(tree->hit_test(marker->bounds().x + 1.0f, marker->bounds().y + 1.0f) == failed);
    // 고정하지 않은 기능(린트, `bar`)의 실패는 `⋯`의 점이다.
    const luil::ui_element* const more_marker { find(*tree, ui::kind_card_function_more_marker, u8"p01") };
    REQUIRE(more_marker != nullptr);
    REQUIRE(more_marker->visible());
    const luil::rect_f more { bounds_of(*tree, ui::kind_card_function_more, u8"p01") };
    REQUIRE(more_marker->bounds().x == more.x + more.width - 2.0f - 6.0f);

    // 실패가 없는 카드에는 점이 없다.
    REQUIRE(find(*tree, ui::kind_card_function_marker, u8"p02:build") == nullptr);
    REQUIRE(find(*tree, ui::kind_card_function_more_marker, u8"p02") == nullptr);
}

TEST_CASE("The more dot shows running and failed functions without a card button", "[function_buttons][t9][ui][tree]")
{
    SECTION("고정하지 않은 기능만 실행 중이면 `⋯`에 점이 있다 (accent)")
    {
        function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
        fixture.run(u8"p00", u8"lint");
        REQUIRE(fixture.functions.requests.size() == 1u);
        fixture.started(fixture.functions.requests[0], std::chrono::steady_clock::now());
        const gitman::card_view_model card { fixture.card(u8"p00") };
        REQUIRE(card.function_running);
        const auto tree { fixture.tree() };
        const luil::ui_element* const marker { find(*tree, ui::kind_card_function_more_marker, u8"p00") };
        REQUIRE(marker != nullptr);
        REQUIRE(marker->visible());
        // 색은 `function_more_marker_state`가 정한다. 카드 열에 있는 `card` 버튼(빌드·테스트·열기)은 보지 않는다.
        REQUIRE(gitman::function_more_marker_state(card.functions, { true, false, true, false, true }) == gitman::function_more_marker::running);
    }

    SECTION("실패와 실행 중이 섞이면 실패를 먼저 보인다")
    {
        gitman::card_function_view running {};
        running.running = true;
        gitman::card_function_view failed {};
        failed.failed = true;
        gitman::card_function_view idle {};
        REQUIRE(gitman::function_more_marker_state({ running, failed, idle }, { false, false, false }) == gitman::function_more_marker::failed);
        REQUIRE(gitman::function_more_marker_state({ failed, running }, { false, false }) == gitman::function_more_marker::failed);
        // 카드 열에 버튼이 있는 기능의 상태는 그 버튼이 보인다.
        REQUIRE(gitman::function_more_marker_state({ failed, running }, { true, false }) == gitman::function_more_marker::running);
        REQUIRE(gitman::function_more_marker_state({ failed, idle }, { true, false }) == gitman::function_more_marker::none);
    }

    SECTION("카드 폭 442 미만에서 접힌 `card` 기능은 `⋯`의 점으로 옮겨 간다")
    {
        // 창 470이면 카드 폭 438이라 고정 셋 중 둘만 놓인다 (2.3 검산). 접힌 셋째(열기)는 기능 메뉴에 그대로 있다.
        function_card_fixture fixture { make_function_card_projects(2), sample_buttons(), 470.0f, 320.0f };
        fixture.run(u8"p00", u8"open");
        REQUIRE(fixture.functions.requests.size() == 1u);
        fixture.started(fixture.functions.requests[0], std::chrono::steady_clock::now());
        const auto tree { fixture.tree() };
        const luil::rect_f card { bounds_of(*tree, ui::kind_card_body, u8"p00") };
        REQUIRE(card.width == 438.0f);

        REQUIRE(find(*tree, ui::kind_card_function, u8"p00:build")->visible());
        REQUIRE(find(*tree, ui::kind_card_function, u8"p00:test")->visible());
        REQUIRE(find(*tree, ui::kind_card_function, u8"p00:open")->visible() == false);
        REQUIRE(bounds_of(*tree, ui::kind_card_function, u8"p00:test").x - card.x == card.width - 180.0f);
        const luil::ui_element* const marker { find(*tree, ui::kind_card_function_more_marker, u8"p00") };
        REQUIRE(marker != nullptr);
        REQUIRE(marker->visible());
        const luil::rect_f more { bounds_of(*tree, ui::kind_card_function_more, u8"p00") };
        REQUIRE(marker->bounds().x == more.x + more.width - 2.0f - 6.0f);

        // 접히지 않은 카드(p01)에는 점이 없다.
        const luil::ui_element* const other { find(*tree, ui::kind_card_function_more_marker, u8"p01") };
        REQUIRE((other == nullptr || other->visible() == false));
    }
}

TEST_CASE("Card function buttons carry fixed access names and the card lists its function count", "[function_buttons][t9][ui][tree]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons() };
    const auto tree { fixture.tree() };

    // `라벨, 카드 이름[, 상태]`다 (2.3.4). 대기 중이면 상태가 없고 tooltip은 라벨과 단축키다.
    const luil::ui_element* const build { find(*tree, ui::kind_card_function, u8"p00:build") };
    REQUIRE(build->access_name() == u8"빌드, p00");
    REQUIRE(build->accessibility().role == luil::access_role::button);
    REQUIRE(build->accessibility().name == u8"빌드, p00");
    REQUIRE(build->tooltip() == u8"빌드 (Alt+1)");
    // `⋯`는 `기능 더 보기, 카드 이름, N개`다.
    const luil::ui_element* const more { find(*tree, ui::kind_card_function_more, u8"p00") };
    REQUIRE(more->accessibility().name == u8"기능 더 보기, p00, 5개");
    // 카드 항목의 이름 끝에 기능 수가 붙는다.
    const std::u8string card_name { find(*tree, ui::kind_card_body, u8"p00")->accessibility().name };
    REQUIRE(card_name.ends_with(u8"기능 5개"));
}

TEST_CASE("Disabled card function buttons still read their name", "[function_buttons][t9][ui][tree]")
{
    // 비활성 버튼도 접근 tree에 단추로 서서 이름이 읽힌다. 사유는 tooltip에 있다 (4.8).
    gitman::function_button runtime { make_function_card_button(u8"deploy", u8"배포", u8"card") };
    runtime.args = { u8"${branch}" };
    function_card_fixture fixture { make_function_card_projects(2), { runtime } };
    const auto tree { fixture.tree() };
    const luil::ui_element* const deploy { find(*tree, ui::kind_card_function, u8"p00:deploy") };
    REQUIRE(deploy->enabled() == false);
    REQUIRE(deploy->accessibility().name == u8"배포, p00");
    REQUIRE(deploy->tooltip() == u8"배포 — 저장소를 조회한 뒤에 쓸 수 있습니다");
}
