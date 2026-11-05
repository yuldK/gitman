#include "presentation/ui/function_bar_element.h"

#include "presentation/function_presentation.h"
#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_icon_catalog.h"
#include "presentation/ui/function_marker_element.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/button_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/text_block.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"

#include <memory>
#include <utility>

namespace gitman::ui {
    namespace {
        // 카드 이름과 힌트의 글 불투명도다. 로그 헤더의 보조 글(`log_view_element`)처럼 버튼 라벨보다 옅다.
        constexpr float name_alpha { 0.75f };
        constexpr float hint_alpha { 0.6f };

        // 카드 이름과 힌트를 그리는 한 줄 규칙이다. 줄였는지 재는 `measure_text_block`과 그리는 `draw_text_line`이 같은 값을 쓴다.
        [[nodiscard]] luil::text_block_style label_style()
        {
            luil::text_block_style style {};
            style.font = { .size = layout_function_bar_label_size };
            style.elide = luil::text_elide::end;
            return style;
        }

        char32_t button_glyph(const card_function_view& function) noexcept
        {
            return function.running ? codicons::icon_stop_circle : function_icon_glyph(function.icon);
        }
    } // namespace

    function_bar_element::function_bar_element(function_bar_view bar)
        : ui_element { ui_element_id { kind_function_bar } }
        , bar_ { std::move(bar) }
    {
        // 빈 곳 클릭이 root의 선택 해제로 새지 않게 한다 (`log_view_element`와 같다, 2.4).
        set_hit_opaque(true);
        set_tab_stop(false);

        for (const card_function_view& function : bar_.functions)
        {
            luil::text_button_config config {};
            config.text = function_bar_text(function);
            config.glyph = button_glyph(function);
            // 실행 중이면 제자리에서 중지 버튼이 된다. 카드 버튼의 켜진 토글처럼 강조 바탕이다 (2.3.5).
            if (function.running)
            {
                config.fill = [](const ui_color_palette& palette) { return palette.accent; };
                config.label_color = [](const ui_color_palette& palette) { return palette.accent_foreground; };
            }
            auto button { std::make_unique<luil::text_button_element>(ui_element_id { kind_function_bar_button, card_function_owner(bar_.card.value, function.button_id) }, std::move(config)) };
            // 경과 시간은 넣지 않는다. tooltip도 snapshot마다 다시 지어지므로 시간을 넣으면 접근 이름처럼 흔들린다
            // (T9의 카드 버튼과 같다). 흐르는 시간은 카드 3번째 줄 칩과 로그 헤더가 보인다.
            button->set_tooltip(function_button_tooltip(function));
            button->set_live_region(luil::access_live::polite);
            // 비활성 버튼에도 액션을 단다. luil은 비활성 element의 액션을 실행하지 않는다 (우클릭도, `Shift+F10`도). 카드
            // 버튼처럼 logic이 누른 시점에 다시 판정한다.
            if (function.running)
                button->set_action(ui_trigger::left_click, make_logic_click(stop_function_intent { bar_.card, function.button_id }));
            else
                button->set_action(ui_trigger::left_click, make_logic_click(run_function_intent { bar_.card, function.button_id }));
            // 배치 메뉴다 (2.2.5). 우클릭은 누른 점, `Shift+F10`은 luil이 버튼 가운데를 준다.
            button->set_action(ui_trigger::right_click
                , [card = bar_.card, id = function.button_id](const ui_action_context& context) -> std::vector<input_action>
                {
                    return { make_logic_action(open_function_placement_menu_intent { card, id, true, context.x, context.y }) };
                }
            );
            if (function.enabled == false)
                button->set_enabled(false);
            buttons_.push_back({ button.get(), nullptr, function_bar_widest_text(function.label), function_icon_glyph(function.icon) });
            add_child(std::move(button));
        }

        auto more { std::make_unique<luil::button_element>(ui_element_id { kind_function_bar_more }, luil::button_config { .glyph = codicons::icon_ellipsis }) };
        more->set_tooltip(u8"기능 더 보기");
        // 선택 카드의 기능 메뉴다 (2.3.2와 같은 메뉴). 선택이 없으면 비활성이다 (2.2.2 규칙 5).
        //  - 비활성이어도 액션을 단다 (S4). luil은 비활성 element의 액션을 실행하지 않고, 액션이 있어야 접근 tree에 단추로
        //    서서 사유가 읽힌다. 선택이 없을 때의 카드 id는 비어 있어 logic도 메뉴를 열지 않는다 (`handle_open_function_menu`).
        const ui_element* const anchor { more.get() };
        more->set_action(ui_trigger::left_click
            , [card = bar_.card, anchor](const ui_action_context&) -> std::vector<input_action>
            {
                const rect_f box { anchor->bounds() };
                return { make_logic_action(open_function_menu_intent { card, box.x, box.y + box.height }) };
            }
        );
        if (bar_.card.value.empty() == false)
            more->set_access_name(function_bar_more_access_name(bar_.card_name, bar_.total_functions));
        else
        {
            // 이름은 무엇인지만, 막힌 사유는 tooltip(UIA HelpText)이다. 사유는 줄이 그리는 힌트와 같다.
            more->set_access_name(u8"기능 더 보기 (기능 줄)");
            more->set_tooltip(bar_.hint);
            more->set_enabled(false);
        }
        more_ = more.get();
        add_child(std::move(more));

        // 실패 표식은 버튼 위에 그려야 하므로 모든 버튼 뒤에 붙인다 (2.3.5의 기능 줄 열). 카드의 실패 표식과 같은 마름모다
        // (D35). 실패는 버튼 글(`빌드 · 실패`)도 말한다.
        for (std::size_t index = 0; index < buttons_.size(); ++index)
        {
            const card_function_view& function { bar_.functions[index] };
            if (function.failed == false || function.running)
                continue;
            auto marker { std::make_unique<function_marker_element>(ui_element_id { kind_function_bar_marker, card_function_owner(bar_.card.value, function.button_id) }, function_marker_shape::failed) };
            buttons_[index].marker = marker.get();
            add_child(std::move(marker));
        }
    }

