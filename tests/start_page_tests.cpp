#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "domain/app_settings.h"
#include "helpers/ui_test_support.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/start_page_element.h"

#include "luil/ui/ui_events.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using namespace gitman::testing;

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
            controller.handle(gitman::logic_message { gitman::startup_intent {} });
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

    // element의 왼쪽 클릭 액션을 그 자리에서 실행한다. 어떤 액션이 나오는지가
    // gitman의 몫이고, 누름·뗌의 상태 기계는 luil test가 본다.
    std::vector<luil::input_action> invoke_click(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const action { element->action(luil::ui_trigger::left_click) };
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
    controller.handle(gitman::logic_message { gitman::startup_intent {} });

    const std::shared_ptr<const gitman::view_snapshot> view { controller.make_view_snapshot() };
    REQUIRE(view->start_page.has_value());
    REQUIRE(view->start_page->loading);
    REQUIRE(view->start_page->recents.empty());
}

TEST_CASE("The start page tree exposes the start actions and hides the empty label", "[ui][start-page]")
{
    start_page_fixture fixture { { u8"D:\\workspaces\\team.version-list" } };
    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };

    REQUIRE(find(*tree, gitman::ui::kind_start_page) != nullptr);
    REQUIRE(find(*tree, gitman::ui::kind_empty_state)->visible() == false);

    // 문서 열기는 UI thread가 여는 파일 dialog라 앱 명령이다.
    const std::vector<luil::input_action> open { invoke_click(*tree, id_of(gitman::ui::kind_start_page_open_document)) };
    const luil::app_ui_command* const command { as_app_command(open) };
    REQUIRE(command != nullptr);
    REQUIRE(gitman::parse_app_command(command->command) == gitman::app_command::show_open_document_dialog);
    REQUIRE(command->argument.empty());

    // 새 문서 만들기는 Win32 팝업이 아니라 앱 UI dialog를 연다 (F3).
    const std::vector<luil::input_action> generate { invoke_click(*tree, id_of(gitman::ui::kind_start_page_generate_document)) };
    REQUIRE(as_intent<gitman::open_generate_document_intent>(generate) != nullptr);
}

TEST_CASE("A recent row opens its document and the remove icon only drops the entry", "[ui][start-page]")
{
    start_page_fixture fixture { { u8"D:\\workspaces\\team.version-list", u8"E:\\tools\\client.version-list" } };
    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };

    const std::vector<luil::input_action> opened { invoke_click(*tree, gitman::ui::start_page_recent_item_id(1)) };
    const auto* const open_intent { as_intent<gitman::open_document_intent>(opened) };
    REQUIRE(open_intent != nullptr);
    REQUIRE(open_intent->path == u8"E:\\tools\\client.version-list");

    const std::vector<luil::input_action> removed { invoke_click(*tree, gitman::ui::start_page_recent_remove_id(0)) };
    const auto* const remove_intent { as_intent<gitman::remove_recent_document_intent>(removed) };
    REQUIRE(remove_intent != nullptr);
    REQUIRE(remove_intent->path == u8"D:\\workspaces\\team.version-list");

    // 최근 항목 행과 그 안의 아이콘은 Tab 자리가 아니다 (D1-B). Tab은 시작 열의
    // 두 줄만 지난다.
    REQUIRE(tree->find(gitman::ui::start_page_recent_item_id(1))->tab_stop() == false);
    REQUIRE(tree->find(gitman::ui::start_page_recent_remove_id(0))->tab_stop() == false);
    REQUIRE(tree->find(gitman::ui::start_page_recent_pin_id(0))->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_start_page_open_document)->tab_stop());

    // 제거 아이콘은 행 위에 있어 hit test에서 먼저 걸린다.
    const luil::ui_element* const remove { tree->find(gitman::ui::start_page_recent_remove_id(0)) };
    REQUIRE(remove != nullptr);
    const luil::rect_f box { remove->bounds() };
    REQUIRE(tree->hit_test(box.x + box.width / 2.0f, box.y + box.height / 2.0f) == remove);

    // 실제로 목록에서 사라지고 저장이 예약된다.
    fixture.controller.handle(gitman::logic_message { gitman::remove_recent_document_intent { u8"D:\\workspaces\\team.version-list" } });
    const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
    REQUIRE(view->start_page->recents.size() == 1);
    REQUIRE(view->start_page->recents.front().display_name == u8"client");
    REQUIRE(fixture.submitter.requests.size() == 1);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::save_app_settings);
}

