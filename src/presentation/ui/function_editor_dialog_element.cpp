#include "presentation/ui/function_editor_dialog_element.h"

#include "presentation/function_editor_layout.h"
#include "presentation/ui/dialog_host.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_icon_catalog.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/button_element.h"
#include "luil/ui/check_element.h"
#include "luil/ui/choice_group_element.h"
#include "luil/ui/dialog_elements.h"
#include "luil/ui/layout_metrics.h"
#include "luil/ui/scrollbar_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gitman::ui {
    namespace {
        constexpr float action_button_width { 96.0f };
        constexpr float action_button_height { 28.0f };
        constexpr float tabs_width { 150.0f };
        constexpr float text_size { 12.0f };
        constexpr float small_size { 11.0f };
        constexpr float code_size { 11.0f };

        // dialog 안 자리들이다 (물리 픽셀). 본체 배치와 panel 그리기가 같은 값을 쓴다.
        struct editor_geometry
        {
            float scale { 1.0f };
            rect_f box {};
            bool collapsed { false };
            rect_f title {};
            rect_f tabs {};
            // 펼친 목록의 머리 줄, 행 영역, 도구 줄이다.
            rect_f list_header {};
            rect_f list_rows {};
            rect_f tools {};
            // 접힌 선택기 줄이다.
            rect_f selector {};
            // 폼이 보이는 자리다 (스크롤 막대 자리를 뺀 폭).
            rect_f form {};
            rect_f form_scrollbar {};
            // 변수 탭이 쓰는 자리다.
            rect_f body {};
            rect_f preview {};
            rect_f footer {};
            // 아래 줄에서 버튼 앞의 글 자리다.
            rect_f footer_text {};
        };

        [[nodiscard]] editor_geometry make_geometry(const rect_f& slot, const float scale)
        {
            editor_geometry geometry {};
            geometry.scale = scale;
            geometry.box = slot;
            const float logical_width { slot.width / scale };
            const float logical_height { slot.height / scale };
            geometry.collapsed = function_editor_collapsed(logical_width);
            const float padding { layout_function_editor_padding * scale };
            const float left { slot.x + padding };
            const float right { slot.x + slot.width - padding };
            const float bar { layout_scrollbar_hit_width * scale };

            geometry.title = { left, slot.y + 10.0f * scale, right - left - tabs_width * scale - 8.0f * scale, 22.0f * scale };
            geometry.tabs = { right - tabs_width * scale, slot.y + 8.0f * scale, tabs_width * scale, luil::choice_toggle_height * scale };

            const float body_top { slot.y + layout_function_editor_header_height * scale };
            const float body_height { function_editor_body_height(logical_height) * scale };
            geometry.body = { left, body_top, right - left, body_height };

            float form_left { left };
            float form_top { body_top };
            if (geometry.collapsed)
            {
                geometry.selector = { left, body_top, right - left, layout_function_editor_selector_height * scale };
                form_top += layout_function_editor_selector_height * scale;
            }
            else
            {
                const float list_width { layout_function_editor_list_width * scale };
                geometry.list_header = { left, body_top, list_width, layout_function_editor_list_header_height() * scale };
                geometry.list_rows = { left, body_top + geometry.list_header.height, list_width, function_editor_list_viewport_height(logical_height) * scale };
                geometry.tools = { left, geometry.list_rows.y + geometry.list_rows.height, list_width, layout_function_editor_tools_height * scale };
                form_left += list_width + layout_function_editor_list_gap * scale;
            }
            const float form_height { function_editor_form_viewport_height(logical_height, geometry.collapsed) * scale };
            geometry.form = { form_left, form_top, function_editor_form_width(logical_width) * scale, form_height };
            geometry.form_scrollbar = { right - bar, form_top, bar, form_height };

            const float preview_top { body_top + body_height };
            geometry.preview = { left, preview_top + 4.0f * scale, right - left, (layout_function_editor_preview_height() - 8.0f) * scale };
            geometry.footer = { left, slot.y + slot.height - layout_function_editor_footer_height * scale, right - left, layout_function_editor_footer_height * scale };
            const float buttons { (action_button_width * 2.0f + 8.0f + 12.0f) * scale };
            geometry.footer_text = { geometry.footer.x, geometry.footer.y, std::max(0.0f, geometry.footer.width - buttons), geometry.footer.height };
            return geometry;
        }

        void draw_box(draw_context& context, const rect_f& box, const float radius, const ui_color fill, const ui_color border)
        {
            const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), solid_paint(fill));
            SkPaint stroke { solid_paint(border) };
            stroke.setStyle(SkPaint::kStroke_Style);
            stroke.setStrokeWidth(1.0f * context.scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, radius, radius), stroke);
        }

        // 흐린 글 한 줄이다.
        void draw_dim_line(draw_context& context, const std::u8string_view text, const rect_f& box, const float alpha, const text_line_style& style = { .size = small_size })
        {
            SkPaint paint { solid_paint(context.palette.primary_foreground) };
            paint.setAlphaf(alpha);
            draw_text_line(context, text, box, paint, style);
        }

        // 세로로 흘리는 자리가 초점 받은 자식을 들이는 데 필요한 스크롤 변화량이다 (논리 픽셀, luil `scroll_view_element`와
        // 같은 식). policy의 `on_focus_moved`가 `route_reveal`로 묻는다.
        [[nodiscard]] float reveal_delta(const rect_f& viewport, const rect_f& target, const float scale) noexcept
        {
            return luil::scroll_delta_to_reveal(target.y, target.height, viewport.y, viewport.height) / (scale > 0.0f ? scale : 1.0f);
        }

        // 판이 그린 글을 보조 기술에 내는 자리다. 그리지 않고 누를 것도 없어 hit를 받지 않는다 (미리보기, 거절 사유, 변수 탭).
        class editor_text_element final : public ui_element
        {
        public:
            editor_text_element(const std::u8string_view owner, const access_role role, std::u8string name)
                : ui_element { ui_element_id { kind_function_editor_text, std::u8string { owner } } }
                , role_ { role }
                , name_ { std::move(name) }
            {
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
                return { .role = role_, .name = name_ };
            }

        private:
            access_role role_ { access_role::static_text };
            std::u8string name_ {};
        };

        // 목록 행 하나다. 배치 표식(`Alt+N`·기능 줄 글리프·없음·경고), 아이콘, 이름을 그리고 누르면 그 항목을 고른다.
        class editor_row_element final : public ui_element
        {
        public:
            editor_row_element(const std::size_t index, function_editor_row_view row, const bool selected)
                : ui_element { function_editor_item_id(row.key) }
                , row_ { std::move(row) }
                , selected_ { selected }
            {
                set_action(ui_trigger::left_click, make_logic_click(select_function_editor_entry_intent { index }));
                if (row_.warning)
                    set_tooltip(u8"검증에 실패한 항목입니다. 지우지 않으면 원문을 그대로 둡니다.");
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                if (selected_)
                {
                    const SkRect fill { SkRect::MakeXYWH(box.x, box.y + 1.0f * scale, box.width, box.height - 2.0f * scale) };
                    context.canvas.drawRRect(SkRRect::MakeRectXY(fill, 4.0f * scale, 4.0f * scale), solid_paint(context.palette.row_selection_background));
                    context.canvas.drawRect(SkRect::MakeXYWH(box.x, box.y + 6.0f * scale, 3.0f * scale, box.height - 12.0f * scale), solid_paint(context.palette.accent));
                }
                else
                    draw_hover_fill(context, box, id(), interaction, true, 4.0f * scale);

                // 배치 표식 칸이다. 카드면 `Alt+N`의 N, 기능 줄이면 줄 글리프, 메뉴만이면 비우고, 경고 행은 경고 글리프다.
                const rect_f marker { box.x + 8.0f * scale, box.y, 18.0f * scale, box.height };
                if (row_.warning)
                {
                    draw_centered_glyph(context.canvas, codicons::icon_warning, marker, icon_font(context, 13.0f), solid_paint(context.palette.warning_accent));
                }
                else if (row_.placement == function_placement::card && row_.shortcut > 0)
                {
                    draw_text_line(context, function_decimal_text(row_.shortcut), marker, solid_paint(context.palette.accent_emphasis_foreground), { .size = small_size, .weight = luil::font_weight::bold, .align = luil::text_align::center });
                }
                else if (row_.placement == function_placement::bar)
                {
                    SkPaint paint { solid_paint(context.palette.primary_foreground) };
                    paint.setAlphaf(0.6f);
                    draw_centered_glyph(context.canvas, codicons::icon_layout_panel, marker, icon_font(context, 12.0f), paint);
                }

                const rect_f glyph { marker.x + marker.width + 4.0f * scale, box.y, 16.0f * scale, box.height };
                if (row_.warning == false)
                    draw_centered_glyph(context.canvas, function_icon_glyph(row_.icon), glyph, icon_font(context, 14.0f), solid_paint(context.palette.primary_foreground));
                const float text_left { glyph.x + glyph.width + 6.0f * scale };
                SkPaint text { solid_paint(context.palette.primary_foreground) };
                if (row_.warning)
                    text.setAlphaf(0.7f);
                draw_text_line(context, row_.label, { text_left, box.y, box.x + box.width - text_left - 6.0f * scale, box.height }, text, { .size = text_size });
            }

            [[nodiscard]] access_info accessibility() const override
            {
                access_info info {};
                info.role = access_role::list_item;
                info.name = row_.label;
                if (row_.warning)
                    info.name.append(u8", 경고");
                else if (row_.placement == function_placement::card)
                {
                    info.name.append(u8", 카드 Alt+");
                    info.name.append(function_decimal_text(row_.shortcut));
                }
                else if (row_.placement == function_placement::bar)
                    info.name.append(u8", 기능 줄");
                info.selected = selected_;
                return info;
            }

        private:
            function_editor_row_view row_ {};
            bool selected_ { false };
        };

        // 목록 행 영역이다. 잘라 그리고(스크롤) Tab에서 한 자리로 묶어 화살표로 돈다. 영역 밖 행도 숨기지 않는다 — 숨긴
        // element는 초점 순서에서 빠져(`focusable`) 화살표가 닿지 않는다. 그리기와 hit는 자르기가 막고, 화살표로 닿은 행은
        // `on_focus_moved`가 보이게 스크롤한다.
        class editor_list_element final : public ui_element
        {
        public:
            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            editor_list_element(const std::optional<std::u8string>& selected_key, std::u8string name)
                : ui_element { ui_element_id { kind_function_editor_list } }
                , name_ { std::move(name) }
            {
                set_clip_children(true);
                set_focus_group(luil::focus_axis::vertical);
                if (selected_key.has_value())
                    set_focus_entry(function_editor_item_id(*selected_key));
            }

            void arrange(const arrange_context& context) override
            {
                scale_ = context.scale > 0.0f ? context.scale : 1.0f;
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

            [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override
            {
                return reveal_delta(bounds(), target, scale_);
            }

            // 이름은 목록 머리 줄(`기능 N개 · 카드 k/3`)이다. 화면 읽기도 개수와 카드 수를 듣는다.
            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::list, .name = name_ };
            }

        private:
            std::u8string name_ {};
            float scale_ { 1.0f };
        };

        // 아이콘 버튼 묶음이다. Tab에서 한 자리이고 화살표로 돈다.
        class editor_icons_element final : public ui_element
        {
        public:
            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            explicit editor_icons_element(const ui_element_id entry)
                : ui_element { ui_element_id { kind_function_editor_icons } }
            {
                set_focus_group(luil::focus_axis::both);
                set_focus_entry(entry);
            }

            void arrange(const arrange_context& context) override
            {
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                draw_children(context, interaction);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::group, .name = u8"아이콘" };
            }
        };

        // 폼이다. 줄 라벨과 칸 아래 안내 줄, 경고 행의 사유와 원문을 그리고 자식(칸·묶음)을 잘라 그린다.
        class editor_form_element final : public ui_element
        {
        public:
            void adopt(std::unique_ptr<ui_element> child)
            {
                add_child(std::move(child));
            }

            explicit editor_form_element(const function_editor_view& dialog)
                : ui_element { ui_element_id { kind_function_editor_form } }
                , dialog_ { dialog }
            {
                set_clip_children(true);
            }

            // 본체가 배치하며 정한다. 줄 위쪽 좌표는 스크롤을 뺀 논리 픽셀이다.
            function_editor_form_layout layout {};
            float scroll { 0.0f };

            void arrange(const arrange_context& context) override
            {
                scale_ = context.scale > 0.0f ? context.scale : 1.0f;
                set_bounds(context.slot);
            }

            void draw(draw_context& context, const interaction_snapshot& interaction) const override
            {
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const rect_f box { bounds() };
                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(box.x, box.y, box.width, box.height));
                if (dialog_.editable)
                    draw_labels(context, box, scale);
                else
                    draw_notice(context, box, scale);
                context.canvas.restore();
                draw_children(context, interaction);
            }

            [[nodiscard]] float scroll_delta_to_reveal(const rect_f& target) const override
            {
                return reveal_delta(bounds(), target, scale_);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                if (dialog_.editable)
                    return { .role = access_role::group, .name = u8"기능 속성" };
                // 경고 행이면 칸이 없다. 사유와 원문을 이름으로 낸다.
                std::u8string name { dialog_.warning_text.empty() ? std::u8string { notice_empty_text() } : std::u8string { u8"검증에 실패한 항목입니다. " } + dialog_.warning_text };
                if (dialog_.warning_source.empty() == false)
                {
                    name.append(u8" 원문: ");
                    name.append(dialog_.warning_source);
                }
                return { .role = access_role::static_text, .name = std::move(name) };
            }

        private:
            [[nodiscard]] std::u8string_view notice_empty_text() const noexcept
            {
                return dialog_.rows.empty() ? std::u8string_view { u8"기능이 없습니다. + 버튼으로 더합니다." } : std::u8string_view { u8"목록에서 기능을 고르십시오." };
            }

            [[nodiscard]] float row_top(const function_editor_form_row row, const rect_f& box, const float scale) const noexcept
            {
                return box.y + (layout.tops[static_cast<std::size_t>(row)] - scroll) * scale;
            }

            void draw_labels(draw_context& context, const rect_f& box, const float scale) const
            {
                constexpr std::array<std::u8string_view, function_editor_form_row_count> labels {
                    u8"이름",
                    u8"아이콘",
                    u8"프로그램",
                    u8"인자",
                    u8"작업 폴더",
                    u8"제한 시간(초)",
                    u8"실행 방식",
                    u8"적용 범위",
                    u8"배치",
                    u8"실행 전 확인",
                };
                const float label_width { layout_function_editor_label_width * scale };
                const float field_height { layout_function_editor_field_height() * scale };
                for (std::size_t index = 0; index < labels.size(); ++index)
                {
                    const float top { box.y + (layout.tops[index] - scroll) * scale };
                    draw_dim_line(context, labels[index], { box.x, top, label_width - 6.0f * scale, field_height }, 0.75f);
                }

                const float field_left { box.x + label_width };
                const float field_width { box.width - label_width };
                const float note_height { layout_function_editor_note_height() * scale };
                // 인자 칸 아래 줄이다. 닫히지 않은 따옴표는 칸의 오류 글(luil `text_input_config::error`)이 그 자리에 그리고
                // 보조 기술에 알린다. 오류가 없을 때의 안내만 여기서 그린다.
                if (dialog_.arguments_error == false)
                {
                    const float arguments_note { row_top(function_editor_form_row::arguments, box, scale) + field_height };
                    draw_dim_line(context, dialog_.arguments_message, { field_left, arguments_note, field_width, note_height }, 0.55f);
                }

                const float timeout_top { row_top(function_editor_form_row::timeout, box, scale) };
                const float unit_left { field_left + (layout_function_editor_timeout_width + 8.0f) * scale };
                draw_dim_line(context, u8"0은 무제한, 따로 실행에는 쓰지 않음", { unit_left, timeout_top, field_left + field_width - unit_left, field_height }, 0.6f);

                // 배치 묶음 아래 줄이다. 거절 사유는 경고색이고, 없으면 기능 메뉴에는 늘 나온다는 안내다.
                const float placement_note { row_top(function_editor_form_row::placement, box, scale) + field_height };
                if (dialog_.placement_message.empty() == false)
                    draw_text_line(context, dialog_.placement_message, { field_left, placement_note, field_width, note_height }, solid_paint(context.palette.warning_accent), { .size = small_size });
                else
                    draw_dim_line(context, u8"어느 배치든 ⋯ 기능 메뉴에는 늘 나옵니다.", { field_left, placement_note, field_width, note_height }, 0.55f);
            }

            // 경고 행의 안내다. 사유와 원문은 폼 폭의 칸 수로 줄을 나누고, 높이는 logic의 스크롤 한계와 같은 함수다
            // (`function_editor_notice_content_height`). 끝을 숨기지 않는다 (2.7 "원문째").
            void draw_notice(draw_context& context, const rect_f& box, const float scale) const
            {
                const float row { layout_function_editor_notice_line_height() * scale };
                float top { box.y + 4.0f * scale - scroll * scale };
                if (dialog_.warning_text.empty())
                {
                    draw_dim_line(context, notice_empty_text(), { box.x, top, box.width, row }, 0.6f, { .size = text_size });
                    return;
                }
                const auto visible = [&box](const float line_top, const float height) {
                    return line_top + height > box.y && line_top < box.y + box.height;
                };
                draw_text_line(context, u8"검증에 실패한 항목입니다. 지우지 않으면 원문을 그대로 두고 저장합니다.", { box.x, top, box.width, row }, solid_paint(context.palette.warning_accent), { .size = text_size });
                top += row;
                draw_dim_line(context, u8"고치려면 문서 JSON을 고친 뒤 문서 다시 읽기로 불러오십시오.", { box.x, top, box.width, row }, 0.7f);
                top += row;
                const std::size_t columns { function_editor_notice_columns(box.width / scale) };
                for (const std::u8string& line : chunk_function_editor_text(dialog_.warning_text, columns))
                {
                    if (visible(top, row))
                        draw_dim_line(context, line, { box.x, top, box.width, row }, 0.85f);
                    top += row;
                }
                top += 6.0f * scale;
                for (const std::u8string& line : chunk_function_editor_text(dialog_.warning_source, columns))
                {
                    if (visible(top, row))
                        draw_dim_line(context, line, { box.x, top, box.width, row }, 0.7f, { .size = code_size, .face = luil::text_layout_face::code });
                    top += row;
                }
            }

            function_editor_view dialog_ {};
            float scale_ { 1.0f };
        };

        // 미리보기 경고를 한 줄로 잇는다.
        [[nodiscard]] std::u8string preview_warning_text(const function_editor_view& dialog)
        {
            std::u8string warnings {};
            for (const std::u8string& warning : dialog.preview_warnings)
            {
                if (warnings.empty() == false)
                    warnings.append(u8" · ");
                warnings.append(warning);
            }
            return warnings;
        }

        // 판이다. 배경·테두리, 제목, 목록 머리 줄, 접힌 선택기의 이름, 변수 탭, 미리보기, 아래 줄 글을 그린다.
        class editor_panel_element final : public ui_element
        {
        public:
            explicit editor_panel_element(const function_editor_view& dialog)
                : ui_element { ui_element_id { kind_function_editor_panel } }
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
                const float scale { context.scale > 0.0f ? context.scale : 1.0f };
                const editor_geometry geometry { make_geometry(bounds(), scale) };
                draw_box(context, geometry.box, 5.0f * scale, context.palette.surface_background, with_alpha(context.palette.primary_foreground, 0.25f));
                draw_text_line(context, dialog_.title, geometry.title, solid_paint(context.palette.primary_foreground), { .size = 13.0f, .weight = luil::font_weight::bold, .elide = luil::text_elide::middle });
                context.canvas.drawRect(SkRect::MakeXYWH(geometry.box.x, geometry.body.y - 6.0f * scale, geometry.box.width, 1.0f * scale), solid_paint(context.palette.divider));

                if (dialog_.tab == function_editor_tab::variables)
                    draw_variables(context, geometry, scale);
                else if (geometry.collapsed)
                    draw_selector(context, geometry, scale);
                else
                {
                    draw_dim_line(context, dialog_.list_header, geometry.list_header, 0.75f);
                    const float divider { geometry.list_header.x + geometry.list_header.width + layout_function_editor_list_gap * scale / 2.0f };
                    context.canvas.drawRect(SkRect::MakeXYWH(divider, geometry.body.y, 1.0f * scale, geometry.body.height), solid_paint(context.palette.divider));
                }

                if (dialog_.tab == function_editor_tab::functions)
                    draw_preview(context, geometry, scale);
                draw_footer(context, geometry);
                draw_children(context, interaction);
            }

            [[nodiscard]] access_info accessibility() const override
            {
                return { .role = access_role::pane, .name = dialog_.title };
            }

        private:
            void draw_selector(draw_context& context, const editor_geometry& geometry, const float scale) const
            {
                // 접힌 선택기는 `<` 이름 `>`와 도구 버튼이다. 이름 자리는 `<` 다음부터 도구 버튼 앞까지다.
                const float tool { layout_function_editor_tool_size * scale };
                const float left { geometry.selector.x + tool + 6.0f * scale };
                const float right { geometry.selector.x + geometry.selector.width - tool * 6.0f - 12.0f * scale };
                std::u8string name { dialog_.rows.empty() ? std::u8string { u8"기능 없음" } : std::u8string {} };
                if (dialog_.selected.has_value() && *dialog_.selected < dialog_.rows.size())
                {
                    name = dialog_.rows[*dialog_.selected].label;
                    name.append(u8"  (");
                    name.append(function_decimal_text(*dialog_.selected + 1));
                    name.push_back(u8'/');
                    name.append(function_decimal_text(dialog_.rows.size()));
                    name.push_back(u8')');
                }
                draw_text_line(context, name, { left, geometry.selector.y, right - left, tool }, solid_paint(context.palette.primary_foreground), { .size = text_size, .align = luil::text_align::center });
            }

            void draw_variables(draw_context& context, const editor_geometry& geometry, const float scale) const
            {
                const float row { layout_function_editor_variable_row_height() * scale };
                context.canvas.save();
                context.canvas.clipRect(SkRect::MakeXYWH(geometry.body.x, geometry.body.y, geometry.body.width, geometry.body.height));
                float top { geometry.body.y - dialog_.list_scroll * scale };
                draw_dim_line(context, u8"변수는 읽기만 합니다. 사용자 변수는 문서 JSON의 function_buttons.variables에서 고칩니다.", { geometry.body.x, top, geometry.body.width, row }, 0.6f);
                top += row;
                if (dialog_.variables.empty())
                    draw_dim_line(context, u8"정의한 사용자 변수가 없습니다.", { geometry.body.x, top, geometry.body.width, row }, 0.6f, { .size = text_size });
                const float name_width { std::min(220.0f * scale, geometry.body.width / 2.0f) };
                for (const function_editor_variable_view& variable : dialog_.variables)
                {
                    std::u8string name { variable.scope.empty() ? std::u8string {} : variable.scope + u8": " };
                    name.append(variable.name);
                    const ui_color color { variable.warning ? context.palette.warning_accent : context.palette.primary_foreground };
                    draw_text_line(context, name, { geometry.body.x, top, name_width - 8.0f * scale, row }, solid_paint(color), { .size = text_size });
                    draw_dim_line(context, variable.value, { geometry.body.x + name_width, top, geometry.body.width - name_width - layout_scrollbar_hit_width * scale, row }, 0.8f
                        , { .size = code_size, .face = luil::text_layout_face::code, .elide = luil::text_elide::middle });
                    top += row;
                }
                context.canvas.restore();
            }

            // 미리보기 칸은 네 줄이다: 머리(미리보기·대상 카드), 명령줄, 작업 폴더, 경고 (`function_editor_preview_lines`).
            // 높이 계산(`layout_function_editor_preview_height`)이 네 줄을 담으므로 셋이 모두 나와도 경고 줄이 그려진다.
            void draw_preview(draw_context& context, const editor_geometry& geometry, const float scale) const
            {
                const rect_f box { geometry.preview };
                draw_box(context, box, 4.0f * scale, with_alpha(context.palette.primary_foreground, 0.035f), context.palette.divider);
                const float inset { 8.0f * scale };
                const float row { layout_function_editor_preview_line_height() * scale };
                float top { box.y + 3.0f * scale };
                draw_dim_line(context, u8"미리보기", { box.x + inset, top, 80.0f * scale, row }, 0.75f);
                // 대상 카드 이름은 `<` `>` 사이다 (버튼은 본체가 놓는다).
                const float step { 22.0f * scale };
                const float name_right { box.x + box.width - inset - step };
                const float name_left { name_right - 180.0f * scale };
                draw_text_line(context, dialog_.preview_card, { name_left, top, name_right - name_left, row }, solid_paint(context.palette.primary_foreground), { .size = small_size, .align = luil::text_align::center });
                top += row;

                const float width { box.width - inset * 2.0f };
                const text_line_style code { .size = code_size, .face = luil::text_layout_face::code, .elide = luil::text_elide::middle };
                if (dialog_.preview_command.empty() == false)
                {
                    draw_text_line(context, dialog_.preview_command, { box.x + inset, top, width, row }, solid_paint(context.palette.primary_foreground), code);
                    top += row;
                }
                if (dialog_.preview_working_directory.empty() == false)
                {
                    std::u8string folder { u8"작업 폴더  " };
                    folder.append(dialog_.preview_working_directory);
                    draw_dim_line(context, folder, { box.x + inset, top, width, row }, 0.7f, code);
                    top += row;
                }
                // 경고는 남은 줄에 하나로 잇는다. 넘치면 말줄임이고 전체 글은 미리보기 자리의 접근 이름에 있다.
                if (dialog_.preview_warnings.empty() == false)
                    draw_text_line(context, preview_warning_text(dialog_), { box.x + inset, top, width, row }, solid_paint(context.palette.warning_accent), { .size = small_size });
            }

            void draw_footer(draw_context& context, const editor_geometry& geometry) const
            {
                if (dialog_.message.empty() == false)
                {
                    draw_text_line(context, dialog_.message, geometry.footer_text, solid_paint(context.palette.warning_accent), { .size = small_size });
                    return;
                }
                std::u8string hint { u8"적용하면 문서에 바로 씁니다" };
                // 접힌 dialog는 목록 머리 줄이 없으므로 여기에 개수를 보인다.
                if (geometry.collapsed && dialog_.tab == function_editor_tab::functions)
                {
                    hint.append(u8" — ");
                    hint.append(dialog_.list_header);
                }
                draw_dim_line(context, hint, geometry.footer_text, 0.6f);
            }

            function_editor_view dialog_ {};
        };

        // 미리보기 자리의 접근 이름이다. 그린 줄을 차례로 잇는다.
        [[nodiscard]] std::u8string preview_access_name(const function_editor_view& dialog)
        {
            std::u8string name { u8"미리보기" };
            if (dialog.preview_card.empty() == false)
            {
                name.append(u8", ");
                name.append(dialog.preview_card);
            }
            if (dialog.preview_command.empty() == false)
            {
                name.append(u8". 명령줄 ");
                name.append(dialog.preview_command);
            }
            if (dialog.preview_working_directory.empty() == false)
            {
                name.append(u8". 작업 폴더 ");
                name.append(dialog.preview_working_directory);
            }
            if (dialog.preview_warnings.empty() == false)
            {
                name.append(u8". ");
                name.append(preview_warning_text(dialog));
            }
            return name;
        }

        // 변수 탭의 접근 이름이다. 줄마다 `범위: 이름 = 값`이다.
        [[nodiscard]] std::u8string variables_access_name(const function_editor_view& dialog)
        {
            std::u8string name { u8"변수는 읽기만 합니다." };
            if (dialog.variables.empty())
                name.append(u8" 정의한 사용자 변수가 없습니다.");
            for (const function_editor_variable_view& variable : dialog.variables)
            {
                name.push_back(u8'\n');
                if (variable.scope.empty() == false)
                {
                    name.append(variable.scope);
                    name.append(u8": ");
                }
                name.append(variable.name);
                name.append(u8" = ");
                name.append(variable.value);
            }
            return name;
        }

        [[nodiscard]] std::unique_ptr<luil::button_element> make_tool(const std::u8string_view owner, const char32_t glyph, const std::u8string_view tooltip, const bool enabled, ui_action click)
        {
            auto button { std::make_unique<luil::button_element>(ui_element_id { kind_function_editor_tool, std::u8string { owner } }, luil::button_config { .glyph = glyph }) };
            button->set_tooltip(std::u8string { tooltip });
            button->set_access_name(std::u8string { tooltip });
            button->set_enabled(enabled);
            button->set_action(ui_trigger::left_click, std::move(click));
            return button;
        }

        // 한 줄 칸이다. `error`가 있으면 luil이 칸 아래 줄에 그리고 바뀔 때 보조 기술에 알린다 (그 줄만큼 slot을 키운다).
        [[nodiscard]] std::unique_ptr<luil::text_input_element> make_input(const ui_element_kind kind, const text_input_view& value, const std::u8string_view placeholder, const std::u8string_view name, std::u8string error = {})
        {
            auto input { std::make_unique<luil::text_input_element>(ui_element_id { kind }, value, luil::text_input_config { .placeholder = std::u8string { placeholder }, .error = std::move(error) }) };
            input->set_access_name(std::u8string { name });
            return input;
        }

        [[nodiscard]] std::unique_ptr<luil::choice_group_element> make_choice(const std::u8string_view owner, const std::u8string_view name, std::vector<luil::choice_item> items, std::u8string selected, const function_editor_field field)
        {
            luil::choice_group_config config {};
            config.owner = std::u8string { owner };
            config.style = luil::choice_style::toggle;
            config.items = std::move(items);
            config.selected = std::move(selected);
            config.name = std::u8string { name };
            config.select = [field](const std::u8string& value) -> input_action
            {
                return make_logic_action(set_function_editor_field_intent { field, value });
            };
            return std::make_unique<luil::choice_group_element>(std::move(config));
        }
    }

    ui_element_id function_editor_item_id(const std::u8string_view key)
    {
        return { kind_function_editor_item, std::u8string { key } };
    }

    function_editor_dialog_element::function_editor_dialog_element(const function_editor_view& dialog)
        : ui_element { ui_element_id { kind_function_editor_dialog } }
        , dialog_ { dialog }
    {
        // 본체는 Tab 자리가 아니다. 초점은 안의 컨트롤이 받는다 (설계 D1-B).
        set_tab_stop(false);

        auto panel { std::make_unique<editor_panel_element>(dialog_) };
        panel_ = panel.get();
        add_child(std::move(panel));

        // 판이 그린 글의 접근 자리다. 누를 것이 없어 위의 버튼들 아래에 둔다.
        const bool functions { dialog_.tab == function_editor_tab::functions };
        if (functions)
            add_child(std::make_unique<editor_text_element>(u8"preview", access_role::static_text, preview_access_name(dialog_)));
        else
            add_child(std::make_unique<editor_text_element>(u8"variables", access_role::static_text, variables_access_name(dialog_)));
        // 거절·검증 사유다. luil에는 알림(live region) 경로가 없어 alert 역할의 글로 둔다.
        if (dialog_.message.empty() == false)
            add_child(std::make_unique<editor_text_element>(u8"message", access_role::alert, dialog_.message));

        // 기능 / 변수 탭이다.
        luil::choice_group_config tabs {};
        tabs.owner = u8"function-editor-tab";
        tabs.style = luil::choice_style::toggle;
        tabs.width = luil::choice_width::fill;
        tabs.items = { { u8"functions", u8"기능" }, { u8"variables", u8"변수" } };
        tabs.selected = dialog_.tab == function_editor_tab::variables ? u8"variables" : u8"functions";
        tabs.name = u8"기능 편집 탭";
        tabs.select = [](const std::u8string& value) -> input_action
        {
            return make_logic_action(select_function_editor_tab_intent { value == u8"variables" ? function_editor_tab::variables : function_editor_tab::functions });
        };
        add_child(std::make_unique<luil::choice_group_element>(std::move(tabs)));

        const bool has_selection { dialog_.selected.has_value() && *dialog_.selected < dialog_.rows.size() };
        const std::size_t selected { has_selection ? *dialog_.selected : 0 };
        if (functions)
        {
            // 목록이다. 행은 Tab에서 한 자리이고 화살표로 돈다 (luil focus group).
            const std::optional<std::u8string> selected_key { has_selection ? std::optional<std::u8string> { dialog_.rows[selected].key } : std::nullopt };
            auto list { std::make_unique<editor_list_element>(selected_key, dialog_.list_header) };
            for (std::size_t index = 0; index < dialog_.rows.size(); ++index)
                list->adopt(std::make_unique<editor_row_element>(index, dialog_.rows[index], has_selection && index == selected));
            list_ = list.get();
            add_child(std::move(list));

            // 접힌 선택기의 `<` `>`다. 펼친 dialog에서는 숨긴다.
            auto previous { make_tool(u8"previous", codicons::icon_chevron_left, u8"이전 기능", has_selection && selected > 0, make_logic_click(select_function_editor_entry_intent { selected > 0 ? selected - 1 : 0 })) };
            auto next { make_tool(u8"next", codicons::icon_chevron_right, u8"다음 기능", has_selection && selected + 1 < dialog_.rows.size(), make_logic_click(select_function_editor_entry_intent { selected + 1 })) };
            add_child(std::move(previous));
            add_child(std::move(next));

            // 경고 행은 복제할 수 없다 (편집할 수 없는 원문이다). 지우기와 순서 바꾸기는 받는다 ("설계에서 달라진 것" T12).
            const bool editable_selection { has_selection && dialog_.rows[selected].warning == false };
            add_child(make_tool(u8"add", codicons::icon_add, u8"기능 추가", true, make_logic_click(edit_function_editor_list_intent { function_editor_list_edit::add })));
            add_child(make_tool(u8"duplicate", codicons::icon_copy, u8"기능 복제", editable_selection, make_logic_click(edit_function_editor_list_intent { function_editor_list_edit::duplicate })));
            add_child(make_tool(u8"remove", codicons::icon_trash, u8"기능 삭제", has_selection, make_logic_click(edit_function_editor_list_intent { function_editor_list_edit::remove })));
            add_child(make_tool(u8"move-up", codicons::icon_arrow_up, u8"위로 (Alt+↑)", has_selection && selected > 0, make_logic_click(edit_function_editor_list_intent { function_editor_list_edit::move_up })));
            add_child(make_tool(u8"move-down", codicons::icon_arrow_down, u8"아래로 (Alt+↓)", has_selection && selected + 1 < dialog_.rows.size(), make_logic_click(edit_function_editor_list_intent { function_editor_list_edit::move_down })));

            // 폼이다. 고른 항목이 편집할 수 있을 때만 칸을 만든다.
            auto form { std::make_unique<editor_form_element>(dialog_) };
            if (dialog_.editable)
            {
                form->adopt(make_input(kind_function_editor_label_input, dialog_.label, u8"메뉴와 버튼에 보일 이름", u8"이름"));

                // 아이콘 목록이다 (D16의 20개). Tab에서 한 자리이고 화살표로 돈다. 고른 아이콘은 이름에 `선택됨`을 붙인다 — 버튼의
                // `active`는 색으로만 드러난다.
                auto icons { std::make_unique<editor_icons_element>(ui_element_id { kind_function_editor_icon, dialog_.icon }) };
                for (const std::u8string_view name : function_icon_names())
                {
                    const bool active { name == dialog_.icon };
                    auto icon { std::make_unique<luil::button_element>(ui_element_id { kind_function_editor_icon, std::u8string { name } }, luil::button_config { .glyph = function_icon_glyph(name), .active = active }) };
                    std::u8string label { function_icon_label(name) };
                    icon->set_tooltip(label);
                    if (active)
                        label.append(u8", 선택됨");
                    icon->set_access_name(std::move(label));
                    icon->set_action(ui_trigger::left_click, make_logic_click(set_function_editor_field_intent { function_editor_field::icon, std::u8string { name } }));
                    icons->adopt(std::move(icon));
                }
                form->adopt(std::move(icons));

                form->adopt(make_input(kind_function_editor_program_input, dialog_.program, u8"C:\\Tools\\build.exe 또는 ${변수}", u8"프로그램"));
                auto browse {
                    std::make_unique<luil::text_button_element>(ui_element_id { kind_function_editor_program_browse }, luil::text_button_config { .text = std::u8string { u8"찾아보기…" } }),
                };
                // 파일 선택은 Win32 dialog라 UI thread 명령이다 (설계 L7). 고른 경로만 intent로 돌아온다.
                browse->set_action(ui_trigger::left_click, make_app_command_click(app_command::show_function_program_picker));
                form->adopt(std::move(browse));
                form->adopt(make_input(kind_function_editor_arguments_input, dialog_.arguments, u8"--build \"${projectRoot}\\build\"", u8"인자", dialog_.arguments_error ? dialog_.arguments_message : std::u8string {}));
                form->adopt(make_input(kind_function_editor_working_directory_input, dialog_.working_directory, u8"${projectRoot}", u8"작업 폴더"));
                form->adopt(make_input(kind_function_editor_timeout_input, dialog_.timeout, u8"3600", u8"제한 시간(초)"));

                form->adopt(make_choice(u8"function-editor-mode", u8"실행 방식", { { u8"capture", u8"출력을 로그로 받기" }, { u8"detached", u8"따로 실행" } }, dialog_.mode == function_run_mode::detached ? u8"detached" : u8"capture", function_editor_field::mode));
                form->adopt(make_choice(u8"function-editor-scope", u8"적용 범위", { { u8"all", u8"모든 저장소" }, { u8"git", u8"Git" }, { u8"svn", u8"SVN" }, { u8"projects", u8"고른 저장소" } }, dialog_.scope, function_editor_field::scope));
                for (const function_editor_project_view& project : dialog_.projects)
                {
                    luil::check_config check {};
                    check.owner = u8"function-editor-project:" + project.id;
                    check.label = project.name;
                    check.checked = project.checked;
                    check.toggle = make_logic_click(set_function_editor_field_intent { function_editor_field::project, project.id });
                    form->adopt(std::make_unique<luil::check_element>(std::move(check)));
                }

                // 배치 선택 묶음이다. 선택 묶음에는 항목별 비활성이 없어 고를 수 없는 `카드`는 라벨로 알리고 logic이 거절한다 (2.7).
                std::u8string card_label { u8"카드" };
                if (dialog_.card_full)
                    card_label = u8"카드 — 3개까지";
                else if (dialog_.card_shortcut > 0)
                    card_label += u8" (Alt+" + function_decimal_text(dialog_.card_shortcut) + u8")";
                const std::u8string_view placement { function_placement_name(dialog_.placement) };
                form->adopt(make_choice(u8"function-editor-placement", u8"배치", { { u8"card", std::move(card_label) }, { u8"bar", u8"기능 줄" }, { u8"menu", u8"메뉴만" } }, std::u8string { placement }, function_editor_field::placement));

                luil::check_config confirm {};
                confirm.owner = u8"function-editor-confirm";
                confirm.label = u8"실행 전에 명령줄을 보이고 확인합니다";
                confirm.checked = dialog_.confirm;
                confirm.toggle = make_logic_click(set_function_editor_field_intent { function_editor_field::confirm, {} });
                form->adopt(std::make_unique<luil::check_element>(std::move(confirm)));
            }
            form_ = form.get();
            add_child(std::move(form));

            auto preview_previous { std::make_unique<luil::button_element>(ui_element_id { kind_function_editor_preview_step, u8"previous" }, luil::button_config { .glyph = codicons::icon_chevron_left, .icon_size = 12.0f }) };
            preview_previous->set_tooltip(u8"이전 저장소로 미리보기");
            preview_previous->set_access_name(u8"이전 저장소로 미리보기");
            preview_previous->set_enabled(dialog_.preview_can_step);
            preview_previous->set_action(ui_trigger::left_click, make_logic_click(step_function_editor_preview_intent { false }));
            add_child(std::move(preview_previous));
            auto preview_next { std::make_unique<luil::button_element>(ui_element_id { kind_function_editor_preview_step, u8"next" }, luil::button_config { .glyph = codicons::icon_chevron_right, .icon_size = 12.0f }) };
            preview_next->set_tooltip(u8"다음 저장소로 미리보기");
            preview_next->set_access_name(u8"다음 저장소로 미리보기");
            preview_next->set_enabled(dialog_.preview_can_step);
            preview_next->set_action(ui_trigger::left_click, make_logic_click(step_function_editor_preview_intent { true }));
            add_child(std::move(preview_next));
        }

        // 목록(또는 변수 탭)과 폼의 스크롤 막대다. logic의 한계 계산과 같은 content·viewport를 쓴다 (논리 픽셀).
        auto list_bar {
            std::make_unique<luil::scrollbar_element>(ui_element_id { kind_scrollbar, std::u8string { scrollbar_owner_function_editor_list } },
                luil::scrollbar_config {
                    .scroll = [](const float delta) -> input_action { return make_logic_action(function_editor_scroll_intent { function_editor_scroll_region::list, delta }); },
                    .scroll_offset = dialog_.list_scroll,
                }),
        };
        list_bar->set_tab_stop(false);
        list_scrollbar_ = list_bar.get();
        add_child(std::move(list_bar));
        auto form_bar {
            std::make_unique<luil::scrollbar_element>(ui_element_id { kind_scrollbar, std::u8string { scrollbar_owner_function_editor_form } },
                luil::scrollbar_config {
                    .scroll = [](const float delta) -> input_action { return make_logic_action(function_editor_scroll_intent { function_editor_scroll_region::form, delta }); },
                    .scroll_offset = dialog_.form_scroll,
                }),
        };
        form_bar->set_tab_stop(false);
        form_scrollbar_ = form_bar.get();
        add_child(std::move(form_bar));

        auto apply {
            std::make_unique<luil::text_button_element>(
                ui_element_id { kind_function_editor_apply }, luil::text_button_config { .text = std::u8string { u8"적용" }, .visual = luil::text_button_visual::accent }),
        };
        apply->set_enabled(dialog_.locked == false);
        apply->set_action(ui_trigger::left_click, make_logic_click(apply_function_editor_intent {}));
        add_child(std::move(apply));
        auto cancel {
            std::make_unique<luil::text_button_element>(ui_element_id { kind_function_editor_cancel }, luil::text_button_config { .text = std::u8string { u8"취소" } }),
        };
        cancel->set_action(ui_trigger::left_click, make_logic_click(cancel_function_editor_intent {}));
        add_child(std::move(cancel));
    }

    void function_editor_dialog_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);
        const editor_geometry geometry { make_geometry(context.slot, scale) };
        const float logical_width { context.slot.width / scale };
        const float logical_height { context.slot.height / scale };
        panel_->arrange({ context.slot, scale });

        const std::span<const std::unique_ptr<ui_element>> children { this->children() };
        const auto place = [scale](ui_element& element, const rect_f& slot, const bool visible = true) {
            element.set_visible(visible);
            element.arrange({ slot, scale });
        };
        const float tool { layout_function_editor_tool_size * scale };
        const float gap { 2.0f * scale };
        const float bar { layout_scrollbar_hit_width * scale };

        for (const std::unique_ptr<ui_element>& child : children)
        {
            const ui_element_id& id { child->id() };
            if (id.kind == luil::ui_element_kind::choice_group)
                place(*child, geometry.tabs);
            else if (id.kind == kind_function_editor_text)
            {
                if (id.owner == u8"preview")
                    place(*child, geometry.preview);
                else if (id.owner == u8"variables")
                    place(*child, geometry.body);
                else
                    place(*child, geometry.footer_text);
            }
            else if (id.kind == kind_function_editor_tool)
            {
                // 펼친 dialog는 목록 아래 도구 줄에, 접힌 dialog는 선택기 줄 오른쪽에 둔다. `<` `>`는 접힌 dialog에만 있다.
                constexpr std::array<std::u8string_view, 5> order { u8"add", u8"duplicate", u8"remove", u8"move-up", u8"move-down" };
                const auto found { std::ranges::find(order, id.owner) };
                if (found == order.end())
                {
                    const bool next { id.owner == u8"next" };
                    const float x { next ? geometry.selector.x + geometry.selector.width - tool * 6.0f - 12.0f * scale : geometry.selector.x };
                    place(*child, { x, geometry.selector.y + 3.0f * scale, tool, tool }, geometry.collapsed);
                    continue;
                }
                const float index { static_cast<float>(found - order.begin()) };
                if (geometry.collapsed)
                {
                    const float right { geometry.selector.x + geometry.selector.width };
                    place(*child, { right - (5.0f - index) * (tool + gap), geometry.selector.y + 3.0f * scale, tool, tool });
                }
                else
                    place(*child, { geometry.tools.x + index * (tool + gap), geometry.tools.y + 3.0f * scale, tool, tool });
            }
            else if (id.kind == kind_function_editor_preview_step)
            {
                const float step { 22.0f * scale };
                const float right { geometry.preview.x + geometry.preview.width - 8.0f * scale };
                const float x { id.owner == u8"next" ? right - step : right - step - 180.0f * scale - step };
                place(*child, { x, geometry.preview.y + 1.0f * scale, step, step });
            }
            else if (id.kind == kind_function_editor_apply || id.kind == kind_function_editor_cancel)
            {
                const float width { action_button_width * scale };
                const float height { action_button_height * scale };
                const float top { geometry.footer.y + (geometry.footer.height - height) / 2.0f };
                const float right { geometry.footer.x + geometry.footer.width };
                const float x { id.kind == kind_function_editor_cancel ? right - width : right - width * 2.0f - 8.0f * scale };
                place(*child, { x, top, width, height });
            }
        }

        // 목록이다. 행은 스크롤을 반영하고, 영역 밖 행도 숨기지 않는다 (목록의 자르기가 그리기와 hit를 막는다). 접힌 dialog에는
        // 목록이 보이지 않는다.
        if (list_ != nullptr)
        {
            place(*list_, geometry.list_rows, geometry.collapsed == false);
            const float row { layout_function_editor_row_height() * scale };
            float top { geometry.list_rows.y - dialog_.list_scroll * scale };
            for (const std::unique_ptr<ui_element>& item : list_->children())
            {
                place(*item, { geometry.list_rows.x, top, geometry.list_rows.width, row });
                top += row;
            }
        }

        // 목록 막대는 기능 탭의 목록 오른쪽 또는 변수 탭의 오른쪽 끝이다.
        {
            const bool variables { dialog_.tab == function_editor_tab::variables };
            const float content { variables ? function_editor_variables_content_height(dialog_.variables.size()) : function_editor_list_content_height(dialog_.rows.size()) };
            const float viewport { variables ? function_editor_body_height(logical_height) : function_editor_list_viewport_height(logical_height) };
            auto* const scrollbar { static_cast<luil::scrollbar_element*>(list_scrollbar_) };
            scrollbar->set_metrics(content, viewport, dialog_.list_scroll);
            const rect_f slot {
                variables
                    ? rect_f { geometry.body.x + geometry.body.width - bar, geometry.body.y, bar, geometry.body.height }
                    : rect_f { geometry.list_rows.x + geometry.list_rows.width - bar, geometry.list_rows.y, bar, geometry.list_rows.height }
            };
            place(*list_scrollbar_, slot, content > viewport && (variables || geometry.collapsed == false));
        }

        // 폼이다. 줄 자리와 내용 높이는 logic의 스크롤 한계와 같은 함수(`layout_function_editor_form`,
        // `function_editor_form_content_height`)다. 경고 행의 안내도 넘치면 스크롤한다.
        bool form_scrolls { false };
        if (form_ != nullptr)
        {
            auto* const form { static_cast<editor_form_element*>(form_) };
            const float form_width { function_editor_form_width(logical_width) };
            form->layout = layout_function_editor_form(form_width, dialog_.projects.size());
            form->scroll = dialog_.form_scroll;
            place(*form_, geometry.form);
            const float viewport { function_editor_form_viewport_height(logical_height, geometry.collapsed) };
            const float content { function_editor_form_content_height(dialog_.editable, dialog_.projects.size(), dialog_.warning_text, dialog_.warning_source, logical_width) };
            form_scrolls = content > viewport;
            auto* const scrollbar { static_cast<luil::scrollbar_element*>(form_scrollbar_) };
            scrollbar->set_metrics(content, viewport, dialog_.form_scroll);

            const float label_width { layout_function_editor_label_width * scale };
            const float field_left { geometry.form.x + label_width };
            const float field_width { geometry.form.width - label_width };
            const float field_height { layout_function_editor_field_height() * scale };
            const auto top_of = [&](const function_editor_form_row row) {
                return geometry.form.y + (form->layout.tops[static_cast<std::size_t>(row)] - dialog_.form_scroll) * scale;
            };
            const std::size_t columns { function_editor_icon_columns(form_width) };
            std::size_t project { 0 };
            for (const std::unique_ptr<ui_element>& control : form_->children())
            {
                const ui_element_id& id { control->id() };
                if (id.kind == kind_function_editor_label_input)
                    place(*control, { field_left, top_of(function_editor_form_row::label), field_width, field_height });
                else if (id.kind == kind_function_editor_icons)
                {
                    // 아이콘 목록이다. 칸 폭에 들어가는 만큼 한 줄에 놓고 다음 줄로 넘긴다.
                    const float top { top_of(function_editor_form_row::icon) };
                    const float slot { layout_function_editor_icon_slot * scale };
                    place(*control, { field_left, top, field_width, form->layout.heights[static_cast<std::size_t>(function_editor_form_row::icon)] * scale });
                    std::size_t index { 0 };
                    for (const std::unique_ptr<ui_element>& icon : control->children())
                    {
                        const float x { field_left + static_cast<float>(index % columns) * slot };
                        const float y { top + static_cast<float>(index / columns) * slot };
                        place(*icon, { x, y, slot - 4.0f * scale, slot - 4.0f * scale });
                        ++index;
                    }
                }
                else if (id.kind == kind_function_editor_program_input)
                    place(*control, { field_left, top_of(function_editor_form_row::program), field_width - (layout_function_editor_browse_width + 6.0f) * scale, field_height });
                else if (id.kind == kind_function_editor_program_browse)
                    place(*control, { field_left + field_width - layout_function_editor_browse_width * scale, top_of(function_editor_form_row::program), layout_function_editor_browse_width * scale, field_height });
                else if (id.kind == kind_function_editor_arguments_input)
                {
                    // 오류 글이 있으면 luil이 slot 아래쪽에 그 줄을 그린다. 입력 상자는 위쪽 칸 높이 그대로다.
                    const float error { dialog_.arguments_error ? luil::text_input_error_height * scale : 0.0f };
                    place(*control, { field_left, top_of(function_editor_form_row::arguments), field_width, field_height + error });
                }
                else if (id.kind == kind_function_editor_working_directory_input)
                    place(*control, { field_left, top_of(function_editor_form_row::working_directory), field_width, field_height });
                else if (id.kind == kind_function_editor_timeout_input)
                    place(*control, { field_left, top_of(function_editor_form_row::timeout), layout_function_editor_timeout_width * scale, field_height });
                else if (id.kind == luil::ui_element_kind::choice_group)
                {
                    function_editor_form_row row { function_editor_form_row::mode };
                    if (id.owner == u8"function-editor-scope")
                        row = function_editor_form_row::scope;
                    else if (id.owner == u8"function-editor-placement")
                        row = function_editor_form_row::placement;
                    place(*control, { field_left, top_of(row), field_width, luil::choice_toggle_height * scale });
                }
                else if (id.kind == luil::ui_element_kind::check)
                {
                    const float check { layout_function_editor_check_height() * scale };
                    if (id.owner == u8"function-editor-confirm")
                        place(*control, { field_left, top_of(function_editor_form_row::confirm), field_width, check });
                    else
                    {
                        const float top { top_of(function_editor_form_row::scope) + field_height + 4.0f * scale + static_cast<float>(project) * check };
                        place(*control, { field_left, top, field_width, check });
                        ++project;
                    }
                }
            }
        }
        place(*form_scrollbar_, geometry.form_scrollbar, form_scrolls);
    }

    void function_editor_dialog_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        // 뒤를 덮는 scrim은 host가 그린다. 본체는 자식(판·목록·폼·버튼·막대)만 그린다.
        draw_children(context, interaction);
    }

    std::unique_ptr<luil::modal_host_element> make_function_editor_dialog(const function_editor_view& view, const float window_width, const float window_height)
    {
        const function_editor_size size { function_editor_dialog_size(window_width, window_height) };
        dialog_host_spec spec {};
        spec.owner = std::u8string { dialog_owner_function_editor };
        spec.name = std::u8string { u8"기능 편집" };
        spec.content_width = size.width;
        spec.content_height = size.height;
        // 배경 클릭은 흡수만 한다. 고친 초안을 실수로 버리지 않게 한다. Esc는 취소다.
        spec.dismiss = make_logic_click(cancel_function_editor_intent {});
        return make_dialog_host(std::move(spec), std::make_unique<function_editor_dialog_element>(view));
    }
}
