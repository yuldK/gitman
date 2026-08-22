#include "application/logic_controller.h"
#include "domain/app_settings.h"
#include "domain/path_syntax.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <utility>
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

    // 문서 하나와 카드 2장을 갖춘 controller다. 카드 경로는 문서 폴더 아래와 그
    // 바깥에 하나씩 두어 상대 경로 표시(`..`)도 함께 본다.
    struct display_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        display_fixture()
        {
            controller.handle(gitman::logic_message { gitman::window_metrics_intent { 900.0f, 600.0f, 1.0f } });
            controller.handle(gitman::logic_message { gitman::open_document_intent { u8"D:\\workspaces\\team.version-list" } });

            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"D:\\workspaces\\team.version-list";
            for (const std::u8string_view path : { std::u8string_view { u8"D:\\workspaces\\alpha" }, std::u8string_view { u8"D:\\other\\beta" } })
            {
                gitman::project_definition project {};
                project.id.value = path;
                project.display_name = u8"card";
                project.path.original = path;
                project.path.normalized = path;
                document.projects.push_back(std::move(project));
            }
            loaded.document = { std::move(document) };
            controller.handle(gitman::logic_message { std::move(loaded) });
        }

        [[nodiscard]] std::shared_ptr<const gitman::view_snapshot> view() const
        {
            return controller.make_view_snapshot();
        }
    };
} // namespace

TEST_CASE("Display paths use forward slashes", "[domain][path][display]")
{
    REQUIRE(gitman::to_display_path(u8"C:\\work\\alpha") == u8"C:/work/alpha");
    REQUIRE(gitman::to_display_path(u8"..\\..\\alpha") == u8"../../alpha");
    // 이미 `/`인 구분자와 UNC 경로도 그대로 통일된다.
    REQUIRE(gitman::to_display_path(u8"C:/work\\alpha/beta") == u8"C:/work/alpha/beta");
    REQUIRE(gitman::to_display_path(u8"\\\\server\\share\\alpha") == u8"//server/share/alpha");
    // 구분자가 없는 값과 빈 값은 그대로다.
    REQUIRE(gitman::to_display_path(u8"alpha") == u8"alpha");
    REQUIRE(gitman::to_display_path(u8"").empty());
}

TEST_CASE("The snapshot carries both the native and the displayed document path", "[ui][path][display]")
{
    const display_fixture fixture {};
    const auto view { fixture.view() };

    // 셸 실행·파일 dialog가 쓰는 경로는 Windows 원형 그대로다.
    REQUIRE(view->document_path == u8"D:\\workspaces\\team.version-list");
    REQUIRE(view->document_display_path == u8"D:/workspaces/team.version-list");
}

TEST_CASE("Card paths are displayed with forward slashes in both absolute and relative modes", "[ui][path][display]")
{
    display_fixture fixture {};

    {
        const auto view { fixture.view() };
        REQUIRE(view->cards.size() == 2u);
        REQUIRE(view->relative_paths == false);
        REQUIRE(view->cards[0].path == u8"D:/workspaces/alpha");
        REQUIRE(view->cards[1].path == u8"D:/other/beta");
    }

    // 상대 경로 표시로 바꾸면 `..` 구간도 `/`로 이어진다.
    fixture.controller.handle(gitman::logic_message { gitman::toggle_path_display_intent {} });
    const auto view { fixture.view() };
    REQUIRE(view->relative_paths);
    REQUIRE(view->cards[0].path == u8"alpha");
    REQUIRE(view->cards[1].path == u8"../other/beta");
}

TEST_CASE("Start page recent folders are displayed with forward slashes", "[ui][path][display]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::logic_message { gitman::window_metrics_intent { 900.0f, 600.0f, 1.0f } });
    controller.start();

    gitman::app_settings_loaded_event loaded {};
    loaded.operation_id = submitter.requests.front().operation_id;
    gitman::touch_recent_document(loaded.settings, u8"D:\\workspaces\\team.version-list", u8"2026-08-21T10:00:00Z");
    controller.handle(gitman::logic_message { std::move(loaded) });

    const auto view { controller.make_view_snapshot() };
    REQUIRE(view->start_page.has_value());
    REQUIRE(view->start_page->recents.size() == 1u);
    REQUIRE(view->start_page->recents.front().folder == u8"D:/workspaces");
    // 여는 데 쓰는 경로는 원형이다.
    REQUIRE(view->start_page->recents.front().path == u8"D:\\workspaces\\team.version-list");
}
