#include "helpers/ui_test_support.h"
#include "helpers/vcs_test_doubles.h"

#include "application/cancellation_registry.h"
#include "application/logic_controller.h"
#include "domain/function_trust.h"
#include "infrastructure/vcs_operation_executor.h"
#include "presentation/ui/element_kinds.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// 문서가 지정한 VCS 실행 파일(`settings.git_executable`·`svn_executable`)의 신뢰다 (function-buttons-design 3.10, 5차
// 검수 S1). logic이 요청에 싣는 설정, 알림 띠, 배너 메뉴의 신뢰 확인, 환경설정의 신뢰를 보고, logic이 낸 요청을 실제
// executor와 도구 탐색에 넣어 문서의 실행 파일이 실행되는지 본다. 프로세스와 파일은 대역이다.
namespace {
    constexpr std::u8string_view document_path { u8"C:\\work\\team.version-list" };
    constexpr std::u8string_view document_git { u8"C:\\work\\tools\\git.exe" };
    constexpr std::u8string_view app_git { u8"C:\\Apps\\Git\\cmd\\git.exe" };

    class recording_submitter final : public gitman::operation_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::operation_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        [[nodiscard]] const gitman::operation_request* last_of(const gitman::operation_kind kind) const
        {
            for (auto found = requests.rbegin(); found != requests.rend(); ++found)
            {
                if (found->kind == kind)
                    return &*found;
            }
            return nullptr;
        }

        // 카드 하나의 마지막 조회 요청이다(문서를 연 직후의 로컬 조회 또는 새로 고침).
        [[nodiscard]] const gitman::operation_request* last_query() const
        {
            for (auto found = requests.rbegin(); found != requests.rend(); ++found)
            {
                if (found->kind == gitman::operation_kind::query_local || found->kind == gitman::operation_kind::refresh)
                    return &*found;
            }
            return nullptr;
        }

