#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "domain/text_edit.h"
#include "helpers/ui_test_support.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/font_dialog_element.h"
#include "presentation/ui/settings_dialog_element.h"

#include "luil/theme/ui_theme.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
    // 제한 시간 칸의 편집 intent다. 키 입력 한 번과 같다.
    gitman::edit_text_input_intent timeout_insert(const std::u8string_view text)
    {
        gitman::edit_text_input_intent intent {};
        intent.target = gitman::text_input_target::settings_timeout;
        intent.command = gitman::text_edit_command::insert;
        intent.text = text;
        return intent;
    }

    gitman::edit_text_input_intent timeout_command(const gitman::text_edit_command command)
    {
        gitman::edit_text_input_intent intent {};
        intent.target = gitman::text_input_target::settings_timeout;
        intent.command = command;
        return intent;
    }

    // 글꼴 질의 칸의 편집 intent다.
    gitman::edit_text_input_intent font_query_insert(const std::u8string_view text)
    {
        gitman::edit_text_input_intent intent {};
        intent.target = gitman::text_input_target::font_filter;
        intent.command = gitman::text_edit_command::insert;
        intent.text = text;
        return intent;
    }

    // 글꼴 질의 칸의 조합 상태다 (TSF가 보내는 것과 같다). `text`는 조합 중인
    // 글자까지 담은 박스 전체 표시 글이고, `composing_begin`부터 끝까지가 조합
    // 구간이다.
    gitman::set_text_composition_intent compose_font_query(const std::u8string_view text, const std::size_t composing_begin = 0u)
    {
        gitman::set_text_composition_intent intent {};
        intent.target = gitman::text_input_target::font_filter;
        intent.text = text;
        intent.caret = text.size();
        intent.composing_begin = composing_begin;
        intent.composing_end = text.size();
        intent.composing = true;
        return intent;
    }

    // 조합이 끝났음을 알린다. 확정된 글은 별도의 편집 intent로 온다.
    gitman::set_text_composition_intent end_font_composition()
    {
        gitman::set_text_composition_intent intent {};
        intent.target = gitman::text_input_target::font_filter;
        intent.composing = false;
        return intent;
    }

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

    // element가 등록한 왼쪽 클릭 액션을 그 자리에서 실행한다 (상호작용 상태 기계를
    // 거치지 않고 "무엇을 내는가"만 본다).
    std::vector<luil::input_action> click(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const action { element->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }

    // 항목 열의 스크롤 막대다. 막대 kind는 하나뿐이고 어느 목록인지는 owner가
    // 말한다 (설계 L4).
    const luil::ui_element* settings_scrollbar(const luil::ui_tree& tree)
    {
        return gitman::testing::find(tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_settings_dialog);
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

TEST_CASE("Untouched document rows follow the app settings and touched ones override", "[logic][settings-ui][s1]")
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
    // svn이 합성된 유효 설정이 실린다. 문서 git은 이 문서의 실행 파일을 신뢰하지 않아
    // 싣지 않는다 (function-buttons-design 3.10, S1).
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
    REQUIRE(refresh.settings.git_executable.empty());
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
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    const luil::ui_element_id git_badge { gitman::ui::kind_settings_override_badge, std::u8string { u8"git" } };
    REQUIRE(tree->find(git_badge) != nullptr);
    const luil::ui_element* const svn_badge { tree->find({ gitman::ui::kind_settings_override_badge, std::u8string { u8"svn" } }) };
    REQUIRE(svn_badge != nullptr);
    // 전역 배지는 눌러도 아무 일도 하지 않는다. tooltip으로만 뜻을 알린다.
    REQUIRE(svn_badge->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE_FALSE(svn_badge->tooltip().empty());

    const std::vector<luil::input_action> actions { click(*tree, git_badge) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::clear_settings_override_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->field == gitman::settings_override_field::git_executable);

    // 전역 모드에는 배지가 없다.
    recording_submitter global_submitter {};
    gitman::logic_controller global_controller { global_submitter };
    global_controller.handle(gitman::window_metrics_intent { 800.0f, 1000.0f, 1.0f });
    global_controller.handle(gitman::open_settings_intent {});
    const auto global_tree { gitman::testing::make_tree(*global_controller.make_view_snapshot()) };
    REQUIRE(global_tree->find(git_badge) == nullptr);
}

TEST_CASE("The settings dialog elements register the picker commands and intents", "[ui][settings-ui]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::testing::make_tree(*view) };

    REQUIRE(tree->find(luil::ui_element_id { gitman::ui::kind_settings_dialog }) != nullptr);

    // 찾아보기는 UI thread의 파일 선택 명령이다.
    const std::vector<luil::input_action> browse { click(*tree, luil::ui_element_id { gitman::ui::kind_settings_git_browse }) };
    REQUIRE(browse.size() == 1u);
    const luil::app_ui_command* const command { gitman::testing::as_app_command(browse) };
    REQUIRE(command != nullptr);
    REQUIRE(gitman::parse_app_command(command->command) == gitman::app_command::show_git_executable_picker);

    // Git 경로가 지정되어 있으므로 지우기는 활성이고 intent를 반환한다.
    const luil::ui_element* const git_clear { tree->find(luil::ui_element_id { gitman::ui::kind_settings_git_clear }) };
    REQUIRE(git_clear != nullptr);
    REQUIRE(git_clear->enabled());
    const std::vector<luil::input_action> clear_actions { click(*tree, git_clear->id()) };
    REQUIRE(clear_actions.size() == 1u);
    const auto* const clear_message { gitman::testing::as_message(clear_actions) };
    REQUIRE(clear_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::clear_settings_executable_intent>(*clear_message));

    // SVN 경로는 비어 있어 지우기가 비활성이다.
    const luil::ui_element* const svn_clear { tree->find(luil::ui_element_id { gitman::ui::kind_settings_svn_clear }) };
    REQUIRE(svn_clear != nullptr);
    REQUIRE(svn_clear->enabled() == false);
}

TEST_CASE("The settings dialog association buttons request the UI commands", "[ui][settings-ui][association]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    // 파일 연결은 시스템 탭이다 (settings-tabs-and-appearance-scope-design S1.2).
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::system });
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    const std::vector<luil::input_action> associate { click(*tree, luil::ui_element_id { gitman::ui::kind_settings_associate }) };
    REQUIRE(associate.size() == 1u);
    const luil::app_ui_command* const associate_command { gitman::testing::as_app_command(associate) };
    REQUIRE(associate_command != nullptr);
    REQUIRE(gitman::parse_app_command(associate_command->command) == gitman::app_command::register_file_association);

    const std::vector<luil::input_action> dissociate { click(*tree, luil::ui_element_id { gitman::ui::kind_settings_dissociate }) };
    REQUIRE(dissociate.size() == 1u);
    const luil::app_ui_command* const dissociate_command { gitman::testing::as_app_command(dissociate) };
    REQUIRE(dissociate_command != nullptr);
    REQUIRE(gitman::parse_app_command(dissociate_command->command) == gitman::app_command::unregister_file_association);
}

TEST_CASE("The toolbar settings button opens the dialog with or without a document", "[ui][settings-ui]")
{
    settings_fixture fixture {};
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    const std::vector<luil::input_action> actions { click(*tree, luil::ui_element_id { gitman::ui::kind_toolbar_settings }) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::open_settings_intent>(*message));

    // 문서가 없어도 전역 설정 진입점으로 항상 활성이다 (G3.2).
    gitman::view_snapshot empty {};
    empty.window_width = 800.0f;
    empty.window_height = 600.0f;
    empty.scale = 1.0f;
    const auto empty_tree { gitman::testing::make_tree(empty) };
    const luil::ui_element* const button { empty_tree->find(luil::ui_element_id { gitman::ui::kind_toolbar_settings }) };
    REQUIRE(button != nullptr);
    REQUIRE(button->enabled());
}

