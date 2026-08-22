#include "domain/text_edit.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace {
    gitman::text_edit_state at_end(const std::u8string_view text)
    {
        gitman::text_edit_state state {};
        gitman::text_edit_set_text(state, text, true);
        return state;
    }
} // namespace

TEST_CASE("Text edit inserts and deletes at the caret", "[domain][text-edit]")
{
    gitman::text_edit_state state { at_end(u8"main") };
    REQUIRE(state.caret == 4u);

    gitman::text_edit_move(state, gitman::text_edit_motion::line_start, false);
    gitman::text_edit_insert(state, u8"origin/");
    REQUIRE(state.text == u8"origin/main");
    REQUIRE(state.caret == 7u);
    REQUIRE_FALSE(gitman::text_edit_has_selection(state));

    gitman::text_edit_backspace(state);
    REQUIRE(state.text == u8"originmain");
    REQUIRE(state.caret == 6u);

    gitman::text_edit_delete_forward(state);
    REQUIRE(state.text == u8"originain");
    REQUIRE(state.caret == 6u);

    // 끝에서의 delete와 처음에서의 backspace는 아무 일도 하지 않는다.
    gitman::text_edit_move(state, gitman::text_edit_motion::line_end, false);
    gitman::text_edit_delete_forward(state);
    gitman::text_edit_move(state, gitman::text_edit_motion::line_start, false);
    gitman::text_edit_backspace(state);
    REQUIRE(state.text == u8"originain");
}

TEST_CASE("Text edit moves over whole characters, not bytes", "[domain][text-edit]")
{
    // 한글 한 글자는 UTF-8 3바이트다. caret이 그 가운데 서면 안 된다.
    gitman::text_edit_state state { at_end(u8"가나") };
    REQUIRE(state.caret == 6u);

    gitman::text_edit_move(state, gitman::text_edit_motion::left, false);
    REQUIRE(state.caret == 3u);
    gitman::text_edit_backspace(state);
    REQUIRE(state.text == u8"나");
    REQUIRE(state.caret == 0u);

    // 바이트 한가운데를 가리켜도 문자 시작으로 되돌아간다.
    gitman::text_edit_place_caret(state, 2u, false);
    REQUIRE(state.caret == 0u);
    gitman::text_edit_place_caret(state, 99u, false);
    REQUIRE(state.caret == 3u);
}

TEST_CASE("Text edit moves by word over branch separators", "[domain][text-edit]")
{
    gitman::text_edit_state state { at_end(u8"origin/feature-a") };

    gitman::text_edit_move(state, gitman::text_edit_motion::word_left, false);
    REQUIRE(state.caret == 15u);
    gitman::text_edit_move(state, gitman::text_edit_motion::word_left, false);
    REQUIRE(state.caret == 7u);
    gitman::text_edit_move(state, gitman::text_edit_motion::word_left, false);
    REQUIRE(state.caret == 0u);

    gitman::text_edit_move(state, gitman::text_edit_motion::word_right, false);
    REQUIRE(state.caret == 7u);
    gitman::text_edit_move(state, gitman::text_edit_motion::word_right, false);
    REQUIRE(state.caret == 15u);
}

TEST_CASE("Text edit selection replaces, copies and collapses", "[domain][text-edit]")
{
    gitman::text_edit_state state { at_end(u8"origin/main") };

    // Shift+←는 anchor를 두고 caret만 옮겨 범위를 늘린다.
    gitman::text_edit_move(state, gitman::text_edit_motion::word_left, true);
    REQUIRE(gitman::text_edit_has_selection(state));
    REQUIRE(gitman::text_edit_selected_text(state) == u8"main");
    REQUIRE(gitman::text_edit_selection_begin(state) == 7u);
    REQUIRE(gitman::text_edit_selection_end(state) == 11u);

    // 늘리지 않는 이동은 선택의 끝으로 접힌다.
    gitman::text_edit_move(state, gitman::text_edit_motion::right, false);
    REQUIRE_FALSE(gitman::text_edit_has_selection(state));
    REQUIRE(state.caret == 11u);

    gitman::text_edit_select_all(state);
    gitman::text_edit_insert(state, u8"develop");
    REQUIRE(state.text == u8"develop");
    REQUIRE(state.caret == 7u);

    // 선택이 있으면 backspace는 그 범위를 지운다.
    gitman::text_edit_select_all(state);
    gitman::text_edit_backspace(state);
    REQUIRE(state.text.empty());
    REQUIRE(state.caret == 0u);
}

TEST_CASE("Text edit selects the word under an offset", "[domain][text-edit]")
{
    gitman::text_edit_state state { at_end(u8"/branches/2026-08") };

    gitman::text_edit_select_word(state, 3u);
    REQUIRE(gitman::text_edit_selected_text(state) == u8"branches");

    // 구분자 위에서는 그 구분자 덩어리를 고른다.
    gitman::text_edit_select_word(state, 9u);
    REQUIRE(gitman::text_edit_selected_text(state) == u8"/");

    gitman::text_edit_select_word(state, 12u);
    REQUIRE(gitman::text_edit_selected_text(state) == u8"2026");

    // 빈 글에서는 선택이 생기지 않는다.
    gitman::text_edit_state empty {};
    gitman::text_edit_select_word(empty, 4u);
    REQUIRE_FALSE(gitman::text_edit_has_selection(empty));
}

TEST_CASE("Text edit drag selection keeps the anchor", "[domain][text-edit]")
{
    gitman::text_edit_state state { at_end(u8"origin/main") };

    // 누른 자리가 anchor이고 끌면 caret만 따라간다.
    gitman::text_edit_place_caret(state, 2u, false);
    gitman::text_edit_place_caret(state, 6u, true);
    REQUIRE(state.anchor == 2u);
    REQUIRE(state.caret == 6u);
    REQUIRE(gitman::text_edit_selected_text(state) == u8"igin");

    // 왼쪽으로 되돌아가면 범위가 뒤집힌다.
    gitman::text_edit_place_caret(state, 0u, true);
    REQUIRE(gitman::text_edit_selection_begin(state) == 0u);
    REQUIRE(gitman::text_edit_selection_end(state) == 2u);
}

TEST_CASE("Text edit clamps offsets when the text changes underneath", "[domain][text-edit]")
{
    gitman::text_edit_state state { at_end(u8"origin/feature") };
    gitman::text_edit_select_all(state);

    gitman::text_edit_set_text(state, u8"dev", false);
    REQUIRE(state.caret == 3u);
    REQUIRE(state.anchor == 0u);

    gitman::text_edit_set_text(state, u8"/trunk", true);
    REQUIRE(state.caret == 6u);
    REQUIRE(state.anchor == 6u);
}

TEST_CASE("Text edit encodes BMP characters as UTF-8", "[domain][text-edit]")
{
    REQUIRE(gitman::text_edit_encode_utf8(U'a') == u8"a");
    REQUIRE(gitman::text_edit_encode_utf8(U'é') == u8"é");
    REQUIRE(gitman::text_edit_encode_utf8(U'가') == u8"가");
}
