#include "helpers/function_card_fixture.h"
#include "helpers/ui_test_support.h"

#include "application/logic_controller.h"
#include "presentation/ui/context_menu_popup.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_confirm_dialog_element.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

// 안전한 다시 읽기와 실행 중 닫기·다시 읽기·다른 문서 열기·종료 확인, 저장 충돌 안내다 (function-buttons-design 4.9,
// 4.10, 6.1 T11). intent와 worker event를 직접 넣고 제출된 요청, view snapshot, 실행 token을 본다. 실제 파일로 저장이
// 회복되는 것과 종료 join은 `function_shutdown_tests.cpp`가 본다.
namespace {
    using gitman::testing::function_card_fixture;
    using gitman::testing::id_of;
    using gitman::testing::make_function_card_button;
    using gitman::testing::make_function_card_projects;
    using gitman::testing::make_tree;

    constexpr std::u8string_view conflict_notice { u8"문서가 밖에서 바뀌었습니다. 문서 다시 읽기로 불러오십시오." };
    constexpr std::u8string_view reload_failed_notice { u8"문서를 다시 읽지 못해 지금 문서를 그대로 둡니다." };

    [[nodiscard]] gitman::diagnostic make_error(const gitman::diagnostic_code code, const std::u8string_view message)
    {
        gitman::diagnostic value {};
        value.code = code;
        value.severity = gitman::diagnostic_severity::error;
        value.message = message;
        return value;
    }

    // 기능 둘(출력 수집 `build`, 분리 실행 `ide`)을 든 문서를 카드 둘(p00, p01)로 연다.
    struct reload_fixture : function_card_fixture
    {
        reload_fixture()
            : function_card_fixture { make_function_card_projects(2), buttons() }
        {
        }

        [[nodiscard]] static std::vector<gitman::function_button> buttons()
        {
            gitman::function_button build { make_function_card_button(u8"build", u8"빌드") };
            gitman::function_button ide { make_function_card_button(u8"ide", u8"IDE") };
            ide.mode = gitman::function_run_mode::detached;
            return { std::move(build), std::move(ide) };
        }

        // 제출된 요청 중 그 종류의 마지막 것이다. 없으면 null이다.
        [[nodiscard]] const gitman::operation_request* last_request(const gitman::operation_kind kind) const
        {
            const gitman::operation_request* found { nullptr };
            for (const gitman::operation_request& request : submitter.requests)
            {
                if (request.kind == kind)
                    found = &request;
            }
            return found;
        }

        [[nodiscard]] std::size_t request_count(const gitman::operation_kind kind) const
        {
            std::size_t count { 0 };
            for (const gitman::operation_request& request : submitter.requests)
            {
                if (request.kind == kind)
                    ++count;
            }
            return count;
        }

        // 다시 읽기의 load 결과다. `document`가 비면 읽지 못한 것이다.
        void reloaded(std::optional<gitman::workspace_document> document, std::vector<gitman::diagnostic> diagnostics = {})
        {
            const gitman::operation_request* const load { last_request(gitman::operation_kind::load_document) };
            REQUIRE(load != nullptr);
            gitman::document_loaded_event event {};
            event.operation_id = load->operation_id;
            event.document = std::move(document);
            event.diagnostics = std::move(diagnostics);
            controller.handle(std::move(event));
        }

        // 손으로 고친 문서다. 카드 하나(p01)를 빼고 기능 정의는 그대로 둔다.
        [[nodiscard]] static gitman::workspace_document edited_document()
        {
            gitman::workspace_document document {};
            document.document_path = document_path;
            std::vector<gitman::project_definition> projects { make_function_card_projects(2) };
            projects.pop_back();
            document.projects = std::move(projects);
            gitman::function_buttons_definition definition {};
            for (gitman::function_button& button : buttons())
                definition.buttons.emplace_back(std::move(button));
            document.function_buttons = std::move(definition);
            return document;
        }

