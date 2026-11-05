#include "helpers/function_card_fixture.h"
#include "helpers/ui_test_support.h"

#include "application/logic_controller.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/context_menu_popup.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_confirm_dialog_element.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
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

    struct revision_probe : gitman::project_store
    {
        static gitman::workspace_revision_token first(const std::u8string& path)
        {
            return make_revision_token(revision_file_state::present, path, u8"first saved bytes", u8"{}", {});
        }

        static std::u8string_view bytes(const gitman::workspace_revision_token& token)
        {
            const auto value { inspect_revision(token) };
            return value.has_value() ? value->expected_source_bytes : std::u8string_view {};
        }
    };

    // 저장하지 않았다는 것을 밝힌다 (D28, user-manual 18장).
    constexpr std::u8string_view conflict_notice { u8"문서가 밖에서 바뀌어 저장하지 않았습니다. 문서 다시 읽기로 불러오십시오." };
    constexpr std::u8string_view conflict_message { u8"load 이후 작업공간 문서가 변경되어 저장하지 않았습니다." };
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

        // 성공한 저장이다. 새 revision이 와야 성공이다 — revision 없는 결과는 실패로 본다.
        void saved_ok(const std::uint64_t operation_id)
        {
            gitman::document_saved_event event {};
            event.operation_id = operation_id;
            event.revision = { gitman::workspace_revision_token {} };
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
    fixture.saved_ok(save_id);
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

TEST_CASE("A save conflict points to reload and a reload recovers saving", "[function_buttons][t11][r2]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    const gitman::operation_request* const save { fixture.last_request(gitman::operation_kind::save_document) };
    REQUIRE(save != nullptr);

    // 손 편집 뒤의 저장은 낙관적 잠금에 걸린다. 저장하지 않았다는 것과 다시 읽기로 불러오는 길을 알린다 (4.9, D28).
    fixture.saved(save->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });
    REQUIRE(fixture.snapshot()->notices.front().text == conflict_notice);

    // 다른 저장 실패는 그 진단 그대로다.
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, false });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
    REQUIRE(fixture.snapshot()->notices.front().text == u8"쓰지 못했습니다.");

    // 다시 읽으면 저장하지 못한 카드 순서가 사라지므로 먼저 묻는다 (D28). 확인해야 읽는다.
    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->unsaved_changes);
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
    fixture.controller.handle(gitman::confirm_function_run_intent {});
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);

    // 다시 읽으면 알림이 걷히고 다음 변경이 읽은 revision으로 저장된다.
    fixture.reloaded(reload_fixture::edited_document());
    REQUIRE(fixture.snapshot()->notices.empty());
    fixture.submitter.requests.clear();
    fixture.controller.handle(gitman::toggle_path_display_intent {});
    const gitman::operation_request* const recovered { fixture.last_request(gitman::operation_kind::save_document) };
    REQUIRE(recovered != nullptr);
    REQUIRE(recovered->document.has_value());
    REQUIRE(recovered->document->projects.size() == 1u);
}

TEST_CASE("Reloading over an unsaved change asks first and cancelling keeps everything", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });

    fixture.controller.handle(gitman::reload_document_intent {});
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    // 실행 중인 기능 확인과 같은 틀이다. 실행 중인 것이 없으면 목록은 비고 저장하지 못한 변경만 말한다.
    REQUIRE(dialog->mode == gitman::function_confirm_mode::running);
    REQUIRE(dialog->unsaved_changes);
    REQUIRE(dialog->items.empty());
    REQUIRE(dialog->subject == u8"저장하지 못한 변경이 있습니다");
    // 사라진다는 말은 경고 줄이고, 안내 줄에는 읽지 못했을 때의 일만 남는다 (R2 검토).
    REQUIRE(dialog->description == std::vector<std::u8string> { u8"읽지 못하면 지금 문서와 변경을 그대로 둡니다." });
    REQUIRE(dialog->note == std::vector<std::u8string> { u8"다시 읽은 문서로 바꾸면 저장하지 못한 변경이 사라집니다." });
    REQUIRE(dialog->confirm_label == u8"버리고 읽기");
    REQUIRE(dialog->exit_application == false);

    // 취소하면 읽지 않고 모델·알림·저장하지 못한 상태가 그대로다. 다시 읽기를 다시 하면 또 묻는다.
    fixture.controller.handle(gitman::cancel_function_run_intent {});
    const auto view { fixture.snapshot() };
    REQUIRE(view->function_confirm_dialog.has_value() == false);
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
    REQUIRE(view->notices.front().text == conflict_notice);
    REQUIRE(view->cards.size() == 2u);
    REQUIRE(view->cards.front().id.value == u8"p01");
    REQUIRE(view->exit_needs_confirmation);
    fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });
    REQUIRE(fixture.menu().items[2].enabled);
    fixture.controller.handle(gitman::close_context_menu_intent {});
    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.dialog().has_value());
    fixture.controller.handle(gitman::cancel_function_run_intent {});

    // 저장 실패는 기능 실행을 막지 않는다. 카드 순서는 신뢰 해시 입력이 아니다 — 편집 dialog의 Apply만 막는다.
    fixture.run(u8"p00", u8"ide");
    REQUIRE(fixture.functions.requests.size() == 1u);

    SECTION("읽지 못하면 변경이 남아 다음 다시 읽기도 묻는다")
    {
        fixture.controller.handle(gitman::reload_document_intent {});
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        fixture.reloaded(std::nullopt, { make_error(gitman::diagnostic_code::malformed_document, u8"JSON 오류") });
        REQUIRE(fixture.snapshot()->cards.front().id.value == u8"p01");
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->unsaved_changes);
    }

    SECTION("다시 읽으면 상태가 풀려 다음 다시 읽기는 묻지 않는다")
    {
        fixture.controller.handle(gitman::reload_document_intent {});
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        fixture.reloaded(reload_fixture::edited_document());
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 2u);
    }

    SECTION("뒤의 저장이 성공하면 묻지 않는다")
    {
        // 쓰기 실패처럼 다시 저장해 회복하는 경우다(logic은 실패 사유를 가리지 않는다). 성공한 저장은 모델 전체를 쓰므로
        // 앞서 저장하지 못한 변경도 실었다.
        fixture.controller.handle(gitman::toggle_path_display_intent {});
        fixture.saved_ok(fixture.last_request(gitman::operation_kind::save_document)->operation_id);
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    }
}

