#include "presentation/ui/card_element.h"

#include "presentation/function_presentation.h"
#include "presentation/list_metrics.h"
#include "presentation/log_presentation.h"
#include "presentation/ui/element_kinds.h"
#include "presentation/ui/function_icon_catalog.h"
#include "presentation/ui/function_marker_element.h"
#include "presentation/ui/status_glyphs.h"
#include "presentation/ui/ui_actions.h"

#include "luil/ui/button_element.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkTypeface.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gitman::ui {
    namespace {
        // 상태 줄의 조각 하나다. 배경이 0이면 배경 없이 글자만 그린다.
        struct card_chip
        {
            char32_t glyph { 0 };
            std::u8string text {};
            ui_color background { 0 };
            ui_color foreground { 0 };
        };

        ui_color state_accent(const ui_color_palette& palette, const card_view_state state) noexcept
        {
            switch (state)
            {
            case card_view_state::failed:
                return palette.error_accent;
            case card_view_state::warning:
                return palette.warning_accent;
            case card_view_state::ready:
                return palette.accent;
            case card_view_state::loading:
            case card_view_state::running:
            case card_view_state::disabled:
                break;
            }
            return palette.primary_foreground;
        }

        // 막힌 업데이트·전환의 사유다 (S4 검토). S4부터 막힌 버튼이 접근 tree에 남아 이 글이 HelpText로 읽히므로 실제 사유를
        // 고른다. 순서는 logic `change_allowed`의 조건을 view에 있는 칸으로 되짚은 것이다: 프로젝트 비활성, 변경 작업 실행 중
        // (업데이트 버튼은 그때 취소 버튼이라 전환만 닿는다), 출력 수집 기능 실행 중, 조회 중(`busy`: 처음 조회·새로 고침),
        // 로컬 조회 전. 나머지는 저장소를 쓸 수 없을 때다 — `can_change`가 거짓이고 앞의 어느 것도 아니면
        // `availability`가 준비가 아니다.
        std::u8string change_blocked_reason(const card_view_model& card, const bool update)
        {
            if (card.enabled && card.change_running == false && card.function_running)
                return function_running_reason(card.running_function_label);

            // 기다리면 풀리는 사유는 `…한 뒤 업데이트할 수 있습니다`, 기다려도 풀리지 않는 사유는 `…라 업데이트할 수 없습니다`다.
            std::u8string_view lead { u8"저장소를 쓸 수 없는 상태라 " };
            bool waits { false };
            if (card.enabled == false)
                lead = u8"비활성 프로젝트라 ";
            else if (card.change_running)
            {
                lead = u8"실행 중인 변경 작업이 끝난 뒤 ";
                waits = true;
            }
            else if (card.busy)
            {
                lead = u8"카드 상태 조회가 끝난 뒤 ";
                waits = true;
            }
            else if (card.state == card_view_state::loading)
            {
                lead = u8"로컬 상태를 조회한 뒤 ";
                waits = true;
            }

            std::u8string text { lead };
            text += update ? std::u8string_view { u8"업데이트" } : std::u8string_view { u8"전환" };
            text += waits ? std::u8string_view { u8"할 수 있습니다" } : std::u8string_view { u8"할 수 없습니다" };
            return text;
        }
    } // namespace

    card_element::card_element(card_view_model card)
        : ui_element { ui_element_id { kind_card_body, card.id.value } }
        , card_ { std::move(card) }
    {
        // 제외된 카드도 선택은 가능해야 상태를 확인할 수 있으므로 body는 항상
        // 활성이다. 흐림 표시는 draw가 card_.enabled로 판정한다.
        set_action(ui_trigger::left_click, make_logic_click(select_card_intent { { card_.id } }));

        // 카드 body는 Tab 자리가 아니다 (설계 D1-B). 카드 사이 이동은 policy의
        // ↑/↓이고, Tab은 카드 버튼 3개만 지난다. 자리로 두면 클릭 뒤 Enter가
        // 이 선택 액션을 다시 실행해 "초점 카드 새로 고침"과 어긋난다.
        set_tab_stop(false);

        // 더블 클릭은 로컬 변경 확인 dialog다 (field-feedback-design 2.3). 카드 3번째
        // 줄 chip은 element가 아니라 hit 대상이 될 수 없어 카드 수준 진입점을 쓴다.
        set_action(ui_trigger::double_click, make_logic_click(open_local_changes_intent { card_.id }));

        // 우클릭은 컨텍스트 메뉴다 (field-feedback-design 3장). 클릭 지점이 메뉴의
        // 앵커가 되고 logic이 카드를 선택한 뒤 연다.
        set_action(ui_trigger::right_click,
            [id = card_.id](const ui_action_context& context) -> std::vector<input_action> { return { make_logic_action(open_context_menu_intent { id, context.x, context.y }) }; });

        // 카드 body는 순서 변경의 drag 출발지다. drop 대상은 카드가 아니라 목록
        // 전체다 (card_list_element, field-feedback-design 4.1). 제외된 카드도 문서
        // 순서는 옮길 수 있다. lambda는 element가 소유하고 tree는 게시 후 불변이라
        // this 캡처가 안전하다.
        drag_source source {};
        source.make_payload = [this](const ui_action_context& context) {
            // 들린 카드는 목록이 직접 따라 그리고(custom_visual) 놓일 자리는 삽입
            // 틈이 보이므로 luil의 기본 ghost도 drop 대상 강조도 끈다 (설계 L3).
            return drag_payload {
                .source = id(),
                .dragged_owner = card_.id.value,
                .label = card_.display_name,
                .custom_visual = true,
                .grab_offset_x = context.x - bounds().x,
                .grab_offset_y = context.y - bounds().y,
                .suppress_drop_highlight = true
            };
        };
        set_drag_source(std::move(source));

        // 기능 열이다 (function-buttons-design 2.3). `card` 버튼은 Tab 자리가 아니라 생성 순서가 Tab 순서에
        // 영향이 없고, `⋯`가 새로 고침 앞 자리라 Tab은 `⋯`, 새로 고침, 업데이트, 전환 순서다 (2.3.4).
        for (std::size_t index = 0; index < card_.functions.size(); ++index)
        {
            const card_function_view& function { card_.functions[index] };
            if (function.placement != function_placement::card)
                continue;

            luil::button_config config {};
            // 실행 중이면 제자리에서 중지 버튼이 된다. 켜진 토글의 강조 바탕(active)으로 보인다 (2.3.5).
            config.glyph = function.running ? codicons::icon_stop_circle : function_icon_glyph(function.icon);
            config.active = function.running;
            auto button { std::make_unique<luil::button_element>(ui_element_id { kind_card_function, card_function_owner(card_.id.value, function.button_id) }, config) };
            // 키보드는 `Alt+1..3`, `Alt+↓`, `Shift+F10`으로 닿는다. 카드마다 정지점이 늘지 않게 한다 (2.3.4).
            button->set_tab_stop(false);
            button->set_tooltip(function_button_tooltip(function));
            // 경과 시간이 없는 고정 이름이다. 초마다 바뀌면 낭독이 되풀이된다 (2.3.4).
            button->set_access_name(function_button_access_name(function, card_.display_name));
            // 비활성 버튼에도 액션을 단다. luil은 비활성 element의 액션을 실행하지 않고, 액션이 있어야
            // 접근 tree에 단추로 서서 비활성 기능의 이름도 읽힌다. logic이 누른 시점에 다시 판정한다.
            if (function.running)
                button->set_action(ui_trigger::left_click, make_logic_click(stop_function_intent { card_.id, function.button_id }));
            else
                button->set_action(ui_trigger::left_click, make_logic_click(run_function_intent { card_.id, function.button_id }));
            // 우클릭은 배치 메뉴다 (2.2.5). luil은 우클릭을 hit 요소에서만 실행하므로 카드 body의 카드 메뉴와 섞이지
            // 않는다. 비활성 버튼은 받지 않는다. `card` 버튼은 Tab 자리가 아니라 `Shift+F10`은 카드 메뉴로 간다.
            button->set_action(ui_trigger::right_click
                , [id = card_.id, button_id = function.button_id](const ui_action_context& context) -> std::vector<input_action>
                {
                    return { make_logic_action(open_function_placement_menu_intent { id, button_id, false, context.x, context.y }) };
                }
            );
            if (function.enabled == false)
                button->set_enabled(false);
            function_buttons_.push_back({ button.get(), nullptr, index });
            add_child(std::move(button));
        }
        if (card_.functions.empty() == false)
        {
            auto more { std::make_unique<luil::button_element>(ui_element_id { kind_card_function_more, card_.id.value }, luil::button_config { .glyph = codicons::icon_ellipsis }) };
            // 상태 수는 접힘이 정해지는 배치 때 붙인다 (`arrange`).
            more->set_tooltip(function_more_tooltip({}));
            more->set_access_name(function_more_access_name(card_.display_name, card_.functions.size()));
            // 메뉴는 `⋯`의 왼쪽 아래에 연다. 클릭 액션은 누른 점만 주므로 버튼 자신의 자리를 읽는다.
            // lambda는 element가 소유하고 tree는 게시 후 불변이라 포인터 캡처가 안전하다 (drag 출발과 같다).
            const ui_element* const anchor { more.get() };
            more->set_action(ui_trigger::left_click
                , [id = card_.id, anchor](const ui_action_context&) -> std::vector<input_action>
                {
                    const rect_f box { anchor->bounds() };
                    return { make_logic_action(open_function_menu_intent { id, box.x, box.y + box.height }) };
                }
            );
            more_ = more.get();
            add_child(std::move(more));
        }

        auto refresh { std::make_unique<luil::button_element>(ui_element_id { kind_card_refresh, card_.id.value }, luil::button_config { .glyph = codicons::icon_refresh }) };
        refresh->set_tooltip(u8"이 카드 새로 고침");
        refresh->set_action(ui_trigger::left_click, make_logic_click(refresh_card_intent { card_.id }));
        refresh_ = refresh.get();
        add_child(std::move(refresh));

        // update 버튼: 실행 중에는 그 작업의 중지 버튼이 된다 (stage-7-plan 4.4).
        luil::button_config update_config {};
        update_config.glyph = card_.change_running ? codicons::icon_stop_circle : codicons::icon_repo_pull;
        auto update { std::make_unique<luil::button_element>(ui_element_id { kind_card_update, card_.id.value }, update_config) };
        const std::u8string update_name { card_.kind == repository_kind::subversion ? u8"업데이트 (svn update)" : u8"업데이트 (git pull --ff-only)" };
        if (card_.change_running)
        {
            update->set_tooltip(u8"실행 중인 작업 취소");
            update->set_action(ui_trigger::left_click, make_logic_click(cancel_operation_intent { card_.id }));
        }
        else
        {
            // 확인 overlay 없이 곧바로 실행한다 (2026-08-20 검수). submodule 갱신
            // 여부는 문서 settings가 정하고 logic이 채운다.
            update->set_tooltip(update_name);
            // 막힌 버튼에도 같은 액션을 단다 (function-buttons-design S4). luil은 비활성 element의 액션을 실행하지 않고, 액션이
            // 있어야 접근 tree에 단추로 서서 무엇이 왜 막혔는지가 읽힌다. 이름은 누를 수 있을 때와 같게 고정하고 사유는 tooltip
            // (UIA HelpText)이 말한다. busy와 출력 수집 기능 실행은 logic도 누른 시점에 다시 막는다 (`begin_change`).
            update->set_action(ui_trigger::left_click, make_logic_click(request_update_intent { card_.id, {} }));
            update->set_access_name(update_name);
            if (card_.can_change == false)
            {
                // 같은 카드의 출력 수집 기능과 서로 막는다 (4.7, D6-A). 그때 사유는 그 기능 이름이다.
                update->set_tooltip(change_blocked_reason(card_, true));
                update->set_enabled(false);
            }
        }
        update_ = update.get();
        add_child(std::move(update));

        auto switch_button { std::make_unique<luil::button_element>(ui_element_id { kind_card_switch, card_.id.value }, luil::button_config { .glyph = codicons::icon_source_control }) };
        const std::u8string switch_name { card_.kind == repository_kind::subversion ? u8"URL 전환 (svn switch)" : u8"브랜치 전환 (git switch)" };
        // update와 같이 막힌 버튼에도 액션과 고정 이름을 단다 (S4). switch dialog를 연 뒤의 실행은 busy와 출력 수집 기능
        // 실행이면 `begin_change`가 다시 막는다.
        switch_button->set_tooltip(switch_name);
        switch_button->set_access_name(switch_name);
        switch_button->set_action(ui_trigger::left_click, make_logic_click(begin_switch_intent { card_.id }));
        if (card_.can_change == false)
        {
            switch_button->set_tooltip(change_blocked_reason(card_, false));
            switch_button->set_enabled(false);
        }
        switch_ = switch_button.get();
        add_child(std::move(switch_button));

        // 표식은 버튼 위에 그려야 하므로 모든 버튼 뒤에 붙인다 (2.3.5). `card` 버튼의 표식은 실패(마름모)했고
        // 실행 중이 아닐 때다 — 실행 중이면 버튼 자신이 중지 글리프로 바뀐다.
        for (function_button_slot& slot : function_buttons_)
        {
            const card_function_view& function { card_.functions[slot.function] };
            if (function.failed == false || function.running)
                continue;
            auto marker { std::make_unique<function_marker_element>(ui_element_id { kind_card_function_marker, card_function_owner(card_.id.value, function.button_id) }, function_marker_shape::failed) };
            slot.marker = marker.get();
            add_child(std::move(marker));
        }
        // `⋯`의 표식은 버튼이 아닌 자리(`bar`·`menu`)나 접힌 `card` 기능의 것이다. 접힘은 배치 때 정해지므로
        // 실행 중·실패인 기능이 하나라도 있으면 만들어 두고, 배치가 보일지와 모양(원·마름모)을 정한다.
        const bool any_state {
            std::ranges::any_of(card_.functions
                , [](const card_function_view& function)
                {
                    return function.running || function.failed;
                }
            )
        };

        if (more_ != nullptr && any_state)
        {
            auto marker { std::make_unique<function_marker_element>(ui_element_id { kind_card_function_more_marker, card_.id.value }, function_marker_shape::failed) };
            more_marker_ = marker.get();
            add_child(std::move(marker));
        }
    }

    void card_element::arrange(const arrange_context& context)
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        set_bounds(context.slot);

        // 카드 오른쪽 끝에 버튼 3개를 세로 중앙 정렬로 둔다. 오른쪽(전환)부터 왼쪽으로 놓으므로 창이
        // 좁아 글자가 들어갈 최소 폭도 남지 않으면 **왼쪽** 버튼(새로 고침)부터 숨는다.
        const float margin { layout_margin * scale };
        const float gap { layout_card_gap * scale };
        const float button { layout_button_size * scale };
        const float buttons_y { context.slot.y + (context.slot.height - button) / 2.0f };
        const float minimum_right { context.slot.x + (layout_card_text_left + layout_card_minimum_text) * scale };

        float next_x { context.slot.x + context.slot.width - margin - button };
        const auto place = [&](ui_element* const element) {
            if (next_x < minimum_right)
            {
                element->set_visible(false);
                return;
            }
            element->set_visible(true);
            element->arrange(context.for_child({ next_x, buttons_y, button, button }));
            next_x -= button + gap;
        };

        place(switch_);
        place(update_);
        place(refresh_);
        text_limit_ = next_x + button;
        separator_x_.reset();
        if (more_ == nullptr)
            return;

        // 기능 열이다 (function-buttons-design 2.3). 자리는 카드 폭만으로 정해진다 — 칩·제목·경로 길이를
        // 보지 않는다 (2.3.1 규칙 4). `⋯`는 내장 버튼과 같은 바닥 규칙을 따른다 (규칙 5).
        const card_function_slots slots { layout_card_function_slots(context.slot.width / scale, function_buttons_.size(), true) };
        const float more_x { context.slot.x + slots.more_x * scale };
        const bool more_visible { more_x >= minimum_right };
        more_->set_visible(more_visible);
        if (more_visible)
        {
            more_->arrange(context.for_child({ more_x, buttons_y, button, button }));
            separator_x_ = context.slot.x + (slots.separator_x + layout_card_function_separator / 2.0f) * scale;
            text_limit_ = context.slot.x + slots.text_right * scale;
        }

        // 글 예약을 지키지 못하면 뒤쪽 `card` 버튼부터 접는다. 접힌 기능은 `⋯` 메뉴에 그대로 있고
        // 그 실행 중·실패는 `⋯`의 표식이 맡는다.
        const std::size_t visible_count { more_visible ? slots.visible_count : 0 };
        std::vector<bool> on_card(card_.functions.size(), false);
        for (std::size_t index = 0; index < function_buttons_.size(); ++index)
        {
            const function_button_slot& slot { function_buttons_[index] };
            const bool visible { index < visible_count };
            slot.button->set_visible(visible);
            if (slot.marker != nullptr)
                slot.marker->set_visible(visible);
            if (visible == false)
                continue;
            on_card[slot.function] = true;
            const rect_f box { context.slot.x + card_function_button_x(slots, index) * scale, buttons_y, button, button };
            slot.button->arrange(context.for_child(box));
            if (slot.marker != nullptr)
                slot.marker->arrange(context.for_child(function_marker_slot(box, scale, function_marker_shape::failed)));
        }

        // `⋯`의 접근 이름과 tooltip에 표식이 맡는 기능의 실행 중·실패 수를 붙인다 (2.3.4, D35). 접힘이 정해진 뒤라야 어느
        // 기능을 표식이 맡는지 안다. tree는 게시 전이라 여기서 고쳐도 된다.
        const function_more_status status { function_more_status_of(card_.functions, on_card) };
        more_->set_access_name(function_more_access_name(card_.display_name, card_.functions.size(), status));
        more_->set_tooltip(function_more_tooltip(status));
        if (more_marker_ == nullptr)
            return;

        // 실행 중임은 상태 아이콘 회전과 3번째 줄 칩으로도 보이지만 실패는 이 표식뿐이라 실패를 먼저 보인다. 모양으로도
        // 가른다: 실행 중은 원, 실패는 마름모다 (D35).
        const function_more_marker state { function_more_marker_state(status) };
        const bool marker_visible { more_visible && state != function_more_marker::none };
        more_marker_->set_visible(marker_visible);
        const function_marker_shape shape { state == function_more_marker::failed ? function_marker_shape::failed : function_marker_shape::running };
        more_marker_->set_shape(shape);
        if (marker_visible)
            more_marker_->arrange(context.for_child(function_marker_slot(more_->bounds(), scale, shape)));
    }

    void card_element::draw(draw_context& context, const interaction_snapshot& interaction) const
    {
        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const rect_f box { bounds() };
        const SkRect body { SkRect::MakeXYWH(box.x, box.y, box.width, box.height) };

        SkPaint card_paint { solid_paint(context.palette.surface_background) };
        if (card_.enabled == false)
            card_paint.setAlphaf(0.5f);
        context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), card_paint);
        if (card_.selected)
        {
            SkPaint border { solid_paint(context.palette.accent) };
            border.setStyle(SkPaint::kStroke_Style);
            border.setStrokeWidth(1.0f * scale);
            context.canvas.drawRRect(SkRRect::MakeRectXY(body, 3.0f * scale, 3.0f * scale), border);
        }

        const SkPaint foreground { solid_paint(context.palette.primary_foreground) };
        SkPaint dim_foreground { solid_paint(context.palette.primary_foreground) };
        dim_foreground.setAlphaf(0.6f);

        // 세 줄의 상자다. 100%에서 기준선이 예전 손 계산(위 8 + 11, 그 아래 16)과 같은
        // 자리에 서도록 잡았고, 줄마다 그 글이 시스템 글자 크기로 자란 만큼 커진다.
        // 합은 `layout_card_height()`와 같다.
        const float padding { 8.0f * scale };
        const float text_x { box.x + layout_card_text_left * scale };
        const float title_top { box.y + 4.0f * scale };
        const float title_height { (19.0f + text_growth(14.0f)) * scale };
        const float path_top { title_top + title_height };
        const float path_height { (15.0f + text_growth(11.0f)) * scale };
        const float line_3_top { path_top + path_height + 2.0f * scale };

        // 상태 아이콘: 진행 중이면 sync, 아니면 상태 글리프다. 확인되지 않은
        // 상태(`?`)는 강조색 대신 비활성 계열로 흐리게 그려 정상처럼 보이지
        // 않게 한다.
        if (context.codicon_typeface != nullptr)
        {
            const rect_f icon_slot { box.x + padding, box.y + padding, 16.0f * scale, 16.0f * scale };
            const SkFont glyph_font { icon_font(context, 14.0f) };
            // 기능 실행도 진행 표시다. `busy`는 세우지 않는다 (B8, 2.3.5).
            const bool spinning { card_.busy || card_.function_running };
            const bool undetermined { spinning == false && card_.status.undetermined };
            SkPaint icon_paint { solid_paint(undetermined ? context.palette.primary_foreground : state_accent(context.palette, card_.state)) };
            if (undetermined)
                icon_paint.setAlphaf(0.45f);
            if (spinning)
                draw_spinning_glyph(context.canvas, codicons::icon_sync, icon_slot, glyph_font, icon_paint, context.now);
            else
                draw_centered_glyph(context.canvas, codicon_for_name(card_.status.codicon), icon_slot, glyph_font, icon_paint);
        }

        // 글자가 버튼을 침범하지 않도록 arrange가 정한 한계 안에서만 그린다.
        const float text_width { text_limit_ - text_x };
        draw_text_line(context, card_.display_name, { text_x, title_top, text_width, title_height }, foreground, { .size = 14.0f });
        // 경로는 앞을 줄여 저장소 폴더 이름(끝)을 남긴다 (설계 D6).
        draw_text_line(context, card_.path, { text_x, path_top, text_width, path_height }, dim_foreground, { .size = 11.0f, .elide = luil::text_elide::start });

        // 세 번째 줄은 조각(chip)으로 그린다. 브랜치·리비전·상태·작업 트리를 같은
        // 폰트의 한 줄로 이어 붙이면 구분이 되지 않는다는 지적을 반영한 것이다.
        draw_status_row(context, { text_x, line_3_top, text_width, layout_card_status_height() * scale });

        // 기능 묶음과 내장 버튼 사이의 12px 구분선이다. 가운데 1px 선을 버튼보다 짧게 긋는다 (2.3).
        if (separator_x_.has_value())
        {
            const float line_height { layout_card_function_separator_height * scale };
            const float line_top { box.y + (box.height - line_height) / 2.0f };
            const SkRect line { SkRect::MakeXYWH(*separator_x_ - 0.5f * scale, line_top, 1.0f * scale, line_height) };
            context.canvas.drawRect(line, solid_paint(context.palette.divider));
        }

        draw_children(context, interaction);
    }

    std::optional<std::chrono::steady_clock::time_point> card_element::next_update(const update_context& context, const interaction_snapshot& interaction) const
    {
        static_cast<void>(interaction);
        // 계속 도는 표시다. 지금 시각을 돌려주면 platform이 짧은 주기로 다시 그린다.
        if (card_.busy || card_.function_running)
            return context.now;
        return std::nullopt;
    }

    access_info card_element::accessibility() const
    {
        // 상태 조각과 같은 순서로 잇는다. 진행 중이면 상태 글보다 그 사실이 먼저다.
        std::u8string name { card_.display_name };
        const auto append = [&name](const std::u8string_view part) {
            if (part.empty())
                return;
            name += u8", ";
            name += part;
        };
        append(card_.path);
        if (card_.busy)
            append(u8"진행 중");
        append(card_.reference);
        if (card_.revision.empty() == false)
            append(revision_display_text(card_.kind, card_.revision));
        append(card_.status.tooltip);
        append(card_.working_tree_text);
        if (card_.enabled == false)
            append(u8"제외됨");
        // 카드 열에 보이지 않는 기능도 있으므로 개수를 알린다 (function-buttons-design 2.3.4).
        if (card_.functions.empty() == false)
        {
            const std::string count { std::to_string(card_.functions.size()) };
            append(u8"기능 " + std::u8string { count.begin(), count.end() } + u8"개");
        }
        return { .role = access_role::list_item, .name = std::move(name), .selected = card_.selected };
    }

    void card_element::draw_status_row(draw_context& context, const rect_f& row) const
    {
        if (row.width <= 0.0f || row.height <= 0.0f)
            return;

        const float scale { context.scale > 0.0f ? context.scale : 1.0f };
        const ui_color accent { state_accent(context.palette, card_.state) };
        const ui_color neutral { context.palette.primary_foreground };

        std::vector<card_chip> chips {};
        // 출력 수집 기능이 실행 중이면 맨 앞에 `▶ 빌드 00:12`다 (function-buttons-design 2.3.5). 경과 시간은
        // started부터 draw 시각까지라 연속 다시 그리기(`next_update`)와 함께 흐른다. started 전이면 이름뿐이다.
        if (card_.function_running)
        {
            std::u8string text { card_.running_function_label };
            if (card_.running_function_started_at.has_value())
                text += u8" " + format_elapsed_time(context.now - *card_.running_function_started_at);
            chips.push_back({ codicons::icon_play, std::move(text), context.palette.accent, context.palette.accent_foreground });
        }
        if (card_.reference.empty() == false)
        {
            // SVN의 참조는 URL이라 branch 아이콘 대신 link 아이콘을 쓴다.
            const char32_t glyph { card_.kind == repository_kind::subversion ? codicons::icon_link : codicons::icon_git_branch };
            chips.push_back({ glyph, card_.reference, with_alpha(neutral, 0.12f), neutral });
        }
        if (card_.revision.empty() == false)
            chips.push_back({ codicons::icon_git_commit, std::u8string { revision_display_text(card_.kind, card_.revision) }, with_alpha(neutral, 0.12f), with_alpha(neutral, 0.75f) });
        if (card_.status.tooltip.empty() == false)
        {
            // 확인되지 않은 상태(`?`)는 상태 강조색이 아니라 비활성 계열이다.
            // 제대로 조회되지 않았다는 사실이 색으로도 드러난다.
            if (card_.status.undetermined)
                chips.push_back({ codicon_for_name(card_.status.codicon), card_.status.tooltip, with_alpha(neutral, 0.10f), with_alpha(neutral, 0.5f) });
            else
                chips.push_back({ codicon_for_name(card_.status.codicon), card_.status.tooltip, with_alpha(accent, 0.18f), accent });
        }
        if (card_.working_tree_text.empty() == false)
        {
            const ui_color tree_color { context.palette.warning_accent };
            chips.push_back({ codicons::icon_edit, card_.working_tree_text, with_alpha(tree_color, 0.18f), tree_color });
        }

        const SkFont chip_font { ui_font(context, 10.5f) };
        const SkFont glyph_font { icon_font(context, 10.0f) };
        const float padding { 5.0f * scale };
        const float icon_width { context.codicon_typeface != nullptr ? 11.0f * scale : 0.0f };
        const float gap { 4.0f * scale };
        const float radius { 3.0f * scale };

        float left { row.x };
        const float limit { row.x + row.width };
        for (const card_chip& chip : chips)
        {
            const float fixed { padding * 2.0f + (icon_width > 0.0f && chip.glyph != 0 ? icon_width + gap * 0.5f : 0.0f) };
            const float available { limit - left - fixed };
            // 글자가 한 자도 들어가지 않는 조각은 그리지 않는다. 뒤 조각도 마찬가지다.
            const std::u8string text { elide_text(chip.text, available, chip_font) };
            if (text.empty())
                break;

            const float text_width { measure_text(text, chip_font) };
            const float width { text_width + fixed };
            const SkRect box { SkRect::MakeXYWH(left, row.y, width, row.height) };
            if (chip.background != 0)
                context.canvas.drawRRect(SkRRect::MakeRectXY(box, radius, radius), solid_paint(chip.background));

            const SkPaint foreground { solid_paint(chip.foreground) };
            float text_left { left + padding };
            if (icon_width > 0.0f && chip.glyph != 0)
            {
                const rect_f icon_slot { left + padding, row.y, icon_width, row.height };
                draw_centered_glyph(context.canvas, chip.glyph, icon_slot, glyph_font, foreground);
                text_left += icon_width + gap * 0.5f;
            }
            draw_text_line(context, text, { text_left, row.y, limit - text_left, row.height }, foreground, { .size = 10.5f });

            left += width + gap;
            if (left >= limit)
                break;
        }
    }
} // namespace gitman::ui
