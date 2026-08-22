#include "application/logic_controller.h"
#include "presentation/ui/build_ui_tree.h"
#include "presentation/ui/settings_dialog_element.h"
#include "presentation/ui/ui_interaction.h"
#include "presentation/ui_theme.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstddef>
#include <optional>
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

    // 마지막으로 제출된 그 종류의 요청이다. 없으면 nullptr다.
    const gitman::operation_request* last_of(const recording_submitter& submitter, const gitman::operation_kind kind)
    {
        const gitman::operation_request* found { nullptr };
        for (const gitman::operation_request& request : submitter.requests)
            if (request.kind == kind)
                found = &request;
        return found;
    }

    std::chrono::steady_clock::time_point at(const int milliseconds)
    {
        return std::chrono::steady_clock::time_point {} + std::chrono::milliseconds { milliseconds };
    }

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

TEST_CASE("The settings dialog opens without a document and edits the global settings", "[logic][settings-ui]")
{
    // 문서가 없으면 전역 설정을 편집한다 (G3.2). 저장은 앱 설정 파일로 나가고,
    // 카드가 없으므로 재조회는 없다.
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.start();
    const std::uint64_t load_id { submitter.requests.front().operation_id };
    gitman::app_settings_loaded_event loaded_settings {};
    loaded_settings.operation_id = load_id;
    controller.handle(gitman::logic_message { std::move(loaded_settings) });
    submitter.requests.clear();

    controller.handle(gitman::open_settings_intent {});
    {
        const auto view { controller.make_view_snapshot() };
        REQUIRE(view->settings_dialog.has_value());
        REQUIRE(view->settings_dialog->document_mode == false);
        REQUIRE(view->settings_dialog->git_follows_app == false);
    }

    controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, u8"C:\\tools\\git\\git.exe" });
    controller.handle(gitman::toggle_settings_submodules_intent {});
    controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(controller.make_view_snapshot()->settings_dialog.has_value() == false);

    // 전역 값이 앱 설정 저장에 실린다.
    const gitman::operation_request* save { nullptr };
    for (const gitman::operation_request& request : submitter.requests)
        if (request.kind == gitman::operation_kind::save_app_settings)
            save = &request;
    REQUIRE(save != nullptr);
    REQUIRE(save->app_settings_payload.has_value());
    REQUIRE(save->app_settings_payload->settings.git_executable == u8"C:\\tools\\git\\git.exe");
    REQUIRE(save->app_settings_payload->settings.update_submodules);

    // 이후 문서 없이 다시 열면 저장된 전역 값이 초안으로 보인다.
    controller.handle(gitman::open_settings_intent {});
    REQUIRE(controller.make_view_snapshot()->settings_dialog->git_path == u8"C:\\tools\\git\\git.exe");
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

TEST_CASE("Untouched document rows follow the app settings and touched ones override", "[logic][settings-ui]")
{
    // 문서 모드의 암묵 덮어쓰기다 (G3.2): 건드린 행만 문서에 남고, 나머지는 앱
    // 설정을 따른다.
    settings_fixture fixture {};
    fixture.controller.start();
    REQUIRE(fixture.submitter.requests.size() == 1u);
    const std::uint64_t load_id { fixture.submitter.requests.front().operation_id };
    gitman::app_settings stored {};
    stored.settings.svn_executable = u8"C:\\tools\\svn\\svn.exe";
    stored.settings.update_submodules = true;
    gitman::app_settings_loaded_event loaded_settings {};
    loaded_settings.operation_id = load_id;
    loaded_settings.settings = std::move(stored);
    fixture.controller.handle(gitman::logic_message { std::move(loaded_settings) });
    fixture.submitter.requests.clear();

    fixture.controller.handle(gitman::open_settings_intent {});
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->settings_dialog->document_mode);
        // 문서가 정의한 git은 override 값이고, 정의하지 않은 svn·submodule은 앱
        // 값이 "따름"으로 보인다.
        REQUIRE(view->settings_dialog->git_follows_app == false);
        REQUIRE(view->settings_dialog->svn_follows_app);
        REQUIRE(view->settings_dialog->svn_path == u8"C:\\tools\\svn\\svn.exe");
        REQUIRE(view->settings_dialog->submodules_follows_app);
        REQUIRE(view->settings_dialog->update_submodules);
    }

    // 건드리지 않고 확인하면 아무것도 저장·재조회하지 않는다.
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.empty());

    // ignore_local만 건드리면 그 행만 문서 override가 된다. 재조회 요청에는 전역
    // svn과 문서 git이 합성된 유효 설정이 실린다.
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::toggle_settings_ignore_local_intent {});
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.size() == 2u);
    const gitman::operation_request& save { fixture.submitter.requests[0] };
    REQUIRE(save.kind == gitman::operation_kind::save_document);
    REQUIRE(save.document->settings.ignore_local_changes == std::optional<bool> { true });
    REQUIRE(save.document->settings.svn_executable.has_value() == false);
    REQUIRE(save.document->settings.update_submodules.has_value() == false);
    const gitman::operation_request& refresh { fixture.submitter.requests[1] };
    REQUIRE(refresh.kind == gitman::operation_kind::refresh);
    REQUIRE(refresh.settings.git_executable == u8"C:\\tools\\git\\git.exe");
    REQUIRE(refresh.settings.svn_executable == u8"C:\\tools\\svn\\svn.exe");
    REQUIRE(refresh.settings.update_submodules);
    REQUIRE(refresh.settings.ignore_local_changes);
}

