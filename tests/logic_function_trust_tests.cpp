#include "application/cancellation_registry.h"
#include "application/logic_controller.h"
#include "application/process_request.h"
#include "domain/function_trust.h"
#include "helpers/ui_test_support.h"
#include "infrastructure/function_execution_policy.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_confirm_dialog_element.h"
#include "presentation/ui/gitman_policy.h"

#include "luil/ui/font_scale.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 기능 버튼의 신뢰 확인과 `confirm` 미리보기, cmd 보정의 최종 승인이다 (function-buttons-design 3.10, 4.5, 6.1 T7·R4). intent와
// 앱 설정 event를 직접 넣고 dialog view, 제출된 요청, 앱 설정 저장 요청, 카드 로그를 본다.
namespace {
    using gitman::testing::as_intent;
    using gitman::testing::find;
    using gitman::testing::id_of;
    using gitman::testing::make_tree;

    constexpr std::u8string_view document_path { u8"C:\\work\\projects.version-list" };

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

        std::vector<gitman::operation_request> requests {};
    };

    class recording_function_submitter final : public gitman::function_run_submitter
    {
    public:
        [[nodiscard]] bool submit(gitman::function_run_request request) override
        {
            requests.push_back(std::move(request));
            return true;
        }

        std::vector<gitman::function_run_request> requests {};
    };

    gitman::project_definition make_project(const std::u8string_view id, const std::u8string_view root = u8"C:\\work\\")
    {
        gitman::project_definition project {};
        project.id.value = id;
        project.display_name = id;
        project.path.original = std::u8string { root } + std::u8string { id };
        project.path.normalized = project.path.original;
        project.path.state = gitman::configured_path_state::available;
        return project;
    }

    gitman::function_button make_button(const std::u8string_view id, const std::u8string_view label, const std::u8string_view program = u8"C:\\Tools\\build.exe")
    {
        gitman::function_button button {};
        button.id = id;
        button.label = label;
        button.program = program;
        return button;
    }

    gitman::query_completed_event make_local_result(const std::u8string_view id, const std::u8string_view branch)
    {
        gitman::query_completed_event event {};
        event.id.value = id;
        event.generation = 1u;
        event.final_event = true;
        event.result.snapshot.project.value = id;
        event.result.snapshot.kind = gitman::repository_kind::git;
        event.result.snapshot.availability = gitman::repository_availability::ready;
        event.result.snapshot.current_reference = branch;
        event.result.snapshot.repository_root = std::u8string { u8"C:\\work\\" } + std::u8string { id };
        event.result.snapshot.local_revision = u8"a1b2c3d";
        event.result.snapshot.working_tree.state = gitman::working_tree_state::clean;
        return event;
    }

    gitman::function_buttons_definition make_definition(std::vector<gitman::function_button> buttons, const std::u8string_view digest_input = u8"{\"buttons\":[]}")
    {
        gitman::function_buttons_definition definition {};
        for (gitman::function_button& button : buttons)
            definition.buttons.emplace_back(std::move(button));
        // 파서가 문서를 읽을 때 싣는 값이다. 여기서는 정의가 바뀐 것을 흉내 낼 때 바꾼다.
        definition.source_digest_input = digest_input;
        return definition;
    }

    // 문서를 열고 기능 실행 경계를 묶은 controller다. 앱 설정은 `load_settings`로 읽은 것으로 한다.
    struct trust_fixture
    {
        recording_submitter submitter {};
        recording_function_submitter functions {};
        gitman::cancellation_registry registry {};
        gitman::logic_controller controller { submitter, registry };
        std::map<std::u8string, std::u8string> environment {};

        trust_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 1280.0f, 720.0f, 1.0f });
            controller.bind_function_execution(functions
                , gitman::make_function_run_services(
                    [this](const std::u8string_view name) -> std::optional<std::u8string>
                    {
                        const auto found { environment.find(std::u8string { name }) };
                        if (found == environment.end())
                            return std::nullopt;
                        return found->second;
                    }
                )
            );
        }

        // 앱 설정 읽기를 끝낸다. 문서를 열기 전에 부르면 최근 목록 저장이 바로 나간다.
        void load_settings(gitman::app_settings settings = {})
        {
            controller.start();
            const gitman::operation_request* const load { submitter.last_of(gitman::operation_kind::load_app_settings) };
            REQUIRE(load != nullptr);
            gitman::app_settings_loaded_event event {};
            event.operation_id = load->operation_id;
            event.settings = std::move(settings);
            controller.handle(std::move(event));
        }

        void open(std::vector<gitman::project_definition> projects, gitman::function_buttons_definition definition, const std::u8string_view path = document_path)
        {
            controller.handle(gitman::open_document_intent { std::u8string { path } });
            // 출력 수집 기능이 실행 중이면 다른 문서를 열기 전에 묻는다 (T11, 4.10). 이 fixture는 확인하고 연다.
            const std::shared_ptr<const gitman::view_snapshot> view { controller.make_view_snapshot() };
            if (view->function_confirm_dialog.has_value() && view->function_confirm_dialog->mode == gitman::function_confirm_mode::running)
                controller.handle(gitman::confirm_function_run_intent {});
            gitman::document_loaded_event event {};
            gitman::workspace_document document {};
            document.document_path = path;
            document.projects = std::move(projects);
            document.function_buttons = std::move(definition);
            event.document = { std::move(document) };
            controller.handle(std::move(event));
        }

        void query(const std::u8string_view id, const std::u8string_view branch = u8"main")
        {
            controller.handle(make_local_result(id, branch));
        }

        void run(const std::u8string_view id, const std::u8string_view button)
        {
            controller.handle(gitman::run_function_intent { gitman::project_id { std::u8string { id } }, std::u8string { button } });
            static_cast<void>(answer_cmd_inspection());
        }

        // worker가 할 cmd 대상 확인(배치 파일 읽기, S3)에 답한다. 답하지 않은 요청 중 마지막 것이다. 기본은 파일이 없다는
        // 답이라 S3 전의 판정 그대로다. 답할 요청이 없으면 거짓이다.
        bool answer_cmd_inspection(const std::optional<gitman::cmd_batch_findings> batch = std::nullopt, const bool path_exists = false)
        {
            const gitman::operation_request* const request { submitter.last_of(gitman::operation_kind::inspect_cmd_target) };
            if (request == nullptr || request->operation_id <= answered_cmd_inspection || request->cmd_target.has_value() == false)
                return false;
            answered_cmd_inspection = request->operation_id;
            gitman::cmd_target_inspected_event event {};
            event.operation_id = request->operation_id;
            event.facts.query = *request->cmd_target;
            event.facts.path_exists = path_exists;
            event.facts.batch = batch;
            controller.handle(std::move(event));
            return true;
        }

        std::uint64_t answered_cmd_inspection { 0 };

        void confirm()
        {
            controller.handle(gitman::confirm_function_run_intent {});
        }

        [[nodiscard]] std::optional<gitman::function_confirm_dialog_view> dialog() const
        {
            return controller.make_view_snapshot()->function_confirm_dialog;
        }

        [[nodiscard]] const gitman::operation_log_entry& last_log(const std::u8string_view id) const
        {
            const gitman::operation_log_buffer* const buffer { controller.card_log(gitman::project_id { std::u8string { id } }) };
            REQUIRE(buffer != nullptr);
            REQUIRE(buffer->records().empty() == false);
            return buffer->records().back().entry;
        }

        // 마지막 앱 설정 저장 요청이 실은 신뢰 목록이다. 저장 요청이 없으면 값이 없다.
        [[nodiscard]] std::optional<std::vector<gitman::trusted_function_definition>> saved_trust() const
        {
            const gitman::operation_request* const save { submitter.last_of(gitman::operation_kind::save_app_settings) };
            if (save == nullptr || save->app_settings_payload.has_value() == false)
                return std::nullopt;
            return save->app_settings_payload->trusted_function_definitions;
        }

        // 제출된 기능 실행을 모두 끝낸다. 같은 카드에서 출력 수집은 하나만 돌기 때문이다.
        void completed_all()
        {
            for (const gitman::function_run_request& request : functions.requests)
            {
                gitman::function_completed_event event {};
                event.id = request.id;
                event.run_id = request.run_id;
                event.outcome = gitman::function_run_outcome::exited;
                event.exit_code = 0;
                controller.handle(std::move(event));
            }
        }

        // 진행 중인 앱 설정 저장을 끝낸다. 다음 저장이 바로 나가게 하려는 것이다.
        void finish_settings_save(const bool succeeded)
        {
            const gitman::operation_request* const save { submitter.last_of(gitman::operation_kind::save_app_settings) };
            REQUIRE(save != nullptr);
            gitman::app_settings_saved_event event {};
            event.operation_id = save->operation_id;
            event.succeeded = succeeded;
            if (succeeded == false)
            {
                gitman::diagnostic value {};
                value.code = gitman::diagnostic_code::app_settings_write_failed;
                value.severity = gitman::diagnostic_severity::warning;
                value.message = u8"앱 설정을 저장하지 못했습니다.";
                event.diagnostics.push_back(std::move(value));
            }
            controller.handle(std::move(event));
        }
    };

    bool contains(const std::u8string_view text, const std::u8string_view part)
    {
        return text.find(part) != std::u8string_view::npos;
    }

    // 항목의 줄을 한 글로 잇는다 (test 비교용).
    std::u8string joined_lines(const gitman::function_confirm_item_view& item)
    {
        std::u8string text {};
        for (const gitman::function_confirm_line& line : item.lines)
        {
            text += line.text;
            text += u8"\n";
        }
        return text;
    }

    std::vector<luil::input_action> click_action(const luil::ui_tree& tree, const luil::ui_element_id& id)
    {
        const luil::ui_element* const element { tree.find(id) };
        REQUIRE(element != nullptr);
        const luil::ui_action* const action { element->action(luil::ui_trigger::left_click) };
        REQUIRE(action != nullptr);
        return (*action)({});
    }
} // namespace

