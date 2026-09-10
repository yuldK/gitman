#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "domain/command_definition.h"
#include "domain/text_edit.h"
#include "helpers/ui_test_support.h"
#include "presentation/ui/command_editor_dialog_element.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/settings_dialog_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 명령·훅 편집 UI다 (REQ-020·REQ-021). 목록은 환경설정 `명령` 탭이고, 항목 하나는
// 그 위에 뜨는 편집 dialog가 고친다. 스크립트 **내용**은 다루지 않는다 — 외부
// 편집기가 할 일이라는 사용자 결정(2026-09-10)이라, 여기서 확인하는 것은 경로를
// 고르고 편집기로 넘기는 데까지다.
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

    // 문서 하나를 연 controller다. 문서가 있어야 상대 경로를 풀 수 있고 `덮어씀`
    // 배지도 뜻을 갖는다.
    struct editor_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        editor_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 1280.0f, 800.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });

            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            gitman::project_definition alpha {};
            alpha.id.value = u8"alpha";
            alpha.display_name = u8"alpha";
            alpha.path.original = u8"C:\\work\\alpha";
            alpha.path.normalized = alpha.path.original;
            document.projects.push_back(std::move(alpha));
            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));
            submitter.requests.clear();

            controller.handle(gitman::open_settings_intent {});
            controller.handle(gitman::select_settings_tab_intent { gitman::settings_tab::commands });
        }

        // 편집 dialog의 칸 하나에 글을 넣는다. 키 입력 한 번과 같은 경로다.
        void type(const gitman::text_input_target target, const std::u8string_view text)
        {
            gitman::edit_text_input_intent intent {};
            intent.target = target;
            intent.command = gitman::text_edit_command::insert;
            intent.text = text;
            controller.handle(std::move(intent));
        }

        [[nodiscard]] gitman::command_editor_dialog_view editor() const
        {
            const auto view { controller.make_view_snapshot() };
            REQUIRE(view->command_editor.has_value());
            return *view->command_editor;
        }

        [[nodiscard]] gitman::settings_dialog_view settings() const
        {
            const auto view { controller.make_view_snapshot() };
            REQUIRE(view->settings_dialog.has_value());
            return *view->settings_dialog;
        }

        // 이름·스크립트만 채워 확인까지 마친 명령 하나를 만든다.
        void add_command(const std::u8string_view label, const std::u8string_view script)
        {
            controller.handle(gitman::open_command_editor_intent { false, 0u, true });
            type(gitman::text_input_target::command_name, label);
            type(gitman::text_input_target::command_script, script);
            controller.handle(gitman::confirm_command_editor_intent {});
        }
    };

    // element가 등록한 왼쪽 클릭 액션을 그 자리에서 실행한다.
    std::vector<luil::input_action> click(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const action { element->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }
} // namespace

TEST_CASE("Adding a command needs a name and a script before it can be confirmed", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });

    // 목록이 비어 있으므로 자리 번호 0도 "만드는 중"이다.
    gitman::command_editor_dialog_view view { fixture.editor() };
    REQUIRE(view.title == u8"명령 추가");
    REQUIRE(view.hook == false);
    REQUIRE(view.can_confirm == false);
    REQUIRE(view.message.empty() == false);

    fixture.type(gitman::text_input_target::command_name, u8"빌드");
    REQUIRE(fixture.editor().can_confirm == false);

    fixture.type(gitman::text_input_target::command_script, u8"scripts\\build.py");
    view = fixture.editor();
    REQUIRE(view.can_confirm);
    REQUIRE(view.message.empty());

    fixture.controller.handle(gitman::confirm_command_editor_intent {});
    const auto after { fixture.controller.make_view_snapshot() };
    REQUIRE(after->command_editor.has_value() == false);
    REQUIRE(after->settings_dialog->commands.size() == 1u);
    REQUIRE(after->settings_dialog->commands.front().label == u8"빌드");
    // 문서 모드라 목록을 고치면 그 묶음이 문서 정의가 된다 (G3.2 `덮어씀`).
    REQUIRE(after->settings_dialog->commands_follows_app == false);
}

TEST_CASE("Confirming an invalid command changes nothing", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    fixture.type(gitman::text_input_target::command_name, u8"이름만");
    fixture.controller.handle(gitman::confirm_command_editor_intent {});

    // 버튼이 비활성이어도 늦게 도착한 확인이 있을 수 있다. 목록도 dialog도 그대로다.
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->command_editor.has_value());
    REQUIRE(view->settings_dialog->commands.empty());
}

