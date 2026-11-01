#include "application/app_commands.h"
#include "application/logic_controller.h"
#include "helpers/ui_test_support.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/generate_document_dialog_element.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
    using gitman::testing::as_app_command;
    using gitman::testing::as_intent;
    using gitman::testing::as_message;
    using gitman::testing::at;
    using gitman::testing::find;
    using gitman::testing::id_of;
    using gitman::testing::make_tree;

    // 이름 칸의 편집 intent다. 키 입력 한 번과 같다.
    gitman::edit_text_input_intent name_insert(const std::u8string_view text)
    {
        gitman::edit_text_input_intent intent {};
        intent.target = gitman::text_input_target::generate_document_name;
        intent.command = gitman::text_edit_command::insert;
        intent.text = text;
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

    // 새 문서 만들기 dialog를 연 controller다. 문서가 없어도 열린다 — 이 dialog가
    // 만드는 것이 바로 문서다.
    struct generate_fixture
    {
        recording_submitter submitter {};
        gitman::logic_controller controller { submitter };

        generate_fixture()
        {
            controller.handle(gitman::window_metrics_intent { 800.0f, 600.0f, 1.0f });
            controller.handle(gitman::open_generate_document_intent {});
        }

        // 만들 수 있는 상태로 채운다 (저장소 폴더와 이름).
        void fill()
        {
            controller.handle(gitman::set_generate_scan_root_intent { u8"C:\\work\\scan" });
            controller.handle(name_insert(u8"team"));
        }

        [[nodiscard]] gitman::generate_document_dialog_view dialog()
        {
            const auto view { controller.make_view_snapshot() };
            REQUIRE(view->generate_dialog.has_value());
            return *view->generate_dialog;
        }
    };

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
} // namespace

TEST_CASE("The generate dialog opens empty and refuses to confirm", "[logic][generate-ui]")
{
    generate_fixture fixture {};
    const gitman::generate_document_dialog_view dialog { fixture.dialog() };
    REQUIRE(dialog.name.text.empty());
    REQUIRE(dialog.same_folder);
    REQUIRE(dialog.document_path.empty());
    REQUIRE(dialog.can_confirm == false);
    // 아직 덜 채운 것을 안내한다.
    REQUIRE(dialog.message.empty() == false);

    // 채우지 않은 채 확인해도 아무것도 제출되지 않고 dialog는 열려 있다.
    fixture.controller.handle(gitman::confirm_generate_document_intent {});
    REQUIRE(fixture.submitter.requests.empty());
    REQUIRE(fixture.dialog().executing == false);
}

TEST_CASE("The scan folder and the name build the document path", "[logic][generate-ui]")
{
    generate_fixture fixture {};
    fixture.controller.handle(gitman::set_generate_scan_root_intent { u8"C:\\work\\scan" });
    // 폴더만으로는 부족하다 — 이름이 있어야 경로가 생긴다.
    REQUIRE(fixture.dialog().document_path.empty());

    fixture.controller.handle(name_insert(u8"team"));
    const gitman::generate_document_dialog_view dialog { fixture.dialog() };
    REQUIRE(dialog.scan_root == u8"C:\\work\\scan");
    // 확장자는 앱이 붙인다.
    REQUIRE(dialog.document_path == u8"C:\\work\\scan\\team.version-list");
    REQUIRE(dialog.can_confirm);
    REQUIRE(dialog.message.empty());
}

TEST_CASE("The same folder toggle switches between the scan root and the chosen location", "[logic][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();

    // 끄면 저장 위치가 필요해져 확인이 다시 막힌다.
    fixture.controller.handle(gitman::toggle_generate_same_folder_intent {});
    gitman::generate_document_dialog_view dialog { fixture.dialog() };
    REQUIRE(dialog.same_folder == false);
    REQUIRE(dialog.can_confirm == false);
    REQUIRE(dialog.message.empty() == false);

    // 저장 위치를 고르면 그 폴더에 만들어진다.
    fixture.controller.handle(gitman::set_generate_location_intent { u8"C:\\out" });
    dialog = fixture.dialog();
    REQUIRE(dialog.same_folder == false);
    REQUIRE(dialog.location == u8"C:\\out");
    REQUIRE(dialog.document_path == u8"C:\\out\\team.version-list");

    // 다시 켜면 저장소 폴더로 돌아간다.
    fixture.controller.handle(gitman::toggle_generate_same_folder_intent {});
    dialog = fixture.dialog();
    REQUIRE(dialog.same_folder);
    REQUIRE(dialog.document_path == u8"C:\\work\\scan\\team.version-list");
}

