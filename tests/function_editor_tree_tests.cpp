#include "helpers/function_card_fixture.h"
#include "helpers/ui_test_support.h"

#include "application/app_commands.h"
#include "presentation/function_editor_layout.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_editor_dialog_element.h"
#include "presentation/ui/gitman_policy.h"

#include "luil/ui/ui_tree.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// 기능 편집 dialog의 tree와 입력이다 (function-buttons-design 2.7, 6.1 T12). logic이 지은 snapshot으로 실제 tree를 짓는다.
using namespace gitman::testing;

namespace {
    namespace ui = gitman::ui;

    void open_editor(function_card_fixture& fixture, const std::u8string_view button = {})
    {
        fixture.controller.handle(gitman::open_function_editor_intent { std::u8string { button } });
    }

    [[nodiscard]] std::vector<gitman::function_button> sample_buttons()
    {
        return {
            make_function_card_button(u8"a", u8"가", u8"card"),
            make_function_card_button(u8"b", u8"나", u8"card"),
            make_function_card_button(u8"c", u8"다", u8"card"),
            make_function_card_button(u8"d", u8"라", u8"bar"),
            make_function_card_button(u8"e", u8"마"),
        };
    }

    [[nodiscard]] bool shown(const luil::ui_tree& tree, const luil::ui_element_kind kind, const std::u8string_view owner = u8"")
    {
        const luil::ui_element* const element { find(tree, kind, owner) };
        return element != nullptr && tree.visibly_contains(id_of(kind, owner));
    }

    [[nodiscard]] luil::key_pressed_event alt_key(const luil::key_code key)
    {
        luil::key_pressed_event event { key };
        event.alt = true;
        return event;
    }
}

TEST_CASE("The editor dialog builds its list, form and buttons", "[function_buttons][t12][ui][tree]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons(), 1280.0f, 720.0f };
    open_editor(fixture, u8"d");
    const auto tree { fixture.tree() };
    const luil::ui_element* const dialog { find(*tree, ui::kind_function_editor_dialog) };
    REQUIRE(dialog != nullptr);
    // 크기는 `min(760, W-40) × min(520, H-40)`이다.
    REQUIRE(dialog->bounds().width == 760.0f);
    REQUIRE(dialog->bounds().height == 520.0f);

    // 펼친 목록은 행마다 하나이고 고른 행은 선택이다.
    REQUIRE(shown(*tree, ui::kind_function_editor_list));
    // 행 id는 순번이 아니라 버튼 id다 (`button-<id>`).
    for (const std::u8string_view key : { u8"button-a", u8"button-b", u8"button-c", u8"button-d", u8"button-e" })
        REQUIRE(tree->find(ui::function_editor_item_id(key)) != nullptr);
    REQUIRE(tree->find(ui::function_editor_item_id(u8"button-d"))->accessibility().selected == std::optional<bool> { true });
    REQUIRE(tree->find(ui::function_editor_item_id(u8"button-d"))->accessibility().name == u8"라, 기능 줄");
    REQUIRE(tree->find(ui::function_editor_item_id(u8"button-a"))->accessibility().name == u8"가, 카드 Alt+1");
    // 목록의 이름은 머리 줄이다.
    REQUIRE(find(*tree, ui::kind_function_editor_list)->accessibility().name == u8"기능 5개 · 카드 3/3");

    // 텍스트 칸은 다섯이다 (2.7).
    constexpr std::array inputs {
        ui::kind_function_editor_label_input,
        ui::kind_function_editor_program_input,
        ui::kind_function_editor_arguments_input,
        ui::kind_function_editor_working_directory_input,
        ui::kind_function_editor_timeout_input,
    };
    for (const luil::ui_element_kind kind : inputs)
    {
        REQUIRE(find(*tree, kind) != nullptr);
    }
    REQUIRE(tree->ids_of_kind(ui::kind_function_editor_icon).size() == gitman::function_editor_icon_count);
    REQUIRE(find(*tree, ui::kind_function_editor_program_browse) != nullptr);
    REQUIRE(find(*tree, ui::kind_function_editor_apply)->enabled());
    REQUIRE(find(*tree, ui::kind_function_editor_cancel) != nullptr);
    // 펼친 dialog에는 접힌 선택기의 `<` `>`가 보이지 않는다.
    REQUIRE_FALSE(shown(*tree, ui::kind_function_editor_tool, u8"previous"));
    REQUIRE(shown(*tree, ui::kind_function_editor_tool, u8"add"));

    // 보이는 `card`가 3개이고 고른 기능이 그중 하나가 아니면 `카드` 라벨이 `카드 — 3개까지`다.
    const luil::ui_element* const card { find(*tree, luil::ui_element_kind::choice, u8"card") };
    REQUIRE(card != nullptr);
    REQUIRE(card->accessibility().name == u8"카드 — 3개까지");
}