        void saved(const std::uint64_t operation_id, std::vector<gitman::diagnostic> diagnostics)
        {
            gitman::document_saved_event event {};
            event.operation_id = operation_id;
            event.diagnostics = std::move(diagnostics);
            controller.handle(std::move(event));
        }

        [[nodiscard]] std::optional<gitman::function_confirm_dialog_view> dialog() const
        {
            return snapshot()->function_confirm_dialog;
        }

        void select(const std::u8string_view id)
        {
            controller.handle(gitman::select_card_intent { gitman::project_id { std::u8string { id } } });
        }

        [[nodiscard]] std::size_t log_size(const std::u8string_view id) const
        {
            const gitman::operation_log_buffer* const buffer { controller.card_log(gitman::project_id { std::u8string { id } }) };
            REQUIRE(buffer != nullptr);
            return buffer->records().size();
        }
    };

    [[nodiscard]] bool contains(const std::u8string_view text, const std::u8string_view part)
    {
        return text.find(part) != std::u8string_view::npos;
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

TEST_CASE("Reloading a corrupt document keeps the document, cards and log", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.select(u8"p00");
    fixture.run(u8"p00", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);
    fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::exited, 0);
    const std::size_t records { fixture.log_size(u8"p00") };
    REQUIRE(records == 2u);

    fixture.controller.handle(gitman::reload_document_intent {});
    // 새 파일을 먼저 읽는다. 결과가 오기 전에는 아무것도 비우지 않는다 (4.9의 3).
    const gitman::operation_request* const load { fixture.last_request(gitman::operation_kind::load_document) };
    REQUIRE(load != nullptr);
    REQUIRE(load->document_path == function_card_fixture::document_path);
    REQUIRE(fixture.snapshot()->cards.size() == 2u);
    REQUIRE(fixture.log_size(u8"p00") == records);

    SECTION("JSON 오류")
    {
        fixture.reloaded(std::nullopt, { make_error(gitman::diagnostic_code::malformed_document, u8"작업공간 문서가 유효한 JSON이 아닙니다.") });
    }

    SECTION("파일이 없어짐")
    {
        fixture.reloaded(std::nullopt, { make_error(gitman::diagnostic_code::document_not_found, u8"작업공간 문서를 찾을 수 없습니다.") });
    }

    SECTION("문서는 오지만 project 항목 오류")
    {
        // 열기는 그 project를 빼고 받는 오류다. 다시 읽기는 받지 않는다 — 손 편집의 실수로 카드가 사라지면 안 된다.
        fixture.reloaded(reload_fixture::edited_document(), { make_error(gitman::diagnostic_code::missing_project_field, u8"project path가 필요합니다.") });
        REQUIRE(fixture.snapshot()->notices.front().details.front() == u8"project path가 필요합니다.");
    }

    // 지금 문서·카드·선택·로그가 그대로이고 알림 띠 맨 앞에 실패가 선다 (4.9의 4, B10).
    const auto view { fixture.snapshot() };
    REQUIRE(view->document_path == function_card_fixture::document_path);
    REQUIRE(view->cards.size() == 2u);
    REQUIRE(view->selected.has_value());
    REQUIRE(view->selected->value == u8"p00");
    REQUIRE(fixture.log_size(u8"p00") == records);
    REQUIRE(view->log.has_value());
    REQUIRE(view->log->records.size() == records);
    REQUIRE(view->notices.empty() == false);
    REQUIRE(view->notices.front().text == reload_failed_notice);
    REQUIRE(view->notices.front().details.size() == 1u);
    // 기능은 그대로 실행할 수 있다. 다시 읽기도 다시 할 수 있다.
    fixture.run(u8"p00", u8"build");
    REQUIRE(fixture.functions.requests.size() == 2u);
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
    // 실행 중이라 이번에는 먼저 묻는다.
    REQUIRE(fixture.dialog().has_value());
}

TEST_CASE("A failed reload leaves a running capture alone and a successful one cancels it", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.run(u8"p00", u8"build");
    const gitman::function_run_request request { fixture.functions.requests.front() };

    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.dialog().has_value());
    fixture.controller.handle(gitman::confirm_function_run_intent {});
    // 확인해도 취소는 교체할 때 한다 (4.9의 5).
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    REQUIRE(request.token.cancelled() == false);