TEST_CASE("Clearing a document executable defines an empty override", "[logic][settings-ui]")
{
    // 지우기는 두 모드 모두 빈 값(자동 탐색)이며, 문서 모드에서는 "빈 값으로
    // 덮어씀"이 된다. 문서 정의 삭제는 배지의 몫이다 (G3.2).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::clear_settings_executable_intent { gitman::repository_kind::git });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->settings_dialog->git_follows_app == false);
        REQUIRE(view->settings_dialog->git_path.empty());
    }

    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.size() == 2u);
    REQUIRE(fixture.submitter.requests[0].kind == gitman::operation_kind::save_document);
    REQUIRE(fixture.submitter.requests[0].document->settings.git_executable == std::optional<std::u8string> { std::u8string {} });
    REQUIRE(fixture.submitter.requests[1].settings.git_executable.empty());
}

TEST_CASE("The override badge deletes a document override and follows the app settings", "[logic][settings-ui]")
{
    // `덮어씀` 배지 클릭이 그 행의 문서 정의를 지운다 (2026-08-22 지시). 이
    // fixture의 앱 설정은 기본값(자동 탐색)이다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->git_follows_app == false);

    fixture.controller.handle(gitman::clear_settings_override_intent { gitman::settings_override_field::git_executable });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->settings_dialog->git_follows_app);
        // 초안에는 앱의 값(기본값 = 자동 탐색)이 다시 보인다.
        REQUIRE(view->settings_dialog->git_path.empty());
    }

    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.size() == 2u);
    REQUIRE(fixture.submitter.requests[0].kind == gitman::operation_kind::save_document);
    REQUIRE(fixture.submitter.requests[0].document->settings.git_executable.has_value() == false);
    REQUIRE(fixture.submitter.requests[1].settings.git_executable.empty());
}

TEST_CASE("Every document item shows a scope badge and only overrides are clickable", "[ui][settings-ui]")
{
    // 문서 모드에서는 모든 항목이 범위 배지를 갖는다. 문서가 덮어쓴 항목(git)만
    // 누를 수 있고, 전역을 따르는 항목(svn)은 액션이 없다 (D2).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    const gitman::ui::ui_element_id git_badge { gitman::ui::ui_element_kind::settings_override_badge, gitman::project_id { u8"git" } };
    REQUIRE(tree->find(git_badge) != nullptr);
    const gitman::ui::ui_element* const svn_badge { tree->find({ gitman::ui::ui_element_kind::settings_override_badge, gitman::project_id { u8"svn" } }) };
    REQUIRE(svn_badge != nullptr);
    // 전역 배지는 눌러도 아무 일도 하지 않는다. tooltip으로만 뜻을 알린다.
    REQUIRE(svn_badge->action(gitman::ui::ui_trigger::left_click) == nullptr);
    REQUIRE_FALSE(svn_badge->tooltip().empty());

    const std::vector<gitman::ui::input_action> actions { click(*tree, git_badge) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::clear_settings_override_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->field == gitman::settings_override_field::git_executable);

    // 전역 모드에는 배지가 없다.
    recording_submitter global_submitter {};
    gitman::logic_controller global_controller { global_submitter };
    global_controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    global_controller.handle(gitman::open_settings_intent {});
    const auto global_tree { gitman::ui::build_ui_tree(*global_controller.make_view_snapshot()) };
    REQUIRE(global_tree->find(git_badge) == nullptr);
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
    // 파일 연결은 시스템 탭이다 (settings-tabs-and-appearance-scope-design S1.2).
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::system });
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

TEST_CASE("The toolbar settings button opens the dialog with or without a document", "[ui][settings-ui]")
{
    settings_fixture fixture {};
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    const std::vector<gitman::ui::input_action> actions { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::toolbar_settings }) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::open_settings_intent>(*message));

    // 문서가 없어도 전역 설정 진입점으로 항상 활성이다 (G3.2).
    gitman::view_snapshot empty {};
    empty.window_width = 800.0f;
    empty.window_height = 600.0f;
    empty.scale = 1.0f;
    const auto empty_tree { gitman::ui::build_ui_tree(empty) };
    const gitman::ui::ui_element* const button { empty_tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::toolbar_settings }) };
    REQUIRE(button != nullptr);
    REQUIRE(button->enabled());
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