TEST_CASE("A reload after a successful save does not ask", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::set_function_placement_intent { u8"build", gitman::function_placement::bar });
    fixture.saved_ok(fixture.last_request(gitman::operation_kind::save_document)->operation_id);

    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
}

TEST_CASE("A reload waiting for a save asks only when that save fails", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    // 배치 변경도 문서를 쓰는 변경이다. 저장이 진행 중이면 아직 실패하지 않았으므로 묻지 않고 기다린다 (4.9의 2).
    fixture.controller.handle(gitman::set_function_placement_intent { u8"build", gitman::function_placement::bar });
    const std::uint64_t save_id { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
    fixture.controller.handle(gitman::reload_document_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
    // 저장이 진행 중이어도 창 닫기를 받아 그 결과를 기다린다 (S3, 4.10).
    REQUIRE(fixture.snapshot()->exit_needs_confirmation);

    SECTION("저장이 성공하면 곧바로 읽는다")
    {
        fixture.saved_ok(save_id);
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    }

    SECTION("저장이 실패하면 읽지 않고 묻는다")
    {
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->unsaved_changes);
        // 기다림이 풀려 메뉴가 `읽는 중`에 묶이지 않는다. 취소하면 다시 고를 수 있다.
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });
        REQUIRE(fixture.menu().items[2].enabled);
        REQUIRE(fixture.menu().items[2].label == u8"문서 다시 읽기");
        fixture.controller.handle(gitman::close_context_menu_intent {});

        fixture.controller.handle(gitman::reload_document_intent {});
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    }
}

TEST_CASE("A running capture and an unsaved change share one dialog", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    fixture.run(u8"p00", u8"build");
    const gitman::function_run_request request { fixture.functions.requests.front() };
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });

    fixture.controller.handle(gitman::reload_document_intent {});
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->unsaved_changes);
    REQUIRE(dialog->items.size() == 1u);
    REQUIRE(dialog->subject == u8"빌드 (p00)");
    REQUIRE(dialog->confirm_label == u8"중지하고 읽기");
    REQUIRE(dialog->note == std::vector<std::u8string> { u8"중지하면 끝나기를 기다리지 않고 바로 끝냅니다.", u8"저장하지 못한 변경도 사라집니다." });
    // 읽지 못하면 변경도 남는다는 것을 단독 dialog처럼 밝힌다 (R2 검토).
    REQUIRE(dialog->description == std::vector<std::u8string> { u8"다시 읽은 문서로 바꿀 때 실행 중인 기능을 중지합니다.", u8"읽지 못하면 중지하지 않고 지금 문서와 변경을 둡니다." });

    // 한 번 확인하면 읽는다. 교체할 때 실행을 취소한다.
    fixture.controller.handle(gitman::confirm_function_run_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    fixture.reloaded(reload_fixture::edited_document());
    REQUIRE(request.token.cancelled());
}