    SECTION("읽지 못하면 실행이 남는다")
    {
        fixture.reloaded(std::nullopt, { make_error(gitman::diagnostic_code::malformed_document, u8"JSON 오류") });
        REQUIRE(request.token.cancelled() == false);
        REQUIRE(fixture.card(u8"p00").function_running);
    }

    SECTION("읽으면 실행을 취소하고 카드와 로그를 새로 만든다")
    {
        fixture.reloaded(reload_fixture::edited_document());
        REQUIRE(request.token.cancelled());
        const auto view { fixture.snapshot() };
        REQUIRE(view->cards.size() == 1u);
        REQUIRE(view->cards.front().function_running == false);
        REQUIRE(fixture.log_size(u8"p00") == 0u);
        REQUIRE(view->notices.empty());
        // 모든 카드를 다시 조회한다.
        REQUIRE(fixture.request_count(gitman::operation_kind::query_local) == 1u);
        // 늦게 온 completed는 run id로 버린다.
        fixture.completed(request, gitman::function_run_outcome::cancelled);
        REQUIRE(fixture.log_size(u8"p00") == 0u);
    }
}

TEST_CASE("Reloading, closing or opening another document while a capture runs asks first", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.run(u8"p00", u8"build");
    fixture.run(u8"p01", u8"build");
    REQUIRE(fixture.functions.requests.size() == 2u);
    const gitman::function_run_request first { fixture.functions.requests[0] };
    const gitman::function_run_request second { fixture.functions.requests[1] };
    fixture.started(first, std::chrono::steady_clock::now());

    SECTION("다시 읽기")
    {
        fixture.controller.handle(gitman::reload_document_intent {});
        const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
        REQUIRE(dialog.has_value());
        REQUIRE(dialog->mode == gitman::function_confirm_mode::running);
        // 개수와 이름이다 (4.10).
        REQUIRE(dialog->subject == u8"빌드 (p00) 외 1개");
        REQUIRE(dialog->items.size() == 2u);
        REQUIRE(dialog->items[0].label == u8"빌드 (p00)");
        REQUIRE(dialog->items[0].mode_text == u8"실행 중");
        REQUIRE(dialog->items[1].label == u8"빌드 (p01)");
        REQUIRE(dialog->items[1].mode_text == u8"시작 대기");
        REQUIRE(dialog->items[0].lines.front().text == u8"\"C:\\Tools\\run.exe\"");
        REQUIRE(dialog->confirm_label == u8"중지하고 읽기");
        REQUIRE(dialog->exit_application == false);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);

        // 취소하면 아무것도 하지 않는다.
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
        REQUIRE(first.token.cancelled() == false);
    }

    SECTION("문서 닫기")
    {
        fixture.controller.handle(gitman::close_document_intent {});
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->confirm_label == u8"중지하고 닫기");
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);

        fixture.controller.handle(gitman::cancel_function_run_intent {});
        REQUIRE(fixture.snapshot()->cards.size() == 2u);
        REQUIRE(first.token.cancelled() == false);

        fixture.controller.handle(gitman::close_document_intent {});
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.snapshot()->document_path.empty());
        REQUIRE(first.token.cancelled());
        REQUIRE(second.token.cancelled());
    }

    SECTION("다른 문서 열기")
    {
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->confirm_label == u8"중지하고 열기");
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);

        fixture.controller.handle(gitman::confirm_function_run_intent {});
        const gitman::operation_request* const load { fixture.last_request(gitman::operation_kind::load_document) };
        REQUIRE(load != nullptr);
        REQUIRE(load->document_path == u8"C:\\work\\other.version-list");
        REQUIRE(first.token.cancelled());
    }

    SECTION("새 문서 만들기 뒤 열기")
    {
        fixture.controller.handle(gitman::generate_document_intent { u8"C:\\work", u8"C:\\work\\new.version-list" });
        const gitman::operation_request* const generate { fixture.last_request(gitman::operation_kind::generate_document) };
        REQUIRE(generate != nullptr);
        gitman::document_generated_event event {};
        event.operation_id = generate->operation_id;
        event.document_path = u8"C:\\work\\new.version-list";
        event.document = { gitman::workspace_document {} };
        event.revision = { gitman::workspace_revision_token {} };
        fixture.controller.handle(std::move(event));

        // 만든 문서는 파일에 있으므로, 확인하면 그 경로를 다시 읽어 연다.
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        const gitman::operation_request* const load { fixture.last_request(gitman::operation_kind::load_document) };
        REQUIRE(load != nullptr);
        REQUIRE(load->document_path == u8"C:\\work\\new.version-list");
        REQUIRE(first.token.cancelled());
    }

    SECTION("새 문서 만들기 뒤 열기를 취소")
    {
        fixture.controller.handle(gitman::generate_document_intent { u8"C:\\work", u8"C:\\work\\new.version-list" });
        gitman::document_generated_event event {};
        event.operation_id = fixture.last_request(gitman::operation_kind::generate_document)->operation_id;
        event.document_path = u8"C:\\work\\new.version-list";
        event.document = { gitman::workspace_document {} };
        event.revision = { gitman::workspace_revision_token {} };
        fixture.controller.handle(std::move(event));
        REQUIRE(fixture.dialog().has_value());

        // 생성 dialog는 닫혔고 파일은 있으므로 그 경로를 알린다. 지금 문서와 실행은 그대로다.
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        const auto view { fixture.snapshot() };
        REQUIRE(view->function_confirm_dialog.has_value() == false);
        REQUIRE(view->document_path == function_card_fixture::document_path);
        REQUIRE(view->notices.size() == 1u);
        REQUIRE(view->notices.front().text == u8"새 문서를 만들었지만 열지 않았습니다.");
        REQUIRE(view->notices.front().details == std::vector<std::u8string> { u8"C:\\work\\new.version-list" });
        REQUIRE(first.token.cancelled() == false);
    }
}