TEST_CASE("An untrusted definition asks before the first run and remembers the trust", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    gitman::function_button build { make_button(u8"build", u8"빌드") };
    build.args = { u8"--config", u8"${config}", u8"--token", u8"abc123secret", u8"${env:API_KEY}" };
    gitman::function_button lint { make_button(u8"lint", u8"린트", u8"C:\\Tools\\lint.exe") };
    lint.mode = gitman::function_run_mode::detached;
    gitman::function_buttons_definition definition { make_definition({ lint, build }) };
    definition.variables.push_back({ u8"config", std::u8string { u8"Debug" } });
    definition.project_variables.push_back({ u8"beta", std::vector<gitman::function_variable> { { u8"config", std::u8string { u8"Release" } } } });
    fixture.open({ make_project(u8"alpha"), make_project(u8"beta") }, definition);
    fixture.environment[u8"API_KEY"] = u8"env-secret";
    fixture.query(u8"alpha");
    // 문서를 열 때의 최근 목록 저장을 끝내 둔다. 신뢰 저장이 대기하지 않고 바로 나간다.
    fixture.finish_settings_save(true);
    fixture.submitter.requests.clear();

    fixture.run(u8"alpha", u8"build");

    // 확인 전에는 실행하지 않는다 (B9).
    REQUIRE(fixture.functions.requests.empty());
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::trust);
    REQUIRE(dialog->subject == document_path);
    REQUIRE(dialog->confirm_label == u8"신뢰하고 실행");
    REQUIRE(dialog->note.empty() == false);

    // 정의 전체를 치환 전 원문으로 보인다. 누른 기능이 맨 앞이고 나머지는 문서 순서다.
    REQUIRE(dialog->items.size() == 2u);
    const gitman::function_confirm_item_view& first { dialog->items[0] };
    REQUIRE(first.label == u8"빌드");
    REQUIRE(first.mode_text == u8"출력을 로그로 받기");
    const std::u8string text { joined_lines(first) };
    REQUIRE(contains(text, u8"C:\\Tools\\build.exe\n"));
    REQUIRE(contains(text, u8"--config ${config} --token "));
    REQUIRE(contains(text, u8"${env:API_KEY}"));
    REQUIRE(contains(text, u8"작업 폴더 ${projectRoot}"));
    // 원문에 적은 비밀도 표시용 명령줄과 같은 가리기를 거친다. env 값은 원문이라 나오지 않는다.
    REQUIRE_FALSE(contains(text, u8"abc123secret"));
    REQUIRE_FALSE(contains(text, u8"env-secret"));
    // 참조하는 사용자 변수는 문서 값과 저장소별 덮어쓰기를 함께 보인다.
    REQUIRE(first.lines.back().text == u8"config = Debug (beta: Release)");
    REQUIRE(first.lines.back().dim);
    REQUIRE(dialog->items[1].label == u8"린트");
    REQUIRE(dialog->items[1].mode_text == u8"따로 실행");

    // 떠 있는 동안 다른 실행 intent는 받지 않는다 (`Alt+N`).
    fixture.run(u8"alpha", u8"lint");
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.dialog()->items.front().label == u8"빌드");

    fixture.confirm();

    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 1u);
    const gitman::process_request& command { std::get<gitman::process_request>(fixture.functions.requests.front().command) };
    REQUIRE(command.arguments.back() == u8"env-secret");
    // 앱 설정에 문서 경로와 해시를 쓴다.
    const std::optional<std::vector<gitman::trusted_function_definition>> saved { fixture.saved_trust() };
    REQUIRE(saved.has_value());
    REQUIRE(saved->size() == 1u);
    REQUIRE(saved->front().document_path == document_path);
    const gitman::project_definition projects[] { make_project(u8"alpha"), make_project(u8"beta") };
    REQUIRE(saved->front().digest == gitman::function_trust_digest(definition, projects));

    // 신뢰한 뒤에는 묻지 않는다. 다른 기능도 같은 정의라 묻지 않는다.
    fixture.run(u8"alpha", u8"lint");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 2u);
}

TEST_CASE("Cancelling the trust dialog runs nothing and asks again next time", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    fixture.open({ make_project(u8"alpha") }, make_definition({ make_button(u8"build", u8"빌드") }));
    fixture.submitter.requests.clear();

    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.dialog().has_value());
    fixture.controller.handle(gitman::cancel_function_run_intent {});

    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE_FALSE(fixture.saved_trust().has_value());

    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->mode == gitman::function_confirm_mode::trust);
}

TEST_CASE("The trust is asked again when the definition or a project path or name changes", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    const std::vector<gitman::project_definition> projects { make_project(u8"alpha"), make_project(u8"beta") };
    fixture.open(projects, make_definition({ make_button(u8"build", u8"빌드") }));
    fixture.run(u8"alpha", u8"build");
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.functions.requests.clear();

    SECTION("same digest input with placement changed and cards reordered")
    {
        // 해시 입력(파서가 `placement`만 뺀 원문, project `{id, path, 표시 이름}`)이 같으면 묻지 않는다. 이 fixture는
        // 해시 입력을 고정 값으로 넣으므로 모델에서는 배치만 바꾼다. 원문에서 `placement`만 빠지고 `label`·`icon`이
        // 남는 것은 `function_trust_tests.cpp`가 파서로 확인한다 (D29).
        gitman::function_button placed { make_button(u8"build", u8"빌드") };
        placed.placement = u8"bar";
        fixture.open({ projects[1], projects[0] }, make_definition({ placed }));
        fixture.run(u8"alpha", u8"build");
        REQUIRE_FALSE(fixture.dialog().has_value());
        REQUIRE(fixture.functions.requests.size() == 1u);
    }

    SECTION("definition changed")
    {
        fixture.open(projects, make_definition({ make_button(u8"build", u8"빌드") }, u8"{\"buttons\":[{\"id\":\"build\"}]}"));
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.functions.requests.empty());
    }

    SECTION("project path changed")
    {
        fixture.open({ make_project(u8"alpha", u8"D:\\elsewhere\\"), projects[1] }, make_definition({ make_button(u8"build", u8"빌드") }));
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.functions.requests.empty());
    }

    SECTION("project display name changed")
    {
        // `${projectName}`과 `GITMAN_PROJECT_NAME`은 실행 입력이다 (M3).
        gitman::project_definition renamed { make_project(u8"alpha") };
        renamed.display_name = u8"알파";
        fixture.open({ renamed, projects[1] }, make_definition({ make_button(u8"build", u8"빌드") }));
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.functions.requests.empty());
    }

    SECTION("another document with the same definition")
    {
        fixture.open(projects, make_definition({ make_button(u8"build", u8"빌드") }), u8"C:\\work\\other.version-list");
        fixture.run(u8"alpha", u8"build");
        REQUIRE(fixture.dialog().has_value());
    }
}

TEST_CASE("A trust that cannot be written is remembered for the session", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    fixture.open({ make_project(u8"alpha") }, make_definition({ make_button(u8"build", u8"빌드") }));
    // 문서를 열 때의 최근 목록 저장을 실패로 끝낸다 (실행 파일 폴더가 보호된 위치).
    fixture.finish_settings_save(false);
    fixture.submitter.requests.clear();

    fixture.run(u8"alpha", u8"build");
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.saved_trust().has_value());
    fixture.finish_settings_save(false);

    // 파일에는 남지 않았지만 이 실행 동안은 다시 묻지 않는다.
    fixture.completed_all();
    fixture.functions.requests.clear();
    fixture.run(u8"alpha", u8"build");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 1u);
    // 저장 실패 알림은 한 번만이다 (기존 규칙).
    REQUIRE(fixture.controller.make_view_snapshot()->notices.size() == 1u);
}