TEST_CASE("A recent row pins and unpins through its left icon", "[ui][start-page][pin]")
{
    start_page_fixture fixture { { u8"D:\\workspaces\\team.version-list", u8"E:\\tools\\client.version-list" } };
    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };

    const std::vector<luil::input_action> pinned { invoke_click(*tree, gitman::ui::start_page_recent_pin_id(1)) };
    const auto* const pin_intent { as_intent<gitman::toggle_recent_document_pin_intent>(pinned) };
    REQUIRE(pin_intent != nullptr);
    REQUIRE(pin_intent->path == u8"E:\\tools\\client.version-list");

    // pin 아이콘도 행 위에 있어 hit test에서 먼저 걸린다 — 누르면 문서가 열리지 않는다.
    const luil::ui_element* const pin { tree->find(gitman::ui::start_page_recent_pin_id(1)) };
    REQUIRE(pin != nullptr);
    const luil::rect_f box { pin->bounds() };
    REQUIRE(tree->hit_test(box.x + box.width / 2.0f, box.y + box.height / 2.0f) == pin);

    // 고정하면 목록 맨 앞으로 올라가고 저장이 예약된다.
    fixture.controller.handle(gitman::logic_message { gitman::toggle_recent_document_pin_intent { u8"E:\\tools\\client.version-list" } });
    {
        const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
        REQUIRE(view->start_page->recents.front().display_name == u8"client");
        REQUIRE(view->start_page->recents.front().pinned);
        REQUIRE(view->start_page->recents.back().pinned == false);
    }
    REQUIRE(fixture.submitter.requests.size() == 1);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::save_app_settings);

    // 다시 누르면 풀리고, 가장 최근에 연 문서로 쳐서 여전히 맨 앞이다 (2026-08-23 지시).
    fixture.controller.handle(gitman::logic_message { gitman::toggle_recent_document_pin_intent { u8"E:\\tools\\client.version-list" } });
    const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
    REQUIRE(view->start_page->recents.front().display_name == u8"client");
    REQUIRE(view->start_page->recents.front().pinned == false);

    // 목록에 없는 경로는 아무 일도 하지 않는다.
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::logic_message { gitman::toggle_recent_document_pin_intent { u8"Z:\\none.version-list" } });
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("Pinned rows sit under their own section header", "[ui][start-page][pin]")
{
    start_page_fixture fixture { { u8"D:\\ws\\one.version-list", u8"D:\\ws\\two.version-list" } };

    // 고정이 없으면 머리글도 하나뿐이라 두 행이 맞붙는다.
    {
        const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };
        const float first { tree->find(gitman::ui::start_page_recent_item_id(0))->bounds().y };
        const float second { tree->find(gitman::ui::start_page_recent_item_id(1))->bounds().y };
        REQUIRE(second - first == gitman::layout_start_page_row_height());
    }

    // 하나를 고정하면 그 행이 맨 위 `고정됨` 무리로 가고, 나머지 앞에 `최근 항목`
    // 머리글이 한 줄 더 들어간다 (2026-08-23 지시).
    fixture.controller.handle(gitman::logic_message { gitman::toggle_recent_document_pin_intent { u8"D:\\ws\\two.version-list" } });
    const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
    REQUIRE(view->start_page->recents.front().display_name == u8"two");
    REQUIRE(view->start_page->recents.front().pinned);

    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*view) };
    const float pinned { tree->find(gitman::ui::start_page_recent_item_id(0))->bounds().y };
    const float loose { tree->find(gitman::ui::start_page_recent_item_id(1))->bounds().y };
    REQUIRE(loose - pinned == gitman::layout_start_page_row_height() + gitman::layout_start_page_section_height());
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

    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };
    std::size_t visible { 0 };
    for (std::size_t index = 0; index < 5; ++index)
        if (tree->find(gitman::ui::start_page_recent_item_id(index))->visible())
            ++visible;

    REQUIRE(visible > 0);
    REQUIRE(visible < 5);
}

TEST_CASE("Start page rows are named by what they show", "[ui][start-page][accessibility]")
{
    start_page_fixture fixture { { u8"D:\\workspaces\\team.version-list" } };
    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };

    // 시작 열의 두 줄은 툴팁이 없어 접근 tree에서 접혔었다. 라벨이 이름이다.
    const luil::access_info open { find(*tree, gitman::ui::kind_start_page_open_document)->accessibility() };
    REQUIRE(open.role == luil::access_role::button);
    REQUIRE(open.name == u8"문서 열기…");
    REQUIRE(find(*tree, gitman::ui::kind_start_page_generate_document)->accessibility().name == u8"새 문서 만들기…");

    // 최근 항목은 전체 경로 대신 화면처럼 이름과 폴더로 읽힌다.
    const luil::ui_element* const recent { tree->find(gitman::ui::start_page_recent_item_id(0)) };
    REQUIRE(recent != nullptr);
    REQUIRE(recent->accessibility().name == u8"team (D:/workspaces)");
    REQUIRE(recent->tooltip() == u8"D:\\workspaces\\team.version-list");
}
