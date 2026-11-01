#include "application/logic_controller.h"
#include "helpers/git_repository_fixture.h"
#include "helpers/ui_test_support.h"
#include "infrastructure/vcs_operation_executor.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/switch_dialog_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using gitman::testing::as_intent;
    using gitman::testing::at;
    using gitman::testing::bounds_of;
    using gitman::testing::find;
    using gitman::testing::id_of;
    using gitman::testing::interaction_harness;
    using gitman::testing::make_tree;

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

    class null_project_store final : public gitman::project_store
    {
    public:
        [[nodiscard]] gitman::project_store_load_result load(std::u8string_view) noexcept override
        {
            return {};
        }

        [[nodiscard]] gitman::project_store_save_result save(std::u8string_view, const gitman::workspace_document&, const gitman::workspace_revision_token&) noexcept override
        {
            return {};
        }
    };

    gitman::switch_candidate make_candidate(const std::u8string_view display, const gitman::switch_candidate_kind kind, const bool requires_tracking = false)
    {
        gitman::switch_candidate candidate {};
        candidate.kind = kind;
        candidate.display_name = display;
        candidate.target = display;
        candidate.requires_tracking_branch = requires_tracking;
        return candidate;
    }

    // 문서와 준비된 카드(현재 branch main), 열린 switch dialog까지 갖춘 controller다.
    struct dialog_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };
        std::uint64_t candidates_operation_id { 0 };

        explicit dialog_fixture(const gitman::repository_kind kind = gitman::repository_kind::git)
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_document_intent { u8"C:\\work\\p.version-list" });
            gitman::document_loaded_event loaded {};
            gitman::workspace_document document {};
            document.document_path = u8"C:\\work\\p.version-list";
            gitman::project_definition project {};
            project.id.value = u8"alpha";
            project.display_name = u8"alpha";
            project.path.original = u8"C:\\work\\alpha";
            project.path.normalized = project.path.original;
            project.hint = kind == gitman::repository_kind::subversion ? gitman::vcs_hint::subversion : gitman::vcs_hint::git;
            document.projects.push_back(std::move(project));
            loaded.document = { std::move(document) };
            controller.handle(std::move(loaded));

            gitman::query_completed_event local {};
            local.id.value = u8"alpha";
            local.generation = 1;
            local.final_event = true;
            local.result.snapshot.project.value = u8"alpha";
            local.result.snapshot.kind = kind;
            local.result.snapshot.availability = gitman::repository_availability::ready;
            local.result.snapshot.current_reference = kind == gitman::repository_kind::subversion ? u8"^/trunk" : u8"main";
            local.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
            controller.handle(std::move(local));
            submitter.requests.clear();

            controller.handle(gitman::begin_switch_intent { gitman::project_id { u8"alpha" } });
            REQUIRE(submitter.requests.size() == 1u);
            candidates_operation_id = submitter.requests.front().operation_id;
        }

        void deliver_candidates(std::vector<gitman::switch_candidate> candidates, const bool stale = false)
        {
            gitman::switch_candidates_event event {};
            event.operation_id = candidates_operation_id;
            event.id.value = u8"alpha";
            event.result.candidates = std::move(candidates);
            event.result.stale = stale;
            controller.handle(std::move(event));
        }

        std::uint64_t deliver_svn_browser(const std::u8string_view root = u8"https://svn.example.com/repo", const std::u8string_view current = u8"https://svn.example.com/repo/trunk")
        {
            gitman::switch_candidates_event event {};
            event.operation_id = candidates_operation_id;
            event.id.value = u8"alpha";
            event.result.svn_browser = { gitman::svn_repository_browser_info { std::u8string { root }, std::u8string { current } } };
            controller.handle(std::move(event));
            REQUIRE(submitter.requests.empty() == false);
            REQUIRE(submitter.requests.back().kind == gitman::operation_kind::query_svn_directory);
            return submitter.requests.back().operation_id;
        }

        void deliver_svn_directory(
            const std::uint64_t operation_id, const std::u8string_view url, std::vector<std::u8string> directories, const gitman::svn_browser_query_error error = gitman::svn_browser_query_error::none)
        {
            gitman::svn_directory_event event {};
            event.operation_id = operation_id;
            event.id.value = u8"alpha";
            event.url = url;
            event.result.directories = std::move(directories);
            event.result.error = error;
            controller.handle(std::move(event));
        }
    };

    // 질의 막대에 글자를 한 개씩 친다 (WM_CHAR 한 번과 같다).
    void type_query(gitman::logic_controller& controller, const std::u8string_view text)
    {
        for (const char8_t value : text)
        {
            gitman::edit_text_input_intent intent {};
            intent.target = gitman::text_input_target::switch_query;
            intent.command = gitman::text_edit_command::insert;
            intent.text.push_back(value);
            controller.handle(std::move(intent));
        }
    }

    // 질의 막대의 편집 명령 하나다.
    gitman::edit_text_input_intent query_command(const gitman::text_edit_command command)
    {
        gitman::edit_text_input_intent intent {};
        intent.target = gitman::text_input_target::switch_query;
        intent.command = command;
        return intent;
    }

    // 질의 막대의 조합 상태다 (TSF가 보내는 것과 같다). `text`는 조합 중인 글자까지
    // 담은 박스 전체 표시 글이고, `composing_begin`부터 끝까지가 조합 구간이다.
    gitman::set_text_composition_intent compose_query(const std::u8string_view text, const std::size_t composing_begin = 0u)
    {
        gitman::set_text_composition_intent intent {};
        intent.target = gitman::text_input_target::switch_query;
        intent.text = text;
        intent.caret = text.size();
        intent.composing_begin = composing_begin;
        intent.composing_end = text.size();
        intent.composing = true;
        return intent;
    }

    // 조합이 끝났음을 알린다. 확정된 글은 별도의 편집 intent로 온다.
    gitman::set_text_composition_intent end_query_composition()
    {
        gitman::set_text_composition_intent intent {};
        intent.target = gitman::text_input_target::switch_query;
        intent.composing = false;
        return intent;
    }

    // element에 걸린 왼쪽 클릭 액션을 그 자리에서 실행한다 (조립 확인용).
    std::vector<luil::input_action> click_action(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const action { element->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }

    // dialog 하나만 띄운 창 800×600·배율 1의 view다.
    gitman::view_snapshot make_dialog_view(gitman::switch_dialog_view dialog)
    {
        gitman::view_snapshot view {};
        view.window_width = 800.0f;
        view.window_height = 600.0f;
        view.scale = 1.0f;
        view.switch_dialog = { std::move(dialog) };
        return view;
    }

    gitman::switch_dialog_view make_dialog(const std::u8string_view confirm_label = u8"전환 실행")
    {
        gitman::switch_dialog_view dialog {};
        dialog.card.value = u8"alpha";
        dialog.title = u8"alpha";
        dialog.loading = false;
        dialog.confirm_label = confirm_label;
        return dialog;
    }

    // 질의 칸 id는 dialog가 붙은 카드의 id를 owner로 쓴다.
    const luil::ui_element_id query_input_id { gitman::ui::kind_switch_dialog_query_input, std::u8string { u8"alpha" } };

    // 질의 칸을 눌러 초점을 준다. 누름이 caret 자리 액션을 내므로 click 도우미를
    // 쓰지 않는다.
    void focus_query(interaction_harness& harness, const luil::rect_f& box, const int time_milliseconds = 0)
    {
        static_cast<void>(harness.process(luil::pointer_pressed_event { box.x + 4.0f, box.y + 4.0f, luil::pointer_button::left, at(time_milliseconds) }));
        static_cast<void>(harness.process(luil::pointer_released_event { box.x + 4.0f, box.y + 4.0f, luil::pointer_button::left, at(time_milliseconds + 10) }));
        REQUIRE(harness.snapshot().focused_input == query_input_id);
    }

    // 편집 키 하나가 만든 편집 intent다.
    gitman::edit_text_input_intent edit_of(const std::vector<luil::input_action>& actions)
    {
        const auto* const intent { as_intent<gitman::edit_text_input_intent>(actions) };
        REQUIRE(intent != nullptr);
        return *intent;
    }
} // namespace