        std::vector<gitman::operation_request> requests {};
    };

    struct executable_fixture
    {
        recording_submitter submitter {};
        gitman::cancellation_registry registry {};
        gitman::logic_controller controller { submitter, registry };

        executable_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 1280.0f, 720.0f, 1.0f });
            controller.start();
        }

        void load_settings(gitman::app_settings settings = {})
        {
            const gitman::operation_request* const load { submitter.last_of(gitman::operation_kind::load_app_settings) };
            REQUIRE(load != nullptr);
            gitman::app_settings_loaded_event event {};
            event.operation_id = load->operation_id;
            event.settings = std::move(settings);
            controller.handle(std::move(event));
        }

        void open(gitman::workspace_settings_overrides overrides)
        {
            controller.handle(gitman::open_document_intent { std::u8string { document_path } });
            gitman::workspace_document document {};
            document.document_path = document_path;
            document.settings = std::move(overrides);
            gitman::project_definition project {};
            project.id.value = u8"alpha";
            project.display_name = u8"alpha";
            project.path.original = u8"C:\\work\\alpha";
            project.path.normalized = project.path.original;
            project.path.state = gitman::configured_path_state::available;
            document.projects.push_back(std::move(project));
            gitman::document_loaded_event event {};
            event.document = { std::move(document) };
            controller.handle(std::move(event));
            // 문서를 열면 최근 목록 저장이 나간다. 끝내 두어야 신뢰의 앱 설정 저장이 곧바로 나간다.
            finish_settings_save();
        }

        // 진행 중인 조회를 끝낸다. 카드가 바쁘면 새로 고침이 끝날 때까지 미뤄지기 때문이다.
        void finish_query()
        {
            const gitman::operation_request* const query { submitter.last_query() };
            REQUIRE(query != nullptr);
            gitman::query_completed_event event {};
            event.id = query->project.id;
            event.generation = query->generation;
            event.final_event = true;
            event.result.snapshot.project = query->project.id;
            event.result.snapshot.kind = gitman::repository_kind::git;
            event.result.snapshot.availability = gitman::repository_availability::ready;
            controller.handle(std::move(event));
        }

        // 진행 중인 앱 설정 저장을 끝낸다. 없으면 아무것도 하지 않는다(이미 끝난 저장의 결과는 logic이 버린다).
        void finish_settings_save()
        {
            const gitman::operation_request* const save { submitter.last_of(gitman::operation_kind::save_app_settings) };
            if (save == nullptr)
                return;
            gitman::app_settings_saved_event event {};
            event.operation_id = save->operation_id;
            event.succeeded = true;
            controller.handle(std::move(event));
        }

        [[nodiscard]] std::shared_ptr<const gitman::view_snapshot> snapshot() const
        {
            return controller.make_view_snapshot();
        }

        [[nodiscard]] bool has_executable_notice() const
        {
            const std::shared_ptr<const gitman::view_snapshot> view { snapshot() };
            for (const gitman::notice_view_model& notice : view->notices)
            {
                // Git만이면 `Git`, 둘 다면 `Git·SVN`이다.
                if (notice.text.starts_with(u8"문서가 지정한 Git") && notice.text.ends_with(u8" 실행 파일을 쓰지 않았습니다"))
                    return true;
            }
            return false;
        }

        // 배너 우클릭 메뉴의 문서 실행 파일 신뢰 항목이다. 없으면 null이다.
        [[nodiscard]] std::optional<gitman::context_menu_item_view> trust_item()
        {
            controller.handle(gitman::open_document_context_menu_intent { 10.0f, 10.0f });
            const std::shared_ptr<const gitman::view_snapshot> view { snapshot() };
            REQUIRE(view->context_menu.has_value());
            for (const gitman::context_menu_item_view& item : view->context_menu->items)
            {
                if (item.entry == gitman::context_menu_entry::trust_document_executables)
                    return item;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::vector<gitman::trusted_function_definition>> saved_executable_trust() const
        {
            const gitman::operation_request* const save { submitter.last_of(gitman::operation_kind::save_app_settings) };
            if (save == nullptr || save->app_settings_payload.has_value() == false)
                return std::nullopt;
            return save->app_settings_payload->trusted_vcs_executables;
        }
    };

    [[nodiscard]] gitman::workspace_settings_overrides git_override(const std::u8string_view path)
    {
        gitman::workspace_settings_overrides overrides {};
        overrides.git_executable = std::u8string { path };
        return overrides;
    }

    [[nodiscard]] gitman::app_settings app_with_git(const std::u8string_view path)
    {
        gitman::app_settings settings {};
        settings.settings.git_executable = path;
        return settings;
    }

    // 실제 executor와 도구 탐색이다. `PATH`의 폴더와 문서가 지정한 경로에 모두 `git.exe`가 있다. 어떤 git이든
    // `--version`에 같은 답을 한다.
    // 조회만 하므로 문서 store는 쓰이지 않는다.
    class empty_store final : public gitman::project_store
    {
    public:
        [[nodiscard]] gitman::project_store_load_result load(const std::u8string_view) noexcept override
        {
            return {};
        }

        [[nodiscard]] gitman::project_store_save_result save(const std::u8string_view, const gitman::workspace_document&, const gitman::workspace_revision_token&) noexcept override
        {
            return {};
        }
    };

    struct executor_fixture
    {
        empty_store store {};
        gitman::testing::fake_process_runner runner {};
        gitman::testing::fake_vcs_file_probe probe {};
        gitman::vcs_operation_executor executor;
        std::vector<gitman::logic_message> emitted {};

        executor_fixture()
            : executor { store, runner, probe, gitman::vcs_tool_environment { u8"C:\\Path\\Git\\cmd", {} } }
        {
            probe.add_file(u8"C:\\Path\\Git\\cmd\\git.exe");
            probe.add_file(document_git);
            runner.set_default_response({ gitman::process_completion::exited, 0, u8"git version 2.45.1.windows.1\n", {} });
        }

        void run(gitman::operation_request request)
        {
            // 조회 대상 폴더는 없다. 실행 파일 탐색이 먼저 끝나고, 어느 git을 띄웠는지만 본다.
            executor.execute(request, [this](gitman::logic_message message) { emitted.push_back(std::move(message)); });
        }
    };
} // namespace