TEST_CASE("The settings dialog shows one tab at a time and keeps the drafts", "[ui][settings-ui][tabs]")
{
    // 탭은 보기 상태다 — 옮겨도 다른 탭의 초안은 남는다
    // (settings-tabs-and-appearance-scope-design S1.2).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});

    const auto tools { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    // 네 탭 모두 rail에 있고, 기본 탭은 도구다.
    for (const std::u8string_view name : { std::u8string_view { u8"tools" }, std::u8string_view { u8"operations" }, std::u8string_view { u8"appearance" }, std::u8string_view { u8"system" } })
        REQUIRE(tools->find({ gitman::ui::ui_element_kind::settings_tab_item, gitman::project_id { std::u8string { name } } }) != nullptr);
    REQUIRE(tools->find({ gitman::ui::ui_element_kind::settings_git_browse }) != nullptr);
    REQUIRE(tools->find({ gitman::ui::ui_element_kind::settings_timeout_input }) != nullptr);
    // 다른 탭의 컨트롤은 만들어지지 않는다.
    REQUIRE(tools->find({ gitman::ui::ui_element_kind::settings_submodules_toggle }) == nullptr);
    REQUIRE(tools->find({ gitman::ui::ui_element_kind::settings_associate }) == nullptr);
    REQUIRE(tools->ids_of_kind(gitman::ui::ui_element_kind::settings_accent_swatch).empty());

    // 초안을 고친 뒤 탭을 옮겨도 값은 남는다.
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::subversion, u8"C:\\tools\\svn.exe" });
    const std::vector<gitman::ui::input_action> actions {
        click(*tools, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_tab_item, gitman::project_id { u8"operations" } }),
    };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::select_settings_tab_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->tab == gitman::settings_tab::operations);
    fixture.controller.handle(*message);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog->active_tab == gitman::settings_tab::operations);
    REQUIRE(view->settings_dialog->svn_path == u8"C:\\tools\\svn.exe");

    const auto operations { gitman::ui::build_ui_tree(*view) };
    REQUIRE(operations->find({ gitman::ui::ui_element_kind::settings_submodules_toggle }) != nullptr);
    REQUIRE(operations->find({ gitman::ui::ui_element_kind::settings_log_files_toggle }) != nullptr);
    REQUIRE(operations->find({ gitman::ui::ui_element_kind::settings_git_browse }) == nullptr);

    // panel 높이는 탭을 옮겨도 같다 — 창이 튀지 않는다 (S1.3).
    REQUIRE(operations->find({ gitman::ui::ui_element_kind::settings_dialog_panel })->bounds().height == tools->find({ gitman::ui::ui_element_kind::settings_dialog_panel })->bounds().height);
}

TEST_CASE("The scope badge sits above the item title without moving the controls", "[ui][settings-ui]")
{
    // 배지는 항목 제목 바로 위 왼쪽이고, 행 컨트롤은 제목 줄 오른쪽 끝을 그대로
    // 쓴다 (E3).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    const gitman::ui::ui_element* const badge { tree->find({ gitman::ui::ui_element_kind::settings_override_badge, gitman::project_id { u8"git" } }) };
    REQUIRE(badge != nullptr);
    const gitman::ui::rect_f badge_bounds { badge->bounds() };
    REQUIRE(badge_bounds.height >= 16.0f);

    // 같은 항목의 지우기 버튼보다 위에 있고 줄이 겹치지 않는다.
    const gitman::ui::rect_f clear { tree->find({ gitman::ui::ui_element_kind::settings_git_clear })->bounds() };
    REQUIRE(badge_bounds.y + badge_bounds.height <= clear.y);
    // 배지는 항목 왼쪽 끝에서 시작한다.
    const gitman::ui::rect_f panel { tree->find({ gitman::ui::ui_element_kind::settings_dialog_panel })->bounds() };
    REQUIRE(badge_bounds.x > panel.x);
    REQUIRE(badge_bounds.x + badge_bounds.width < clear.x);

    // 컨트롤은 전역 모드와 같은 오른쪽 끝에 있다 — 배지 때문에 밀리지 않는다.
    recording_submitter global_submitter {};
    gitman::logic_controller global_controller { global_submitter };
    global_controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    global_controller.handle(gitman::open_settings_intent {});
    const auto global_tree { gitman::ui::build_ui_tree(*global_controller.make_view_snapshot()) };
    const gitman::ui::rect_f global_clear { global_tree->find({ gitman::ui::ui_element_kind::settings_git_clear })->bounds() };
    const gitman::ui::rect_f global_panel { global_tree->find({ gitman::ui::ui_element_kind::settings_dialog_panel })->bounds() };
    REQUIRE(clear.x - panel.x == global_clear.x - global_panel.x);

    // 외양 항목도 같은 규칙이다. 초안을 고치면 `문서 설정`이 되어 누를 수 있다 (D4).
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });
    const auto follows { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    const gitman::ui::ui_element* const following_badge { follows->find({ gitman::ui::ui_element_kind::settings_override_badge, gitman::project_id { u8"accent" } }) };
    REQUIRE(following_badge != nullptr);
    REQUIRE(following_badge->action(gitman::ui::ui_trigger::left_click) == nullptr);

    fixture.controller.handle(gitman::set_accent_intent { u8"blue" });
    const auto appearance { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    const gitman::ui::ui_element* const accent_badge { appearance->find({ gitman::ui::ui_element_kind::settings_override_badge, gitman::project_id { u8"accent" } }) };
    REQUIRE(accent_badge != nullptr);
    REQUIRE(accent_badge->action(gitman::ui::ui_trigger::left_click) != nullptr);

    const std::vector<gitman::ui::input_action> cleared { click(*appearance, accent_badge->id()) };
    REQUIRE(cleared.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&cleared.front()) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::clear_settings_override_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->field == gitman::settings_override_field::accent);
}

