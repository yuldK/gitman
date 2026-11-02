#include "helpers/discovery_test_doubles.h"
#include "helpers/function_card_fixture.h"

#include "application/project_path_resolver.h"
#include "domain/function_editor.h"
#include "domain/function_trust.h"
#include "infrastructure/json_project_store.h"
#include "presentation/function_editor_layout.h"
#include "platform/win32/workspace_document_file_system.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 기능 편집 dialog(function-buttons-design 2.7, 4.11, 6.1 T12)의 domain 함수와 logic이다. intent를 직접 넣고 편집 dialog view,
// 문서 모델, 저장 요청, 앱 설정 저장 요청을 본다. 실제 store로 저장한 해시 입력도 본다.
using namespace gitman::testing;

namespace {
    using gitman::function_argument_split;
    using gitman::function_editor_field;
    using gitman::function_editor_list_edit;

    [[nodiscard]] function_argument_split split(const std::u8string_view line)
    {
        return gitman::split_function_arguments(line);
    }

    [[nodiscard]] std::vector<std::u8string> arguments(std::initializer_list<std::u8string_view> values)
    {
        std::vector<std::u8string> result {};
        for (const std::u8string_view value : values)
            result.emplace_back(value);
        return result;
    }

    // 진행 중인 앱 설정 저장을 성공으로 끝낸다.
    void finish_settings_save(const function_card_submitter& submitter, gitman::logic_controller& controller)
    {
        for (auto found = submitter.requests.rbegin(); found != submitter.requests.rend(); ++found)
        {
            if (found->kind != gitman::operation_kind::save_app_settings)
                continue;
            gitman::app_settings_saved_event saved {};
            saved.operation_id = found->operation_id;
            saved.succeeded = true;
            controller.handle(std::move(saved));
            return;
        }
    }

    // 편집 dialog를 쓰는 logic fixture다. `trusted`면 앱 설정에 지금 정의의 해시를 실어 신뢰한 정의로 시작한다.
    struct editor_fixture
    {
        function_card_submitter submitter {};
        function_card_run_submitter functions {};
        gitman::cancellation_registry registry {};
        gitman::logic_controller controller { submitter, registry };

        editor_fixture(std::vector<gitman::project_definition> projects, std::vector<gitman::function_button_entry> entries, const bool trusted = true
            , std::vector<gitman::diagnostic> diagnostics = {}, const float width = 1280.0f, const float height = 720.0f)
        {
            gitman::function_buttons_definition definition {};
            definition.buttons = std::move(entries);
            definition.source_digest_input = u8"{\"before\":1}";

            controller.bind_function_execution(functions
                , gitman::make_function_run_services(
                    [](const std::u8string_view) -> std::optional<std::u8string>
                    {
                        return std::nullopt;
                    }
                )
            );
            controller.handle(gitman::window_metrics_intent { width, height, 1.0f });
            controller.start();
            gitman::app_settings_loaded_event settings {};
            settings.operation_id = submitter.requests.back().operation_id;
            if (trusted)
                settings.settings.trusted_function_definitions.push_back({ std::u8string { document_path }, gitman::function_trust_digest(definition, projects) });
            controller.handle(std::move(settings));

            controller.handle(gitman::open_document_intent { std::u8string { document_path } });
            gitman::document_loaded_event event {};
            gitman::workspace_document document {};
            document.document_path = document_path;
            document.projects = std::move(projects);
            document.function_buttons = std::move(definition);
            event.document = { std::move(document) };
            event.diagnostics = std::move(diagnostics);
            controller.handle(std::move(event));
            // 문서를 열면 최근 목록 저장이 나간다. 끝내 두어야 신뢰 갱신의 앱 설정 저장이 곧바로 나간다.
            finish_settings_save(submitter, controller);
            submitter.requests.clear();
        }

        void open(const std::u8string_view button = {})
        {
            controller.handle(gitman::open_function_editor_intent { std::u8string { button } });
        }

        [[nodiscard]] gitman::function_editor_view editor() const
        {
            const auto view { controller.make_view_snapshot() };
            REQUIRE(view->function_editor.has_value());
            return *view->function_editor;
        }

        void select(const std::size_t index)
        {
            controller.handle(gitman::select_function_editor_entry_intent { index });
        }

        void field(const function_editor_field field, const std::u8string_view value = {})
        {
            controller.handle(gitman::set_function_editor_field_intent { field, std::u8string { value } });
        }

        void list(const function_editor_list_edit edit)
        {
            controller.handle(gitman::edit_function_editor_list_intent { edit });
        }

        // 고른 항목의 칸 글을 통째로 바꾼다 (모두 고르고 넣기).
        void type(const gitman::text_input_target target, const std::u8string_view text)
        {
            controller.handle(gitman::edit_text_input_intent { target, gitman::text_edit_command::select_all });
            controller.handle(gitman::edit_text_input_intent { target, gitman::text_edit_command::insert, std::u8string { text } });
        }

        void apply()
        {
            controller.handle(gitman::apply_function_editor_intent {});
        }

        [[nodiscard]] const gitman::operation_request* last_of(const gitman::operation_kind kind) const
        {
            for (auto found = submitter.requests.rbegin(); found != submitter.requests.rend(); ++found)
            {
                if (found->kind == kind)
                    return &*found;
            }
            return nullptr;
        }

        // 마지막 문서 저장 요청의 기능 버튼 절이다.
        [[nodiscard]] const gitman::function_buttons_definition& saved() const
        {
            const gitman::operation_request* const save { last_of(gitman::operation_kind::save_document) };
            REQUIRE(save != nullptr);
            REQUIRE(save->document.has_value());
            REQUIRE(save->document->function_buttons.has_value());
            return *save->document->function_buttons;
        }

        // 마지막 문서 저장을 성공으로 끝낸다. `digest_input`은 store가 쓴 글을 다시 읽어 돌려주는 해시 입력이다.
        void finish_save(const std::optional<std::u8string> digest_input, const bool succeeded = true)
        {
            const gitman::operation_request* const save { last_of(gitman::operation_kind::save_document) };
            REQUIRE(save != nullptr);
            gitman::document_saved_event event {};
            event.operation_id = save->operation_id;
            if (succeeded)
            {
                event.revision = gitman::workspace_revision_token {};
                event.function_digest_input = digest_input;
            }
            else
            {
                gitman::diagnostic value {};
                value.code = gitman::diagnostic_code::document_write_failed;
                value.severity = gitman::diagnostic_severity::error;
                value.message = u8"쓰지 못했습니다.";
                event.diagnostics.push_back(std::move(value));
            }
            controller.handle(std::move(event));
        }

        [[nodiscard]] std::optional<std::vector<gitman::trusted_function_definition>> saved_trust() const
        {
            const gitman::operation_request* const save { last_of(gitman::operation_kind::save_app_settings) };
            if (save == nullptr || save->app_settings_payload.has_value() == false)
                return std::nullopt;
            return save->app_settings_payload->trusted_function_definitions;
        }

        void run(const std::u8string_view id, const std::u8string_view button)
        {
            controller.handle(gitman::run_function_intent { gitman::project_id { std::u8string { id } }, std::u8string { button } });
        }

        [[nodiscard]] std::shared_ptr<const gitman::view_snapshot> snapshot() const
        {
            return controller.make_view_snapshot();
        }

        static constexpr std::u8string_view document_path { function_card_fixture::document_path };
    };

    [[nodiscard]] std::vector<gitman::function_button_entry> entries_of(std::vector<gitman::function_button> buttons)
    {
        std::vector<gitman::function_button_entry> entries {};
        for (gitman::function_button& button : buttons)
            entries.emplace_back(std::move(button));
        return entries;
    }

    [[nodiscard]] const gitman::function_button& button_at(const gitman::function_buttons_definition& definition, const std::size_t index)
    {
        REQUIRE(index < definition.buttons.size());
        const gitman::function_button* const button { std::get_if<gitman::function_button>(&definition.buttons[index]) };
        REQUIRE(button != nullptr);
        return *button;
    }

