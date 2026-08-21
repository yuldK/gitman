#include "presentation/ui/draw_primitives.h"

#include "gitman/generated/codicons.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRect.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gitman::ui {
    namespace {
        // 자를 수 있는 위치, 즉 UTF-8 이어지는 바이트(10xxxxxx)가 아닌 지점들이다.
        // 끝 위치(size)를 포함해 오름차순으로 담는다.
        std::vector<std::size_t> character_boundaries(const std::u8string_view text)
        {
            std::vector<std::size_t> boundaries {};
            boundaries.reserve(text.size() + 1);
            for (std::size_t index = 0; index < text.size(); ++index)
                if ((static_cast<std::uint8_t>(text[index]) & 0xC0U) != 0x80U)
                    boundaries.push_back(index);
            boundaries.push_back(text.size());
            return boundaries;
        }
    } // namespace

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

    ui_color with_alpha(const ui_color color, const float alpha) noexcept
    {
        const float clamped { alpha < 0.0f ? 0.0f : (alpha > 1.0f ? 1.0f : alpha) };
        const auto value { static_cast<std::uint32_t>(clamped * 255.0f + 0.5f) };
        return (color & 0x00ffffffU) | (value << 24U);
    }

    float measure_text(const std::u8string_view text, const SkFont& font)
    {
        if (text.empty())
            return 0.0f;
        return font.measureText(text.data(), text.size(), SkTextEncoding::kUTF8);
    }

    std::u8string elide_text(const std::u8string_view text, const float max_width, const SkFont& font)
    {
        if (text.empty() || max_width <= 0.0f)
            return {};
        if (measure_text(text, font) <= max_width)
            return std::u8string { text };

        constexpr std::u8string_view ellipsis { u8"…" };
        const float ellipsis_width { measure_text(ellipsis, font) };
        if (ellipsis_width > max_width)
            return {};

        // 경계 목록에서 들어가는 가장 긴 앞부분을 이분 탐색한다. 빈 앞부분은 항상
        // 들어가고(위에서 `…` 폭을 확인했다) 전체는 들어가지 않는 것이 확정이다.
        const std::vector<std::size_t> boundaries { character_boundaries(text) };
        std::size_t low { 0 };
        std::size_t high { boundaries.size() - 1 };
        while (low < high)
        {
            const std::size_t middle { low + (high - low + 1) / 2 };
            if (measure_text(text.substr(0, boundaries[middle]), font) + ellipsis_width <= max_width)
                low = middle;
            else
                high = middle - 1;
        }

        std::u8string result { text.substr(0, boundaries[low]) };
        result.append(ellipsis);
        return result;
    }

    float draw_text_within(SkCanvas& canvas, const std::u8string_view text, const float x, const float y, const float max_width, const SkFont& font, const SkPaint& paint)
    {
        const std::u8string drawn { elide_text(text, max_width, font) };
        if (drawn.empty())
            return 0.0f;
        draw_text(canvas, drawn, x, y, font, paint);
        return measure_text(drawn, font);
    }

    void draw_downward_shadow(SkCanvas& canvas, const rect_f& area, const ui_color color, const float strength)
    {
        if (area.width <= 0.0f || area.height <= 0.0f || strength <= 0.0f)
            return;

        // 띠 개수는 고정이다. DPI가 커지면 띠가 두꺼워질 뿐 계단이 더 보이지 않는다.
        constexpr int band_count { 8 };
        const float band_height { area.height / static_cast<float>(band_count) };
        for (int band = 0; band < band_count; ++band)
        {
            // 위쪽이 가장 진하고 아래로 갈수록 제곱으로 옅어진다.
            const float distance { static_cast<float>(band) / static_cast<float>(band_count) };
            const float remaining { 1.0f - distance };
            SkPaint paint { solid_paint(with_alpha(color, strength * remaining * remaining)) };
            paint.setAntiAlias(false);
            const float top { area.y + band_height * static_cast<float>(band) };
            canvas.drawRect(SkRect::MakeXYWH(area.x, top, area.width, band_height), paint);
        }
    }

    void draw_upward_shadow(SkCanvas& canvas, const rect_f& area, const ui_color color, const float strength)
    {
        if (area.width <= 0.0f || area.height <= 0.0f || strength <= 0.0f)
            return;

        constexpr int band_count { 8 };
        const float band_height { area.height / static_cast<float>(band_count) };
        for (int band = 0; band < band_count; ++band)
        {
            // 아래쪽이 가장 진하고 위로 갈수록 제곱으로 옅어진다.
            const float distance { static_cast<float>(band) / static_cast<float>(band_count) };
            const float remaining { 1.0f - distance };
            SkPaint paint { solid_paint(with_alpha(color, strength * remaining * remaining)) };
            paint.setAntiAlias(false);
            const float bottom { area.y + area.height - band_height * static_cast<float>(band) };
            canvas.drawRect(SkRect::MakeXYWH(area.x, bottom - band_height, area.width, band_height), paint);
        }
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

    float centered_text_baseline(const SkFont& font, const float target_height)
    {
        SkFontMetrics font_metrics {};
        font.getMetrics(&font_metrics);
        return target_height * 0.5f - (font_metrics.fAscent + font_metrics.fDescent) * 0.5f;
    }

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
} // namespace gitman::ui