TEST_CASE("Every settings control stays inside the panel on all tabs", "[ui][settings-ui][tabs]")
{
    // 항목 높이가 늘어도(색 20개) panel 안에 담겨야 한다 — panel 높이는 가장 높은
    // 탭에 맞춘다 (settings-tabs-and-appearance-scope-design S1.3).
    const std::vector<gitman::settings_tab> tabs {
        gitman::settings_tab::tools,
        gitman::settings_tab::operations,
        gitman::settings_tab::appearance,
        gitman::settings_tab::system,
    };

    for (const bool document_mode : { true, false })
        for (const gitman::settings_tab tab : tabs)
        {
            recording_submitter submitter {};
            gitman::logic_controller controller { submitter };
            controller.handle(gitman::window_metrics_intent { 1280.0f, 800.0f, 1.0f });
            if (document_mode)
            {
                controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
                gitman::document_loaded_event loaded {};
                gitman::workspace_document document {};
                document.document_path = u8"C:\\work\\p.version-list";
                document.settings.git_executable = u8"C:\\tools\\git.exe";
                document.appearance.accent_id = { u8"blue" };
                loaded.document = { std::move(document) };
                controller.handle(std::move(loaded));
            }
            controller.handle(gitman::open_settings_intent {});
            controller.handle(gitman::select_settings_tab_intent { tab });

            const auto tree { gitman::ui::build_ui_tree(*controller.make_view_snapshot()) };
            const gitman::ui::ui_element* const panel { tree->find({ gitman::ui::ui_element_kind::settings_dialog_panel }) };
            REQUIRE(panel != nullptr);
            const gitman::ui::rect_f box { panel->bounds() };

            const std::vector<gitman::ui::ui_element_kind> kinds {
                gitman::ui::ui_element_kind::settings_tab_item,
                gitman::ui::ui_element_kind::settings_git_browse,
                gitman::ui::ui_element_kind::settings_git_clear,
                gitman::ui::ui_element_kind::settings_svn_browse,
                gitman::ui::ui_element_kind::settings_svn_clear,
                gitman::ui::ui_element_kind::settings_timeout_input,
                gitman::ui::ui_element_kind::settings_submodules_toggle,
                gitman::ui::ui_element_kind::settings_ignore_local_toggle,
                gitman::ui::ui_element_kind::settings_log_files_toggle,
                gitman::ui::ui_element_kind::settings_theme_option,
                gitman::ui::ui_element_kind::settings_accent_swatch,
                gitman::ui::ui_element_kind::settings_override_badge,
                gitman::ui::ui_element_kind::settings_associate,
                gitman::ui::ui_element_kind::settings_dissociate,
                gitman::ui::ui_element_kind::settings_dialog_confirm,
                gitman::ui::ui_element_kind::settings_dialog_cancel,
            };
            for (const gitman::ui::ui_element_kind kind : kinds)
                for (const gitman::ui::ui_element_id& id : tree->ids_of_kind(kind))
                {
                    const gitman::ui::rect_f bounds { tree->find(id)->bounds() };
                    REQUIRE(bounds.x >= box.x);
                    REQUIRE(bounds.y >= box.y);
                    REQUIRE(bounds.x + bounds.width <= box.x + box.width);
                    REQUIRE(bounds.y + bounds.height <= box.y + box.height);
                }
        }
}

TEST_CASE("Timeout characters edit the draft and only digits count", "[logic][settings-ui][timeout]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});

    // 기본 상태에서는 초안이 비어 있다 (기본값 표기).
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout_text.empty());

    // 숫자는 뒤에 붙고, 숫자가 아닌 문자는 무시되고, backspace는 마지막 자리를
    // 지운다. 최대값(3600)이 4자리라 다섯 자리째는 버린다.
    for (const char32_t character : { U'9', U'a', U'0', U'힣', U'0', U' ', U'0', U'1' })
        fixture.controller.handle(gitman::edit_settings_timeout_intent { character });
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout_text == u8"9000");

    fixture.controller.handle(gitman::edit_settings_timeout_intent { U'\b' });
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout_text == u8"900");
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("An out of range timeout blocks confirmation with a Korean message", "[logic][settings-ui][timeout]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::edit_settings_timeout_intent { U'5' });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog->can_confirm == false);
    REQUIRE(view->settings_dialog->message.empty() == false);

    // 버튼 비활성과 별개로 늦은 확인 intent도 저장을 만들지 않는다.
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.empty());
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog.has_value());
}

