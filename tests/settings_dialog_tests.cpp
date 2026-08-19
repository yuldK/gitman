#include "application/logic_controller.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/settings_dialog_element.h"
#include "presentation/ui/ui_interaction.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
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

    // 문서(설정에 Git 경로 지정, 활성 카드 alpha와 비활성 카드 beta)를 갖춘
    // controller다. 열자마자 제출되는 query_local은 기록에서 지운다.
    struct settings_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        settings_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            document.settings.git_executable = u8"C:\\tools\\git\\git.exe";

            gitman::project_definition alpha {};
            alpha.id.value = u8"alpha";
            alpha.display_name = u8"alpha";
            alpha.path.original = u8"C:\\work\\alpha";
            alpha.path.normalized = alpha.path.original;
            document.projects.push_back(std::move(alpha));

            gitman::project_definition beta {};
            beta.id.value = u8"beta";
            beta.display_name = u8"beta";
            beta.path.original = u8"C:\\work\\beta";
            beta.path.normalized = beta.path.original;
            beta.enabled = false;
            document.projects.push_back(std::move(beta));

            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));

            // 초기 query_local을 끝내 alpha가 busy에서 벗어나야 refresh가 대기열이
            // 아니라 즉시 제출된다.
            gitman::query_completed_event local {};
            local.id.value = u8"alpha";
            local.generation = 1;
            local.final_event = true;
            local.result.snapshot.project.value = u8"alpha";
            local.result.snapshot.kind = gitman::repository_kind::git;
            local.result.snapshot.availability = gitman::repository_availability::ready;
            controller.handle(std::move(local));
            submitter.requests.clear();
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
} // namespace

TEST_CASE("Opening the settings dialog copies the document settings into the drafts", "[logic][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog.has_value());
    REQUIRE(view->settings_dialog->git_path == u8"C:\\tools\\git\\git.exe");
    REQUIRE(view->settings_dialog->svn_path.empty());
    REQUIRE(view->settings_dialog->can_confirm);
    REQUIRE(view->settings_dialog->message.empty());
}

TEST_CASE("The settings dialog does not open without a document", "[logic][settings-ui]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::open_settings_intent {});

    REQUIRE(controller.make_view_snapshot()->settings_dialog.has_value() == false);
}

TEST_CASE("Executable intents edit the drafts of the addressed tool only", "[logic][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::subversion, u8"C:\\tools\\svn\\svn.exe" });
    fixture.controller.handle(gitman::clear_settings_executable_intent { gitman::repository_kind::git });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog.has_value());
    REQUIRE(view->settings_dialog->git_path.empty());
    REQUIRE(view->settings_dialog->svn_path == u8"C:\\tools\\svn\\svn.exe");

    // 문서 자체는 저장 전까지 바뀌지 않는다. 저장 요청도 아직 없다.
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("A late picker result after closing the dialog is discarded", "[logic][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::cancel_settings_dialog_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, u8"C:\\tools\\other\\git.exe" });

    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog.has_value() == false);

    // 다시 열면 초안은 여전히 문서의 값이다.
    fixture.controller.handle(gitman::open_settings_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->git_path == u8"C:\\tools\\git\\git.exe");
}

TEST_CASE("A relative draft path blocks confirmation with a Korean message", "[logic][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, u8"tools\\git.exe" });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog.has_value());
    REQUIRE(view->settings_dialog->can_confirm == false);
    REQUIRE(view->settings_dialog->message.empty() == false);

    // 버튼 비활성과 별개로 늦은 확인 intent도 저장을 만들지 않는다.
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.empty());
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog.has_value());
}

TEST_CASE("Confirming changed settings saves the document and refreshes enabled cards", "[logic][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, u8"C:\\tools\\new\\git.exe" });
    fixture.controller.handle(gitman::confirm_settings_intent {});

    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog.has_value() == false);
    REQUIRE(fixture.submitter.requests.size() == 2u);

    const gitman::operation_request& save { fixture.submitter.requests[0] };
    REQUIRE(save.kind == gitman::operation_kind::save_document);
    REQUIRE(save.document.has_value());
    REQUIRE(save.document->settings.git_executable == u8"C:\\tools\\new\\git.exe");

    // 비활성 카드 beta는 재조회하지 않고, refresh 요청에는 새 settings 사본이 실린다.
    const gitman::operation_request& refresh { fixture.submitter.requests[1] };
    REQUIRE(refresh.kind == gitman::operation_kind::refresh);
    REQUIRE(refresh.project.id.value == u8"alpha");
    REQUIRE(refresh.settings.git_executable == u8"C:\\tools\\new\\git.exe");
}

