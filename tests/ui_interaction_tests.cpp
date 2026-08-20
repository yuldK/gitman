#include "presentation/ui/ui_interaction.h"

#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/button_element.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using namespace std::chrono_literals;

    std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    gitman::view_snapshot make_view(const std::size_t card_count, const gitman::view_empty_state empty_state = gitman::view_empty_state::none)
    {
        gitman::view_snapshot view {};
        view.window_width = 800.0f;
        view.window_height = 600.0f;
        view.scale = 1.0f;
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

    std::shared_ptr<const gitman::ui::ui_tree> make_tree(const std::size_t card_count, const gitman::view_empty_state empty_state = gitman::view_empty_state::none)
    {
        return gitman::ui::build_ui_tree(make_view(card_count, empty_state));
    }

    gitman::ui::rect_f bounds_of(const gitman::ui::ui_tree& tree, const gitman::ui::ui_element_kind kind, const std::u8string_view id = u8"")
    {
        const gitman::ui::ui_element* const element { tree.find({ kind, gitman::project_id { std::u8string { id } } }) };
        REQUIRE(element != nullptr);
        return element->bounds();
    }

    std::vector<gitman::ui::input_action> click(gitman::ui::interaction_controller& controller, const gitman::ui::rect_f& bounds, const int time_milliseconds = 0)
    {
        const float x { bounds.x + 1.0f };
        const float y { bounds.y + 1.0f };
        auto pressed { controller.process(gitman::ui::pointer_pressed_event { x, y, gitman::ui::pointer_button::left, at(time_milliseconds) }) };
        REQUIRE(pressed.empty());
        return controller.process(gitman::ui::pointer_released_event { x, y, gitman::ui::pointer_button::left, at(time_milliseconds) });
    }

    const gitman::logic_message* as_message(const std::vector<gitman::ui::input_action>& actions)
    {
        if (actions.size() != 1u)
            return nullptr;
        return std::get_if<gitman::logic_message>(&actions.front());
    }

    // 더블 클릭·drag 검증용의 최소 컨테이너다.
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
} // namespace

TEST_CASE("Clicks resolve to intents through the tree", "[ui][interaction]")
{
    const auto tree { make_tree(2) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    SECTION("refresh 버튼")
    {
        const auto actions { click(controller, bounds_of(*tree, gitman::ui::ui_element_kind::card_refresh, u8"card-1")) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::refresh_card_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-1");
    }

    SECTION("카드 body는 선택이다")
    {
        const auto actions { click(controller, bounds_of(*tree, gitman::ui::ui_element_kind::card_body, u8"card-0")) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::select_card_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.has_value());
        REQUIRE(intent->id->value == u8"card-0");
    }

    SECTION("빈 영역 클릭은 선택 해제다")
    {
        const auto actions { click(controller, gitman::ui::rect_f { 700.0f, 580.0f, 4.0f, 4.0f }) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::select_card_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.has_value() == false);
    }

    SECTION("전체 refresh")
    {
        const auto actions { click(controller, bounds_of(*tree, gitman::ui::ui_element_kind::toolbar_refresh_all)) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(*message));
    }
}

TEST_CASE("A press that leaves its target produces nothing", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    const gitman::ui::rect_f refresh { bounds_of(*tree, gitman::ui::ui_element_kind::card_refresh, u8"card-0") };
    const gitman::ui::rect_f body { bounds_of(*tree, gitman::ui::ui_element_kind::card_body, u8"card-0") };
    REQUIRE(controller.process(gitman::ui::pointer_pressed_event { refresh.x + 1.0f, refresh.y + 1.0f, gitman::ui::pointer_button::left, at(0) }).empty());
    REQUIRE(controller.process(gitman::ui::pointer_released_event { body.x + 1.0f, body.y + 1.0f, gitman::ui::pointer_button::left, at(10) }).empty());
}

TEST_CASE("Disabled stage seven buttons produce nothing but still show a tooltip target", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    const gitman::ui::rect_f update { bounds_of(*tree, gitman::ui::ui_element_kind::card_update, u8"card-0") };
    REQUIRE(click(controller, update).empty());
    REQUIRE(click(controller, bounds_of(*tree, gitman::ui::ui_element_kind::card_switch, u8"card-0")).empty());

    // 비활성 버튼도 hover 대상이 되어 tooltip을 예고한다. 강조는 렌더러가
    // enabled로 거른다.
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { update.x + 1.0f, update.y + 1.0f, at(50) }).empty());
    REQUIRE(controller.snapshot().hovered.kind == gitman::ui::ui_element_kind::card_update);
    REQUIRE(controller.snapshot().hover_started_at == at(50));
}