TEST_CASE("Escape closes the settings dialog before clearing the selection", "[ui][settings-ui]")
{
    // Esc는 host의 dismiss 액션이다 (설계 D2-A, modal-dialog-design.md). 앱 키 정책은
    // dialog가 없을 때만 카드 선택을 푼다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    gitman::testing::interaction_harness harness { tree };
    const std::vector<luil::input_action> actions { harness.process(luil::key_pressed_event { luil::key_code::escape }) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_settings_dialog_intent>(*message));
}

TEST_CASE("The settings dialog stands in a luil modal host", "[ui][settings-ui][modal]")
{
    // C3-2: 본체는 host의 content이고 scrim·가운데 배치·초점 가둠·Esc는 host의 몫이다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    REQUIRE(gitman::testing::find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_settings) != nullptr);
    // 초점은 dialog 안에 갇힌다 — Tab이 뒤의 카드로 새지 않는다.
    REQUIRE(tree->focus_trap() != nullptr);

    // 배경 클릭은 이제 host의 scrim이 낸다.
    const luil::ui_element* const scrim { gitman::testing::find(*tree, luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_settings) };
    REQUIRE(scrim != nullptr);
    const std::vector<luil::input_action> actions { click(*tree, scrim->id()) };
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_settings_dialog_intent>(*message));

    // 본체에는 배경 클릭 액션이 남아 있지 않고, 누름은 host가 흡수하게 세워 준다.
    const luil::ui_element* const body { tree->find({ gitman::ui::kind_settings_dialog }) };
    REQUIRE(body != nullptr);
    REQUIRE(body->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(body->hit_opaque());
}

TEST_CASE("Clicking the empty panel area does not cancel the settings dialog", "[ui][settings-ui][modal]")
{
    // panel은 흡수를 따로 걸지 않는다 — host가 content(본체)에 건 흡수가 panel의 빈
    // 자리를 받아 scrim(취소)으로 새지 않는다 (C3-4).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const body { tree->find({ gitman::ui::kind_settings_dialog }) };
    REQUIRE(body != nullptr);
    // panel 왼쪽 아래 빈 자리다 — 탭 rail 아래, 버튼 줄 왼쪽.
    const luil::rect_f box { body->bounds() };
    const float x { box.x + 8.0f };
    const float y { box.y + box.height - 8.0f };
    const luil::ui_element* const hit { tree->hit_test(x, y) };
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id().kind != luil::ui_element_kind::modal_scrim);

    gitman::testing::interaction_harness harness { tree };
    const std::vector<luil::input_action> actions { gitman::testing::click(harness, luil::rect_f { x - 1.0f, y - 1.0f, 2.0f, 2.0f }, 0) };
    REQUIRE(gitman::testing::as_message(actions) == nullptr);
}