TEST_CASE("Closing, opening another document or exiting over an unsaved change asks first", "[function_buttons][r2][ui]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    // 저장이 진행 중이어도 창 닫기를 받아 그 결과를 기다린다 (S3, 4.10).
    REQUIRE(fixture.snapshot()->exit_needs_confirmation);
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
    // 창 닫기도 같은 확인을 거친다. driver가 닫힘 훅을 건다.
    REQUIRE(fixture.snapshot()->exit_needs_confirmation);

    SECTION("문서 닫기")
    {
        fixture.controller.handle(gitman::close_document_intent {});
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->description.empty());
        REQUIRE(fixture.dialog()->note == std::vector<std::u8string> { u8"문서를 닫으면 저장하지 못한 변경이 사라집니다." });
        REQUIRE(fixture.dialog()->confirm_label == u8"버리고 닫기");
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);

        fixture.controller.handle(gitman::close_document_intent {});
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.snapshot()->document_path.empty());
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
    }

    SECTION("다른 문서 열기")
    {
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->note == std::vector<std::u8string> { u8"다른 문서를 열면 저장하지 못한 변경이 사라집니다." });
        REQUIRE(fixture.dialog()->confirm_label == u8"버리고 열기");
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.last_request(gitman::operation_kind::load_document)->document_path == u8"C:\\work\\other.version-list");
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
    }

    SECTION("새 문서 만들기의 결과 열기")
    {
        // 만든 문서를 여는 길도 다른 문서 열기와 같은 확인을 거친다 (4.10). 취소하면 지금 문서와 변경이 남는다.
        fixture.controller.handle(gitman::generate_document_intent { u8"C:\\work", u8"C:\\work\\new.version-list" });
        gitman::document_generated_event event {};
        event.operation_id = fixture.last_request(gitman::operation_kind::generate_document)->operation_id;
        event.document_path = u8"C:\\work\\new.version-list";
        event.document = { gitman::workspace_document {} };
        event.revision = { gitman::workspace_revision_token {} };
        fixture.controller.handle(std::move(event));
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->items.empty());
        REQUIRE(fixture.dialog()->note == std::vector<std::u8string> { u8"다른 문서를 열면 저장하지 못한 변경이 사라집니다." });
        REQUIRE(fixture.dialog()->confirm_label == u8"버리고 열기");

        SECTION("확인")
        {
            fixture.controller.handle(gitman::confirm_function_run_intent {});
            REQUIRE(fixture.last_request(gitman::operation_kind::load_document)->document_path == u8"C:\\work\\new.version-list");
            REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
        }

        SECTION("취소")
        {
            fixture.controller.handle(gitman::cancel_function_run_intent {});
            REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
            REQUIRE(fixture.snapshot()->cards.front().id.value == u8"p01");
            REQUIRE(fixture.snapshot()->exit_needs_confirmation);
        }
    }

    SECTION("앱 종료")
    {
        fixture.controller.handle(gitman::request_exit_intent {});
        const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
        REQUIRE(dialog.has_value());
        REQUIRE(dialog->exit_application);
        REQUIRE(dialog->unsaved_changes);
        REQUIRE(dialog->subject == u8"저장하지 못한 변경이 있습니다");
        REQUIRE(dialog->description.empty());
        REQUIRE(dialog->note == std::vector<std::u8string> { u8"종료하면 저장하지 못한 변경이 사라집니다." });
        REQUIRE(dialog->confirm_label == u8"버리고 종료");

        // 제목과 화면 읽기 이름은 실행 중인 기능이 아니라 저장하지 못한 변경을 말한다. 경고 줄도 이름에 든다.
        const auto tree { make_tree(*fixture.snapshot()) };
        const luil::ui_element* const host { gitman::testing::find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_function_confirm) };
        REQUIRE(host != nullptr);
        REQUIRE(host->accessibility().name == u8"저장하지 못한 변경");
        const luil::ui_element* const panel { gitman::testing::find(*tree, gitman::ui::kind_function_confirm_dialog_panel) };
        REQUIRE(panel != nullptr);
        REQUIRE(panel->accessibility().name == u8"저장하지 못한 변경 확인, 저장하지 못한 변경이 있습니다 종료하면 저장하지 못한 변경이 사라집니다.");
        // 목록이 없어 빈 목록 자리 없이 대상·경고 줄과 버튼만 담는 높이다 (480 고정이 아님, R2 검토). 버튼은 panel 안이다.
        const luil::rect_f box { panel->bounds() };
        REQUIRE(box.height == gitman::function_confirm_list_top(1) + gitman::function_confirm_list_bottom(1));
        REQUIRE(box.height < gitman::layout_function_confirm_dialog_height);
        const luil::ui_element* const button { tree->find(id_of(gitman::ui::kind_function_confirm_dialog_confirm)) };
        REQUIRE(button != nullptr);
        REQUIRE(button->bounds().y > box.y + gitman::function_confirm_list_top(1));
        REQUIRE(button->bounds().y + button->bounds().height <= box.y + box.height);
        const std::vector<luil::input_action> confirm { click_action(*tree, id_of(gitman::ui::kind_function_confirm_dialog_confirm)) };
        REQUIRE(confirm.size() == 1u);
        const luil::ui_command* const command { std::get_if<luil::ui_command>(&confirm.front()) };
        REQUIRE(command != nullptr);
        REQUIRE(*command == luil::ui_command::application_exit);
    }
}