TEST_CASE("The open document button asks for a UI command", "[ui][interaction]")
{
    const auto tree { make_tree(0, gitman::view_empty_state::no_document) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    const auto actions { click(controller, bounds_of(*tree, gitman::ui::ui_element_kind::toolbar_open_document)) };
    REQUIRE(actions.size() == 1u);
    const auto* const command { std::get_if<gitman::ui::ui_command>(&actions.front()) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == gitman::ui::ui_command::show_open_document_dialog);
}

TEST_CASE("The generate document button asks for a UI command", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    const auto actions { click(controller, bounds_of(*tree, gitman::ui::ui_element_kind::toolbar_generate_document)) };
    REQUIRE(actions.size() == 1u);
    const auto* const command { std::get_if<gitman::ui::ui_command>(&actions.front()) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == gitman::ui::ui_command::show_generate_document_dialog);
}

TEST_CASE("Caption buttons return window commands", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    const auto actions { click(controller, bounds_of(*tree, gitman::ui::ui_element_kind::caption_close)) };
    REQUIRE(actions.size() == 1u);
    const auto* const command { std::get_if<gitman::ui::ui_command>(&actions.front()) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == gitman::ui::ui_command::window_close);
}

TEST_CASE("Wheel rotation becomes a scroll intent", "[ui][interaction]")
{
    gitman::ui::interaction_controller controller {};
    controller.set_tree(make_tree(3));

    const auto actions { controller.process(gitman::ui::mouse_wheel_event { 100.0f, 100.0f, -120.0f }) };
    const auto* const message { as_message(actions) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::scroll_intent>(message) };
    REQUIRE(intent != nullptr);
    // 아래로 굴리면 목록이 아래로 이동한다.
    REQUIRE(intent->delta == gitman::ui::input_wheel_scroll_step);
}

TEST_CASE("Hover and pressed states follow the pointer", "[ui][interaction]")
{
    const auto tree { make_tree(1) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    const gitman::ui::rect_f refresh { bounds_of(*tree, gitman::ui::ui_element_kind::card_refresh, u8"card-0") };
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { refresh.x + 1.0f, refresh.y + 1.0f, at(100) }).empty());
    REQUIRE(controller.snapshot().hovered.kind == gitman::ui::ui_element_kind::card_refresh);
    REQUIRE(controller.snapshot().hover_started_at == at(100));

    // 같은 element 안의 이동은 hover 시작 시각을 유지한다 (tooltip 지연 기준).
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { refresh.x + 5.0f, refresh.y + 5.0f, at(200) }).empty());
    REQUIRE(controller.snapshot().hover_started_at == at(100));

    // 누르면 pressed가 켜지고 떼면 꺼진다.
    REQUIRE(controller.process(gitman::ui::pointer_pressed_event { refresh.x + 1.0f, refresh.y + 1.0f, gitman::ui::pointer_button::left, at(300) }).empty());
    REQUIRE(controller.snapshot().pressed.kind == gitman::ui::ui_element_kind::card_refresh);
    REQUIRE(controller.process(gitman::ui::pointer_released_event { refresh.x + 1.0f, refresh.y + 1.0f, gitman::ui::pointer_button::left, at(350) }).empty() == false);
    REQUIRE(controller.snapshot().pressed == gitman::ui::ui_element_id {});

    // 창을 벗어나면 hover가 사라진다.
    REQUIRE(controller.process(gitman::ui::pointer_left_event {}).empty());
    REQUIRE(controller.snapshot().hovered == gitman::ui::ui_element_id {});
    REQUIRE(controller.snapshot().hover_started_at.has_value() == false);
}