TEST_CASE("Enter in the timeout box runs the default save button", "[ui][settings-ui][timeout]")
{
    // 저장이 기본 버튼이라 텍스트 칸의 Enter가 그리로 흐른다 (enter-default-design.md).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    const luil::ui_element* const confirm { tree->find({ gitman::ui::kind_settings_dialog_confirm }) };
    REQUIRE(confirm != nullptr);
    REQUIRE(confirm->default_button());
    REQUIRE(confirm->enabled());

    gitman::testing::interaction_harness harness { tree };
    const luil::rect_f input { gitman::testing::bounds_of(*tree, gitman::ui::kind_settings_timeout_input) };
    static_cast<void>(harness.process(luil::pointer_pressed_event { input.x + 2.0f, input.y + 2.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(harness.process(luil::pointer_released_event { input.x + 2.0f, input.y + 2.0f, luil::pointer_button::left, at(10) }));
    REQUIRE(harness.snapshot().focused_input.kind == gitman::ui::kind_settings_timeout_input);

    const std::vector<luil::input_action> actions { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::confirm_settings_intent>(*message));
}

TEST_CASE("The settings dialog shows one tab at a time and keeps the drafts", "[ui][settings-ui][tabs]")
{
    // 탭은 보기 상태다 — 옮겨도 다른 탭의 초안은 남는다
    // (settings-tabs-and-appearance-scope-design S1.2).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});

    const auto tools { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    // 네 탭 모두 rail에 있고, 기본 탭은 도구다.
    for (const std::u8string_view name : { std::u8string_view { u8"tools" }, std::u8string_view { u8"operations" }, std::u8string_view { u8"appearance" }, std::u8string_view { u8"system" } })
        REQUIRE(tools->find({ gitman::ui::kind_settings_tab_item, std::u8string { name } }) != nullptr);
    REQUIRE(tools->find({ gitman::ui::kind_settings_git_browse }) != nullptr);
    REQUIRE(tools->find({ gitman::ui::kind_settings_timeout_input }) != nullptr);
    // 다른 탭의 컨트롤은 만들어지지 않는다.
    REQUIRE(tools->find({ gitman::ui::kind_settings_submodules_toggle }) == nullptr);
    REQUIRE(tools->find({ gitman::ui::kind_settings_associate }) == nullptr);
    REQUIRE(tools->ids_of_kind(gitman::ui::kind_settings_accent_swatch).empty());

    // D1-B: scrim은 Tab 자리가 아니고 탭 rail 항목·텍스트 칸 같은 dialog 컨트롤은 자리다.
    REQUIRE(tools->find({ gitman::ui::kind_settings_dialog })->tab_stop() == false);
    REQUIRE(tools->find({ gitman::ui::kind_settings_tab_item, std::u8string { u8"tools" } })->tab_stop());
    REQUIRE(tools->find({ gitman::ui::kind_settings_timeout_input })->tab_stop());

    // 초안을 고친 뒤 탭을 옮겨도 값은 남는다.
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::subversion, u8"C:\\tools\\svn.exe" });
    const std::vector<luil::input_action> actions {
        click(*tools, luil::ui_element_id { gitman::ui::kind_settings_tab_item, std::u8string { u8"operations" } }),
    };
    REQUIRE(actions.size() == 1u);
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    const auto* const intent { std::get_if<gitman::select_settings_tab_intent>(message) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->tab == gitman::settings_tab::operations);
    fixture.controller.handle(*message);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog->active_tab == gitman::settings_tab::operations);
    REQUIRE(view->settings_dialog->svn_path == u8"C:\\tools\\svn.exe");

    const auto operations { gitman::testing::make_tree(*view) };
    REQUIRE(operations->find({ gitman::ui::kind_settings_submodules_toggle }) != nullptr);
    REQUIRE(operations->find({ gitman::ui::kind_settings_log_files_toggle }) != nullptr);
    REQUIRE(operations->find({ gitman::ui::kind_settings_git_browse }) == nullptr);

    // panel 높이는 탭을 옮겨도 같다 — 창이 튀지 않는다 (S1.3).
    REQUIRE(operations->find({ gitman::ui::kind_settings_dialog_panel })->bounds().height == tools->find({ gitman::ui::kind_settings_dialog_panel })->bounds().height);
}

TEST_CASE("The scope badge sits above the item title without moving the controls", "[ui][settings-ui]")
{
    // 배지는 항목 제목 바로 위 왼쪽이고, 행 컨트롤은 제목 줄 오른쪽 끝을 그대로
    // 쓴다 (E3).
    settings_fixture fixture {};
    // 두 모드를 나란히 비교하므로 **둘 다 스크롤이 필요 없는** 창 높이를 쓴다.
    // 한쪽에만 스크롤 막대가 생기면 항목 열이 그만큼 좁아져 x가 달라진다 (E1).
    fixture.controller.handle(gitman::window_metrics_intent { 800.0f, 1000.0f, 1.0f });
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    const luil::ui_element* const badge { tree->find({ gitman::ui::kind_settings_override_badge, std::u8string { u8"git" } }) };
    REQUIRE(badge != nullptr);
    const luil::rect_f badge_bounds { badge->bounds() };
    REQUIRE(badge_bounds.height >= 16.0f);

    // 같은 항목의 지우기 버튼보다 위에 있고 줄이 겹치지 않는다.
    const luil::rect_f clear { tree->find({ gitman::ui::kind_settings_git_clear })->bounds() };
    REQUIRE(badge_bounds.y + badge_bounds.height <= clear.y);
    // 배지는 항목 왼쪽 끝에서 시작한다.
    const luil::rect_f panel { tree->find({ gitman::ui::kind_settings_dialog_panel })->bounds() };
    REQUIRE(badge_bounds.x > panel.x);
    REQUIRE(badge_bounds.x + badge_bounds.width < clear.x);

    // 컨트롤은 전역 모드와 같은 오른쪽 끝에 있다 — 배지 때문에 밀리지 않는다.
    recording_submitter global_submitter {};
    gitman::logic_controller global_controller { global_submitter };
    global_controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
    global_controller.handle(gitman::open_settings_intent {});
    const auto global_tree { gitman::testing::make_tree(*global_controller.make_view_snapshot()) };
    const luil::rect_f global_clear { global_tree->find({ gitman::ui::kind_settings_git_clear })->bounds() };
    const luil::rect_f global_panel { global_tree->find({ gitman::ui::kind_settings_dialog_panel })->bounds() };
    REQUIRE(clear.x - panel.x == global_clear.x - global_panel.x);

    // 외양 항목도 같은 규칙이다. 초안을 고치면 `문서 설정`이 되어 누를 수 있다 (D4).
    fixture.controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });
    const auto follows { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const following_badge { follows->find({ gitman::ui::kind_settings_override_badge, std::u8string { u8"accent" } }) };
    REQUIRE(following_badge != nullptr);
    REQUIRE(following_badge->action(luil::ui_trigger::left_click) == nullptr);

    fixture.controller.handle(gitman::set_accent_intent { u8"blue" });
    const auto appearance { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const accent_badge { appearance->find({ gitman::ui::kind_settings_override_badge, std::u8string { u8"accent" } }) };
    REQUIRE(accent_badge != nullptr);
    REQUIRE(accent_badge->action(luil::ui_trigger::left_click) != nullptr);

    const std::vector<luil::input_action> cleared { click(*appearance, accent_badge->id()) };
    REQUIRE(cleared.size() == 1u);
    const auto* const message { gitman::testing::as_message(cleared) };
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

            const auto tree { gitman::testing::make_tree(*controller.make_view_snapshot()) };
            const luil::ui_element* const panel { tree->find({ gitman::ui::kind_settings_dialog_panel }) };
            REQUIRE(panel != nullptr);
            const luil::rect_f box { panel->bounds() };

            const std::vector<luil::ui_element_kind> kinds {
                gitman::ui::kind_settings_tab_item,
                gitman::ui::kind_settings_git_browse,
                gitman::ui::kind_settings_git_clear,
                gitman::ui::kind_settings_svn_browse,
                gitman::ui::kind_settings_svn_clear,
                gitman::ui::kind_settings_timeout_input,
                gitman::ui::kind_settings_submodules_toggle,
                gitman::ui::kind_settings_ignore_local_toggle,
                gitman::ui::kind_settings_log_files_toggle,
                gitman::ui::kind_settings_theme_option,
                gitman::ui::kind_settings_accent_swatch,
                gitman::ui::kind_settings_override_badge,
                gitman::ui::kind_settings_associate,
                gitman::ui::kind_settings_dissociate,
                gitman::ui::kind_settings_dialog_confirm,
                gitman::ui::kind_settings_dialog_cancel,
            };
            for (const luil::ui_element_kind kind : kinds)
                for (const luil::ui_element_id& id : tree->ids_of_kind(kind))
                {
                    const luil::rect_f bounds { tree->find(id)->bounds() };
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
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout.text.empty());

    // 숫자는 뒤에 붙고, 숫자가 아닌 문자는 무시되고, backspace는 마지막 자리를
    // 지운다. 최대값(3600)이 4자리라 다섯 자리째는 버린다.
    for (const char32_t character : { U'9', U'a', U'0', U'힣', U'0', U' ', U'0', U'1' })
        fixture.controller.handle(timeout_insert(gitman::text_edit_encode_utf8(character)));
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout.text == u8"9000");

    fixture.controller.handle(timeout_command(gitman::text_edit_command::backspace));
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout.text == u8"900");
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("An out of range timeout blocks confirmation with a Korean message", "[logic][settings-ui][timeout]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(timeout_insert(u8"5"));

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
        fixture.controller.handle(timeout_insert(gitman::text_edit_encode_utf8(character)));
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
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->timeout.text == u8"1800");
}