TEST_CASE("The editor folds its list into a selector below 640", "[function_buttons][t12][ui][tree]")
{
    // 600 창에서 dialog는 560이라 접힌다. 목록 대신 위쪽 선택기의 `<` `>`와 도구 버튼이 서고 폼이 전체 폭이다.
    function_card_fixture narrow { make_function_card_projects(2), sample_buttons(), 600.0f, 480.0f };
    open_editor(narrow, u8"b");
    const auto tree { narrow.tree() };
    const luil::ui_element* const dialog { find(*tree, ui::kind_function_editor_dialog) };
    REQUIRE(dialog->bounds().width == 560.0f);
    REQUIRE(gitman::function_editor_collapsed(560.0f));
    REQUIRE_FALSE(shown(*tree, ui::kind_function_editor_list));
    REQUIRE(shown(*tree, ui::kind_function_editor_tool, u8"previous"));
    REQUIRE(shown(*tree, ui::kind_function_editor_tool, u8"next"));
    REQUIRE(shown(*tree, ui::kind_function_editor_tool, u8"move-down"));
    const luil::ui_element* const form { find(*tree, ui::kind_function_editor_form) };
    REQUIRE(form->bounds().x - dialog->bounds().x == gitman::layout_function_editor_padding);

    // `<` `>`는 이웃 항목을 고른다.
    interaction_harness harness { tree };
    const auto previous { click(harness, bounds_of(*tree, ui::kind_function_editor_tool, u8"previous")) };
    const auto* const intent { as_intent<gitman::select_function_editor_entry_intent>(previous) };
    REQUIRE(intent != nullptr);
    REQUIRE(intent->index == 0u);

    // 640 이상이면 펼친다.
    function_card_fixture wide { make_function_card_projects(2), sample_buttons(), 700.0f, 480.0f };
    open_editor(wide);
    REQUIRE(shown(*wide.tree(), ui::kind_function_editor_list));
    REQUIRE_FALSE(shown(*wide.tree(), ui::kind_function_editor_tool, u8"previous"));
}

TEST_CASE("Editor rows, browse and keys send their intents", "[function_buttons][t12][ui][interaction]")
{
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons(), 1280.0f, 720.0f };
    open_editor(fixture);
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };

    const auto row { click(harness, tree->find(ui::function_editor_item_id(u8"button-c"))->bounds()) };
    const auto* const select { as_intent<gitman::select_function_editor_entry_intent>(row) };
    REQUIRE(select != nullptr);
    REQUIRE(select->index == 2u);

    // 찾아보기는 UI thread의 파일 dialog 명령이다. 고른 경로만 intent로 돌아온다.
    const auto browse { click(harness, bounds_of(*tree, ui::kind_function_editor_program_browse)) };
    const luil::app_ui_command* const command { as_app_command(browse) };
    REQUIRE(command != nullptr);
    REQUIRE(gitman::parse_app_command(command->command) == gitman::app_command::show_function_program_picker);

    // `Alt+↑/↓`이 순서를 바꾸고, 다른 앱 키(↓의 카드 이동 등)는 dialog 뒤로 가지 않는다.
    const auto down { harness.process(alt_key(luil::key_code::arrow_down)) };
    const auto* const move { as_intent<gitman::edit_function_editor_list_intent>(down) };
    REQUIRE(move != nullptr);
    REQUIRE(move->edit == gitman::function_editor_list_edit::move_down);
    REQUIRE(as_intent<gitman::edit_function_editor_list_intent>(harness.process(alt_key(luil::key_code::arrow_up)))->edit == gitman::function_editor_list_edit::move_up);
    REQUIRE(harness.process(alt_key(luil::key_code::key_1)).empty());
    REQUIRE(harness.process(luil::key_pressed_event { luil::key_code::f5 }).empty());

    // Esc는 취소다.
    REQUIRE(as_intent<gitman::cancel_function_editor_intent>(harness.process(luil::key_pressed_event { luil::key_code::escape })) != nullptr);

    // 다섯 칸은 텍스트 칸 표에 있다.
    REQUIRE(harness.policy.text_target_of(ui::kind_function_editor_label_input) == ui::to_luil_target(gitman::text_input_target::function_editor_label));
    REQUIRE(harness.policy.text_target_of(ui::kind_function_editor_timeout_input) == ui::to_luil_target(gitman::text_input_target::function_editor_timeout));
    REQUIRE(ui::from_luil_target(ui::to_luil_target(gitman::text_input_target::function_editor_working_directory)) == gitman::text_input_target::function_editor_working_directory);
}