TEST_CASE("Keyboard focus walks the visible cards", "[ui][interaction]")
{
    const auto tree { make_tree(3) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    const auto first { controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::arrow_down }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(first))->id->value == u8"card-0");

    const auto second { controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::arrow_down }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(second))->id->value == u8"card-1");

    const auto back { controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::arrow_up }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(back))->id->value == u8"card-0");

    // Enter는 초점 카드를 refresh한다.
    const auto refresh { controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::enter }) };
    REQUIRE(std::get_if<gitman::refresh_card_intent>(as_message(refresh))->id.value == u8"card-0");

    // F5는 전체 refresh, Escape는 선택 해제다.
    REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(*as_message(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::f5 }))));
    const auto cleared { controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape }) };
    REQUIRE(std::get_if<gitman::select_card_intent>(as_message(cleared))->id.has_value() == false);

    // 초점이 풀린 Enter는 아무것도 하지 않는다.
    REQUIRE(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::enter }).empty());
}

TEST_CASE("Dragging a card lets the list insert it at the nearest slot", "[ui][interaction][drag]")
{
    const auto tree { make_tree(3) };
    gitman::ui::interaction_controller controller { gitman::ui::interaction_config { 500ms, 4.0f, 6.0f } };
    controller.set_tree(tree);

    const gitman::ui::rect_f source { bounds_of(*tree, gitman::ui::ui_element_kind::card_body, u8"card-0") };
    const gitman::ui::rect_f target { bounds_of(*tree, gitman::ui::ui_element_kind::card_body, u8"card-2") };

    // 카드 body 왼쪽(버튼이 없는 곳)에서 눌러 임계 거리 밖으로 끌면 drag가 시작된다.
    REQUIRE(controller.process(gitman::ui::pointer_pressed_event { source.x + 5.0f, source.y + 5.0f, gitman::ui::pointer_button::left, at(0) }).empty());
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { source.x + 5.0f, source.y + 25.0f, at(20) }).empty());
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.snapshot().drag->payload.dragged_project.value == u8"card-0");
    // 떠 있는 카드가 잡은 지점 그대로 따라오도록 offset이 실린다.
    REQUIRE(controller.snapshot().drag->payload.grab_offset_x == 5.0f);
    REQUIRE(controller.snapshot().drag->payload.grab_offset_y == 5.0f);

    SECTION("대상 카드의 아래쪽 절반은 그 카드 뒤로 삽입한다")
    {
        // 카드가 아니라 목록이 drop 대상이다. 버튼 영역 위라도 잡힌다.
        const float drop_x { target.x + target.width - 20.0f };
        const float drop_y { target.y + target.height - 5.0f };
        REQUIRE(controller.process(gitman::ui::pointer_moved_event { drop_x, drop_y, at(40) }).empty());
        REQUIRE(controller.snapshot().drag->hovered_drop_target.kind == gitman::ui::ui_element_kind::card_list);

        const auto actions { controller.process(gitman::ui::pointer_released_event { drop_x, drop_y, gitman::ui::pointer_button::left, at(60) }) };
        const auto* const message { as_message(actions) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::reorder_card_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
        REQUIRE(intent->target.value == u8"card-2");
        REQUIRE(intent->place_after);
    }

    SECTION("대상 카드의 위쪽 절반은 그 카드 앞(= 이전 카드 뒤)으로 삽입한다")
    {
        const auto actions { controller.process(gitman::ui::pointer_released_event { target.x + 5.0f, target.y + 5.0f, gitman::ui::pointer_button::left, at(60) }) };
        const auto* const intent { std::get_if<gitman::reorder_card_intent>(as_message(actions)) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
        REQUIRE(intent->target.value == u8"card-1");
        REQUIRE(intent->place_after);
    }

    SECTION("카드 사이 여백에 놓아도 그 자리로 삽입한다")
    {
        const gitman::ui::rect_f middle { bounds_of(*tree, gitman::ui::ui_element_kind::card_body, u8"card-1") };
        const float gap_y { middle.y + middle.height + 3.0f };
        const auto actions { controller.process(gitman::ui::pointer_released_event { middle.x + 5.0f, gap_y, gitman::ui::pointer_button::left, at(60) }) };
        const auto* const intent { std::get_if<gitman::reorder_card_intent>(as_message(actions)) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->target.value == u8"card-1");
        REQUIRE(intent->place_after);
    }

    SECTION("제자리에 놓으면 intent가 없다")
    {
        REQUIRE(controller.process(gitman::ui::pointer_moved_event { source.x + 5.0f, source.y + 30.0f, at(40) }).empty());
        // 목록이 drop을 받지만 위치가 그대로라 아무것도 만들지 않는다.
        REQUIRE(controller.snapshot().drag->hovered_drop_target.kind == gitman::ui::ui_element_kind::card_list);
        REQUIRE(controller.process(gitman::ui::pointer_released_event { source.x + 5.0f, source.y + 30.0f, gitman::ui::pointer_button::left, at(60) }).empty());
    }
}

namespace {
    struct counting_tree
    {
        std::shared_ptr<gitman::ui::ui_tree> tree {};
        std::shared_ptr<int> left_clicks { std::make_shared<int>(0) };
        std::shared_ptr<int> double_clicks { std::make_shared<int>(0) };
        std::shared_ptr<int> right_clicks { std::make_shared<int>(0) };
        std::shared_ptr<int> drops { std::make_shared<int>(0) };
    };

    // 왼쪽·오른쪽·더블 클릭과 drag & drop을 등록한 합성 tree다. build_ui_tree의
    // 화면 구성과 무관하게 기반 API만 검증한다.
    counting_tree make_counting_tree()
    {
        counting_tree result {};
        auto root { std::make_unique<test_panel>(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::root }) };
        root->arrange({ { 0.0f, 0.0f, 400.0f, 400.0f }, 1.0f });

        const gitman::ui::ui_element_id button_id { gitman::ui::ui_element_kind::card_refresh, gitman::project_id { u8"button" } };
        auto button { std::make_unique<gitman::ui::button_element>(button_id, gitman::ui::button_config {}) };
        button->arrange({ { 10.0f, 10.0f, 40.0f, 40.0f }, 1.0f });
        button->set_action(gitman::ui::ui_trigger::left_click, [counter = result.left_clicks](const gitman::ui::ui_action_context&) -> std::vector<gitman::ui::input_action> {
            ++*counter;
            return {};
        });
        button->set_action(gitman::ui::ui_trigger::double_click, [counter = result.double_clicks](const gitman::ui::ui_action_context&) -> std::vector<gitman::ui::input_action> {
            ++*counter;
            return {};
        });
        button->set_action(gitman::ui::ui_trigger::right_click, [counter = result.right_clicks](const gitman::ui::ui_action_context&) -> std::vector<gitman::ui::input_action> {
            ++*counter;
            return {};
        });
        root->add(std::move(button));

        auto source { std::make_unique<test_panel>(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::card_body, gitman::project_id { u8"source" } }) };
        source->arrange({ { 100.0f, 100.0f, 60.0f, 60.0f }, 1.0f });
        gitman::ui::drag_source source_config {};
        source_config.make_payload = [](const gitman::ui::ui_action_context& context) { return gitman::ui::drag_payload { context.element, gitman::project_id { u8"source" } }; };
        source->set_drag_source(source_config);
        root->add(std::move(source));

        auto target { std::make_unique<test_panel>(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::card_body, gitman::project_id { u8"target" } }) };
        target->arrange({ { 300.0f, 100.0f, 60.0f, 60.0f }, 1.0f });
        target->set_drop_target(gitman::ui::drop_target {
            [](const gitman::ui::drag_payload& payload) { return payload.dragged_project.value == u8"source"; },
            [counter = result.drops](const gitman::ui::drag_payload&, const gitman::ui::ui_action_context&) -> std::vector<gitman::ui::input_action> {
                ++*counter;
                return {};
            },
        });
        root->add(std::move(target));

        result.tree = std::make_shared<gitman::ui::ui_tree>(std::move(root));
        return result;
    }
} // namespace