    // 파서가 검증에 실패한 항목에 다는 진단이다. 편집 dialog가 경고 행의 사유로 쓴다.
    [[nodiscard]] gitman::diagnostic failed_item(const std::size_t index, const std::u8string_view message)
    {
        gitman::diagnostic value {};
        value.code = gitman::diagnostic_code::invalid_function_button;
        value.severity = gitman::diagnostic_severity::warning;
        value.message = message;
        value.source.json_pointer = u8"/function_buttons/buttons/" + std::u8string { static_cast<char8_t>(u8'0' + index) };
        return value;
    }
}

TEST_CASE("The argument line splits like CommandLineToArgvW", "[function_buttons][t12][domain]")
{
    // 공백 구분, 큰따옴표 묶음, 공백이 든 경로 (2.7).
    REQUIRE(split(u8"--build \"C:\\Program Files\\x\\build\" --config Debug").arguments == arguments({ u8"--build", u8"C:\\Program Files\\x\\build", u8"--config", u8"Debug" }));
    // 따옴표 앞이 아닌 역슬래시는 글자 그대로다. 경로 끝의 역슬래시도 남는다.
    REQUIRE(split(u8"C:\\Tools\\ \\\\server\\share\\").arguments == arguments({ u8"C:\\Tools\\", u8"\\\\server\\share\\" }));
    // 따옴표 앞의 역슬래시 2n개는 n개 + 묶음, 2n+1개는 n개 + 글자 `"`다.
    REQUIRE(split(u8"a\\\"b").arguments == arguments({ u8"a\"b" }));
    REQUIRE(split(u8"\"a\\\\\" b").arguments == arguments({ u8"a\\", u8"b" }));
    REQUIRE(split(u8"\"a\\\\\\\"b\"").arguments == arguments({ u8"a\\\"b" }));
    // 빈 따옴표는 빈 인자이고, 탭과 겹친 공백은 한 구분이다.
    REQUIRE(split(u8"\"\"  x\t\ty").arguments == arguments({ u8"", u8"x", u8"y" }));
    REQUIRE(split(u8"   ").arguments.empty());
    // 따옴표가 낱말 가운데에 있어도 묶음이다.
    REQUIRE(split(u8"--root=\"${projectRoot}\\my dir\"").arguments == arguments({ u8"--root=${projectRoot}\\my dir" }));

    // 닫히지 않은 따옴표는 막는다.
    REQUIRE(split(u8"--name \"open").unterminated_quote);
    REQUIRE(split(u8"\\\"\"").unterminated_quote);
    REQUIRE_FALSE(split(u8"\\\"").unterminated_quote);
}

TEST_CASE("Joined arguments split back to the same array", "[function_buttons][t12][domain]")
{
    const std::vector<std::vector<std::u8string>> samples {
        arguments({ u8"--build", u8"${projectRoot}\\build", u8"--config", u8"${build_config}" }),
        arguments({ u8"C:\\Program Files\\x", u8"", u8"say \"hi\"" }),
        arguments({ u8"trailing\\", u8"with space\\", u8"quote\\\"", u8"\\\\server\\share" }),
        arguments({ u8"한글 경로", u8"tab\there" }),
        {},
    };
    for (const std::vector<std::u8string>& sample : samples)
    {
        const std::u8string line { gitman::join_function_arguments(sample) };
        const function_argument_split back { split(line) };
        REQUIRE_FALSE(back.unterminated_quote);
        REQUIRE(back.arguments == sample);
    }
    // 따옴표가 필요 없는 인자는 감싸지 않는다.
    REQUIRE(gitman::join_function_arguments(arguments({ u8"--build", u8"${projectRoot}\\build" })) == u8"--build ${projectRoot}\\build");
    REQUIRE(gitman::join_function_arguments(arguments({ u8"C:\\Program Files\\x\\" })) == u8"\"C:\\Program Files\\x\\\\\"");
}

TEST_CASE("New function ids avoid taken ids and failed source items", "[function_buttons][t12][domain]")
{
    const std::vector<std::u8string> taken { u8"function-1", u8"build-1" };
    const std::vector<gitman::function_source_text> sources { { u8"{\"id\":\"function-2\",\"label\":\"x\"}" } };
    REQUIRE(gitman::make_function_button_id(u8"function", taken, sources) == u8"function-3");
    REQUIRE(gitman::make_function_button_id(u8"build", taken, sources) == u8"build-2");
    REQUIRE(gitman::make_function_button_id({}, {}, {}) == u8"function-1");
    // 64자를 넘으면 기본 이름으로 센다.
    REQUIRE(gitman::make_function_button_id(std::u8string(63, u8'a'), {}, {}) == u8"function-1");
}

TEST_CASE("The editor rule function is the parser rule", "[function_buttons][t12][domain]")
{
    gitman::function_button button { make_function_card_button(u8"build", u8"빌드") };
    const std::vector<std::u8string> names { u8"tool" };
    REQUIRE(gitman::function_button_problem(button, names).empty());

    gitman::function_button relative { button };
    relative.program = u8"tools\\run.exe";
    REQUIRE(gitman::function_button_problem(relative, names) == gitman::function_program_rejection_message(gitman::function_program_rejection::not_absolute));
    gitman::function_button script { button };
    script.program = u8"C:\\Tools\\run.bat";
    REQUIRE(gitman::function_button_problem(script, names) == gitman::function_program_rejection_message(gitman::function_program_rejection::extension_not_allowed));
    gitman::function_button undefined { button };
    undefined.args = arguments({ u8"${tool}", u8"${missing}" });
    REQUIRE(gitman::function_button_problem(undefined, names) == u8"args[1]에서 정의되지 않은 변수를 씁니다: ${missing}");
    gitman::function_button open { button };
    open.cwd = u8"${projectRoot";
    REQUIRE(gitman::function_button_problem(open, names) == u8"cwd의 변수 문법이 잘못되었습니다: ${가 닫히지 않았습니다.");
    gitman::function_button long_timeout { button };
    long_timeout.timeout_seconds = 86401;
    REQUIRE(gitman::function_button_problem(long_timeout, names) == u8"timeout_seconds는 0~86400 사이의 정수여야 합니다.");
    gitman::function_button empty_label { button };
    empty_label.label.clear();
    REQUIRE(gitman::function_button_problem(empty_label, names) == u8"label은 비지 않은 문자열이어야 합니다.");
    // 변수가 든 program은 읽을 때 허용 목록을 보지 않는다 (클릭 시점에 본다).
    gitman::function_button variable { button };
    variable.program = u8"${tool}\\run.bat";
    REQUIRE(gitman::function_button_problem(variable, names).empty());
}