    void function_bar_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        // 줄 높이는 버튼 + 위아래 4다. 큰 글자에서는 줄이 자라고 버튼도 slot 높이만큼 자란다 (2.2.4).
        const float inset { layout_function_bar_inset * scale };
        const float button_height { context.slot.height - inset * 2.0f > 0.0f ? context.slot.height - inset * 2.0f : 0.0f };
        const float button_y { context.slot.y + inset };
        const function_bar_slots slots { layout_function_bar_slots(context.slot.width / scale, bar_.card.value.empty() == false) };

        name_box_ = { context.slot.x + layout_margin * scale, context.slot.y, slots.name_width * scale, context.slot.height };
        separator_x_.reset();
        if (slots.name_width > 0.0f)
            separator_x_ = context.slot.x + (slots.separator_x + 0.5f) * scale;
        hint_box_ = { context.slot.x + slots.buttons_left * scale, context.slot.y, (slots.buttons_right - slots.buttons_left) * scale, context.slot.height };

        // 줄여 그리는 글은 줄의 tooltip으로 다 보인다 (luil-ui 검토 목록). 접근 이름에는 늘 전체 글이 있다. 그리기와 같은
        // 규칙·측정기로 재므로 줄였는지가 그린 글과 어긋나지 않는다.
        const luil::measure_context measure { scale, context.measurer, context.metrics };
        const luil::text_block_style style { label_style() };
        std::u8string tooltip {};
        if (slots.name_width > 0.0f && measure_text_block(measure, bar_.card_name, style, slots.name_width).truncated)
            tooltip = bar_.card_name;
        if (bar_.hint.empty() == false && slots.buttons_right > slots.buttons_left && measure_text_block(measure, bar_.hint, style, slots.buttons_right - slots.buttons_left).truncated)
        {
            if (tooltip.empty() == false)
                tooltip += u8": ";
            tooltip += bar_.hint;
        }
        set_tooltip(std::move(tooltip));

