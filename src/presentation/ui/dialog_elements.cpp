#include "presentation/ui/dialog_elements.h"

#include "presentation/ui/draw_primitives.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <cmath>
#include <utility>

namespace gitman::ui {
    text_button_element::text_button_element(const ui_element_id id, std::u8string text, const bool accent)
        : ui_element { id }
        , text_ { std::move(text) }
        , accent_ { accent }
    {}

    void text_button_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
    }

    void text_button_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { 3.0f * scale };

        ui_color background { accent_ ? with_alpha(context.palette.accent_soft, 0.25f) : with_alpha(context.palette.primary_foreground, 0.08f) };
        if (enabled() && interaction.pressed == id())
            background = context.palette.button_pressed_background;
        else if (enabled() && interaction.hovered == id())
            background = accent_ ? with_alpha(context.palette.accent_hover, 0.35f) : context.palette.button_hover_background;
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(background));

        const SkFont font { sk_ref_sp(context.ui_typeface), 12.0f * scale };
        SkPaint foreground { solid_paint(accent_ ? context.palette.accent_emphasis_foreground : context.palette.primary_foreground) };
        if (enabled() == false)
            foreground.setAlphaf(0.4f);
        const float text_width { measure_text(text_, font) };
        draw_text(context.canvas, text_, box.x + (box.width - text_width) / 2.0f, box.y + centered_text_baseline(font, box.height), font, foreground);
    }

    text_input_element::text_input_element(const ui_element_id id, text_input_view value, std::u8string placeholder)
        : ui_element { id }
        , value_ { std::move(value) }
        , placeholder_ { std::move(placeholder) }
    {
        // 누름·끌기·더블 클릭은 interaction controller가 직접 다룬다 — 포인터 x를
        // offset으로 옮기려면 측정 함수가 필요한데 그것은 입력 thread가 가진다
        // (switch-dialog-query-bar-design R2.3). 여기서는 클릭을 흡수해 hit test
        // 대상이 되게만 한다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });
    }

    float text_input_element::font_size() const noexcept
    {
        return 11.0f * scale_;
    }

    float text_input_element::text_left() const noexcept
    {
        return bounds().x + 7.0f * scale_;
    }

    float text_input_element::inner_width() const noexcept
    {
        const float width { bounds().width - 14.0f * scale_ };
        return width > 0.0f ? width : 0.0f;
    }

    std::u8string_view text_input_element::displayed_text() const noexcept
    {
        // 조합 중에는 조합 글이 섞인 쪽을 그린다 (E2.6). 확정된 글은 그 동안
        // logic의 초안에 그대로 남아 있다.
        return value_.composing ? std::u8string_view { value_.composition_text } : std::u8string_view { value_.text };
    }

    std::size_t text_input_element::displayed_caret() const noexcept
    {
        const std::u8string_view text { displayed_text() };
        const std::size_t caret { value_.composing ? value_.composition_caret : value_.caret };
        return caret < text.size() ? caret : text.size();
    }

    float text_input_element::scroll_for(const std::function<float(std::u8string_view)>& measure) const
    {
        const std::u8string_view text { displayed_text() };
        const float total { measure(text) };
        const float visible { inner_width() };
        float maximum { total - visible };
        if (maximum < 0.0f)
            maximum = 0.0f;

        // caret이 칸 밖으로 나가지 않을 만큼만 민다. 값은 글·caret·폭만으로 정해져
        // 그리기와 hit test가 같은 결과를 낸다.
        const float caret_x { measure(text.substr(0, displayed_caret())) };
        float scroll { caret_x - visible };
        if (scroll < 0.0f)
            scroll = 0.0f;
        if (scroll > caret_x)
            scroll = caret_x;
        if (scroll > maximum)
            scroll = maximum;
        return scroll;
    }

    void text_input_element::arrange(const arrange_context& context)
    {
        set_bounds(context.slot);
        scale_ = context.scale > 0.0f ? context.scale : 1.0f;
    }

    std::optional<text_input_snapshot> text_input_element::text_input() const
    {
        return text_input_snapshot { value_.text, value_.caret, value_.anchor };
    }

    std::optional<std::size_t> text_input_element::offset_at(const float x, const text_measurer& measurer) const
    {
        // 조합 중에는 포인터로 caret을 옮기지 않는다 — 조합은 IME가 소유한다.
        const std::u8string_view text { value_.text };
        if (measurer == nullptr)
            return text.size();

        const float size { font_size() };
        const auto measure { [&measurer, size](const std::u8string_view value) { return measurer(value, size); } };
        const float target { x - text_left() + scroll_for(measure) };
        if (target <= 0.0f)
            return std::size_t { 0 };

        // 문자 경계마다 재서 가장 가까운 자리를 고른다. 한 줄 박스라 글이 짧다.
        std::size_t best { 0 };
        float best_distance { target };
        for (std::size_t offset = 1; offset <= text.size(); ++offset)
        {
            // 이어지는 바이트(10xxxxxx)는 문자 경계가 아니다.
            if (offset < text.size() && (static_cast<unsigned int>(text[offset]) & 0xC0u) == 0x80u)
                continue;
            const float distance { std::abs(measure(text.substr(0, offset)) - target) };
            if (distance <= best_distance)
            {
                best_distance = distance;
                best = offset;
            }
        }
        return best;
    }

    void text_input_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const bool focused { interaction.focused_input == id() };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
        const float radius { 3.0f * scale };
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(with_alpha(context.palette.primary_foreground, 0.08f)));
        // 초점을 받은 칸은 테두리를 강조해 입력이 이곳으로 간다는 것을 보인다.
        SkPaint border { solid_paint(focused ? context.palette.accent : with_alpha(context.palette.primary_foreground, 0.35f)) };
        border.setStyle(SkPaint::kStroke_Style);
        border.setStrokeWidth(1.0f * scale);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

        const SkFont font { sk_ref_sp(context.ui_typeface), font_size() };
        const float left { text_left() };
        const float baseline { box.y + centered_text_baseline(font, box.height) };
        const float visible { inner_width() };

        const std::u8string_view text { displayed_text() };
        if (text.empty())
        {
            // 빈 값의 뜻은 안내가 설명한다. 초점이 없을 때만 흐리게 그린다.
            if (focused == false && placeholder_.empty() == false)
            {
                SkPaint placeholder { solid_paint(context.palette.primary_foreground) };
                placeholder.setAlphaf(0.5f);
                static_cast<void>(draw_text_within(context.canvas, placeholder_, left, baseline, visible, font, placeholder));
            }
        }

        // 글이 칸보다 길면 잘라 `…`를 붙이는 대신 clip + 가로 스크롤로 그린다.
        // 편집 중에는 잘린 표시보다 caret 주변이 보이는 것이 중요하다.
        const auto measure { [&font](const std::u8string_view value) { return measure_text(value, font); } };
        const float scroll { scroll_for(measure) };
        // 조합 중에는 선택 대신 조합 구간에 밑줄을 긋는다 — 그 동안 선택은 IME의 것이다.
        const std::size_t selection_begin { value_.composing ? 0u : (value_.caret < value_.anchor ? value_.caret : value_.anchor) };
        const std::size_t selection_end { value_.composing ? 0u : (value_.caret < value_.anchor ? value_.anchor : value_.caret) };
        const float caret_x { left - scroll + measure(text.substr(0, displayed_caret())) };

        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(left, box.y, visible, box.height));
        if (selection_begin != selection_end)
        {
            const float from { left - scroll + measure(text.substr(0, selection_begin)) };
            const float to { left - scroll + measure(text.substr(0, selection_end)) };
            context.canvas.drawRect(SkRect::MakeXYWH(from, box.y + 3.0f * scale, to - from, box.height - 6.0f * scale), solid_paint(with_alpha(context.palette.accent_soft, 0.35f)));
        }
        if (text.empty() == false)
        {
            SkPaint foreground { solid_paint(context.palette.primary_foreground) };
            if (enabled() == false)
                foreground.setAlphaf(0.45f);
            draw_text(context.canvas, text, left - scroll, baseline, font, foreground);
        }

        // 조합 구간은 밑줄로 표시한다 (E2.6). 아직 확정되지 않은 글이라는 뜻이다.
        if (value_.composing && value_.composing_end > value_.composing_begin)
        {
            const std::size_t underline_begin { value_.composing_begin < text.size() ? value_.composing_begin : text.size() };
            const std::size_t underline_end { value_.composing_end < text.size() ? value_.composing_end : text.size() };
            const float from { left - scroll + measure(text.substr(0, underline_begin)) };
            const float to { left - scroll + measure(text.substr(0, underline_end)) };
            const float underline_top { baseline + 2.0f * scale };
            context.canvas.drawRect(SkRect::MakeXYWH(from, underline_top, to - from, 1.0f * scale), solid_paint(context.palette.accent));
        }

        // caret은 초점을 받은 동안 caret 자리에서 깜빡인다. 초점을 받은 시각이 위상
        // 기준이라 받는 순간에는 항상 켜져 있다. 다시 그리기는 UI thread의 caret
        // timer가 반주기마다 일으킨다.
        const bool caret_on { focused && interaction.focus_started_at.has_value() && ((context.now - *interaction.focus_started_at) / caret_blink_interval) % 2 == 0 };
        if (caret_on)
        {
            const float caret_top { box.y + 4.0f * scale };
            context.canvas.drawRect(SkRect::MakeXYWH(caret_x, caret_top, 1.0f * scale, box.height - 8.0f * scale), solid_paint(context.palette.primary_foreground));
        }
        context.canvas.restore();
    }
} // namespace gitman::ui