TEST_CASE("Editing an existing command replaces it in place and keeps its id", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"빌드", u8"scripts\\build.py");
    fixture.add_command(u8"테스트", u8"scripts\\test.py");

    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u });
    gitman::command_editor_dialog_view view { fixture.editor() };
    REQUIRE(view.title == u8"명령 편집");
    REQUIRE(view.name.text == u8"빌드");
    REQUIRE(view.script.text == u8"scripts\\build.py");

    // 커서가 끝에 있으므로 이어 쓰면 뒤에 붙는다.
    fixture.type(gitman::text_input_target::command_name, u8" 전체");
    fixture.controller.handle(gitman::confirm_command_editor_intent {});

    const gitman::settings_dialog_view settings { fixture.settings() };
    REQUIRE(settings.commands.size() == 2u);
    REQUIRE(settings.commands[0].label == u8"빌드 전체");
    REQUIRE(settings.commands[1].label == u8"테스트");
}

TEST_CASE("Command rows move within the list and stop at the ends", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"하나", u8"a.py");
    fixture.add_command(u8"둘", u8"b.py");

    fixture.controller.handle(gitman::move_command_intent { false, 1u, -1 });
    REQUIRE(fixture.settings().commands[0].label == u8"둘");

    // 첫 항목의 위, 마지막 항목의 아래, 없는 자리는 아무 일도 하지 않는다.
    fixture.controller.handle(gitman::move_command_intent { false, 0u, -1 });
    fixture.controller.handle(gitman::move_command_intent { false, 1u, 1 });
    fixture.controller.handle(gitman::move_command_intent { false, 9u, 1 });
    const gitman::settings_dialog_view settings { fixture.settings() };
    REQUIRE(settings.commands[0].label == u8"둘");
    REQUIRE(settings.commands[1].label == u8"하나");
    // 첫 줄은 위로, 마지막 줄은 아래로 갈 수 없다 — 버튼이 비활성이다.
    REQUIRE(settings.commands[0].can_move_up == false);
    REQUIRE(settings.commands[0].can_move_down);
    REQUIRE(settings.commands[1].can_move_up);
    REQUIRE(settings.commands[1].can_move_down == false);
}

TEST_CASE("Removing a command closes an editor open on the same list", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"하나", u8"a.py");
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u });
    REQUIRE(fixture.controller.make_view_snapshot()->command_editor.has_value());

    fixture.controller.handle(gitman::remove_command_intent { false, 0u });
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog->commands.empty());
    // 자리 번호가 밀려 다른 항목을 덮어쓰지 않도록 편집도 닫힌다.
    REQUIRE(view->command_editor.has_value() == false);
}

TEST_CASE("Cycling command choices walks every value and comes back", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });

    const std::u8string first_directory { fixture.editor().working_directory_label };
    const std::u8string first_vcs { fixture.editor().vcs_label };
    std::vector<std::u8string> directories {};
    std::vector<std::u8string> targets {};
    for (int step = 0; step < 3; ++step)
    {
        fixture.controller.handle(gitman::cycle_command_choice_intent { gitman::command_choice::working_directory });
        fixture.controller.handle(gitman::cycle_command_choice_intent { gitman::command_choice::target_vcs });
        directories.push_back(fixture.editor().working_directory_label);
        targets.push_back(fixture.editor().vcs_label);
    }

    // 세 값이 모두 다르고 세 번 누르면 처음으로 돌아온다.
    REQUIRE(directories[0] != directories[1]);
    REQUIRE(directories[1] != directories[2]);
    REQUIRE(directories[2] == first_directory);
    REQUIRE(targets[0] != targets[1]);
    REQUIRE(targets[1] != targets[2]);
    REQUIRE(targets[2] == first_vcs);
}

TEST_CASE("Command flags toggle on the editor draft only", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    const bool show { fixture.editor().show_on_card };
    const bool refresh { fixture.editor().refresh_after };

    fixture.controller.handle(gitman::toggle_command_flag_intent { gitman::command_flag::show_on_card });
    fixture.controller.handle(gitman::toggle_command_flag_intent { gitman::command_flag::refresh_after });
    REQUIRE(fixture.editor().show_on_card == (show == false));
    REQUIRE(fixture.editor().refresh_after == (refresh == false));

    // 편집이 닫혀 있으면 명령 항목 토글은 아무 일도 하지 않는다.
    fixture.controller.handle(gitman::close_command_editor_intent {});
    fixture.controller.handle(gitman::toggle_command_flag_intent { gitman::command_flag::show_on_card });
    REQUIRE(fixture.controller.make_view_snapshot()->command_editor.has_value() == false);
}

