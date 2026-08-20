#include "application/logic_controller.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/context_menu_element.h"
#include "presentation/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    class recording_submitter final : public gitman::operation_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::operation_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::operation_request> requests {};
    };

    // 문서와 카드 2장(alpha는 준비 완료, beta는 로컬 조회 대기)을 갖춘 controller다.
    struct menu_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        menu_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            for (const std::u8string_view id : { std::u8string_view { u8"alpha" }, std::u8string_view { u8"beta" } })
            {
                gitman::project_definition project {};
                project.id.value = id;
                project.display_name = id;
                project.path.original = std::u8string { u8"C:\\work\\" } + std::u8string { id };
                project.path.normalized = project.path.original;
                document.projects.push_back(std::move(project));
            }
            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));

            gitman::query_completed_event local {};
            local.id.value = u8"alpha";
            local.generation = 1;
            local.final_event = true;
            local.result.snapshot.project.value = u8"alpha";
            local.result.snapshot.kind = gitman::repository_kind::git;
            local.result.snapshot.availability = gitman::repository_availability::ready;
            local.result.snapshot.current_reference = u8"main";
            local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
            controller.handle(std::move(local));
            submitter.requests.clear();
        }
    };

    const gitman::context_menu_item_view* find_item(const gitman::context_menu_view& menu, const gitman::context_menu_entry entry)
    {
        for (const gitman::context_menu_item_view& item : menu.items)
            if (item.entry == entry)
                return &item;
        return nullptr;
    }

    std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

    std::vector<gitman::ui::input_action> click(gitman::ui::interaction_controller& controller, const gitman::ui::rect_f& bounds,
        const gitman::ui::pointer_button button = gitman::ui::pointer_button::left)
    {
        const float x { bounds.x + 1.0f };
        const float y { bounds.y + 1.0f };
        static_cast<void>(controller.process(gitman::ui::pointer_pressed_event { x, y, button, at(0) }));
        return controller.process(gitman::ui::pointer_released_event { x, y, button, at(10) });
    }

    // 카드 목록과 임의 항목의 메뉴를 담은 view다. UI element 검증용이다.
    gitman::view_snapshot make_menu_view(const float anchor_x, const float anchor_y, const std::vector<bool>& enabled_flags)
    {
        gitman::view_snapshot view {};
        view.window_width = 800.0f;
        view.window_height = 600.0f;
        view.scale = 1.0f;
        view.empty_state = gitman::view_empty_state::none;
        gitman::card_view_model card {};
        card.id.value = u8"card-0";
        view.cards.push_back(std::move(card));

        gitman::context_menu_view menu {};
        menu.owner.value = u8"card-0";
        menu.anchor_x = anchor_x;
        menu.anchor_y = anchor_y;
        menu.repository_path = u8"C:\\work\\card-0";
        const std::vector<gitman::context_menu_entry> entries {
            gitman::context_menu_entry::open_repository,
            gitman::context_menu_entry::show_local_changes,
            gitman::context_menu_entry::refresh,
            gitman::context_menu_entry::update,
            gitman::context_menu_entry::switch_to,
        };
        for (std::size_t index = 0; index < enabled_flags.size() && index < entries.size(); ++index)
            menu.items.push_back({ entries[index], u8"항목", enabled_flags[index] });
        view.context_menu = { std::move(menu) };
        return view;
    }
} // namespace

TEST_CASE("A right click intent selects the card and opens the menu with button availability", "[logic][context-menu]")
{
    menu_fixture fixture {};
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 120.0f, 240.0f });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->selected.has_value());
    REQUIRE(view->selected->value == u8"alpha");
    REQUIRE(view->context_menu.has_value());
    REQUIRE(view->context_menu->owner.value == u8"alpha");
    REQUIRE(view->context_menu->anchor_x == 120.0f);
    REQUIRE(view->context_menu->anchor_y == 240.0f);
    REQUIRE(view->context_menu->repository_path == u8"C:\\work\\alpha");
    REQUIRE(view->context_menu->items.size() == 5u);

    // 준비된 카드는 모든 항목이 활성이다.
    for (const gitman::context_menu_item_view& item : view->context_menu->items)
        REQUIRE(item.enabled);
}