TEST_CASE("A document git executable is not used until the document executables are trusted", "[function_buttons][s1][trust][logic]")
{
    executable_fixture fixture {};
    fixture.load_settings(app_with_git(app_git));
    fixture.finish_settings_save();
    fixture.open(git_override(document_git));

    // 신뢰하지 않은 문서의 값은 정의하지 않은 것처럼 본다. 앱 설정의 값을 따른다.
    const gitman::operation_request* const query { fixture.submitter.last_query() };
    REQUIRE(query != nullptr);
    REQUIRE(query->settings.git_executable == app_git);
    REQUIRE(fixture.has_executable_notice());

    // 배너 메뉴의 항목이 신뢰 확인을 연다. 확인 dialog는 그 경로를 보이고 `신뢰`로 끝난다.
    const std::optional<gitman::context_menu_item_view> item { fixture.trust_item() };
    REQUIRE(item.has_value());
    REQUIRE(item->label == u8"문서 실행 파일 신뢰…");
    fixture.controller.handle(gitman::select_context_menu_item_intent { item->entry, item->argument });
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.snapshot()->function_confirm_dialog };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::trust);
    REQUIRE(dialog->title == u8"실행 파일 신뢰 확인");
    REQUIRE(dialog->confirm_label == u8"신뢰");
    REQUIRE(dialog->items.size() == 1u);
    REQUIRE(dialog->items.front().label == u8"Git 실행 파일");
    REQUIRE(dialog->items.front().lines.front().text == document_git);

    fixture.finish_query();
    fixture.controller.handle(gitman::confirm_function_run_intent {});
    REQUIRE_FALSE(fixture.snapshot()->function_confirm_dialog.has_value());
    const std::optional<std::vector<gitman::trusted_function_definition>> saved { fixture.saved_executable_trust() };
    REQUIRE(saved.has_value());
    REQUIRE(saved->size() == 1u);
    REQUIRE(saved->front().digest == gitman::vcs_executables_trust_digest(document_git, u8""));
    // 신뢰하면 곧바로 다시 조회해 문서의 실행 파일로 바꾼다.
    const gitman::operation_request* const refreshed { fixture.submitter.last_query() };
    REQUIRE(refreshed->kind == gitman::operation_kind::refresh);
    REQUIRE(refreshed->settings.git_executable == document_git);
    REQUIRE_FALSE(fixture.has_executable_notice());
    REQUIRE_FALSE(fixture.trust_item().has_value());
}

TEST_CASE("A trusted document git executable is used and a changed one is not", "[function_buttons][s1][trust][logic]")
{
    executable_fixture fixture {};
    gitman::app_settings settings {};
    settings.trusted_vcs_executables.push_back({ std::u8string { document_path }, gitman::vcs_executables_trust_digest(document_git, u8"") });
    fixture.load_settings(settings);
    fixture.finish_settings_save();

    fixture.open(git_override(document_git));
    REQUIRE(fixture.submitter.last_query()->settings.git_executable == document_git);
    REQUIRE_FALSE(fixture.has_executable_notice());

    // 손으로 고친 다른 경로는 다시 신뢰해야 쓴다. 앱 설정이 비어 있으면 자동 탐색이다.
    fixture.open(git_override(u8"C:\\work\\tools\\other-git.exe"));
    REQUIRE(fixture.submitter.last_query()->settings.git_executable.empty());
    REQUIRE(fixture.has_executable_notice());
}

TEST_CASE("An empty document executable means automatic discovery and needs no trust", "[function_buttons][s1][trust][logic]")
{
    executable_fixture fixture {};
    fixture.load_settings(app_with_git(app_git));
    fixture.finish_settings_save();
    fixture.open(git_override(u8""));
    REQUIRE(fixture.submitter.last_query()->settings.git_executable.empty());
    REQUIRE_FALSE(fixture.has_executable_notice());
    REQUIRE_FALSE(fixture.trust_item().has_value());
}

TEST_CASE("A document opened before the app settings arrive switches to its trusted executable after the load", "[function_buttons][s1][trust][logic]")
{
    executable_fixture fixture {};
    fixture.open(git_override(document_git));
    // 신뢰 목록을 읽기 전에는 신뢰하지 않은 것으로 본다. 알림은 판정할 수 있게 된 뒤에만 띄운다.
    REQUIRE(fixture.submitter.last_query()->settings.git_executable.empty());
    REQUIRE_FALSE(fixture.has_executable_notice());
    fixture.finish_query();

    gitman::app_settings settings {};
    settings.trusted_vcs_executables.push_back({ std::u8string { document_path }, gitman::vcs_executables_trust_digest(document_git, u8"") });
    fixture.load_settings(settings);
    const gitman::operation_request* const refreshed { fixture.submitter.last_query() };
    REQUIRE(refreshed->kind == gitman::operation_kind::refresh);
    REQUIRE(refreshed->settings.git_executable == document_git);
    REQUIRE_FALSE(fixture.has_executable_notice());
}

