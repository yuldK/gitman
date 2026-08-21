#include "application/logic_controller.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

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

    const std::shared_ptr<const gitman::ui::ui_tree> tree { gitman::ui::build_ui_tree(*fixture.view()) };
    const gitman::ui::ui_element* const dialog { tree->find({ gitman::ui::ui_element_kind::notice_dialog }) };
    REQUIRE(dialog != nullptr);
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::settings_dialog }) != nullptr);

    // 확인 버튼과 배경 클릭이 모두 닫기다.
    const gitman::ui::ui_element* const confirm { tree->find({ gitman::ui::ui_element_kind::notice_dialog_confirm }) };
    REQUIRE(confirm != nullptr);
    const std::vector<gitman::ui::input_action> confirmed { (*confirm->action(gitman::ui::ui_trigger::left_click))({}) };
    REQUIRE(std::holds_alternative<gitman::dismiss_notice_intent>(std::get<gitman::logic_message>(confirmed.front())));
    const std::vector<gitman::ui::input_action> background { (*dialog->action(gitman::ui::ui_trigger::left_click))({}) };
    REQUIRE(std::holds_alternative<gitman::dismiss_notice_intent>(std::get<gitman::logic_message>(background.front())));

    // 알림은 환경설정 위에 있으므로 hit test와 Esc 모두 알림이 먼저 가져간다.
    const gitman::ui::rect_f box { confirm->bounds() };
    REQUIRE(tree->hit_test(box.x + box.width / 2.0f, box.y + box.height / 2.0f) == confirm);

    gitman::ui::interaction_controller interaction {};
    interaction.set_tree(tree);
    const std::vector<gitman::ui::input_action> escaped { interaction.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape }) };
    REQUIRE(escaped.size() == 1);
    REQUIRE(std::holds_alternative<gitman::dismiss_notice_intent>(std::get<gitman::logic_message>(escaped.front())));
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
    const std::shared_ptr<const gitman::ui::ui_tree> tree { gitman::ui::build_ui_tree(*view) };
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::start_page_generate_document }) != nullptr);
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::toolbar_close_document })->visible() == false);
    REQUIRE(tree->find({ gitman::ui::ui_element_kind::toolbar_settings })->enabled());

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