TEST_CASE("A trust asked for a run pressed before the app settings arrive is saved with the loaded list", "[function_buttons][t7][s1]")
{
    // 앱 설정을 읽기 전에 누른 기능은 읽기가 끝날 때까지 기다린다 (5차 검수, S1). 그 뒤에 묻고 기록한 신뢰는 읽은 목록에
    // 더해 저장한다 — 읽기 전 신뢰를 읽은 값에 합치는 T7의 규칙은 그대로다(읽기 전에는 신뢰를 기록할 길이 없어졌다).
    trust_fixture fixture {};
    fixture.controller.start();
    const std::uint64_t load_id { fixture.submitter.last_of(gitman::operation_kind::load_app_settings)->operation_id };
    fixture.open({ make_project(u8"alpha") }, make_definition({ make_button(u8"build", u8"빌드") }));
    fixture.run(u8"alpha", u8"build");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.empty());
    // 읽기가 끝나기 전에는 저장하지 않는다 (파일 내용을 지우지 않으려는 기존 규칙).
    REQUIRE_FALSE(fixture.saved_trust().has_value());

    gitman::app_settings stored {};
    stored.trusted_function_definitions.push_back({ u8"C:\\work\\old.version-list", std::u8string(64, u8'e') });
    gitman::app_settings_loaded_event event {};
    event.operation_id = load_id;
    event.settings = stored;
    fixture.controller.handle(std::move(event));

    // 읽은 목록에 이 문서의 신뢰가 없어 이제 묻는다.
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::trust);
    fixture.finish_settings_save(true);
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);

    const std::optional<std::vector<gitman::trusted_function_definition>> saved { fixture.saved_trust() };
    REQUIRE(saved.has_value());
    REQUIRE(saved->size() == 2u);
    REQUIRE(gitman::recent_document_key(saved->back().document_path) == gitman::recent_document_key(document_path));

    fixture.completed_all();
    fixture.functions.requests.clear();
    fixture.run(u8"alpha", u8"build");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("A run pressed before the app settings arrive runs without asking once the trust is read", "[function_buttons][s1]")
{
    // 명령줄·파일 연결로 문서를 연 직후에 누른 기능이다. 읽은 신뢰 목록에 이 정의가 있으면 묻지 않고 실행한다 (5차 검수).
    trust_fixture fixture {};
    fixture.controller.start();
    const std::uint64_t load_id { fixture.submitter.last_of(gitman::operation_kind::load_app_settings)->operation_id };
    const gitman::function_buttons_definition definition { make_definition({ make_button(u8"build", u8"빌드") }) };
    const gitman::project_definition projects[] { make_project(u8"alpha") };
    fixture.open({ make_project(u8"alpha") }, definition);
    fixture.run(u8"alpha", u8"build");
    // 키로 오는 실행(`Alt+N`과 같은 길)이 한 번 더 와도 기다리는 실행은 하나다.
    fixture.run(u8"alpha", u8"build");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.empty());

    gitman::app_settings_loaded_event event {};
    event.operation_id = load_id;
    event.settings.trusted_function_definitions.push_back({ std::u8string { document_path }, gitman::function_trust_digest(definition, projects) });
    fixture.controller.handle(std::move(event));

    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("A run waiting for the app settings is dropped when another document opens", "[function_buttons][s1]")
{
    trust_fixture fixture {};
    fixture.controller.start();
    const std::uint64_t load_id { fixture.submitter.last_of(gitman::operation_kind::load_app_settings)->operation_id };
    const gitman::function_buttons_definition definition { make_definition({ make_button(u8"build", u8"빌드") }) };
    const gitman::project_definition projects[] { make_project(u8"alpha") };
    fixture.open({ make_project(u8"alpha") }, definition);
    fixture.run(u8"alpha", u8"build");
    // 같은 카드와 기능이 있는 다른 문서다. 기다리던 실행은 이전 문서의 것이라 버린다.
    fixture.open({ make_project(u8"alpha") }, definition, u8"C:\\work\\other.version-list");

    gitman::app_settings_loaded_event event {};
    event.operation_id = load_id;
    event.settings.trusted_function_definitions.push_back({ u8"C:\\work\\other.version-list", gitman::function_trust_digest(definition, projects) });
    fixture.controller.handle(std::move(event));

    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.empty());
}

TEST_CASE("A confirm button previews the display command line before every run", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    gitman::function_button clean { make_button(u8"clean", u8"정리") };
    clean.args = { u8"--token", u8"abc123secret", u8"${env:API_KEY}", u8"${branch}" };
    clean.confirm = true;
    const gitman::function_buttons_definition definition { make_definition({ clean }) };
    const gitman::project_definition projects[] { make_project(u8"alpha") };
    gitman::app_settings settings {};
    settings.trusted_function_definitions.push_back({ std::u8string { document_path }, gitman::function_trust_digest(definition, projects) });
    fixture.load_settings(settings);
    fixture.open({ make_project(u8"alpha") }, definition);
    fixture.environment[u8"API_KEY"] = u8"env-secret";
    fixture.query(u8"alpha", u8"main");

    fixture.run(u8"alpha", u8"clean");

    REQUIRE(fixture.functions.requests.empty());
    std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::preview);
    REQUIRE(dialog->subject == u8"alpha");
    REQUIRE(dialog->confirm_label == u8"실행");
    REQUIRE(dialog->items.size() == 1u);
    // 로그 시작 줄과 같은 표시용 명령줄이다. env는 이름으로 남고 비밀은 가린다 (3.6).
    const std::u8string text { joined_lines(dialog->items.front()) };
    REQUIRE(contains(text, u8"\"C:\\Tools\\build.exe\" --token "));
    REQUIRE(contains(text, u8"${env:API_KEY} main\n"));
    REQUIRE_FALSE(contains(text, u8"abc123secret"));
    REQUIRE_FALSE(contains(text, u8"env-secret"));
    REQUIRE(dialog->items.front().lines.back().text == u8"작업 폴더 C:\\work\\alpha");

    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(contains(fixture.last_log(u8"alpha").text, u8"실행: "));

    // 신뢰와 달리 미리보기는 매번이다.
    fixture.completed_all();
    fixture.run(u8"alpha", u8"clean");
    REQUIRE(fixture.dialog().has_value());
}

TEST_CASE("An untrusted confirm button shows the preview inside the trust dialog and runs once", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    gitman::function_button clean { make_button(u8"clean", u8"정리") };
    clean.args = { u8"${branch}" };
    clean.confirm = true;
    fixture.open({ make_project(u8"alpha") }, make_definition({ clean, make_button(u8"build", u8"빌드") }));
    fixture.query(u8"alpha", u8"main");
    fixture.finish_settings_save(true);
    fixture.submitter.requests.clear();

    fixture.run(u8"alpha", u8"clean");

    // 신뢰 dialog 하나가 원문과 미리보기 줄(표시용 명령줄, 치환한 작업 폴더)을 함께 보인다.
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::trust);
    const std::u8string text { joined_lines(dialog->items.front()) };
    REQUIRE(contains(text, u8"${branch}\n"));
    REQUIRE(contains(text, u8"실행할 명령 \"C:\\Tools\\build.exe\" main\n"));
    REQUIRE(contains(text, u8"실행할 작업 폴더 C:\\work\\alpha\n"));
    // 미리보기 줄은 누른 `confirm` 기능에만 붙는다.
    REQUIRE_FALSE(contains(joined_lines(dialog->items[1]), u8"실행할 명령"));

    // 확인 한 번이 신뢰와 미리보기를 함께 끝낸다. 같은 자리를 한 번 더 누른 확인(더블클릭, Enter 반복)은
    // 이어 뜬 dialog가 없으므로 아무것도 하지 않는다.
    fixture.confirm();
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.saved_trust().has_value());
    REQUIRE(fixture.saved_trust()->size() == 1u);

    // 신뢰한 뒤에도 `confirm`은 별개다 (3.10). 다음 실행은 미리보기 dialog를 거친다.
    fixture.completed_all();
    fixture.run(u8"alpha", u8"clean");
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->mode == gitman::function_confirm_mode::preview);
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("The confirm dialog wraps long lines and reveals hidden characters", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    // 긴 인자의 뒤에 둔 명령이 말줄임으로 숨지 않아야 한다 (검토 M2).
    std::u8string payload { u8"Write-Host ok" };
    payload += std::u8string(200, u8' ');
    payload += u8"; Remove-Item -Recurse C:\\data";
    gitman::function_button script { make_button(u8"script", u8"스크립트", u8"C:\\Windows\\System32\\WindowsPowerShell\\v1.0\\powershell.exe") };
    script.args = { u8"-Command", payload, u8"a\nb\u202Ec" };
    fixture.open({ make_project(u8"alpha") }, make_definition({ script }));

    fixture.run(u8"alpha", u8"script");

    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    const gitman::function_confirm_item_view& item { dialog->items.front() };
    const std::size_t columns { gitman::function_confirm_code_columns(1280.0f) };
    bool continued { false };
    for (const gitman::function_confirm_line& line : item.lines)
    {
        // 한 줄은 칸 수를 넘지 않는다 (모두 ASCII라 byte 수가 칸 수다).
        REQUIRE(line.text.size() <= columns);
        continued = continued || line.continuation;
    }
    REQUIRE(continued);
    // 나눈 줄을 이으면 원래 글이 그대로다. 뒤의 명령이 어느 줄엔가 보인다.
    std::u8string joined {};
    for (const gitman::function_confirm_line& line : item.lines)
    {
        if (line.continuation == false && joined.empty() == false)
            joined += u8"\n";
        joined += line.text;
    }
    REQUIRE(contains(joined, u8"; Remove-Item -Recurse C:\\data"));
    // 줄 바꿈과 방향 바꿈 글자는 이름으로 드러난다.
    REQUIRE(contains(joined, u8"a<U+000A>b<U+202E>c"));

    // 창이 좁으면 snapshot이 그 폭으로 다시 나눈다.
    const std::size_t wide_lines { item.lines.size() };
    fixture.controller.handle(gitman::window_metrics_intent { 480.0f, 320.0f, 1.0f });
    REQUIRE(fixture.dialog()->items.front().lines.size() > wide_lines);
}