TEST_CASE("Only detached runs need no confirmation", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.run(u8"p00", u8"ide");
    REQUIRE(fixture.functions.requests.size() == 1u);
    REQUIRE(fixture.card(u8"p00").function_running == false);
    REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);

    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);

    fixture.controller.handle(gitman::close_document_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.snapshot()->document_path.empty());
}

TEST_CASE("Reload waits for a save in progress and holds saves while it reads", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    // 카드 순서 변경이 저장을 낸다.
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    const gitman::operation_request* const save { fixture.last_request(gitman::operation_kind::save_document) };
    REQUIRE(save != nullptr);
    const std::uint64_t save_id { save->operation_id };

    fixture.controller.handle(gitman::reload_document_intent {});
    // 진행 중인 저장이 끝난 뒤 읽는다 (4.9의 2).
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
    fixture.saved(save_id, {});
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);

    // 읽는 동안 들어온 변경은 저장을 미룬다. 지금 모델은 읽은 파일과 어긋난다.
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, false });
    REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 1u);

    SECTION("읽으면 미룬 저장을 버린다")
    {
        fixture.reloaded(reload_fixture::edited_document());
        REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 1u);
    }

    SECTION("읽지 못하면 미룬 저장을 이어 낸다")
    {
        fixture.reloaded(std::nullopt, { make_error(gitman::diagnostic_code::malformed_document, u8"JSON 오류") });
        REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 2u);
    }
}