TEST_CASE("The editor lists functions with placement marks and the card count", "[function_buttons][t12][logic]")
{
    std::vector<gitman::function_button_entry> entries {
        entries_of({
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"bar"),
            make_function_card_button(u8"c", u8"다"),
            make_function_card_button(u8"d", u8"라", u8"card"),
        }),
    };
    entries.emplace(entries.begin() + 2, gitman::function_source_text { u8"{\"id\":\"Bad\",\"label\":\"망가짐\"}" });
    editor_fixture fixture { make_function_card_projects(2), std::move(entries), true, { failed_item(2, u8"function_buttons.buttons[2] (\"망가짐\"): id는 영문 소문자·숫자·_·-로 64자까지 쓴 문자열이어야 합니다. 이 기능 버튼을 쓰지 않습니다.") } };
    fixture.open();

    const gitman::function_editor_view view { fixture.editor() };
    REQUIRE(view.title == u8"기능 편집 · projects.version-list");
    // N은 경고 행을 포함한 행 수, k는 보이는 `card` 수다.
    REQUIRE(view.list_header == u8"기능 5개 · 카드 2/3");
    REQUIRE(view.rows.size() == 5u);
    REQUIRE(view.rows[0].placement == std::optional { gitman::function_placement::card });
    REQUIRE(view.rows[0].shortcut == 1u);
    REQUIRE(view.rows[1].placement == std::optional { gitman::function_placement::bar });
    REQUIRE(view.rows[2].warning);
    REQUIRE(view.rows[2].label == u8"망가짐");
    REQUIRE(view.rows[3].placement == std::optional { gitman::function_placement::menu });
    REQUIRE(view.rows[4].shortcut == 2u);
    REQUIRE(view.selected == std::optional<std::size_t> { 0 });
    REQUIRE(view.editable);
    REQUIRE(view.label.text == u8"가");
    REQUIRE(view.program.text == u8"C:\\Tools\\run.exe");
    REQUIRE(view.timeout.text == u8"3600");
    REQUIRE(view.placement == gitman::function_placement::card);

    // 경고 행을 고르면 편집할 수 없고 사유와 원문을 보인다.
    fixture.select(2);
    const gitman::function_editor_view warning { fixture.editor() };
    REQUIRE_FALSE(warning.editable);
    REQUIRE(warning.warning_source == u8"{\"id\":\"Bad\",\"label\":\"망가짐\"}");
    REQUIRE(warning.warning_text.find(u8"id는 영문 소문자") != std::u8string::npos);
}

TEST_CASE("Apply validates every item with the parser rule before touching the model", "[function_buttons][t12][logic]")
{
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가", u8"card"), make_function_card_button(u8"b", u8"나") }) };
    fixture.open();
    fixture.select(1);
    fixture.type(gitman::text_input_target::function_editor_program, u8"tools\\run.exe");
    fixture.select(0);
    fixture.type(gitman::text_input_target::function_editor_label, u8"빌드");
    fixture.apply();

    // 실패하면 그 항목을 고르고 사유를 보이며, 모델과 파일은 그대로다.
    const gitman::function_editor_view view { fixture.editor() };
    REQUIRE(view.selected == std::optional<std::size_t> { 1 });
    REQUIRE(view.message.starts_with(u8"\"나\": program"));
    REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);
    REQUIRE(fixture.snapshot()->cards[0].functions[0].label == u8"가");

    SECTION("닫히지 않은 따옴표는 칸 아래에 알리고 Apply를 막는다")
    {
        fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\run.exe");
        fixture.type(gitman::text_input_target::function_editor_arguments, u8"--name \"open");
        const gitman::function_editor_view quoted { fixture.editor() };
        REQUIRE(quoted.arguments_error);
        REQUIRE(quoted.arguments_message == u8"인자에 닫히지 않은 큰따옴표가 있습니다.");
        fixture.apply();
        REQUIRE(fixture.editor().message == u8"\"나\": 인자에 닫히지 않은 큰따옴표가 있습니다.");
        REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);
    }

    SECTION("고치면 Apply가 모델을 바꾸고 곧바로 저장한다")
    {
        fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Program Files\\Tool\\run.exe");
        fixture.type(gitman::text_input_target::function_editor_arguments, u8"--root \"${projectRoot}\\my dir\" --verbose");
        fixture.type(gitman::text_input_target::function_editor_working_directory, u8"${workspaceFolder}");
        fixture.type(gitman::text_input_target::function_editor_timeout, u8"12a0");
        REQUIRE(fixture.editor().timeout.text == u8"120");
        fixture.apply();
        REQUIRE(fixture.snapshot()->function_editor.has_value() == false);
        const gitman::function_buttons_definition& saved { fixture.saved() };
        REQUIRE(button_at(saved, 0).label == u8"빌드");
        const gitman::function_button& edited { button_at(saved, 1) };
        REQUIRE(edited.program == u8"C:\\Program Files\\Tool\\run.exe");
        REQUIRE(edited.args == arguments({ u8"--root", u8"${projectRoot}\\my dir", u8"--verbose" }));
        REQUIRE(edited.cwd == std::optional<std::u8string> { u8"${workspaceFolder}" });
        REQUIRE(edited.timeout_seconds == 120);
    }
}

TEST_CASE("Untouched fields keep their stored form", "[function_buttons][t12][logic]")
{
    // 칸을 건드리지 않으면 원래 배열·생략한 cwd·배치 원문을 그대로 쓴다 (3.9, 생략한 필드에 기본값을 만들지 않는다).
    gitman::function_button button { make_function_card_button(u8"a", u8"가", u8"sidebar") };
    button.args = arguments({ u8"x", u8"y" });
    editor_fixture fixture { make_function_card_projects(1), entries_of({ button, make_function_card_button(u8"b", u8"나") }) };
    fixture.open();
    fixture.select(1);
    fixture.type(gitman::text_input_target::function_editor_label, u8"나2");
    fixture.apply();
    const gitman::function_buttons_definition& saved { fixture.saved() };
    REQUIRE(button_at(saved, 0) == button);
    REQUIRE(button_at(saved, 1).cwd.has_value() == false);
    REQUIRE(button_at(saved, 1).placement.has_value() == false);

    SECTION("바꾼 것이 없으면 저장하지 않는다")
    {
        editor_fixture same { make_function_card_projects(1), entries_of({ button }) };
        same.open();
        same.apply();
        REQUIRE(same.snapshot()->function_editor.has_value() == false);
        REQUIRE(same.last_of(gitman::operation_kind::save_document) == nullptr);
    }
}

TEST_CASE("Failed items stay as warning rows in place through Apply", "[function_buttons][t12][logic]")
{
    std::vector<gitman::function_button_entry> entries { entries_of({ make_function_card_button(u8"a", u8"가"), make_function_card_button(u8"b", u8"나") }) };
    const gitman::function_source_text source { u8"{\"id\":\"x\",\"label\":\"엑스\",\"program\":\"run.bat\",\"future\":1}" };
    entries.emplace(entries.begin() + 1, source);
    editor_fixture fixture { make_function_card_projects(1), std::move(entries) };
    fixture.open();

    // 앞 기능을 아래로 옮기고 지워도 경고 행은 원문 그대로 남는다. 순서 변경은 경고 행도 함께 옮긴다.
    fixture.list(function_editor_list_edit::move_down);
    REQUIRE(fixture.editor().rows[0].warning);
    REQUIRE(fixture.editor().selected == std::optional<std::size_t> { 1 });
    fixture.list(function_editor_list_edit::move_up);
    fixture.select(2);
    fixture.type(gitman::text_input_target::function_editor_label, u8"나나");
    fixture.apply();
    const gitman::function_buttons_definition& saved { fixture.saved() };
    REQUIRE(saved.buttons.size() == 3u);
    REQUIRE(std::get<gitman::function_source_text>(saved.buttons[1]) == source);
    REQUIRE(button_at(saved, 2).label == u8"나나");
}

TEST_CASE("Reordering moves the selected function and renumbers Alt shortcuts after Apply", "[function_buttons][t12][logic]")
{
    editor_fixture fixture {
        make_function_card_projects(1),
        entries_of({ make_function_card_button(u8"a", u8"가", u8"card"), make_function_card_button(u8"b", u8"나", u8"card"), make_function_card_button(u8"c", u8"다") }),
    };
    fixture.open(u8"b");
    REQUIRE(fixture.editor().selected == std::optional<std::size_t> { 1 });
    fixture.list(function_editor_list_edit::move_up);
    REQUIRE(fixture.editor().selected == std::optional<std::size_t> { 0 });
    REQUIRE(fixture.editor().rows[0].label == u8"나");
    REQUIRE(fixture.editor().rows[0].shortcut == 1u);
    // 맨 위에서 위로는 아무것도 하지 않는다.
    fixture.list(function_editor_list_edit::move_up);
    REQUIRE(fixture.editor().rows[0].label == u8"나");
    fixture.apply();
    REQUIRE(button_at(fixture.saved(), 0).id == u8"b");
    const gitman::card_view_model card { fixture.snapshot()->cards[0] };
    REQUIRE(card.functions[0].button_id == u8"b");
    REQUIRE(card.functions[0].shortcut == 1u);
    REQUIRE(card.functions[1].shortcut == 2u);
}

