#include "domain/text_edit.h"

#include <algorithm>
#include <utility>

namespace gitman {
    namespace {
        bool is_continuation(const char8_t value) noexcept
        {
            return (static_cast<unsigned int>(value) & 0xC0u) == 0x80u;
        }

        // 낱말 문자다. 비 ASCII는 모두 낱말로 본다 — 한글 경로 조각이 통째로 움직인다.
        bool is_word_byte(const char8_t value) noexcept
        {
            if (value >= 0x80u)
                return true;
            return (value >= u8'a' && value <= u8'z') || (value >= u8'A' && value <= u8'Z') || (value >= u8'0' && value <= u8'9');
        }

        std::size_t previous_boundary(const std::u8string_view text, std::size_t offset) noexcept
        {
            if (offset == 0)
                return 0;
            --offset;
            while (offset > 0 && is_continuation(text[offset]))
                --offset;
            return offset;
        }

        std::size_t next_boundary(const std::u8string_view text, std::size_t offset) noexcept
        {
            if (offset >= text.size())
                return text.size();
            ++offset;
            while (offset < text.size() && is_continuation(text[offset]))
                ++offset;
            return offset;
        }

        // 왼쪽으로: 구분자를 먼저 지나고 그다음 낱말을 지난다.
        std::size_t word_left(const std::u8string_view text, std::size_t offset) noexcept
        {
            while (offset > 0 && is_word_byte(text[previous_boundary(text, offset)]) == false)
                offset = previous_boundary(text, offset);
            while (offset > 0 && is_word_byte(text[previous_boundary(text, offset)]))
                offset = previous_boundary(text, offset);
            return offset;
        }

        // 오른쪽으로: 낱말을 먼저 지나고 그다음 구분자를 지나 다음 낱말의 시작에 선다.
        std::size_t word_right(const std::u8string_view text, std::size_t offset) noexcept
        {
            while (offset < text.size() && is_word_byte(text[offset]))
                offset = next_boundary(text, offset);
            while (offset < text.size() && is_word_byte(text[offset]) == false)
                offset = next_boundary(text, offset);
            return offset;
        }
        // 선택 범위만 지운다. 기록은 호출한 쪽이 연다.
        void erase_selection_only(text_edit_state& state)
        {
            if (text_edit_has_selection(state) == false)
                return;
            const std::size_t begin { text_edit_selection_begin(state) };
            state.text.erase(begin, text_edit_selection_end(state) - begin);
            state.caret = begin;
            state.anchor = begin;
        }

        // 새 편집 덩어리를 연다. 종류가 같고 caret이 직전 편집이 끝난 자리에서
        // 이어질 때만 묶어 기록을 남기지 않는다.
        void begin_edit(text_edit_state& state, const text_edit_group group)
        {
            const bool joins { group != text_edit_group::replace && group == state.group && state.caret == state.group_caret && state.undo.empty() == false };
            if (joins == false)
            {
                state.undo.push_back({ state.text, state.caret, state.anchor });
                if (state.undo.size() > text_edit_history_limit)
                    state.undo.erase(state.undo.begin());
            }
            state.redo.clear();
            state.group = group;
        }

        // 편집이 끝난 자리를 기억한다. 다음 같은 종류의 편집이 여기서 이어지면 묶인다.
        void end_edit(text_edit_state& state)
        {
            state.group_caret = state.caret;
        }

        // caret 이동·선택은 다음 편집을 새 덩어리로 만든다.
        void break_group(text_edit_state& state)
        {
            state.group = text_edit_group::none;
        }
    } // namespace

    std::u8string text_edit_encode_utf8(const char32_t character)
    {
        std::u8string result {};
        if (character < 0x80u)
        {
            result.push_back(static_cast<char8_t>(character));
            return result;
        }
        if (character < 0x800u)
        {
            result.push_back(static_cast<char8_t>(0xC0u | (character >> 6)));
            result.push_back(static_cast<char8_t>(0x80u | (character & 0x3Fu)));
            return result;
        }
        result.push_back(static_cast<char8_t>(0xE0u | (character >> 12)));
        result.push_back(static_cast<char8_t>(0x80u | ((character >> 6) & 0x3Fu)));
        result.push_back(static_cast<char8_t>(0x80u | (character & 0x3Fu)));
        return result;
    }

