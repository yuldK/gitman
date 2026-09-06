#include "presentation/ui/generate_document_dialog_element.h"

#include "presentation/list_metrics.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/dialog_elements.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <memory>
#include <string>
#include <utility>

namespace gitman::ui {
    namespace {
        constexpr float dialog_padding { 14.0f };
        constexpr float action_button_width { 96.0f };
        constexpr float action_button_height { 28.0f };
        constexpr float browse_button_width { 84.0f };
        constexpr float first_row_top { 40.0f };
        constexpr float row_gap { 6.0f };
        constexpr float field_height { 26.0f };

        // 행 하나의 위쪽 좌표다 (논리 픽셀). 네 줄이 같은 간격으로 쌓인다.
        float row_top(const std::size_t index)
        {
            return first_row_top + static_cast<float>(index) * (layout_generate_dialog_row_height + row_gap);
        }

        // 읽기 전용 경로 칸이다. 편집하지 않으므로 element가 아니라 panel이 그린다.
        void draw_path_field(draw_context& context, const rect_f& box, const std::u8string_view path, const std::u8string_view placeholder, const bool enabled)
        {
            const float scale { context.scale > 0.0f ? context.scale : 1.0f };
            const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
            const float radius { 3.0f * scale };
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(with_alpha(context.palette.primary_foreground, enabled ? 0.08f : 0.04f)));
            SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, enabled ? 0.35f : 0.18f)) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(1.0f * scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

            const SkFont font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
            const float inset { 7.0f * scale };
            SkPaint foreground { solid_paint(context.palette.primary_foreground) };
            if (path.empty())
                foreground.setAlphaf(0.5f);
            else if (enabled == false)
                foreground.setAlphaf(0.45f);
            static_cast<void>(
                draw_text_within(context.canvas, path.empty() ? placeholder : path, box.x + inset, box.y + centered_text_baseline(font, box.height), box.width - inset * 2.0f, font, foreground));
        }

        class generate_panel_element final : public ui_element
        {
        public:
            explicit generate_panel_element(generate_document_dialog_view dialog)
                : ui_element { ui_element_id { kind_generate_dialog_panel } }
                , dialog_ { std::move(dialog) }
            {
                // panel 위의 클릭은 배경 닫기로 흐르지 않는다.
                set_hit_opaque(true);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
                const float radius { 5.0f * scale };
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(context.palette.surface_background));
                SkPaint border { solid_paint(with_alpha(context.palette.primary_foreground, 0.25f)) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(1.0f * scale);
                context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), border);

                const float padding { dialog_padding * scale };
                const SkFont title_font { sk_ref_sp(context.ui_typeface), 13.0f * scale };
                static_cast<void>(draw_text_within(context.canvas, std::u8string_view { u8"새 문서 만들기" }, box.x + padding, box.y + padding + 11.0f * scale, box.width - padding * 2.0f, title_font,
                    solid_paint(context.palette.primary_foreground)));

                const SkFont label_font { sk_ref_sp(context.ui_typeface), 11.0f * scale };
                const float label_left { box.x + padding };
                const float field_left { label_left + layout_generate_dialog_label_width * scale };
                const float field_right { box.x + box.width - padding };

                const auto label_at = [&](const std::size_t index, const std::u8string_view text, const bool enabled) {
                    const float top { box.y + row_top(index) * scale };
                    SkPaint paint { solid_paint(context.palette.primary_foreground) };
                    paint.setAlphaf(enabled ? 0.8f : 0.4f);
                    draw_text(context.canvas, text, label_left, top + centered_text_baseline(label_font, layout_generate_dialog_row_height * scale), label_font, paint);
                };

                label_at(0, u8"이름", true);
                label_at(1, u8"저장소 폴더", true);
                label_at(2, u8"저장 위치", dialog_.same_folder == false);

                // 스캔 폴더 칸과 저장 위치 칸은 읽기 전용이다. 값은 Win32 폴더
                // dialog가 채운다.
                const float browse { browse_button_width * scale };
                const float gap { 8.0f * scale };
                const float path_width { field_right - field_left - browse - gap };
                const float scan_top { box.y + row_top(1) * scale + (layout_generate_dialog_row_height * scale - field_height * scale) / 2.0f };
                draw_path_field(context, { field_left, scan_top, path_width, field_height * scale }, dialog_.scan_root, u8"저장소들을 담은 폴더를 고르세요", true);

                const float location_top { box.y + row_top(2) * scale + (layout_generate_dialog_row_height * scale - field_height * scale) / 2.0f };
                const std::u8string_view location { dialog_.same_folder ? std::u8string_view { dialog_.scan_root } : std::u8string_view { dialog_.location } };
                draw_path_field(context, { field_left, location_top, path_width, field_height * scale }, location, u8"문서를 저장할 폴더를 고르세요", dialog_.same_folder == false);

                // 만들어질 최종 경로를 미리 보여 준다. 확장자는 앱이 붙인다.
                const float preview_top { box.y + row_top(4) * scale };
                SkPaint preview_paint { solid_paint(context.palette.primary_foreground) };
                preview_paint.setAlphaf(0.6f);
                const std::u8string_view preview { dialog_.document_path.empty() ? std::u8string_view { u8"만들 경로: —" } : std::u8string_view { dialog_.document_path } };
                static_cast<void>(draw_text_within(context.canvas, preview, label_left, preview_top + centered_text_baseline(label_font, layout_generate_dialog_row_height * scale),
                    box.width - padding * 2.0f, label_font, preview_paint));

                if (dialog_.message.empty() == false)
                {
                    const float message_top { box.y + row_top(5) * scale };
                    const ui_color color { dialog_.executing ? context.palette.primary_foreground : context.palette.warning_accent };
                    static_cast<void>(draw_text_within(context.canvas, dialog_.message, label_left, message_top + centered_text_baseline(label_font, layout_generate_dialog_row_height * scale),
                        box.width - padding * 2.0f, label_font, solid_paint(color)));
                }

                draw_children(context, interaction);
            }

        private:
            generate_document_dialog_view dialog_ {};
        };
    } // namespace

    generate_document_dialog_element::generate_document_dialog_element(generate_document_dialog_view dialog)
        : ui_element { ui_element_id { kind_generate_dialog } }
        , dialog_ { std::move(dialog) }
    {
        set_action(ui_trigger::left_click, make_logic_click(cancel_generate_document_intent {}));

        auto panel { std::make_unique<generate_panel_element>(dialog_) };
        panel_ = panel.get();
        add_child(std::move(panel));

        auto name {
            std::make_unique<luil::text_input_element>(ui_element_id { kind_generate_dialog_name_input }, dialog_.name, luil::text_input_config { .placeholder = dialog_.name_placeholder }),
        };
        name->set_enabled(dialog_.executing == false);
        name->set_tooltip(u8"확장자(.version-list)는 앱이 붙입니다");
        name_input_ = name.get();
        add_child(std::move(name));

        const auto add_button = [this](const ui_element_kind kind, const std::u8string_view text, const bool accent, const bool enabled, ui_action click) {
            auto button {
                std::make_unique<luil::text_button_element>(ui_element_id { kind },
                    luil::text_button_config { .text = std::u8string { text }, .visual = accent ? luil::text_button_visual::accent : luil::text_button_visual::normal }),
            };
            button->set_enabled(enabled);
            button->set_action(ui_trigger::left_click, std::move(click));
            ui_element* const raw { button.get() };
            add_child(std::move(button));
            return raw;
        };

        // 폴더 선택은 Win32 dialog라 UI thread 명령으로 나간다 (설계 L7).
        scan_browse_ = add_button(kind_generate_dialog_scan_browse, u8"찾아보기…", false, dialog_.executing == false, make_app_command_click(app_command::show_generate_scan_folder_picker));
        location_browse_ = add_button(kind_generate_dialog_location_browse, u8"찾아보기…", false, dialog_.executing == false, make_app_command_click(app_command::show_generate_location_picker));

        // 켬·끔이 강조색 바탕으로 보이는 토글 버튼이다.
        same_folder_ = add_button(
            kind_generate_dialog_same_folder, u8"저장소 폴더에 만들기", dialog_.same_folder, dialog_.executing == false, make_logic_click(toggle_generate_same_folder_intent {}));
        confirm_ = add_button(kind_generate_dialog_confirm, u8"만들기", true, dialog_.can_confirm, make_logic_click(confirm_generate_document_intent {}));
        cancel_ = add_button(kind_generate_dialog_cancel, u8"취소", false, true, make_logic_click(cancel_generate_document_intent {}));
    }

    void generate_document_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        float width { layout_generate_dialog_width * scale };
        float height { layout_generate_dialog_height * scale };
        if (width > context.slot.width)
            width = context.slot.width;
        if (height > context.slot.height)
            height = context.slot.height;
        const float left { context.slot.x + (context.slot.width - width) / 2.0f };
        const float top { context.slot.y + (context.slot.height - height) / 2.0f };
        panel_->arrange({ { left, top, width, height }, scale });

        const float padding { dialog_padding * scale };
        const float field_left { left + padding + layout_generate_dialog_label_width * scale };
        const float field_right { left + width - padding };
        const float browse { browse_button_width * scale };
        const float gap { 8.0f * scale };
        const float row { layout_generate_dialog_row_height * scale };
        const float field { field_height * scale };
        const auto centered = [row, field](const float row_y) { return row_y + (row - field) / 2.0f; };

        float path_width { field_right - field_left - browse - gap };
        if (path_width < 0.0f)
            path_width = 0.0f;

        name_input_->arrange({ { field_left, centered(top + row_top(0) * scale), field_right - field_left, field }, scale });
        scan_browse_->arrange({ { field_right - browse, centered(top + row_top(1) * scale), browse, field }, scale });
        location_browse_->arrange({ { field_right - browse, centered(top + row_top(2) * scale), browse, field }, scale });

        // 토글은 저장 위치 줄의 오른쪽 위, 경로 칸 위 줄에 둔다.
        same_folder_->arrange({ { field_left, centered(top + row_top(3) * scale), 168.0f * scale, field }, scale });
        static_cast<void>(path_width);

        const float button_width { action_button_width * scale };
        const float button_height { action_button_height * scale };
        const float button_top { top + height - padding - button_height };
        confirm_->arrange({ { left + width - padding - button_width * 2.0f - 8.0f * scale, button_top, button_width, button_height }, scale });
        cancel_->arrange({ { left + width - padding - button_width, button_top, button_width, button_height }, scale });
    }

    void generate_document_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const rect_f box { bounds() };
        context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height), solid_paint(with_alpha(0xFF000000u, 0.45f)));
        draw_children(context, interaction);
    }
} // namespace gitman::ui