TEST_CASE("Opening the switch dialog queries candidates and shows the loading state", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::query_switch_candidates);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog.has_value());
    REQUIRE(view->switch_dialog->loading);
    REQUIRE(view->switch_dialog->can_confirm == false);
}

TEST_CASE("Delivered candidates fill the dialog and late results are discarded", "[logic][switch-ui]")
{
    dialog_fixture fixture {};

    // 다른 조회의 늦은 결과는 무시된다.
    gitman::switch_candidates_event stale_event {};
    stale_event.operation_id = fixture.candidates_operation_id + 7;
    stale_event.id.value = u8"alpha";
    stale_event.result.candidates.push_back(make_candidate(u8"ghost", gitman::switch_candidate_kind::git_local_branch));
    fixture.controller.handle(std::move(stale_event));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->loading);

    fixture.deliver_candidates(
        { make_candidate(u8"origin/feature", gitman::switch_candidate_kind::git_remote_branch, true), make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) }, true);
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->loading == false);
    REQUIRE(view->switch_dialog->stale);
    REQUIRE(view->switch_dialog->candidates.size() == 2u);
    // remote group이 먼저다 (provider가 만든 순서를 그대로 쓴다).
    REQUIRE(view->switch_dialog->candidates.front().kind == gitman::switch_candidate_kind::git_remote_branch);
}

TEST_CASE("Selecting the current reference blocks the confirm with a message", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"main", gitman::switch_candidate_kind::git_local_branch), make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) });

    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->can_confirm == false);
        REQUIRE(view->switch_dialog->message.empty() == false);
    }

    fixture.controller.handle(gitman::select_switch_candidate_intent { 1 });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->can_confirm);
        REQUIRE(view->switch_dialog->confirm_label == u8"전환 실행");
    }

    // 검증에 걸린 후보로는 확인해도 명령이 만들어지지 않는다 (REQ-007).
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("A tracking branch candidate needs the two step confirmation", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"origin/feature", gitman::switch_candidate_kind::git_remote_branch, true) });
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->confirm_label == u8"브랜치 만들고 전환");

    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    // 첫 확인은 실행하지 않고 안내만 띄운다.
    REQUIRE(fixture.submitter.requests.empty());
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->confirm_label == u8"생성 확인");
        REQUIRE(view->switch_dialog->message.empty() == false);
    }

    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& request { fixture.submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::switch_to);
    REQUIRE(request.switch_target.has_value());
    REQUIRE(request.switch_target->tracking_branch_confirmed);
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->executing);
}

TEST_CASE("A rejected switch keeps the dialog open with the reason and an executed one closes it", "[logic][switch-ui]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) });
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request request { fixture.submitter.requests.front() };

    SECTION("재검증 거부는 dialog에 사유를 남긴다 (REQ-007)")
    {
        gitman::change_completed_event rejected {};
        rejected.operation_id = request.operation_id;
        rejected.id = request.project.id;
        rejected.kind = gitman::operation_kind::switch_to;
        rejected.result.executed = false;
        rejected.result.rejected_by = gitman::switch_rejection::working_tree_unsafe;
        fixture.controller.handle(std::move(rejected));

        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog.has_value());
        REQUIRE(view->switch_dialog->executing == false);
        REQUIRE(view->switch_dialog->message.empty() == false);
    }

    SECTION("실행된 전환은 성패와 관계없이 dialog를 닫는다")
    {
        gitman::change_completed_event executed {};
        executed.operation_id = request.operation_id;
        executed.id = request.project.id;
        executed.kind = gitman::operation_kind::switch_to;
        executed.result.executed = true;
        executed.result.succeeded = true;
        executed.result.snapshot.availability = gitman::repository_availability::ready;
        executed.result.snapshot.current_reference = u8"develop";
        fixture.controller.handle(std::move(executed));
        REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog.has_value() == false);
    }

    SECTION("취소 intent는 dialog를 닫는다")
    {
        fixture.controller.handle(gitman::cancel_switch_dialog_intent {});
        REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog.has_value() == false);
    }
}

TEST_CASE("SVN switch dialog loads the repository tree and switches the selected directory", "[logic][switch-ui][svn]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser() };
    const gitman::operation_request& root_request { fixture.submitter.requests.back() };
    REQUIRE(root_request.svn_repository_root_url == u8"https://svn.example.com/repo");
    REQUIRE(root_request.svn_directory_url == u8"https://svn.example.com/repo");

    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_browser);
        REQUIRE(view->switch_dialog->svn_rows.size() == 2u);
        REQUIRE(view->switch_dialog->svn_rows[1].kind == gitman::svn_browser_row_kind::loading);
        REQUIRE_FALSE(view->switch_dialog->can_confirm);
    }

    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"tags", u8"trunk" });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_rows.size() == 4u);
        REQUIRE(view->switch_dialog->svn_rows.back().current);
        REQUIRE(view->switch_dialog->svn_rows.back().selected);
        REQUIRE_FALSE(view->switch_dialog->can_confirm);
        REQUIRE(view->switch_dialog->confirm_label == u8"전환");
    }

    constexpr std::u8string_view branch { u8"https://svn.example.com/repo/branches" };
    fixture.controller.handle(gitman::select_svn_browser_node_intent { std::u8string { branch } });
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->can_confirm);

    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::switch_to);
    REQUIRE(fixture.submitter.requests.front().switch_target.has_value());
    REQUIRE(fixture.submitter.requests.front().switch_target->target == branch);
}

TEST_CASE("SVN switch dialog keeps authentication failure on the queried node", "[logic][switch-ui][svn]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser() };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", {}, gitman::svn_browser_query_error::authentication_required);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->svn_rows.size() == 2u);
    REQUIRE(view->switch_dialog->svn_rows[1].kind == gitman::svn_browser_row_kind::error);
    REQUIRE(view->switch_dialog->svn_rows[1].text == u8"인증이 필요해 조회하지 못했습니다");
}