TEST_CASE("Editing the name rebuilds the path and rejects unusable characters", "[logic][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();
    fixture.controller.handle(name_insert(u8"-2026"));
    REQUIRE(fixture.dialog().document_path == u8"C:\\work\\scan\\team-2026.version-list");

    // 파일 이름으로 쓸 수 없는 글자가 들어가면 경로가 사라지고 사유가 보인다.
    fixture.controller.handle(name_insert(u8"?"));
    const gitman::generate_document_dialog_view dialog { fixture.dialog() };
    REQUIRE(dialog.name.text == u8"team-2026?");
    REQUIRE(dialog.document_path.empty());
    REQUIRE(dialog.can_confirm == false);
    REQUIRE(dialog.message.empty() == false);
}

TEST_CASE("Confirming submits the generation and marks the dialog executing", "[logic][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();
    fixture.controller.handle(gitman::confirm_generate_document_intent {});

    REQUIRE(fixture.submitter.requests.size() == 1u);
    const gitman::operation_request& request { fixture.submitter.requests.front() };
    REQUIRE(request.kind == gitman::operation_kind::generate_document);
    REQUIRE(request.scan_root == u8"C:\\work\\scan");
    REQUIRE(request.document_path == u8"C:\\work\\scan\\team.version-list");

    const gitman::generate_document_dialog_view dialog { fixture.dialog() };
    REQUIRE(dialog.executing);
    REQUIRE(dialog.can_confirm == false);
}

TEST_CASE("Cancelling closes the generate dialog", "[logic][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();
    fixture.controller.handle(gitman::cancel_generate_document_intent {});

    REQUIRE(fixture.controller.make_view_snapshot()->generate_dialog.has_value() == false);
    REQUIRE(fixture.submitter.requests.empty());
}

TEST_CASE("The generate dialog stands in a modal host that cancels on the scrim and Escape", "[ui][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };

    // 뼈대는 luil `modal_host_element`다 (설계 D2-A). host가 scrim·가운데 배치·초점
    // 가둠을 맡고 본체는 panel과 칸·버튼만 그린다.
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog) != nullptr);
    REQUIRE(find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_generate) != nullptr);
    REQUIRE(tree->focus_trap() != nullptr);
    // 화면 읽기가 dialog가 뜰 때 말하는 이름은 본체 제목과 같다.
    REQUIRE(find(*tree, luil::ui_element_kind::modal_host, gitman::ui::dialog_owner_generate)->accessibility().name == u8"새 문서 만들기");

    // 배경 클릭은 host의 scrim이 받는다. 본체에는 배경 액션이 없다.
    const std::vector<luil::input_action> outside { click(*tree, id_of(luil::ui_element_kind::modal_scrim, gitman::ui::dialog_owner_generate)) };
    const gitman::logic_message* const outside_message { as_message(outside) };
    REQUIRE(outside_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_generate_document_intent>(*outside_message));
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog)->action(luil::ui_trigger::left_click) == nullptr);

    // Esc도 host의 dismiss가 낸다 (앱 policy의 dialog 사슬은 없다).
    gitman::testing::interaction_harness harness { tree };
    const std::vector<luil::input_action> escape { harness.process(luil::key_pressed_event { luil::key_code::escape }) };
    const gitman::logic_message* const escape_message { as_message(escape) };
    REQUIRE(escape_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_generate_document_intent>(*escape_message));
}

TEST_CASE("The generate dialog rows are not tab stops but its field and buttons are", "[ui][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };

    // D1-B: 본체(배경)와 panel은 Tab 자리가 아니고 칸·버튼 줄만 자리다.
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog)->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_panel)->tab_stop() == false);
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_name_input)->tab_stop());
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_scan_browse)->tab_stop());
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_location_browse)->tab_stop());
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_same_folder)->tab_stop());
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_confirm)->tab_stop());
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_cancel)->tab_stop());

    // 만들기 버튼이 Enter의 기본 자리다 (luil enter-default-design).
    REQUIRE(find(*tree, gitman::ui::kind_generate_dialog_confirm)->default_button());
    REQUIRE(tree->default_button() == find(*tree, gitman::ui::kind_generate_dialog_confirm));
}