TEST_CASE("Double clicks fire the registered action instead of a second left click", "[ui][interaction]")
{
    const counting_tree fixture { make_counting_tree() };
    gitman::ui::interaction_controller controller { gitman::ui::interaction_config { 500ms, 4.0f, 6.0f } };
    controller.set_tree(fixture.tree);

    const gitman::ui::rect_f button { fixture.tree->find({ gitman::ui::ui_element_kind::card_refresh, gitman::project_id { u8"button" } })->bounds() };
    static_cast<void>(click(controller, button, 0));
    static_cast<void>(click(controller, button, 200));
    REQUIRE(*fixture.left_clicks == 1);
    REQUIRE(*fixture.double_clicks == 1);

    // 임계 시간을 넘긴 두 번째 클릭은 다시 왼쪽 클릭이다.
    static_cast<void>(click(controller, button, 1000));
    static_cast<void>(click(controller, button, 1700));
    REQUIRE(*fixture.left_clicks == 3);
    REQUIRE(*fixture.double_clicks == 1);
}

TEST_CASE("Right clicks fire the registered right action", "[ui][interaction]")
{
    const counting_tree fixture { make_counting_tree() };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(fixture.tree);

    const gitman::ui::rect_f button { fixture.tree->find({ gitman::ui::ui_element_kind::card_refresh, gitman::project_id { u8"button" } })->bounds() };
    REQUIRE(controller.process(gitman::ui::pointer_pressed_event { button.x + 1.0f, button.y + 1.0f, gitman::ui::pointer_button::right, at(0) }).empty());
    REQUIRE(controller.process(gitman::ui::pointer_released_event { button.x + 1.0f, button.y + 1.0f, gitman::ui::pointer_button::right, at(50) }).empty());
    REQUIRE(*fixture.right_clicks == 1);
    REQUIRE(*fixture.left_clicks == 0);
}