    std::size_t text_edit_selection_begin(const text_edit_state& state) noexcept
    {
        return state.caret < state.anchor ? state.caret : state.anchor;
    }

    std::size_t text_edit_selection_end(const text_edit_state& state) noexcept
    {
        return state.caret < state.anchor ? state.anchor : state.caret;
    }

    bool text_edit_has_selection(const text_edit_state& state) noexcept
    {
        return state.caret != state.anchor;
    }

    std::u8string_view text_edit_selected_text(const text_edit_state& state) noexcept
    {
        const std::size_t begin { text_edit_selection_begin(state) };
        return std::u8string_view { state.text }.substr(begin, text_edit_selection_end(state) - begin);
    }

    std::size_t text_edit_clamp_offset(const std::u8string_view text, std::size_t offset) noexcept
    {
        if (offset >= text.size())
            return text.size();
        // 이어지는 바이트 한가운데를 가리키면 그 문자의 시작으로 되돌린다.
        while (offset > 0 && is_continuation(text[offset]))
            --offset;
        return offset;
    }

    bool text_edit_erase_selection(text_edit_state& state)
    {
        if (text_edit_has_selection(state) == false)
            return false;
        begin_edit(state, text_edit_group::replace);
        erase_selection_only(state);
        end_edit(state);
        return true;
    }

    void text_edit_insert(text_edit_state& state, const std::u8string_view text)
    {
        if (text.empty())
            return;
        // 선택을 덮어쓰거나 한 글자보다 긴 조각을 넣는 것은 묶지 않는다 — 붙여넣기와
        // 대체는 한 번에 되돌아가야 한다.
        const bool replaces { text_edit_has_selection(state) || text.size() > 3u };
        begin_edit(state, replaces ? text_edit_group::replace : text_edit_group::insert);
        erase_selection_only(state);
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        state.text.insert(state.caret, text);
        state.caret += text.size();
        state.anchor = state.caret;
        end_edit(state);
    }

    void text_edit_backspace(text_edit_state& state)
    {
        if (text_edit_has_selection(state))
        {
            begin_edit(state, text_edit_group::replace);
            erase_selection_only(state);
            end_edit(state);
            return;
        }
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        if (state.caret == 0)
            return;
        begin_edit(state, text_edit_group::erase);
        const std::size_t begin { previous_boundary(state.text, state.caret) };
        state.text.erase(begin, state.caret - begin);
        state.caret = begin;
        state.anchor = begin;
        end_edit(state);
    }

    void text_edit_delete_forward(text_edit_state& state)
    {
        if (text_edit_has_selection(state))
        {
            begin_edit(state, text_edit_group::replace);
            erase_selection_only(state);
            end_edit(state);
            return;
        }
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        if (state.caret >= state.text.size())
            return;
        begin_edit(state, text_edit_group::erase);
        const std::size_t end { next_boundary(state.text, state.caret) };
        state.text.erase(state.caret, end - state.caret);
        state.anchor = state.caret;
        end_edit(state);
    }

    void text_edit_delete_word_left(text_edit_state& state)
    {
        if (text_edit_has_selection(state) == false)
        {
            state.caret = text_edit_clamp_offset(state.text, state.caret);
            state.anchor = word_left(state.text, state.caret);
        }
        if (text_edit_has_selection(state) == false)
            return;
        begin_edit(state, text_edit_group::replace);
        erase_selection_only(state);
        end_edit(state);
    }

    void text_edit_delete_word_right(text_edit_state& state)
    {
        if (text_edit_has_selection(state) == false)
        {
            state.caret = text_edit_clamp_offset(state.text, state.caret);
            state.anchor = word_right(state.text, state.caret);
        }
        if (text_edit_has_selection(state) == false)
            return;
        begin_edit(state, text_edit_group::replace);
        erase_selection_only(state);
        end_edit(state);
    }

