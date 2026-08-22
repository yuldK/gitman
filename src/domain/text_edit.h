#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 한 줄 텍스트 박스의 편집 상태다 (switch-dialog-query-bar-design R2.1).
    // `caret`과 `anchor`는 UTF-8 byte offset이며 항상 문자 경계에 붙는다. 둘이 같으면
    // 선택이 없고, 다르면 그 사이가 선택 범위다. 글꼴도 화면도 모르는 순수 모델이라
    // logic thread가 그대로 소유한다.
    // 실행 취소가 되돌리는 한 시점이다.
    struct text_edit_revision
    {
        std::u8string text {};
        std::size_t caret { 0 };
        std::size_t anchor { 0 };
    };

    // 연속된 편집을 한 덩어리로 묶는 기준이다. 종류가 같고 caret이 이어지는 동안은
    // 한 번의 실행 취소로 함께 되돌아간다 — 글자마다 되돌리면 쓸 수 없다.
    enum class text_edit_group
    {
        // 다음 편집은 무조건 새 덩어리다 (caret 이동·선택 뒤).
        none,
        insert,
        erase,
        // 선택 대체·붙여넣기처럼 한 번에 크게 바뀌는 편집이다. 절대 묶이지 않는다.
        replace,
    };

    // 실행 취소 기록의 상한이다. 한 줄 칸이라 이 정도면 충분하다.
    inline constexpr std::size_t text_edit_history_limit { 100 };

    struct text_edit_state
    {
        std::u8string text {};
        std::size_t caret { 0 };
        std::size_t anchor { 0 };

        // 실행 취소·복구 기록이다 (2026-08-23 지시). 편집이 새 덩어리를 열 때 이전
        // 시점을 undo에 쌓고 redo를 비운다.
        std::vector<text_edit_revision> undo {};
        std::vector<text_edit_revision> redo {};
        text_edit_group group { text_edit_group::none };
        // 마지막 편집이 끝난 caret 자리다. 여기서 이어지는 같은 종류의 편집만 묶는다.
        std::size_t group_caret { 0 };
    };

    enum class text_edit_motion
    {
        left,
        right,
        // 낱말 단위 이동이다 (Ctrl+←/→). 낱말 문자는 ASCII 영숫자와 비 ASCII
        // 바이트이고 나머지는 구분자다 — branch 이름의 `/`·`-`에서 멈춘다.
        word_left,
        word_right,
        line_start,
        line_end,
    };

    // BMP 문자 하나를 UTF-8 조각으로 만든다. win32의 WM_CHAR가 surrogate를 걸러
    // 주므로 3바이트까지면 충분하다.
    [[nodiscard]] std::u8string text_edit_encode_utf8(char32_t character);

    [[nodiscard]] std::size_t text_edit_selection_begin(const text_edit_state& state) noexcept;
    [[nodiscard]] std::size_t text_edit_selection_end(const text_edit_state& state) noexcept;
    [[nodiscard]] bool text_edit_has_selection(const text_edit_state& state) noexcept;
    [[nodiscard]] std::u8string_view text_edit_selected_text(const text_edit_state& state) noexcept;

    // offset을 글 안의 문자 경계로 맞춘다. 범위를 벗어나면 끝으로 자른다.
    [[nodiscard]] std::size_t text_edit_clamp_offset(std::u8string_view text, std::size_t offset) noexcept;

    // 선택이 있으면 지우고 caret을 그 자리에 둔다. 실제로 지웠을 때만 true다.
    bool text_edit_erase_selection(text_edit_state& state);

    // 선택을 대체하며 넣는다. 빈 조각은 아무 일도 하지 않는다.
    void text_edit_insert(text_edit_state& state, std::u8string_view text);

    // 선택이 있으면 선택을, 없으면 caret 앞뒤 문자 하나를 지운다.
    void text_edit_backspace(text_edit_state& state);
    void text_edit_delete_forward(text_edit_state& state);

    // Ctrl+Backspace·Ctrl+Delete다. 선택이 있으면 선택을, 없으면 낱말 하나를 지운다.
    void text_edit_delete_word_left(text_edit_state& state);
    void text_edit_delete_word_right(text_edit_state& state);

    // 한 덩어리씩 되돌리고 다시 실행한다. 기록이 없으면 아무 일도 하지 않으며,
    // 실제로 바뀌었을 때만 true다.
    bool text_edit_undo(text_edit_state& state);
    bool text_edit_redo(text_edit_state& state);

    // `extend`면 anchor를 두고 caret만 옮겨 선택을 늘린다. 아니면 선택을 접는다
    // (선택이 있을 때 ←/→는 그 끝으로 접힌다).
    void text_edit_move(text_edit_state& state, text_edit_motion motion, bool extend);

    void text_edit_select_all(text_edit_state& state);

    // 그 자리의 낱말을 고른다 (더블 클릭). 구분자 위면 그 구분자 덩어리를 고른다.
    void text_edit_select_word(text_edit_state& state, std::size_t offset);

    // 포인터가 가리킨 자리로 caret을 옮긴다. `extend`면 드래그 중이라 anchor를 둔다.
    void text_edit_place_caret(text_edit_state& state, std::size_t offset, bool extend);

    // 글을 밖에서 바꾼다 (SVN 노드 선택 같은 외부 갱신). caret·anchor는 범위 안
    // 문자 경계로 되돌아가고, 사용자의 편집이 아니므로 실행 취소 기록을 비운다.
    void text_edit_set_text(text_edit_state& state, std::u8string_view text, bool caret_to_end);
} // namespace gitman