TEST_CASE("Confirming a timeout change saves it and carries it on refresh requests", "[logic][settings-ui][timeout]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    for (const char32_t character : { U'1', U'8', U'0', U'0' })
        fixture.controller.handle(gitman::edit_settings_timeout_intent { character });
    fixture.controller.handle(gitman::confirm_settings_intent {});

    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog.has_value() == false);
    REQUIRE(fixture.submitter.requests.size() == 2u);

    const gitman::operation_request& save { fixture.submitter.requests[0] };
    REQUIRE(save.kind == gitman::operation_kind::save_document);
    REQUIRE(save.document.has_value());
    REQUIRE(save.document->settings.query_timeout_seconds == 1800);

    const gitman::operation_request& refresh { fixture.submitter.requests[1] };
    REQUIRE(refresh.kind == gitman::operation_kind::refresh);
    REQUIRE(refresh.settings.query_timeout_seconds == 1800);

    // 다시 열면 저장한 값이 초안 텍스트로 돌아온다. (빈 초안의 기본값 복원 저장은
    // store 테스트가 필드 제거로 검증한다.)
    fixture.controller.handle(gitman::open_settings_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout_text == u8"1800");
}

TEST_CASE("Typed characters reach the timeout box only while it has focus", "[ui][settings-ui][timeout]")
{
    settings_fixture fixture {};

    // dialog가 없으면 문자 입력은 버려진다.
    {
        const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
        gitman::ui::interaction_controller interaction {};
        interaction.set_tree(tree);
        REQUIRE(interaction.process(gitman::ui::character_typed_event { U'5' }).empty());
    }

    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    // 텍스트 박스 element가 존재하고 안내 tooltip을 가진다.
    const gitman::ui::ui_element* const input { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_timeout_input }) };
    REQUIRE(input != nullptr);
    REQUIRE(input->visible());
    REQUIRE(input->tooltip().empty() == false);

    // dialog가 열려 있어도 초점이 없으면 문자는 버려진다 (자동 초점 없음).
    gitman::ui::interaction_controller interaction {};
    interaction.set_tree(tree);
    REQUIRE(interaction.process(gitman::ui::character_typed_event { U'5' }).empty());

    // 텍스트 박스를 누르면 초점이 생기고 문자가 편집 intent로 바뀐다.
    const gitman::ui::rect_f box { input->bounds() };
    static_cast<void>(interaction.process(gitman::ui::pointer_pressed_event { box.x + 2.0f, box.y + 2.0f, gitman::ui::pointer_button::left, at(0) }));
    static_cast<void>(interaction.process(gitman::ui::pointer_released_event { box.x + 2.0f, box.y + 2.0f, gitman::ui::pointer_button::left, at(10) }));
    const std::vector<gitman::ui::input_action> actions { interaction.process(gitman::ui::character_typed_event { U'5' }) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::edit_settings_timeout_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->character == U'5');
}

