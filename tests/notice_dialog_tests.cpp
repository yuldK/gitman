#include "application/logic_controller.h"
#include "helpers/ui_test_support.h"

#include "luil/ui/ui_events.h"

#include <catch2/catch_test_macros.hpp>

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

    // 문서 하나와 카드 하나를 연 controller다. 알림은 이 위에 겹쳐 뜬다.
    struct notice_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        notice_fixture()
        {
            controller.handle(gitman::logic_message { gitman::window_metrics_intent { 900.0f, 700.0f, 1.0f } });
            controller.handle(gitman::logic_message { gitman::open_document_intent { u8"C:\\work\\p.version-list" } });

            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            gitman::project_definition project {};
            project.id.value = u8"alpha";
            project.display_name = u8"alpha";
            project.path.original = u8"C:\\work\\alpha";
            project.path.normalized = project.path.original;
            document.projects.push_back(std::move(project));
            loaded.document = { std::move(document) };
            controller.handle(gitman::logic_message { std::move(loaded) });
            submitter.requests.clear();
        }

        [[nodiscard]] std::shared_ptr<const gitman::view_snapshot> view() const
        {
            return controller.make_view_snapshot();
        }

        void show_notice(const bool error = false)
        {
            gitman::show_notice_intent notice {};
            notice.title = u8"파일 연결";
            notice.lines.push_back(u8".version-list 문서가 이 프로그램에 연결되었습니다.");
            notice.error = error;
            controller.handle(gitman::logic_message { std::move(notice) });
        }
    };

    // element의 왼쪽 클릭 액션을 그 자리에서 실행한다.
    std::vector<luil::input_action> invoke_click(const luil::ui_element& element)
    {
        const luil::ui_action* const action { element.action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }
} // namespace

TEST_CASE("A notice intent opens an in-app dialog that any of its exits close", "[logic][notice]")
{
    notice_fixture fixture {};
    REQUIRE(fixture.view()->notice_dialog.has_value() == false);

    fixture.show_notice();
    const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
    REQUIRE(view->notice_dialog.has_value());
    REQUIRE(view->notice_dialog->title == u8"파일 연결");
    REQUIRE(view->notice_dialog->lines.size() == 1);
    REQUIRE(view->notice_dialog->error == false);

    fixture.controller.handle(gitman::logic_message { gitman::dismiss_notice_intent {} });
    REQUIRE(fixture.view()->notice_dialog.has_value() == false);
}

TEST_CASE("The notice dialog draws above the other dialogs and closes on click or escape", "[ui][notice]")
{
    notice_fixture fixture {};
    fixture.controller.handle(gitman::logic_message { gitman::open_settings_intent {} });
    fixture.show_notice(true);

    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };
    const luil::ui_element* const dialog { find(*tree, gitman::ui::kind_notice_dialog) };
    REQUIRE(dialog != nullptr);
    REQUIRE(find(*tree, gitman::ui::kind_settings_dialog) != nullptr);

    // D2-A: 본체는 luil modal host의 content이고 초점은 그 안에 갇힌다.
    REQUIRE(find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_notice) != nullptr);
    REQUIRE(tree->focus_trap() != nullptr);

    // 확인 버튼과 배경(scrim) 클릭이 모두 닫기다.
    const luil::ui_element* const confirm { find(*tree, gitman::ui::kind_notice_dialog_confirm) };
    REQUIRE(confirm != nullptr);
    const std::vector<luil::input_action> confirmed { invoke_click(*confirm) };
    REQUIRE(as_intent<gitman::dismiss_notice_intent>(confirmed) != nullptr);
    const luil::ui_element* const scrim { find(*tree, luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_notice) };
    REQUIRE(scrim != nullptr);
    const std::vector<luil::input_action> background { invoke_click(*scrim) };
    REQUIRE(as_intent<gitman::dismiss_notice_intent>(background) != nullptr);

    // D1-B: 본체도 scrim도 Tab 자리가 아니고 확인 버튼만 자리다.
    REQUIRE(dialog->tab_stop() == false);
    REQUIRE(scrim->tab_stop() == false);
    REQUIRE(confirm->tab_stop());

    // 알림은 환경설정 위에 있으므로 hit test와 Esc 모두 알림이 먼저 가져간다.
    const luil::rect_f box { confirm->bounds() };
    REQUIRE(tree->hit_test(box.x + box.width / 2.0f, box.y + box.height / 2.0f) == confirm);

    interaction_harness harness { tree };
    const std::vector<luil::input_action> escaped { harness.process(luil::key_pressed_event { luil::key_code::escape }) };
    REQUIRE(as_intent<gitman::dismiss_notice_intent>(escaped) != nullptr);
}

