#pragma once

#include "presentation/list_metrics.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gitman {
    // 기능 편집 dialog(function-buttons-design 2.7)의 치수다 (논리 픽셀). logic의 스크롤 한계와 element의 배치가 같은
    // 함수를 써야 스크롤이 어긋나지 않는다. element 어휘가 없는 값 헤더라 logic이 include해도 된다.
    // 글이 드는 줄은 100% 값에 그 글이 시스템 글자 크기만큼 자란 몫을 더한다 (`text_growth`, text-font-migration-design 7.3).
    inline constexpr float layout_function_editor_width { 760.0f };
    inline constexpr float layout_function_editor_height { 520.0f };
    // 창 가장자리와 dialog 사이에 남기는 여백(양쪽 합)이다. 크기는 `min(760, W-40) × min(520, H-40)`다.
    inline constexpr float layout_function_editor_window_margin { 40.0f };
    inline constexpr float layout_function_editor_padding { 14.0f };
    // 제목과 탭 묶음 줄, 아래 버튼 줄이다.
    inline constexpr float layout_function_editor_header_height { 42.0f };
    inline constexpr float layout_function_editor_footer_height { 48.0f };
    // 이 폭보다 좁으면 목록을 위쪽 한 줄 선택기로 접고 폼을 전체 폭으로 쓴다.
    inline constexpr float layout_function_editor_collapse_width { 640.0f };
    inline constexpr float layout_function_editor_list_width { 220.0f };
    inline constexpr float layout_function_editor_list_gap { 12.0f };
    inline constexpr float layout_function_editor_tools_height { 34.0f };
    inline constexpr float layout_function_editor_tool_size { 28.0f };
    inline constexpr float layout_function_editor_selector_height { 34.0f };
    // 폼의 왼쪽 라벨 열과 줄 사이다.
    inline constexpr float layout_function_editor_label_width { 76.0f };
    inline constexpr float layout_function_editor_row_gap { 8.0f };
    inline constexpr float layout_function_editor_icon_slot { 28.0f };
    inline constexpr float layout_function_editor_browse_width { 84.0f };
    inline constexpr float layout_function_editor_timeout_width { 72.0f };
    // D16 아이콘 이름 수다 (`function_icon_names`). 같은지는 test가 본다 (`function_icon_count`).
    inline constexpr std::size_t function_editor_icon_count { 20 };
    // 미리보기 칸의 줄 수다: 머리(미리보기·대상 카드), 명령줄, 작업 폴더, 경고.
    inline constexpr std::size_t function_editor_preview_lines { 4 };
    // 경고 행의 사유와 원문을 나누는 한 줄의 칸 수다. 한글 같은 넓은 글자는 두 칸이다 (`chunk_function_editor_text`).
    // 한 칸은 코드 글(11)의 고정폭 글자보다 조금 넓게 잡는다.
    inline constexpr float layout_function_editor_notice_column_width { 7.0f };

    // 목록 행이다. 100%에서 28이고 이름(12)이 자란다.
    [[nodiscard]] inline float layout_function_editor_row_height() noexcept
    {
        return 28.0f + text_growth(12.0f);
    }
    // 목록 머리 줄(`기능 N개 · 카드 k/3`)이다. 100%에서 22이고 글(11)이 자란다.
    [[nodiscard]] inline float layout_function_editor_list_header_height() noexcept
    {
        return 22.0f + text_growth(11.0f);
    }
    // 폼의 칸 한 줄이다. 100%에서 26이고 칸 글(11)이 자란다.
    [[nodiscard]] inline float layout_function_editor_field_height() noexcept
    {
        return 26.0f + text_growth(11.0f);
    }
    // 칸 아래 안내 줄이다. 100%에서 16이고 글(11)이 자란다.
    [[nodiscard]] inline float layout_function_editor_note_height() noexcept
    {
        return 16.0f + text_growth(11.0f);
    }
    // 체크 한 줄이다. 100%에서 24이고 라벨(12)이 자란다.
    [[nodiscard]] inline float layout_function_editor_check_height() noexcept
    {
        return 24.0f + text_growth(12.0f);
    }
    // 변수 탭의 행이다. 100%에서 22이고 이름(12)이 자란다.
    [[nodiscard]] inline float layout_function_editor_variable_row_height() noexcept
    {
        return 22.0f + text_growth(12.0f);
    }
    // 미리보기 한 줄이다. 100%에서 18이고 코드 글(11)이 자란다.
    [[nodiscard]] inline float layout_function_editor_preview_line_height() noexcept
    {
        return 18.0f + text_growth(11.0f);
    }
    // 미리보기 칸이다. 위아래 바깥 여백 4씩, 안쪽 여백 3씩과 줄 넷이다 (100%에서 86).
    [[nodiscard]] inline float layout_function_editor_preview_height() noexcept
    {
        return 14.0f + static_cast<float>(function_editor_preview_lines) * layout_function_editor_preview_line_height();
    }
    // 경고 행의 안내·사유·원문 한 줄이다. 100%에서 18이고 글(12)이 자란다.
    [[nodiscard]] inline float layout_function_editor_notice_line_height() noexcept
    {
        return 18.0f + text_growth(12.0f);
    }

    struct function_editor_size
    {
        float width { 0.0f };
        float height { 0.0f };
    };

    [[nodiscard]] inline function_editor_size function_editor_dialog_size(const float window_width, const float window_height) noexcept
    {
        float width { window_width - layout_function_editor_window_margin };
        if (width > layout_function_editor_width)
            width = layout_function_editor_width;
        float height { window_height - layout_function_editor_window_margin };
        if (height > layout_function_editor_height)
            height = layout_function_editor_height;
        return { width < 0.0f ? 0.0f : width, height < 0.0f ? 0.0f : height };
    }

    [[nodiscard]] inline bool function_editor_collapsed(const float dialog_width) noexcept
    {
        return dialog_width < layout_function_editor_collapse_width;
    }

    // 머리 줄과 미리보기·버튼 줄 사이의 높이다. 목록·폼·변수 탭이 이 자리를 나눈다.
    [[nodiscard]] inline float function_editor_body_height(const float dialog_height) noexcept
    {
        const float body { dialog_height - layout_function_editor_header_height - layout_function_editor_preview_height() - layout_function_editor_footer_height };
        return body < 0.0f ? 0.0f : body;
    }

    // 펼친 목록의 행 영역 높이다 (머리 줄과 도구 줄을 뺀 자리).
    [[nodiscard]] inline float function_editor_list_viewport_height(const float dialog_height) noexcept
    {
        const float viewport { function_editor_body_height(dialog_height) - layout_function_editor_list_header_height() - layout_function_editor_tools_height };
        return viewport < 0.0f ? 0.0f : viewport;
    }

    // 목록의 내용 높이다.
    [[nodiscard]] inline float function_editor_list_content_height(const std::size_t rows) noexcept
    {
        return static_cast<float>(rows) * layout_function_editor_row_height();
    }

    // 변수 탭의 내용 높이다. 맨 위 안내 줄이 한 행이다.
    [[nodiscard]] inline float function_editor_variables_content_height(const std::size_t variables) noexcept
    {
        return static_cast<float>(variables + 1) * layout_function_editor_variable_row_height();
    }

    // 폼이 보이는 높이다. 접힌 dialog는 위의 선택기 줄을 뺀다.
    [[nodiscard]] inline float function_editor_form_viewport_height(const float dialog_height, const bool collapsed) noexcept
    {
        const float viewport { function_editor_body_height(dialog_height) - (collapsed ? layout_function_editor_selector_height : 0.0f) };
        return viewport < 0.0f ? 0.0f : viewport;
    }

    // 폼의 폭이다. 스크롤 막대 자리를 늘 비워 둬 내용이 넘치는지에 따라 칸이 좁아지지 않게 한다.
    [[nodiscard]] inline float function_editor_form_width(const float dialog_width) noexcept
    {
        float width { dialog_width - layout_function_editor_padding * 2.0f - layout_scrollbar_hit_width };
        if (function_editor_collapsed(dialog_width) == false)
            width -= layout_function_editor_list_width + layout_function_editor_list_gap;
        return width < 0.0f ? 0.0f : width;
    }

    [[nodiscard]] inline std::size_t function_editor_icon_columns(const float form_width) noexcept
    {
        const float field { form_width - layout_function_editor_label_width };
        const std::size_t columns { field > 0.0f ? static_cast<std::size_t>(field / layout_function_editor_icon_slot) : 0 };
        return columns == 0 ? 1 : columns;
    }

    // 폼의 줄이다. 순서가 곧 그리기·Tab 순서다.
    enum class function_editor_form_row
    {
        label,
        icon,
        program,
        arguments,
        working_directory,
        timeout,
        mode,
        scope,
        placement,
        confirm,
        count,
    };

    inline constexpr std::size_t function_editor_form_row_count { static_cast<std::size_t>(function_editor_form_row::count) };

    struct function_editor_form_layout
    {
        // 줄마다 폼 맨 위에서 잰 위쪽 좌표와 높이다 (스크롤 전).
        std::array<float, function_editor_form_row_count> tops {};
        std::array<float, function_editor_form_row_count> heights {};
        float content_height { 0.0f };
    };

    // `project_rows`는 적용 범위가 `저장소 목록`일 때 늘어놓는 카드 수다 (아니면 0).
    [[nodiscard]] inline function_editor_form_layout layout_function_editor_form(const float form_width, const std::size_t project_rows) noexcept
    {
        const std::size_t columns { function_editor_icon_columns(form_width) };
        const std::size_t icon_rows { (function_editor_icon_count + columns - 1) / columns };
        const float field_height { layout_function_editor_field_height() };
        function_editor_form_layout layout {};
        float top { 0.0f };
        for (std::size_t index = 0; index < function_editor_form_row_count; ++index)
        {
            float height { field_height };
            switch (static_cast<function_editor_form_row>(index))
            {
            case function_editor_form_row::icon:
                height = static_cast<float>(icon_rows) * layout_function_editor_icon_slot - 4.0f;
                break;
            case function_editor_form_row::arguments:
            case function_editor_form_row::placement:
                height += layout_function_editor_note_height();
                break;
            case function_editor_form_row::scope:
                if (project_rows > 0)
                    height += 4.0f + static_cast<float>(project_rows) * layout_function_editor_check_height();
                break;
            case function_editor_form_row::confirm:
                height = layout_function_editor_check_height();
                break;
            default:
                break;
            }
            layout.tops[index] = top;
            layout.heights[index] = height;
            top += height + layout_function_editor_row_gap;
        }
        layout.content_height = top - layout_function_editor_row_gap;
        return layout;
    }

    // 경고 행의 사유·원문 한 줄에 드는 칸 수다. 글 폭을 재지 않고 칸으로 어림한다 — logic(스크롤 한계)은 글꼴을 모른다.
    [[nodiscard]] inline std::size_t function_editor_notice_columns(const float form_width) noexcept
    {
        // 글(11)이 자란 비율만큼 한 칸도 넓어진다.
        const float growth { (11.0f + text_growth(11.0f)) / 11.0f };
        const float columns { form_width / (layout_function_editor_notice_column_width * growth) };
        return columns < 8.0f ? 8 : static_cast<std::size_t>(columns);
    }

    // UTF-8 글을 `columns` 칸씩 나눈다. 3바이트 이상 글자(U+0800 이상, 한글·한자·전각 등)는 두 칸이다. 경고 행의 사유와 원문(한 줄
    // JSON)을 끝을 숨기지 않고 여러 줄로 보인다. 줄 수가 스크롤 한계가 되므로 logic과 element가 같은 함수를 쓴다.
    [[nodiscard]] inline std::vector<std::u8string> chunk_function_editor_text(const std::u8string_view text, const std::size_t columns)
    {
        std::vector<std::u8string> lines {};
        std::u8string line {};
        std::size_t used { 0 };
        std::size_t index { 0 };
        while (index < text.size())
        {
            const char8_t lead { text[index] };
            std::size_t length { 1 };
            if ((lead & 0xe0) == 0xc0)
                length = 2;
            else if ((lead & 0xf0) == 0xe0)
                length = 3;
            else if ((lead & 0xf8) == 0xf0)
                length = 4;
            if (index + length > text.size())
                length = text.size() - index;
            // U+0800~U+10FF(일부 문자 체계)도 넓게 세어 넘치기보다 일찍 끊는다.
            const std::size_t width { length >= 3 ? std::size_t { 2 } : std::size_t { 1 } };
            if (used > 0 && used + width > columns)
            {
                lines.push_back(std::move(line));
                line.clear();
                used = 0;
            }
            line.append(text.substr(index, length));
            used += width;
            index += length;
        }
        if (line.empty() == false)
            lines.push_back(std::move(line));
        return lines;
    }

    // 경고 행의 폼 내용 높이다: 위 여백, 안내 두 줄, 사유 줄, 사이 6, 원문 줄, 아래 여백.
    [[nodiscard]] inline float function_editor_notice_content_height(const std::u8string_view problem, const std::u8string_view source, const float form_width)
    {
        const std::size_t columns { function_editor_notice_columns(form_width) };
        const std::size_t lines { 2 + chunk_function_editor_text(problem, columns).size() + chunk_function_editor_text(source, columns).size() };
        return 4.0f + static_cast<float>(lines) * layout_function_editor_notice_line_height() + 6.0f + 4.0f;
    }

    // 폼 자리의 내용 높이다. 고른 항목을 편집할 수 있으면 칸들이고, 경고 행이면 안내·사유·원문이다. 고른 것이 없으면 0이다.
    // `project_rows`는 `layout_function_editor_form`과 같다.
    [[nodiscard]] inline float function_editor_form_content_height(const bool editable, const std::size_t project_rows, const std::u8string_view warning_text, const std::u8string_view warning_source, const float dialog_width)
    {
        const float form_width { function_editor_form_width(dialog_width) };
        if (editable)
            return layout_function_editor_form(form_width, project_rows).content_height;
        if (warning_text.empty())
            return 0.0f;
        return function_editor_notice_content_height(warning_text, warning_source, form_width);
    }

    // 스크롤 한계다. 내용이 보이는 높이보다 낮으면 0이다.
    [[nodiscard]] inline float function_editor_maximum_scroll(const float content_height, const float viewport_height) noexcept
    {
        const float maximum { content_height - viewport_height };
        return maximum < 0.0f ? 0.0f : maximum;
    }
}