TEST_CASE("A queued save keeps the unsaved mark until the last save ends", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    // 앞 저장(배치)이 진행 중일 때 낸 변경(카드 순서)은 대기열에 선다. 앞 저장의 결과는 뒤 변경을 싣지 않았다.
    fixture.controller.handle(gitman::set_function_placement_intent { u8"build", gitman::function_placement::bar });
    const std::uint64_t first { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 1u);

    SECTION("앞 저장이 성공해도 뒤 저장이 끝날 때까지 남는다")
    {
        fixture.saved_ok(first);
        // 뒤 저장이 이어 나가 진행 중이다. 아직 실패하지 않았으므로 묻지 않는다.
        REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 2u);
        // 저장이 진행 중이어도 창 닫기를 받아 그 결과를 기다린다 (S3, 4.10).
        REQUIRE(fixture.snapshot()->exit_needs_confirmation);
        fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });
        REQUIRE(fixture.snapshot()->exit_needs_confirmation);
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->unsaved_changes);
    }

    SECTION("앞 저장이 쓰기에 실패해도 이어 낸 저장이 성공하면 풀린다")
    {
        fixture.saved(first, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
        REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 2u);
        // 저장이 진행 중이어도 창 닫기를 받아 그 결과를 기다린다 (S3, 4.10).
        REQUIRE(fixture.snapshot()->exit_needs_confirmation);
        // 이어 낸 저장은 모델 전체를 쓰므로 앞서 실패한 배치도 실었다.
        fixture.saved_ok(fixture.last_request(gitman::operation_kind::save_document)->operation_id);
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    }
}

TEST_CASE("A successful project registration clears an unsaved change it wrote", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
    REQUIRE(fixture.snapshot()->exit_needs_confirmation);

    // 탐색 후보 등록은 `request_save`를 거치지 않고 지금 모델 전체를 쓴다.
    fixture.controller.handle(gitman::begin_discovery_intent { u8"C:\\work\\scan" });
    gitman::discovery_completed_event scanned {};
    scanned.operation_id = fixture.submitter.requests.back().operation_id;
    scanned.result.completed = true;
    gitman::discovery_candidate candidate {};
    candidate.directory_name = u8"repo-a";
    candidate.absolute_path = u8"C:\\work\\scan\\repo-a";
    candidate.normalized_path = candidate.absolute_path;
    candidate.kind = gitman::repository_kind::git;
    scanned.result.candidates.push_back(std::move(candidate));
    fixture.controller.handle(std::move(scanned));
    fixture.controller.handle(gitman::confirm_discovery_intent {});
    const gitman::operation_request* const registration { fixture.last_request(gitman::operation_kind::register_projects) };
    REQUIRE(registration != nullptr);
    REQUIRE(registration->document.has_value());
    REQUIRE(registration->document->projects.front().id.value == u8"p01");

    gitman::projects_registered_event registered {};
    registered.operation_id = registration->operation_id;

    SECTION("성공하면 앞서 저장하지 못한 변경도 파일에 있어 묻지 않는다")
    {
        registered.document = registration->document;
        registered.revision = { gitman::workspace_revision_token {} };
        fixture.controller.handle(std::move(registered));
        REQUIRE(fixture.snapshot()->discovery_dialog.has_value() == false);
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 1u);
    }

    SECTION("실패하면 남는다")
    {
        registered.diagnostics.push_back(make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다."));
        fixture.controller.handle(std::move(registered));
        REQUIRE(fixture.snapshot()->exit_needs_confirmation);
    }
}

TEST_CASE("An open confirmation picks up a save that fails behind it", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    fixture.run(u8"p00", u8"build");
    REQUIRE(fixture.functions.requests.size() == 1u);

    SECTION("저장 중에 연 닫기 확인에 경고 줄이 붙는다")
    {
        fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
        const std::uint64_t save_id { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
        // 저장이 진행 중이라 실행 중인 기능만 묻는다.
        fixture.controller.handle(gitman::close_document_intent {});
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->unsaved_changes == false);
        REQUIRE(fixture.dialog()->note.size() == 1u);

        // 그 저장이 실패하면 확인 전에 변경도 사라진다는 것을 보인다. 같은 일(닫기)이다.
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });
        const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
        REQUIRE(dialog.has_value());
        REQUIRE(dialog->unsaved_changes);
        REQUIRE(dialog->confirm_label == u8"중지하고 닫기");
        REQUIRE(dialog->note == std::vector<std::u8string> { u8"중지하면 끝나기를 기다리지 않고 바로 끝냅니다.", u8"저장하지 못한 변경도 사라집니다." });
    }

    SECTION("저장을 기다리던 다시 읽기는 떠 있는 확인을 바꾸지 않고 거둔다")
    {
        // 실행 중인 기능을 확인한 다시 읽기가 진행 중인 저장을 기다린다 (4.9의 2).
        fixture.controller.handle(gitman::set_function_placement_intent { u8"build", gitman::function_placement::bar });
        const std::uint64_t save_id { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog().has_value());
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);

        // 기다리는 동안 닫기 확인을 열었다. 그 저장이 실패해도 다시 읽기 확인으로 바꾸지 않는다.
        fixture.controller.handle(gitman::close_document_intent {});
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->confirm_label == u8"중지하고 닫기");
        REQUIRE(fixture.dialog()->unsaved_changes);
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);

        // 다시 읽기는 거둬 메뉴가 `읽는 중`에 묶이지 않고, 저장 실패 알림이 남아 다시 고를 수 있다.
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        REQUIRE(fixture.snapshot()->notices.front().text == conflict_notice);
        fixture.controller.handle(gitman::open_document_context_menu_intent { 40.0f, 50.0f });
        REQUIRE(fixture.menu().items[2].label == u8"문서 다시 읽기");
        REQUIRE(fixture.menu().items[2].enabled);
        fixture.controller.handle(gitman::close_context_menu_intent {});
        fixture.controller.handle(gitman::reload_document_intent {});
        REQUIRE(fixture.dialog()->confirm_label == u8"중지하고 읽기");
        REQUIRE(fixture.dialog()->unsaved_changes);
    }
}