TEST_CASE("The confirm dialog list scrolls by keyboard", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.controller.handle(gitman::window_metrics_intent { 800.0f, 400.0f, 1.0f });
    fixture.load_settings();
    std::vector<gitman::function_button> buttons {};
    for (char8_t index = u8'a'; index <= u8'h'; ++index)
        buttons.push_back(make_button(std::u8string { u8"tool-" } + index, std::u8string { u8"도구 " } + index));
    fixture.open({ make_project(u8"alpha"), make_project(u8"beta") }, make_definition(std::move(buttons)));
    fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"alpha" } });
    fixture.run(u8"alpha", u8"tool-a");

    // 방향키·쪽 키·Home·End는 dialog가 떠 있는 동안 목록 스크롤이다. 방향키가 뒤의 카드 선택으로 가지 않는다.
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    gitman::ui::gitman_policy policy {};
    luil::interaction_snapshot interaction {};
    const auto key_step = [&](const luil::key_code key)
    {
        const std::vector<luil::input_action> actions { policy.on_key(tree.get(), luil::key_pressed_event { key }, interaction) };
        const auto* const scroll { as_intent<gitman::function_confirm_scroll_intent>(actions) };
        REQUIRE(scroll != nullptr);
        REQUIRE(as_intent<gitman::select_card_intent>(actions) == nullptr);
        return *scroll;
    };
    REQUIRE(key_step(luil::key_code::arrow_down).step == gitman::function_confirm_scroll_step::line_down);
    REQUIRE(key_step(luil::key_code::arrow_up).step == gitman::function_confirm_scroll_step::line_up);
    REQUIRE(key_step(luil::key_code::page_down).step == gitman::function_confirm_scroll_step::page_down);
    REQUIRE(key_step(luil::key_code::page_up).step == gitman::function_confirm_scroll_step::page_up);
    REQUIRE(key_step(luil::key_code::home).step == gitman::function_confirm_scroll_step::start);
    REQUIRE(key_step(luil::key_code::end).step == gitman::function_confirm_scroll_step::end);

    // logic이 목록 높이로 한 쪽을 정하고 끝에서 멈춘다.
    const gitman::function_confirm_dialog_view before { *fixture.dialog() };
    float content { 0.0f };
    for (std::size_t index = 0; index < before.items.size(); ++index)
    {
        if (index > 0)
            content += gitman::layout_function_confirm_item_gap;
        content += gitman::layout_function_confirm_item_height(before.items[index].lines.size());
    }
    const float viewport { gitman::function_confirm_list_height(400.0f, before.description.size() + 1, before.note.size()) };
    REQUIRE(viewport < content);

    fixture.controller.handle(gitman::function_confirm_scroll_intent { 0.0f, gitman::function_confirm_scroll_step::line_down });
    const float line { fixture.dialog()->scroll_offset };
    REQUIRE(line > 0.0f);
    fixture.controller.handle(gitman::function_confirm_scroll_intent { 0.0f, gitman::function_confirm_scroll_step::page_down });
    REQUIRE(fixture.dialog()->scroll_offset > line);
    fixture.controller.handle(gitman::function_confirm_scroll_intent { 0.0f, gitman::function_confirm_scroll_step::end });
    REQUIRE(fixture.dialog()->scroll_offset == content - viewport);
    fixture.controller.handle(gitman::function_confirm_scroll_intent { 0.0f, gitman::function_confirm_scroll_step::start });
    REQUIRE(fixture.dialog()->scroll_offset == 0.0f);

    // dialog가 닫히면 방향키는 다시 카드 선택이다.
    fixture.controller.handle(gitman::cancel_function_run_intent {});
    const auto closed { make_tree(*fixture.controller.make_view_snapshot()) };
    const std::vector<luil::input_action> actions { policy.on_key(closed.get(), luil::key_pressed_event { luil::key_code::arrow_down }, interaction) };
    REQUIRE(as_intent<gitman::function_confirm_scroll_intent>(actions) == nullptr);
}

TEST_CASE("A run whose values changed after the confirmation is aborted with one log line", "[function_buttons][t7]")
{
    trust_fixture fixture {};

    SECTION("a runtime value changes under the preview")
    {
        gitman::function_button deploy { make_button(u8"deploy", u8"배포") };
        deploy.args = { u8"${branch}" };
        deploy.confirm = true;
        const gitman::function_buttons_definition definition { make_definition({ deploy }) };
        const gitman::project_definition projects[] { make_project(u8"alpha") };
        gitman::app_settings settings {};
        settings.trusted_function_definitions.push_back({ std::u8string { document_path }, gitman::function_trust_digest(definition, projects) });
        fixture.load_settings(settings);
        fixture.open({ make_project(u8"alpha") }, definition);
        fixture.query(u8"alpha", u8"main");
        fixture.run(u8"alpha", u8"deploy");
        REQUIRE(fixture.dialog().has_value());

        // dialog가 떠 있는 동안 조회 결과가 와서 `${branch}`가 바뀌었다.
        fixture.query(u8"alpha", u8"release");
        fixture.confirm();

        REQUIRE(fixture.functions.requests.empty());
        REQUIRE_FALSE(fixture.dialog().has_value());
        REQUIRE(fixture.last_log(u8"alpha").text == u8"[배포] 확인한 뒤 실행 값이 바뀌어 실행하지 않습니다");
        REQUIRE(fixture.last_log(u8"alpha").severity == gitman::diagnostic_severity::warning);
    }

    SECTION("an environment value changes under the trust dialog")
    {
        // 표시용 명령줄에는 env 이름만 남아 같아 보여도 실제 argv가 다르면 중단한다.
        fixture.load_settings();
        gitman::function_button deploy { make_button(u8"deploy", u8"배포") };
        deploy.args = { u8"${env:TARGET}" };
        fixture.open({ make_project(u8"alpha") }, make_definition({ deploy }));
        fixture.environment[u8"TARGET"] = u8"staging";
        fixture.run(u8"alpha", u8"deploy");
        REQUIRE(fixture.dialog().has_value());

        fixture.environment[u8"TARGET"] = u8"production";
        fixture.confirm();

        REQUIRE(fixture.functions.requests.empty());
        REQUIRE(fixture.last_log(u8"alpha").text == u8"[배포] 확인한 뒤 실행 값이 바뀌어 실행하지 않습니다");
        // 사용자가 본 정의는 신뢰로 남는다. 다시 누르면 묻지 않고 지금 값으로 실행한다.
        fixture.run(u8"alpha", u8"deploy");
        REQUIRE_FALSE(fixture.dialog().has_value());
        REQUIRE(fixture.functions.requests.size() == 1u);
        REQUIRE(std::get<gitman::process_request>(fixture.functions.requests.front().command).arguments.front() == u8"production");
    }
}