TEST_CASE("Dragging past the threshold starts a drag and dropping runs the target action", "[ui][interaction][drag]")
{
    const counting_tree fixture { make_counting_tree() };
    gitman::ui::interaction_controller controller { gitman::ui::interaction_config { 500ms, 4.0f, 6.0f } };
    controller.set_tree(fixture.tree);

    // source(100,100 60x60)에서 눌러 임계 거리 밖으로 끌면 drag가 시작되고 클릭
    // 후보는 사라진다.
    REQUIRE(controller.process(gitman::ui::pointer_pressed_event { 110.0f, 110.0f, gitman::ui::pointer_button::left, at(0) }).empty());
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { 130.0f, 110.0f, at(20) }).empty());
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.snapshot().drag->payload.dragged_project.value == u8"source");
    REQUIRE(controller.snapshot().pressed == gitman::ui::ui_element_id {});

    // 수락하는 대상 위에서는 drop 대상 강조가 켜진다.
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { 310.0f, 110.0f, at(40) }).empty());
    REQUIRE(controller.snapshot().drag->hovered_drop_target.owner.value == u8"target");

    // 대상 위에서 떼면 drop 액션이 실행되고 drag가 끝난다.
    REQUIRE(controller.process(gitman::ui::pointer_released_event { 310.0f, 110.0f, gitman::ui::pointer_button::left, at(60) }).empty());
    REQUIRE(*fixture.drops == 1);
    REQUIRE(controller.snapshot().drag.has_value() == false);
}

TEST_CASE("A drag cancels with escape and a short movement stays a click", "[ui][interaction][drag]")
{
    const counting_tree fixture { make_counting_tree() };
    gitman::ui::interaction_controller controller { gitman::ui::interaction_config { 500ms, 4.0f, 6.0f } };
    controller.set_tree(fixture.tree);

    // Escape는 drag만 취소하고 선택 해제 intent를 내지 않는다.
    REQUIRE(controller.process(gitman::ui::pointer_pressed_event { 110.0f, 110.0f, gitman::ui::pointer_button::left, at(0) }).empty());
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { 130.0f, 110.0f, at(20) }).empty());
    REQUIRE(controller.snapshot().drag.has_value());
    REQUIRE(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape }).empty());
    REQUIRE(controller.snapshot().drag.has_value() == false);
    REQUIRE(*fixture.drops == 0);

    // 임계 거리 안의 흔들림은 drag가 아니다. drag source에는 클릭 액션이 없으므로
    // 아무 액션도 나오지 않지만 drag 상태도 만들어지지 않아야 한다.
    REQUIRE(controller.process(gitman::ui::pointer_pressed_event { 110.0f, 110.0f, gitman::ui::pointer_button::left, at(100) }).empty());
    REQUIRE(controller.process(gitman::ui::pointer_moved_event { 112.0f, 110.0f, at(120) }).empty());
    REQUIRE(controller.snapshot().drag.has_value() == false);
    REQUIRE(controller.process(gitman::ui::pointer_released_event { 112.0f, 110.0f, gitman::ui::pointer_button::left, at(140) }).empty());
}