TEST_CASE("The generate dialog buttons ask for the folder pickers and the toggles", "[ui][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };

    // 폴더 선택은 Win32 dialog라 UI thread 명령으로 나간다 (설계 L7).
    // 액션 목록을 지역에 잡는다 — as_app_command·as_message는 그 목록을 가리키는 포인터다.
    const std::vector<luil::input_action> scan_actions { click(*tree, id_of(gitman::ui::kind_generate_dialog_scan_browse)) };
    const luil::app_ui_command* const scan { as_app_command(scan_actions) };
    REQUIRE(scan != nullptr);
    REQUIRE(gitman::parse_app_command(scan->command) == gitman::app_command::show_generate_scan_folder_picker);

    const std::vector<luil::input_action> location_actions { click(*tree, id_of(gitman::ui::kind_generate_dialog_location_browse)) };
    const luil::app_ui_command* const location { as_app_command(location_actions) };
    REQUIRE(location != nullptr);
    REQUIRE(gitman::parse_app_command(location->command) == gitman::app_command::show_generate_location_picker);

    const std::vector<luil::input_action> same_folder_actions { click(*tree, id_of(gitman::ui::kind_generate_dialog_same_folder)) };
    const gitman::logic_message* const same_folder { as_message(same_folder_actions) };
    REQUIRE(same_folder != nullptr);
    REQUIRE(std::holds_alternative<gitman::toggle_generate_same_folder_intent>(*same_folder));

    const std::vector<luil::input_action> confirm_actions { click(*tree, id_of(gitman::ui::kind_generate_dialog_confirm)) };
    const gitman::logic_message* const confirm { as_message(confirm_actions) };
    REQUIRE(confirm != nullptr);
    REQUIRE(std::holds_alternative<gitman::confirm_generate_document_intent>(*confirm));

    const std::vector<luil::input_action> cancel_actions { click(*tree, id_of(gitman::ui::kind_generate_dialog_cancel)) };
    const gitman::logic_message* const cancel { as_message(cancel_actions) };
    REQUIRE(cancel != nullptr);
    REQUIRE(std::holds_alternative<gitman::cancel_generate_document_intent>(*cancel));
}

TEST_CASE("Typing in the name field edits the generate name and Enter confirms", "[ui][generate-ui]")
{
    generate_fixture fixture {};
    fixture.fill();
    const auto tree { make_tree(*fixture.controller.make_view_snapshot()) };
    gitman::testing::interaction_harness harness { tree };

    // dialog가 열려 있어도 초점이 없으면 문자는 버려진다 (자동 초점 없음).
    REQUIRE(harness.process(luil::character_typed_event { U'x' }).empty());

    const luil::ui_element* const input { find(*tree, gitman::ui::kind_generate_dialog_name_input) };
    REQUIRE(input != nullptr);
    const luil::rect_f box { input->bounds() };
    static_cast<void>(harness.process(luil::pointer_pressed_event { box.x + 2.0f, box.y + 2.0f, luil::pointer_button::left, at(0) }));
    static_cast<void>(harness.process(luil::pointer_released_event { box.x + 2.0f, box.y + 2.0f, luil::pointer_button::left, at(10) }));

    // 문자·편집 키를 요청으로 만드는 일은 luil의 몫이라(계약 3장) 여기서는 policy가
    // 그것을 어떤 gitman intent로 옮기는지만 본다.
    const std::vector<luil::input_action> typed { harness.process(luil::character_typed_event { U'x' }) };
    const auto* const intent { as_intent<gitman::edit_text_input_intent>(typed) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->target == gitman::text_input_target::generate_document_name);
    REQUIRE(intent->command == gitman::text_edit_command::insert);
    REQUIRE(intent->text == u8"x");

    // 이름 칸의 Enter는 기본 버튼(만들기)으로 흐른다 (luil enter-default-design).
    const std::vector<luil::input_action> enter { harness.process(luil::key_pressed_event { luil::key_code::enter }) };
    const gitman::logic_message* const enter_message { as_message(enter) };
    REQUIRE(enter_message != nullptr);
    REQUIRE(std::holds_alternative<gitman::confirm_generate_document_intent>(*enter_message));
}