TEST_CASE("A long notice line wraps inside the panel instead of being cut", "[ui][notice]")
{
    // 본문 줄이 panel 폭을 넘으면 다음 줄로 넘어가고 panel이 그만큼 높아진다 (글꼴 설계 D6).
    const auto panel_height = [](std::u8string line) {
        gitman::view_snapshot view { make_view(0, gitman::view_empty_state::no_document) };
        view.notice_dialog = { gitman::notice_dialog_view { .title = u8"알림", .lines = { std::move(line) } } };
        const std::shared_ptr<const luil::ui_tree> tree { make_tree(view) };
        const luil::ui_element* const panel { find(*tree, gitman::ui::kind_notice_dialog_panel) };
        REQUIRE(panel != nullptr);
        return panel->bounds().height;
    };

    const float short_line { panel_height(u8"연결했습니다.") };
    const float long_line { panel_height(u8"C:/Users/someone/AppData/Local/Programs/Gitman/bin/gitman.exe 를 .version-list 문서의 기본 프로그램으로 등록했습니다. 탐색기에서 문서를 두 번 누르면 이 프로그램이 열립니다.") };
    REQUIRE(long_line > short_line);
}

TEST_CASE("The notice dialog's confirm button is the default button for Enter", "[ui][notice]")
{
    notice_fixture fixture {};
    fixture.show_notice();

    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*fixture.view()) };
    const luil::ui_element* const confirm { find(*tree, gitman::ui::kind_notice_dialog_confirm) };
    REQUIRE(confirm != nullptr);
    REQUIRE(tree->default_button() == confirm);

    // 초점을 아무 데도 두지 않아도 Enter가 기본 버튼으로 흐른다 (enter-default-design).
    interaction_harness harness { tree };
    const std::vector<luil::input_action> entered { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
    REQUIRE(as_intent<gitman::dismiss_notice_intent>(entered) != nullptr);
}

TEST_CASE("Closing a document returns to the start page and keeps the recent list", "[logic][close-document]")
{
    notice_fixture fixture {};
    fixture.controller.handle(gitman::logic_message { gitman::select_card_intent { gitman::project_id { u8"alpha" } } });
    REQUIRE(fixture.view()->cards.size() == 1);

    fixture.controller.handle(gitman::logic_message { gitman::close_document_intent {} });

    const std::shared_ptr<const gitman::view_snapshot> view { fixture.view() };
    REQUIRE(view->cards.empty());
    REQUIRE(view->document_path.empty());
    REQUIRE(view->empty_state == gitman::view_empty_state::no_document);
    REQUIRE(view->start_page.has_value());
    // 하단 로그 pane과 선택은 함께 사라진다.
    REQUIRE(view->log.has_value() == false);
    REQUIRE(view->selected.has_value() == false);

    // 닫기 뒤에는 시작 페이지가 새 문서 만들기 진입점을 제공한다 (G2: 도구
    // 막대에는 생성 버튼이 없다). 환경설정은 전역 설정 진입점으로 계속 활성이다
    // (G3.2).
    const std::shared_ptr<const luil::ui_tree> tree { make_tree(*view) };
    REQUIRE(find(*tree, gitman::ui::kind_start_page_generate_document) != nullptr);
    REQUIRE(find(*tree, gitman::ui::kind_toolbar_close_document)->visible() == false);
    REQUIRE(find(*tree, gitman::ui::kind_toolbar_settings)->enabled());

    // 닫힌 뒤의 두 번째 닫기는 아무 일도 하지 않는다.
    fixture.controller.handle(gitman::logic_message { gitman::close_document_intent {} });
    REQUIRE(fixture.view()->empty_state == gitman::view_empty_state::no_document);
}

TEST_CASE("Closing a document flushes a pending window placement", "[logic][close-document]")
{
    notice_fixture fixture {};
    gitman::window_placement placement {};
    placement.x = 10;
    placement.y = 20;
    placement.width = 800;
    placement.height = 600;
    fixture.controller.handle(gitman::logic_message { gitman::window_placement_intent { placement } });
    fixture.submitter.requests.clear();

    fixture.controller.handle(gitman::logic_message { gitman::close_document_intent {} });

    REQUIRE(fixture.submitter.requests.size() == 1);
    const gitman::operation_request& save { fixture.submitter.requests.front() };
    REQUIRE(save.kind == gitman::operation_kind::save_document);
    REQUIRE(save.document.has_value());
    REQUIRE(save.document->window.has_value());
    REQUIRE(save.document->window->width == 800);
}