TEST_CASE("Focus follows presses and carries the caret phase origin", "[ui][settings-ui][timeout]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
    gitman::ui::interaction_controller interaction {};
    interaction.set_tree(tree);

    // 열림만으로는 초점이 없다 (검수 지시: 자동 초점 없음).
    REQUIRE(interaction.process(gitman::ui::pointer_moved_event { 1.0f, 1.0f, at(0) }).empty());
    REQUIRE(interaction.snapshot().focused_input == gitman::ui::ui_element_id {});
    REQUIRE(interaction.snapshot().focus_started_at.has_value() == false);

    // 텍스트 박스를 누르면 초점과 caret 위상 기준 시각이 생긴다.
    const gitman::ui::rect_f input { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_timeout_input })->bounds() };
    static_cast<void>(interaction.process(gitman::ui::pointer_pressed_event { input.x + 2.0f, input.y + 2.0f, gitman::ui::pointer_button::left, at(20) }));
    static_cast<void>(interaction.process(gitman::ui::pointer_released_event { input.x + 2.0f, input.y + 2.0f, gitman::ui::pointer_button::left, at(30) }));
    REQUIRE(interaction.snapshot().focused_input.kind == gitman::ui::ui_element_kind::settings_timeout_input);
    REQUIRE(interaction.snapshot().focus_started_at == at(20));

    // 다른 곳(패널)을 누르면 초점이 풀리고 문자 입력이 버려진다.
    const gitman::ui::rect_f panel { tree->find(gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_dialog_panel })->bounds() };
    static_cast<void>(interaction.process(gitman::ui::pointer_pressed_event { panel.x + 2.0f, panel.y + 2.0f, gitman::ui::pointer_button::left, at(40) }));
    static_cast<void>(interaction.process(gitman::ui::pointer_released_event { panel.x + 2.0f, panel.y + 2.0f, gitman::ui::pointer_button::left, at(50) }));
    REQUIRE(interaction.snapshot().focused_input == gitman::ui::ui_element_id {});
    REQUIRE(interaction.snapshot().focus_started_at.has_value() == false);
    REQUIRE(interaction.process(gitman::ui::character_typed_event { U'5' }).empty());

    // dialog가 닫히면 남아 있던 초점도 거둔다.
    static_cast<void>(interaction.process(gitman::ui::pointer_pressed_event { input.x + 2.0f, input.y + 2.0f, gitman::ui::pointer_button::left, at(60) }));
    static_cast<void>(interaction.process(gitman::ui::pointer_released_event { input.x + 2.0f, input.y + 2.0f, gitman::ui::pointer_button::left, at(70) }));
    REQUIRE(interaction.snapshot().focused_input.kind == gitman::ui::ui_element_kind::settings_timeout_input);
    fixture.controller.handle(gitman::cancel_settings_dialog_intent {});
    interaction.set_tree(gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()));
    REQUIRE(interaction.process(gitman::ui::pointer_moved_event { 1.0f, 1.0f, at(80) }).empty());
    REQUIRE(interaction.snapshot().focused_input == gitman::ui::ui_element_id {});
}

TEST_CASE("The submodule toggle edits the draft and confirm saves it", "[logic][settings-ui][submodule]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    // 토글은 작업 탭이다 (S1.2).
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::operations });
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->update_submodules == false);

    // 토글 버튼이 intent를 보내고 초안이 뒤집힌다.
    {
        const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
        const std::vector<gitman::ui::input_action> actions { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_submodules_toggle }) };
        const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::toggle_settings_submodules_intent>(*message));
    }
    fixture.controller.handle(gitman::toggle_settings_submodules_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->update_submodules);

    // 저장하면 문서 settings에 남고, 이후 update 요청이 이 값을 쓴다.
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.size() >= 1u);
    const gitman::operation_request& save { fixture.submitter.requests[0] };
    REQUIRE(save.kind == gitman::operation_kind::save_document);
    REQUIRE(save.document->settings.update_submodules);

    // 저장이 예약한 재조회(refresh)를 끝내 카드 busy를 풀고 update를 요청한다.
    gitman::query_completed_event refreshed {};
    refreshed.id.value = u8"alpha";
    refreshed.generation = 2;
    refreshed.final_event = true;
    refreshed.result.snapshot.project.value = u8"alpha";
    refreshed.result.snapshot.kind = gitman::repository_kind::git;
    refreshed.result.snapshot.availability = gitman::repository_availability::ready;
    fixture.controller.handle(std::move(refreshed));

    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::request_update_intent { gitman::project_id { u8"alpha" }, {} });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().options.update_submodules);
}

TEST_CASE("The log file toggle edits the draft and confirm saves it", "[logic][settings-ui][log-file]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::operations });
    // 기본값은 켬이다 (app-shell-design A4.5).
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->write_log_files);

    {
        const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };
        const std::vector<gitman::ui::input_action> actions { click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_log_files_toggle }) };
        const auto* const message { std::get_if<gitman::logic_message>(&actions.front()) };
        REQUIRE(message != nullptr);
        REQUIRE(std::holds_alternative<gitman::toggle_settings_log_files_intent>(*message));
    }
    fixture.controller.handle(gitman::toggle_settings_log_files_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->write_log_files == false);

    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.requests.size() >= 1u);
    const gitman::operation_request& save { fixture.submitter.requests[0] };
    REQUIRE(save.kind == gitman::operation_kind::save_document);
    REQUIRE(save.document->settings.write_log_files == false);
}