TEST_CASE("A git executable picked in the document settings is trusted as the user's own choice", "[function_buttons][s1][trust][logic]")
{
    executable_fixture fixture {};
    fixture.load_settings();
    fixture.finish_settings_save();
    fixture.open({});
    fixture.finish_query();

    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, std::u8string { document_git } });
    fixture.controller.handle(gitman::confirm_settings_intent {});

    const gitman::operation_request* const refreshed { fixture.submitter.last_query() };
    REQUIRE(refreshed->kind == gitman::operation_kind::refresh);
    REQUIRE(refreshed->settings.git_executable == document_git);
    REQUIRE_FALSE(fixture.has_executable_notice());
    const std::optional<std::vector<gitman::trusted_function_definition>> saved { fixture.saved_executable_trust() };
    REQUIRE(saved.has_value());
    REQUIRE(saved->size() == 1u);
}

TEST_CASE("Confirming the settings without picking keeps an untrusted document executable unused", "[function_buttons][s1][trust][logic]")
{
    // 남이 적은 경로를 사용자가 고르지 않았으면 다른 항목을 저장해도 신뢰하지 않는다.
    executable_fixture fixture {};
    fixture.load_settings();
    fixture.finish_settings_save();
    fixture.open(git_override(document_git));
    fixture.finish_query();

    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::subversion, u8"C:\\Apps\\svn\\svn.exe" });
    fixture.controller.handle(gitman::confirm_settings_intent {});
    REQUIRE(fixture.submitter.last_query()->settings.git_executable.empty());
    // 신뢰는 두 값을 함께 본다. 고른 SVN 경로도 남의 Git 경로와 함께라 아직 쓰지 않는다.
    REQUIRE(fixture.submitter.last_query()->settings.svn_executable.empty());
    REQUIRE(fixture.has_executable_notice());
    REQUIRE(fixture.snapshot()->notices.front().text == u8"문서가 지정한 Git·SVN 실행 파일을 쓰지 않았습니다");
}

TEST_CASE("A document svn executable alone is named in the notice", "[function_buttons][s1][trust][logic]")
{
    executable_fixture fixture {};
    fixture.load_settings();
    fixture.finish_settings_save();
    gitman::workspace_settings_overrides overrides {};
    overrides.svn_executable = std::u8string { u8"C:\\work\\tools\\svn.exe" };
    fixture.open(std::move(overrides));

    REQUIRE(fixture.submitter.last_query()->settings.svn_executable.empty());
    const std::shared_ptr<const gitman::view_snapshot> view { fixture.snapshot() };
    REQUIRE_FALSE(view->notices.empty());
    REQUIRE(view->notices.front().text == u8"문서가 지정한 SVN 실행 파일을 쓰지 않았습니다");
    REQUIRE(fixture.trust_item().has_value());
}

TEST_CASE("A document executable trust picked before the app settings arrive is merged into the loaded list", "[function_buttons][s1][trust][logic]")
{
    // 명령줄·파일 연결로 문서를 연 직후 앱 설정을 다 읽기 전에 환경설정에서 실행 파일을 고른 경우다. 그 신뢰는 파일의 어떤
    // 항목보다 최신이라 읽은 목록에 합쳐 저장한다(3.10 "저장·한계").
    executable_fixture fixture {};
    fixture.open({});
    fixture.finish_query();
    fixture.controller.handle(gitman::open_settings_intent {});
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, std::u8string { document_git } });
    fixture.controller.handle(gitman::confirm_settings_intent {});
    // 읽기 전에는 신뢰 목록을 몰라 문서의 값을 아직 쓰지 않고, 앱 설정도 쓰지 않는다(읽은 뒤 한 번에 쓴다).
    REQUIRE(fixture.submitter.last_query()->settings.git_executable.empty());
    REQUIRE_FALSE(fixture.saved_executable_trust().has_value());
    // 바뀐 설정으로 낸 새로 고침을 끝내 둔다. 카드가 바쁘면 읽은 뒤의 새로 고침이 미뤄진다.
    fixture.finish_query();

    constexpr std::u8string_view other_document { u8"C:\\work\\other.version-list" };
    const std::u8string other_digest { gitman::vcs_executables_trust_digest(u8"C:\\other\\git.exe", u8"") };
    gitman::app_settings settings {};
    settings.trusted_vcs_executables.push_back({ std::u8string { other_document }, other_digest });
    fixture.load_settings(settings);

    const std::optional<std::vector<gitman::trusted_function_definition>> saved { fixture.saved_executable_trust() };
    REQUIRE(saved.has_value());
    REQUIRE(saved->size() == 2u);
    bool has_other { false };
    bool has_document { false };
    for (const gitman::trusted_function_definition& value : *saved)
    {
        if (value.document_path == other_document && value.digest == other_digest)
            has_other = true;
        if (value.document_path == document_path && value.digest == gitman::vcs_executables_trust_digest(document_git, u8""))
            has_document = true;
    }
    REQUIRE(has_other);
    REQUIRE(has_document);
    // 읽은 목록에서 신뢰가 확인되어 문서의 실행 파일로 다시 조회한다.
    const gitman::operation_request* const refreshed { fixture.submitter.last_query() };
    REQUIRE(refreshed->kind == gitman::operation_kind::refresh);
    REQUIRE(refreshed->settings.git_executable == document_git);
    REQUIRE_FALSE(fixture.has_executable_notice());
}

