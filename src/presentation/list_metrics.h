#pragma once

#include "luil/ui/caption_metrics.h"
#include "luil/ui/font_scale.h"
#include "luil/ui/layout_metrics.h"

#include <cstddef>
#include <span>

namespace gitman {
    // 논리 픽셀 단위의 layout 상수다. 실제 좌표는 DPI 배율을 곱한다.
    // custom caption이 client 영역 위쪽을 차지하므로 목록은 그 아래에서 시작한다.
    // luil의 caption 치수에서 직접 읽어 수동 동기화를 없앤다 (설계 L4). 두 헤더는
    // element 어휘가 없는 값 헤더라 logic이 include해도 된다.
    // 시스템 글자 크기가 논리 글자 크기 `size`를 키운 몫이다 (논리 픽셀, 100%면 0).
    // 글이 드는 칸의 높이는 100% 값에 이 몫을 더한다 (text-font-migration-design 7.3).
    // logic thread(스크롤 한계)와 UI thread(배치)가 같은 함수를 부른다 — luil 글자 크기 표는
    // 어느 thread에서나 읽을 수 있다.
    [[nodiscard]] inline float text_growth(const float size) noexcept
    {
        return luil::scaled_font_size(size) - size;
    }

    inline constexpr float layout_caption_height { static_cast<float>(luil::default_caption_ui_metrics.height) };
    inline constexpr float layout_toolbar_height { 40.0f };
    // notice 배너의 높이다. 배너가 보이면 목록이 그만큼 아래에서 시작한다.
    // 100%에서 22.0이고, 배너 label(12)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_notice_height() noexcept
    {
        return 22.0f + text_growth(12.0f);
    }
    // 100%에서 64.0이고, 제목(14)·경로(11)·상태 조각(10.5) 세 줄이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_card_height() noexcept
    {
        return 64.0f + text_growth(14.0f) + text_growth(11.0f) + text_growth(10.5f);
    }
    inline constexpr float layout_card_gap { 6.0f };
    inline constexpr float layout_margin { 10.0f };
    inline constexpr float layout_button_size { 28.0f };
    // 카드 텍스트가 시작하는 왼쪽 여백(상태 아이콘 자리)과, 버튼을 숨기기 전에
    // 지켜 주는 최소 텍스트 폭이다.
    inline constexpr float layout_card_text_left { 28.0f };
    inline constexpr float layout_card_minimum_text { 72.0f };

    // 카드 오른쪽 열의 기능 버튼 치수다 (function-buttons-design 2.3). 열은 `[card 0~3][⋯] │ [R][U][S]`이고,
    // 내장 세 버튼의 자리는 기능 유무와 상관없이 같다. 글 크기와 무관한 상수라 logic과 UI가 같은 값을 본다.
    //  - 슬롯은 버튼 28 + 간격 6이다. 가장 오른쪽 `card` 버튼이 `cw-180`이고 왼쪽으로 34씩 간다.
    //  - 구분선은 `cw-118 ~ cw-106`, `⋯`는 `cw-146`이다.
    //  - 글 예약 160 밑으로 내려가면 뒤쪽 `card` 버튼부터 `⋯`로 접는다. 상한 3에서는 최소 창(480)에서도
    //    접히지 않으므로 예약 상수가 바뀔 때의 안전장치다.
    inline constexpr float layout_card_function_slot { 34.0f };
    inline constexpr float layout_card_function_separator { 12.0f };
    inline constexpr float layout_card_function_text_reserve { 160.0f };
    // 선의 길이다. 버튼(28)보다 짧게 그려 묶음을 가르기만 한다 (시안 값).
    inline constexpr float layout_card_function_separator_height { 16.0f };
    // 실행 중 표식(채운 원)이다. 버튼 오른쪽 위 안쪽에 그린다 (2.3.5).
    inline constexpr float layout_function_marker_size { 6.0f };
    inline constexpr float layout_function_marker_inset { 2.0f };
    // 실패 표식(채운 마름모)이다 (2.3.5, D35). 대각선 8이면 넓이(32)가 지름 6 원(28)과 비슷해 한쪽이 작아 보이지 않는다.
    // 안쪽 여백을 1로 줄여 가운데가 실행 중 표식과 같다.
    inline constexpr float layout_function_failure_marker_size { 8.0f };
    inline constexpr float layout_function_failure_marker_inset { 1.0f };