TEST_CASE("Closing the SVN browser discards its cache and late directory result", "[logic][switch-ui][svn]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t old_root_operation { fixture.deliver_svn_browser() };
    fixture.controller.handle(gitman::cancel_switch_dialog_intent {});
    REQUIRE_FALSE(fixture.controller.make_view_snapshot()->switch_dialog.has_value());

    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::begin_switch_intent { gitman::project_id { u8"alpha" } });
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::query_switch_candidates);

    // 이전 dialog의 node 응답은 새 dialog의 초기 loading 상태를 바꾸지 않는다.
    fixture.deliver_svn_directory(old_root_operation, u8"https://svn.example.com/repo", { u8"ghost" });
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog.has_value());
    REQUIRE(view->switch_dialog->loading);
    REQUIRE(view->switch_dialog->svn_rows.empty());
}

TEST_CASE("The switch dialog tree wires rows and buttons and routes escape and the wheel", "[ui][switch-ui]")
{
    gitman::switch_dialog_view dialog { make_dialog() };
    dialog.candidates.push_back(make_candidate(u8"origin/feature", gitman::switch_candidate_kind::git_remote_branch, true));
    dialog.candidates.push_back(make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch));
    dialog.selected = { 1u };
    dialog.can_confirm = true;

    gitman::view_snapshot view { make_dialog_view(std::move(dialog)) };
    view.document_path = u8"C:\\work\\p.version-list";
    gitman::card_view_model card {};
    card.id.value = u8"alpha";
    card.display_name = u8"alpha";
    view.cards.push_back(std::move(card));

    const auto tree { make_tree(view) };
    REQUIRE(find(*tree, gitman::ui::kind_switch_dialog) != nullptr);

    // 후보 행의 owner는 index를 담은 문자열이다 (element와 test가 같은 규칙을 쓴다).
    REQUIRE(gitman::ui::switch_dialog_item_id(0).owner == u8"switch-item-0");

    {
        const auto intent_actions { click_action(*tree, gitman::ui::switch_dialog_item_id(0)) };
        const auto* const intent { as_intent<gitman::select_switch_candidate_intent>(intent_actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->index == 0u);
    }
    REQUIRE(as_intent<gitman::confirm_switch_intent>(click_action(*tree, id_of(gitman::ui::kind_switch_dialog_confirm, u8"alpha"))) != nullptr);
    REQUIRE(as_intent<gitman::cancel_switch_dialog_intent>(click_action(*tree, id_of(gitman::ui::kind_switch_dialog_cancel, u8"alpha"))) != nullptr);

    // 본체는 luil modal host의 content다 (설계 D2-A). scrim·가운데 배치·초점 가둠·
    // Esc는 host가 맡는다.
    REQUIRE(find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_switch) != nullptr);
    REQUIRE(tree->focus_trap() != nullptr);
    // 배경(scrim) 클릭도 취소다. 그 액션은 이제 host의 scrim에 걸린다.
    REQUIRE(as_intent<gitman::cancel_switch_dialog_intent>(click_action(*tree, id_of(luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_switch))) != nullptr);

    // D1-B: 본체·후보 행·scrim은 Tab 자리가 아니고 버튼만 자리다.
    REQUIRE(find(*tree, gitman::ui::kind_switch_dialog)->tab_stop() == false);
    REQUIRE(tree->find(gitman::ui::switch_dialog_item_id(0))->tab_stop() == false);
    REQUIRE(find(*tree, luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_switch)->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_switch_dialog_confirm, u8"alpha")->tab_stop());

    // Esc는 가둠의 dismiss 액션으로 나간다 (luil `process_dismiss_key`).
    interaction_harness harness { tree };
    REQUIRE(as_intent<gitman::cancel_switch_dialog_intent>(harness.process(luil::key_pressed_event { luil::key_code::escape })) != nullptr);
    REQUIRE(as_intent<gitman::switch_dialog_scroll_intent>(harness.process(luil::mouse_wheel_event { 400.0f, 300.0f, -120.0f })) != nullptr);
}

TEST_CASE("The switch dialog shows a scrollbar only when the candidates overflow", "[ui][switch-ui]")
{
    gitman::switch_dialog_view dialog { make_dialog() };
    dialog.candidates.push_back(make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch));

    // 목록이 영역에 다 들어가면 막대가 보이지 않는다. 막대는 kind 하나에 owner로
    // 갈린다 (계약 2장).
    {
        const auto tree { make_tree(make_dialog_view(dialog)) };
        const luil::ui_element* const bar { find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_switch_dialog) };
        REQUIRE(bar != nullptr);
        REQUIRE(bar->visible() == false);
    }

    // 넘치면 막대가 보이고, thumb 밖 track을 누르면 그 자리로 이동하는 스크롤
    // intent가 나간다 (logic의 clamp 경로와 같은 switch_dialog_scroll_intent).
    for (std::size_t index = 0; index < 30; ++index)
    {
        const std::string digits { std::to_string(index) };
        std::u8string name { u8"branch-" };
        name.append(digits.begin(), digits.end());
        dialog.candidates.push_back(make_candidate(name, gitman::switch_candidate_kind::git_local_branch));
    }
    const auto tree { make_tree(make_dialog_view(std::move(dialog))) };
    const luil::ui_element* const bar { find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_switch_dialog) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->visible());

    interaction_harness harness { tree };
    const luil::rect_f track { bar->bounds() };
    const auto actions { harness.process(luil::pointer_pressed_event { track.x + track.width - 1.0f, track.y + track.height - 2.0f, luil::pointer_button::left, at(0) }) };
    const auto* const intent { as_intent<gitman::switch_dialog_scroll_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->delta > 0.0f);
}