TEST_CASE("A loading card disables the update and switch items only", "[logic][context-menu]")
{
    menu_fixture fixture {};
    // beta는 로컬 조회가 아직 끝나지 않아 변경 작업을 시작할 수 없다.
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"beta" }, 10.0f, 10.0f });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->context_menu.has_value());
    REQUIRE(find_item(*view->context_menu, gitman::context_menu_entry::open_repository)->enabled);
    REQUIRE(find_item(*view->context_menu, gitman::context_menu_entry::show_local_changes)->enabled);
    REQUIRE(find_item(*view->context_menu, gitman::context_menu_entry::refresh)->enabled);
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::update)->enabled);
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::switch_to)->enabled);
}

TEST_CASE("A running change disables the update item while the menu stays open", "[logic][context-menu]")
{
    menu_fixture fixture {};
    fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->context_menu.has_value());
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::update)->enabled);
    REQUIRE_FALSE(find_item(*view->context_menu, gitman::context_menu_entry::switch_to)->enabled);
}

TEST_CASE("Unknown cards, the close intent, and a document swap dismiss the menu", "[logic][context-menu]")
{
    menu_fixture fixture {};

    SECTION("없는 카드는 열지 않는다")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"ghost" }, 10.0f, 10.0f });
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
    }

    SECTION("닫기 intent")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
        fixture.controller.handle(gitman::close_context_menu_intent {});
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
    }

    SECTION("문서 교체")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 10.0f });
        gitman::document_loaded_event loaded {};
        gitman::workspace_document document {};
        document.document_path = u8"C:\\work\\other.version-list";
        loaded.document = { std::move(document) };
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
        fixture.controller.handle(std::move(loaded));
        REQUIRE(fixture.controller.make_view_snapshot()->context_menu.has_value() == false);
    }
}

TEST_CASE("The built tree anchors the menu panel and pushes it back inside the window", "[ui][context-menu]")
{
    SECTION("앵커에 붙는다")
    {
        const auto tree { gitman::ui::build_ui_tree(make_menu_view(120.0f, 240.0f, { true, true, true, true, true })) };
        const gitman::ui::ui_element* const panel { tree->find({ gitman::ui::ui_element_kind::context_menu_panel }) };
        REQUIRE(panel != nullptr);
        REQUIRE(panel->bounds().x == 120.0f);
        REQUIRE(panel->bounds().y == 240.0f);
        REQUIRE(tree->find(gitman::ui::context_menu_item_id(4)) != nullptr);
    }

    SECTION("창 밖으로 나가면 안쪽으로 민다")
    {
        const auto tree { gitman::ui::build_ui_tree(make_menu_view(790.0f, 595.0f, { true, true, true, true, true })) };
        const gitman::ui::ui_element* const panel { tree->find({ gitman::ui::ui_element_kind::context_menu_panel }) };
        REQUIRE(panel != nullptr);
        REQUIRE(panel->bounds().x + panel->bounds().width <= 800.0f);
        REQUIRE(panel->bounds().y + panel->bounds().height <= 600.0f);
    }
}