TEST_CASE("The appearance draft lands in the document on save", "[logic][settings-ui][theme]")
{
    // 외양도 전역/문서로 나뉘고, 다른 항목과 같이 `저장`에서만 반영된다 (S2.3, D4).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_theme_preference_intent { gitman::theme_preference::light });
    fixture.controller.handle(gitman::set_accent_intent { u8"blue" });

    // dialog가 열려 있는 동안에는 초안을 미리 보여 준다 (E2). 문서는 그대로다.
    const auto drafted { fixture.controller.make_view_snapshot() };
    REQUIRE(drafted->appearance.theme == gitman::theme_preference::light);
    REQUIRE(drafted->appearance.accent_id == u8"blue");
    REQUIRE(drafted->settings_dialog->theme == gitman::theme_preference::light);
    REQUIRE(drafted->settings_dialog->accent_id == u8"blue");
    REQUIRE_FALSE(drafted->settings_dialog->theme_follows_app);
    REQUIRE_FALSE(drafted->settings_dialog->accent_follows_app);
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) == nullptr);

    fixture.controller.handle(gitman::confirm_settings_intent {});
    const gitman::operation_request* const saved { last_of(fixture.submitter, gitman::operation_kind::save_document) };
    REQUIRE(saved != nullptr);
    REQUIRE(saved->document.has_value());
    REQUIRE(saved->document->appearance.theme == std::optional<gitman::theme_preference> { gitman::theme_preference::light });
    REQUIRE(saved->document->appearance.accent_id == std::optional<std::u8string> { u8"blue" });
    // 앱 설정은 그대로다 — 문서가 열려 있으면 문서 override다.
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_app_settings) == nullptr);

    // 저장 뒤의 유효 값은 문서 override다.
    const auto applied { fixture.controller.make_view_snapshot() };
    REQUIRE(applied->appearance.theme == gitman::theme_preference::light);
    REQUIRE(applied->appearance.accent_id == u8"blue");

    // 배지가 문서 정의를 거두면 초안이 앱 값으로 돌아가고, 저장이 문서에서 지운다.
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::clear_settings_override_intent { gitman::settings_override_field::theme });
    fixture.controller.handle(gitman::clear_settings_override_intent { gitman::settings_override_field::accent });
    const auto cleared { fixture.controller.make_view_snapshot() };
    REQUIRE(cleared->settings_dialog->theme_follows_app);
    REQUIRE(cleared->settings_dialog->accent_follows_app);
    REQUIRE(cleared->settings_dialog->theme == gitman::theme_preference::system);

    fixture.controller.handle(gitman::confirm_settings_intent {});
    const auto followed { fixture.controller.make_view_snapshot() };
    REQUIRE(followed->appearance.theme == gitman::theme_preference::system);
    REQUIRE(followed->appearance.accent_id == gitman::default_accent_id);
}

TEST_CASE("Cancelling the settings dialog rolls the appearance preview back", "[logic][settings-ui][theme]")
{
    // 고르는 즉시 미리 보여 주되, 취소하면 원래 값으로 돌아간다 (E2).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_theme_preference_intent { gitman::theme_preference::dark });
    fixture.controller.handle(gitman::set_accent_intent { u8"blue" });

    const auto previewed { fixture.controller.make_view_snapshot() };
    REQUIRE(previewed->appearance.theme == gitman::theme_preference::dark);
    REQUIRE(previewed->appearance.accent_id == u8"blue");
    // 미리 보기는 저장을 일으키지 않는다.
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) == nullptr);

    fixture.controller.handle(gitman::cancel_settings_dialog_intent {});

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->appearance.theme == gitman::theme_preference::system);
    REQUIRE(view->appearance.accent_id == gitman::default_accent_id);
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) == nullptr);

    // 다시 열면 초안도 유효 값에서 시작한다.
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto reopened { fixture.controller.make_view_snapshot() };
    REQUIRE(reopened->settings_dialog->theme == gitman::theme_preference::system);
    REQUIRE(reopened->settings_dialog->accent_id == gitman::default_accent_id);
}

TEST_CASE("The appearance edits the app settings without a document", "[logic][settings-ui][theme]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    controller.start();
    gitman::app_settings_loaded_event loaded {};
    loaded.operation_id = submitter.requests.front().operation_id;
    controller.handle(std::move(loaded));

    controller.handle(gitman::open_settings_intent {});
    controller.handle(gitman::set_accent_intent { u8"blue" });
    REQUIRE(last_of(submitter, gitman::operation_kind::save_app_settings) == nullptr);

    controller.handle(gitman::confirm_settings_intent {});
    const gitman::operation_request* const saved { last_of(submitter, gitman::operation_kind::save_app_settings) };
    REQUIRE(saved != nullptr);
    REQUIRE(saved->app_settings_payload.has_value());
    REQUIRE(saved->app_settings_payload->appearance.accent_id == u8"blue");

    controller.handle(gitman::open_settings_intent {});
    const auto view { controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog.has_value());
    REQUIRE_FALSE(view->settings_dialog->document_mode);
    REQUIRE(view->settings_dialog->accent_id == u8"blue");
}