TEST_CASE("The SVN switch tree separates row selection from expansion", "[ui][switch-ui][svn]")
{
    gitman::switch_dialog_view dialog { make_dialog(u8"전환") };
    dialog.svn_browser = true;
    dialog.svn_rows.push_back({ gitman::svn_browser_row_kind::directory, u8"https://svn.example.com/repo", u8"https://svn.example.com/repo", 0, true, true, false, false });
    dialog.svn_rows.push_back({ gitman::svn_browser_row_kind::loading, u8"https://svn.example.com/repo", u8"조회 중…", 1 });

    const auto tree { make_tree(make_dialog_view(std::move(dialog))) };
    // SVN 행과 펼침 손잡이는 Tab 자리가 아니다 (D1-B).
    REQUIRE(tree->find(gitman::ui::switch_dialog_svn_item_id(u8"https://svn.example.com/repo"))->tab_stop() == false);
    REQUIRE(tree->find(gitman::ui::switch_dialog_svn_expand_id(u8"https://svn.example.com/repo"))->tab_stop() == false);
    {
        const auto intent_actions { click_action(*tree, gitman::ui::switch_dialog_svn_item_id(u8"https://svn.example.com/repo")) };
        const auto* const intent { as_intent<gitman::select_svn_browser_node_intent>(intent_actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->url == u8"https://svn.example.com/repo");
    }
    {
        const auto intent_actions { click_action(*tree, gitman::ui::switch_dialog_svn_expand_id(u8"https://svn.example.com/repo")) };
        const auto* const intent { as_intent<gitman::toggle_svn_browser_node_intent>(intent_actions) };
        REQUIRE(intent != nullptr);
        REQUIRE(intent->url == u8"https://svn.example.com/repo");
    }
}

#define REQUIRE_GIT_AVAILABLE(fixture)                                                                                                                                                                 \
    if ((fixture).available() == false)                                                                                                                                                                \
    SKIP("호스트에 사용할 수 있는 Git이 없어 통합 test를 건너뜁니다")

TEST_CASE("An executor switch creates the confirmed tracking branch on a real repository", "[executor][switch-ui][integration]")
{
    gitman::testing::git_repository_fixture fixture {};
    REQUIRE_GIT_AVAILABLE(fixture);

    const std::u8string remote { fixture.make_bare_repository(u8"remote") };
    const std::u8string writer { fixture.make_repository(u8"writer") };
    fixture.write_file(writer, u8"a.txt", "first\n");
    fixture.git(writer, { u8"add", u8"." });
    fixture.git(writer, { u8"commit", u8"-m", u8"first" });
    fixture.git(writer, { u8"remote", u8"add", u8"origin", remote });
    fixture.git(writer, { u8"push", u8"-u", u8"origin", u8"main" });
    fixture.git(writer, { u8"switch", u8"-c", u8"feature" });
    fixture.git(writer, { u8"commit", u8"--allow-empty", u8"-m", u8"feature work" });
    fixture.git(writer, { u8"push", u8"-u", u8"origin", u8"feature" });
    fixture.git(writer, { u8"switch", u8"main" });
    fixture.git(fixture.path_of(u8""), { u8"clone", remote, u8"reader" });
    const std::u8string reader { fixture.path_of(u8"reader") };
    REQUIRE(fixture.failures().empty());

    null_project_store store {};
    gitman::vcs_operation_executor executor { store, fixture.runner(), fixture.probe(), {} };
    std::vector<gitman::logic_message> emitted {};
    const auto emit { [&emitted](gitman::logic_message message) { emitted.push_back(std::move(message)); } };

    gitman::operation_request request {};
    request.operation_id = 60;
    request.generation = 1;
    request.kind = gitman::operation_kind::query_switch_candidates;
    request.project.id.value = u8"reader";
    request.project.path.original = reader;
    request.project.path.normalized = reader;
    request.project.hint = gitman::vcs_hint::git;
    request.settings.git_executable = fixture.tool().executable;
    executor.execute(request, emit);

    REQUIRE(emitted.size() == 1u);
    const auto* const candidates { std::get_if<gitman::switch_candidates_event>(&emitted.front()) };
    REQUIRE(candidates != nullptr);
    const gitman::switch_candidate* feature { nullptr };
    for (const gitman::switch_candidate& candidate : candidates->result.candidates)
        if (candidate.display_name == u8"origin/feature")
            feature = &candidate;
    REQUIRE(feature != nullptr);
    REQUIRE(feature->requires_tracking_branch);

    // dialog의 두 단계 확인을 거친 것과 같은 상태로 실행한다.
    gitman::switch_candidate confirmed { *feature };
    confirmed.tracking_branch_confirmed = true;
    emitted.clear();
    request.operation_id = 61;
    request.kind = gitman::operation_kind::switch_to;
    request.switch_target = { std::move(confirmed) };
    executor.execute(request, emit);

    REQUIRE(emitted.empty() == false);
    const auto* const final_event { std::get_if<gitman::change_completed_event>(&emitted.back()) };
    REQUIRE(final_event != nullptr);
    REQUIRE(final_event->result.executed);
    REQUIRE(final_event->result.succeeded);
    REQUIRE(final_event->result.snapshot.current_reference == u8"feature");
}

TEST_CASE("The Git query bar filters candidates while it is typed", "[logic][switch-ui][query]")
{
    dialog_fixture fixture {};
    std::vector<gitman::switch_candidate> candidates {};
    candidates.push_back(make_candidate(u8"origin/feature-a", gitman::switch_candidate_kind::git_remote_branch));
    candidates.push_back(make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch));
    candidates.push_back(make_candidate(u8"feature-b", gitman::switch_candidate_kind::git_local_branch));
    fixture.deliver_candidates(std::move(candidates));

    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->query.text.empty());
        REQUIRE(view->switch_dialog->query_enabled);
        REQUIRE(view->switch_dialog->query_action == gitman::switch_query_action::search);
        REQUIRE_FALSE(view->switch_dialog->query_action_enabled);
        REQUIRE(view->switch_dialog->candidates.size() == 3u);
    }

    // 대소문자를 가리지 않는다 (카드 목록 필터와 같은 규칙).
    type_query(fixture.controller, u8"FEATURE");
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->query.text == u8"FEATURE");
        REQUIRE(view->switch_dialog->query_action == gitman::switch_query_action::clear);
        REQUIRE(view->switch_dialog->query_action_enabled);
        REQUIRE(view->switch_dialog->candidates.size() == 2u);
        REQUIRE(view->switch_dialog->candidates[0].display_name == u8"origin/feature-a");
        REQUIRE(view->switch_dialog->candidates[1].display_name == u8"feature-b");
    }

    // 선택 index는 거른 목록 기준이다. 1번은 feature-b다.
    fixture.controller.handle(gitman::select_switch_candidate_intent { 1 });
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().switch_target->display_name == u8"feature-b");
}

TEST_CASE("Editing the Git query drops the selection and empty results say so", "[logic][switch-ui][query]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) });
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->can_confirm);

    type_query(fixture.controller, u8"zzz");
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE_FALSE(view->switch_dialog->selected.has_value());
        REQUIRE_FALSE(view->switch_dialog->can_confirm);
        REQUIRE(view->switch_dialog->candidates.empty());
        REQUIRE(view->switch_dialog->list_note == u8"필터에 맞는 후보가 없습니다.");
    }

    // backspace 한 번은 한 글자다.
    fixture.controller.handle(query_command(gitman::text_edit_command::backspace));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->query.text == u8"zz");

    fixture.controller.handle(gitman::clear_switch_query_intent {});
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->query.text.empty());
        REQUIRE(view->switch_dialog->candidates.size() == 1u);
        REQUIRE(view->switch_dialog->list_note.empty());
    }
}

TEST_CASE("Enter picks the first filtered Git candidate", "[logic][switch-ui][query]")
{
    dialog_fixture fixture {};
    std::vector<gitman::switch_candidate> candidates {};
    candidates.push_back(make_candidate(u8"main", gitman::switch_candidate_kind::git_local_branch));
    candidates.push_back(make_candidate(u8"release/1", gitman::switch_candidate_kind::git_local_branch));
    candidates.push_back(make_candidate(u8"release/2", gitman::switch_candidate_kind::git_local_branch));
    fixture.deliver_candidates(std::move(candidates));

    type_query(fixture.controller, u8"release");
    fixture.controller.handle(gitman::submit_switch_query_intent {});
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->selected == 0u);
    REQUIRE(view->switch_dialog->can_confirm);
    REQUIRE(view->switch_dialog->candidates[0].display_name == u8"release/1");
}