TEST_CASE("A save conflict points to reload and a reload recovers saving", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    const gitman::operation_request* const save { fixture.last_request(gitman::operation_kind::save_document) };
    REQUIRE(save != nullptr);

    // 손 편집 뒤의 저장은 낙관적 잠금에 걸린다. 다시 읽기로 불러오라고 알린다 (4.9).
    fixture.saved(save->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, u8"load 이후 작업공간 문서가 변경되어 저장하지 않았습니다.") });
    REQUIRE(fixture.snapshot()->notices.front().text == conflict_notice);

    // 다른 저장 실패는 그 진단 그대로다.
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, false });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
    REQUIRE(fixture.snapshot()->notices.front().text == u8"쓰지 못했습니다.");

    // 다시 읽으면 알림이 걷히고 다음 변경이 읽은 revision으로 저장된다.
    fixture.controller.handle(gitman::reload_document_intent {});
    fixture.reloaded(reload_fixture::edited_document());
    REQUIRE(fixture.snapshot()->notices.empty());
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::toggle_path_display_intent {});
    const gitman::operation_request* const recovered { fixture.last_request(gitman::operation_kind::save_document) };
    REQUIRE(recovered != nullptr);
    REQUIRE(recovered->document.has_value());
    REQUIRE(recovered->document->projects.size() == 1u);
}

TEST_CASE("A failed reload after a save conflict shows the failure first", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, u8"load 이후 작업공간 문서가 변경되어 저장하지 않았습니다.") });
    REQUIRE(fixture.snapshot()->notices.front().text == conflict_notice);

    // 충돌 안내를 따라 다시 읽었으나 파일이 아직 깨져 있다. 띠 첫 줄이 그 실패여야 한다 — 충돌 안내가 남으면 아무 일도
    // 없었던 것처럼 보인다.
    fixture.controller.handle(gitman::reload_document_intent {});
    fixture.reloaded(std::nullopt, { make_error(gitman::diagnostic_code::malformed_document, u8"작업공간 문서가 유효한 JSON이 아닙니다.") });
    REQUIRE(fixture.snapshot()->notices.size() == 1u);
    REQUIRE(fixture.snapshot()->notices.front().text == reload_failed_notice);

    // 그 뒤 저장이 다시 충돌하면 안내가 다시 선다.
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, false });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, u8"load 이후 작업공간 문서가 변경되어 저장하지 않았습니다.") });
    REQUIRE(fixture.snapshot()->notices.front().text == conflict_notice);
}

TEST_CASE("A reload result after shutdown began changes nothing", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reload_document_intent {});
    const gitman::operation_request* const load { fixture.last_request(gitman::operation_kind::load_document) };
    REQUIRE(load != nullptr);
    const std::uint64_t reload_id { load->operation_id };
    fixture.controller.handle(gitman::close_intent {});
    fixture.submitter.requests.clear();
    // 종료가 시작된 뒤 도착한 결과로 문서를 바꾸거나 조회를 내지 않는다.
    gitman::document_loaded_event event {};
    event.operation_id = reload_id;
    event.document = { reload_fixture::edited_document() };
    fixture.controller.handle(std::move(event));
    REQUIRE(fixture.snapshot()->cards.size() == 2u);
    REQUIRE(fixture.request_count(gitman::operation_kind::refresh) == 0u);
}