TEST_CASE("Confirming unchanged settings closes the dialog without saving or refreshing", "[logic][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::confirm_settings_intent {});

    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog.has_value() == false);
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("Cancelling the settings dialog keeps the document settings", "[logic][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, u8"C:\\tools\\other\\git.exe" });
    fixture.controller.handle(gitman::cancel_settings_dialog_intent {});

    REQUIRE(fixture.submitter.requests.empty());
    fixture.controller.handle(gitman::open_settings_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->git_path == u8"C:\\tools\\git\\git.exe");
}

TEST_CASE("The settings dialog elements register the picker commands and intents", "[ui][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::ui::build_ui_tree(*view) };

    REQUIRE(tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_dialog }) != nullptr);

    // 찾아보기는 UI thread의 파일 선택 명령이다.
    const std::vector<gitman::ui::input_action> browse { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_git_browse }) };
    REQUIRE(browse.size() == 1u);
    const auto* const command { std::get_if<gitman::ui::ui_command>(&browse.front()) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == gitman::ui::ui_command::show_git_executable_picker);

    // Git 경로가 지정되어 있으므로 지우기는 활성이고 intent를 반환한다.
    const gitman::ui::ui_element* const git_clear { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_git_clear }) };
    REQUIRE(git_clear != nullptr);
    REQUIRE(git_clear->enabled());
    const std::vector<gitman::ui::input_action> clear_actions { click(*tree, git_clear->id()) };
    REQUIRE(clear_actions.size() == 1u);
    const auto* const clear_message { std::get_if<gitman::logic_message>(&clear_actions.front()) };
    REQUIRE(clear_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::clear_settings_executable_intent>(*clear_message));

    // SVN 경로는 비어 있어 지우기가 비활성이다.
    const gitman::ui::ui_element* const svn_clear { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_svn_clear }) };
    REQUIRE(svn_clear != nullptr);
    REQUIRE(svn_clear->enabled() == false);
}

TEST_CASE("The settings dialog association buttons request the UI commands", "[ui][settings-ui][association]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    const std::vector<gitman::ui::input_action> associate { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_associate }) };
    REQUIRE(associate.size() == 1u);
    const auto* const associate_command { std::get_if<gitman::ui::ui_command>(&associate.front()) };
    REQUIRE(associate_command != nullptr);
    REQUIRE(*associate_command == gitman::ui::ui_command::register_file_association);

    const std::vector<gitman::ui::input_action> dissociate { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_dissociate }) };
    REQUIRE(dissociate.size() == 1u);
    const auto* const dissociate_command { std::get_if<gitman::ui::ui_command>(&dissociate.front()) };
    REQUIRE(dissociate_command != nullptr);
    REQUIRE(*dissociate_command == gitman::ui::ui_command::unregister_file_association);
}

TEST_CASE("The toolbar settings button opens the dialog only with a document", "[ui][settings-ui]")
{
    settings_fixture fixture {};
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    const std::vector<gitman::ui::input_action> actions { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::toolbar_settings }) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::open_settings_intent>(*message));

    // 문서가 없으면 버튼이 비활성이다.
    gitman::view_snapshot empty {};
    empty.window_width = 800.0f;
    empty.window_height = 600.0f;
    empty.scale = 1.0f;
    const auto empty_tree { gitman::ui::build_ui_tree(empty) };
    const gitman::ui::ui_element* const button { empty_tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::toolbar_settings }) };
    REQUIRE(button != nullptr);
    REQUIRE(button->enabled() == false);
}

TEST_CASE("Escape closes the settings dialog before clearing the selection", "[ui][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    gitman::ui::interaction_controller interaction {};
    interaction.set_tree(tree);
    const std::vector<gitman::ui::input_action> actions { interaction.process(gitman::ui::key_pressed_event { gitman::ui::key_code::escape }) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_settings_dialog_intent>(*message));
}