TEST_CASE("Menu item clicks close the menu first and then fire the action", "[ui][context-menu]")
{
    const auto tree { gitman::ui::build_ui_tree(make_menu_view(100.0f, 100.0f, { true, true, true, false, true })) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    SECTION("저장소 열기는 폴더 열기 요청이다")
    {
        const auto actions { click(controller, tree->find(gitman::ui::context_menu_item_id(0))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const auto* const close { std::get_if<gitman::logic_message>(&actions[0]) };
        REQUIRE(close != nullptr);
        REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*close));
        const auto* const open { std::get_if<gitman::ui::open_external_request>(&actions[1]) };
        REQUIRE(open != nullptr);
        REQUIRE(open->target == gitman::ui::external_open_target::explorer_folder);
        REQUIRE(open->absolute_path == u8"C:\\work\\card-0");
    }

    SECTION("상태 갱신")
    {
        const auto actions { click(controller, tree->find(gitman::ui::context_menu_item_id(2))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const auto* const refresh { std::get_if<gitman::logic_message>(&actions[1]) };
        REQUIRE(refresh != nullptr);
        const auto* const intent { std::get_if<gitman::refresh_card_intent>(refresh) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
    }

    SECTION("로컬 변경 확인은 F4 dialog를 연다")
    {
        const auto actions { click(controller, tree->find(gitman::ui::context_menu_item_id(1))->bounds()) };
        REQUIRE(actions.size() == 2u);
        const auto* const message { std::get_if<gitman::logic_message>(&actions[1]) };
        REQUIRE(message != nullptr);
        const auto* const intent { std::get_if<gitman::open_local_changes_intent>(message) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->id.value == u8"card-0");
    }

    SECTION("비활성 항목은 아무 일도 하지 않는다")
    {
        REQUIRE(click(controller, tree->find(gitman::ui::context_menu_item_id(3))->bounds()).empty());
    }

    SECTION("바깥 클릭은 좌·우 모두 닫기다")
    {
        for (const gitman::ui::pointer_button button : { gitman::ui::pointer_button::left, gitman::ui::pointer_button::right })
        {
            const auto actions { click(controller, gitman::ui::rect_f { 700.0f, 500.0f, 4.0f, 4.0f }, button) };
            const auto* const message { actions.size() == 1u ? std::get_if<gitman::logic_message>(&actions.front()) : nullptr };
            REQUIRE(message != nullptr);
            REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*message));
        }
    }

    SECTION("메뉴가 떠 있는 동안 휠은 무시된다")
    {
        REQUIRE(controller.process(gitman::ui::mouse_wheel_event { 400.0f, 300.0f, 120.0f }).empty());
    }
}

TEST_CASE("A card body right click asks for the menu at the pointer", "[ui][context-menu]")
{
    gitman::view_snapshot view {};
    view.window_width = 800.0f;
    view.window_height = 600.0f;
    view.scale = 1.0f;
    view.empty_state = gitman::view_empty_state::none;
    gitman::card_view_model card {};
    card.id.value = u8"card-0";
    view.cards.push_back(std::move(card));
    const auto tree { gitman::ui::build_ui_tree(view) };

    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);
    const gitman::ui::rect_f body { tree->find({ gitman::ui::ui_element_kind::card_body, gitman::project_id { u8"card-0" } })->bounds() };
    const auto actions { click(controller, body, gitman::ui::pointer_button::right) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::open_context_menu_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->id.value == u8"card-0");
    REQUIRE(intent->anchor_x == body.x + 1.0f);
    REQUIRE(intent->anchor_y == body.y + 1.0f);
}

TEST_CASE("The keyboard walks enabled items, runs the highlight, and escape closes", "[ui][context-menu]")
{
    // 항목 1이 비활성이라 ↑/↓가 건너뛰어야 한다.
    const auto tree { gitman::ui::build_ui_tree(make_menu_view(100.0f, 100.0f, { true, false, true, true, true })) };
    gitman::ui::interaction_controller controller {};
    controller.set_tree(tree);

    SECTION("↓는 활성 항목 사이를 오가고 Enter가 실행한다")
    {
        REQUIRE(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::arrow_down }).empty());
        REQUIRE(controller.snapshot().menu_highlight == gitman::ui::context_menu_item_id(0));
        // 비활성 항목 1을 건너뛴다.
        REQUIRE(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::arrow_down }).empty());
        REQUIRE(controller.snapshot().menu_highlight == gitman::ui::context_menu_item_id(2));
        REQUIRE(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::arrow_up }).empty());
        REQUIRE(controller.snapshot().menu_highlight == gitman::ui::context_menu_item_id(0));

        const auto actions { controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::enter }) };
        REQUIRE(actions.size() == 2u);
        REQUIRE(std::get_if<gitman::ui::open_external_request>(&actions[1]) != nullptr);
    }

    SECTION("강조가 없으면 Enter는 아무 일도 하지 않는다")
    {
        REQUIRE(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::enter }).empty());
    }

    SECTION("Esc는 닫기 intent다")
    {
        const auto actions { controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape }) };
        REQUIRE(actions.size() == 1u);
        const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::close_context_menu_intent>(*message));
    }

    SECTION("메뉴가 닫히면 강조가 지워진다")
    {
        REQUIRE(controller.process(gitman::ui::key_pressed_event { gitman::ui::key_code::arrow_down }).empty());
        REQUIRE((controller.snapshot().menu_highlight == gitman::ui::ui_element_id {}) == false);
        gitman::view_snapshot plain {};
        plain.window_width = 800.0f;
        plain.window_height = 600.0f;
        plain.scale = 1.0f;
        controller.set_tree(gitman::ui::build_ui_tree(plain));
        static_cast<void>(controller.process(gitman::ui::pointer_moved_event { 5.0f, 5.0f, at(50) }));
        REQUIRE(controller.snapshot().menu_highlight == gitman::ui::ui_element_id {});
    }
}