TEST_CASE("An exit confirmation opened during a save turns into the unsaved one when it fails", "[function_buttons][r2]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
    // driver가 닫힘 훅을 건 frame 뒤에 다시 저장이 나갔다. 그 사이의 종료 요청은 실행 중인 것도, 아직 실패한 저장도 없다.
    REQUIRE(fixture.snapshot()->exit_needs_confirmation);
    fixture.controller.handle(gitman::toggle_path_display_intent {});
    const std::uint64_t save_id { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
    fixture.controller.handle(gitman::request_exit_intent {});
    REQUIRE(fixture.dialog().has_value());
    // 저장이 끝나기를 기다리는 동안은 종료를 누를 수 없다 (S3, 4.10).
    REQUIRE(fixture.dialog()->subject == u8"문서를 저장하는 중입니다");
    REQUIRE(fixture.dialog()->confirm_enabled == false);

    fixture.saved(save_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
    const std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->exit_application);
    REQUIRE(dialog->subject == u8"저장하지 못한 변경이 있습니다");
    REQUIRE(dialog->note == std::vector<std::u8string> { u8"종료하면 저장하지 못한 변경이 사라집니다." });
    REQUIRE(dialog->confirm_label == u8"버리고 종료");
    REQUIRE(dialog->confirm_enabled);
}

TEST_CASE("Closing or opening during a save waits for its result", "[function_buttons][s3]")
{
    // 진행 중인 저장은 아직 실패하지 않아 누를 때는 묻지 않는다. 그 결과를 버리면 실패한 변경이 말없이 사라지므로 결과를
    // 기다렸다가 성공하면 그 일을 하고, 실패하면 저장하지 못한 변경 확인을 띄운다 (S3, 4.10).
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    const std::uint64_t save_id { fixture.last_request(gitman::operation_kind::save_document)->operation_id };

    SECTION("닫기: 성공하면 닫는다")
    {
        fixture.controller.handle(gitman::close_document_intent {});
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
        fixture.saved_ok(save_id);
        REQUIRE(fixture.snapshot()->document_path.empty());
        // 늦은 결과를 다시 받아도 두 번 하지 않는다.
        fixture.saved_ok(save_id);
        REQUIRE(fixture.snapshot()->document_path.empty());
    }

    SECTION("닫기: 실패하면 묻고 취소하면 그대로다")
    {
        fixture.controller.handle(gitman::close_document_intent {});
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->unsaved_changes);
        REQUIRE(fixture.dialog()->confirm_label == u8"버리고 닫기");
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
        REQUIRE(fixture.snapshot()->cards.front().id.value == u8"p01");

        fixture.controller.handle(gitman::close_document_intent {});
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.snapshot()->document_path.empty());
    }

    SECTION("다른 문서 열기: 성공하면 연다")
    {
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
        fixture.saved_ok(save_id);
        REQUIRE(fixture.last_request(gitman::operation_kind::load_document)->document_path == u8"C:\\work\\other.version-list");
    }

    SECTION("다른 문서 열기: 실패하면 묻는다")
    {
        fixture.controller.handle(gitman::open_document_intent { u8"C:\\work\\other.version-list" });
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });
        REQUIRE(fixture.request_count(gitman::operation_kind::load_document) == 0u);
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->confirm_label == u8"버리고 열기");
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.last_request(gitman::operation_kind::load_document)->document_path == u8"C:\\work\\other.version-list");
    }

    SECTION("새 문서 만들기의 결과: 기다렸다가 그 파일을 연다")
    {
        fixture.controller.handle(gitman::generate_document_intent { u8"C:\\work", u8"C:\\work\\new.version-list" });
        gitman::document_generated_event event {};
        event.operation_id = fixture.last_request(gitman::operation_kind::generate_document)->operation_id;
        event.document_path = u8"C:\\work\\new.version-list";
        event.document = { gitman::workspace_document {} };
        event.revision = { gitman::workspace_revision_token {} };
        fixture.controller.handle(std::move(event));
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
        fixture.saved_ok(save_id);
        REQUIRE(fixture.last_request(gitman::operation_kind::load_document)->document_path == u8"C:\\work\\new.version-list");
    }

    SECTION("대기열의 저장까지 기다린다")
    {
        // 앞 저장이 진행 중일 때 낸 변경은 대기열에 선다. 앞 저장의 성공은 뒤 변경을 싣지 않았다.
        fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, false });
        fixture.controller.handle(gitman::close_document_intent {});
        fixture.saved_ok(save_id);
        REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 2u);
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
        fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
        REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->confirm_label == u8"버리고 닫기");
    }
}