TEST_CASE("The TortoiseSVN toggle lives on the tab, not the editor", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    const bool before { fixture.settings().use_tortoise_svn };
    fixture.controller.handle(gitman::toggle_command_flag_intent { gitman::command_flag::use_tortoise_svn });

    const gitman::settings_dialog_view settings { fixture.settings() };
    REQUIRE(settings.use_tortoise_svn == (before == false));
    REQUIRE(settings.hooks_follows_app == false);
}

TEST_CASE("The hook editor shows the phase instead of a name", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { true, 0u, true });

    gitman::command_editor_dialog_view view { fixture.editor() };
    REQUIRE(view.hook);
    REQUIRE(view.title == u8"훅 추가");
    REQUIRE(view.phase_label.empty() == false);

    const std::u8string first { view.phase_label };
    fixture.controller.handle(gitman::cycle_command_choice_intent { gitman::command_choice::hook_phase });
    REQUIRE(fixture.editor().phase_label != first);

    fixture.type(gitman::text_input_target::command_script, u8"hooks\\pre.ps1");
    REQUIRE(fixture.editor().can_confirm);
    fixture.controller.handle(gitman::confirm_command_editor_intent {});

    const gitman::settings_dialog_view settings { fixture.settings() };
    REQUIRE(settings.hooks.size() == 1u);
    REQUIRE(settings.hooks.front().detail.find(u8"hooks\\pre.ps1") != std::u8string::npos);
    REQUIRE(settings.hooks_follows_app == false);
}

TEST_CASE("A picked script path lands in the script box", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    fixture.controller.handle(gitman::set_command_script_intent { u8"C:\\tools\\build.ps1" });
    REQUIRE(fixture.editor().script.text == u8"C:\\tools\\build.ps1");

    // 절대 경로는 그대로 편집기로 넘어간다.
    REQUIRE(fixture.editor().script_open_path == u8"C:\\tools\\build.ps1");

    // dialog가 닫힌 뒤 도착한 선택 결과는 버린다.
    fixture.controller.handle(gitman::close_command_editor_intent {});
    fixture.controller.handle(gitman::set_command_script_intent { u8"C:\\tools\\other.ps1" });
    REQUIRE(fixture.controller.make_view_snapshot()->command_editor.has_value() == false);
}

TEST_CASE("A relative script path is resolved against the document folder", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    fixture.type(gitman::text_input_target::command_script, u8"scripts\\build.py");

    const gitman::command_editor_dialog_view view { fixture.editor() };
    REQUIRE(view.script_open_path == u8"C:\\work\\scripts\\build.py");
    REQUIRE(view.script_browse_folder == u8"C:\\work");
}

TEST_CASE("The commands tab lists every row with its own move, edit and remove buttons", "[ui][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"빌드", u8"build.py");
    fixture.add_command(u8"테스트", u8"test.py");

    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::testing::make_tree(*view) };

    const std::u8string first { gitman::ui::settings_command_row_owner(false, 0u) };
    const std::u8string second { gitman::ui::settings_command_row_owner(false, 1u) };
    REQUIRE(first != second);

    // 첫 줄의 위 버튼은 비활성이라 누를 액션이 없다.
    const luil::ui_element* const up { tree->find({ gitman::ui::kind_settings_command_row_up, first }) };
    REQUIRE(up != nullptr);
    REQUIRE(up->enabled() == false);

    const auto down { click(*tree, { gitman::ui::kind_settings_command_row_down, first }) };
    const auto* const move { gitman::testing::as_intent<gitman::move_command_intent>(down) };
    REQUIRE(move != nullptr);
    REQUIRE(move->hook == false);
    REQUIRE(move->index == 0u);
    REQUIRE(move->delta == 1);

    // 줄마다 자리 번호가 다르다 — 예전 컨텍스트 메뉴처럼 첫 줄로 몰리지 않는다.
    const auto edit { click(*tree, { gitman::ui::kind_settings_command_row_edit, second }) };
    const auto* const open { gitman::testing::as_intent<gitman::open_command_editor_intent>(edit) };
    REQUIRE(open != nullptr);
    REQUIRE(open->index == 1u);

    const auto remove { click(*tree, { gitman::ui::kind_settings_command_row_remove, second }) };
    const auto* const erase { gitman::testing::as_intent<gitman::remove_command_intent>(remove) };
    REQUIRE(erase != nullptr);
    REQUIRE(erase->index == 1u);
}