    // 카드 기능 열의 자리다 (카드 왼쪽 가장자리 기준 논리 px). 카드와 test가 같은 함수를 쓴다.
    struct card_function_slots
    {
        // `⋯` 버튼의 x다. 적용 기능이 없으면 쓰지 않는다.
        float more_x { 0.0f };
        // 구분선 영역의 왼쪽 x다 (폭 `layout_card_function_separator`).
        float separator_x { 0.0f };
        // 접지 않고 둘 수 있는 `card` 버튼 수다. 요청 수 이하다.
        std::size_t visible_count { 0 };
        // 이름·경로·상태 줄이 쓸 수 있는 오른쪽 한계다 (가장 왼쪽 버튼 6px 앞).
        float text_right { 0.0f };
    };

    // `card_width` 폭 카드에 `card` 버튼 `requested`개(문서 순서로 그 카드에 적용되는 것)를 둘 때의 자리다.
    // `has_functions`가 거짓이면(적용 기능 0개) 지금 카드와 같아 글은 새로 고침 버튼 6px 앞까지다.
    [[nodiscard]] inline card_function_slots layout_card_function_slots(const float card_width, const std::size_t requested, const bool has_functions) noexcept
    {
        card_function_slots slots {};
        const float refresh_x { card_width - layout_margin - layout_button_size * 3.0f - layout_card_gap * 2.0f };
        slots.separator_x = refresh_x - layout_card_function_separator;
        slots.more_x = slots.separator_x - layout_button_size;
        if (has_functions == false)
        {
            slots.text_right = refresh_x - layout_card_gap;
            return slots;
        }
        // 가장 오른쪽 `card` 버튼은 `⋯` 앞 한 슬롯이다. 왼쪽으로 슬롯 하나씩 늘어난다.
        const auto text_right_for = [&](const std::size_t count) {
            const float leftmost { count == 0 ? slots.more_x : slots.more_x - layout_card_function_slot * static_cast<float>(count) };
            return leftmost - layout_card_gap;
        };
        std::size_t count { requested };
        while (count > 0 && text_right_for(count) - layout_card_text_left < layout_card_function_text_reserve)
            --count;
        slots.visible_count = count;
        slots.text_right = text_right_for(count);
        return slots;
    }

    // 보이는 `card` 버튼 `visible`개 중 `index`번째(왼쪽부터)의 x다.
    [[nodiscard]] inline float card_function_button_x(const card_function_slots& slots, const std::size_t index) noexcept
    {
        return slots.more_x - layout_card_function_slot * static_cast<float>(slots.visible_count - index);
    }
    // 선택 카드 기능 줄의 치수다 (function-buttons-design 2.2.4, 2.4). 줄은 로그 pane 160 안 맨 위에 서고 그만큼
    // 로그 본문이 준다 — `compute_list_layout`은 바꾸지 않아 목록 viewport가 그대로다. 줄 폭은 창 폭이다.
    //  - 줄 높이는 버튼 28 + 위아래 4다. 버튼 라벨(luil 글자 버튼의 본문 글 12)이 시스템 글자 크기만큼 자라면 줄도
    //    자라고 그만큼 로그 본문에서 가져간다.
    //  - 카드 이름은 창 폭 640 이상이면 150, 560~639면 72, 560 미만이면 없다. 이름 뒤 13(6 + 구분선 1 + 6)이다.
    //  - 버튼 폭은 `clamp(가장 긴 상태 글의 선호 폭, 64, 160)`, 간격 6이다. 버튼 영역 끝은 `W - 44`(`⋯` 28 + 앞
    //    간격 6 + 오른쪽 여백 10)이고, 넘치는 버튼은 오른쪽부터 줄의 `⋯`로 접는다.
    inline constexpr float layout_function_bar_label_size { 12.0f };
    // 버튼 위아래 여백이다. 버튼 높이는 줄 높이에서 이것을 두 번 뺀 값이라 큰 글자에서는 버튼도 함께 자란다.
    inline constexpr float layout_function_bar_inset { 4.0f };
    inline constexpr float layout_function_bar_name_wide { 150.0f };
    inline constexpr float layout_function_bar_name_narrow { 72.0f };
    inline constexpr float layout_function_bar_wide_from { 640.0f };
    inline constexpr float layout_function_bar_narrow_from { 560.0f };
    inline constexpr float layout_function_bar_name_gap { 6.0f };
    inline constexpr float layout_function_bar_button_minimum { 64.0f };
    inline constexpr float layout_function_bar_button_maximum { 160.0f };
    inline constexpr float layout_function_bar_trailing { layout_card_gap + layout_button_size + layout_margin };