TEST_CASE("A confirmed close with a running capture also waits for the save", "[function_buttons][s3]")
{
    reload_fixture fixture {};
    fixture.run(u8"p00", u8"build");
    const gitman::function_run_request request { fixture.functions.requests.front() };
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    const std::uint64_t save_id { fixture.last_request(gitman::operation_kind::save_document)->operation_id };

    // 실행 중인 기능만 묻고, 확인해도 저장 결과를 기다린다.
    fixture.controller.handle(gitman::close_document_intent {});
    REQUIRE(fixture.dialog()->unsaved_changes == false);
    fixture.controller.handle(gitman::confirm_function_run_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);
    REQUIRE(request.token.cancelled() == false);

    SECTION("성공하면 다시 묻지 않고 닫는다")
    {
        fixture.saved_ok(save_id);
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.snapshot()->document_path.empty());
        REQUIRE(request.token.cancelled());
    }

    SECTION("실패하면 변경까지 실은 확인을 다시 띄운다")
    {
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::concurrent_modification, conflict_message) });
        REQUIRE(fixture.dialog().has_value());
        REQUIRE(fixture.dialog()->unsaved_changes);
        REQUIRE(fixture.dialog()->confirm_label == u8"중지하고 닫기");
        REQUIRE(request.token.cancelled() == false);
        fixture.controller.handle(gitman::confirm_function_run_intent {});
        REQUIRE(fixture.snapshot()->document_path.empty());
        REQUIRE(request.token.cancelled());
    }
}

TEST_CASE("Exiting during a save waits for its result within a time limit", "[function_buttons][s3][ui]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    const std::uint64_t save_id { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
    // 저장이 진행 중이어도 창 닫기를 받는다. 받지 않으면 종료가 그 결과를 버린다.
    REQUIRE(fixture.snapshot()->exit_needs_confirmation);
    REQUIRE(fixture.controller.next_tick().has_value() == false);

    fixture.controller.handle(gitman::request_exit_intent {});
    std::optional<gitman::function_confirm_dialog_view> dialog { fixture.dialog() };
    REQUIRE(dialog.has_value());
    REQUIRE(dialog->exit_application);
    REQUIRE(dialog->subject == u8"문서를 저장하는 중입니다");
    REQUIRE(dialog->description == std::vector<std::u8string> { u8"저장이 끝나면 종료합니다." });
    REQUIRE(dialog->confirm_label == u8"종료");
    REQUIRE(dialog->confirm_enabled == false);
    // 누를 수 없는 확인 버튼이다. 취소는 그대로 누를 수 있다.
    {
        const auto tree { make_tree(*fixture.snapshot()) };
        const luil::ui_element* const confirm { tree->find(id_of(gitman::ui::kind_function_confirm_dialog_confirm)) };
        REQUIRE(confirm != nullptr);
        REQUIRE(confirm->enabled() == false);
        const luil::ui_element* const cancel { tree->find(id_of(gitman::ui::kind_function_confirm_dialog_cancel)) };
        REQUIRE(cancel != nullptr);
        REQUIRE(cancel->enabled());
    }
    // 기다림에는 상한이 있다. logic이 그 시각에 깨어나 dialog를 바꾼다.
    const std::optional<std::chrono::steady_clock::time_point> deadline { fixture.controller.next_tick() };
    REQUIRE(deadline.has_value());

    SECTION("저장이 성공하면 다시 묻지 않고 종료를 요청한다")
    {
        fixture.saved_ok(save_id);
        dialog = fixture.dialog();
        REQUIRE(dialog.has_value() == false);
        REQUIRE(fixture.snapshot()->application_exit_requested);
        REQUIRE(fixture.controller.next_tick().has_value() == false);
        REQUIRE(fixture.snapshot()->exit_needs_confirmation == false);
    }

    SECTION("이어 나갈 저장이 있으면 마지막 성공까지 기다린다")
    {
        fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p01" }, gitman::project_id { u8"p00" }, true });
        fixture.saved_ok(save_id);
        REQUIRE(fixture.snapshot()->application_exit_requested == false);
        REQUIRE(fixture.dialog()->confirm_enabled == false);
        const std::uint64_t next_save { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
        REQUIRE(next_save != save_id);
        fixture.saved_ok(next_save);
        REQUIRE(fixture.snapshot()->application_exit_requested);
        REQUIRE(fixture.dialog().has_value() == false);
    }

    SECTION("저장이 실패하면 저장하지 못한 변경 확인이 된다")
    {
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
        dialog = fixture.dialog();
        REQUIRE(dialog.has_value());
        REQUIRE(dialog->confirm_enabled);
        REQUIRE(dialog->unsaved_changes);
        REQUIRE(dialog->confirm_label == u8"버리고 종료");
        REQUIRE(fixture.snapshot()->application_exit_requested == false);
    }

    SECTION("상한 전에는 그대로 기다린다")
    {
        fixture.controller.tick(*deadline - std::chrono::milliseconds { 1 });
        REQUIRE(fixture.dialog()->confirm_enabled == false);
    }

    SECTION("상한이 지나면 기다리지 않고 종료할 수 있다")
    {
        fixture.controller.tick(*deadline);
        dialog = fixture.dialog();
        REQUIRE(dialog.has_value());
        REQUIRE(dialog->confirm_enabled);
        REQUIRE(dialog->confirm_label == u8"기다리지 않고 종료");
        // 두 줄로 나눠 최소 창에서도 끝까지 보인다.
        REQUIRE(dialog->note == std::vector<std::u8string> { u8"저장이 끝나지 않았습니다.", u8"지금 종료하면 저장하지 못한 변경이 사라질 수 있습니다." });
        REQUIRE(fixture.controller.next_tick().has_value() == false);
        // 그 뒤에 결과가 와도 바르게 바뀐다.
        fixture.saved(save_id, { make_error(gitman::diagnostic_code::document_write_failed, u8"쓰지 못했습니다.") });
        REQUIRE(fixture.dialog()->confirm_label == u8"버리고 종료");
    }

    SECTION("취소하면 기다림도 끝난다")
    {
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.controller.next_tick().has_value() == false);
        fixture.saved_ok(save_id);
        REQUIRE(fixture.snapshot()->application_exit_requested == false);
    }

    SECTION("상한 뒤에도 취소하지 않았으면 성공한 저장 뒤 종료한다")
    {
        fixture.controller.tick(*deadline);
        fixture.saved_ok(save_id);
        REQUIRE(fixture.snapshot()->application_exit_requested);
        REQUIRE(fixture.dialog().has_value() == false);
    }

    SECTION("실행 중인 기능이 있으면 저장 성공 뒤에도 확인을 받는다")
    {
        fixture.controller.handle(gitman::cancel_function_run_intent {});
        fixture.run(u8"p00", u8"build");
        REQUIRE(fixture.functions.requests.size() == 1u);
        fixture.controller.handle(gitman::request_exit_intent {});
        fixture.saved_ok(save_id);
        REQUIRE(fixture.snapshot()->application_exit_requested == false);
        REQUIRE(fixture.dialog()->confirm_enabled);
        REQUIRE(fixture.dialog()->confirm_label == u8"중지하고 종료");
    }
}

