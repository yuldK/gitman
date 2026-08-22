#include "application/logic_controller.h"
#include "domain/app_settings.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/start_page_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
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

    gitman::app_settings make_settings(const std::vector<std::u8string_view>& paths)
    {
        gitman::app_settings settings {};
        // touch는 맨 앞에 넣으므로 뒤에서부터 올려 인자 순서를 그대로 유지한다.
        for (std::size_t index = paths.size(); index > 0; --index)
            gitman::touch_recent_document(settings, std::u8string { paths[index - 1] }, u8"2026-08-21T10:00:00Z");
        return settings;
    }

    // 문서 없이 앱 설정만 도착한 controller다. 시작 페이지가 보이는 상태다.
    struct start_page_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        explicit start_page_fixture(const std::vector<std::u8string_view>& paths = {}, const float height = 600.0f)
        {
            controller.handle(gitman::logic_message { gitman::window_metrics_intent { 900.0f, height, 1.0f } });
            controller.start();
            gitman::app_settings_loaded_event loaded {};
            loaded.operation_id = submitter.requests.front().operation_id;
            loaded.settings = make_settings(paths);
            controller.handle(gitman::logic_message { std::move(loaded) });
            submitter.requests.clear();
        }

        [[nodiscard]] std::shared_ptr<const gitman::view_snapshot> view() const
        {
            return controller.make_view_snapshot();
        }
    };

    std::vector<gitman::ui::input_action> click(const gitman::ui::ui_tree& tree, const gitman::ui::ui_element_id& id)
    {
        const gitman::ui::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const gitman::ui::ui_action* const action { element->action(gitman::ui::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }

    gitman::document_loaded_event make_loaded_document()
    {
        gitman::document_loaded_event event {};
        gitman::workspace_document document {};
        document.document_path = u8"D:\\workspaces\\team.version-list";
        event.document = { std::move(document) };
        return event;
    }
} // namespace

TEST_CASE("The start page replaces the empty state while no document is open", "[logic][start-page]")
{
    start_page_fixture fixture { { u8"D:\\workspaces\\team.version-list", u8"E:\\tools\\client.version-list" } };

    const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
    REQUIRE(view->empty_state == gitman::view_empty_state::no_document);
    REQUIRE(view->start_page.has_value());
    REQUIRE(view->start_page->loading == false);
    REQUIRE(view->start_page->recents.size() == 2);
    REQUIRE(view->start_page->recents.front().display_name == u8"team");
    // 폴더는 표시 전용이라 구분자가 `/`다. 여는 데 쓰는 path는 원형이다 (T2).
    REQUIRE(view->start_page->recents.front().folder == u8"D:/workspaces");
    REQUIRE(view->start_page->recents.front().path == u8"D:\\workspaces\\team.version-list");
    REQUIRE(view->start_page->recents.back().display_name == u8"client");

    // 문서를 열면 시작 페이지가 사라진다.
    fixture.controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\workspaces\\team.version-list" } });
    fixture.controller.handle(gitman::logic_message { make_loaded_document() });
    REQUIRE(fixture.view()->start_page.has_value() == false);
}

TEST_CASE("The start page reports that the recent list is still being read", "[logic][start-page]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::logic_message { gitman::window_metrics_intent { 900.0f, 600.0f, 1.0f } });
    controller.start();

    const std::shared_ptr<const gitman::view_snapshot> view { controller.make_view_snapshot() };
    REQUIRE(view->start_page.has_value());
    REQUIRE(view->start_page->loading);
    REQUIRE(view->start_page->recents.empty());
}

TEST_CASE("The start page tree exposes the start actions and hides the empty label", "[ui][start-page]")
{
    start_page_fixture fixture { { u8"D:\\workspaces\\team.version-list" } };
    const std::shared_ptr<const gitman::ui::ui_tree> tree { gitman::ui::build_ui_tree(*fixture.view()) };

    REQUIRE(tree->find({ gitman::ui::ui_element_kind::start_page }) != nullptr);
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::empty_state })->visible() == false);

    const std::vector<gitman::ui::input_action> open { click(*tree, { gitman::ui::ui_element_kind::start_page_open_document }) };
    REQUIRE(open.size() == 1);
    REQUIRE(std::get<gitman::ui::ui_command>(open.front()) == gitman::ui::ui_command::show_open_document_dialog);

    const std::vector<gitman::ui::input_action> generate { click(*tree, { gitman::ui::ui_element_kind::start_page_generate_document }) };
    REQUIRE(std::get<gitman::ui::ui_command>(generate.front()) == gitman::ui::ui_command::show_generate_document_dialog);
}

TEST_CASE("A recent row opens its document and the remove icon only drops the entry", "[ui][start-page]")
{
    start_page_fixture fixture { { u8"D:\\workspaces\\team.version-list", u8"E:\\tools\\client.version-list" } };
    const std::shared_ptr<const gitman::ui::ui_tree> tree { gitman::ui::build_ui_tree(*fixture.view()) };

    const std::vector<gitman::ui::input_action> opened { click(*tree, gitman::ui::start_page_recent_item_id(1)) };
    REQUIRE(opened.size() == 1);
    const auto& open_message { std::get<gitman::logic_message>(opened.front()) };
    REQUIRE(std::get<gitman::open_document_intent>(open_message).path == u8"E:\\tools\\client.version-list");

    const std::vector<gitman::ui::input_action> removed { click(*tree, gitman::ui::start_page_recent_remove_id(0)) };
    const auto& remove_message { std::get<gitman::logic_message>(removed.front()) };
    REQUIRE(std::get<gitman::remove_recent_document_intent>(remove_message).path == u8"D:\\workspaces\\team.version-list");

    // 제거 아이콘은 행 위에 있어 hit test에서 먼저 걸린다.
    const gitman::ui::ui_element* const remove { tree->find(gitman::ui::start_page_recent_remove_id(0)) };
    const gitman::ui::rect_f box { remove->bounds() };
    REQUIRE(tree->hit_test(box.x + box.width / 2.0f, box.y + box.height / 2.0f) == remove);

    // 실제로 목록에서 사라지고 저장이 예약된다.
    fixture.controller.handle(gitman::logic_message { gitman::remove_recent_document_intent { u8"D:\\workspaces\\team.version-list" } });
    const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
    REQUIRE(view->start_page->recents.size() == 1);
    REQUIRE(view->start_page->recents.front().display_name == u8"client");
    REQUIRE(fixture.submitter.requests.size() == 1);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::save_app_settings);
}

TEST_CASE("A short window draws only the rows that fit and reports the rest", "[ui][start-page]")
{
    // 카드 목록 자리가 좁으면 들어가는 행만 보이고 나머지는 숨겨진다.
    start_page_fixture fixture {
        {
            u8"D:\\ws\\a.version-list",
            u8"D:\\ws\\b.version-list",
            u8"D:\\ws\\c.version-list",
            u8"D:\\ws\\d.version-list",
            u8"D:\\ws\\e.version-list",
        },
        320.0f,
    };

    const std::shared_ptr<const gitman::ui::ui_tree> tree { gitman::ui::build_ui_tree(*fixture.view()) };
    std::size_t visible { 0 };
    for (std::size_t index = 0; index < 5; ++index)
        if (tree->find(gitman::ui::start_page_recent_item_id(index))->visible())
            ++visible;

    REQUIRE(visible > 0);
    REQUIRE(visible < 5);
}