TEST_CASE("The wheel scrolls the editor form or list", "[function_buttons][t12][ui][interaction]")
{
    // 480×320 창에서는 폼이 보이는 높이보다 길어 스크롤한다.
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons(), 480.0f, 320.0f };
    open_editor(fixture);
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    const luil::rect_f form { bounds_of(*tree, ui::kind_function_editor_form) };
    const auto over_form { harness.policy.on_wheel(*tree, luil::mouse_wheel_event { form.x + 5.0f, form.y + 5.0f, -1.0f }, 40.0f) };
    const auto* const scroll { as_intent<gitman::function_editor_scroll_intent>(over_form) };
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->region == gitman::function_editor_scroll_region::form);
    fixture.controller.handle(*scroll);
    REQUIRE(fixture.snapshot()->function_editor->form_scroll == 40.0f);
    REQUIRE(shown(*fixture.tree(), ui::kind_scrollbar, ui::scrollbar_owner_function_editor_form));

    // 끝을 넘지 않는다.
    fixture.controller.handle(gitman::function_editor_scroll_intent { gitman::function_editor_scroll_region::form, 100000.0f });
    const float maximum { fixture.snapshot()->function_editor->form_scroll };
    REQUIRE(maximum > 40.0f);
    fixture.controller.handle(gitman::function_editor_scroll_intent { gitman::function_editor_scroll_region::form, 10.0f });
    REQUIRE(fixture.snapshot()->function_editor->form_scroll == maximum);
}

TEST_CASE("The editor folds exactly below 640", "[function_buttons][t12][ui][tree]")
{
    // 창 679이면 dialog는 639라 접히고, 680이면 640이라 펼친다.
    function_card_fixture folded { make_function_card_projects(2), sample_buttons(), 679.0f, 480.0f };
    open_editor(folded);
    REQUIRE(find(*folded.tree(), ui::kind_function_editor_dialog)->bounds().width == 639.0f);
    REQUIRE_FALSE(shown(*folded.tree(), ui::kind_function_editor_list));
    REQUIRE(shown(*folded.tree(), ui::kind_function_editor_tool, u8"previous"));

    function_card_fixture unfolded { make_function_card_projects(2), sample_buttons(), 680.0f, 480.0f };
    open_editor(unfolded);
    REQUIRE(find(*unfolded.tree(), ui::kind_function_editor_dialog)->bounds().width == 640.0f);
    REQUIRE(shown(*unfolded.tree(), ui::kind_function_editor_list));
    REQUIRE_FALSE(shown(*unfolded.tree(), ui::kind_function_editor_tool, u8"previous"));
}

TEST_CASE("Keyboard focus scrolls the editor form and list to the focused control", "[function_buttons][t12][ui][interaction]")
{
    // 480×320 창에서 폼은 낮아 아래 칸이 보이는 자리 밖이다. Tab으로 닿으면 policy가 그만큼 폼을 흘린다 (luil
    // `on_focus_moved` → `route_reveal`). 이미 보이는 칸이면 메시지를 내지 않는다.
    function_card_fixture fixture { make_function_card_projects(2), sample_buttons(), 480.0f, 320.0f };
    open_editor(fixture);
    const auto tree { fixture.tree() };
    interaction_harness harness { tree };
    const auto reveal { harness.policy.on_focus_moved(*tree, id_of(ui::kind_function_editor_timeout_input)) };
    const auto* const scroll { as_intent<gitman::function_editor_scroll_intent>(reveal) };
    REQUIRE(scroll != nullptr);
    REQUIRE(scroll->region == gitman::function_editor_scroll_region::form);
    REQUIRE(scroll->delta > 0.0f);
    REQUIRE(harness.policy.on_focus_moved(*tree, id_of(ui::kind_function_editor_label_input)).empty());

    fixture.controller.handle(*scroll);
    const auto scrolled { fixture.tree() };
    const luil::rect_f form { bounds_of(*scrolled, ui::kind_function_editor_form) };
    const luil::rect_f timeout { bounds_of(*scrolled, ui::kind_function_editor_timeout_input) };
    REQUIRE(timeout.y >= form.y);
    REQUIRE(timeout.y + timeout.height <= form.y + form.height + 0.01f);

    // 목록 행은 영역 밖이어도 숨기지 않는다 — 숨기면 초점 순서에서 빠져 화살표가 닿지 않는다. 닿은 행은 목록을 흘린다.
    std::vector<gitman::function_button> many {};
    for (std::size_t index = 0; index < 16; ++index)
    {
        std::u8string id { u8"f" };
        id.push_back(static_cast<char8_t>(u8'a' + index));
        many.push_back(make_function_card_button(id, id));
    }
    function_card_fixture list { make_function_card_projects(1), std::move(many), 1280.0f, 720.0f };
    open_editor(list);
    const auto list_tree { list.tree() };
    const luil::ui_element* const last { list_tree->find(ui::function_editor_item_id(u8"button-fp")) };
    REQUIRE(last != nullptr);
    REQUIRE(last->visible());
    REQUIRE(last->bounds().y > bounds_of(*list_tree, ui::kind_function_editor_list).y + bounds_of(*list_tree, ui::kind_function_editor_list).height);
    interaction_harness list_harness { list_tree };
    const auto row_reveal { list_harness.policy.on_focus_moved(*list_tree, ui::function_editor_item_id(u8"button-fp")) };
    const auto* const row_scroll { as_intent<gitman::function_editor_scroll_intent>(row_reveal) };
    REQUIRE(row_scroll != nullptr);
    REQUIRE(row_scroll->region == gitman::function_editor_scroll_region::list);
    REQUIRE(row_scroll->delta > 0.0f);
}