TEST_CASE("Refreshing the Git switch dialog queries again and keeps the filter", "[logic][switch-ui][query]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch) });
    type_query(fixture.controller, u8"dev");
    fixture.submitter.requests.clear();

    fixture.controller.handle(gitman::refresh_switch_dialog_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().kind == gitman::operation_kind::query_switch_candidates);
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->loading);
        REQUIRE(view->switch_dialog->query.text == u8"dev");
        // 조회 중에는 막대 전체가 비활성이다.
        REQUIRE_FALSE(view->switch_dialog->query_enabled);
    }

    // 새 조회의 결과만 반영된다. 필터는 그대로 걸린다.
    fixture.candidates_operation_id = fixture.submitter.requests.front().operation_id;
    fixture.deliver_candidates({ make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch), make_candidate(u8"main", gitman::switch_candidate_kind::git_local_branch) });
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->loading == false);
    REQUIRE(view->switch_dialog->candidates.size() == 1u);
    REQUIRE(view->switch_dialog->candidates.front().display_name == u8"develop");
}

TEST_CASE("The SVN switch dialog opens revealed down to the current location", "[logic][switch-ui][svn][query]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser(u8"https://svn.example.com/repo", u8"https://svn.example.com/repo/trunk/source") };
    fixture.submitter.requests.clear();
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"trunk" });

    // 현재 위치로 가는 길만 한 단계씩 조회한다. branches는 건드리지 않는다.
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().svn_directory_url == u8"https://svn.example.com/repo/trunk");
    const std::uint64_t trunk_operation { fixture.submitter.requests.front().operation_id };

    fixture.submitter.requests.clear();
    fixture.deliver_svn_directory(trunk_operation, u8"https://svn.example.com/repo/trunk", { u8"docs", u8"source" });
    // 현재 위치에 닿으면 멈춘다. 그 아래는 사용자가 열 때만 조회한다.
    REQUIRE(fixture.submitter.requests.empty());

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->query.text == u8"/trunk/source");
    REQUIRE(view->switch_dialog->query_action == gitman::switch_query_action::navigate);
    REQUIRE(view->switch_dialog->query_action_enabled);
    REQUIRE(view->switch_dialog->svn_rows.size() == 5u);
    REQUIRE(view->switch_dialog->svn_rows.back().text == u8"source");
    REQUIRE(view->switch_dialog->svn_rows.back().current);
    REQUIRE(view->switch_dialog->svn_rows.back().selected);
    REQUIRE_FALSE(view->switch_dialog->svn_rows.back().expanded);
}

TEST_CASE("The SVN browser follows each depth as its query lands", "[logic][switch-ui][svn][query]")
{
    // 조회 하나가 초 단위로 걸린다. 목표에 닿을 때까지 아무 데도 붙지 않으면 한참
    // 동안 어디까지 갔는지 알 수 없다 (2026-08-23 지시).
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser(u8"https://svn.example.com/repo", u8"https://svn.example.com/repo") };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"a", u8"z" });

    fixture.controller.handle(query_command(gitman::text_edit_command::select_all));
    type_query(fixture.controller, u8"/a/b/c");
    fixture.controller.handle(gitman::submit_switch_query_intent {});

    // 첫 걸음에서 이미 a에 붙는다 — root는 cache에 있고 a가 다음 조회다.
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_rows[1].text == u8"a");
        REQUIRE(view->switch_dialog->svn_rows[1].selected);
        // 걸어 내려가는 동안의 선택은 지나가는 자리라 `전환`을 받지 않는다.
        REQUIRE_FALSE(view->switch_dialog->can_confirm);
        // 주소 막대는 사용자가 친 목표를 그대로 들고 있는다.
        REQUIRE(view->switch_dialog->query.text == u8"/a/b/c");
    }

    REQUIRE(fixture.submitter.requests.back().svn_directory_url == u8"https://svn.example.com/repo/a");
    fixture.deliver_svn_directory(fixture.submitter.requests.back().operation_id, u8"https://svn.example.com/repo/a", { u8"b" });
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_rows[2].text == u8"b");
        REQUIRE(view->switch_dialog->svn_rows[2].selected);
        REQUIRE_FALSE(view->switch_dialog->can_confirm);
        REQUIRE(view->switch_dialog->query.text == u8"/a/b/c");
    }

    REQUIRE(fixture.submitter.requests.back().svn_directory_url == u8"https://svn.example.com/repo/a/b");
    fixture.deliver_svn_directory(fixture.submitter.requests.back().operation_id, u8"https://svn.example.com/repo/a/b", { u8"c" });
    {
        // 목표에 닿으면 거기서 멈추고 `전환`이 열린다.
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_rows[3].text == u8"c");
        REQUIRE(view->switch_dialog->svn_rows[3].selected);
        REQUIRE(view->switch_dialog->query.text == u8"/a/b/c");
        REQUIRE(view->switch_dialog->can_confirm);
    }
}

TEST_CASE("The SVN browser scrolls the revealed location into view", "[logic][switch-ui][svn][query]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    // 목록이 두 행 남짓만 보이는 낮은 창이다. 도달한 행이 화면 아래에 숨는다.
    fixture.controller.handle(gitman::window_metrics_intent { 800.0f, 260.0f, 1.0f });
    const float viewport { gitman::switch_dialog_list_height(true, 260.0f) };
    REQUIRE(viewport < 5.0f * gitman::layout_switch_dialog_row_height());

    const std::uint64_t root_operation { fixture.deliver_svn_browser(u8"https://svn.example.com/repo", u8"https://svn.example.com/repo/trunk/source") };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"trunk" });
    REQUIRE(fixture.submitter.requests.back().svn_directory_url == u8"https://svn.example.com/repo/trunk");
    fixture.deliver_svn_directory(fixture.submitter.requests.back().operation_id, u8"https://svn.example.com/repo/trunk", { u8"docs", u8"source" });

    {
        // repo·branches·trunk·docs·source 다섯 행 중 마지막이 현재 위치다. 가운데에
        // 놓으려 해도 목록 끝이라 한계에 걸려 아래 끝에 선다 (2026-08-23 지시).
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_rows.size() == 5u);
        REQUIRE(view->switch_dialog->svn_rows.back().selected);
        REQUIRE(view->switch_dialog->scroll_offset == 5.0f * gitman::layout_switch_dialog_row_height() - viewport);
    }

    // 가운데 가지로 이동하면 그 행이 목록 한가운데에 온다.
    fixture.controller.handle(query_command(gitman::text_edit_command::select_all));
    type_query(fixture.controller, u8"/trunk/docs");
    fixture.controller.handle(gitman::submit_switch_query_intent {});
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_rows[3].text == u8"docs");
        REQUIRE(view->switch_dialog->svn_rows[3].selected);
        const float centered { 3.0f * gitman::layout_switch_dialog_row_height() - (viewport - gitman::layout_switch_dialog_row_height()) / 2.0f };
        REQUIRE(view->switch_dialog->scroll_offset == centered);
    }

    // 맨 위 행은 가운데로 갈 수 없다. 한계가 0으로 붙인다.
    fixture.controller.handle(query_command(gitman::text_edit_command::select_all));
    type_query(fixture.controller, u8"/");
    fixture.controller.handle(gitman::submit_switch_query_intent {});
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->svn_rows.front().selected);
        REQUIRE(view->switch_dialog->scroll_offset == 0.0f);
    }
}