TEST_CASE("Closing or opening a document drops a pending confirmation", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    fixture.open({ make_project(u8"alpha") }, make_definition({ make_button(u8"build", u8"빌드") }));
    fixture.run(u8"alpha", u8"build");
    REQUIRE(fixture.dialog().has_value());

    SECTION("close")
    {
        fixture.controller.handle(gitman::close_document_intent {});
    }

    SECTION("open another")
    {
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
    }

    REQUIRE_FALSE(fixture.dialog().has_value());
    // 늦은 확인은 아무것도 하지 않는다. 새 문서 경로에 이전 정의의 신뢰를 남기지 않는다.
    fixture.confirm();
    REQUIRE(fixture.functions.requests.empty());
    const std::optional<std::vector<gitman::trusted_function_definition>> saved { fixture.saved_trust() };
    REQUIRE((saved.has_value() == false || saved->empty()));
}

TEST_CASE("The trust dialog list scrolls within the logic limit", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    // 창이 낮으면 목록이 짧아 스크롤할 것이 생긴다.
    fixture.controller.handle(gitman::window_metrics_intent { 800.0f, 400.0f, 1.0f });
    fixture.load_settings();
    std::vector<gitman::function_button> buttons {};
    for (char8_t index = u8'a'; index <= u8'h'; ++index)
        buttons.push_back(make_button(std::u8string { u8"tool-" } + index, std::u8string { u8"도구 " } + index));
    fixture.open({ make_project(u8"alpha") }, make_definition(std::move(buttons)));
    fixture.run(u8"alpha", u8"tool-a");
    REQUIRE(fixture.dialog()->items.size() == 8u);

    fixture.controller.handle(gitman::function_confirm_scroll_intent { -50.0f });
    REQUIRE(fixture.dialog()->scroll_offset == 0.0f);

    fixture.controller.handle(gitman::function_confirm_scroll_intent { 100000.0f });
    const gitman::function_confirm_dialog_view dialog { *fixture.dialog() };
    float content { 0.0f };
    for (std::size_t index = 0; index < dialog.items.size(); ++index)
    {
        if (index > 0)
            content += gitman::layout_function_confirm_item_gap;
        content += gitman::layout_function_confirm_item_height(dialog.items[index].lines.size());
    }
    const float viewport { gitman::function_confirm_list_height(400.0f, dialog.description.size() + 1, dialog.note.size()) };
    REQUIRE(viewport < content);
    REQUIRE(dialog.scroll_offset == content - viewport);
}

TEST_CASE("The confirm dialog tree wires its buttons, scrim and wheel", "[function_buttons][t7]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    fixture.open({ make_project(u8"alpha") }, make_definition({ make_button(u8"build", u8"빌드"), make_button(u8"test", u8"테스트") }));
    fixture.run(u8"alpha", u8"build");

    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    REQUIRE(find(*tree, gitman::ui::kind_function_confirm_dialog) != nullptr);
    const luil::ui_element* const host { find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_function_confirm) };
    REQUIRE(host != nullptr);
    REQUIRE(host->accessibility().name == u8"기능 실행 확인");
    REQUIRE(tree->focus_trap() != nullptr);

    // 항목은 읽기 전용이고 화면 읽기는 라벨, 실행 방식, 줄을 읽는다.
    const luil::ui_element* const item { tree->find(gitman::ui::function_confirm_dialog_item_id(0)) };
    REQUIRE(item != nullptr);
    REQUIRE(item->tab_stop() == false);
    REQUIRE(item->accessibility().role == luil::access_role::list_item);
    REQUIRE(item->accessibility().name.starts_with(u8"빌드, 출력을 로그로 받기, C:\\Tools\\build.exe"));
    REQUIRE(tree->find(gitman::ui::function_confirm_dialog_item_id(1)) != nullptr);

    // 대상·안내·경고 줄은 panel의 화면 읽기 이름으로 닿는다.
    const luil::ui_element* const panel { find(*tree, gitman::ui::kind_function_confirm_dialog_panel) };
    REQUIRE(panel != nullptr);
    REQUIRE(panel->accessibility().role == luil::access_role::group);
    REQUIRE(contains(panel->accessibility().name, document_path));
    REQUIRE(contains(panel->accessibility().name, u8"신뢰는 스크립트 파일의 내용을 확인하지 않습니다."));

    // 확인은 확인 intent, 취소·배경 클릭은 취소 intent다.
    REQUIRE(as_intent<gitman::confirm_function_run_intent>(click_action(*tree, id_of(gitman::ui::kind_function_confirm_dialog_confirm))) != nullptr);
    REQUIRE(as_intent<gitman::cancel_function_run_intent>(click_action(*tree, id_of(gitman::ui::kind_function_confirm_dialog_cancel))) != nullptr);
    REQUIRE(as_intent<gitman::cancel_function_run_intent>(click_action(*tree, id_of(luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_function_confirm))) != nullptr);

    // 휠은 그 목록을 스크롤한다.
    gitman::ui::gitman_policy policy {};
    luil::mouse_wheel_event wheel {};
    wheel.x = 640.0f;
    wheel.y = 360.0f;
    const std::vector<luil::input_action> actions { policy.on_wheel(*tree, wheel, 24.0f) };
    const auto* const scroll { as_intent<gitman::function_confirm_scroll_intent>(actions) };
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->delta == 24.0f);
}

namespace {
    constexpr std::u8string_view system_cmd { u8"C:\\Windows\\System32\\cmd.exe" };

    // 항목의 줄을 나누기 전 줄로 되이어 한 글로 잇는다. 폭에 맞춰 나눈 줄(`continuation`)은 앞 줄에 붙인다.
    std::u8string unwrapped_lines(const gitman::function_confirm_item_view& item)
    {
        std::u8string text {};
        for (std::size_t index = 0; index < item.lines.size(); ++index)
        {
            if (index > 0 && item.lines[index].continuation == false)
                text += u8"\n";
            text += item.lines[index].text;
        }
        if (item.lines.empty() == false)
            text += u8"\n";
        return text;
    }

    // 글자 크기 표를 넣고 test가 끝나면 비운다 (`function_bar_tree_tests.cpp`와 같다).
    class text_size_scope
    {
    public:
        explicit text_size_scope(const float factor)
        {
            std::vector<float> sizes {};
            for (std::size_t size = 0; size <= 64; ++size)
                sizes.push_back(static_cast<float>(size) * factor);
            applied_ = luil::set_system_font_sizes(std::move(sizes));
        }

        ~text_size_scope()
        {
            static_cast<void>(luil::set_system_font_sizes({}));
        }

        text_size_scope(const text_size_scope&) = delete;
        text_size_scope& operator=(const text_size_scope&) = delete;

        [[nodiscard]] bool applied() const noexcept
        {
            return applied_;
        }

    private:
        bool applied_ { false };
    };

    // 저장소 `alpha` 하나와 신뢰를 기록한 정의를 연다 (보정 확인만 보려는 test다).
    void open_trusted(trust_fixture& fixture, std::vector<gitman::function_button> buttons, const std::u8string_view root = u8"C:\\work\\")
    {
        const gitman::function_buttons_definition definition { make_definition(std::move(buttons)) };
        const gitman::project_definition projects[] { make_project(u8"alpha", root) };
        gitman::app_settings settings {};
        settings.trusted_function_definitions.push_back({ std::u8string { document_path }, gitman::function_trust_digest(definition, projects) });
        fixture.load_settings(settings);
        fixture.open({ make_project(u8"alpha", root) }, definition);
    }

    // 배치를 부르는 cmd 기능이다. 브랜치를 인자로 넘기므로 `&`가 든 브랜치는 보정과 경고를 함께 낸다.
    gitman::function_button cmd_batch(const std::u8string_view id = u8"batch", const std::u8string_view label = u8"배치")
    {
        gitman::function_button button { make_button(id, label, system_cmd) };
        button.args = { u8"/d", u8"/c", u8"${projectRoot}\\tools\\run.bat", u8"${branch}" };
        return button;
    }

    const gitman::process_request& submitted(const trust_fixture& fixture, const std::size_t index)
    {
        REQUIRE(fixture.functions.requests.size() > index);
        return std::get<gitman::process_request>(fixture.functions.requests[index].command);
    }

    bool has_warning_line(const gitman::function_confirm_item_view& item, const std::u8string_view part)
    {
        for (const gitman::function_confirm_line& line : item.lines)
        {
            if (line.warning && contains(line.text, part))
                return true;
        }
        return false;
    }
} // namespace