TEST_CASE("Reordering keeps the row id on the same function", "[function_buttons][t12][ui][tree]")
{
    // 행 id가 버튼 id라 순서를 바꿔도 luil 초점과 선택이 같은 기능을 가리킨다 (luil-ui "반복 항목 id는 값으로").
    function_card_fixture fixture { make_function_card_projects(1), sample_buttons(), 1280.0f, 720.0f };
    open_editor(fixture, u8"a");
    fixture.controller.handle(gitman::edit_function_editor_list_intent { gitman::function_editor_list_edit::move_down });
    const auto tree { fixture.tree() };
    const luil::ui_element* const moved { tree->find(ui::function_editor_item_id(u8"button-a")) };
    REQUIRE(moved != nullptr);
    REQUIRE(moved->accessibility().selected == std::optional<bool> { true });
    REQUIRE(moved->bounds().y > tree->find(ui::function_editor_item_id(u8"button-b"))->bounds().y);
}

TEST_CASE("The editor exposes its drawn text to assistive technology", "[function_buttons][t12][ui][tree]")
{
    // 미리보기·거절 사유·아이콘 묶음·인자 오류는 그리기만 하지 않고 접근 정보로도 낸다.
    std::vector<gitman::function_button> buttons { sample_buttons() };
    buttons[4].project_ids = std::vector<std::u8string> { u8"p01" };
    function_card_fixture fixture { make_function_card_projects(2), std::move(buttons), 1280.0f, 720.0f };
    open_editor(fixture, u8"e");
    {
        const auto tree { fixture.tree() };
        // 미리보기 대상(첫 카드 p00)은 이 기능의 범위 밖이다. 명령줄과 경고가 함께 나온다.
        const std::u8string preview { find(*tree, ui::kind_function_editor_text, u8"preview")->accessibility().name };
        REQUIRE(preview.find(u8"명령줄") != std::u8string::npos);
        REQUIRE(preview.find(u8"작업 폴더") != std::u8string::npos);
        REQUIRE(preview.find(u8"이 저장소에는 적용되지 않습니다.") != std::u8string::npos);
        REQUIRE(find(*tree, ui::kind_function_editor_icons)->accessibility().name == u8"아이콘");
        REQUIRE(find(*tree, ui::kind_function_editor_icon, u8"play")->accessibility().name == u8"재생, 선택됨");
        REQUIRE(find(*tree, ui::kind_function_editor_icon, u8"tools")->accessibility().name == u8"도구");
        REQUIRE(find(*tree, ui::kind_function_editor_text, u8"message") == nullptr);
    }

    // 닫히지 않은 따옴표는 luil 칸의 오류 글이다. Apply의 거절 사유는 alert 글이다.
    fixture.controller.handle(gitman::edit_text_input_intent { gitman::text_input_target::function_editor_arguments, gitman::text_edit_command::insert, u8"\"abc" });
    fixture.controller.handle(gitman::apply_function_editor_intent {});
    const auto tree { fixture.tree() };
    REQUIRE(find(*tree, ui::kind_function_editor_arguments_input)->accessibility().error == u8"인자에 닫히지 않은 큰따옴표가 있습니다.");
    const luil::access_info message { find(*tree, ui::kind_function_editor_text, u8"message")->accessibility() };
    REQUIRE(message.role == luil::access_role::alert);
    REQUIRE(message.name == u8"\"마\": 인자에 닫히지 않은 큰따옴표가 있습니다.");
}