TEST_CASE("The add buttons say they are creating rather than pointing past the end", "[ui][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"빌드", u8"build.py");

    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::testing::make_tree(*view) };

    // 액션 목록을 지역에 붙든다 — intent 포인터가 그 안을 가리킨다.
    const auto command_actions { click(*tree, { gitman::ui::kind_settings_command_add, u8"" }) };
    const auto* const command { gitman::testing::as_intent<gitman::open_command_editor_intent>(command_actions) };
    REQUIRE(command != nullptr);
    REQUIRE(command->hook == false);
    // 자리 번호가 아니라 뜻을 나른다. 목록이 그 사이에 바뀌어도 추가는 추가다.
    REQUIRE(command->creating);

    const auto hook_actions { click(*tree, { gitman::ui::kind_settings_hook_add, u8"" }) };
    const auto* const hook { gitman::testing::as_intent<gitman::open_command_editor_intent>(hook_actions) };
    REQUIRE(hook != nullptr);
    REQUIRE(hook->hook);
    REQUIRE(hook->creating);
}

TEST_CASE("The editor dialog sits above the settings dialog with the right rows", "[ui][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });

    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::testing::make_tree(*view) };
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_settings_dialog) != nullptr);
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_dialog) != nullptr);

    // 명령은 이름·아이콘·표면 표시가 있고 훅은 단계가 있다. 줄 수가 다르다.
    REQUIRE(gitman::ui::command_editor_row_count(false) > gitman::ui::command_editor_row_count(true));

    // 명령 편집에만 있는 칸들이다.
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_name_input) != nullptr);
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_icon_input) != nullptr);
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_phase) == nullptr);

    // 편집 dialog는 환경설정보다 뒤에 붙어 Esc를 먼저 받는다 (설계 D2-A).
    const luil::rect_f editor { gitman::testing::bounds_of(*tree, gitman::ui::kind_command_editor_dialog) };
    REQUIRE(editor.width > 0.0f);
    REQUIRE(editor.height > 0.0f);
}

TEST_CASE("The hook editor drops the command only rows", "[ui][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { true, 0u, true });

    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::testing::make_tree(*view) };
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_phase) != nullptr);
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_enabled) != nullptr);
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_name_input) == nullptr);
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_icon_input) == nullptr);
    REQUIRE(gitman::testing::find(*tree, gitman::ui::kind_command_editor_show_on_card) == nullptr);
}

TEST_CASE("The editor hands the script off to Win32 and to the external editor", "[ui][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    fixture.controller.handle(gitman::set_command_script_intent { u8"C:\\tools\\build.ps1" });

    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::testing::make_tree(*view) };

    // 파일 dialog는 UI thread의 것이라 앱 명령으로 나간다 (설계 L7). 인자는 시작 폴더다.
    const auto browse { click(*tree, { gitman::ui::kind_command_editor_script_browse, u8"" }) };
    const luil::app_ui_command* const picker { gitman::testing::as_app_command(browse) };
    REQUIRE(picker != nullptr);
    REQUIRE(gitman::parse_app_command(picker->command) == gitman::app_command::show_command_script_picker);
    REQUIRE(picker->argument == u8"C:\\work");

    // 스크립트 **내용**은 외부 편집기가 고친다 (2026-09-10 지시).
    const auto open { click(*tree, { gitman::ui::kind_command_editor_script_open, u8"" }) };
    const luil::app_ui_command* const editor { gitman::testing::as_app_command(open) };
    REQUIRE(editor != nullptr);
    REQUIRE(gitman::parse_app_command(editor->command) == gitman::app_command::open_in_vscode);
    REQUIRE(editor->argument == u8"C:\\tools\\build.ps1");
}

TEST_CASE("The editor buttons cancel and confirm", "[ui][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    fixture.type(gitman::text_input_target::command_name, u8"빌드");
    fixture.type(gitman::text_input_target::command_script, u8"build.py");

    const auto view { fixture.controller.make_view_snapshot() };
    const auto tree { gitman::testing::make_tree(*view) };

    REQUIRE(gitman::testing::as_intent<gitman::close_command_editor_intent>(click(*tree, { gitman::ui::kind_command_editor_cancel, u8"" })) != nullptr);
    REQUIRE(gitman::testing::as_intent<gitman::confirm_command_editor_intent>(click(*tree, { gitman::ui::kind_command_editor_confirm, u8"" })) != nullptr);
}