TEST_CASE("A cmd command gitman corrected asks for a final approval and runs only after it", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    open_trusted(fixture, { cmd_batch() });
    fixture.query(u8"alpha", u8"main&whoami");

    fixture.run(u8"alpha", u8"batch");

    REQUIRE(fixture.functions.requests.empty());
    std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::correction);
    REQUIRE(dialog->subject == u8"alpha");
    REQUIRE(dialog->confirm_label == u8"실행");
    REQUIRE(dialog->items.size() == 1u);
    // 원래 명령, 보정한 실행 명령, 바뀐 자리와 이유, 경고를 보인다.
    const gitman::function_confirm_item_view& item { dialog->items.front() };
    const std::u8string text { unwrapped_lines(item) };
    REQUIRE(contains(text, u8"원래 명령 \"C:\\Windows\\System32\\cmd.exe\" /d /c C:\\work\\alpha\\tools\\run.bat main&whoami\n"));
    REQUIRE(contains(text, u8"실행할 명령 \"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"C:\\work\\alpha\\tools\\run.bat \"main&whoami\"\"\n"));
    REQUIRE(contains(text, u8"보정 args[3]: main&whoami → \"main&whoami\"\n"));
    REQUIRE(contains(text, u8"작업 폴더 C:\\work\\alpha\n"));
    REQUIRE(has_warning_line(item, u8"GITMAN_BRANCH"));
    // 거부가 아니므로 실패 점과 거부 줄이 없다.
    REQUIRE_FALSE(fixture.controller.function_state(gitman::project_id { u8"alpha" }, u8"batch")->failed);

    // 취소하면 실행하지 않고 기억하지 않는다.
    fixture.controller.handle(gitman::cancel_function_run_intent {});
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.empty());
    fixture.run(u8"alpha", u8"batch");
    REQUIRE(fixture.dialog().has_value());

    // 승인하면 보정한 명령줄 그대로 실행한다. whoami는 따옴표 안이라 명령이 아니다.
    fixture.confirm();
    REQUIRE_FALSE(fixture.dialog().has_value());
    const gitman::process_request& request { submitted(fixture, 0) };
    REQUIRE(request.syntax == gitman::command_line_syntax::cmd);
    REQUIRE(request.command_line == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"C:\\work\\alpha\\tools\\run.bat \"main&whoami\"\"");
    REQUIRE(request.arguments.back() == u8"main&whoami");
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[배치] 실행: \"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"C:\\work\\alpha\\tools\\run.bat \"main&whoami\"\"");
}

TEST_CASE("An approved cmd correction is not asked again until the command, the warnings or the document change", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button env_only { make_button(u8"env", u8"환경", system_cmd) };
    env_only.args = { u8"/d", u8"/c", u8"${projectRoot}\\tools\\env.bat" };
    open_trusted(fixture, { cmd_batch(), env_only });
    fixture.query(u8"alpha", u8"main&whoami");

    fixture.run(u8"alpha", u8"batch");
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.completed_all();

    // 같은 버튼, 같은 최종 명령줄, 같은 경고면 이 세션에서 다시 묻지 않는다.
    fixture.run(u8"alpha", u8"batch");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 2u);
    fixture.completed_all();

    // 다른 버튼은 따로 묻는다. 인자에 브랜치가 없어도 배치가 읽을 GITMAN_BRANCH 경고가 있다.
    fixture.run(u8"alpha", u8"env");
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->mode == gitman::function_confirm_mode::correction);
    REQUIRE(fixture.dialog()->items.front().lines.size() >= 3u);
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 3u);
    fixture.completed_all();

    // 값이 바뀌면 최종 명령줄이 달라져 다시 묻는다.
    fixture.query(u8"alpha", u8"main&calc");
    fixture.run(u8"alpha", u8"batch");
    REQUIRE(fixture.dialog().has_value());
    fixture.controller.handle(gitman::cancel_function_run_intent {});

    // 명령줄이 같아도 경고가 바뀌면 다시 묻는다 (`env`는 인자에 브랜치를 쓰지 않는다).
    fixture.run(u8"alpha", u8"env");
    REQUIRE(fixture.dialog().has_value());
    fixture.controller.handle(gitman::cancel_function_run_intent {});

    // 메타 문자가 없어지면 보정도 경고도 없어 묻지 않는다.
    fixture.query(u8"alpha", u8"main");
    fixture.run(u8"alpha", u8"batch");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 4u);
    fixture.completed_all();

    // 처음 승인한 값으로 돌아오면 그 승인이 아직 맞다. 문서를 다시 열면 승인 기억을 지운다.
    fixture.query(u8"alpha", u8"main&whoami");
    fixture.run(u8"alpha", u8"batch");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 5u);
    fixture.completed_all();
    const std::size_t before_reopen { fixture.functions.requests.size() };
    fixture.open({ make_project(u8"alpha") }, make_definition({ cmd_batch(), env_only }));
    fixture.query(u8"alpha", u8"main&whoami");
    fixture.run(u8"alpha", u8"batch");
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == before_reopen);
}

TEST_CASE("A cmd command without corrections or warnings runs without asking", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button plain { make_button(u8"plain", u8"보통", system_cmd) };
    // 공백 경로를 감싸는 것은 보통 조립이라 묻지 않는다.
    plain.args = { u8"/d", u8"/c", u8"${projectRoot}\\tools\\run.bat", u8"${branch}", u8"--config=Release" };
    open_trusted(fixture, { plain }, u8"C:\\OneDrive - 회사명\\");
    fixture.query(u8"alpha", u8"feature/login");

    fixture.run(u8"alpha", u8"plain");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(submitted(fixture, 0).command_line == u8"\"C:\\Windows\\System32\\cmd.exe\" /d /s /c \"\"C:\\OneDrive - 회사명\\alpha\\tools\\run.bat\" feature/login \"--config=Release\"\"");
}

TEST_CASE("The final cmd approval joins the trust dialog and the confirm preview in one dialog", "[function_buttons][r4]")
{
    trust_fixture fixture {};

    SECTION("an untrusted definition")
    {
        fixture.load_settings();
        fixture.open({ make_project(u8"alpha") }, make_definition({ cmd_batch(), make_button(u8"build", u8"빌드") }));
        fixture.query(u8"alpha", u8"main&whoami");
        fixture.finish_settings_save(true);

        fixture.run(u8"alpha", u8"batch");

        const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
        REQUIRE(dialog.has_value());
        REQUIRE(dialog->mode == gitman::function_confirm_mode::trust);
        REQUIRE(dialog->confirm_label == u8"신뢰하고 실행");
        const std::u8string text { unwrapped_lines(dialog->items.front()) };
        REQUIRE(contains(text, u8"실행할 명령 \"C:\\Windows\\System32\\cmd.exe\" /d /s /c "));
        REQUIRE(contains(text, u8"보정 args[3]: main&whoami → \"main&whoami\"\n"));
        REQUIRE(has_warning_line(dialog->items.front(), u8"GITMAN_BRANCH"));
        REQUIRE_FALSE(contains(unwrapped_lines(dialog->items[1]), u8"보정"));

        // 확인 한 번이 신뢰와 보정 승인을 함께 끝낸다. 다음 실행은 묻지 않는다.
        fixture.confirm();
        fixture.confirm();
        REQUIRE(fixture.functions.requests.size() == 1u);
        REQUIRE(fixture.saved_trust().has_value());
        fixture.completed_all();
        fixture.run(u8"alpha", u8"batch");
        REQUIRE_FALSE(fixture.dialog().has_value());
        REQUIRE(fixture.functions.requests.size() == 2u);
    }

    SECTION("a confirm button")
    {
        gitman::function_button batch { cmd_batch() };
        batch.confirm = true;
        open_trusted(fixture, { batch });
        fixture.query(u8"alpha", u8"main&whoami");

        fixture.run(u8"alpha", u8"batch");

        const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
        REQUIRE(dialog.has_value());
        REQUIRE(dialog->mode == gitman::function_confirm_mode::preview);
        const std::u8string text { unwrapped_lines(dialog->items.front()) };
        REQUIRE(contains(text, u8"원래 명령 "));
        REQUIRE(contains(text, u8"보정 args[3]: main&whoami → \"main&whoami\"\n"));
        fixture.confirm();
        REQUIRE(fixture.functions.requests.size() == 1u);

        // `confirm`은 매번 미리보기를 거친다 (3.10의 "별개"). 보정 줄도 함께 보인다.
        fixture.completed_all();
        fixture.run(u8"alpha", u8"batch");
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->mode == gitman::function_confirm_mode::preview);
        REQUIRE(contains(unwrapped_lines(fixture.dialog()->items.front()), u8"보정 args[3]"));
    }
}

TEST_CASE("A value changed under the cmd approval aborts the run", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button env_only { make_button(u8"env", u8"환경", system_cmd) };
    env_only.args = { u8"/d", u8"/c", u8"${projectRoot}\\tools\\env.bat" };
    open_trusted(fixture, { env_only });
    fixture.query(u8"alpha", u8"main&whoami");

    fixture.run(u8"alpha", u8"env");
    REQUIRE(fixture.dialog().has_value());
    // 인자는 같고 배치가 읽을 브랜치만 바뀌었다. 승인한 경고가 아니므로 실행하지 않는다.
    fixture.query(u8"alpha", u8"main|more");
    fixture.confirm();

    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[환경] 확인한 뒤 실행 값이 바뀌어 실행하지 않습니다");
}