TEST_CASE("Typed characters reach the timeout box only while it has focus", "[ui][settings-ui][timeout]")
{
    settings_fixture fixture {};

    // dialog가 없으면 문자 입력은 버려진다.
    {
        gitman::testing::interaction_harness idle { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
        REQUIRE(idle.process(luil::character_typed_event { U'5' }).empty());
    }

    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    // 텍스트 박스 element가 존재하고 안내 tooltip을 가진다.
    const luil::ui_element* const input { gitman::testing::find(*tree, gitman::ui::kind_settings_timeout_input) };
    REQUIRE(input != nullptr);
    REQUIRE(input->visible());
    REQUIRE(input->tooltip().empty() == false);

    // dialog가 열려 있어도 초점이 없으면 문자는 버려진다 (자동 초점 없음).
    gitman::testing::interaction_harness harness { tree };
    REQUIRE(harness.process(luil::character_typed_event { U'5' }).empty());

    // 텍스트 박스를 누르면 초점이 생기고 문자가 편집 intent로 바뀐다. 문자·편집
    // 키를 `text_edit_request`로 만드는 일은 luil의 몫이라(계약 3장) 여기서는
    // policy가 그것을 어떤 gitman intent로 옮기는지만 본다.
    const luil::rect_f box { input->bounds() };
    static_cast<void>(harness.process(luil::pointer_pressed_event { box.x + 2.0f, box.y + 2.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(harness.process(luil::pointer_released_event { box.x + 2.0f, box.y + 2.0f, luil::pointer_button::left, at(10) }));
    const std::vector<luil::input_action> actions { harness.process(luil::character_typed_event { U'5' }) };
    const auto* const intent { gitman::testing::as_intent<gitman::edit_text_input_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->target == gitman::text_input_target::settings_timeout);
    REQUIRE(intent->command == gitman::text_edit_command::insert);
    REQUIRE(intent->text == u8"5");

    // 편집 키도 같은 칸의 편집 intent가 된다 (키 조합 전수는 luil test의 몫이라
    // 대표로 Ctrl+A 하나만 본다).
    luil::key_pressed_event select_all {};
    select_all.key = luil::key_code::key_a;
    select_all.control = true;
    const auto select_actions { harness.process(select_all) };
    const auto* const select_intent { gitman::testing::as_intent<gitman::edit_text_input_intent>(select_actions) };
    REQUIRE(select_intent != nullptr);
    REQUIRE(select_intent->target == gitman::text_input_target::settings_timeout);
    REQUIRE(select_intent->command == gitman::text_edit_command::select_all);
}

TEST_CASE("Focus follows presses and carries the caret phase origin", "[ui][settings-ui][timeout]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    gitman::testing::interaction_harness harness { tree };

    // 열림만으로는 초점이 없다 (검수 지시: 자동 초점 없음).
    REQUIRE(harness.process(luil::pointer_moved_event { 1.0f, 1.0f, at(0) }).empty());
    REQUIRE(harness.snapshot().focused_input == luil::ui_element_id {});
    REQUIRE(harness.snapshot().focus_started_at.has_value() == false);

    // 텍스트 박스를 누르면 초점과 caret 위상 기준 시각이 생긴다.
    const luil::rect_f input { gitman::testing::bounds_of(*tree, gitman::ui::kind_settings_timeout_input) };
    static_cast<void>(harness.process(luil::pointer_pressed_event { input.x + 2.0f, input.y + 2.0f, luil::pointer_button::left, at(20) }));
    static_cast<void>(harness.process(luil::pointer_released_event { input.x + 2.0f, input.y + 2.0f, luil::pointer_button::left, at(30) }));
    REQUIRE(harness.snapshot().focused_input.kind == gitman::ui::kind_settings_timeout_input);
    REQUIRE(harness.snapshot().focus_started_at == at(20));

    // 다른 곳(패널)을 누르면 초점이 풀리고 문자 입력이 버려진다.
    const luil::rect_f panel { gitman::testing::bounds_of(*tree, gitman::ui::kind_settings_dialog_panel) };
    static_cast<void>(harness.process(luil::pointer_pressed_event { panel.x + 2.0f, panel.y + 2.0f, luil::pointer_button::left, at(40) }));
    static_cast<void>(harness.process(luil::pointer_released_event { panel.x + 2.0f, panel.y + 2.0f, luil::pointer_button::left, at(50) }));
    REQUIRE(harness.snapshot().focused_input == luil::ui_element_id {});
    REQUIRE(harness.snapshot().focus_started_at.has_value() == false);
    REQUIRE(harness.process(luil::character_typed_event { U'5' }).empty());

    // dialog가 닫히면 남아 있던 초점도 거둔다.
    static_cast<void>(harness.process(luil::pointer_pressed_event { input.x + 2.0f, input.y + 2.0f, luil::pointer_button::left, at(60) }));
    static_cast<void>(harness.process(luil::pointer_released_event { input.x + 2.0f, input.y + 2.0f, luil::pointer_button::left, at(70) }));
    REQUIRE(harness.snapshot().focused_input.kind == gitman::ui::kind_settings_timeout_input);
    fixture.controller.handle(gitman::cancel_settings_dialog_intent {});
    harness.controller.set_tree(gitman::testing::make_tree(*fixture.controller.make_view_snapshot()));
    REQUIRE(harness.process(luil::pointer_moved_event { 1.0f, 1.0f, at(80) }).empty());
    REQUIRE(harness.snapshot().focused_input == luil::ui_element_id {});
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
        const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
        const std::vector<luil::input_action> actions { click(*tree, luil::ui_element_id { gitman::ui::kind_settings_submodules_toggle }) };
        const auto* const message { gitman::testing::as_message(actions) };
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
        const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
        const std::vector<luil::input_action> actions { click(*tree, luil::ui_element_id { gitman::ui::kind_settings_log_files_toggle }) };
        const auto* const message { gitman::testing::as_message(actions) };
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
    REQUIRE(followed->appearance.accent_id == luil::default_accent_id);
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
    REQUIRE(view->appearance.accent_id == luil::default_accent_id);
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) == nullptr);

    // 다시 열면 초안도 유효 값에서 시작한다.
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto reopened { fixture.controller.make_view_snapshot() };
    REQUIRE(reopened->settings_dialog->theme == gitman::theme_preference::system);
    REQUIRE(reopened->settings_dialog->accent_id == luil::default_accent_id);
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

    const auto tree { gitman::testing::make_tree(*view) };
    const luil::ui_element_id light { gitman::ui::kind_settings_theme_option, std::u8string { u8"light" } };
    const luil::ui_element_id system { gitman::ui::kind_settings_theme_option, std::u8string { u8"system" } };
    const luil::ui_element_id dark { gitman::ui::kind_settings_theme_option, std::u8string { u8"dark" } };
    REQUIRE(tree->find(light) != nullptr);
    REQUIRE(tree->find(system) != nullptr);
    REQUIRE(tree->find(dark) != nullptr);

    // 색 동그라미는 내장 목록만큼 있고, 각자 자기 id를 가진다.
    REQUIRE(tree->ids_of_kind(gitman::ui::kind_settings_accent_swatch).size() == luil::accent_catalog().size());
    for (const luil::accent_definition& accent : luil::accent_catalog())
        REQUIRE(tree->find({ gitman::ui::kind_settings_accent_swatch, std::u8string { accent.id } }) != nullptr);

    // 세 칸과 동그라미는 서로 겹치지 않고 panel 안에 있다.
    const luil::rect_f panel { tree->find({ gitman::ui::kind_settings_dialog_panel })->bounds() };
    const luil::rect_f system_bounds { tree->find(system)->bounds() };
    REQUIRE(system_bounds.x >= panel.x);
    REQUIRE(system_bounds.x + system_bounds.width <= panel.x + panel.width);
    REQUIRE(tree->find(light)->bounds().x < system_bounds.x);
    REQUIRE(tree->find(dark)->bounds().x > system_bounds.x);

    // 색 격자는 왼쪽에서 오른쪽으로 채우고 panel을 넘지 않는다. 줄이 바뀌면 x가
    // 처음으로 돌아가고 y가 내려간다 (S2.4).
    std::vector<luil::rect_f> swatches {};
    for (const luil::accent_definition& accent : luil::accent_catalog())
        swatches.push_back(tree->find({ gitman::ui::kind_settings_accent_swatch, std::u8string { accent.id } })->bounds());
    for (const luil::rect_f& swatch : swatches)
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
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    const std::vector<luil::input_action> theme_actions {
        click(*tree, luil::ui_element_id { gitman::ui::kind_settings_theme_option, std::u8string { u8"dark" } }),
    };
    REQUIRE(theme_actions.size() == 1u);
    const auto* const theme_message { gitman::testing::as_message(theme_actions) };
    REQUIRE(theme_message != nullptr);
    const auto* const theme_intent { std::get_if<gitman::set_theme_preference_intent>(theme_message) };
    REQUIRE(theme_intent != nullptr);
    REQUIRE(theme_intent->theme == gitman::theme_preference::dark);

    const std::vector<luil::input_action> accent_actions {
        click(*tree, luil::ui_element_id { gitman::ui::kind_settings_accent_swatch, std::u8string { u8"blue" } }),
    };
    REQUIRE(accent_actions.size() == 1u);
    const auto* const accent_message { gitman::testing::as_message(accent_actions) };
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

TEST_CASE("A short window scrolls the settings items instead of overflowing", "[ui][settings-ui][scroll]")
{
    // 지시(2026-08-23): 클라이언트 높이가 설정 팝업보다 낮으면 스타일이 깨진다.
    // 항목이 저장·취소 버튼 줄을 뚫고 나가는 대신 스크롤이 생겨야 한다.
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 1280.0f, 320.0f, 1.0f });
    controller.handle(gitman::open_settings_intent {});
    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });

    const auto snapshot { controller.make_view_snapshot() };
    REQUIRE(snapshot->settings_dialog.has_value());

    const gitman::ui::settings_dialog_scroll_metrics metrics {
        gitman::ui::settings_dialog_metrics(*snapshot->settings_dialog, 320.0f),
    };
    REQUIRE(metrics.maximum_scroll > 0.0f);
    REQUIRE(metrics.viewport_height < metrics.content_height);

    const auto tree { gitman::testing::make_tree(*snapshot) };
    REQUIRE(settings_scrollbar(*tree) != nullptr);

    // 저장·취소 버튼은 제자리에 남고, 항목 컨트롤은 그 위에서 끝난다.
    const luil::ui_element* const confirm { tree->find({ gitman::ui::kind_settings_dialog_confirm }) };
    REQUIRE(confirm != nullptr);
    const luil::ui_element* const panel { tree->find({ gitman::ui::kind_settings_dialog_panel }) };
    REQUIRE(panel != nullptr);
    REQUIRE(confirm->bounds().y + confirm->bounds().height <= panel->bounds().y + panel->bounds().height);
}

