#include "presentation/ui/log_view_element.h"

#include "gitman/generated/codicons.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"
#include "presentation/ui/button_element.h"
#include "presentation/ui/draw_primitives.h"
#include "presentation/ui/label_element.h"
#include "presentation/ui/scrollbar_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        // record 한 줄의 글자 색이다. 심각도가 스트림 종류보다 우선한다.
        ui_color record_color(const ui_color_palette& palette, const operation_log_entry& entry) noexcept
        {
            if (entry.severity == diagnostic_severity::error)
                return palette.error_accent;
            if (entry.severity == diagnostic_severity::warning)
                return palette.warning_accent;
            if (entry.kind == log_entry_kind::standard_error)
                return palette.warning_accent;
            if (entry.kind == log_entry_kind::lifecycle)
                return palette.primary_foreground;
            return with_alpha(palette.primary_foreground, 0.85f);
        }
    } // namespace

    log_view_element::log_view_element(log_view_model log, const float body_viewport_height, const float scale)
        : ui_element { ui_element_id { ui_element_kind::log_pane } }
        , log_ { std::move(log) }
    {
        // pane 배경 클릭이 root의 선택 해제로 흐르지 않도록 클릭을 흡수한다.
        set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return {}; });

        label_config title_config {};
        title_config.text = log_.truncated ? log_.title + std::u8string { u8" (오래된 로그 제거됨)" } : log_.title;
        title_config.font_size = 11.5f;
        title_config.color = label_color_role::primary;
        auto title { std::make_unique<label_element>(ui_element_id { ui_element_kind::log_title, log_.card }, std::move(title_config)) };
        title_ = title.get();
        add_child(std::move(title));

        button_config filter_config {};
        filter_config.glyph = codicons::icon_filter;
        filter_config.icon_size = 12.0f;
        filter_config.active = log_.filter != log_stream_filter::all;
        auto filter { std::make_unique<button_element>(ui_element_id { ui_element_kind::log_filter, log_.card }, filter_config) };
        filter->set_tooltip(std::u8string { u8"로그 필터: " } + std::u8string { log_stream_filter_label(log_.filter) } + std::u8string { u8" (클릭해 전환)" });
        filter->set_action(ui_trigger::left_click,
            [next = next_log_filter(log_.filter)](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { set_log_filter_intent { next } } } }; });
        filter_ = filter.get();
        add_child(std::move(filter));

        button_config autoscroll_config {};
        autoscroll_config.glyph = codicons::icon_arrow_down;
        autoscroll_config.icon_size = 12.0f;
        autoscroll_config.active = log_.auto_scroll;
        auto autoscroll { std::make_unique<button_element>(ui_element_id { ui_element_kind::log_autoscroll, log_.card }, autoscroll_config) };
        autoscroll->set_tooltip(log_.auto_scroll ? std::u8string { u8"자동 스크롤 끄기" } : std::u8string { u8"자동 스크롤 켜기 (맨 아래 따라가기)" });
        autoscroll->set_action(ui_trigger::left_click,
            [enable = log_.auto_scroll == false](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { set_log_auto_scroll_intent { enable } } } }; });
        autoscroll_ = autoscroll.get();
        add_child(std::move(autoscroll));

        auto copy { std::make_unique<button_element>(ui_element_id { ui_element_kind::log_copy, log_.card }, button_config { .glyph = codicons::icon_copy, .icon_size = 12.0f }) };
        copy->set_tooltip(u8"표시 중인 로그 복사");
        copy->set_action(ui_trigger::left_click, [](const ui_action_context&) -> std::vector<input_action> { return { input_action { ui_command::copy_selected_log } }; });
        copy->set_enabled(log_.records.empty() == false);
        copy_ = copy.get();
        add_child(std::move(copy));

        auto clear { std::make_unique<button_element>(ui_element_id { ui_element_kind::log_clear, log_.card }, button_config { .glyph = codicons::icon_clear_all, .icon_size = 12.0f }) };
        clear->set_tooltip(u8"이 카드 로그 지우기");
        clear->set_action(ui_trigger::left_click, [id = log_.card](const ui_action_context&) -> std::vector<input_action> { return { input_action { logic_message { clear_log_intent { id } } } }; });
        clear->set_enabled(log_.records.empty() == false);
        clear_ = clear.get();
        add_child(std::move(clear));

        // 본문의 시각적 스크롤 막대다 (stage-8-plan 5.3). 내용·화면 높이는 접힌
        // 표시 줄 기준이라 logic의 스크롤 한계와 같은 값을 쓴다. 끌기는
        // log_scroll_intent로 흘러 자동 스크롤 규칙(위로 끌면 꺼짐)이 유지된다.
        const float effective_scale { scale > 0.0f ? scale : 1.0f };
        const float content_height { (static_cast<float>(log_.lines.size()) * layout_log_line_height + layout_log_text_inset * 2.0f) * effective_scale };
        const float scroll_physical { log_.scroll_offset * effective_scale };
        const ui_element_id scrollbar_id { ui_element_kind::log_scrollbar, log_.card };
        const scrollbar_element::scroll_message_factory make_log_scroll { [](const float delta) { return logic_message { log_scroll_intent { delta } }; } };
        auto scrollbar { std::make_unique<scrollbar_element>(scrollbar_id, make_log_scroll, content_height, body_viewport_height, scroll_physical, effective_scale) };
        scrollbar->set_visible(content_height > body_viewport_height && body_viewport_height > 0.0f);
        scrollbar_ = scrollbar.get();
        add_child(std::move(scrollbar));
    }

    void log_view_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        const float header_height { layout_log_header_height * scale };
        const float margin { layout_margin * scale };
        const float gap { layout_card_gap * scale };
        // 헤더가 낮으므로 toolbar 버튼보다 작은 정사각형을 쓴다.
        const float button { header_height - 4.0f * scale };
        const float button_y { context.slot.y + (header_height - button) / 2.0f };
        const float title_left { context.slot.x + margin };

        float next_x { context.slot.x + context.slot.width - margin - button };
        const auto place = [&](ui_element* const element) {
            if (next_x < title_left)
            {
                element->set_visible(false);
                return;
            }
            element->set_visible(true);
            element->arrange({ { next_x, button_y, button, button }, scale });
            next_x -= button + gap;
        };

        place(clear_);
        place(copy_);
        place(autoscroll_);
        place(filter_);

        // 실행 중 변경 작업의 경과 시간 자리다. 버튼 왼쪽에 예약해 제목이 침범하지
        // 않는다. 자리가 없으면 그리지 않는다.
        elapsed_right_ = 0.0f;
        if (log_.change_started_at.has_value())
        {
            const float reserved { layout_log_elapsed_width * scale };
            const float right { next_x + button - gap };
            if (right - reserved >= title_left)
            {
                elapsed_right_ = right;
                next_x -= reserved;
            }
        }

        const float title_width { next_x + button - title_left };
        title_->arrange({ { title_left, context.slot.y, title_width > 0.0f ? title_width : 0.0f, header_height }, scale });

        body_ = { context.slot.x, context.slot.y + header_height, context.slot.width, context.slot.height - header_height };
        if (body_.height < 0.0f)
            body_.height = 0.0f;

        // 막대는 본문 오른쪽 안쪽에 세로로 선다 (카드 목록과 같은 배치 규칙).
        const float hit_width { layout_scrollbar_hit_width * scale };
        const float inset { layout_scrollbar_right_inset * scale };
        const float vertical_inset { layout_scrollbar_vertical_inset * scale };
        const float track_left { body_.x + body_.width - inset - hit_width };
        float track_height { body_.height - vertical_inset * 2.0f };
        if (track_height < 0.0f)
            track_height = 0.0f;
        scrollbar_->arrange({ { track_left, body_.y + vertical_inset, hit_width, track_height }, scale });
        scrollbar_reserved_ = scrollbar_->visible() ? hit_width + inset : 0.0f;
    }

    void log_view_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };

        // 목록과 구분되도록 pane 전체를 표면색으로 깔고 위쪽 경계선을 긋는다.
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.surface_background));
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, 1.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.15f)));

        // 변경 작업(update·switch)이 실행 중이면 헤더에 경과 시간을 그린다. 값은
        // draw 시각 기준이라 UI thread의 timer가 1초마다 다시 그리게 한다.
        if (log_.change_started_at.has_value() && elapsed_right_ > 0.0f)
        {
            const std::u8string text { format_elapsed_time(context.now - *log_.change_started_at) };
            const SkFont font { sk_ref_sp(context.ui_typeface), 10.5f * scale };
            SkPaint paint { solid_paint(context.palette.primary_foreground) };
            paint.setAlphaf(0.6f);
            const float header_height { layout_log_header_height * scale };
            draw_text(context.canvas, text, elapsed_right_ - measure_text(text, font), box.y + centered_text_baseline(font, header_height), font, paint);
        }

        draw_records(context, body_);
        draw_children(context, interaction);
    }

    void log_view_element::draw_records(draw_context& context, const rect_f& body) const
    {
        if (body.width <= 0.0f || body.height <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(body.x, body.y, body.width, body.height));

        const SkFont font { sk_ref_sp(context.ui_typeface), 10.5f * scale };
        const float inset { layout_log_text_inset * scale };

        if (log_.records.empty())
        {
            SkPaint dim { solid_paint(context.palette.primary_foreground) };
            dim.setAlphaf(0.5f);
            draw_text(context.canvas,
                log_.filter == log_stream_filter::all ? std::u8string_view { u8"이 카드에서 실행한 작업의 로그가 여기에 표시됩니다." } : std::u8string_view { u8"필터와 일치하는 로그가 없습니다." },
                body.x + inset, body.y + centered_text_baseline(font, body.height), font, dim);
            context.canvas.restore();
            return;
        }

        const float line_height { layout_log_line_height * scale };
        // 시각 열의 폭은 고정 형식(HH:MM:SS)이라 한 번만 잰다.
        const float time_width { measure_text(u8"00:00:00", font) + inset };
        const float top { body.y + inset - log_.scroll_offset * scale };
        // 막대가 보이면 글자가 그 아래로 지나가지 않게 폭을 줄인다.
        const float text_limit { body.x + body.width - inset - scrollbar_reserved_ };

        for (std::size_t index = 0; index < log_.lines.size(); ++index)
        {
            const float line_top { top + static_cast<float>(index) * line_height };
            if (line_top + line_height < body.y || line_top > body.y + body.height)
                continue;

            const log_display_line& line { log_.lines[index] };
            const float baseline { line_top + centered_text_baseline(font, line_height) };
            SkPaint time_paint { solid_paint(context.palette.primary_foreground) };
            time_paint.setAlphaf(0.45f);
            draw_text(context.canvas, format_log_timestamp(line.record.entry.time), body.x + inset, baseline, font, time_paint);

            const SkPaint text_paint { solid_paint(record_color(context.palette, line.record.entry)) };
            const float text_left { body.x + inset + time_width };
            // 접힌 진행 표시는 마지막 줄 뒤에 접힌 수를 붙여 한 줄로 보인다
            // (stage-8-plan 5.3). buffer에는 원본이 남아 복사·진단이 보존된다.
            std::u8string text { line.record.entry.text };
            if (line.collapsed > 0)
            {
                text += u8" (진행 표시 ";
                std::u8string digits {};
                std::size_t remaining { line.collapsed };
                do
                {
                    digits.insert(digits.begin(), static_cast<char8_t>(u8'0' + remaining % 10));
                    remaining /= 10;
                } while (remaining > 0);
                text += digits;
                text += u8"줄 접힘)";
            }
            static_cast<void>(draw_text_within(context.canvas, text, text_left, baseline, text_limit - text_left, font, text_paint));
        }
        context.canvas.restore();
    }
} // namespace gitman::ui