    // 100%에서 36.0이다.
    [[nodiscard]] inline float layout_function_bar_height() noexcept
    {
        return 36.0f + text_growth(layout_function_bar_label_size);
    }

    // 기능 줄 버튼 하나의 폭이다. `preferred`는 가장 긴 상태 글(`라벨 · 실행 중`, 글리프 자리 포함)의 선호 폭이라
    // 상태가 바뀌어도 폭이 같고, 옆 버튼이 접혔다 펴지지 않는다.
    [[nodiscard]] inline float function_bar_button_width(const float preferred) noexcept
    {
        if (preferred < layout_function_bar_button_minimum)
            return layout_function_bar_button_minimum;
        return preferred > layout_function_bar_button_maximum ? layout_function_bar_button_maximum : preferred;
    }

    // 기능 줄의 가로 자리다 (줄 왼쪽 가장자리 기준 논리 px).
    struct function_bar_slots
    {
        // 카드 이름 칸의 폭이다. 0이면 이름이 없다.
        float name_width { 0.0f };
        // 이름 뒤 구분선의 가운데 x다. 이름이 없으면 쓰지 않는다.
        float separator_x { 0.0f };
        // 버튼(또는 힌트)이 쓰는 영역이다.
        float buttons_left { 0.0f };
        float buttons_right { 0.0f };
        // 줄의 `⋯` x다.
        float more_x { 0.0f };
    };

    // `width` 폭 줄의 자리다. `with_name`이 거짓이면(선택이 없다) 이름 칸이 없다.
    [[nodiscard]] inline function_bar_slots layout_function_bar_slots(const float width, const bool with_name) noexcept
    {
        function_bar_slots slots {};
        if (with_name && width >= layout_function_bar_wide_from)
            slots.name_width = layout_function_bar_name_wide;
        else if (with_name && width >= layout_function_bar_narrow_from)
            slots.name_width = layout_function_bar_name_narrow;
        slots.buttons_left = layout_margin;
        if (slots.name_width > 0.0f)
        {
            slots.separator_x = layout_margin + slots.name_width + layout_function_bar_name_gap;
            slots.buttons_left = slots.separator_x + 1.0f + layout_function_bar_name_gap;
        }
        slots.buttons_right = width - layout_function_bar_trailing;
        slots.more_x = width - layout_margin - layout_button_size;
        return slots;
    }

    // 왼쪽부터 버튼을 놓아 영역 안에 다 들어가는 수다. 넘치는 버튼과 그 뒤는 줄의 `⋯`로 접힌다 (오른쪽부터 접힘).
    [[nodiscard]] inline std::size_t function_bar_visible_count(const function_bar_slots& slots, const std::span<const float> widths) noexcept
    {
        float right { slots.buttons_left };
        for (std::size_t index = 0; index < widths.size(); ++index)
        {
            const float next { (index == 0 ? right : right + layout_card_gap) + widths[index] };
            if (next > slots.buttons_right)
                return index;
            right = next;
        }
        return widths.size();
    }