TEST_CASE("A dirty window placement survives a reload", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    const gitman::window_placement moved { 40, 50, 900, 700, false };
    fixture.controller.handle(gitman::window_placement_intent { moved });

    gitman::workspace_document document { reload_fixture::edited_document() };
    const gitman::window_placement in_file { 10, 20, 640, 480, false };
    document.window = in_file;

    SECTION("저장하지 않은 배치")
    {
        fixture.controller.handle(gitman::reload_document_intent {});
        fixture.reloaded(std::move(document));

        // 화면의 배치를 그대로 둔다. 파일의 배치로 창을 옮기지 않는다.
        const auto view { fixture.snapshot() };
        REQUIRE(view->window_placement_request == std::optional<gitman::window_placement> { moved });
        // dirty가 남아 닫을 때 새 모델에 저장한다 (4.9의 6).
        fixture.submitter.requests.clear();
        fixture.controller.handle(gitman::close_document_intent {});
        const gitman::operation_request* const save { fixture.last_request(gitman::operation_kind::save_document) };
        REQUIRE(save != nullptr);
        REQUIRE(save->document.has_value());
        REQUIRE(save->document->window == std::optional<gitman::window_placement> { moved });
        REQUIRE(save->document->projects.size() == 1u);
    }

    SECTION("이미 저장한 배치면 파일의 배치를 따른다")
    {
        // 닫기·종료에서만 저장하므로 dirty를 지우는 길은 문서를 다시 여는 것이다. 같은 배치를 다시 알리면 dirty가
        // 서지 않는다.
        fixture.controller.handle(gitman::open_document_intent { std::u8string { function_card_fixture::document_path } });
        gitman::document_loaded_event opened {};
        gitman::workspace_document original { reload_fixture::edited_document() };
        original.window = moved;
        opened.document = { std::move(original) };
        fixture.controller.handle(std::move(opened));
        fixture.controller.handle(gitman::window_placement_intent { moved });

        fixture.submitter.requests.clear();
        fixture.controller.handle(gitman::reload_document_intent {});
        fixture.reloaded(std::move(document));
        REQUIRE(fixture.snapshot()->window_placement_request == std::optional<gitman::window_placement> { in_file });
        fixture.submitter.requests.clear();
        fixture.controller.handle(gitman::close_document_intent {});
        REQUIRE(fixture.last_request(gitman::operation_kind::save_document) == nullptr);
    }
}

TEST_CASE("A reload abandoned by opening another document is dropped", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reload_document_intent {});
    const gitman::operation_request* const reload { fixture.last_request(gitman::operation_kind::load_document) };
    REQUIRE(reload != nullptr);
    const std::uint64_t reload_id { reload->operation_id };

    fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
    // 버린 다시 읽기의 결과가 먼저 와도 새 문서로 설치하지 않는다.
    gitman::document_loaded_event late {};
    late.operation_id = reload_id;
    late.document = { reload_fixture::edited_document() };
    fixture.controller.handle(std::move(late));
    REQUIRE(fixture.snapshot()->cards.empty());
    REQUIRE(fixture.snapshot()->document_path == u8"C:\\work\\other.version-list");

    // 새 문서의 결과는 그대로 연다.
    gitman::document_loaded_event opened {};
    opened.operation_id = fixture.last_request(gitman::operation_kind::load_document)->operation_id;
    gitman::workspace_document other {};
    other.document_path = u8"C:\\work\\other.version-list";
    other.projects = make_function_card_projects(3);
    opened.document = { std::move(other) };
    fixture.controller.handle(std::move(opened));
    REQUIRE(fixture.snapshot()->cards.size() == 3u);
}

TEST_CASE("The banner and function menus reload the document", "[function_buttons][t11][context-menu]")
{
    reload_fixture fixture {};

    SECTION("배너 메뉴")
    {
        fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.items.size() == 3u);
        REQUIRE(menu.items[2].entry == gitman::context_menu_entry::reload_document);
        REQUIRE(menu.items[2].label == u8"문서 다시 읽기");
        REQUIRE(menu.items[2].icon == u8"sync");
        REQUIRE(menu.items[2].enabled);
        const luil::win32::ui_popup popup { gitman::ui::build_context_menu_popup(menu, 1.0f) };
        REQUIRE(popup.height == 86.0f);
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::reload_document, {} });
    }

    SECTION("기능 메뉴")
    {
        fixture.controller.handle(gitman::open_function_menu_intent { gitman::project_id { u8"p00" }, 10.0f, 20.0f });
        const gitman::context_menu_view menu { fixture.menu() };
        REQUIRE(menu.items.back().entry == gitman::context_menu_entry::reload_document);
        REQUIRE(menu.items.back().enabled);
        fixture.controller.handle(gitman::select_context_menu_item_intent { gitman::context_menu_entry::reload_document, {} });
    }

    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    // 읽는 동안은 항목이 비활성이고, 늦은 선택도 두 번째 읽기를 내지 않는다.
    fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });
    REQUIRE(fixture.menu().items[2].enabled == false);
    // 비활성 사유는 라벨 접미다 (2.3.2).
    REQUIRE(fixture.menu().items[2].label == u8"문서 다시 읽기 — 읽는 중");
    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
}