        const float more_size { layout_button_size * scale };
        more_->arrange(context.for_child({ context.slot.x + slots.more_x * scale, context.slot.y + (context.slot.height - more_size) / 2.0f, more_size, more_size }));

        // 버튼 폭은 가장 긴 상태 글을 실은 글자 버튼의 선호 폭이다 — 그리는 버튼과 같은 측정 규칙(luil
        // `text_button_element::preferred_size`)이라 글이 잘리지 않는다. 배치의 측정기를 그대로 넘긴다.
        std::vector<float> widths {};
        widths.reserve(buttons_.size());
        for (const button_slot& slot : buttons_)
        {
            const luil::text_button_element probe { ui_element_id {}, luil::text_button_config { .text = slot.widest, .glyph = slot.glyph } };
            const std::optional<luil::size_f> preferred { probe.preferred_size(measure, 0.0f) };
            widths.push_back(function_bar_button_width(preferred.has_value() ? preferred->width : 0.0f));
        }

        const std::size_t visible { function_bar_visible_count(slots, widths) };
        float left { slots.buttons_left };
        for (std::size_t index = 0; index < buttons_.size(); ++index)
        {
            const button_slot& slot { buttons_[index] };
            const bool shown { index < visible };
            slot.button->set_visible(shown);
            if (slot.marker != nullptr)
                slot.marker->set_visible(shown);
            if (shown == false)
                continue;
            const rect_f box { context.slot.x + left * scale, button_y, widths[index] * scale, button_height };
            slot.button->arrange(context.for_child(box));
            if (slot.marker != nullptr)
                slot.marker->arrange(context.for_child(function_marker_slot(box, scale, function_marker_shape::failed)));
            left += widths[index] + layout_card_gap;
        }
    }

    void function_bar_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };

        // 로그 pane과 같은 표면색과 위쪽 경계선이다. 아래 로그 헤더의 경계선이 줄과 로그를 가른다.
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.surface_background));
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, 1.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.15f)));

        if (name_box_.width > 0.0f)
        {
            SkPaint name_paint { solid_paint(context.palette.primary_foreground) };
            name_paint.setAlphaf(name_alpha);
            luil::draw_text_block(context, bar_.card_name, name_box_, label_style(), name_paint);
        }
        // 이름과 버튼 사이의 1px 구분선이다. 카드 열의 구분선처럼 버튼보다 짧다.
        if (separator_x_.has_value())
        {
            const float line_height { layout_card_function_separator_height * scale };
            const SkRect line { SkRect::MakeXYWH(*separator_x_ - 0.5f * scale, box.y + (box.height - line_height) / 2.0f, 1.0f * scale, line_height) };
            context.canvas.drawRect(line, solid_paint(context.palette.divider));
        }
        if (bar_.hint.empty() == false && hint_box_.width > 0.0f)
        {
            SkPaint hint_paint { solid_paint(context.palette.primary_foreground) };
            hint_paint.setAlphaf(hint_alpha);
            luil::draw_text_block(context, bar_.hint, hint_box_, label_style(), hint_paint);
        }

        draw_children(context, interaction);
    }

    access_info function_bar_element::accessibility() const
    {
        std::u8string name { u8"기능 줄" };
        if (bar_.card_name.empty() == false)
        {
            name += u8", ";
            name += bar_.card_name;
        }
        if (bar_.hint.empty() == false)
        {
            name += u8", ";
            name += bar_.hint;
        }
        return { .role = access_role::tool_bar, .name = std::move(name) };
    }
} // namespace gitman::ui