TEST_CASE("Settings scrolling stops at both ends and resets when the tab changes", "[ui][settings-ui][scroll]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 1280.0f, 320.0f, 1.0f });
    controller.handle(gitman::open_settings_intent {});
    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });

    const float maximum {
        gitman::ui::settings_dialog_metrics(*controller.make_view_snapshot()->settings_dialog, 320.0f).maximum_scroll,
    };
    REQUIRE(maximum > 0.0f);

    // 위로 굴려도 0 아래로 내려가지 않는다.
    controller.handle(gitman::settings_scroll_intent { -400.0f });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == 0.0f);

    // 아래로 아무리 굴려도 한계에서 멈춘다.
    controller.handle(gitman::settings_scroll_intent { 10'000.0f });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == maximum);

    // 탭을 옮기면 맨 위부터 본다.
    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::tools });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == 0.0f);
}

TEST_CASE("A tall window keeps the settings dialog unscrolled", "[ui][settings-ui][scroll]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 1280.0f, 1000.0f, 1.0f });
    controller.handle(gitman::open_settings_intent {});
    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });

    const auto snapshot { controller.make_view_snapshot() };
    REQUIRE(gitman::ui::settings_dialog_metrics(*snapshot->settings_dialog, 1000.0f).maximum_scroll == 0.0f);

    // 스크롤할 것이 없으면 막대도 없고 휠도 아무 일을 하지 않는다.
    const auto tree { gitman::testing::make_tree(*snapshot) };
    const luil::ui_element* const bar { settings_scrollbar(*tree) };
    REQUIRE((bar == nullptr || bar->visible() == false));

    controller.handle(gitman::settings_scroll_intent { 500.0f });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == 0.0f);
}

TEST_CASE("Scrolled settings controls leave the viewport and stop responding", "[ui][settings-ui][scroll]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 1280.0f, 320.0f, 1.0f });
    controller.handle(gitman::open_settings_intent {});

    const auto before { controller.make_view_snapshot() };
    const auto tree_before { gitman::testing::make_tree(*before) };
    const luil::ui_element* const browse_before { tree_before->find({ gitman::ui::kind_settings_git_browse }) };
    REQUIRE(browse_before != nullptr);
    const float y_before { browse_before->bounds().y };

    controller.handle(gitman::settings_scroll_intent { 60.0f });
    const auto after { controller.make_view_snapshot() };
    REQUIRE(after->settings_dialog->scroll_offset > 0.0f);

    const auto tree_after { gitman::testing::make_tree(*after) };
    const luil::ui_element* const browse_after { tree_after->find({ gitman::ui::kind_settings_git_browse }) };
    REQUIRE(browse_after != nullptr);
    // 항목은 위로 흐르고 탭 rail은 제자리다.
    REQUIRE(browse_after->bounds().y < y_before);

    const luil::ui_element_id rail_id { gitman::ui::kind_settings_tab_item, std::u8string { u8"tools" } };
    const luil::ui_element* const rail_before { tree_before->find(rail_id) };
    const luil::ui_element* const rail_after { tree_after->find(rail_id) };
    REQUIRE(rail_before != nullptr);
    REQUIRE(rail_after != nullptr);
    REQUIRE(rail_after->bounds().y == rail_before->bounds().y);

    // viewport 밖으로 밀려난 자리는 눌리지 않는다.
    const luil::ui_element* const panel { tree_after->find({ gitman::ui::kind_settings_dialog_panel }) };
    REQUIRE(panel != nullptr);
    const luil::rect_f bounds { browse_after->bounds() };
    if (bounds.y + bounds.height < panel->bounds().y + 44.0f)
        REQUIRE(tree_after->hit_test(bounds.x + 1.0f, bounds.y + 1.0f) != browse_after);
}

TEST_CASE("The wheel scrolls the settings dialog and the font dialog wins on top", "[ui][settings-ui][scroll]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 1280.0f, 320.0f, 1.0f });
    controller.handle(gitman::open_settings_intent {});

    gitman::testing::interaction_harness harness { gitman::testing::make_tree(*controller.make_view_snapshot()) };

    const auto settings_actions { harness.process(luil::mouse_wheel_event { 640.0f, 200.0f, -120.0f }) };
    REQUIRE(gitman::testing::as_intent<gitman::settings_scroll_intent>(settings_actions) != nullptr);

    // 글꼴 dialog는 환경설정 위에 뜨므로 휠을 먼저 가져간다.
    controller.handle(gitman::open_font_dialog_intent { false });
    const auto font_tree { gitman::testing::make_tree(*controller.make_view_snapshot()) };
    // D1-B: 글꼴 목록의 행과 scrim은 Tab 자리가 아니고 확인·취소 버튼과 질의 칸만 자리다.
    REQUIRE(font_tree->find(gitman::ui::font_dialog_item_id(0))->tab_stop() == false);
    REQUIRE(font_tree->find({ gitman::ui::kind_font_dialog })->tab_stop() == false);
    REQUIRE(font_tree->find({ gitman::ui::kind_font_dialog_query_input })->tab_stop());
    harness.controller.set_tree(font_tree);

    const auto font_actions { harness.process(luil::mouse_wheel_event { 640.0f, 200.0f, -120.0f }) };
    REQUIRE(gitman::testing::as_intent<gitman::font_dialog_scroll_intent>(font_actions) != nullptr);
}

TEST_CASE("The font dialog stands in a luil modal host", "[ui][settings-ui][font][modal]")
{
    // C3-2: 글꼴 dialog도 host의 content다. 배경 클릭은 host의 scrim이 낸다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code", u8"맑은 고딕" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { false });
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    REQUIRE(gitman::testing::find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_font) != nullptr);
    REQUIRE(tree->focus_trap() != nullptr);

    const luil::ui_element* const scrim { gitman::testing::find(*tree, luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_font) };
    REQUIRE(scrim != nullptr);
    const std::vector<luil::input_action> actions { click(*tree, scrim->id()) };
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_font_dialog_intent>(*message));

    const luil::ui_element* const body { tree->find({ gitman::ui::kind_font_dialog }) };
    REQUIRE(body != nullptr);
    REQUIRE(body->action(luil::ui_trigger::left_click) == nullptr);
    REQUIRE(body->hit_opaque());
}