namespace {
    void pump_thread_main(messaging::channel<gitman::ui::raw_input_event>& input_inbox, messaging::latest_slot<std::shared_ptr<const gitman::ui::ui_tree>>& tree_slot,
        messaging::channel<gitman::logic_message>& logic_inbox, messaging::latest_slot<gitman::ui::interaction_snapshot>& interaction_slot, std::atomic<int>& dialog_requests)
    {
        run_ui_input_pump(input_inbox, tree_slot, logic_inbox, interaction_slot, [&dialog_requests](const gitman::ui::ui_command command) {
            if (command == gitman::ui::ui_command::show_open_document_dialog)
                dialog_requests.fetch_add(1);
        });
    }
} // namespace

TEST_CASE("The input pump forwards intents, UI commands, and interaction snapshots until closed", "[ui][interaction]")
{
    messaging::channel<gitman::ui::raw_input_event> input_inbox { messaging::channel_options { 64, messaging::overflow_policy::drop_oldest, {} } };
    messaging::latest_slot<std::shared_ptr<const gitman::ui::ui_tree>> tree_slot {};
    messaging::channel<gitman::logic_message> logic_inbox { messaging::channel_options { 64, messaging::overflow_policy::reject_newest, {} } };
    messaging::latest_slot<gitman::ui::interaction_snapshot> interaction_slot {};
    std::atomic<int> dialog_requests { 0 };

    const auto tree { make_tree(1, gitman::view_empty_state::no_document) };
    REQUIRE(tree_slot.publish(tree) == 1u);

    std::thread pump { &pump_thread_main, std::ref(input_inbox), std::ref(tree_slot), std::ref(logic_inbox), std::ref(interaction_slot), std::ref(dialog_requests) };

    const gitman::ui::rect_f refresh_all { bounds_of(*tree, gitman::ui::ui_element_kind::toolbar_refresh_all) };
    REQUIRE(input_inbox.post(gitman::ui::pointer_moved_event { refresh_all.x + 1.0f, refresh_all.y + 1.0f, at(0) }) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(gitman::ui::pointer_pressed_event { refresh_all.x + 1.0f, refresh_all.y + 1.0f, gitman::ui::pointer_button::left, at(10) }) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(gitman::ui::pointer_released_event { refresh_all.x + 1.0f, refresh_all.y + 1.0f, gitman::ui::pointer_button::left, at(20) }) == messaging::post_result::posted);

    const gitman::ui::rect_f open_button { bounds_of(*tree, gitman::ui::ui_element_kind::toolbar_open_document) };
    REQUIRE(input_inbox.post(gitman::ui::pointer_pressed_event { open_button.x + 1.0f, open_button.y + 1.0f, gitman::ui::pointer_button::left, at(30) }) == messaging::post_result::posted);
    REQUIRE(input_inbox.post(gitman::ui::pointer_released_event { open_button.x + 1.0f, open_button.y + 1.0f, gitman::ui::pointer_button::left, at(40) }) == messaging::post_result::posted);

    messaging::envelope<gitman::logic_message> received {};
    REQUIRE(logic_inbox.receive_wait(received, std::chrono::milliseconds { 5000 }) == messaging::receive_status::received);
    REQUIRE(std::holds_alternative<gitman::refresh_all_intent>(received.payload));

    input_inbox.close();
    pump.join();
    REQUIRE(dialog_requests.load() == 1);

    // hover가 게시되어 UI thread가 강조를 그릴 수 있어야 한다.
    REQUIRE(interaction_slot.take_newer(0).has_value());
}