TEST_CASE("Every editor text box routes to its own text target", "[ui][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });

    const gitman::ui::gitman_policy policy {};
    const std::vector<std::pair<luil::ui_element_kind, gitman::text_input_target>> pairs {
        { gitman::ui::kind_command_editor_name_input, gitman::text_input_target::command_name },
        { gitman::ui::kind_command_editor_script_input, gitman::text_input_target::command_script },
        { gitman::ui::kind_command_editor_arguments_input, gitman::text_input_target::command_arguments },
        { gitman::ui::kind_command_editor_icon_input, gitman::text_input_target::command_icon },
        { gitman::ui::kind_command_editor_path_pattern_input, gitman::text_input_target::command_path_pattern },
        { gitman::ui::kind_command_editor_projects_input, gitman::text_input_target::command_projects },
        { gitman::ui::kind_command_editor_timeout_input, gitman::text_input_target::command_timeout },
    };

    for (const auto& [kind, target] : pairs)
    {
        const std::optional<luil::text_input_target> routed { policy.text_target_of(kind) };
        REQUIRE(routed.has_value());
        // 왕복이 표 하나를 거치므로 값이 갈릴 자리가 없다 (설계 L4).
        REQUIRE(gitman::ui::from_luil_target(*routed) == target);
    }
}

TEST_CASE("Arguments, projects and the timeout round trip through their text boxes", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    fixture.type(gitman::text_input_target::command_name, u8"빌드");
    fixture.type(gitman::text_input_target::command_script, u8"build.py");
    fixture.type(gitman::text_input_target::command_arguments, u8"--config \"Release With Space\" -j8");
    fixture.type(gitman::text_input_target::command_projects, u8"alpha, beta");
    fixture.type(gitman::text_input_target::command_timeout, u8"90");
    fixture.controller.handle(gitman::confirm_command_editor_intent {});

    // 다시 열면 같은 글이 보인다 — 나누기와 합치기가 서로의 역이다.
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u });
    const gitman::command_editor_dialog_view view { fixture.editor() };
    REQUIRE(view.arguments.text == u8"--config \"Release With Space\" -j8");
    REQUIRE(view.projects.text == u8"alpha, beta");
    REQUIRE(view.timeout.text == u8"90");
}

TEST_CASE("Editing a row that no longer exists does nothing", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"하나", u8"a.py");
    fixture.add_command(u8"둘", u8"b.py");

    // 지우기와 편집이 같은 프레임에 들어오면 편집의 자리 번호가 한 칸 밀려 있다.
    fixture.controller.handle(gitman::remove_command_intent { false, 0u });
    fixture.controller.handle(gitman::open_command_editor_intent { false, 1u });
    REQUIRE(fixture.controller.make_view_snapshot()->command_editor.has_value() == false);

    // 남은 줄은 그대로 열린다.
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u });
    REQUIRE(fixture.editor().name.text == u8"둘");
}

TEST_CASE("Reordering the list closes an open editor", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"하나", u8"a.py");
    fixture.add_command(u8"둘", u8"b.py");
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u });

    fixture.controller.handle(gitman::move_command_intent { false, 0u, 1 });
    // 자리 번호가 다른 항목을 가리키게 되므로 편집을 남겨 두지 않는다.
    REQUIRE(fixture.controller.make_view_snapshot()->command_editor.has_value() == false);
}

TEST_CASE("Closing or swapping the document drops the editor draft", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"하나", u8"a.py");
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u });

    fixture.controller.handle(gitman::close_document_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->command_editor.has_value() == false);

    // 환경설정을 다시 열어도 이전 문서의 초안이 되살아나지 않는다.
    fixture.controller.handle(gitman::open_settings_intent {});
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->settings_dialog.has_value());
    REQUIRE(view->command_editor.has_value() == false);
}

TEST_CASE("Reopening the settings dialog drops a stale editor draft", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.add_command(u8"하나", u8"a.py");
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u });
    fixture.controller.handle(gitman::open_settings_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->command_editor.has_value() == false);
}

TEST_CASE("The timeout box keeps only digits", "[logic][commands-ui]")
{
    editor_fixture fixture {};
    fixture.controller.handle(gitman::open_command_editor_intent { false, 0u, true });
    fixture.type(gitman::text_input_target::command_timeout, u8"60초 ");
    // 숫자가 아닌 글자는 남지 않는다. 남기면 `600`이 통째로 "제한 없음"이 된다.
    REQUIRE(fixture.editor().timeout.text == u8"60");
}

TEST_CASE("Arguments with quotes, spaces and tabs survive the round trip", "[domain][commands-ui]")
{
    const std::vector<std::u8string> arguments {
        u8"--config",
        u8"Release With Space",
        u8"a\"b",
        u8"\"",
        u8"tab\there",
        u8"",
        u8"plain",
    };

    const std::u8string text { gitman::join_command_arguments(arguments) };
    REQUIRE(gitman::split_command_arguments(text) == arguments);

    // 빈 인자를 적는 방법은 여전히 `""` 하나다.
    REQUIRE(gitman::split_command_arguments(u8"\"\"") == std::vector<std::u8string> { u8"" });
}
