#pragma once

// element 구현이 쓰는 luil 어휘를 `gitman::ui` 안으로 들인다 (ADR-007). element
// 파일은 예전처럼 `rect_f`·`draw_context`·`solid_paint`를 한정자 없이 쓰고, 기반
// 계층이 luil이라는 사실은 이 헤더 한 곳에 드러난다. UI 계층(presentation/ui,
// platform/win32의 창 조립)만 include한다 — application·domain은 luil UI 헤더를
// 들이지 않는다 (설계 1장).
#include "luil/generated/codicons.h"
#include "luil/text/font_style.h"
#include "luil/theme/ui_theme.h"
#include "luil/ui/draw_primitives.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/text_block.h"
#include "luil/ui/text_input_state.h"
#include "luil/ui/ui_element.h"
#include "luil/ui/ui_element_id.h"
#include "luil/ui/ui_events.h"
#include "luil/ui/ui_tree.h"

#include "include/core/SkFont.h"
#include "include/core/SkTypeface.h"

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

    // ─── 직접 그리는 글의 글꼴 (text-font-migration-design 3.1) ────────────────
    //
    // 크기는 논리 픽셀이다. 배율과 시스템 글자 크기는 luil `make_text_font`가 곱하고,
    // 굵기는 그 굵기의 진짜 face로 건다. element가 `SkFont { typeface, size * scale }`를
    // 손으로 짓지 않는다 — 크기 규칙이 바뀔 때 그 자리만 따라오지 못한다.

    // UI 글꼴이다.
    [[nodiscard]] inline SkFont ui_font(const draw_context& context, const float size, const luil::font_weight weight = luil::font_weight::regular)
    {
        return luil::make_text_font(context, { .size = size, .weight = weight });
    }

    // 로그·diff 본문의 코드 글꼴이다. 문맥에 코드 글꼴이 없으면 luil이 UI 글꼴로 물러선다.
    [[nodiscard]] inline SkFont code_font(const draw_context& context, const float size)
    {
        return luil::make_text_font(context, { .size = size, .face = luil::text_layout_face::code });
    }

    // 한 줄 글의 규칙이다 (text-font-migration-design 6.2). 크기는 논리 픽셀이다.
    struct text_line_style
    {
        float size { 12.0f };
        luil::font_weight weight { luil::font_weight::regular };
        luil::text_layout_face face { luil::text_layout_face::ui };
        // 넘치면 줄이는 자리다. 경로는 `start`라 폴더 이름(끝)이 남는다.
        luil::text_elide elide { luil::text_elide::end };
        luil::text_align align { luil::text_align::start };
        // 있으면 `face` 대신 이 글꼴이다 (글꼴 선택 dialog의 미리 보기).
        sk_sp<SkTypeface> typeface {};
    };

    // 한 줄 글을 `box`(물리 픽셀)에 세로 가운데로 놓아 그린다. 기준선은 지금까지 손으로
    // 셈하던 `box.y + centered_text_baseline(font, box.height)`와 같다. 그린 자리를 돌려준다.
    inline rect_f draw_text_line(draw_context& context, const std::u8string_view text, const rect_f& box, const SkPaint& paint, const text_line_style& line)
    {
        luil::text_block_style style {};
        style.font = { .size = line.size, .face = line.face, .weight = line.weight, .typeface = line.typeface };
        style.align = line.align;
        style.vertical = luil::text_vertical_align::center;
        style.elide = line.elide;
        return luil::draw_text_block(context, text, box, style, paint);
    }

    // codicon 글리프다. 아이콘은 글자 크기 규칙을 따르지 않고 화면 배율만 곱한다
    // (luil `font_scale.h`, luil 단추도 같다).
    [[nodiscard]] inline SkFont icon_font(const draw_context& context, const float size)
    {
        return SkFont { sk_ref_sp(context.codicon_typeface), size * (context.scale > 0.0f ? context.scale : 1.0f) };
    }
} // namespace gitman::ui