    bool text_edit_undo(text_edit_state& state)
    {
        if (state.undo.empty())
            return false;
        state.redo.push_back({ state.text, state.caret, state.anchor });
        const text_edit_revision previous { std::move(state.undo.back()) };
        state.undo.pop_back();
        state.text = previous.text;
        state.caret = text_edit_clamp_offset(state.text, previous.caret);
        state.anchor = text_edit_clamp_offset(state.text, previous.anchor);
        break_group(state);
        return true;
    }

    bool text_edit_redo(text_edit_state& state)
    {
        if (state.redo.empty())
            return false;
        state.undo.push_back({ state.text, state.caret, state.anchor });
        const text_edit_revision next { std::move(state.redo.back()) };
        state.redo.pop_back();
        state.text = next.text;
        state.caret = text_edit_clamp_offset(state.text, next.caret);
        state.anchor = text_edit_clamp_offset(state.text, next.anchor);
        break_group(state);
        return true;
    }

    void text_edit_move(text_edit_state& state, const text_edit_motion motion, const bool extend)
    {
        const std::u8string_view text { state.text };
        state.caret = text_edit_clamp_offset(text, state.caret);
        state.anchor = text_edit_clamp_offset(text, state.anchor);

        // 선택이 있을 때 늘리지 않는 ←/→는 선택의 끝으로 접힌다 (편집기 관례).
        if (extend == false && text_edit_has_selection(state) && (motion == text_edit_motion::left || motion == text_edit_motion::right))
        {
            state.caret = motion == text_edit_motion::left ? text_edit_selection_begin(state) : text_edit_selection_end(state);
            state.anchor = state.caret;
            break_group(state);
            return;
        }

        switch (motion)
        {
        case text_edit_motion::left:
            state.caret = previous_boundary(text, state.caret);
            break;
        case text_edit_motion::right:
            state.caret = next_boundary(text, state.caret);
            break;
        case text_edit_motion::word_left:
            state.caret = word_left(text, state.caret);
            break;
        case text_edit_motion::word_right:
            state.caret = word_right(text, state.caret);
            break;
        case text_edit_motion::line_start:
            state.caret = 0;
            break;
        case text_edit_motion::line_end:
            state.caret = text.size();
            break;
        }
        if (extend == false)
            state.anchor = state.caret;
        break_group(state);
    }

    void text_edit_select_all(text_edit_state& state)
    {
        state.anchor = 0;
        state.caret = state.text.size();
        break_group(state);
    }

    void text_edit_select_word(text_edit_state& state, const std::size_t offset)
    {
        const std::u8string_view text { state.text };
        const std::size_t at { text_edit_clamp_offset(text, offset) };
        if (text.empty())
        {
            state.caret = 0;
            state.anchor = 0;
            return;
        }

        // 끝에 섰으면 마지막 문자가 속한 덩어리를 고른다.
        const std::size_t sample { at < text.size() ? at : previous_boundary(text, at) };
        const bool word { is_word_byte(text[sample]) };
        std::size_t begin { sample };
        while (begin > 0 && is_word_byte(text[previous_boundary(text, begin)]) == word)
            begin = previous_boundary(text, begin);
        std::size_t end { sample };
        while (end < text.size() && is_word_byte(text[end]) == word)
            end = next_boundary(text, end);
        state.anchor = begin;
        state.caret = end;
        break_group(state);
    }

    void text_edit_place_caret(text_edit_state& state, const std::size_t offset, const bool extend)
    {
        state.caret = text_edit_clamp_offset(state.text, offset);
        if (extend == false)
            state.anchor = state.caret;
        else
            state.anchor = text_edit_clamp_offset(state.text, state.anchor);
        break_group(state);
    }

    void text_edit_set_text(text_edit_state& state, const std::u8string_view text, const bool caret_to_end)
    {
        // 사용자의 편집이 아니라 밖에서 온 값이다. 되돌릴 것이 없다.
        state.undo.clear();
        state.redo.clear();
        break_group(state);
        state.text = text;
        if (caret_to_end)
        {
            state.caret = state.text.size();
            state.anchor = state.caret;
            return;
        }
        state.caret = text_edit_clamp_offset(state.text, state.caret);
        state.anchor = text_edit_clamp_offset(state.text, state.anchor);
        state.group_caret = state.caret;
    }
} // namespace gitman