TEST_CASE("The document settings mark an untrusted document executable as unused", "[function_buttons][s1][trust][logic][settings-ui]")
{
    // 환경설정 초안은 유효 값에서 시작해 신뢰하지 않은 남의 경로도 그 행에 보인다. 쓰는 값처럼 보이지 않게 표시한다.
    executable_fixture fixture {};
    fixture.load_settings();
    fixture.finish_settings_save();
    fixture.open(git_override(document_git));
    fixture.finish_query();

    fixture.controller.handle(gitman::open_settings_intent {});
    const std::optional<gitman::settings_dialog_view> untrusted { fixture.snapshot()->settings_dialog };
    REQUIRE(untrusted.has_value());
    REQUIRE(untrusted->git_path == document_git);
    REQUIRE(untrusted->git_unused);
    REQUIRE_FALSE(untrusted->svn_unused);

    // 이 dialog에서 고른 값은 저장하면 신뢰하므로 표시를 거둔다.
    fixture.controller.handle(gitman::set_settings_executable_intent { gitman::repository_kind::git, std::u8string { document_git } });
    REQUIRE_FALSE(fixture.snapshot()->settings_dialog->git_unused);
    fixture.controller.handle(gitman::confirm_settings_intent {});
    fixture.finish_query();

    // 신뢰한 뒤 다시 열면 쓰는 값이다.
    fixture.controller.handle(gitman::open_settings_intent {});
    const std::optional<gitman::settings_dialog_view> trusted { fixture.snapshot()->settings_dialog };
    REQUIRE(trusted.has_value());
    REQUIRE(trusted->git_path == document_git);
    REQUIRE_FALSE(trusted->git_unused);
}

TEST_CASE("The executable trust dialog names what it trusts to a screen reader", "[function_buttons][s1][trust][ui]")
{
    executable_fixture fixture {};
    fixture.load_settings();
    fixture.finish_settings_save();
    fixture.open(git_override(document_git));
    const std::optional<gitman::context_menu_item_view> item { fixture.trust_item() };
    REQUIRE(item.has_value());
    fixture.controller.handle(gitman::select_context_menu_item_intent { item->entry, item->argument });

    const auto tree { gitman::testing::make_tree(*fixture.snapshot()) };
    const luil::ui_element* const panel { gitman::testing::find(*tree, gitman::ui::kind_function_confirm_dialog_panel) };
    REQUIRE(panel != nullptr);
    REQUIRE(panel->accessibility().name.starts_with(u8"실행 파일 신뢰 확인, "));
}

TEST_CASE("The executor never starts an untrusted document git executable", "[function_buttons][s1][trust][executor]")
{
    // logic이 낸 요청을 실제 executor와 도구 탐색(`vcs_operation_executor.cpp`·`vcs_tool_discovery.cpp`)에 넣는다.
    executable_fixture fixture {};
    fixture.load_settings();
    fixture.finish_settings_save();
    fixture.open(git_override(document_git));

    SECTION("신뢰하지 않으면 자동 탐색한 git만 띄운다")
    {
        executor_fixture executor {};
        executor.run(*fixture.submitter.last_query());
        REQUIRE(executor.runner.request_count() > 0u);
        for (const gitman::process_request& request : executor.runner.requests())
            REQUIRE(request.executable == u8"C:\\Path\\Git\\cmd\\git.exe");
    }

    SECTION("신뢰하면 문서가 지정한 git을 띄운다")
    {
        const std::optional<gitman::context_menu_item_view> item { fixture.trust_item() };
        REQUIRE(item.has_value());
        fixture.controller.handle(gitman::select_context_menu_item_intent { item->entry, item->argument });
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        fixture.finish_query();
        const gitman::operation_request* const refreshed { fixture.submitter.last_query() };
        REQUIRE(refreshed->settings.git_executable == document_git);

        executor_fixture executor {};
        executor.run(*refreshed);
        REQUIRE(executor.runner.request_count() > 0u);
        REQUIRE(executor.runner.requests().front().executable == document_git);
    }
}