TEST_CASE("A cmd value with a line break is still refused with a failure mark", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    open_trusted(fixture, { cmd_batch() });
    fixture.query(u8"alpha", u8"main\nwhoami");

    fixture.run(u8"alpha", u8"batch");

    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[배치] 실행하지 않습니다: cmd.exe에 넘기는 ${branch} 값에 cmd 명령줄로 넘길 수 없는 문자(줄바꿈)가 있습니다.");
    REQUIRE(fixture.controller.function_state(gitman::project_id { u8"alpha" }, u8"batch")->failed);
}

TEST_CASE("Alt+N and the function menu reach the same cmd approval", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button batch { cmd_batch() };
    batch.placement = u8"card";
    open_trusted(fixture, { batch });
    fixture.query(u8"alpha", u8"main&whoami");

    SECTION("Alt+1")
    {
        fixture.controller.handle(gitman::run_function_shortcut_intent { gitman::project_id { u8"alpha" }, 1 });
    }

    SECTION("function menu")
    {
        fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"alpha" }, 10.0f, 20.0f });
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::run_function, u8"batch" });
    }

    // 두 길 모두 배치 파일 확인(S3)을 거친다.
    REQUIRE(fixture.answer_cmd_inspection());
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->mode == gitman::function_confirm_mode::correction);
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("The cmd approval dialog tree names its parts and wraps at 480 wide with large text", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button batch { cmd_batch() };
    batch.args.push_back(u8"${projectRoot}\\a very long folder name that keeps going\\and going (x86) & more\\file.txt");
    open_trusted(fixture, { batch });
    fixture.query(u8"alpha", u8"main&whoami");
    fixture.run(u8"alpha", u8"batch");

    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const host { find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_function_confirm) };
    REQUIRE(host != nullptr);
    REQUIRE(host->accessibility().name == u8"기능 실행 확인");
    const luil::ui_element* const panel { find(*tree, gitman::ui::kind_function_confirm_dialog_panel) };
    REQUIRE(panel != nullptr);
    REQUIRE(panel->accessibility().name.starts_with(u8"보정 확인, alpha"));
    // 보정 줄과 경고 줄은 항목의 화면 읽기 이름으로 닿는다.
    const luil::ui_element* const item { tree->find(gitman::ui::function_confirm_dialog_item_id(0)) };
    REQUIRE(item != nullptr);
    REQUIRE(contains(item->accessibility().name, u8"실행할 명령 "));
    REQUIRE(contains(item->accessibility().name, u8"보정 args[3]: main&whoami"));
    REQUIRE(contains(item->accessibility().name, u8"경고 GITMAN_BRANCH"));
    REQUIRE(as_intent<gitman::confirm_function_run_intent>(click_action(*tree, id_of(gitman::ui::kind_function_confirm_dialog_confirm))) != nullptr);

    // 480 폭, 글자 150%에서 긴 줄을 나눠 말줄임으로 숨기지 않는다. 경고 줄은 나눠도 경고다.
    const text_size_scope large { 1.5f };
    REQUIRE(large.applied());
    fixture.controller.handle(gitman::window_metrics_intent { 480.0f, 320.0f, 1.0f });
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    const std::size_t columns { gitman::function_confirm_code_columns(480.0f) };
    bool continued { false };
    bool continued_warning { false };
    for (const gitman::function_confirm_line& line : dialog->items.front().lines)
    {
        // 표시 칸 수다. 한글은 두 칸으로 센다(`function_confirm_code_columns`).
        std::size_t width { 0 };
        for (std::size_t index = 0; index < line.text.size(); ++index)
        {
            const char8_t byte { line.text[index] };
            if ((byte & 0xC0u) == 0x80u)
                continue;
            width += byte < 0x80u ? 1u : 2u;
        }
        REQUIRE(width <= columns);
        continued = continued || line.continuation;
        continued_warning = continued_warning || (line.continuation && line.warning);
    }
    REQUIRE(continued);
    REQUIRE(continued_warning);
    REQUIRE(dialog->description.size() <= 3u);
}

TEST_CASE("A cmd approval with only warnings does not say gitman corrected the command", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button env_only { make_button(u8"env", u8"환경", system_cmd) };
    env_only.args = { u8"/d", u8"/c", u8"${projectRoot}\\tools\\env.bat" };
    open_trusted(fixture, { env_only, cmd_batch() });
    fixture.query(u8"alpha", u8"main&whoami");

    // 인자에 고친 것이 없고 배치가 읽을 GITMAN_BRANCH 경고만 있다 (R4 검토).
    fixture.run(u8"alpha", u8"env");
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->description.size() == 3u);
    REQUIRE(fixture.dialog()->description.front() == u8"cmd.exe 명령에 확인할 경고가 있습니다.");
    REQUIRE_FALSE(contains(unwrapped_lines(fixture.dialog()->items.front()), u8"보정 "));
    fixture.controller.handle(gitman::cancel_function_run_intent {});

    fixture.run(u8"alpha", u8"batch");
    REQUIRE(fixture.dialog()->description.front() == u8"gitman이 cmd.exe 명령을 보정했습니다.");
}

TEST_CASE("A command passed only as a sentence says so instead of showing the same text twice", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button echo { make_button(u8"echo", u8"인사", system_cmd) };
    echo.args = { u8"/c", u8"echo hi" };
    open_trusted(fixture, { echo });
    fixture.query(u8"alpha", u8"main");

    fixture.run(u8"alpha", u8"echo");
    REQUIRE(fixture.dialog().has_value());
    const std::u8string text { unwrapped_lines(fixture.dialog()->items.front()) };
    REQUIRE(contains(text, u8"보정 args[1]: echo hi (명령 문장으로 넘김)\n"));
    REQUIRE_FALSE(contains(text, u8"→"));
}

TEST_CASE("An approved cmd correction is asked again when the working folder changes or the document is reloaded", "[function_buttons][r4]")
{
    trust_fixture fixture {};
    gitman::function_button relative { make_button(u8"relative", u8"상대", system_cmd) };
    // 상대 경로 배치는 작업 폴더에 따라 다른 파일이다. 명령줄과 경고가 같아도 작업 폴더가 바뀌면 다시 묻는다 (R4 검토).
    relative.args = { u8"/d", u8"/c", u8"run.bat", u8"${branch}" };
    relative.cwd = u8"${env:GITMAN_TEST_CWD}";
    fixture.environment[u8"GITMAN_TEST_CWD"] = u8"C:\\work\\one";
    const gitman::function_buttons_definition definition { make_definition({ relative }) };
    open_trusted(fixture, { relative });
    fixture.query(u8"alpha", u8"main&whoami");

    fixture.run(u8"alpha", u8"relative");
    REQUIRE(fixture.dialog().has_value());
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(submitted(fixture, 0).working_directory == u8"C:\\work\\one");
    fixture.completed_all();
    fixture.run(u8"alpha", u8"relative");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 2u);
    fixture.completed_all();

    fixture.environment[u8"GITMAN_TEST_CWD"] = u8"C:\\work\\two";
    fixture.run(u8"alpha", u8"relative");
    REQUIRE(fixture.dialog().has_value());
    // env 값은 표시용 글에서 이름이다 (3.6). 보이는 글이 같아도 실제 폴더가 바뀌었으므로 묻는다.
    REQUIRE(contains(unwrapped_lines(fixture.dialog()->items.front()), u8"작업 폴더 ${env:GITMAN_TEST_CWD}\n"));
    fixture.controller.handle(gitman::cancel_function_run_intent {});

    // 처음 폴더로 돌아오면 그 승인이 맞다. 다시 읽기에 성공해 문서를 바꾸면 지운다.
    fixture.environment[u8"GITMAN_TEST_CWD"] = u8"C:\\work\\one";
    fixture.run(u8"alpha", u8"relative");
    REQUIRE_FALSE(fixture.dialog().has_value());
    REQUIRE(fixture.functions.requests.size() == 3u);
    fixture.completed_all();

    fixture.controller.handle(gitman::reload_document_intent {});
    const gitman::operation_request* const reload { fixture.submitter.last_of(gitman::operation_kind::load_document) };
    REQUIRE(reload != nullptr);
    gitman::document_loaded_event loaded {};
    loaded.operation_id = reload->operation_id;
    gitman::workspace_document document {};
    document.document_path = document_path;
    document.projects = { make_project(u8"alpha") };
    document.function_buttons = definition;
    loaded.document = { std::move(document) };
    fixture.controller.handle(std::move(loaded));
    fixture.query(u8"alpha", u8"main&whoami");
    fixture.run(u8"alpha", u8"relative");
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->mode == gitman::function_confirm_mode::correction);
    REQUIRE(fixture.functions.requests.size() == 3u);
}