TEST_CASE("The placement group refuses a fourth card", "[function_buttons][t12][logic]")
{
    editor_fixture fixture {
        make_function_card_projects(1),
        entries_of({
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"card"),
            make_function_card_button(u8"c", u8"다", u8"card"),
            make_function_card_button(u8"d", u8"라", u8"bar"),
        }),
    };
    fixture.open(u8"d");
    gitman::function_editor_view view { fixture.editor() };
    REQUIRE(view.list_header == u8"기능 4개 · 카드 3/3");
    REQUIRE(view.card_full);
    REQUIRE(view.card_shortcut == 0u);

    fixture.field(function_editor_field::placement, u8"card");
    view = fixture.editor();
    REQUIRE(view.placement == gitman::function_placement::bar);
    REQUIRE(view.placement_message == u8"카드에는 3개까지 둡니다. 다른 기능을 카드에서 내린 뒤 고르십시오.");

    // 카드 하나를 내리면 고를 수 있다. 카드에 두면 문서 순서로 세 번째라 `Alt+3`이다.
    fixture.select(1);
    REQUIRE(fixture.editor().placement_message.empty());
    fixture.field(function_editor_field::placement, u8"menu");
    fixture.select(3);
    view = fixture.editor();
    REQUIRE_FALSE(view.card_full);
    REQUIRE(view.card_shortcut == 3u);
    fixture.field(function_editor_field::placement, u8"card");
    REQUIRE(fixture.editor().placement == gitman::function_placement::card);
    REQUIRE(fixture.editor().list_header == u8"기능 4개 · 카드 3/3");
}

TEST_CASE("A function added with plus starts in the menu and rises on Apply", "[function_buttons][t12][logic]")
{
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가", u8"card") }) };
    fixture.open();
    fixture.list(function_editor_list_edit::add);
    gitman::function_editor_view view { fixture.editor() };
    REQUIRE(view.rows.size() == 2u);
    REQUIRE(view.selected == std::optional<std::size_t> { 1 });
    // 기본 배치는 `menu`다 (D22-B).
    REQUIRE(view.rows[1].placement == std::optional { gitman::function_placement::menu });
    REQUIRE(view.placement == gitman::function_placement::menu);
    REQUIRE(view.label.text == u8"새 기능");
    REQUIRE(view.program.text.empty());

    // program이 비면 Apply가 막힌다.
    fixture.apply();
    REQUIRE(fixture.editor().message == u8"\"새 기능\": program은 비지 않은 문자열이어야 합니다.");

    SECTION("카드로 올리면 저장 뒤 카드 열에 나온다")
    {
        fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\deploy.exe");
        fixture.field(function_editor_field::placement, u8"card");
        fixture.apply();
        const gitman::function_button& added { button_at(fixture.saved(), 1) };
        REQUIRE(added.id == u8"function-1");
        REQUIRE(added.placement == std::optional<std::u8string> { u8"card" });
        // 저장이 끝난 뒤에도 카드 열에 그대로 있다.
        fixture.finish_save(std::nullopt);
        const gitman::card_view_model card { fixture.snapshot()->cards[0] };
        REQUIRE(card.functions.size() == 2u);
        REQUIRE(card.functions[1].placement == gitman::function_placement::card);
        REQUIRE(card.functions[1].shortcut == 2u);
    }

    SECTION("기능 줄로 올리면 저장 뒤 기능 줄에 나온다")
    {
        fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\deploy.exe");
        fixture.field(function_editor_field::placement, u8"bar");
        fixture.apply();
        fixture.finish_save(std::nullopt);
        fixture.controller.handle(gitman::select_card_intent { gitman::project_id { u8"p00" } });
        const auto snapshot { fixture.snapshot() };
        REQUIRE(snapshot->function_bar.has_value());
        REQUIRE(snapshot->function_bar->functions.size() == 1u);
        REQUIRE(snapshot->function_bar->functions[0].button_id == u8"function-1");
    }

    SECTION("메뉴만이면 기능 메뉴에만 있다")
    {
        fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\deploy.exe");
        fixture.apply();
        fixture.finish_save(std::nullopt);
        REQUIRE(button_at(fixture.saved(), 1).placement.has_value() == false);
        const gitman::card_view_model card { fixture.snapshot()->cards[0] };
        REQUIRE(card.functions[1].placement == gitman::function_placement::menu);
    }
}

TEST_CASE("Duplicate and remove edit the draft list", "[function_buttons][t12][logic]")
{
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"build", u8"빌드", u8"card"), make_function_card_button(u8"b", u8"나") }) };
    fixture.open();
    fixture.list(function_editor_list_edit::duplicate);
    gitman::function_editor_view view { fixture.editor() };
    REQUIRE(view.rows.size() == 3u);
    REQUIRE(view.selected == std::optional<std::size_t> { 1 });
    REQUIRE(view.rows[1].label == u8"빌드 (복사)");
    // 복제는 배치도 함께 옮긴다. 보이는 `card`가 둘이 된다.
    REQUIRE(view.list_header == u8"기능 3개 · 카드 2/3");
    fixture.list(function_editor_list_edit::remove);
    view = fixture.editor();
    REQUIRE(view.rows.size() == 2u);
    REQUIRE(view.selected == std::optional<std::size_t> { 1 });
    REQUIRE(view.rows[1].label == u8"나");
    fixture.list(function_editor_list_edit::duplicate);
    fixture.apply();
    REQUIRE(button_at(fixture.saved(), 2).id == u8"b-1");
}

TEST_CASE("Apply refuses removing a running function or moving its card out of scope", "[function_buttons][t12][logic]")
{
    editor_fixture fixture { make_function_card_projects(2), entries_of({ make_function_card_button(u8"build", u8"빌드", u8"card"), make_function_card_button(u8"b", u8"나") }) };
    fixture.run(u8"p01", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.open();

    SECTION("실행 중인 기능을 지우면 거절한다")
    {
        fixture.list(function_editor_list_edit::remove);
        fixture.apply();
        REQUIRE(fixture.editor().message == u8"\"빌드\"가 p01에서 실행 중입니다. 중지한 뒤 고치십시오.");
        REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);
    }

    SECTION("실행 중인 카드를 저장소 목록에서 빼면 거절한다")
    {
        fixture.field(function_editor_field::scope, u8"projects");
        fixture.field(function_editor_field::project, u8"p00");
        REQUIRE(fixture.editor().projects.size() == 2u);
        REQUIRE(fixture.editor().projects[0].checked);
        REQUIRE_FALSE(fixture.editor().projects[1].checked);
        fixture.apply();
        REQUIRE(fixture.editor().message == u8"\"빌드\"가 p01에서 실행 중입니다. 중지한 뒤 고치십시오.");
        REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);

        // 실행 중인 카드를 범위에 두면 받는다. 실행은 계속된다.
        fixture.field(function_editor_field::project, u8"p01");
        fixture.apply();
        REQUIRE(fixture.last_of(gitman::operation_kind::save_document) != nullptr);
        REQUIRE(button_at(fixture.saved(), 0).project_ids == std::optional { arguments({ u8"p00", u8"p01" }) });
        REQUIRE(fixture.snapshot()->cards[1].function_running);
    }

    SECTION("조회 전 auto 카드는 vcs 범위를 바꿔도 받는다")
    {
        fixture.field(function_editor_field::scope, u8"svn");
        fixture.apply();
        // 조회 전 `auto` 카드는 종류를 몰라 범위 밖이라 단정하지 않는다.
        REQUIRE(fixture.last_of(gitman::operation_kind::save_document) != nullptr);
    }

    SECTION("조회한 git 카드를 svn 범위로 바꾸면 거절한다")
    {
        gitman::query_completed_event event {};
        event.id.value = u8"p01";
        event.generation = 1;
        event.final_event = true;
        event.result.snapshot.project.value = u8"p01";
        event.result.snapshot.kind = gitman::repository_kind::git;
        event.result.snapshot.availability = gitman::repository_availability::ready;
        fixture.controller.handle(std::move(event));
        fixture.field(function_editor_field::scope, u8"svn");
        fixture.apply();
        REQUIRE(fixture.editor().message == u8"\"빌드\"가 p01에서 실행 중입니다. 중지한 뒤 고치십시오.");
    }

    SECTION("실행 중이 아닌 기능은 지울 수 있다")
    {
        fixture.select(1);
        fixture.list(function_editor_list_edit::remove);
        fixture.apply();
        REQUIRE(fixture.saved().buttons.size() == 1u);
    }
}