TEST_CASE("Clicking the empty panel area does not cancel the font dialog", "[ui][settings-ui][font][modal]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { false });
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const body { tree->find({ gitman::ui::kind_font_dialog }) };
    REQUIRE(body != nullptr);
    // panel 왼쪽 아래 빈 자리다 — 미리 보기 칸 아래, 버튼 줄 왼쪽.
    const luil::rect_f box { body->bounds() };
    const float x { box.x + 8.0f };
    const float y { box.y + box.height - 8.0f };
    const luil::ui_element* const hit { tree->hit_test(x, y) };
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id().kind != luil::ui_element_kind::modal_scrim);

    gitman::testing::interaction_harness harness { tree };
    const std::vector<luil::input_action> actions { gitman::testing::click(harness, luil::rect_f { x - 1.0f, y - 1.0f, 2.0f, 2.0f }, 0) };
    REQUIRE(gitman::testing::as_message(actions) == nullptr);
}

TEST_CASE("Escape closes only the font dialog above the settings dialog", "[ui][settings-ui][font][modal]")
{
    // 가둠이 겹치면 마지막에 그린 가둠이 Esc를 받는다 (modal-dialog-design.md).
    // 환경설정은 그대로 남고 글꼴 dialog만 닫힌다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code", u8"맑은 고딕" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { false });
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };
    REQUIRE(gitman::testing::find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_settings) != nullptr);
    REQUIRE(gitman::testing::find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_font) != nullptr);

    gitman::testing::interaction_harness harness { tree };
    const std::vector<luil::input_action> actions { harness.process(luil::key_pressed_event { luil::key_code::escape }) };
    REQUIRE(actions.size() == 1u);
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_font_dialog_intent>(*message));
}

TEST_CASE("Enter in the font query box runs the default pick button", "[ui][settings-ui][font]")
{
    // 선택이 기본 버튼이라 질의 칸의 Enter가 그리로 흐른다 (enter-default-design.md).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code", u8"맑은 고딕" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { true });
    fixture.controller.handle(gitman::select_font_family_intent { 0u });
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    const luil::ui_element* const confirm { tree->find({ gitman::ui::kind_font_dialog_confirm }) };
    REQUIRE(confirm != nullptr);
    REQUIRE(confirm->default_button());
    REQUIRE(confirm->enabled());

    gitman::testing::interaction_harness harness { tree };
    const luil::rect_f query { gitman::testing::bounds_of(*tree, gitman::ui::kind_font_dialog_query_input) };
    static_cast<void>(harness.process(luil::pointer_pressed_event { query.x + 2.0f, query.y + 2.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(harness.process(luil::pointer_released_event { query.x + 2.0f, query.y + 2.0f, luil::pointer_button::left, at(10) }));
    REQUIRE(harness.snapshot().focused_input.kind == gitman::ui::kind_font_dialog_query_input);

    const std::vector<luil::input_action> actions { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
    const auto* const message { gitman::testing::as_message(actions) };
    REQUIRE(message != nullptr);
    REQUIRE(std::holds_alternative<gitman::confirm_font_dialog_intent>(*message));
}

TEST_CASE("A clamped dialog keeps a margin from the client edges", "[ui][settings-ui][scroll]")
{
    // 지시(2026-08-23): 창이 낮아 panel이 잘려도 클라이언트에 딱 붙으면 안 된다.
    // 여백은 `layout_dialog_edge_margin` 하나로 조정한다.
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    constexpr float window_height { 320.0f };
    controller.handle(gitman::window_metrics_intent { 1280.0f, window_height, 1.0f });
    controller.handle(gitman::open_settings_intent {});
    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });

    const auto tree { gitman::testing::make_tree(*controller.make_view_snapshot()) };
    const luil::ui_element* const panel { tree->find({ gitman::ui::kind_settings_dialog_panel }) };
    REQUIRE(panel != nullptr);
    const luil::rect_f box { panel->bounds() };

    const float margin { gitman::dialog_edge_margin(window_height) };
    REQUIRE(margin > 0.0f);
    // 위아래 모두 여백만큼 떨어져 있다.
    REQUIRE(box.y >= margin);
    REQUIRE(box.y + box.height <= window_height - margin);
    // 그래도 잘린 상태다 — 여백이 panel을 없애 버리지는 않는다.
    REQUIRE(box.height > 0.0f);
}

TEST_CASE("The scroll limit accounts for the dialog margin", "[ui][settings-ui][scroll]")
{
    // 배치와 스크롤 한계가 같은 여백을 봐야 휠이 헛돌거나 마지막 항목이 잘리지 않는다.
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    constexpr float window_height { 320.0f };
    controller.handle(gitman::window_metrics_intent { 1280.0f, window_height, 1.0f });
    controller.handle(gitman::open_settings_intent {});
    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });

    const auto snapshot { controller.make_view_snapshot() };
    const gitman::ui::settings_dialog_scroll_metrics metrics {
        gitman::ui::settings_dialog_metrics(*snapshot->settings_dialog, window_height),
    };

    const auto tree { gitman::testing::make_tree(*snapshot) };
    const luil::rect_f box { tree->find({ gitman::ui::kind_settings_dialog_panel })->bounds() };
    // panel 높이 = chrome + viewport다. 배치가 여백을 반영했으면 이 둘이 맞는다.
    REQUIRE(box.height == Catch::Approx(104.0f + metrics.viewport_height));

    // 끝까지 스크롤하면 마지막 항목이 viewport 안에 들어온다.
    controller.handle(gitman::settings_scroll_intent { 10000.0f });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == metrics.maximum_scroll);
}

TEST_CASE("A very short window shrinks the margin instead of the panel to nothing", "[ui][settings-ui][scroll]")
{
    // 창이 아주 낮으면 여백부터 줄인다 — panel이 사라지는 것보다 낫다.
    REQUIRE(gitman::dialog_edge_margin(1000.0f) == gitman::layout_dialog_edge_margin);
    REQUIRE(gitman::dialog_edge_margin(100.0f) < gitman::layout_dialog_edge_margin);
    REQUIRE(gitman::dialog_available_height(100.0f) > 0.0f);
    REQUIRE(gitman::dialog_available_height(20.0f) > 0.0f);
    // 여백은 언제나 창 안에 들어간다.
    for (const float height : { 20.0f, 100.0f, 320.0f, 1000.0f })
        REQUIRE(gitman::dialog_edge_margin(height) * 2.0f < height);
}

