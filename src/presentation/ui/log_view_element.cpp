#include "presentation/ui/log_view_element.h"

#include "luil/ui/button_element.h"
#include "luil/ui/label_element.h"
#include "luil/ui/scrollbar_element.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"
#include "presentation/ui/ui_common.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <chrono>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

        // 그려진 로그 줄 하나를 보조 기술에 내는 자리다 (그리지 않는다, function-buttons-design S4). 누를 것이 없어 hit
        // test는 지나가고 빈 곳 클릭은 지금처럼 pane이 받는다.
        class log_line_element final : public ui_element
        {
        public:
            log_line_element(const std::u8string_view card, const log_display_line& line, const std::size_t index)
                : ui_element { ui_element_id { kind_log_line, log_line_owner(card, line.record.sequence) } }
                , name_ { log_line_access_name(line) }
                , index_ { index }
            {
                set_tab_stop(false);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context&, const interaction_snapshot&) const override
            {
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::list_item, .name = name_ };
            }

            [[nodiscard]] std::size_t index() const noexcept
            {
                return index_;
            }

        private:
            std::u8string name_ {};
            std::size_t index_ { 0 };
        };

        // 로그 본문의 줄 목록이다 (그리지 않는다, S4). 그리기는 pane이 하고(`draw_records`), 이 목록은 같은 줄을 보조 기술에
        // 목록 항목으로 낸다. 창에 걸치는 줄만 항목으로 세운다 — 카드 목록과 같고, 화면 밖 줄은 막대 값을 정해 흘리면
        // 들어온다. 헤더 버튼은 이 목록 밖이다. id가 줄의 자리(순번)가 아니라 record의 sequence라 오래된 줄이 지워지거나
        // 필터가 바뀌어도 같은 줄은 같은 id다.
        class log_lines_element final : public ui_element
        {
        public:
            // `viewport_height`는 본문 높이의 논리 픽셀 값이다.
            log_lines_element(const log_view_model& log, const float viewport_height)
                : ui_element { ui_element_id { kind_log_lines, log.card.value } }
                , name_ { log_list_access_name(log) }
                , scroll_offset_ { log.scroll_offset }
            {
                set_tab_stop(false);
                // 걸친 줄은 본문 위로 한 줄 높이까지 나간다. 자르지 않으면 그 사각형(UIA BoundingRectangle)이 헤더와 겹치고,
                // 좌표 질의(`access_element_at`)가 자식을 뒤에서부터 훑어 헤더 단추 아래쪽에서 단추 대신 줄을 잡는다 (S4 검토).
                // 카드 목록과 같다. 이 목록은 그리지 않고 줄도 누를 것이 없어 hit test는 그대로다.
                set_clip_children(true);
                // 줄 자리는 `draw_records`와 같은 식이다: 본문 위 안쪽 여백에서 스크롤만큼 올라간 자리부터 한 줄씩.
                const float line_height { layout_log_line_height() };
                for (std::size_t index = 0; index < log.lines.size(); ++index)
                {
                    const float top { layout_log_text_inset - scroll_offset_ + static_cast<float>(index) * line_height };
                    if (top + line_height < 0.0f || top > viewport_height)
                        continue;
                    add_child(std::make_unique<log_line_element>(log.card.value, log.lines[index], index));
                }
            }

            void arrange(const arrange_context& context) override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                set_bounds(context.slot);
                const float inset { layout_log_text_inset * scale };
                const float line_height { layout_log_line_height() * scale };
                const float width { context.slot.width - inset * 2.0f > 0.0f ? context.slot.width - inset * 2.0f : 0.0f };
                for (const std::unique_ptr<ui_element>& child : children())
                {
                    const std::size_t index { static_cast<const log_line_element&>(*child).index() };
                    const float top { context.slot.y + inset - scroll_offset_ * scale + static_cast<float>(index) * line_height };
                    child->arrange({ { context.slot.x + inset, top, width, line_height }, scale });
                }
            }

            void draw(draw_context&, const interaction_snapshot&) const override
            {
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::list, .name = name_ };
            }

        private:
            std::u8string name_ {};
            float scroll_offset_ { 0.0f };
        };
    } // namespace

    log_view_element::log_view_element(const log_view_model& log, const float body_viewport_height, const float scale)
        : ui_element { ui_element_id { kind_log_pane } }
        , log_ { log }
    {
        // pane 배경 클릭이 root의 선택 해제로 흐르지 않도록 hit를 흡수한다.
        set_hit_opaque(true);

        luil::label_config title_config {};
        title_config.text = log_.truncated ? log_.title + std::u8string { u8" (오래된 로그 제거됨)" } : log_.title;
        title_config.font_size = 11.5f;
        title_config.color = luil::label_color_role::primary;
        auto title { std::make_unique<luil::label_element>(ui_element_id { kind_log_title, log_.card.value }, std::move(title_config)) };
        title_ = title.get();
        add_child(std::move(title));

        luil::button_config filter_config {};
        filter_config.glyph = codicons::icon_filter;
        filter_config.icon_size = 12.0f;
        filter_config.active = log_.filter != log_stream_filter::all;
        auto filter { std::make_unique<luil::button_element>(ui_element_id { kind_log_filter, log_.card.value }, filter_config) };
        filter->set_tooltip(std::u8string { u8"로그 필터: " } + std::u8string { log_stream_filter_label(log_.filter) } + std::u8string { u8" (클릭해 전환)" });
        filter->set_action(ui_trigger::left_click, make_logic_click(set_log_filter_intent { next_log_filter(log_.filter) }));
        filter_ = filter.get();
        add_child(std::move(filter));

        luil::button_config autoscroll_config {};
        autoscroll_config.glyph = codicons::icon_arrow_down;
        autoscroll_config.icon_size = 12.0f;
        autoscroll_config.active = log_.auto_scroll;
        auto autoscroll { std::make_unique<luil::button_element>(ui_element_id { kind_log_autoscroll, log_.card.value }, autoscroll_config) };
        autoscroll->set_tooltip(log_.auto_scroll ? std::u8string { u8"자동 스크롤 끄기" } : std::u8string { u8"자동 스크롤 켜기 (맨 아래 따라가기)" });
        autoscroll->set_action(ui_trigger::left_click, make_logic_click(set_log_auto_scroll_intent { log_.auto_scroll == false }));
        autoscroll_ = autoscroll.get();
        add_child(std::move(autoscroll));

        auto copy { std::make_unique<luil::button_element>(ui_element_id { kind_log_copy, log_.card.value }, luil::button_config { .glyph = codicons::icon_copy, .icon_size = 12.0f }) };
        copy->set_tooltip(u8"표시 중인 로그 복사");
        // 복사할 글은 누른 시점에 만든다. 클립보드 접근은 UI thread의 몫이라
        // 요청만 돌려준다 (설계 L7).
        copy->set_action(ui_trigger::left_click,
            [this](const ui_action_context&) -> std::vector<input_action> { return { input_action { clipboard_copy_request { format_log_copy_text(log_) } } }; });
        copy->set_enabled(log_.records.empty() == false);
        copy_ = copy.get();
        add_child(std::move(copy));

        auto clear { std::make_unique<luil::button_element>(ui_element_id { kind_log_clear, log_.card.value }, luil::button_config { .glyph = codicons::icon_clear_all, .icon_size = 12.0f }) };
        clear->set_tooltip(u8"이 카드 로그 지우기");
        clear->set_action(ui_trigger::left_click, make_logic_click(clear_log_intent { log_.card }));
        clear->set_enabled(log_.records.empty() == false);
        clear_ = clear.get();
        add_child(std::move(clear));

        // 본문의 시각적 스크롤 막대다 (stage-8-plan 5.3). 내용·화면 높이는 접힌
        // 표시 줄 기준이라 logic의 스크롤 한계와 같은 값을 쓴다. 끌기는
        // log_scroll_intent로 흘러 자동 스크롤 규칙(위로 끌면 꺼짐)이 유지된다.
        // 막대가 받는 길이는 전부 논리 픽셀이라 물리 픽셀로 받은 본문 높이는
        // 배율로 되돌린다 (설계 L4).
        const float effective_scale { scale > 0.0f ? scale : 1.0f };
        const float content_height { static_cast<float>(log_.lines.size()) * layout_log_line_height() + layout_log_text_inset * 2.0f };
        const float viewport_height { body_viewport_height / effective_scale };
        // 그린 줄을 보조 기술에 목록 항목으로 낸다 (S4). 막대보다 앞이라 접근 순서도 헤더 버튼, 줄, 막대다.
        auto lines { std::make_unique<log_lines_element>(log_, viewport_height) };
        lines_ = lines.get();
        add_child(std::move(lines));

        luil::scrollbar_config scrollbar_config {
            .scroll = [](const float delta) -> input_action { return make_logic_action(log_scroll_intent { delta }); },
            // 끝으로 정한 값은 끝을 뜻한다 (S4). 따라가는 동안 줄이 늘면 이 발행본의 끝은 logic의 끝보다 앞이라, 그 값을
            // 그대로 보내면 끝에 닿지 못하고 따라가기가 꺼진다. 휠이 끝을 넘으면 끝에서 잘리는 것과 같게 한다.
            .scroll_to = [end = content_height > viewport_height ? content_height - viewport_height : 0.0f](const float offset) -> input_action
            {
                return make_logic_action(log_scroll_intent { 0.0f, offset >= end - 0.5f ? std::numeric_limits<float>::max() : offset });
            },
            .content_height = content_height,
            .viewport_height = viewport_height,
            .scroll_offset = log_.scroll_offset,
        };
        auto scrollbar { std::make_unique<luil::scrollbar_element>(ui_element_id { kind_scrollbar, std::u8string { scrollbar_owner_log } }, std::move(scrollbar_config)) };
        // 목록에 딸린 막대는 Tab 자리가 아니다 (luil list_element와 같은 관례, D1-B).
        scrollbar->set_tab_stop(false);
        // 막대도 이름이 있어야 보조 기술이 어느 목록의 막대인지 읽는다 (S4).
        scrollbar->set_access_name(std::u8string { scrollbar_access_name(scrollbar->id().owner) });
        scrollbar->set_visible(content_height > viewport_height && viewport_height > 0.0f);
        scrollbar_ = scrollbar.get();
        add_child(std::move(scrollbar));
    }

    void log_view_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        const float header_height { layout_log_header_height() * scale };
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

        // 변경 작업의 경과 시간 자리다. 버튼 왼쪽에 예약해 제목이 침범하지
        // 않는다. 자리가 없으면 그리지 않는다.
        elapsed_right_ = 0.0f;
        if (log_.change_started_at.has_value() || log_.change_elapsed.has_value())
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
        // 줄 목록은 글자가 그려지는 자리와 같다 (막대가 보이면 그만큼 좁다).
        lines_->arrange({ { body_.x, body_.y, body_.width - scrollbar_reserved_ > 0.0f ? body_.width - scrollbar_reserved_ : 0.0f, body_.height }, scale });
    }

    void log_view_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };

        // 목록과 구분되도록 pane 전체를 표면색으로 깔고 위쪽 경계선을 긋는다.
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(context.palette.surface_background));
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, 1.0f * scale), solid_paint(with_alpha(context.palette.primary_foreground, 0.15f)));

        // 변경 작업(update·switch)의 경과 시간이다. 실행 중이면 draw 시각을 기준으로
        // 흘러 UI thread의 timer가 1초마다 다시 그리게 하고, 끝난 뒤에는 마지막 명령이
        // 걸린 시간을 그대로 세워 둔다 (2026-08-22 지시).
        if (elapsed_right_ > 0.0f)
        {
            const std::u8string text { format_elapsed_time(log_.change_started_at.has_value() ? context.now - *log_.change_started_at : *log_.change_elapsed) };
            SkPaint paint { solid_paint(context.palette.primary_foreground) };
            paint.setAlphaf(0.6f);
            // 예약한 칸(`layout_log_elapsed_width`)의 오른쪽 끝에 맞춘다.
            const float width { layout_log_elapsed_width * scale };
            draw_text_line(context, text, { elapsed_right_ - width, box.y, width, layout_log_header_height() * scale }, paint, { .size = 10.5f, .align = luil::text_align::end });
        }

        draw_records(context, body_);
        draw_children(context, interaction);
    }

    std::optional<std::chrono::steady_clock::time_point> log_view_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        // 멈춘 경과 시간(change_elapsed)은 값이 고정이라 다시 그릴 것이 없다.
        // 실행 중일 때만 다음 초 경계에 글자가 넘어간다.
        if (elapsed_right_ <= 0.0f || log_.change_started_at.has_value() == false)
            return std::nullopt;
        const std::chrono::steady_clock::duration elapsed { context.now - *log_.change_started_at };
        const std::chrono::seconds whole { std::chrono::duration_cast<std::chrono::seconds>(elapsed) };
        return *log_.change_started_at + whole + std::chrono::seconds { 1 };
    }

    void log_view_element::draw_records(draw_context& context, const rect_f& body) const
    {
        if (body.width <= 0.0f || body.height <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        context.canvas.save();
        context.canvas.clipRect(SkRect::MakeXYWH(body.x, body.y, body.width, body.height));

        // 본문은 외부 명령의 출력이라 코드 글꼴을 쓴다 (fonts-and-in-app-dialogs-design F1).
        const text_line_style code { .size = 10.5f, .face = luil::text_layout_face::code };
        const float inset { layout_log_text_inset * scale };

        if (log_.records.empty())
        {
            SkPaint dim { solid_paint(context.palette.primary_foreground) };
            dim.setAlphaf(0.5f);
            // 같은 안내가 줄 목록의 접근 이름에도 붙는다 (S4 검토).
            const std::u8string_view hint { log_empty_hint(log_) };
            // 안내는 본문 위쪽 첫 줄 자리다. 세로 가운데에 두면 높은 pane에서 글이
            // 아래로 떠 보였다.
            const float line_height { layout_log_line_height() * scale };
            draw_text_line(context, hint, { body.x + inset, body.y + inset, body.width - inset * 2.0f, line_height }, dim, code);
            context.canvas.restore();
            return;
        }

        const float line_height { layout_log_line_height() * scale };
        // 시각 열의 폭은 고정 형식(HH:MM:SS)이라 한 번만 잰다.
        const float time_width { measure_text(u8"00:00:00", code_font(context, code.size)) + inset };
        const float top { body.y + inset - log_.scroll_offset * scale };
        // 막대가 보이면 글자가 그 아래로 지나가지 않게 폭을 줄인다.
        const float text_limit { body.x + body.width - inset - scrollbar_reserved_ };

        for (std::size_t index = 0; index < log_.lines.size(); ++index)
        {
            const float line_top { top + static_cast<float>(index) * line_height };
            if (line_top + line_height < body.y || line_top > body.y + body.height)
                continue;

            const log_display_line& line { log_.lines[index] };
            SkPaint time_paint { solid_paint(context.palette.primary_foreground) };
            time_paint.setAlphaf(0.45f);
            draw_text_line(context, format_log_timestamp(line.record.entry.time), { body.x + inset, line_top, time_width, line_height }, time_paint, code);

            const SkPaint text_paint { solid_paint(record_color(context.palette, line.record.entry)) };
            const float text_left { body.x + inset + time_width };
            // 접힌 진행 표시는 마지막 줄 뒤에 접힌 수를 붙여 한 줄로 보인다
            // (stage-8-plan 5.3). buffer에는 원본이 남아 복사·진단이 보존된다. 줄 목록의 접근 이름도 같은 글이다 (S4).
            const std::u8string text { format_log_line_text(line) };
            draw_text_line(context, text, { text_left, line_top, text_limit - text_left, line_height }, text_paint, code);
        }
        context.canvas.restore();
    }
} // namespace gitman::ui