TEST_CASE("Saving an edit refreshes trust only when the definition was trusted", "[function_buttons][t12][logic][trust]")
{
    SECTION("신뢰한 정의는 저장한 정의의 해시로 갱신한다")
    {
        editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }) };
        fixture.open();
        fixture.type(gitman::text_input_target::function_editor_arguments, u8"--all");
        fixture.apply();
        // 저장이 끝나기 전에는 신뢰를 바꾸지 않는다.
        REQUIRE(fixture.saved_trust().has_value() == false);
        fixture.finish_save(std::u8string { u8"{\"after\":1}" });
        const std::optional<std::vector<gitman::trusted_function_definition>> trust { fixture.saved_trust() };
        REQUIRE(trust.has_value());
        REQUIRE(trust->size() == 1u);
        gitman::function_buttons_definition expected { fixture.saved() };
        expected.source_digest_input = u8"{\"after\":1}";
        REQUIRE((*trust)[0].digest == gitman::function_trust_digest(expected, make_function_card_projects(1)));
        // 다시 묻지 않고 실행한다.
        fixture.run(u8"p00", u8"a");
        REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value() == false);
        REQUIRE(fixture.functions.requests.size() == 1u);
    }

    SECTION("신뢰하지 않은 정의는 그대로 두어 다음 실행에서 묻는다")
    {
        editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }), false };
        fixture.open();
        fixture.type(gitman::text_input_target::function_editor_arguments, u8"--all");
        fixture.apply();
        fixture.finish_save(std::u8string { u8"{\"after\":1}" });
        REQUIRE(fixture.saved_trust().has_value() == false);
        fixture.run(u8"p00", u8"a");
        REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value());
        REQUIRE(fixture.snapshot()->function_confirm_dialog->mode == gitman::function_confirm_mode::trust);
    }

    SECTION("저장이 실패하면 신뢰를 바꾸지 않는다")
    {
        editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }) };
        fixture.open();
        fixture.type(gitman::text_input_target::function_editor_arguments, u8"--all");
        fixture.apply();
        fixture.finish_save(std::nullopt, false);
        REQUIRE(fixture.saved_trust().has_value() == false);
    }
}

TEST_CASE("Menus open the editor and the placement menu preselects its function", "[function_buttons][t12][logic][context-menu]")
{
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가"), make_function_card_button(u8"b", u8"나", u8"card") }) };

    SECTION("배너 메뉴")
    {
        fixture.controller.handle(gitman::open_document_context_menu_intent { 1.0f, 2.0f });
        const auto view { fixture.snapshot() };
        REQUIRE(view->context_menu->items.back().entry == gitman::context_menu_entry::edit_functions);
        REQUIRE(view->context_menu->items.back().label == u8"기능 편집…");
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::edit_functions, {} });
        REQUIRE(fixture.editor().selected == std::optional<std::size_t> { 0 });
    }

    SECTION("기능 메뉴")
    {
        fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 1.0f, 2.0f });
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::edit_functions, {} });
        REQUIRE(fixture.snapshot()->function_editor.has_value());
    }

    SECTION("카드 메뉴")
    {
        fixture.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 1.0f, 2.0f });
        REQUIRE(fixture.snapshot()->context_menu->items.back().entry == gitman::context_menu_entry::edit_functions);
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::edit_functions, {} });
        REQUIRE(fixture.snapshot()->function_editor.has_value());
    }

    SECTION("배치 메뉴는 그 기능을 고른 상태로 연다")
    {
        fixture.controller.handle(gitman::open_function_placement_menu_intent { gitman::project_id { u8"p00" }, u8"b", false, 1.0f, 2.0f });
        const auto view { fixture.snapshot() };
        REQUIRE(view->context_menu.has_value());
        REQUIRE(view->context_menu->items[3].entry == gitman::context_menu_entry::edit_functions);
        REQUIRE(view->context_menu->items[3].argument == u8"b");
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::edit_functions, u8"b" });
        REQUIRE(fixture.editor().selected == std::optional<std::size_t> { 1 });
        REQUIRE(fixture.editor().label.text == u8"나");
    }

    SECTION("정의가 없는 문서의 진입은 배너 메뉴다")
    {
        editor_fixture empty { make_function_card_projects(1), {} };
        empty.controller.handle(gitman::open_context_menu_intent { gitman::project_id { u8"p00" }, 1.0f, 2.0f });
        REQUIRE(empty.snapshot()->context_menu->items.size() == 6u);
        empty.controller.handle(gitman::open_document_context_menu_intent { 1.0f, 2.0f });
        empty.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::edit_functions, {} });
        REQUIRE(empty.editor().rows.empty());
        REQUIRE(empty.editor().list_header == u8"기능 0개 · 카드 0/3");
    }
}

TEST_CASE("The editor preview resolves the draft with the target card", "[function_buttons][t12][logic]")
{
    gitman::function_button button { make_function_card_button(u8"a", u8"가") };
    button.args = arguments({ u8"--root", u8"${projectRoot}", u8"${env:SECRET}", u8"${branch}" });
    editor_fixture fixture { make_function_card_projects(2), entries_of({ button }) };
    fixture.open();
    gitman::function_editor_view view { fixture.editor() };
    REQUIRE(view.preview_card == u8"p00");
    REQUIRE(view.preview_can_step);
    // 조회 전 런타임 변수는 미해결 경고다.
    REQUIRE(view.preview_command.empty());
    REQUIRE(view.preview_warnings.size() == 1u);
    REQUIRE(view.preview_warnings[0] == u8"인자: 저장소를 조회한 뒤에 풀리는 변수입니다: ${branch}");

    fixture.type(gitman::text_input_target::function_editor_arguments, u8"--root \"${projectRoot}\\my dir\" ${env:SECRET}");
    view = fixture.editor();
    REQUIRE(view.preview_warnings.empty());
    // env 구간은 이름으로 남고, 공백이 든 인자는 인용된다 (표시용 명령줄, 3.6).
    REQUIRE(view.preview_command == u8"\"C:\\Tools\\run.exe\" --root \"C:\\work\\p00\\my dir\" ${env:SECRET}");
    REQUIRE(view.preview_working_directory == u8"C:\\work\\p00");

    fixture.controller.handle(gitman::step_function_editor_preview_intent { true });
    REQUIRE(fixture.editor().preview_card == u8"p01");
    REQUIRE(fixture.editor().preview_working_directory == u8"C:\\work\\p01");
    fixture.controller.handle(gitman::step_function_editor_preview_intent { true });
    REQUIRE(fixture.editor().preview_card == u8"p00");

    // 허용 목록 위반도 경고다.
    fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\run.bat");
    REQUIRE(fixture.editor().preview_warnings.size() == 1u);
    REQUIRE(fixture.editor().preview_warnings[0] == gitman::function_program_rejection_message(gitman::function_program_rejection::extension_not_allowed));
}