TEST_CASE("Each settings tab gets its own scroll extent", "[ui][settings-ui][scroll][tabs]")
{
    // 지시(2026-08-23): 탭마다 스크롤을 새로 계산한다. 가장 높은 탭으로 한계를 세면
    // 짧은 탭에서 빈 공간까지 스크롤된다.
    //
    // 창 520에서는 `외양` 탭만 넘친다 (내용 480 · viewport 368). 나머지 셋은
    // 238 · 266 · 116으로 넉넉히 들어온다.
    constexpr float window_height { 520.0f };
    const std::vector<gitman::settings_tab> short_tabs {
        gitman::settings_tab::tools,
        gitman::settings_tab::operations,
        gitman::settings_tab::system,
    };

    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 1280.0f, window_height, 1.0f });
    controller.handle(gitman::open_settings_intent {});

    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });
    const auto tall { controller.make_view_snapshot() };
    const auto tall_metrics { gitman::ui::settings_dialog_metrics(*tall->settings_dialog, window_height) };
    REQUIRE(tall_metrics.maximum_scroll > 0.0f);
    const auto tall_tree { gitman::testing::make_tree(*tall) };
    REQUIRE(settings_scrollbar(*tall_tree)->visible());

    for (const gitman::settings_tab tab : short_tabs)
    {
        controller.handle(gitman::select_settings_tab_intent { tab });
        const auto snapshot { controller.make_view_snapshot() };
        const auto metrics { gitman::ui::settings_dialog_metrics(*snapshot->settings_dialog, window_height) };

        // 그 탭의 내용은 다 보이므로 스크롤할 것이 없다.
        REQUIRE(metrics.content_height <= metrics.viewport_height);
        REQUIRE(metrics.maximum_scroll == 0.0f);

        // 막대도 없고 휠도 아무 일을 하지 않는다.
        const auto tab_tree { gitman::testing::make_tree(*snapshot) };
        const luil::ui_element* const bar { settings_scrollbar(*tab_tree) };
        INFO("tab " << static_cast<int>(tab) << " content " << metrics.content_height << " viewport " << metrics.viewport_height << " max " << metrics.maximum_scroll);
        REQUIRE((bar == nullptr || bar->visible() == false));
        controller.handle(gitman::settings_scroll_intent { 500.0f });
        REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == 0.0f);

        // panel 크기는 탭이 바뀌어도 그대로다 — dialog가 커졌다 작아졌다 하지 않는다 (S1.3).
        REQUIRE(metrics.panel_content_height == tall_metrics.panel_content_height);
        REQUIRE(metrics.viewport_height == tall_metrics.viewport_height);
    }
}

TEST_CASE("Switching tabs recalculates the scroll limit at that moment", "[ui][settings-ui][scroll][tabs]")
{
    // 긴 탭에서 끝까지 내린 뒤 짧은 탭으로 옮기면, 그 탭의 한계로 다시 계산되어야
    // 한다. 옛 한계가 남으면 빈 공간이 보인다.
    constexpr float window_height { 520.0f };
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    controller.handle(gitman::window_metrics_intent { 1280.0f, window_height, 1.0f });
    controller.handle(gitman::open_settings_intent {});

    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });
    controller.handle(gitman::settings_scroll_intent { 10000.0f });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset > 0.0f);

    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::tools });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == 0.0f);

    // 짧은 탭에서는 아무리 굴려도 0이다.
    controller.handle(gitman::settings_scroll_intent { 500.0f });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == 0.0f);

    // 다시 긴 탭으로 돌아오면 그 탭의 한계까지 내려간다.
    controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::appearance });
    const auto metrics { gitman::ui::settings_dialog_metrics(*controller.make_view_snapshot()->settings_dialog, window_height) };
    controller.handle(gitman::settings_scroll_intent { 10000.0f });
    REQUIRE(controller.make_view_snapshot()->settings_dialog->scroll_offset == metrics.maximum_scroll);
}

TEST_CASE("An overridden item keeps its badge and controls off the highlight edges", "[ui][settings-ui]")
{
    // 지시(2026-08-23): 문서 모드에서 덮어쓴 항목은 블록에 옅은 키 컬러를 까는데
    // 배지와 버튼이 그 가장자리에 딱 붙어 빡빡해 보였다. 안쪽 여백을 확인한다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::window_metrics_intent { 800.0f, 1000.0f, 1.0f });
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto tree { gitman::testing::make_tree(*fixture.controller.make_view_snapshot()) };

    const luil::ui_element* const badge { tree->find({ gitman::ui::kind_settings_override_badge, std::u8string { u8"git" } }) };
    REQUIRE(badge != nullptr);
    const luil::rect_f badge_bounds { badge->bounds() };

    // 같은 항목의 두 버튼이 블록 오른쪽 끝이다.
    const luil::rect_f browse { tree->find({ gitman::ui::kind_settings_git_browse })->bounds() };
    const luil::rect_f clear { tree->find({ gitman::ui::kind_settings_git_clear })->bounds() };
    const float controls_right { clear.x + clear.width };

    // 강조 바탕이 깔리는 블록의 좌우 끝이다. 배지 왼쪽 끝이 블록 왼쪽 끝이었다.
    const luil::rect_f panel { tree->find({ gitman::ui::kind_settings_dialog_panel })->bounds() };
    constexpr float content_left { 14.0f + 132.0f + 14.0f };
    const float block_left { panel.x + content_left };
    const float block_right { panel.x + panel.width - 14.0f };

    // 위 · 왼쪽 · 오른쪽 모두 여백이 있다. 전에는 셋 다 정확히 0이었다.
    REQUIRE(badge_bounds.x - block_left > 0.0f);
    REQUIRE(block_right - controls_right > 0.0f);
    REQUIRE(browse.x > block_left);

    // 배지는 블록 위쪽 끝에서 떨어져 있다. 블록 top은 그 위 항목의 끝 = 배지 top -
    // 여백이므로, 배지가 제목 줄보다 위에 있다는 기존 규칙과 함께 확인한다.
    REQUIRE(badge_bounds.y + badge_bounds.height <= clear.y);
    REQUIRE(badge_bounds.height >= 16.0f);
}

TEST_CASE("The font draft lands in the document on save", "[logic][settings-ui][font]")
{
    // 지시(2026-08-23): 글꼴에 문서 설정이 따로 먹지 않았다. 테마·키 컬러와 같은
    // 계층으로 올린다 — 문서가 열려 있으면 문서 override를 편집한다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code", u8"맑은 고딕" } });
    fixture.controller.handle(gitman::open_settings_intent {});

    // 문서에 정의가 없으면 앱 설정을 따른다.
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->ui_font_follows_app);
    REQUIRE(fixture.controller.make_view_snapshot()->settings_dialog->code_font_follows_app);

    // 글꼴 dialog가 초안에 쓴다.
    fixture.controller.handle(gitman::open_font_dialog_intent { true });
    const auto listed { fixture.controller.make_view_snapshot() };
    REQUIRE(listed->font_dialog.has_value());
    const auto index { std::find(listed->font_dialog->families.begin(), listed->font_dialog->families.end(), std::u8string { u8"Cascadia Code" }) };
    REQUIRE(index != listed->font_dialog->families.end());
    fixture.controller.handle(gitman::select_font_family_intent { static_cast<std::size_t>(index - listed->font_dialog->families.begin()) });
    fixture.controller.handle(gitman::confirm_font_dialog_intent {});

    // dialog가 열려 있는 동안에는 초안을 미리 보여 준다. 문서는 아직 그대로다.
    const auto drafted { fixture.controller.make_view_snapshot() };
    REQUIRE(drafted->fonts.code_family == u8"Cascadia Code");
    REQUIRE(drafted->settings_dialog->code_font_family == u8"Cascadia Code");
    REQUIRE_FALSE(drafted->settings_dialog->code_font_follows_app);
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) == nullptr);

    fixture.controller.handle(gitman::confirm_settings_intent {});
    const gitman::operation_request* const saved { last_of(fixture.submitter, gitman::operation_kind::save_document) };
    REQUIRE(saved != nullptr);
    REQUIRE(saved->document.has_value());
    REQUIRE(saved->document->fonts.code_family == std::optional<std::u8string> { u8"Cascadia Code" });
    // 건드리지 않은 UI 글꼴은 정의되지 않은 채 남는다.
    REQUIRE_FALSE(saved->document->fonts.ui_family.has_value());
    // 앱 설정은 그대로다 — 문서가 열려 있으면 문서 override다.
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_app_settings) == nullptr);

    // 저장 뒤의 유효 값은 문서 override다.
    REQUIRE(fixture.controller.make_view_snapshot()->fonts.code_family == u8"Cascadia Code");
}