    // 카드 세 번째 줄(상태 조각)의 높이다.
    // 100%에서 16.0이고, 상태 조각 글(10.5)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_card_status_height() noexcept
    {
        return 16.0f + text_growth(10.5f);
    }
    // 상단 막대(도구 막대·배너) 아래에 드리우는 그림자의 높이와 진하기다. 스크롤된
    // 카드가 막대 아래로 지나갈 때만 나타난다.
    inline constexpr float layout_content_shadow_height { 8.0f };
    inline constexpr float layout_content_shadow_strength { 0.45f };
    // 스크롤 막대의 치수다. 보이는 막대 폭과 thumb 최소 높이는 luil 상수
    // (`scrollbar_visual_width`·`scrollbar_minimum_thumb`)를 막대가 직접 쓰고, 여기는
    // 잡기 쉽도록 클릭·끌기를 받는 hit 영역 `hit_width`와 창 오른쪽 가장자리의 크기
    // 조절 테두리와 겹치지 않도록 안쪽으로 들여놓는 `right_inset`만 둔다.
    inline constexpr float layout_scrollbar_hit_width { 16.0f };
    // 로그 헤더의 경과 시간(MM:SS) 표시에 예약하는 폭이다. 제목이 이 자리를
    // 침범하지 않는다.
    inline constexpr float layout_log_elapsed_width { 44.0f };
    inline constexpr float layout_scrollbar_right_inset { 6.0f };
    // 막대가 목록 위아래 끝에 붙지 않도록 두는 여백이다.
    inline constexpr float layout_scrollbar_vertical_inset { 6.0f };
    // 선택 카드 전용 하단 로그 pane의 치수다 (stage-7-plan 4.3).
    inline constexpr float layout_log_pane_height { 160.0f };
    // 100%에서 26.0이고, 제목 label(11.5)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_log_header_height() noexcept
    {
        return 26.0f + text_growth(11.5f);
    }
    // 100%에서 15.0이고, 본문 코드 글(10.5)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_log_line_height() noexcept
    {
        return 15.0f + text_growth(10.5f);
    }
    inline constexpr float layout_log_text_inset { 8.0f };
    // 시작 페이지의 치수다 (app-shell-design A1.3). 두 열은 창이 좁으면 세로로
    // 쌓인다.
    inline constexpr float layout_start_page_padding { 28.0f };
    // 100%에서 40.0이고, 제목(26)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_start_page_title_height() noexcept
    {
        return 40.0f + text_growth(26.0f);
    }
    // 100%에서 22.0이고, 설명(12)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_start_page_subtitle_height() noexcept
    {
        return 22.0f + text_growth(12.0f);
    }
    // 100%에서 26.0이고, 머리글(13)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_start_page_section_height() noexcept
    {
        return 26.0f + text_growth(13.0f);
    }
    // 100%에서 26.0이고, 행 글(13)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_start_page_row_height() noexcept
    {
        return 26.0f + text_growth(13.0f);
    }
    inline constexpr float layout_start_page_action_column_width { 240.0f };
    inline constexpr float layout_start_page_stack_width { 720.0f };

    // ─── dialog 여백 (미세 조정 지점) ────────────────────────────────────────
    //
    // 창이 낮아 dialog panel이 잘릴 때, panel이 클라이언트 위아래에 딱 붙지 않게
    // 남기는 최소 여백이다 (2026-08-23 지시). **여백을 바꾸려면 이 값 하나만
    // 고치면 된다** — 스크롤 한계와 배치가 모두 아래 두 함수를 거친다.
    inline constexpr float layout_dialog_edge_margin { 24.0f };
    // 창이 아주 낮으면 여백부터 줄인다 — panel이 사라지는 것보다 낫다. 창 높이의
    // 이 비율까지만 여백에 내준다.
    inline constexpr float layout_dialog_edge_margin_ratio { 0.12f };

    // 실제로 남길 한쪽 여백이다. 창이 넉넉하면 `layout_dialog_edge_margin`이다.
    [[nodiscard]] inline float dialog_edge_margin(const float window_height) noexcept
    {
        const float allowed { window_height * layout_dialog_edge_margin_ratio };
        return allowed < layout_dialog_edge_margin ? allowed : layout_dialog_edge_margin;
    }

    // dialog panel이 쓸 수 있는 세로 공간이다. 창 높이에서 위아래 여백을 뺀 값이며,
    // panel 높이를 자를 때와 스크롤 한계를 셀 때 **같은 함수를** 써야 휠이 헛돌거나
    // 마지막 항목이 잘리지 않는다.
    [[nodiscard]] inline float dialog_available_height(const float window_height) noexcept
    {
        const float available { window_height - dialog_edge_margin(window_height) * 2.0f };
        return available > 0.0f ? available : window_height;
    }

