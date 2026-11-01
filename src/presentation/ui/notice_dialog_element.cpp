#include "presentation/ui/notice_dialog_element.h"

#include "luil/ui/dialog_elements.h"
#include "presentation/ui/dialog_host.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"
#include "presentation/ui/ui_common.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        // panel의 논리 치수다. 본문 줄 수에 따라 높이만 늘어난다.
        constexpr float panel_width { 380.0f };
        constexpr float panel_padding { 16.0f };
        constexpr float title_height { 26.0f };
        constexpr float line_height { 18.0f };
        constexpr float button_width { 88.0f };
        constexpr float button_height { 28.0f };

        [[nodiscard]] float panel_height(const std::size_t line_count) noexcept
        {
            const float lines { static_cast<float>(line_count == 0 ? 1 : line_count) * line_height };
            return panel_padding * 2.0f + title_height + lines + 12.0f + button_height;
        }

        // 제목·본문을 그리는 panel이다.
        class notice_panel_element final : public ui_element
        {
        public:
            explicit notice_panel_element(const notice_dialog_view& dialog)
                : ui_element { ui_element_id { kind_notice_dialog_panel } }
                , dialog_ { dialog }
            {
                // 흡수는 host가 content(본체)에 건다 — panel이 다시 걸 필요가 없다.
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                static_cast<void>(interaction);
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 4.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.surface_background));

                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const float padding { panel_padding * scale };
                const ui_color accent { dialog_.error ? context.palette.error_accent : context.palette.accent_emphasis_foreground };
                float text_left { box.x + padding };
                if (context.codicon_typeface != nullptr)
                {
                    const SkFont glyph_font { icon_font(context, 14.0f) };
                    const char32_t glyph { dialog_.error ? codicons::icon_error : codicons::icon_check };
                    draw_centered_glyph(context.canvas, glyph, { text_left, box.y + padding, 18.0f * scale, title_height * scale }, glyph_font, solid_paint(accent));
                    text_left += 24.0f * scale;
                }

                SkFont title_font { ui_font(context, 13.0f) };
                title_font.setEmbolden(true);
                static_cast<void>(draw_text_within(context.canvas, dialog_.title, text_left, box.y + padding + centered_text_baseline(title_font, title_height * scale),
                    box.x + box.width - padding - text_left, title_font, solid_paint(accent)));

                const SkFont line_font { ui_font(context, 12.0f) };
                SkPaint line_paint { solid_paint(context.palette.primary_foreground) };
                line_paint.setAlphaf(0.85f);
                float line_top { box.y + padding + title_height * scale };
                for (const std::u8string& line : dialog_.lines)
                {
                    static_cast<void>(
                        draw_text_within(context.canvas, line, box.x + padding, line_top + centered_text_baseline(line_font, line_height * scale), box.width - padding * 2.0f, line_font, line_paint));
                    line_top += line_height * scale;
                }
            }

        private:
            notice_dialog_view dialog_ {};
        };
    } // namespace

    notice_dialog_element::notice_dialog_element(const notice_dialog_view& dialog)
        : ui_element { ui_element_id { kind_notice_dialog } }
        , dialog_ { dialog }
    {
        // 배경 클릭(닫기)은 host의 scrim이 맡는다 — `make_notice_dialog`의 outside다.
        // 본체 자체는 Tab 자리가 아니다 (설계 D1-B). 키보드 닫기는 host의 Esc다.
        set_tab_stop(false);

        auto panel { std::make_unique<notice_panel_element>(dialog_) };
        panel_ = panel.get();
        add_child(std::move(panel));

        // 확인은 강조 버튼이자 이 dialog의 기본 버튼이다 — Enter가 여기로 간다
        // (enter-default-design).
        const luil::text_button_config confirm_config { .text = std::u8string { u8"확인" }, .visual = luil::text_button_visual::accent, .default_button = true };
        auto confirm { std::make_unique<luil::text_button_element>(ui_element_id { kind_notice_dialog_confirm }, confirm_config) };
        confirm->set_action(ui_trigger::left_click, make_logic_click(dismiss_notice_intent {}));
        confirm_ = confirm.get();
        add_child(std::move(confirm));
    }

    void notice_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        // host가 창 가운데에 놓아 준 panel 상자를 그대로 받는다 (설계 D2-A).
        const float left { context.slot.x };
        const float top { context.slot.y };
        const float width { context.slot.width };
        const float height { context.slot.height };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { panel_padding * scale };
        confirm_->arrange({ { left + width - padding - button_width * scale, top + height - padding - button_height * scale, button_width * scale, button_height * scale }, scale });
    }

    void notice_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 뒤를 덮는 scrim은 host가 그린다 (팔레트 `content_shadow`).
        draw_children(context, interaction);
    }

    std::unique_ptr<luil::modal_host_element> make_notice_dialog(const notice_dialog_view& view, const float window_width, const float window_height)
    {
        // 예전 arrange의 clamp 규칙 그대로다 — 창보다 크면 창 크기까지 줄인다.
        float width { panel_width };
        float height { panel_height(view.lines.size()) };
        if (width > window_width)
            width = window_width;
        if (height > window_height)
            height = window_height;

        dialog_host_spec spec {};
        spec.owner = std::u8string { dialog_owner_notice };
        spec.name = view.title;
        spec.content_width = width;
        spec.content_height = height;
        // 배경 클릭도 Esc도 닫기다 (예전 overlay의 left_click과 policy 사슬을 옮긴 것).
        spec.outside = make_logic_click(dismiss_notice_intent {});
        spec.dismiss = make_logic_click(dismiss_notice_intent {});
        return make_dialog_host(std::move(spec), std::make_unique<notice_dialog_element>(view));
    }
} // namespace gitman::ui