TEST_CASE("The program picker fills the program field", "[function_buttons][t12][logic]")
{
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }) };
    fixture.open();
    fixture.field(function_editor_field::program, u8"C:\\Program Files\\Tool\\tool.exe");
    REQUIRE(fixture.editor().program.text == u8"C:\\Program Files\\Tool\\tool.exe");
    fixture.field(function_editor_field::icon, u8"rocket");
    fixture.field(function_editor_field::mode, u8"detached");
    fixture.field(function_editor_field::confirm);
    fixture.apply();
    const gitman::function_button& saved { button_at(fixture.saved(), 0) };
    REQUIRE(saved.program == u8"C:\\Program Files\\Tool\\tool.exe");
    REQUIRE(saved.icon == std::optional<std::u8string> { u8"rocket" });
    REQUIRE(saved.mode == gitman::function_run_mode::detached);
    REQUIRE(saved.confirm);
}

TEST_CASE("A document whose button list cannot be read is not written by the editor", "[function_buttons][t12][logic]")
{
    gitman::diagnostic value {};
    value.code = gitman::diagnostic_code::invalid_function_buttons;
    value.severity = gitman::diagnostic_severity::warning;
    value.message = u8"function_buttons.buttons: 배열이어야 합니다.";
    value.source.json_pointer = u8"/function_buttons/buttons";
    editor_fixture fixture { make_function_card_projects(1), {}, true, { value } };
    fixture.open();
    REQUIRE(fixture.editor().locked);
    fixture.list(function_editor_list_edit::add);
    fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\run.exe");
    fixture.apply();
    REQUIRE(fixture.editor().message.starts_with(u8"문서의 function_buttons를 읽지 못해"));
    REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);
}

TEST_CASE("Cancel and document changes drop the draft", "[function_buttons][t12][logic]")
{
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }) };
    fixture.open();
    fixture.type(gitman::text_input_target::function_editor_label, u8"바뀜");
    fixture.controller.handle(gitman::cancel_function_editor_intent {});
    REQUIRE(fixture.snapshot()->function_editor.has_value() == false);
    REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);
    fixture.open();
    REQUIRE(fixture.editor().label.text == u8"가");
    fixture.controller.handle(gitman::close_document_intent {});
    REQUIRE(fixture.snapshot()->function_editor.has_value() == false);
}

TEST_CASE("A display only edit saved through the store keeps the trust digest", "[function_buttons][t12][logic][store][trust]")
{
    // 실제 store로 읽고 쓴다. 표시 필드(`label`, `icon`, `placement`)만 바꾼 Apply는 저장한 글의 해시 입력이 같아 신뢰가
    // 그대로이고, 실행에 쓰는 필드를 바꾸면 해시가 바뀐다 (2.7, 3.10).
    const scoped_scan_directory directory {};
    REQUIRE(directory.available());
    const std::u8string root { directory.make_directory(u8"alpha") };
    const std::u8string document_path { directory.path_of(u8"team.version-list") };
    std::string root_json {};
    for (const char8_t value : root)
        root_json.push_back(value == u8'\\' ? '/' : static_cast<char>(value));
    {
        std::ofstream stream { std::filesystem::path { document_path }, std::ios::binary };
        stream << "{\"schema_version\":1,\"function_buttons\":{\"buttons\":[{\"id\":\"build\",\"label\":\"빌드\",\"icon\":\"tools\",\"program\":\"C:\\\\Tools\\\\run.exe\",\"args\":[\"a b\"],\"placement\":\"card\"}]},"
                  "\"projects\":[{\"id\":\"alpha\",\"path\":\""
            + root_json + "\"}]}";
    }

    gitman::win32::workspace_document_file_system file_system {};
    class resolver final : public gitman::project_path_resolver
    {
    public:
        [[nodiscard]] gitman::project_path_resolution resolve(const std::u8string_view original_path, const std::u8string_view) noexcept override
        {
            try
            {
                return { std::u8string { original_path }, gitman::configured_path_state::available, std::nullopt };
            }
            catch (...)
            {
                return {};
            }
        }

        [[nodiscard]] bool normalized_equal(const std::u8string_view left, const std::u8string_view right) noexcept override
        {
            return left == right;
        }
    } path_resolver {};
    gitman::json_project_store store { file_system, path_resolver };
    gitman::project_store_load_result loaded { store.load(document_path) };
    REQUIRE(loaded.document.has_value());
    const std::u8string digest_before { gitman::function_trust_digest(*loaded.document->function_buttons, loaded.document->projects) };

    function_card_submitter submitter {};
    function_card_run_submitter functions {};
    gitman::cancellation_registry registry {};
    gitman::logic_controller controller { submitter, registry };
    controller.bind_function_execution(functions
        , gitman::make_function_run_services(
            [](const std::u8string_view) -> std::optional<std::u8string>
            {
                return std::nullopt;
            }
        )
    );
    controller.handle(gitman::window_metrics_intent { 1280.0f, 720.0f, 1.0f });
    controller.start();
    gitman::app_settings_loaded_event settings {};
    settings.operation_id = submitter.requests.back().operation_id;
    settings.settings.trusted_function_definitions.push_back({ document_path, digest_before });
    controller.handle(std::move(settings));
    controller.handle(gitman::open_document_intent { document_path });
    gitman::document_loaded_event event {};
    event.document = loaded.document;
    event.revision = loaded.revision;
    controller.handle(std::move(event));
    finish_settings_save(submitter, controller);
    submitter.requests.clear();

    // store를 지나는 저장 한 번이다. 요청의 문서를 실제로 쓰고 결과를 logic에 돌려준다.
    const auto save_through_store = [&]()
    {
        const gitman::operation_request* save { nullptr };
        for (const gitman::operation_request& request : submitter.requests)
        {
            if (request.kind == gitman::operation_kind::save_document)
                save = &request;
        }
        REQUIRE(save != nullptr);
        gitman::project_store_save_result saved { store.save(save->document_path, *save->document, save->revision) };
        REQUIRE(saved.succeeded());
        gitman::document_saved_event done {};
        done.operation_id = save->operation_id;
        done.revision = saved.revision;
        done.function_digest_input = saved.function_digest_input;
        done.diagnostics = saved.diagnostics;
        submitter.requests.clear();
        controller.handle(std::move(done));
        return saved;
    };
    const auto digest_now = [&]()
    {
        gitman::project_store_load_result now { store.load(document_path) };
        REQUIRE(now.document.has_value());
        return gitman::function_trust_digest(*now.document->function_buttons, now.document->projects);
    };

    controller.handle(gitman::open_function_editor_intent {});
    controller.handle(gitman::edit_text_input_intent { gitman::text_input_target::function_editor_label, gitman::text_edit_command::select_all });
    controller.handle(gitman::edit_text_input_intent { gitman::text_input_target::function_editor_label, gitman::text_edit_command::insert, u8"새 이름" });
    controller.handle(gitman::set_function_editor_field_intent { function_editor_field::icon, u8"rocket" });
    controller.handle(gitman::set_function_editor_field_intent { function_editor_field::placement, u8"bar" });
    controller.handle(gitman::apply_function_editor_intent {});
    const gitman::project_store_save_result display { save_through_store() };
    REQUIRE(display.function_digest_input == std::optional { loaded.document->function_buttons->source_digest_input });
    REQUIRE(digest_now() == digest_before);
    // 인자 칸을 건드리지 않았으므로 공백이 든 인자의 원래 배열이 그대로다.
    gitman::project_store_load_result after_display { store.load(document_path) };
    REQUIRE(std::get<gitman::function_button>(after_display.document->function_buttons->buttons[0]).args == arguments({ u8"a b" }));
    // 신뢰가 그대로라 다시 묻지 않는다.
    controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"build" });
    REQUIRE(controller.make_view_snapshot()->function_confirm_dialog.has_value() == false);
    REQUIRE(functions.requests.size() == 1u);
    gitman::function_completed_event completed {};
    completed.id = functions.requests[0].id;
    completed.run_id = functions.requests[0].run_id;
    completed.outcome = gitman::function_run_outcome::exited;
    completed.exit_code = 0;
    controller.handle(std::move(completed));

    // 실행에 쓰는 필드를 바꾸면 해시가 바뀌고, 편집 전에 신뢰했으므로 저장한 정의로 신뢰를 갱신한다 (D24-A).
    controller.handle(gitman::open_function_editor_intent {});
    controller.handle(gitman::edit_text_input_intent { gitman::text_input_target::function_editor_arguments, gitman::text_edit_command::select_all });
    controller.handle(gitman::edit_text_input_intent { gitman::text_input_target::function_editor_arguments, gitman::text_edit_command::insert, u8"--all" });
    controller.handle(gitman::apply_function_editor_intent {});
    save_through_store();
    const std::u8string digest_after { digest_now() };
    REQUIRE(digest_after != digest_before);
    const gitman::operation_request* app_save { nullptr };
    for (const gitman::operation_request& request : submitter.requests)
    {
        if (request.kind == gitman::operation_kind::save_app_settings)
            app_save = &request;
    }
    REQUIRE(app_save != nullptr);
    REQUIRE(app_save->app_settings_payload->trusted_function_definitions.size() == 1u);
    REQUIRE(app_save->app_settings_payload->trusted_function_definitions[0].digest == digest_after);
    controller.handle(gitman::run_function_intent { gitman::project_id { u8"alpha" }, u8"build" });
    REQUIRE(controller.make_view_snapshot()->function_confirm_dialog.has_value() == false);
    REQUIRE(functions.requests.size() == 2u);
}