TEST_CASE("Shutdown sends a save queued behind a reload", "[function_buttons][s3]")
{
    reload_fixture fixture {};
    // 다시 읽기의 결과를 기다리는 동안 낸 변경은 대기열에 선다. 종료가 그 결과를 기다릴 수 없으므로 종료 저장으로 낸다.
    fixture.controller.handle(gitman::reload_document_intent {});
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 0u);
    fixture.controller.handle(gitman::close_intent {});
    const gitman::operation_request* const save { fixture.last_request(gitman::operation_kind::save_document) };
    REQUIRE(save != nullptr);
    REQUIRE(save->document.has_value());
    REQUIRE(save->document->projects.front().id.value == u8"p01");
}

TEST_CASE("Shutdown waits for the current document revision before its final save", "[shutdown_storage][logic][function_buttons]")
{
    reload_fixture fixture {};
    for (const auto& request : fixture.startup_requests)
        if (request.kind == gitman::operation_kind::save_app_settings)
            fixture.controller.handle(gitman::app_settings_saved_event { request.operation_id, true, u8"{}" });
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    const std::uint64_t first { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
    const gitman::window_placement placement { 20, 30, 1600, 900, false };
    fixture.controller.handle(gitman::window_placement_intent { placement });
    fixture.controller.handle(gitman::close_intent {});
    REQUIRE(fixture.controller.shutdown_requested());
    REQUIRE(fixture.controller.shutdown_storage_idle() == false);
    REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 1u);

    SECTION("새 revision으로 마지막 모델을 저장한다")
    {
        gitman::document_saved_event saved {};
        saved.operation_id = first;
        saved.revision = revision_probe::first(std::u8string { function_card_fixture::document_path });
        fixture.controller.handle(std::move(saved));
        const gitman::operation_request final { *fixture.last_request(gitman::operation_kind::save_document) };
        REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 2u);
        REQUIRE(final.operation_id != first);
        REQUIRE(revision_probe::bytes(final.revision) == u8"first saved bytes");
        REQUIRE(final.document->window == placement);
        REQUIRE(final.document->projects.front().id.value == u8"p01");
        REQUIRE(final.token.cancelled() == false);
        REQUIRE(fixture.controller.shutdown_storage_idle() == false);
        fixture.saved_ok(final.operation_id);
        REQUIRE(fixture.controller.shutdown_storage_idle());
        REQUIRE(fixture.snapshot()->notices.empty());
    }

    SECTION("실패 결과도 처리하고 종료 대기를 마친다")
    {
        fixture.saved(first, { make_error(gitman::diagnostic_code::document_write_failed, u8"첫 쓰기 실패") });
        const std::uint64_t final { fixture.last_request(gitman::operation_kind::save_document)->operation_id };
        REQUIRE(final != first);
        fixture.saved(final, { make_error(gitman::diagnostic_code::document_write_failed, u8"마지막 쓰기 실패") });
        REQUIRE(fixture.controller.shutdown_storage_idle());
        REQUIRE(fixture.snapshot()->notices.front().text == u8"마지막 쓰기 실패");
    }
}