TEST_CASE("The SVN address bar walks down to the typed path one query at a time", "[logic][switch-ui][svn][query]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser() };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"trunk" });
    fixture.submitter.requests.clear();

    // 현재 위치(트리에 이미 보이는 trunk)에서 다른 가지로 옮긴다.
    fixture.controller.handle(gitman::select_svn_browser_node_intent { std::u8string { u8"https://svn.example.com/repo" } });
    type_query(fixture.controller, u8"branches/2026-08");
    fixture.controller.handle(gitman::submit_switch_query_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().svn_directory_url == u8"https://svn.example.com/repo/branches");
    const std::uint64_t branches_operation { fixture.submitter.requests.front().operation_id };

    fixture.submitter.requests.clear();
    fixture.deliver_svn_directory(branches_operation, u8"https://svn.example.com/repo/branches", { u8"2026-07", u8"2026-08" });
    // 목표에 닿으면 멈춘다. 그 아래는 조회하지 않는다.
    REQUIRE(fixture.submitter.requests.empty());

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->query.text == u8"/branches/2026-08");
    REQUIRE(view->switch_dialog->svn_rows.size() == 5u);
    REQUIRE(view->switch_dialog->svn_rows[3].text == u8"2026-08");
    REQUIRE(view->switch_dialog->svn_rows[3].selected);
    REQUIRE(view->switch_dialog->can_confirm);
}

TEST_CASE("The SVN address bar reports a path the repository does not have", "[logic][switch-ui][svn][query]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser() };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"trunk" });

    // 노드를 고르면 주소 막대가 그 실제 경로로 덮인다.
    fixture.controller.handle(gitman::select_svn_browser_node_intent { std::u8string { u8"https://svn.example.com/repo" } });
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->query.text == u8"/");

    type_query(fixture.controller, u8"nope");
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::submit_switch_query_intent {});
    REQUIRE(fixture.submitter.requests.empty());
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->message == u8"그 경로를 찾을 수 없습니다: /nope");
}

TEST_CASE("Selecting an SVN node rewrites the address bar", "[logic][switch-ui][svn][query]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser() };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"trunk" });

    fixture.controller.handle(gitman::select_svn_browser_node_intent { std::u8string { u8"https://svn.example.com/repo/branches" } });
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->query.text == u8"/branches");
}

TEST_CASE("Refreshing the SVN dialog requeries the selected directory", "[logic][switch-ui][svn][query]")
{
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser() };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"trunk" });
    fixture.controller.handle(gitman::select_svn_browser_node_intent { std::u8string { u8"https://svn.example.com/repo/trunk" } });
    fixture.submitter.requests.clear();

    fixture.controller.handle(gitman::refresh_switch_dialog_intent {});
    REQUIRE(fixture.submitter.requests.size() == 1u);
    REQUIRE(fixture.submitter.requests.front().svn_directory_url == u8"https://svn.example.com/repo/trunk");
    const std::uint64_t trunk_operation { fixture.submitter.requests.front().operation_id };

    fixture.deliver_svn_directory(trunk_operation, u8"https://svn.example.com/repo/trunk", { u8"source" });
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->svn_rows.size() == 4u);
    REQUIRE(view->switch_dialog->svn_rows.back().text == u8"source");
}

TEST_CASE("The switch query applies edits at the caret and the timeout box keeps digits only", "[logic][switch-ui][query][text-edit]")
{
    dialog_fixture fixture {};
    fixture.deliver_candidates({ make_candidate(u8"origin/main", gitman::switch_candidate_kind::git_remote_branch) });
    type_query(fixture.controller, u8"main");

    // caret을 앞으로 옮겨 넣으면 그 자리에 들어간다.
    fixture.controller.handle(query_command(gitman::text_edit_command::move_line_start));
    type_query(fixture.controller, u8"or");
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->query.text == u8"ormain");
        REQUIRE(view->switch_dialog->query.caret == 2u);
    }

    // 전체 선택 뒤 입력은 대체다.
    fixture.controller.handle(query_command(gitman::text_edit_command::select_all));
    type_query(fixture.controller, u8"o");
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->query.text == u8"o");
        REQUIRE(view->switch_dialog->candidates.size() == 1u);
    }

    // 잘라내기는 선택만 지운다.
    fixture.controller.handle(query_command(gitman::text_edit_command::select_all));
    fixture.controller.handle(query_command(gitman::text_edit_command::cut));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->query.text.empty());
}

TEST_CASE("The switch dialog tree wires the query bar", "[ui][switch-ui][query]")
{
    gitman::switch_dialog_view dialog { make_dialog() };
    dialog.query.text = u8"feat";
    dialog.query_enabled = true;
    dialog.query_action = gitman::switch_query_action::clear;
    dialog.query_action_enabled = true;
    dialog.candidates.push_back(make_candidate(u8"feature", gitman::switch_candidate_kind::git_local_branch));

    const luil::ui_element_id action_id { gitman::ui::kind_switch_dialog_query_action, std::u8string { u8"alpha" } };
    const luil::ui_element_id refresh_id { gitman::ui::kind_switch_dialog_query_refresh, std::u8string { u8"alpha" } };

    {
        const auto tree { make_tree(make_dialog_view(dialog)) };
        REQUIRE(find(*tree, gitman::ui::kind_switch_dialog_query_input, u8"alpha") != nullptr);
        REQUIRE(tree->find(query_input_id)->enabled());
        REQUIRE(tree->find(action_id)->enabled());
        REQUIRE(as_intent<gitman::clear_switch_query_intent>(click_action(*tree, action_id)) != nullptr);
        REQUIRE(as_intent<gitman::refresh_switch_dialog_intent>(click_action(*tree, refresh_id)) != nullptr);

        // 질의 박스를 누르면 초점을 받고, 그 뒤의 문자와 Enter가 그리로 간다.
        interaction_harness harness { tree };
        focus_query(harness, bounds_of(*tree, gitman::ui::kind_switch_dialog_query_input, u8"alpha"));

        const gitman::edit_text_input_intent typed { edit_of(harness.process(luil::character_typed_event { U'x' })) };
        REQUIRE(typed.target == gitman::text_input_target::switch_query);
        REQUIRE(typed.command == gitman::text_edit_command::insert);
        REQUIRE(typed.text == u8"x");

        // 확인 버튼에는 `default_button`을 걸지 않는다 (C3-2). 질의 칸에 초점이
        // 있는 동안의 Enter는 첫 후보 선택·경로 이동이라 기본 버튼이 가로채면
        // 안 된다. tree 전체에 기본 버튼이 없어야 규칙이 잠긴다.
        REQUIRE(tree->default_button() == nullptr);
        REQUIRE(as_intent<gitman::submit_switch_query_intent>(harness.process(luil::key_pressed_event { luil::key_code::enter })) != nullptr);

        // dialog가 닫히면 초점도 사라진다.
        gitman::view_snapshot closed {};
        closed.window_width = 800.0f;
        closed.window_height = 600.0f;
        closed.scale = 1.0f;
        harness.controller.set_tree(make_tree(closed));
        REQUIRE(harness.snapshot().focused_input == luil::ui_element_id {});
    }

    // SVN은 이동 버튼이고, 조회 중에는 막대 전체가 비활성이다.
    gitman::switch_dialog_view svn { std::move(dialog) };
    svn.svn_browser = true;
    svn.loading = true;
    svn.query_enabled = false;
    svn.query_action = gitman::switch_query_action::navigate;
    const auto tree { make_tree(make_dialog_view(std::move(svn))) };
    REQUIRE_FALSE(tree->find(query_input_id)->enabled());
    REQUIRE_FALSE(tree->find(action_id)->enabled());
    REQUIRE_FALSE(tree->find(refresh_id)->enabled());
    REQUIRE(as_intent<gitman::submit_switch_query_intent>(click_action(*tree, action_id)) != nullptr);
}

