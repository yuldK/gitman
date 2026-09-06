#pragma once

// 한 줄 텍스트 편집 상태 기계는 luil의 것을 그대로 쓴다 (ADR-007). gitman 코드는
// `gitman::` 이름으로 계속 부르므로 여기서 전달만 한다. 순수 모델이라 logic thread가
// 그대로 소유하며 UI 헤더를 끌어오지 않는다.
#include "luil/text/text_edit.h"

namespace gitman {
    using luil::text::text_edit_backspace;
    using luil::text::text_edit_clamp_offset;
    using luil::text::text_edit_command;
    using luil::text::text_edit_delete_forward;
    using luil::text::text_edit_delete_word_left;
    using luil::text::text_edit_delete_word_right;
    using luil::text::text_edit_encode_utf8;
    using luil::text::text_edit_erase_selection;
    using luil::text::text_edit_group;
    using luil::text::text_edit_has_selection;
    using luil::text::text_edit_history_limit;
    using luil::text::text_edit_insert;
    using luil::text::text_edit_motion;
    using luil::text::text_edit_move;
    using luil::text::text_edit_place_caret;
    using luil::text::text_edit_redo;
    using luil::text::text_edit_replace_all;
    using luil::text::text_edit_revision;
    using luil::text::text_edit_select_all;
    using luil::text::text_edit_select_word;
    using luil::text::text_edit_selected_text;
    using luil::text::text_edit_selection_begin;
    using luil::text::text_edit_selection_end;
    using luil::text::text_edit_set_text;
    using luil::text::text_edit_state;
    using luil::text::text_edit_undo;
} // namespace gitman