TEST_CASE("Closing while a queued save waits for a reload sends the save and drops the reload", "[function_buttons][s3]")
{
    reload_fixture fixture {};
    // 다시 읽기의 결과를 기다리는 저장은 대기열에 있다. 닫기가 다시 읽기를 기다리면 저장이 나가지 않아 변경이 말없이
    // 사라지므로 다시 읽기를 버리고 저장을 지금 낸다 (S3).
    fixture.controller.handle(gitman::reload_document_intent {});
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 0u);
    fixture.controller.handle(gitman::close_document_intent {});
    REQUIRE(fixture.dialog().has_value() == false);
    const gitman::operation_request* const save { fixture.last_request(gitman::operation_kind::save_document) };
    REQUIRE(save != nullptr);
    REQUIRE(save->document->projects.front().id.value == u8"p01");
    REQUIRE(fixture.snapshot()->document_path == function_card_fixture::document_path);

    // 버린 다시 읽기의 결과는 쓰지 않는다. 저장이 성공하면 닫는다.
    fixture.reloaded(reload_fixture::edited_document());
    REQUIRE(fixture.snapshot()->cards.front().id.value == u8"p01");
    fixture.saved_ok(save->operation_id);
    REQUIRE(fixture.snapshot()->document_path.empty());
}

TEST_CASE("An exit wait that another path closed does not leave its deadline behind", "[function_buttons][s3]")
{
    reload_fixture fixture {};
    // 다시 읽는 사이 낸 변경(대기열 저장)으로 종료가 저장을 기다린다. 다시 읽기가 성공하면 문서를 바꾸며 그 dialog를 닫는다.
    fixture.controller.handle(gitman::reload_document_intent {});
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    fixture.controller.handle(gitman::request_exit_intent {});
    REQUIRE(fixture.dialog().has_value());
    REQUIRE(fixture.dialog()->confirm_enabled == false);
    const std::optional<std::chrono::steady_clock::time_point> first { fixture.controller.next_tick() };
    REQUIRE(first.has_value());
    gitman::workspace_document document { reload_fixture::edited_document() };
    document.projects = gitman::testing::make_function_card_projects(2);
    fixture.reloaded(std::move(document));
    REQUIRE(fixture.dialog().has_value() == false);
    REQUIRE(fixture.controller.next_tick().has_value() == false);

    // 새 저장 중에 다시 닫으면 그때부터 잰다. 앞 기한이 남으면 기다림 없이 `기다리지 않고 종료`가 뜬다 (S3 검토).
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    REQUIRE(fixture.request_count(gitman::operation_kind::save_document) == 1u);
    fixture.controller.handle(gitman::request_exit_intent {});
    const std::optional<std::chrono::steady_clock::time_point> second { fixture.controller.next_tick() };
    REQUIRE(second.has_value());
    REQUIRE(*second > *first);
    fixture.controller.tick(*first);
    REQUIRE(fixture.dialog()->confirm_enabled == false);
    REQUIRE(fixture.dialog()->confirm_label == u8"종료");
}

TEST_CASE("A failed reload after a save conflict shows the failure first", "[function_buttons][t11]")
{
    reload_fixture fixture {};
    fixture.controller.handle(gitman::reorder_card_intent { gitman::project_id { u8"p00" }, gitman::project_id { u8"p01" }, true });
    fixture.saved(fixture.last_request(gitman::operation_kind::save_document)->operation_id, { make_error(gitman::diagnostic_code::concurrent_modification, u8"load 이후 작업공간 문서가 변경되어 저장하지 않았습니다.") });
    REQUIRE(fixture.snapshot()->notices.front().text == conflict_notice);

    // 충돌 안내를 따라 다시 읽었으나 파일이 아직 깨져 있다. 띠 첫 줄이 그 실패여야 한다 — 충돌 안내가 남으면 아무 일도
    // 없었던 것처럼 보인다. 저장하지 못한 카드 순서가 있어 확인을 거친다 (D28).
    fixture.controller.handle(gitman::reload_document_intent {});
    fixture.controller.handle(gitman::confirm_function_run_intent {});
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
        // T12에서 `기능 관리…`이 끝에 붙어 높이가 112다.
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
        REQUIRE(fixture.dialog().has_value() == false);
        REQUIRE(fixture.snapshot()->application_exit_requested);
    }

    SECTION("다른 dialog의 확인은 logic intent 그대로다")
    {
        fixture.controller.handle(gitman::close_document_intent {});
        const auto closing { make_tree(*fixture.snapshot()) };
        REQUIRE(gitman::testing::as_intent<gitman::confirm_function_run_intent>(click_action(*closing, id_of(gitman::ui::kind_function_confirm_dialog_confirm))) != nullptr);
    }
}