    // switch dialog의 치수다. 후보 목록의 스크롤 한계 계산을 logic과 element가
    // 같은 값으로 하도록 여기에 둔다 (stage-7-plan 4.5).
    inline constexpr float layout_switch_dialog_width { 400.0f };
    inline constexpr float layout_switch_dialog_height { 352.0f };
    // 100%에서 22.0이고, 후보 이름(11.5)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_switch_dialog_row_height() noexcept
    {
        return 22.0f + text_growth(11.5f);
    }
    inline constexpr float layout_switch_dialog_list_height { 190.0f };
    inline constexpr float layout_svn_browser_dialog_width { 640.0f };
    inline constexpr float layout_svn_browser_dialog_height { 462.0f };
    inline constexpr float layout_svn_browser_list_height { 300.0f };
    // 제목 아래 질의 막대다 (switch-dialog-query-bar-design Q1). 좌우 정사각 버튼과
    // 가운데 텍스트 박스가 한 줄을 이룬다.
    inline constexpr float layout_switch_dialog_query_top { 32.0f };
    inline constexpr float layout_switch_dialog_query_height { 26.0f };
    inline constexpr float layout_switch_dialog_query_gap { 6.0f };
    // 목록 영역이 시작하는 panel 위쪽부터의 거리다 (제목 34 + 막대 26 + 간격 6).
    inline constexpr float layout_switch_dialog_list_top { 66.0f };
    // panel 높이에서 목록이 아닌 부분(제목·질의 막대·메시지·버튼 줄)이 차지하는
    // 높이다. logic의 스크롤 한계와 element의 배치가 같은 값을 쓴다.
    inline constexpr float layout_switch_dialog_chrome_height { 162.0f };

    [[nodiscard]] inline float switch_dialog_list_height(const bool svn_browser, const float window_height) noexcept
    {
        const float dialog_height { svn_browser ? layout_svn_browser_dialog_height : layout_switch_dialog_height };
        const float preferred { svn_browser ? layout_svn_browser_list_height : layout_switch_dialog_list_height };
        // 창 위아래 여백을 뺀 나머지가 panel이 쓸 수 있는 높이다.
        const float available_height { dialog_available_height(window_height) };
        const float panel_height { available_height < dialog_height ? available_height : dialog_height };
        float available { panel_height - layout_switch_dialog_chrome_height };
        if (available < layout_switch_dialog_row_height())
            available = layout_switch_dialog_row_height();
        return available < preferred ? available : preferred;
    }
    // 로컬 변경 확인 dialog의 치수다 (field-feedback-design 2.3). 상단 목록과 하단
    // diff pane의 스크롤 한계를 logic과 element가 같은 값으로 계산하도록 여기에 둔다.
    inline constexpr float layout_local_changes_dialog_width { 560.0f };
    inline constexpr float layout_local_changes_dialog_height { 460.0f };
    // 100%에서 22.0이고, 경로(11.5)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_local_changes_row_height() noexcept
    {
        return 22.0f + text_growth(11.5f);
    }
    inline constexpr float layout_local_changes_list_height { 132.0f };
    inline constexpr float layout_local_changes_diff_height { 220.0f };
    // 100%에서 15.0이고, diff 코드 글(11)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_local_changes_diff_line_height() noexcept
    {
        return 15.0f + text_growth(11.0f);
    }

    // 탐색 후보 선택 등록 dialog의 치수다 (stage-8-plan 5.2). 목록 스크롤 한계를
    // logic과 element가 같은 값으로 계산하도록 여기에 둔다.
    inline constexpr float layout_discovery_dialog_width { 480.0f };
    inline constexpr float layout_discovery_dialog_height { 360.0f };
    // 100%에서 22.0이고, 후보 이름(11.5)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_discovery_dialog_row_height() noexcept
    {
        return 22.0f + text_growth(11.5f);
    }
    inline constexpr float layout_discovery_dialog_list_height { 224.0f };

    // 글꼴 선택 dialog의 치수다 (fonts-and-in-app-dialogs-design F2.5). 목록·미리
    // 보기의 스크롤 한계를 logic과 element가 같은 값으로 계산하도록 여기에 둔다.
    inline constexpr float layout_font_dialog_width { 460.0f };
    inline constexpr float layout_font_dialog_height { 428.0f };
    // 100%에서 24.0이고, 글꼴 이름(12.5)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_font_dialog_row_height() noexcept
    {
        return 24.0f + text_growth(12.5f);
    }
    inline constexpr float layout_font_dialog_list_height { 214.0f };
    inline constexpr float layout_font_dialog_preview_height { 48.0f };

    // 새 문서 만들기 dialog의 치수다 (fonts-and-in-app-dialogs-design F3).
    inline constexpr float layout_generate_dialog_width { 520.0f };
    inline constexpr float layout_generate_dialog_height { 300.0f };
    // 100%에서 30.0이고, 칸 글(11)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_generate_dialog_row_height() noexcept
    {
        return 30.0f + text_growth(11.0f);
    }
    inline constexpr float layout_generate_dialog_label_width { 76.0f };