TEST_CASE("Exit asks while a capture runs and its confirm button ends the application", "[function_buttons][t11][ui]")
{
    reload_fixture fixture {};
    REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
    fixture.run(u8"p00", u8"build");
    REQUIRE(fixture.snapshot()->exit_needs_confirmation);

    fixture.controller.handle(gitman::request_exit_intent {});
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->mode == gitman::function_confirm_mode::running);
    REQUIRE(dialog->exit_application);
    REQUIRE(dialog->subject == u8"빌드 (p00)");
    REQUIRE(dialog->confirm_label == u8"중지하고 종료");
    REQUIRE(dialog->note.size() == 1u);

    const auto tree { make_tree(*fixture.snapshot()) };
    const luil::ui_element* const host { gitman::testing::find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_function_confirm) };
    REQUIRE(host != nullptr);
    REQUIRE(host->accessibility().name == u8"실행 중인 기능");
    const luil::ui_element* const panel { gitman::testing::find(*tree, gitman::ui::kind_function_confirm_dialog_panel) };
    REQUIRE(panel != nullptr);
    REQUIRE(panel->accessibility().name.starts_with(u8"실행 중인 기능 확인, 빌드 (p00)"));
    REQUIRE(contains(panel->accessibility().name, u8"종료하면 실행 중인 기능을 중지합니다."));

    // 확인은 UI thread의 앱 끝내기 명령이다. logic을 거치지 않는다 — 닫힘 훅을 비켜 기존 종료 순서를 밟는다.
    const std::vector<luil::input_action> confirm { click_action(*tree, id_of(gitman::ui::kind_function_confirm_dialog_confirm)) };
    REQUIRE(confirm.size() == 1u);
    const luil::ui_command* const command { std::get_if<luil::ui_command>(&confirm.front()) };
    REQUIRE(command != nullptr);
    REQUIRE(*command == luil::ui_command::application_exit);
    // 취소는 dialog만 닫는다. 실행은 계속된다.
    REQUIRE(gitman::testing::as_intent<gitman::cancel_function_run_intent>(click_action(*tree, id_of(gitman::ui::kind_function_confirm_dialog_cancel))) != nullptr);
    fixture.controller.handle(gitman::cancel_function_run_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.functions.requests.front().token.cancelled() == false);

    SECTION("훅을 건 뒤 기능이 끝났으면 끝났다고 알리고 종료한다")
    {
        fixture.completed(fixture.functions.requests.front(), gitman::function_run_outcome::exited, 0);
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
        fixture.controller.handle(gitman::request_exit_intent {});
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->items.empty());
        REQUIRE(fixture.dialog()->subject == u8"실행 중인 기능이 없습니다");
        // 안내는 대상 줄을 되풀이하지 않는다.
        REQUIRE(fixture.dialog()->description == std::vector<std::u8string> { u8"그사이 기능이 끝나 바로 종료할 수 있습니다." });
        REQUIRE(fixture.dialog()->confirm_label == u8"종료");
    }

    SECTION("다른 dialog의 확인은 logic intent 그대로다")
    {
        fixture.controller.handle(gitman::close_document_intent {});
        const auto closing { make_tree(*fixture.snapshot()) };
        REQUIRE(gitman::testing::as_intent<gitman::confirm_function_run_intent>(click_action(*closing, id_of(gitman::ui::kind_function_confirm_dialog_confirm))) != nullptr);
    }
}
