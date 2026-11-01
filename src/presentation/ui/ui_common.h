#pragma once

// element 구현이 쓰는 luil 어휘를 `gitman::ui` 안으로 들인다 (ADR-007). element
// 파일은 예전처럼 `rect_f`·`draw_context`·`solid_paint`를 한정자 없이 쓰고, 기반
// 계층이 luil이라는 사실은 이 헤더 한 곳에 드러난다. UI 계층(presentation/ui,
// platform/win32의 창 조립)만 include한다 — application·domain은 luil UI 헤더를
// 들이지 않는다 (설계 1장).
#include "luil/generated/codicons.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_element_id.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"

namespace gitman::ui {
    // 코디콘 코드포인트 표다. 자산은 luil의 것과 byte 단위로 같다 (D7).
    namespace codicons = luil::codicons;

    // element 계약.
    using luil::access_info;
    using luil::access_role;
    using luil::arrange_context;
    using luil::draw_context;
    using luil::drag_payload;
    using luil::drag_source;
    using luil::drag_visual;
    using luil::drop_target;
    using luil::font_resolver;
    using luil::interaction_snapshot;
    using luil::pointer_drag_target;
    using luil::rect_f;
    using luil::text_input_snapshot;
    using luil::text_input_view;
    using luil::text_measurer;
    using luil::ui_action;
    using luil::ui_action_context;
    using luil::ui_cursor;
    using luil::ui_element;
    using luil::ui_element_id;
    using luil::ui_element_kind;
    using luil::ui_tree;
    using luil::ui_trigger;
    using luil::update_context;

    // 액션과 입력 어휘.
    using luil::app_ui_command;
    using luil::clipboard_copy_request;
    using luil::clipboard_paste_request;
    using luil::input_action;
    using luil::key_code;
    using luil::ui_command;

    // 팔레트와 그리기 조각.
    using luil::centered_text_baseline;
    using luil::color_theme;
    using luil::draw_centered_glyph;
    using luil::draw_downward_shadow;
    using luil::draw_hover_fill;
    using luil::draw_spinning_glyph;
    using luil::draw_text;
    using luil::draw_text_within;
    using luil::drawable_text;
    using luil::elide_text;
    using luil::make_ui_color;
    using luil::measure_text;
    using luil::solid_paint;
    using luil::spinner_angle;
    using luil::spinner_period;
    using luil::split_text_runs;
    using luil::text_run;
    using luil::ui_color;
    using luil::ui_color_palette;
    using luil::with_alpha;

    // 배치 상수·함수.
    using luil::clamp_scroll;
    using luil::tooltip_delay;
} // namespace gitman::ui