    // 기능 실행 확인 dialog의 치수다 (function-buttons-design 3.10). 목록 스크롤 한계를 logic과
    // element가 같은 값으로 계산하도록 여기에 둔다. 항목 높이는 줄 수로만 정해진다 — 긴 줄은 logic이
    // 칸 수(`function_confirm_code_columns`)로 미리 나누므로 글 폭을 재지 않아도 된다.
    inline constexpr float layout_function_confirm_dialog_width { 600.0f };
    inline constexpr float layout_function_confirm_dialog_height { 480.0f };
    inline constexpr float layout_function_confirm_padding { 16.0f };
    inline constexpr float layout_function_confirm_title_height { 26.0f };
    inline constexpr float layout_function_confirm_button_height { 28.0f };
    inline constexpr float layout_function_confirm_item_padding { 6.0f };
    inline constexpr float layout_function_confirm_item_gap { 6.0f };
    // 안내·경고 줄이다. 100%에서 16.0이고 글(11)이 시스템 글자 크기만큼 자란다.
    [[nodiscard]] inline float layout_function_confirm_text_line_height() noexcept
    {
        return 16.0f + text_growth(11.0f);
    }
    // 항목의 코드 줄이다. 100%에서 15.0이고 코드 글(10.5)이 자란다.
    [[nodiscard]] inline float layout_function_confirm_code_line_height() noexcept
    {
        return 15.0f + text_growth(10.5f);
    }
    // 항목 머리(라벨과 실행 방식 배지)다. 100%에서 20.0이고 라벨(12)이 자란다.
    [[nodiscard]] inline float layout_function_confirm_item_header_height() noexcept
    {
        return 20.0f + text_growth(12.0f);
    }
    [[nodiscard]] inline float layout_function_confirm_item_height(const std::size_t line_count) noexcept
    {
        return layout_function_confirm_item_padding * 2.0f + layout_function_confirm_item_header_height() + static_cast<float>(line_count) * layout_function_confirm_code_line_height();
    }
    // panel 위쪽부터 목록이 시작하는 거리다. 제목 아래 안내 줄 수만큼 내려간다.
    [[nodiscard]] inline float function_confirm_list_top(const std::size_t description_lines) noexcept
    {
        return layout_function_confirm_padding + layout_function_confirm_title_height + static_cast<float>(description_lines) * layout_function_confirm_text_line_height() + 8.0f;
    }
    // 목록 아래(경고 줄과 버튼 줄)가 차지하는 높이다.
    [[nodiscard]] inline float function_confirm_list_bottom(const std::size_t note_lines) noexcept
    {
        const float note { note_lines > 0 ? 8.0f + static_cast<float>(note_lines) * layout_function_confirm_text_line_height() : 0.0f };
        return note + 12.0f + layout_function_confirm_button_height + layout_function_confirm_padding;
    }
    // panel 높이다. 창이 낮으면 위아래 여백을 남기고 줄인다 (`dialog_available_height`).
    [[nodiscard]] inline float function_confirm_dialog_height(const float window_height) noexcept
    {
        const float available { dialog_available_height(window_height) };
        return available < layout_function_confirm_dialog_height ? available : layout_function_confirm_dialog_height;
    }
    // 목록 항목이 없는 확인(저장하지 못한 변경만 묻는 것, function-buttons-design 4.10·D28)의 panel 높이다. 480 높이를
    // 그대로 두면 제목·안내 몇 줄과 버튼 사이가 빈 목록 자리로 크게 빈다. 그래서 목록 자리 없이 위아래 글과 버튼 줄만
    // 담는다. 창이 낮으면 줄이는 것은 같다.
    [[nodiscard]] inline float function_confirm_compact_dialog_height(const float window_height, const std::size_t description_lines, const std::size_t note_lines) noexcept
    {
        const float wanted { function_confirm_list_top(description_lines) + function_confirm_list_bottom(note_lines) };
        const float available { dialog_available_height(window_height) };
        return available < wanted ? available : wanted;
    }
    // 목록 영역의 높이다. 창이 아주 낮아도 코드 한 줄 높이는 남긴다.
    [[nodiscard]] inline float function_confirm_list_height(const float window_height, const std::size_t description_lines, const std::size_t note_lines) noexcept
    {
        const float available { function_confirm_dialog_height(window_height) - function_confirm_list_top(description_lines) - function_confirm_list_bottom(note_lines) };
        return available < layout_function_confirm_code_line_height() ? layout_function_confirm_code_line_height() : available;
    }
    // 항목 안쪽 좌우 여백이다.
    inline constexpr float layout_function_confirm_item_inset { 10.0f };
    // 이어지는 줄의 들여쓰기다 (코드 글자 칸 수).
    inline constexpr std::size_t function_confirm_continuation_indent { 2 };
    // 코드 줄 하나에 담을 글자 칸 수다. 확인 dialog는 보이는 글이 곧 확인할 내용이라 긴 인자의 뒤를
    // 말줄임으로 숨기지 않고 logic이 이 칸 수로 줄을 나눈다 (T7). 항목 높이가 줄 수로만 정해져야
    // logic이 글 폭을 재지 않고 스크롤 한계를 셀 수 있기 때문이다.
    //  - 폭은 dialog 폭(창에 맞춰 줄인 값)에서 panel 여백, 항목 여백, 스크롤 막대, 이어지는 줄의
    //    들여쓰기를 뺀 값이다.
    //  - 글자 폭은 코드 글꼴(고정폭) 크기의 0.62배로 어림한다. Consolas(0.55)·Cascadia Mono(0.6)보다
    //    넓게 잡아 줄이 넘치지 않는 쪽으로 틀린다. ASCII 밖의 글자는 두 칸으로 센다.
    [[nodiscard]] inline std::size_t function_confirm_code_columns(const float window_width) noexcept
    {
        const float dialog_width { window_width < layout_function_confirm_dialog_width ? window_width : layout_function_confirm_dialog_width };
        const float glyph_width { luil::scaled_font_size(10.5f) * 0.62f };
        const float text_width { dialog_width - layout_function_confirm_padding * 2.0f - layout_function_confirm_item_inset * 2.0f - layout_scrollbar_hit_width };
        const float columns { text_width / glyph_width - static_cast<float>(function_confirm_continuation_indent) };
        constexpr std::size_t minimum_columns { 8 };
        return columns > static_cast<float>(minimum_columns) ? static_cast<std::size_t>(columns) : minimum_columns;
    }