TEST_CASE("The query bar routes the editing keys and the clipboard to the switch query", "[ui][switch-ui][query][text-edit]")
{
    // 낱낱의 키 조합 전수는 luil test의 몫이다. 여기서는 gitman 의미 — 초점을 가진
    // 질의 칸의 키가 `switch_query` 대상의 편집 intent·클립보드 요청이 되는가 —
    // 만 대표 사례로 확인한다 (C2-4 test 계약 3장).
    gitman::switch_dialog_view dialog { make_dialog() };
    dialog.query.text = u8"origin/main";
    dialog.query.anchor = 7u;
    dialog.query.caret = 11u;
    dialog.query_enabled = true;

    const auto tree { make_tree(make_dialog_view(std::move(dialog))) };
    interaction_harness harness { tree };
    focus_query(harness, bounds_of(*tree, gitman::ui::kind_switch_dialog_query_input, u8"alpha"));

    // 이동·선택 키다. Shift는 범위를 늘린다.
    {
        const gitman::edit_text_input_intent intent { edit_of(harness.process(luil::key_pressed_event { luil::key_code::key_a, true })) };
        REQUIRE(intent.target == gitman::text_input_target::switch_query);
        REQUIRE(intent.command == gitman::text_edit_command::select_all);
    }
    REQUIRE(edit_of(harness.process(luil::key_pressed_event { luil::key_code::home })).command == gitman::text_edit_command::move_line_start);
    REQUIRE(edit_of(harness.process(luil::key_pressed_event { luil::key_code::end })).command == gitman::text_edit_command::move_line_end);
    {
        const gitman::edit_text_input_intent intent { edit_of(harness.process(luil::key_pressed_event { luil::key_code::arrow_left, false, true })) };
        REQUIRE(intent.command == gitman::text_edit_command::move_left);
        REQUIRE(intent.extend);
    }
    REQUIRE(edit_of(harness.process(luil::key_pressed_event { luil::key_code::key_z, true })).command == gitman::text_edit_command::undo);

    // 복사는 UI thread가 클립보드에 넣도록 고른 글을 담아 넘긴다. 이 view의 선택은
    // `main`이다.
    {
        const std::vector<luil::input_action> actions { harness.process(luil::key_pressed_event { luil::key_code::key_c, true }) };
        REQUIRE(actions.size() == 1u);
        const auto* const copy { std::get_if<luil::clipboard_copy_request>(&actions.front()) };
        REQUIRE(copy != nullptr);
        REQUIRE(copy->text == u8"main");
    }

    // 붙여넣기는 UI thread가 클립보드를 읽도록 대상만 담아 넘긴다.
    {
        const std::vector<luil::input_action> actions { harness.process(luil::key_pressed_event { luil::key_code::key_v, true }) };
        REQUIRE(actions.size() == 1u);
        const auto* const paste { std::get_if<luil::clipboard_paste_request>(&actions.front()) };
        REQUIRE(paste != nullptr);
        REQUIRE(paste->target == gitman::ui::to_luil_target(gitman::text_input_target::switch_query));
    }
}

TEST_CASE("Editing keys pass through when no text box has focus", "[ui][switch-ui][text-edit]")
{
    gitman::view_snapshot view {};
    view.window_width = 800.0f;
    view.window_height = 600.0f;
    view.scale = 1.0f;
    gitman::card_view_model card {};
    card.id.value = u8"alpha";
    card.display_name = u8"alpha";
    view.cards.push_back(std::move(card));

    interaction_harness harness { make_tree(view) };

    // 초점을 가진 박스가 없으면 편집 키는 아무 일도 하지 않는다.
    REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::arrow_left }).empty());
    REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::key_c, true }).empty());
    REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::delete_forward }).empty());

    // ↓는 여전히 카드 목록 탐색이다.
    REQUIRE(as_intent<gitman::select_card_intent>(harness.process(luil::key_pressed_event { luil::key_code::arrow_down })) != nullptr);
}

TEST_CASE("An IME composition shows in the box without touching the draft", "[logic][switch-ui][ime]")
{
    // TSF가 조합 중인 글을 보내면 박스에는 보이지만 초안(text_edit_state)에는
    // 들어가지 않는다 (text-input-and-font-fallback-design E2.3). 조합 단계마다
    // 실행 취소 기록이 더럽혀지면 쓸 수 없다.
    dialog_fixture fixture {};

    gitman::set_text_composition_intent composing {};
    composing.target = gitman::text_input_target::switch_query;
    composing.text = u8"ㅎ";
    composing.caret = 3u;
    composing.composing_begin = 0u;
    composing.composing_end = 3u;
    composing.composing = true;
    fixture.controller.handle(composing);

    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog.has_value());
    const gitman::text_input_view& query { view->switch_dialog->query };
    REQUIRE(query.composing);
    REQUIRE(query.composition_text == u8"ㅎ");
    REQUIRE(query.composing_begin == 0u);
    REQUIRE(query.composing_end == 3u);
    // 확정된 글은 아직 비어 있다.
    REQUIRE(query.text.empty());
}

TEST_CASE("Ending a composition clears the overlay and commits as one undo step", "[logic][switch-ui][ime]")
{
    dialog_fixture fixture {};

    gitman::set_text_composition_intent composing {};
    composing.target = gitman::text_input_target::switch_query;
    composing.text = u8"한";
    composing.caret = 3u;
    composing.composing_begin = 0u;
    composing.composing_end = 3u;
    composing.composing = true;
    fixture.controller.handle(composing);

    // 확정된다: TSF가 조합을 끝내고 완성된 글을 초안으로 보낸다.
    gitman::edit_text_input_intent commit {};
    commit.target = gitman::text_input_target::switch_query;
    commit.command = gitman::text_edit_command::replace_all;
    commit.text = u8"한";
    commit.offset = 3u;
    fixture.controller.handle(commit);

    gitman::set_text_composition_intent done {};
    done.target = gitman::text_input_target::switch_query;
    done.composing = false;
    fixture.controller.handle(done);

    const auto view { fixture.controller.make_view_snapshot() };
    const gitman::text_input_view& query { view->switch_dialog->query };
    REQUIRE(query.composing == false);
    REQUIRE(query.text == u8"한");
    REQUIRE(query.caret == 3u);

    // 조합 한 번이 실행 취소 한 덩어리다.
    gitman::edit_text_input_intent undo {};
    undo.target = gitman::text_input_target::switch_query;
    undo.command = gitman::text_edit_command::undo;
    fixture.controller.handle(undo);
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->query.text.empty());
}