TEST_CASE("Duplicating a card function keeps the three card limit", "[function_buttons][t12][logic]")
{
    // 카드가 이미 3개면 복사본은 메뉴에 둔다. 그대로 두면 복사본이 원본 뒤에 끼어 세 번째 카드 기능이 기능 줄로 밀리고
    // `Alt+N`이 바뀐다 (2.7 `카드 — 3개까지`).
    editor_fixture fixture {
        make_function_card_projects(1),
        entries_of({
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"card"),
            make_function_card_button(u8"c", u8"다", u8"card"),
        }),
    };
    fixture.open(u8"b");
    fixture.list(function_editor_list_edit::duplicate);
    const gitman::function_editor_view view { fixture.editor() };
    REQUIRE(view.rows.size() == 4u);
    REQUIRE(view.selected == std::optional<std::size_t> { 2 });
    REQUIRE(view.list_header == u8"기능 4개 · 카드 3/3");
    REQUIRE(view.rows[2].placement == std::optional { gitman::function_placement::menu });
    REQUIRE(view.rows[3].shortcut == 3u);
    REQUIRE(view.placement_message == u8"카드에는 3개까지 둡니다. 복사본은 메뉴에만 둡니다.");

    fixture.apply();
    const gitman::function_buttons_definition& saved { fixture.saved() };
    REQUIRE(button_at(saved, 2).id == u8"b-1");
    REQUIRE(button_at(saved, 2).placement.has_value() == false);
    REQUIRE(button_at(saved, 3).placement == std::optional<std::u8string> { u8"card" });
    REQUIRE(fixture.snapshot()->cards[0].functions[3].shortcut == 3u);
}

TEST_CASE("New ids skip the ids of removed and running functions", "[function_buttons][t12][logic]")
{
    // 지운 기능의 id를 새 버튼이 받으면 쓰기 함수가 지운 항목의 원문을 template으로 쓰고(3.9), 실행 중 거절 검사가 새
    // 버튼을 지운 기능으로 본다 (4.11).
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"function-1", u8"가", u8"card"), make_function_card_button(u8"function-2", u8"나") }) };

    SECTION("실행 중인 기능을 지우고 더해도 그 id를 받지 않아 거절이 그대로다")
    {
        fixture.run(u8"p00", u8"function-1");
        REQUIRE(fixture.functions.requests.size() == 1u);
        fixture.open();
        fixture.list(function_editor_list_edit::remove);
        fixture.list(function_editor_list_edit::add);
        REQUIRE(fixture.editor().rows[1].key == u8"button-function-3");
        fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\deploy.exe");
        fixture.apply();
        REQUIRE(fixture.editor().message == u8"\"가\"가 p00에서 실행 중입니다. 중지한 뒤 고치십시오.");
        REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);
    }

    SECTION("실행 중이 아닌 기능의 id도 지운 뒤 다시 쓰지 않는다")
    {
        fixture.open();
        fixture.list(function_editor_list_edit::remove);
        fixture.list(function_editor_list_edit::add);
        fixture.type(gitman::text_input_target::function_editor_program, u8"C:\\Tools\\deploy.exe");
        fixture.apply();
        REQUIRE(fixture.saved().buttons.size() == 2u);
        REQUIRE(button_at(fixture.saved(), 1).id == u8"function-3");
    }
}