TEST_CASE("The font override badge hands the row back to the app settings", "[logic][settings-ui][font]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { false });
    fixture.controller.handle(gitman::select_font_family_intent { 1 });
    fixture.controller.handle(gitman::confirm_font_dialog_intent {});
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->fonts.ui_family == u8"Cascadia Code");

    // 저장은 하나씩 나간다. 앞 저장이 끝나야 다음 저장이 제출된다.
    {
        const gitman::operation_request* const first { last_of(fixture.submitter, gitman::operation_kind::save_document) };
        REQUIRE(first != nullptr);
        gitman::document_saved_event done {};
        done.operation_id = first->operation_id;
        fixture.controller.handle(gitman::logic_message { std::move(done) });
        fixture.submitter.requests.clear();
    }

    // 배지가 문서 정의를 거두면 초안이 앱 값으로 돌아가고, 저장이 문서에서 지운다.
    fixture.controller.handle(gitman::open_settings_intent {});
    REQUIRE_FALSE(fixture.controller.make_view_snapshot()->settings_dialog->ui_font_follows_app);
    fixture.controller.handle(gitman::clear_settings_override_intent { gitman::settings_override_field::ui_font });

    const auto cleared { fixture.controller.make_view_snapshot() };
    REQUIRE(cleared->settings_dialog->ui_font_follows_app);
    REQUIRE(cleared->settings_dialog->ui_font_family.empty());

    fixture.controller.handle(gitman::confirm_settings_intent {});
    const gitman::operation_request* const saved { last_of(fixture.submitter, gitman::operation_kind::save_document) };
    REQUIRE(saved != nullptr);
    REQUIRE_FALSE(saved->document->fonts.ui_family.has_value());
    REQUIRE(fixture.controller.make_view_snapshot()->fonts.ui_family.empty());
}

TEST_CASE("Cancelling the settings dialog rolls the font preview back", "[logic][settings-ui][font]")
{
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { true });
    fixture.controller.handle(gitman::select_font_family_intent { 1 });
    fixture.controller.handle(gitman::confirm_font_dialog_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->fonts.code_family == u8"Cascadia Code");

    fixture.controller.handle(gitman::cancel_settings_dialog_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->fonts.code_family.empty());
    REQUIRE(last_of(fixture.submitter, gitman::operation_kind::save_document) == nullptr);
}

TEST_CASE("Without a document the font draft still edits the app settings", "[logic][settings-ui][font]")
{
    recording_submitter submitter {};
    gitman::logic_controller controller { submitter };
    // 앱 설정 읽기가 끝나야 저장이 나간다 (읽기 전 저장은 파일 항목을 지운다).
    controller.start();
    gitman::app_settings_loaded_event loaded {};
    loaded.operation_id = submitter.requests.front().operation_id;
    controller.handle(gitman::logic_message { std::move(loaded) });
    submitter.requests.clear();

    controller.handle(gitman::window_metrics_intent { 1280.0f, 900.0f, 1.0f });
    controller.handle(gitman::font_families_event { { u8"Cascadia Code" } });
    controller.handle(gitman::open_settings_intent {});
    REQUIRE_FALSE(controller.make_view_snapshot()->settings_dialog->document_mode);

    controller.handle(gitman::open_font_dialog_intent { false });
    controller.handle(gitman::select_font_family_intent { 1 });
    controller.handle(gitman::confirm_font_dialog_intent {});
    controller.handle(gitman::confirm_settings_intent {});

    REQUIRE(last_of(submitter, gitman::operation_kind::save_app_settings) != nullptr);
    REQUIRE(controller.make_view_snapshot()->fonts.ui_family == u8"Cascadia Code");
}

TEST_CASE("A composition in another box leaves the font list alone", "[logic][settings-ui][font][ime]")
{
    // 조합은 자기 칸의 목록만 좁힌다. 글꼴 필터의 조합은 목록이 달라지므로 선택을 지운다.
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code", u8"Malgun Gothic", u8"Consolas" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { true });
    fixture.controller.handle(gitman::select_font_family_intent { 1 });
    REQUIRE(fixture.controller.make_view_snapshot()->font_dialog->selected == 1u);

    gitman::set_text_composition_intent other {};
    other.target = gitman::text_input_target::settings_timeout;
    other.text = u8"12";
    other.caret = 2u;
    other.composing_end = 2u;
    other.composing = true;
    fixture.controller.handle(other);
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->font_dialog->names.size() == 4u);
        REQUIRE(view->font_dialog->selected == 1u);
    }

    fixture.controller.handle(compose_font_query(u8"Malg"));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->font_dialog->names.size() == 1u);
        REQUIRE(view->font_dialog->selected.has_value() == false);
    }
}

TEST_CASE("A composing font query narrows the list and falls back to the committed text", "[logic][settings-ui][font][ime]")
{
    // 점진 검색의 두 줄 규칙이다 (luil `search_query`와 같다). 조합 글이 섞인 표시
    // 글로 먼저 거르고, 그 글로 하나도 맞지 않으면 확정 글로 물러선다 — 단 확정
    // 글이 비어 있으면 물러서지 않는다 (빈 질의는 "전부"라서 목록이 되살아난다).
    settings_fixture fixture {};
    fixture.controller.handle(gitman::font_families_event { { u8"Cascadia Code", u8"Malgun Gothic", u8"Consolas" } });
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::open_font_dialog_intent { true });

    // 기본 글꼴 행까지 넷이다.
    REQUIRE(fixture.controller.make_view_snapshot()->font_dialog->names.size() == 4u);

    // 확정 글이 없어도 조합 글만으로 좁아진다. 기본 글꼴 행이 질의에 걸리는 규칙은
    // 그대로다.
    fixture.controller.handle(compose_font_query(u8"기본"));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->font_dialog->query.text.empty());
        REQUIRE(view->font_dialog->names.size() == 1u);
        REQUIRE(view->font_dialog->names[0] == u8"(기본 글꼴)");
    }

    fixture.controller.handle(compose_font_query(u8"Malg"));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->font_dialog->names.size() == 1u);
        REQUIRE(view->font_dialog->names[0] == u8"Malgun Gothic");
    }

    // 맞는 것이 없어도 확정 글이 비어 있으면 물러서지 않는다.
    fixture.controller.handle(compose_font_query(u8"Malz"));
    REQUIRE(fixture.controller.make_view_snapshot()->font_dialog->names.empty());

    // 조합이 끝나면 확정 글(아직 빈 글) 기준으로 돌아온다.
    fixture.controller.handle(end_font_composition());
    REQUIRE(fixture.controller.make_view_snapshot()->font_dialog->names.size() == 4u);

    // 확정 글이 있으면 맞지 않는 조합 글에서 그리로 물러선다.
    fixture.controller.handle(font_query_insert(u8"Cons"));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->font_dialog->names.size() == 1u);
        REQUIRE(view->font_dialog->names[0] == u8"Consolas");
    }

    fixture.controller.handle(compose_font_query(u8"Consz", 4u));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->font_dialog->query.text == u8"Cons");
        REQUIRE(view->font_dialog->names.size() == 1u);
        REQUIRE(view->font_dialog->names[0] == u8"Consolas");
    }

    fixture.controller.handle(end_font_composition());
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->font_dialog->names.size() == 1u);
        REQUIRE(view->font_dialog->names[0] == u8"Consolas");
    }
}