    // 카드 컨텍스트 메뉴 popup의 너비다 (field-feedback-design 3장). 행 높이·여백은
    // luil `menu_element`의 것(`menu_item_height`·`menu_padding`)을 쓴다.
    inline constexpr float layout_context_menu_width { 190.0f };
    // 기능 이름과 상태 라벨(`빌드 — 실행 중 00:12`), 단축키 열이 드는 메뉴는 글 폭만큼 넓히되 이 폭에서
    // 멈추고 넘치는 라벨은 말줄임에 맡긴다 (function-buttons-design 2.3.2). 190이 하한이다.
    inline constexpr float layout_context_menu_maximum_width { 320.0f };

    // 메뉴가 통째로 들어가야 하는 가장 작은 작업 영역의 높이(논리 px)다 (function-buttons-design D34). 1366×768을 125%로
    // 쓰고 작업 표시줄이 48(Windows 11 기본)이면 768 / 1.25 − 48 = 566이다. luil 메뉴는 스크롤이 없어 이보다 긴 메뉴는
    // 아래가 화면 밖으로 잘린다. 메뉴 높이는 `8 + 26 × 행 + 9 × 구분선`이고 배율은 이 높이와 작업 영역에 같이 곱해지므로
    // 논리 px끼리 견준다.
    inline constexpr float layout_menu_fit_height { 566.0f };
    // 기능 메뉴 한 쪽이 싣는 기능 수다 (2.3.2, D34). 넘치면 `기능 더 보기…`가 다음 쪽을 연다. 한 쪽은 기능 N + 기능 더
    // 보기 + 기능 편집 + 문서 다시 읽기, 구분선 1이라 `95 + 26N`이다. 12면 407이다.
    inline constexpr std::size_t function_menu_page_size { 12 };
    // 카드 메뉴 기능 절이 싣는 기능 수다 (2.3.3, D34). 넘치면 `기능 더 보기…`가 9번째 기능부터 기능 메뉴로 연다. 카드
    // 메뉴는 기존 6 + 기능 N + 기능 더 보기 + 기능 편집, 구분선 1이라 `225 + 26N`이다. 8이면 433으로 T9의 높이 그대로다.
    // 기능 메뉴 쪽 크기(12)를 같이 쓰면 537이 되어 S2 전에 통째로 들던 작업 영역(1280×720·125%의 528, 1920×1080·200%의
    // 492)에서 아래가 새로 잘리므로 따로 둔다. 두 메뉴 모두 1366×768·150%(464)에도 든다.
    inline constexpr std::size_t card_menu_function_count { 8 };

