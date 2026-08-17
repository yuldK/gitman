#include "presentation/card_list_view.h"

#include "gitman/generated/codicons.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace gitman {
    namespace {
        // status_presentation의 Codicon 이름을 embedded 글리프로 옮긴다. 이름이 새로
        // 생기면 여기에 더해야 하며, 모르는 이름은 question으로 표시해 침묵하지 않는다.
        char32_t codicon_for_name(const std::u8string_view name) noexcept
        {
            if (name == u8"pass")
                return codicons::icon_pass;
            if (name == u8"arrow-down")
                return codicons::icon_arrow_down;
            if (name == u8"arrow-up")
                return codicons::icon_arrow_up;
            if (name == u8"git-compare")
                return codicons::icon_git_compare;
            if (name == u8"key")
                return codicons::icon_key;
            if (name == u8"home")
                return codicons::icon_home;
            if (name == u8"warning")
                return codicons::icon_warning;
            if (name == u8"debug-disconnect")
                return codicons::icon_debug_disconnect;
            if (name == u8"error")
                return codicons::icon_error;
            if (name == u8"circle-slash")
                return codicons::icon_circle_slash;
            return codicons::icon_question;
        }

        SkPaint solid_paint(const ui_color color)
        {
            SkPaint paint {};
            paint.setAntiAlias(true);
            paint.setColor(color);
            return paint;
        }

        void draw_text(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const SkFont& font, const SkPaint& paint)
        {
            if (text.empty())
                return;
            canvas.drawSimpleText(text.data(), text.size(), SkTextEncoding::kUTF8, x, y, font, paint);
        }

        void draw_centered_glyph(SkCanvas& canvas, const char32_t codepoint, const rect_f& target, const SkFont& font, const SkPaint& paint)
        {
            const SkGlyphID glyph { font.unicharToGlyph(static_cast<SkUnichar>(codepoint)) };
            if (glyph == 0)
                return;

            const SkRect glyph_bounds { font.getBounds(glyph, &paint) };
            if (glyph_bounds.isEmpty())
                return;

            const SkRect bounds { SkRect::MakeXYWH(target.x, target.y, target.width, target.height) };
            const std::array glyphs { glyph };
            const std::array positions {
                SkPoint {
                    bounds.centerX() - glyph_bounds.centerX(),
                    bounds.centerY() - glyph_bounds.centerY(),
                },
            };
            canvas.drawGlyphs(glyphs, positions, SkPoint {}, font, paint);
        }

        std::u8string_view empty_state_text(const view_empty_state state) noexcept
        {
            switch (state)
            {
            case view_empty_state::no_document:
                return u8"열린 문서가 없습니다. 오른쪽 위 버튼으로 .verison-list 문서를 여세요.";
            case view_empty_state::document_loading:
                return u8"문서를 여는 중입니다...";
            case view_empty_state::no_projects:
                return u8"문서에 등록된 프로젝트가 없습니다.";
            case view_empty_state::no_filter_match:
                return u8"필터와 일치하는 카드가 없습니다.";
            case view_empty_state::none:
                break;
            }
            return u8"";
        }

        ui_color state_accent(const ui_color_palette& palette, const card_view_state state) noexcept
        {
            switch (state)
            {
            case card_view_state::failed:
                return make_ui_color(224, 108, 117);
            case card_view_state::warning:
                return palette.warning_accent;
            case card_view_state::ready:
                return palette.positive_accent;
            case card_view_state::loading:
            case card_view_state::running:
            case card_view_state::disabled:
                break;
            }
            return palette.primary_foreground;
        }
    } // namespace

    void draw_card_list(SkCanvas& canvas, SkTypeface* const codicon_typeface, SkTypeface* const ui_typeface, const view_snapshot& view, const layout_snapshot& layout, const color_theme theme)
    {
        const ui_color_palette& palette { color_palette_for(theme) };
        const float scale { layout.scale > 0.0f ? layout.scale : 1.0f };

        canvas.clear(palette.window_background);

        const SkFont title_font { sk_ref_sp(ui_typeface), 15.0f * scale };
        const SkFont body_font { sk_ref_sp(ui_typeface), 12.0f * scale };
        const SkFont icon_font { sk_ref_sp(codicon_typeface), 16.0f * scale };
        const SkPaint foreground { solid_paint(palette.primary_foreground) };
        SkPaint dim_foreground { solid_paint(palette.primary_foreground) };
        dim_foreground.setAlphaf(0.6f);

        // toolbar 배경과 문서 경로.
        const float caption_height { layout_caption_height * scale };
        const float toolbar_height { layout_toolbar_height * scale };
        canvas.drawRect(SkRect::MakeXYWH(0.0f, caption_height, layout.window_width, toolbar_height), solid_paint(palette.surface_background));
        const std::u8string_view toolbar_text { view.document_path.empty() ? std::u8string_view { u8"문서 없음" } : std::u8string_view { view.document_path } };
        draw_text(canvas, toolbar_text, layout_margin * scale, caption_height + toolbar_height / 2.0f + 4.0f * scale, body_font, dim_foreground);

        // 문서 수준 진단은 toolbar 아래 첫 줄에 요약한다.
        if (view.notices.empty() == false)
        {
            SkPaint notice_paint { solid_paint(make_ui_color(224, 108, 117)) };
            draw_text(canvas, view.notices.front(), layout_margin * scale, caption_height + toolbar_height + 14.0f * scale, body_font, notice_paint);
        }

        // 카드: layout이 화면에 걸친 것만 담고 있으므로 그대로 순회한다.
        for (const hit_area& area : layout.areas)
        {
            if (area.kind != hit_target_kind::card_body)
                continue;

            const card_view_model* card { nullptr };
            for (const card_view_model& candidate : view.cards)
            {
                if (candidate.id == area.id)
                {
                    card = &candidate;
                    break;
                }
            }
            if (card == nullptr)
                continue;

            const SkRect body { SkRect::MakeXYWH(area.bounds.x, area.bounds.y, area.bounds.width, area.bounds.height) };
            SkPaint card_paint { solid_paint(palette.surface_background) };
            if (card->enabled == false)
                card_paint.setAlphaf(0.5f);
            canvas.drawRRect(SkRRect::MakeRectXY(body, 6.0f * scale, 6.0f * scale), card_paint);
            if (card->selected)
            {
                SkPaint border { solid_paint(palette.positive_accent) };
                border.setStyle(SkPaint::kStroke_Style);
                border.setStrokeWidth(2.0f * scale);
                canvas.drawRRect(SkRRect::MakeRectXY(body, 6.0f * scale, 6.0f * scale), border);
            }

            const float padding { 10.0f * scale };
            const float text_x { area.bounds.x + 32.0f * scale };
            const float line_1 { area.bounds.y + padding + 12.0f * scale };
            const float line_2 { line_1 + 18.0f * scale };
            const float line_3 { line_2 + 16.0f * scale };

            // 상태 아이콘: 진행 중이면 sync, 아니면 상태 글리프다.
            const rect_f icon_slot { area.bounds.x + padding, area.bounds.y + padding, 18.0f * scale, 18.0f * scale };
            SkPaint icon_paint { solid_paint(state_accent(palette, card->state)) };
            const char32_t glyph { card->busy ? codicons::icon_sync : codicon_for_name(card->status.codicon) };
            if (codicon_typeface != nullptr)
                draw_centered_glyph(canvas, glyph, icon_slot, icon_font, icon_paint);

            draw_text(canvas, card->display_name, text_x, line_1, title_font, foreground);
            draw_text(canvas, card->path, text_x, line_2, body_font, dim_foreground);

            // 세 번째 줄: 참조(브랜치/URL), 리비전, 상태 툴팁과 작업 트리 요약.
            std::u8string detail {};
            if (card->reference.empty() == false)
                detail.append(card->reference);
            if (card->revision.empty() == false)
            {
                if (detail.empty() == false)
                    detail.append(u8" @ ");
                detail.append(card->revision);
            }
            if (card->status.tooltip.empty() == false)
            {
                if (detail.empty() == false)
                    detail.append(u8" · ");
                detail.append(card->status.tooltip);
            }
            if (card->working_tree_text.empty() == false)
            {
                if (detail.empty() == false)
                    detail.append(u8" · ");
                detail.append(card->working_tree_text);
            }
            draw_text(canvas, detail, text_x, line_3, body_font, dim_foreground);
        }

        // 버튼: refresh는 활성, update와 switch는 단계 7 전까지 비활성 표시다.
        for (const hit_area& area : layout.areas)
        {
            char32_t glyph { 0 };
            bool enabled { true };
            switch (area.kind)
            {
            case hit_target_kind::toolbar_refresh_all:
            case hit_target_kind::card_refresh:
                glyph = codicons::icon_refresh;
                break;
            case hit_target_kind::toolbar_open_document:
                glyph = codicons::icon_folder_opened;
                break;
            case hit_target_kind::card_update_disabled:
                glyph = codicons::icon_repo_pull;
                enabled = false;
                break;
            case hit_target_kind::card_switch_disabled:
                glyph = codicons::icon_source_control;
                enabled = false;
                break;
            case hit_target_kind::card_body:
            case hit_target_kind::none:
                continue;
            }

            SkPaint button_paint { solid_paint(palette.primary_foreground) };
            if (enabled == false)
                button_paint.setAlphaf(0.3f);
            if (codicon_typeface != nullptr)
                draw_centered_glyph(canvas, glyph, area.bounds, icon_font, button_paint);
        }

        const std::u8string_view empty_text { empty_state_text(view.empty_state) };
        if (empty_text.empty() == false)
        {
            const float y { caption_height + (layout.window_height - caption_height) / 2.0f };
            draw_text(canvas, empty_text, layout_margin * scale * 2.0f, y, title_font, dim_foreground);
        }
    }
} // namespace gitman