TEST_CASE("Apply refuses moving a running card whose hint names its kind out of scope", "[function_buttons][t12][logic]")
{
    // `vcs_hint: git`인 카드는 조회 전이어도 종류를 안다. 그 카드에서 실행 중인 기능을 svn 범위로 바꾸면 거절한다.
    std::vector<gitman::project_definition> projects { make_function_card_projects(2) };
    projects[1].hint = gitman::vcs_hint::git;
    editor_fixture fixture { std::move(projects), entries_of({ make_function_card_button(u8"build", u8"빌드", u8"card") }) };
    fixture.run(u8"p01", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.open();
    fixture.field(function_editor_field::scope, u8"svn");
    fixture.apply();
    REQUIRE(fixture.editor().message == u8"\"빌드\"가 p01에서 실행 중입니다. 중지한 뒤 고치십시오.");
    REQUIRE(fixture.last_of(gitman::operation_kind::save_document) == nullptr);
    // Git 범위는 받는다.
    fixture.field(function_editor_field::scope, u8"git");
    fixture.apply();
    REQUIRE(fixture.last_of(gitman::operation_kind::save_document) != nullptr);
}

TEST_CASE("A removed warning row leaves the saved list and the notice follows the save", "[function_buttons][t12][logic]")
{
    // 경고 행은 지우기 전까지 원문째 같은 자리에 남는다. 지우면 저장한 목록에서 빠지고, 저장이 끝나면 알림 띠의 기능
    // 버튼 경고도 쓴 글을 다시 읽은 진단으로 다시 만든다.
    std::vector<gitman::function_button_entry> entries { entries_of({ make_function_card_button(u8"a", u8"가"), make_function_card_button(u8"b", u8"나") }) };
    entries.emplace(entries.begin() + 1, gitman::function_source_text { u8"{\"id\":\"x\",\"label\":\"a: b\",\"program\":\"run.bat\"}" });
    editor_fixture fixture {
        make_function_card_projects(1),
        std::move(entries),
        true,
        { failed_item(1, u8"function_buttons.buttons[1] (\"a: b\"): program은 절대 경로여야 합니다. 이 기능 버튼을 쓰지 않습니다.") },
    };
    REQUIRE(fixture.snapshot()->notices.back().text == u8"기능 버튼 경고 1건");
    fixture.open();
    // label 안의 `: `는 이름을 자르지 않는다.
    REQUIRE(fixture.editor().rows[1].label == u8"a: b");
    REQUIRE(fixture.editor().rows[1].key == u8"source-1");
    fixture.select(1);
    fixture.list(function_editor_list_edit::remove);
    fixture.apply();
    REQUIRE(fixture.saved().buttons.size() == 2u);
    REQUIRE(button_at(fixture.saved(), 1).id == u8"b");
    // 저장이 끝나기 전에는 그대로다.
    REQUIRE(fixture.snapshot()->notices.back().text == u8"기능 버튼 경고 1건");
    fixture.finish_save(std::nullopt);
    for (const gitman::notice_view_model& notice : fixture.snapshot()->notices)
        REQUIRE_FALSE(notice.text.starts_with(u8"기능 버튼 경고"));
}

TEST_CASE("A warning row notice scrolls when it is taller than the form", "[function_buttons][t12][logic]")
{
    // 480×320 창에서 폼 자리는 낮다. 경고 행의 사유와 원문이 넘치면 스크롤한다 (2.7 "원문째").
    std::u8string source { u8"{\"id\":\"x\",\"label\":\"긴 항목\",\"program\":\"run.bat\",\"args\":[" };
    for (std::size_t index = 0; index < 40; ++index)
        source.append(u8"\"--option-with-a-long-name\",");
    source.append(u8"\"end\"]}");
    const std::u8string problem { u8"function_buttons.buttons[0] (\"긴 항목\"): program은 절대 경로여야 합니다. 이 기능 버튼을 쓰지 않습니다." };
    editor_fixture fixture {
        make_function_card_projects(1),
        { gitman::function_source_text { source } },
        true,
        { failed_item(0, problem) },
        480.0f,
        320.0f,
    };
    fixture.open();
    REQUIRE_FALSE(fixture.editor().editable);
    fixture.controller.handle(gitman::function_editor_scroll_intent { gitman::function_editor_scroll_region::form, 100000.0f });
    const gitman::function_editor_size size { gitman::function_editor_dialog_size(480.0f, 320.0f) };
    const float content { gitman::function_editor_form_content_height(false, 0, problem, source, size.width) };
    const float viewport { gitman::function_editor_form_viewport_height(size.height, gitman::function_editor_collapsed(size.width)) };
    REQUIRE(content > viewport);
    REQUIRE(fixture.editor().form_scroll == gitman::function_editor_maximum_scroll(content, viewport));
    REQUIRE(fixture.editor().form_scroll > 0.0f);
}

TEST_CASE("The edit entry waits while the document reloads", "[function_buttons][t12][logic][context-menu]")
{
    // 다시 읽기가 성공하면 문서를 바꾸며 초안을 버린다. 그동안 `기능 편집…`은 `문서 다시 읽기`처럼 비활성이다.
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }) };
    fixture.controller.handle(gitman::reload_document_intent {});
    fixture.controller.handle(gitman::open_document_context_menu_intent { 1.0f, 2.0f });
    const auto view { fixture.snapshot() };
    REQUIRE(view->context_menu.has_value());
    const gitman::context_menu_item_view edit { view->context_menu->items.back() };
    REQUIRE(edit.entry == gitman::context_menu_entry::edit_functions);
    REQUIRE(edit.label == u8"기능 편집… — 읽는 중");
    REQUIRE_FALSE(edit.enabled);
    fixture.open();
    REQUIRE(fixture.snapshot()->function_editor.has_value() == false);
}

TEST_CASE("The program field drops quotes pasted around a path", "[function_buttons][t12][logic]")
{
    // 탐색기의 "경로로 복사"는 경로를 큰따옴표로 감싼다. program 칸은 경로 하나라 감싼 따옴표만 벗긴다.
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }) };
    fixture.open();
    fixture.type(gitman::text_input_target::function_editor_program, u8"\"C:\\Program Files\\Tool\\run.exe\"");
    fixture.apply();
    REQUIRE(button_at(fixture.saved(), 0).program == u8"C:\\Program Files\\Tool\\run.exe");
}

namespace {
    [[nodiscard]] std::u8string last_card_log(const editor_fixture& fixture, const std::u8string_view card)
    {
        const gitman::operation_log_buffer* const log { fixture.controller.card_log(gitman::project_id { std::u8string { card } }) };
        REQUIRE(log != nullptr);
        REQUIRE(log->records().empty() == false);
        return log->records().back().entry.text;
    }
}

TEST_CASE("An edit whose save failed neither runs nor records trust", "[function_buttons][v][logic][trust]")
{
    // 해시 입력은 파일 원문이라 저장하지 못한 편집 모델과 어긋난다. 신뢰 dialog가 보일 모델과 기록할 해시가 다르면 안 된다.
    for (const bool trusted : { false, true })
    {
        CAPTURE(trusted);
        editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가"), make_function_card_button(u8"b", u8"나") }), trusted };
        fixture.open();
        fixture.list(function_editor_list_edit::remove);
        fixture.apply();
        fixture.finish_save(std::nullopt, false);

        fixture.run(u8"p00", u8"b");
        REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value() == false);
        REQUIRE(fixture.functions.requests.empty());
        REQUIRE(fixture.saved_trust().has_value() == false);
        REQUIRE(last_card_log(fixture, u8"p00").find(u8"기능 정의가 아직 저장되지 않아 실행하지 않습니다") != std::u8string::npos);

        // 다시 읽으면 파일의 정의로 돌아가 다시 실행할 수 있다.
        fixture.controller.handle(gitman::reload_document_intent {});
        const gitman::operation_request* const reload { fixture.last_of(gitman::operation_kind::load_document) };
        REQUIRE(reload != nullptr);
        gitman::document_loaded_event loaded {};
        loaded.operation_id = reload->operation_id;
        gitman::workspace_document document {};
        document.document_path = editor_fixture::document_path;
        document.projects = make_function_card_projects(1);
        gitman::function_buttons_definition definition {};
        definition.buttons = entries_of({ make_function_card_button(u8"a", u8"가"), make_function_card_button(u8"b", u8"나") });
        definition.source_digest_input = u8"{\"before\":1}";
        document.function_buttons = std::move(definition);
        loaded.document = { std::move(document) };
        fixture.controller.handle(std::move(loaded));
        fixture.run(u8"p00", u8"b");
        REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value() != trusted);
        REQUIRE(fixture.functions.requests.size() == (trusted ? 1u : 0u));
    }
}

TEST_CASE("A run between Apply and its save result waits for the saved definition", "[function_buttons][v][logic][trust]")
{
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }) };
    fixture.open();
    fixture.type(gitman::text_input_target::function_editor_arguments, u8"--all");
    fixture.apply();

    // 저장이 끝나기 전에는 해시 입력이 편집 전 원문이다. 그 해시로 신뢰를 판정하거나 기록하지 않는다.
    fixture.run(u8"p00", u8"a");
    REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value() == false);
    REQUIRE(fixture.functions.requests.empty());

    fixture.finish_save(std::u8string { u8"{\"after\":1}" });
    fixture.run(u8"p00", u8"a");
    REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value() == false);
    REQUIRE(fixture.functions.requests.size() == 1u);
}

TEST_CASE("A trust dialog confirmed after Apply changed the definition does not run", "[function_buttons][v][logic][trust]")
{
    // 확인 dialog가 떠 있는 동안 편집 dialog의 Apply가 정의를 바꾸면, 확인한 것은 편집 전 정의다 (4.5).
    editor_fixture fixture { make_function_card_projects(1), entries_of({ make_function_card_button(u8"a", u8"가") }), false };
    fixture.open();
    fixture.run(u8"p00", u8"a");
    REQUIRE(fixture.snapshot()->function_confirm_dialog.has_value());
    fixture.type(gitman::text_input_target::function_editor_arguments, u8"--all");
    fixture.apply();

    SECTION("저장 결과 전에 확인한다")
    {
    }

    SECTION("저장 결과가 먼저 도착한다")
    {
        fixture.finish_save(std::u8string { u8"{\"after\":1}" });
    }

    fixture.controller.handle(gitman::confirm_function_run_intent {});
    REQUIRE(fixture.functions.requests.empty());
    REQUIRE(last_card_log(fixture, u8"p00").find(u8"확인한 뒤 기능 정의가 바뀌어 실행하지 않습니다") != std::u8string::npos);
}