    // luil `menu_element`의 높이 식(`8 + 26 × 행 + 9 × 구분선`)으로 가장 긴 두 메뉴가 작업 영역 한계 안인지 빌드가 본다.
    // 쪽 크기를 늘릴 때 메뉴가 잘리는 것을 test보다 먼저 막는다. 실제 popup 높이는 test가 `build_context_menu_popup`으로 잰다.
    static_assert(8.0f + 26.0f * static_cast<float>(6 + card_menu_function_count + 2) + 9.0f <= layout_menu_fit_height);
    static_assert(8.0f + 26.0f * static_cast<float>(function_menu_page_size + 3) + 9.0f <= layout_menu_fit_height);

    // 카드 목록이 차지하는 영역이다. tree 빌드, 카드 목록 element와 logic의 스크롤
    // 한계 계산이 모두 이 함수를 거쳐 같은 값을 쓴다.
    struct list_layout
    {
        // 목록 slot의 위쪽 좌표다 (caption + toolbar + 보이는 notice 배너).
        float content_top { 0.0f };
        // 목록 slot의 높이다. 창이 작으면 0이다.
        float viewport_height { 0.0f };
        // 하단 로그 pane의 영역이다. pane이 없으면 높이가 0이다. 창이 작으면 로그가
        // 먼저 줄어들어 목록이 완전히 사라지지 않는다.
        float log_top { 0.0f };
        float log_height { 0.0f };
    };

    // `window_height`와 결과는 같은 단위다. 논리 픽셀로 물으면 논리 픽셀로 답한다.
    [[nodiscard]] list_layout compute_list_layout(float window_height, float scale, bool has_notice, bool has_log_pane = false) noexcept;

    [[nodiscard]] float card_list_content_height(std::size_t card_count, float scale) noexcept;

    // 목록 content 좌표에서 `card_index` 카드가 차지하는 위쪽 좌표다.
    [[nodiscard]] float card_content_top(std::size_t card_index, float scale) noexcept;

    // 카드 하나가 화면 안에 완전히 들어오도록 조정한 스크롤 값이다. 이미 보이면
    // 현재 값을 그대로 돌려준다.
    [[nodiscard]] float scroll_offset_showing_card(float offset, std::size_t card_index, std::size_t card_count, float viewport_height, float scale) noexcept;

    // 스크롤을 [0, 최대]로 고정한다. 내용이 화면보다 작으면 0이다. 식은 luil의
    // `clamp_scroll`(인자 순서가 다르다)이라 흘리는 컨테이너·막대와 같은 값을 본다.
    [[nodiscard]] constexpr float clamp_scroll_offset(const float offset, const float content_height, const float viewport_height) noexcept
    {
        return luil::clamp_scroll(content_height, viewport_height, offset);
    }

    // 카드 drag 중의 삽입 위치다. `dragged_index`를 뺀 나머지 목록 기준의 위치
    // [0, card_count - 1]을 돌려주며, 경계는 각 카드의 세로 중앙이다(위 절반이면 그
    // 카드 앞, 아래 절반이면 뒤). 여백을 벌려 그리는 쪽과 drop 대상 계산이 이 한
    // 함수를 같이 쓰므로 벌어진 자리와 실제 놓이는 자리가 어긋나지 않는다.
    [[nodiscard]] std::size_t card_drag_insertion_slot(float pointer_y, float list_top, float scroll, std::size_t dragged_index, std::size_t card_count, float scale) noexcept;

    // drag 중 `card_index` 카드가 원래 배치에서 세로로 옮겨 그려질 거리다 (물리
    // 픽셀). dragged 카드가 빠진 자리는 닫히고 `insertion_slot` 자리는 카드 한 장
    // 크기로 벌어진다. dragged 카드 자신은 0이다(목록에서는 그리지 않는다).
    [[nodiscard]] float card_drag_offset(std::size_t card_index, std::size_t dragged_index, std::size_t insertion_slot, float scale) noexcept;
} // namespace gitman