TEST_CASE("A cmd batch is read on a worker before the run and its findings join the final approval", "[function_buttons][s3]")
{
    trust_fixture fixture {};
    open_trusted(fixture, { cmd_batch() });
    fixture.query(u8"alpha", u8"main");

    // 배치 파일을 읽는 것은 파일 I/O라 logic이 하지 않고 worker에 맡긴다 (ADR-004). 답이 올 때까지 실행하지 않는다.
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.dialog().has_value() == false);
    const gitman::operation_request* const request { fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target) };
    REQUIRE(request != nullptr);
    REQUIRE(request->project.id.value == u8"alpha");
    REQUIRE(request->cmd_target.has_value());
    REQUIRE(request->cmd_target->working_directory == u8"C:\\work\\alpha");
    REQUIRE(request->cmd_target->argument == 2u);
    REQUIRE(request->cmd_target->batch_if_path == u8"C:\\work\\alpha\\tools\\run.bat");
    const std::uint64_t first { request->operation_id };

    // 기다리는 동안 다시 눌러도 확인을 또 내지 않는다.
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    std::size_t inspections { 0 };
    for (const gitman::operation_request& value : fixture.submitter.requests)
    {
        if (value.kind == gitman::operation_kind::inspect_cmd_target)
            ++inspections;
    }
    REQUIRE(inspections == 1u);
    REQUIRE(first == fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->operation_id);

    // 배치가 지연 확장을 켜고 call을 쓰면 보정이 없어도 확인 창에 경고로 보인다.
    REQUIRE(fixture.answer_cmd_inspection(gitman::cmd_batch_findings { true, true, false }));
    std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::correction);
    REQUIRE(has_warning_line(dialog->items.front(), u8"EnableDelayedExpansion"));
    REQUIRE(has_warning_line(dialog->items.front(), u8"call을 씁니다"));
    REQUIRE(dialog->description.front() == u8"cmd.exe 명령에 확인할 경고가 있습니다.");

    // 확인은 그때 읽은 결과로 다시 짓는다. 다시 읽지 않는다.
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->operation_id == first);
    fixture.completed_all();

    // 다음 클릭은 파일을 다시 읽는다. 같은 경고면 승인 기억이 맞아 묻지 않는다.
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    REQUIRE(fixture.answer_cmd_inspection(gitman::cmd_batch_findings { true, true, false }));
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.functions.requests.size() == 2u);
    fixture.completed_all();

    // 배치가 바뀌어 경고가 없어지면 묻지 않고, 늦게 온 앞 결과는 버린다.
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    gitman::cmd_target_inspected_event stale {};
    stale.operation_id = first;
    stale.facts.query = *fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->cmd_target;
    stale.facts.batch = gitman::cmd_batch_findings { true, false, false };
    fixture.controller.handle(std::move(stale));
    REQUIRE(fixture.functions.requests.size() == 2u);
    REQUIRE(fixture.answer_cmd_inspection(gitman::cmd_batch_findings {}));
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.functions.requests.size() == 3u);
}

TEST_CASE("A cmd run waiting for its batch check is dropped when the document changes", "[function_buttons][s3]")
{
    trust_fixture fixture {};
    open_trusted(fixture, { cmd_batch() });
    fixture.query(u8"alpha", u8"main");
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    const std::uint64_t waiting { fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->operation_id };

    // 같은 문서를 다시 열었다. 앞 문서에서 누른 실행의 답이 와도 실행하지 않는다.
    fixture.open({ make_project(u8"alpha") }, make_definition({ cmd_batch() }));
    fixture.query(u8"alpha", u8"main");
    gitman::cmd_target_inspected_event late {};
    late.operation_id = waiting;
    late.facts.query.working_directory = u8"C:\\work\\alpha";
    fixture.controller.handle(std::move(late));
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.dialog().has_value() == false);
}

TEST_CASE("An untrusted definition does not read a batch outside local drives before the trust", "[function_buttons][s3]")
{
    // 신뢰하지 않은 문서의 작업 폴더가 UNC다. 누르기만으로 그 share를 읽지 않는다 (S3 검토). 읽지 않았다는 경고가 신뢰
    // dialog에 실리고, 신뢰 확인은 같은 답으로 다시 지어 실행한다.
    trust_fixture fixture {};
    fixture.load_settings();
    fixture.open({ make_project(u8"alpha", u8"\\\\host\\share\\") }, make_definition({ cmd_batch() }));
    fixture.query(u8"alpha", u8"main");
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    REQUIRE(fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target) == nullptr);
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::trust);
    REQUIRE(has_warning_line(dialog->items.front(), u8"확인하지 못했습니다"));
    fixture.confirm();
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.completed_all();

    // 신뢰한 뒤에는 그 경로도 worker가 읽는다.
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    const gitman::operation_request* const request { fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target) };
    REQUIRE(request != nullptr);
    REQUIRE(request->cmd_target->batch_if_path == u8"\\\\host\\share\\alpha\\tools\\run.bat");
}

TEST_CASE("An untrusted definition still reads a batch on a local drive before the trust dialog", "[function_buttons][s3]")
{
    trust_fixture fixture {};
    fixture.load_settings();
    fixture.open({ make_project(u8"alpha") }, make_definition({ cmd_batch() }));
    fixture.query(u8"alpha", u8"main");
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.answer_cmd_inspection(gitman::cmd_batch_findings { true, false, false }));
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::trust);
    REQUIRE(has_warning_line(dialog->items.front(), u8"EnableDelayedExpansion"));
}

TEST_CASE("A cmd run whose batch check does not answer is dropped after a time limit", "[function_buttons][s3]")
{
    trust_fixture fixture {};
    open_trusted(fixture, { cmd_batch() });
    fixture.query(u8"alpha", u8"main");
    REQUIRE(fixture.controller.next_tick().has_value() == false);
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    const std::uint64_t first { fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->operation_id };

    // 답이 오지 않으면 driver가 깨울 시각이 있다. 그 전에는 그대로 기다린다.
    const std::optional<std::chrono::steady_clock::time_point> deadline { fixture.controller.next_tick() };
    REQUIRE(deadline.has_value());
    fixture.controller.tick(*deadline - std::chrono::milliseconds { 1 });
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    REQUIRE(fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->operation_id == first);

    // 상한이 지나면 실행하지 않고 그렇다고 남긴 뒤 다른 실행을 받는다.
    fixture.controller.tick(*deadline);
    REQUIRE(fixture.last_log(u8"alpha").text == u8"[배치] 배치 파일을 확인하지 못해 실행하지 않습니다");
    REQUIRE(fixture.controller.next_tick().has_value() == false);
    fixture.controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"batch" });
    const std::uint64_t second { fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->operation_id };
    REQUIRE(second != first);

    // 늦게 온 앞 답은 버리고, 새 답으로 실행한다.
    gitman::cmd_target_inspected_event late {};
    late.operation_id = first;
    late.facts.query = *fixture.submitter.last_of(gitman::operation_kind::inspect_cmd_target)->cmd_target;
    fixture.controller.handle(std::move(late));
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(fixture.answer_cmd_inspection());
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("Assistive technology sets the confirm dialog scrollbar", "[function_buttons][s4][ui]")
{
    trust_fixture fixture {};
    fixture.controller.handle(gitman::window_metrics_intent { 800.0f, 400.0f, 1.0f });
    fixture.load_settings();
    std::vector<gitman::function_button> buttons {};
    for (char8_t index = u8'a'; index <= u8'h'; ++index)
        buttons.push_back(make_button(std::u8string { u8"tool-" } + index, std::u8string { u8"도구 " } + index));
    fixture.open({ make_project(u8"alpha") }, make_definition(std::move(buttons)));
    fixture.run(u8"alpha", u8"tool-a");

    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    const luil::ui_element* const bar { find(*tree, gitman::ui::kind_scrollbar, gitman::ui::scrollbar_owner_function_confirm_dialog) };
    REQUIRE(bar != nullptr);
    REQUIRE(bar->visible());
    const float maximum { bar->accessibility().range->maximum };
    const std::vector<luil::input_action> actions { gitman::testing::set_scroll_value(*bar, 30.0f) };
    const auto* const intent { as_intent<gitman::function_confirm_scroll_intent>(actions) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->offset == std::optional<float> { 30.0f });

    fixture.controller.handle(*intent);
    fixture.controller.handle(*intent);
    REQUIRE(fixture.dialog()->scroll_offset == 30.0f);
    // 절대 자리는 키 단위(`step`)보다 먼저다.
    fixture.controller.handle(gitman::function_confirm_scroll_intent { 0.0f, gitman::function_confirm_scroll_step::start, 100000.0f });
    REQUIRE(fixture.dialog()->scroll_offset == maximum);
}