TEST_CASE("The appearance items show the effective values and a wrapped swatch grid", "[ui][settings-ui][theme]")
{
    // 외양은 외양 탭이고, 문서가 열려 있으면 문서 override를 편집한다 (S2.3).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });
    // 초안을 고쳐 두면 그 값이 화면에 보인다 (저장 전에도 dialog는 초안을 보여 준다).
    fixture.controller.handle(gitman::set_accent_intent { u8"blue" });
    fixture.controller.handle(gitman::set_theme_preference_intent { gitman::theme_preference::light });

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog.has_value());
    REQUIRE(view->settings_dialog->document_mode);
    REQUIRE(view->settings_dialog->theme == gitman::theme_preference::light);
    REQUIRE(view->settings_dialog->accent_id == u8"blue");

    const auto tree { gitman::ui::build_ui_tree(*view) };
    const gitman::ui::ui_element_id light { gitman::ui::ui_element_kind::settings_theme_option, gitman::project_id { u8"light" } };
    const gitman::ui::ui_element_id system { gitman::ui::ui_element_kind::settings_theme_option, gitman::project_id { u8"system" } };
    const gitman::ui::ui_element_id dark { gitman::ui::ui_element_kind::settings_theme_option, gitman::project_id { u8"dark" } };
    REQUIRE(tree->find(light) != nullptr);
    REQUIRE(tree->find(system) != nullptr);
    REQUIRE(tree->find(dark) != nullptr);

    // 색 동그라미는 내장 목록만큼 있고, 각자 자기 id를 가진다.
    REQUIRE(tree->ids_of_kind(gitman::ui::ui_element_kind::settings_accent_swatch).size() == gitman::accent_catalog().size());
    for (const gitman::accent_definition& accent : gitman::accent_catalog())
        REQUIRE(tree->find({ gitman::ui::ui_element_kind::settings_accent_swatch, gitman::project_id { std::u8string { accent.id } } }) != nullptr);

    // 세 칸과 동그라미는 서로 겹치지 않고 panel 안에 있다.
    const gitman::ui::rect_f panel { tree->find({ gitman::ui::ui_element_kind::settings_dialog_panel })->bounds() };
    const gitman::ui::rect_f system_bounds { tree->find(system)->bounds() };
    REQUIRE(system_bounds.x >= panel.x);
    REQUIRE(system_bounds.x + system_bounds.width <= panel.x + panel.width);
    REQUIRE(tree->find(light)->bounds().x < system_bounds.x);
    REQUIRE(tree->find(dark)->bounds().x > system_bounds.x);

    // 색 격자는 왼쪽에서 오른쪽으로 채우고 panel을 넘지 않는다. 줄이 바뀌면 x가
    // 처음으로 돌아가고 y가 내려간다 (S2.4).
    std::vector<gitman::ui::rect_f> swatches {};
    for (const gitman::accent_definition& accent : gitman::accent_catalog())
        swatches.push_back(tree->find({ gitman::ui::ui_element_kind::settings_accent_swatch, gitman::project_id { std::u8string { accent.id } } })->bounds());
    for (const gitman::ui::rect_f& swatch : swatches)
    {
        REQUIRE(swatch.x >= panel.x);
        REQUIRE(swatch.x + swatch.width <= panel.x + panel.width);
        REQUIRE(swatch.y + swatch.height <= panel.y + panel.height);
    }
    for (std::size_t index = 1; index < swatches.size(); ++index)
    {
        const bool same_row { swatches[index].y == swatches[index - 1].y };
        if (same_row)
            REQUIRE(swatches[index].x > swatches[index - 1].x);
        else
        {
            REQUIRE(swatches[index].y > swatches[index - 1].y);
            REQUIRE(swatches[index].x == swatches.front().x);
        }
    }
}

TEST_CASE("Appearance clicks preview at once and need the save button to persist", "[ui][settings-ui][theme]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });
    const auto tree { gitman::ui::build_ui_tree(*fixture.controller.make_view_snapshot()) };

    const std::vector<gitman::ui::input_action> theme_actions {
        click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_theme_option, gitman::project_id { u8"dark" } }),
    };
    REQUIRE(theme_actions.size() == 1u);
    const auto* const theme_message { std::get_if<gitman::logic_message>(&theme_actions.front()) };
    REQUIRE(theme_message != nullptr);
    const auto* const theme_intent { std::get_if<gitman::set_theme_preference_intent>(theme_message) };
    REQUIRE(theme_intent != nullptr);
    REQUIRE(theme_intent->theme == gitman::theme_preference::dark);

    const std::vector<gitman::ui::input_action> accent_actions {
        click(*tree, gitman::ui::ui_element_id { gitman::ui::ui_element_kind::settings_accent_swatch, gitman::project_id { u8"blue" } }),
    };
    REQUIRE(accent_actions.size() == 1u);
    const auto* const accent_message { std::get_if<gitman::logic_message>(&accent_actions.front()) };
    REQUIRE(accent_message != nullptr);
    const auto* const accent_intent { std::get_if<gitman::set_accent_intent>(accent_message) };
    REQUIRE(accent_intent != nullptr);
    REQUIRE(accent_intent->accent_id == u8"blue");

    // 클릭은 곧바로 미리 보여 주지만 저장은 `저장`이 한다 (E2).
    fixture.controller.handle(*theme_message);
    REQUIRE(fixture.controller.make_view_snapshot()->appearance.theme == gitman::theme_preference::dark);
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) == nullptr);
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->appearance.theme == gitman::theme_preference::dark);
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) != nullptr);
}