TEST_CASE("A composing query narrows the Git list and falls back to the committed text", "[logic][switch-ui][ime][query]")
{
    // 점진 검색의 두 줄 규칙이다 (luil `search_query`와 같다). 조합 글이 섞인 표시
    // 글로 먼저 거르고, 그 글로 하나도 맞지 않으면 확정 글로 물러선다 — 단 확정
    // 글이 비어 있으면 물러서지 않는다 (빈 질의는 "전부"라서 목록이 되살아난다).
    dialog_fixture fixture {};
    std::vector<gitman::switch_candidate> candidates {};
    candidates.push_back(make_candidate(u8"feature/login", gitman::switch_candidate_kind::git_local_branch));
    candidates.push_back(make_candidate(u8"main", gitman::switch_candidate_kind::git_local_branch));
    candidates.push_back(make_candidate(u8"release", gitman::switch_candidate_kind::git_local_branch));
    fixture.deliver_candidates(std::move(candidates));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->candidates.size() == 3u);

    // 확정 글이 없어도 조합 글만으로 목록이 좁아진다.
    fixture.controller.handle(compose_query(u8"ma"));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->query.text.empty());
        REQUIRE(view->switch_dialog->candidates.size() == 1u);
        REQUIRE(view->switch_dialog->candidates[0].display_name == u8"main");
    }

    // Enter는 보이는 첫 후보를 잡는다 — 조합 중에도 같은 목록을 본다.
    fixture.controller.handle(gitman::submit_switch_query_intent {});
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->selected.has_value());
        REQUIRE(*view->switch_dialog->selected == 0u);
        REQUIRE(view->switch_dialog->candidates[*view->switch_dialog->selected].display_name == u8"main");
    }

    // 맞는 것이 없어도 확정 글이 비어 있으면 물러서지 않는다.
    fixture.controller.handle(compose_query(u8"mx"));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->candidates.empty());

    // 조합이 끝나면 확정 글(아직 빈 글) 기준으로 돌아온다.
    fixture.controller.handle(end_query_composition());
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->candidates.size() == 3u);

    // 확정 글이 있으면 맞지 않는 조합 글에서 그리로 물러선다.
    type_query(fixture.controller, u8"ma");
    fixture.controller.handle(compose_query(u8"max", 2u));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->query.text == u8"ma");
        REQUIRE(view->switch_dialog->candidates.size() == 1u);
        REQUIRE(view->switch_dialog->candidates[0].display_name == u8"main");
    }

    fixture.controller.handle(end_query_composition());
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->candidates.size() == 1u);
        REQUIRE(view->switch_dialog->candidates[0].display_name == u8"main");
    }
}

TEST_CASE("A composition clears the selection and the tracking confirmation", "[logic][switch-ui][ime][query]")
{
    // 조합 한 단계마다 목록이 달라질 수 있어(D5) 편집과 같은 규칙으로 사라질 수 있는
    // 선택·확인 단계를 지운다 — 확인한 적 없는 후보로 tracking branch를 만들지 않는다.
    dialog_fixture fixture {};
    std::vector<gitman::switch_candidate> candidates {};
    candidates.push_back(make_candidate(u8"origin/feature", gitman::switch_candidate_kind::git_remote_branch, true));
    candidates.push_back(make_candidate(u8"main", gitman::switch_candidate_kind::git_local_branch));
    fixture.deliver_candidates(std::move(candidates));
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->confirm_label == u8"생성 확인");

    fixture.controller.handle(compose_query(u8"m"));
    {
        const auto view { fixture.controller.make_view_snapshot() };
        REQUIRE(view->switch_dialog->selected.has_value() == false);
        REQUIRE(view->switch_dialog->confirm_label != u8"생성 확인");
    }

    // 선택이 없는 채의 확인은 아무것도 실행하지 않는다.
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::confirm_switch_intent {});
    REQUIRE(fixture.submitter.requests.empty());

    // 표시 글이 그대로인 조합 갱신(caret 이동)은 목록이 그대로라 선택도 남는다.
    fixture.controller.handle(gitman::select_switch_candidate_intent { 0 });
    fixture.controller.handle(compose_query(u8"m"));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->selected == 0u);
    fixture.controller.handle(compose_query(u8"ma"));
    REQUIRE(fixture.controller.make_view_snapshot()->switch_dialog->selected.has_value() == false);
}

TEST_CASE("The SVN address bar keeps its rows while a composition is open", "[logic][switch-ui][ime][svn]")
{
    // SVN의 질의 막대는 필터가 아니라 주소 막대라 조합 글이 목록을 건드리지 않는다.
    dialog_fixture fixture { gitman::repository_kind::subversion };
    const std::uint64_t root_operation { fixture.deliver_svn_browser() };
    fixture.deliver_svn_directory(root_operation, u8"https://svn.example.com/repo", { u8"branches", u8"trunk" });
    const std::size_t rows { fixture.controller.make_view_snapshot()->switch_dialog->svn_rows.size() };
    REQUIRE(rows > 0u);

    fixture.controller.handle(compose_query(u8"zzz"));
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->svn_rows.size() == rows);
    REQUIRE(view->switch_dialog->query.composing);
}

TEST_CASE("The checked out branch is marked in the switch candidates", "[logic][ui][switch-ui]")
{
    dialog_fixture fixture {};
    std::vector<gitman::switch_candidate> candidates {};
    candidates.push_back(make_candidate(u8"origin/main", gitman::switch_candidate_kind::git_remote_branch));
    candidates.push_back(make_candidate(u8"main", gitman::switch_candidate_kind::git_local_branch));
    candidates.push_back(make_candidate(u8"develop", gitman::switch_candidate_kind::git_local_branch));
    fixture.deliver_candidates(std::move(candidates));

    // 같은 이름의 remote 후보가 아니라 체크아웃된 local branch만 현재다.
    const auto view { fixture.controller.make_view_snapshot() };
    REQUIRE(view->switch_dialog->current == std::optional<std::size_t> { 1u });

    // 질의로 거른 목록에서도 index가 그 목록을 따른다.
    type_query(fixture.controller, u8"ma");
    const auto filtered { fixture.controller.make_view_snapshot() };
    REQUIRE(filtered->switch_dialog->candidates.size() == 2u);
    REQUIRE(filtered->switch_dialog->current == std::optional<std::size_t> { 1u });

    const auto tree { make_tree(*filtered) };
    const luil::ui_element* const current { tree->find(gitman::ui::switch_dialog_item_id(1)) };
    REQUIRE(current != nullptr);
    REQUIRE(current->accessibility().name == u8"main (현재 브랜치)");
    REQUIRE(tree->find(gitman::ui::switch_dialog_item_id(0))->accessibility().name == u8"origin/main");
}
