#include "presentation/ui/ui_element.h"

#include "presentation/ui/button_element.h"
#include "presentation/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

namespace {
    // 기반 클래스 계약만 검증하는 최소 element다. 그리기는 하지 않는다.
    class test_panel final : public gitman::ui::ui_element
    {
    public:
        using ui_element::ui_element;

        void add(std::unique_ptr<ui_element> child)
        {
            add_child(std::move(child));
        }

        void arrange(const gitman::ui::arrange_context& context) override
        {
            set_bounds(context.slot);
        }

        void draw(gitman::ui::draw_context&, const gitman::ui::interaction_snapshot&) const override
        {}
    };

    gitman::ui::ui_action noop_action()
    {
        return [](const gitman::ui::ui_action_context&) -> std::vector<gitman::ui::input_action> { return {}; };
    }

    gitman::ui::ui_element_id card_id(const gitman::ui::ui_element_kind kind, const std::u8string_view value)
    {
        return { kind, gitman::project_id { std::u8string { value } } };
    }
} // namespace

TEST_CASE("An element is interactive once it has an action, tooltip, or drag role", "[ui][element]")
{
    test_panel panel { gitman::ui::ui_element_id { gitman::ui::ui_element_kind::card_list } };
    REQUIRE(panel.interactive() == false);

    SECTION("액션")
    {
        panel.set_action(gitman::ui::ui_trigger::left_click, noop_action());
        REQUIRE(panel.interactive());
        REQUIRE(panel.action(gitman::ui::ui_trigger::left_click) != nullptr);
        REQUIRE(panel.action(gitman::ui::ui_trigger::right_click) == nullptr);

        panel.clear_action(gitman::ui::ui_trigger::left_click);
        REQUIRE(panel.interactive() == false);
    }

    SECTION("tooltip")
    {
        panel.set_tooltip(u8"설명");
        REQUIRE(panel.interactive());
        REQUIRE(panel.tooltip() == u8"설명");
    }

    SECTION("drag와 drop은 재설정할 수 있다")
    {
        panel.set_drag_source(gitman::ui::drag_source { [](const gitman::ui::ui_action_context&) { return gitman::ui::drag_payload {}; } });
        REQUIRE(panel.interactive());
        REQUIRE(panel.drag() != nullptr);
        panel.set_drag_source(std::nullopt);
        REQUIRE(panel.drag() == nullptr);
        REQUIRE(panel.interactive() == false);

        panel.set_drop_target(gitman::ui::drop_target {});
        REQUIRE(panel.drop() != nullptr);
        panel.set_drop_target(std::nullopt);
        REQUIRE(panel.drop() == nullptr);
    }
}

TEST_CASE("Hit testing prefers the topmost interactive child", "[ui][element]")
{
    auto root { std::make_unique<test_panel>(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::root }) };
    root->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    root->set_action(gitman::ui::ui_trigger::left_click, noop_action());

    auto below { std::make_unique<test_panel>(card_id(gitman::ui::ui_element_kind::card_body, u8"below")) };
    below->arrange({ { 10.0f, 10.0f, 40.0f, 40.0f }, 1.0f });
    below->set_action(gitman::ui::ui_trigger::left_click, noop_action());

    auto above { std::make_unique<test_panel>(card_id(gitman::ui::ui_element_kind::card_refresh, u8"above")) };
    above->arrange({ { 20.0f, 20.0f, 40.0f, 40.0f }, 1.0f });
    above->set_action(gitman::ui::ui_trigger::left_click, noop_action());

    auto hidden { std::make_unique<test_panel>(card_id(gitman::ui::ui_element_kind::card_update, u8"hidden")) };
    hidden->arrange({ { 0.0f, 0.0f, 100.0f, 100.0f }, 1.0f });
    hidden->set_action(gitman::ui::ui_trigger::left_click, noop_action());
    hidden->set_visible(false);

    const gitman::ui::ui_element* const below_raw { below.get() };
    const gitman::ui::ui_element* const above_raw { above.get() };
    root->add(std::move(below));
    root->add(std::move(above));
    root->add(std::move(hidden));
    const gitman::ui::ui_tree tree { std::move(root) };

    // 겹치는 곳은 나중에 추가된(위에 그려진) 자식이 이긴다.
    REQUIRE(tree.hit_test(30.0f, 30.0f) == above_raw);
    REQUIRE(tree.hit_test(12.0f, 12.0f) == below_raw);
    // 보이지 않는 element는 전체를 덮어도 잡히지 않는다.
    REQUIRE(tree.hit_test(90.0f, 90.0f) == &tree.root());
    // tree 밖은 아무것도 아니다.
    REQUIRE(tree.hit_test(150.0f, 150.0f) == nullptr);
}

TEST_CASE("The tree finds elements by identity and enumerates kinds in draw order", "[ui][element]")
{
    auto root { std::make_unique<test_panel>(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::root }) };
    auto first { std::make_unique<test_panel>(card_id(gitman::ui::ui_element_kind::card_body, u8"one")) };
    auto second { std::make_unique<test_panel>(card_id(gitman::ui::ui_element_kind::card_body, u8"two")) };
    root->add(std::move(first));
    root->add(std::move(second));
    const gitman::ui::ui_tree tree { std::move(root) };

    REQUIRE(tree.find(card_id(gitman::ui::ui_element_kind::card_body, u8"two")) != nullptr);
    REQUIRE(tree.find(card_id(gitman::ui::ui_element_kind::card_body, u8"three")) == nullptr);

    const auto cards { tree.ids_of_kind(gitman::ui::ui_element_kind::card_body) };
    REQUIRE(cards.size() == 2u);
    REQUIRE(cards[0].owner.value == u8"one");
    REQUIRE(cards[1].owner.value == u8"two");
}

TEST_CASE("A disabled button keeps its tooltip target but blocks nothing else at the element level", "[ui][element]")
{
    gitman::ui::button_element button { card_id(gitman::ui::ui_element_kind::card_update, u8"card"), gitman::ui::button_config {} };
    button.set_tooltip(u8"단계 7에서 활성화");
    button.set_enabled(false);
    button.arrange({ { 0.0f, 0.0f, 32.0f, 32.0f }, 1.0f });

    // 비활성이어도 hit는 되어야 tooltip을 보여 줄 수 있다. 액션 차단은
    // interaction controller의 몫이다.
    REQUIRE(button.hit_test(10.0f, 10.0f) == &button);
    REQUIRE(button.enabled() == false);
}
